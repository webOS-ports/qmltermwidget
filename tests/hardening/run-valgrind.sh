#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs the unit tests and the fuzz corpus under valgrind memcheck, and fails
# on any memory error or definite leak. Builds them without sanitizers
# first.
#
#   tests/hardening/run-valgrind.sh
#
# On a LuneOS sysroot instead of the host Qt, point QMAKE at that sysroot's
# qmake and VALGRIND_LOADER at its ld.so with --library-path, e.g.
#   VALGRIND_LOADER="$SYSROOT/usr/lib/ld-linux-x86-64.so.2 --library-path $SYSROOT/usr/lib"
#
# QRegularExpression JIT-compiles its patterns, and memcheck cannot follow
# generated code (every match is reported as a jump on uninitialised
# values), so the JIT is switched off. valgrind.supp holds the few reports
# that are Qt's vectorised string code reading past a block's end.
set -u
TESTS_DIR=$(cd "$(dirname "$0")/.." && pwd)
. "$TESTS_DIR/hardening/common.sh"

out="$BUILD_ROOT/plain"
build "$out/tests" "$TESTS_DIR/tests.pro" || exit 1
build "$out/fuzz" "$TESTS_DIR/fuzz/fuzz.pro" || exit 1

export QT_QPA_PLATFORM=offscreen QT_ENABLE_REGEXP_JIT=0
fail=0

run() {
	name=$1; shift
	echo "== valgrind: $name"
	# shellcheck disable=SC2086
	if ! capped valgrind --error-exitcode=42 --error-limit=no --num-callers=40 \
		--suppressions="$TESTS_DIR/hardening/valgrind.supp" \
		--leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite \
		${VALGRIND_LOADER:-} "$@" >"$out/valgrind-$name.log" 2>&1; then
		grep -E "ERROR SUMMARY|definitely lost|FAIL" "$out/valgrind-$name.log"
		echo "see $out/valgrind-$name.log"
		fail=1
	else
		grep "ERROR SUMMARY" "$out/valgrind-$name.log"
	fi
}

run unit "$out/tests/qmltermwidget-tests"
run fuzz-corpus "$out/fuzz/fuzz-vt" "$TESTS_DIR"/data/*.bin --seed=1 --rounds=500

[ $fail -eq 0 ] && echo "== valgrind: clean" || echo "== valgrind: FAILED"
exit $fail
