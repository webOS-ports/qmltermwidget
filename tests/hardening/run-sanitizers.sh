#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds the unit tests and the fuzzer with sanitizers, with GCC and with
# Clang, and runs them:
#
#   ASan+UBSan  unit tests, the fuzz corpus and FUZZ_SEEDS seeds; every
#               report is fatal (-fno-sanitize-recover). GCC's UBSan leaves
#               out float-cast-overflow, so it is asked for by name.
#   TSan        unit tests, Clang. Qt itself is not instrumented, so its own
#               threads report races TSan cannot see the locking of; only
#               reports with a frame in this library fail the run.
#
#   tests/hardening/run-sanitizers.sh
#
# BUILD_ROOT, JOBS, MEMORY_MAX and FUZZ_SEEDS (default 100) override the
# defaults. Needs qmake6, g++, clang++ and a Qt 6 built for the host.
set -u
TESTS_DIR=$(cd "$(dirname "$0")/.." && pwd)
. "$TESTS_DIR/hardening/common.sh"

FUZZ_SEEDS=${FUZZ_SEEDS:-100}
fail=0

asan_flags="-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer"
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}
export QT_QPA_PLATFORM=offscreen

for compiler in gcc clang; do
	spec=linux-g++
	[ "$compiler" = clang ] && spec=linux-clang
	out="$BUILD_ROOT/asan-$compiler"

	echo "== ASan+UBSan, $compiler"
	if ! build "$out/tests" "$TESTS_DIR/tests.pro" -spec "$spec" \
		"QMAKE_CXXFLAGS+=$asan_flags" "QMAKE_LFLAGS+=$asan_flags" ||
	   ! build "$out/fuzz" "$TESTS_DIR/fuzz/fuzz.pro" -spec "$spec" \
		"QMAKE_CXXFLAGS+=$asan_flags" "QMAKE_LFLAGS+=$asan_flags"; then
		fail=1
		continue
	fi

	if ! capped "$out/tests/qmltermwidget-tests"; then
		echo "unit tests failed under ASan+UBSan ($compiler)"
		fail=1
	fi

	if ! capped "$out/fuzz/fuzz-vt" "$TESTS_DIR"/data/*.bin; then
		echo "fuzz corpus failed under ASan+UBSan ($compiler)"
		fail=1
	fi

	if ! FUZZ_BIN="$out/fuzz/fuzz-vt" "$TESTS_DIR/hardening/run-fuzz.sh" 1 "$FUZZ_SEEDS"; then
		fail=1
	fi
done

echo "== TSan, clang"
tsan_flags="-fsanitize=thread -fno-omit-frame-pointer"
out="$BUILD_ROOT/tsan-clang"
if build "$out" "$TESTS_DIR/tests.pro" -spec linux-clang \
	"QMAKE_CXXFLAGS+=$tsan_flags" "QMAKE_LFLAGS+=$tsan_flags"; then
	TSAN_OPTIONS="halt_on_error=0 report_signal_unsafe=0" \
		capped "$out/qmltermwidget-tests" >"$out/tsan.log" 2>&1
	status=$?

	# A report is ours when one of its frames is in this source tree
	ours=$(awk -v src="$SRC_DIR/" '
		/^==================$/ { if (inrep && hit) n++; inrep = !inrep; hit = 0; next }
		inrep && index($0, src) { hit = 1 }
		END { print n + 0 }' "$out/tsan.log")
	total=$(grep -c "WARNING: ThreadSanitizer" "$out/tsan.log")
	echo "TSan: $total reports, $ours with frames in this library"

	if [ "$ours" -ne 0 ] || ! grep -q " 0 failed" "$out/tsan.log"; then
		[ "$status" -ne 0 ] && echo "unit tests exited with $status under TSan"
		echo "see $out/tsan.log"
		fail=1
	fi
else
	fail=1
fi

[ $fail -eq 0 ] && echo "== sanitizers: clean" || echo "== sanitizers: FAILED"
exit $fail
