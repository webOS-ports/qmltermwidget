#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Static-analysis gate for the library (lib/ and src/). Runs:
#
#   gcc -fanalyzer    at -O0 and -O2: use after free, double free, leaks,
#                     NULL dereference, uninitialised reads
#   clang analyzer    the same from a second engine, with the alpha checkers
#                     for unix/security/core
#   clang-tidy        clang-analyzer-*, bugprone-*, cert-*, concurrency-*,
#                     performance-*
#   cppcheck          all checks, exhaustive, inconclusive included
#   flawfinder        race-prone calls (TOCTOU) and unsafe functions
#
# Not run, because they cannot apply here: sparse parses C only, and
# Coccinelle's C++ support does not cover this code base's Qt idioms.
#
# The code descends from Konsole and carries hundreds of older findings, so
# the gate is a baseline: tests/static-analysis.baseline lists the accepted
# ones (file, tool, check and message; line numbers left out so unrelated
# edits do not disturb it). Any finding not in it fails the run; findings
# that went away are listed so the baseline can shrink with
#
#   tests/static-analysis.sh --update-baseline
#
# BUILD_ROOT, MEMORY_MAX and QMAKE as in tests/hardening/common.sh. A tool
# that is not installed is reported and fails the run, it is not passed.
set -u
TESTS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$TESTS_DIR/hardening/common.sh"

BASELINE="$TESTS_DIR/static-analysis.baseline"
out="$BUILD_ROOT/static-analysis"
update=0
[ "${1:-}" = "--update-baseline" ] && update=1

rm -rf "$out"
mkdir -p "$out/build"

missing=""
for tool in g++ clang++ clang-tidy cppcheck flawfinder python3; do
	command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
	echo "not installed:$missing"
	exit 1
fi

# The compile commands qmake would use, one per source file
(cd "$out/build" && "$QMAKE" "$SRC_DIR/qmltermwidget.pro" >qmake.log 2>&1 &&
	make -n -B 2>/dev/null) | grep -E '^g\+\+ (.* )?-c ' >"$out/commands" || {
	echo "qmake failed, see $out/build/qmake.log"
	exit 1
}
# moc output is generated code; only our sources are analysed
grep -v ' moc_' "$out/commands" >"$out/sources" || true

findings="$out/findings.raw"
: >"$findings"
incomplete=""

# analyse <tool label> <file> <command>: runs one analysis in the memory
# cap, keeps its diagnostics, and records the file as not analysed if the
# command failed - an analyzer killed for memory reports nothing, which
# must not read as "no findings"
analyse() {
	label=$1 file=$2 cmd=$3
	(cd "$out/build" && capped sh -c "$cmd" >"$out/last.log" 2>&1)
	status=$?
	sed "s/^/$label\t/" "$out/last.log" >>"$findings"
	if [ $status -ne 0 ]; then
		incomplete="$incomplete
  $label: $file (exit $status)"
	fi
}

# One compile at a time: -fanalyzer takes many GB for TerminalDisplay.cpp
for opt in -O0 -O2; do
	echo "== gcc -fanalyzer $opt"
	while read -r cmd; do
		gcccmd=$(printf '%s' "$cmd" | sed -e 's/ -o [^ ]*\.o / /')
		analyse "gcc-fanalyzer$opt" "${cmd##* }" "$gcccmd -fanalyzer $opt -o /dev/null"
	done <"$out/sources"
done

echo "== clang analyzer"
while read -r cmd; do
	clangcmd=$(printf '%s' "$cmd" | sed -e 's/^g++ /clang++ --analyze /' -e 's/ -o [^ ]*\.o / /')
	# some alpha checkers refuse to run without the aggressive simplification
	analyse clang-analyzer "${cmd##* }" "$clangcmd -Xanalyzer -analyzer-output=text \
		-Xanalyzer -analyzer-config -Xanalyzer aggressive-binary-operation-simplification=true \
		-Xanalyzer -analyzer-checker=core,unix,deadcode,security,cplusplus,nullability \
		-Xanalyzer -analyzer-checker=alpha.unix,alpha.security,alpha.core.PointerArithm,alpha.core.CastSize,alpha.cplusplus \
		-o /dev/null"
done <"$out/sources"

echo "== clang-tidy"
while read -r cmd; do
	file=$(printf '%s' "$cmd" | awk '{print $NF}')
	flags=$(printf '%s' "$cmd" | sed -e 's/^g++ //' -e 's/ -c / /' -e 's/ -o [^ ]*\.o / /' -e "s# $file\$##")
	analyse clang-tidy "$file" "clang-tidy --quiet \
		-checks='-*,clang-analyzer-*,bugprone-*,cert-*,concurrency-*,performance-*,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions,-cert-err58-cpp' \
		$file -- $flags"
done <"$out/sources"

echo "== cppcheck"
capped cppcheck --enable=all --check-level=exhaustive --inconclusive --library=qt --std=c++17 \
	-I "$SRC_DIR/lib" -I "$SRC_DIR/src" -DHAVE_POSIX_OPENPT -DHAVE_SYS_TIME_H -D__linux__ -DQ_OS_LINUX \
	--suppress=missingIncludeSystem --suppress=missingInclude --suppress=unmatchedSuppression \
	--suppress=checkersReport --suppress=unusedFunction \
	--template='{file}:{line}:{column}: {severity}: {message} [{id}]' \
	-j "$JOBS" "$SRC_DIR"/lib/*.cpp "$SRC_DIR"/src/*.cpp 2>&1 >/dev/null |
	grep -E '^[^ ]+:[0-9]+:[0-9]+: ' | sed "s/^/cppcheck\t/" >>"$findings"

echo "== flawfinder"
flawfinder --minlevel=1 --singleline --dataonly --quiet "$SRC_DIR/lib" "$SRC_DIR/src" |
	grep -E '^[^ ]+:[0-9]+:' | sed "s/^/flawfinder\t/" >>"$findings"

# tool \t file:line:col: severity: message [check]  ->  tool|file|check|message
python3 - "$SRC_DIR" "$findings" "$out/findings" <<'EOF'
import re, sys
src, raw, cooked = sys.argv[1], sys.argv[2], sys.argv[3]
keys = []
for line in open(raw, errors='replace'):
    tool, _, rest = line.rstrip('\n').partition('\t')
    m = re.match(r'(?:\.\./)*(.+?):(\d+):(?:\d+:)?\s*(.*)$', rest)
    if not m:
        continue
    if tool.startswith(('gcc', 'clang')) and not re.match(r'(warning|error): .*\[', m.group(3)):
        continue                      # notes and the path steps of a finding
    path = m.group(1)
    path = path[path.find(src) + len(src) + 1:] if src in path else re.sub(r'^(\.\./)+', '', path)
    if not re.match(r'(lib|src)/', path):
        continue                      # findings in Qt's or the system's headers
    msg = m.group(3)
    check = re.findall(r'\[([^\]]+)\]\s*$', msg)
    check = check[0] if check else ''
    msg = re.sub(r'\s*\[[^\]]+\]\s*$', '', msg)
    msg = re.sub(r'\d+', 'N', msg)    # line numbers and sizes in messages
    keys.append(f"{tool}|{path}|{check}|{msg}")
open(cooked, 'w').write('\n'.join(sorted(keys)) + ('\n' if keys else ''))
EOF

if [ -n "$incomplete" ]; then
	echo "NOT ANALYSED (killed, or the analyzer failed; raise MEMORY_MAX if it ran out):$incomplete"
	exit 1
fi

if [ $update -eq 1 ]; then
	cp "$out/findings" "$BASELINE"
	echo "baseline updated: $(wc -l <"$BASELINE") findings"
	exit 0
fi

# sort, comm and the baseline (written by Python) agree on byte order
new=$(LC_ALL=C sort "$out/findings" | LC_ALL=C comm -23 - "$BASELINE")
gone=$(LC_ALL=C sort "$out/findings" | LC_ALL=C comm -13 - "$BASELINE")

echo "== $(wc -l <"$out/findings") findings, $(wc -l <"$BASELINE") in the baseline"
if [ -n "$gone" ]; then
	echo "no longer found (shrink the baseline with --update-baseline):"
	printf '%s\n' "$gone" | sed 's/^/  /'
fi
if [ -n "$new" ]; then
	echo "NEW findings (details in $out/findings.raw):"
	printf '%s\n' "$new" | sed 's/^/  /'
	exit 1
fi
echo "== static analysis: no new findings"
