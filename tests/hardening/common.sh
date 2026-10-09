# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Sourced by the hardening scripts, which set TESTS_DIR (the tests/
# directory) first.

TESTS_DIR=${TESTS_DIR:?set TESTS_DIR before sourcing common.sh}
SRC_DIR=$(cd "$TESTS_DIR/.." && pwd)
BUILD_ROOT=${BUILD_ROOT:-$PWD/build-hardening}
JOBS=${JOBS:-8}
# Ceiling for each heavy step. gcc -fanalyzer needs many GB per file, and a
# sanitizer build or a fuzz seed gone wrong must fail the run, not lock up
# the machine (both have happened while this suite was being written).
MEMORY_MAX=${MEMORY_MAX:-16G}
QMAKE=${QMAKE:-qmake6}
# Resolved now: the cross builds swap PATH for a recipe's, which has neither
SYSTEMD_RUN=$(command -v systemd-run 2>/dev/null || true)

# Runs a command inside a memory-capped systemd scope when there is a user
# session to make one in; otherwise as is, and says so.
capped() {
	if [ -n "$SYSTEMD_RUN" ] &&
	   "$SYSTEMD_RUN" --user --scope -q true >/dev/null 2>&1; then
		"$SYSTEMD_RUN" --user --scope -q -p MemoryMax="$MEMORY_MAX" -p MemorySwapMax=0 "$@"
	else
		[ -n "${CAPPED_WARNED:-}" ] || echo "note: no systemd user session, running without a memory cap" >&2
		CAPPED_WARNED=1
		"$@"
	fi
}

# build <out-dir> <project.pro> <qmake args...>
build() {
	# local: the callers keep their own $out
	local dir=$1 pro=$2
	shift 2
	mkdir -p "$dir"
	(cd "$dir" && "$QMAKE" "$pro" "$@" >qmake.log 2>&1 &&
		capped make -j"$JOBS" >make.log 2>&1) || {
		echo "build failed in $dir:" >&2
		grep -m20 -E "error|Error" "$dir/make.log" "$dir/qmake.log" >&2
		return 1
	}
	if grep -q "warning:" "$dir/make.log"; then
		echo "warnings in $dir:" >&2
		grep "warning:" "$dir/make.log" | sort -u >&2
		return 1
	fi
}
