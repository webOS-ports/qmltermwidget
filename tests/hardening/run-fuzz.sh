#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs fuzz-vt over a range of seeds, JOBS at a time, and fails if any seed
# crashes, hits an assertion or a sanitizer report. Seeds are reproducible:
# a failing one is rerun on its own with `fuzz-vt --seed=N`.
#
#   FUZZ_BIN=<build>/fuzz-vt tests/hardening/run-fuzz.sh [first] [last]
#
# Build fuzz-vt with sanitizers first (run-sanitizers.sh does). Logs of
# failing seeds are kept in $BUILD_ROOT/fuzz-logs.
set -u
TESTS_DIR=$(cd "$(dirname "$0")/.." && pwd)
. "$TESTS_DIR/hardening/common.sh"

FUZZ_BIN=${FUZZ_BIN:?set FUZZ_BIN to a fuzz-vt binary}
first=${1:-1}
last=${2:-100}
logs="$BUILD_ROOT/fuzz-logs"
mkdir -p "$logs"
rm -f "$logs"/seed-*.fail

export QT_QPA_PLATFORM=offscreen
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}
export FUZZ_BIN logs

echo "== fuzz seeds $first..$last, $JOBS at a time ($FUZZ_BIN)"
capped sh -c '
	seq "$1" "$2" | xargs -P "$3" -I{} sh -c "
		timeout 600 \"\$FUZZ_BIN\" --seed={} >\"\$logs/seed-{}.log\" 2>&1 ||
			mv \"\$logs/seed-{}.log\" \"\$logs/seed-{}.fail\"
		rm -f \"\$logs/seed-{}.log\""
' fuzz "$first" "$last" "$JOBS"

failed=$(ls "$logs"/seed-*.fail 2>/dev/null | wc -l)
if [ "$failed" -ne 0 ]; then
	for f in "$logs"/seed-*.fail; do
		echo "$(basename "$f" .fail): $(grep -m1 -E 'ASSERT|SUMMARY|runtime error' "$f")"
	done
	echo "fuzz: $failed of $((last - first + 1)) seeds failed, logs in $logs"
	exit 1
fi

echo "fuzz: $((last - first + 1)) seeds clean"
