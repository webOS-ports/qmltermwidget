#!/bin/bash
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds the plugin with GCC and with Clang for every LuneOS architecture,
# plus the host, with -Wall -Wextra, and fails on any build error or any
# warning in this library's own lines (warnings from Qt's or the system's
# headers are listed, not counted).
#
#   tests/build-matrix.sh <OE tmp dir>
#
# e.g. tests/build-matrix.sh ~/oe/webos-ports/tmp
#
# The cross builds reuse the environment of an existing qmake-based Qt 6
# recipe workdir for each architecture (luneos-components, say): its
# compiler, sysroot and qmake. ARCHES overrides the architectures; armv7 is
# in the default list on purpose, as it is the 32-bit one. Clang is the
# host's clang++ aimed at each target, linking with the target's binutils.
set -u
TESTS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$TESTS_DIR/hardening/common.sh"

OE_TMP=${1:?usage: $0 <OE tmp dir>}
ARCHES=${ARCHES:-cortexa8t2hf-neon-halium-webos-linux-gnueabi aarch64-webos-linux corei7-64-webos-linux}
out="$BUILD_ROOT/build-matrix"
# The host's Clang, found before a recipe's environment replaces PATH
CLANGXX=$(command -v clang++) || { echo "clang++ not installed"; exit 1; }
J=${JOBS}
fail=0

# A workdir of a qmake (qt6-qmake) recipe for this arch with a Qt 6 sysroot
find_workdir() {
	for d in "$OE_TMP"/work/"$1"/*/*/; do
		[ -e "$d/recipe-sysroot/usr/lib/libQt6Quick.so" ] &&
		[ -e "$d/recipe-sysroot/usr/lib/libQt6Widgets.so" ] &&
		[ -x "$d/recipe-sysroot-native/usr/bin/qmake" ] &&
		[ -e "$d/qt.conf" ] &&
		grep -q 'qmake' "$d/temp/run.do_configure" 2>/dev/null &&
		{ echo "${d%/}"; return 0; }
	done
	return 1
}

# build_one <name> <compiler> [<workdir>]
build_one() {
	name=$1 cc=$2 wd=${3:-}
	dir="$out/$name-$cc"
	rm -rf "$dir"
	mkdir -p "$dir"
	makevars=()

	if [ -z "$wd" ]; then
		spec=linux-g++
		[ "$cc" = clang ] && spec=linux-clang
		(cd "$dir" && "$QMAKE" -spec "$spec" "$SRC_DIR/qmltermwidget.pro" \
			"QMAKE_CXXFLAGS+=-Wall -Wextra" >qmake.log 2>&1) || return 1
	else
		# The recipe's own environment: compiler, flags, PATH, qt.conf
		grep '^export ' "$wd/temp/run.do_configure" |
			grep -vE '^export +(CCACHE|HOME|LOGNAME|USER|SHELL|TERM|PWD|BB)[A-Z_]*=' |
			grep -vE '^export *$' | sed 's/ccache //g' >"$dir/env.sh"
		(
			. "$dir/env.sh"
			# shellcheck disable=SC2086
			set -- $OE_QMAKE_CXX
			triple=${1%-g++}
			if [ "$cc" = clang ]; then
				shift
				sysroot="$wd/recipe-sysroot"
				gccver=$(ls "$sysroot/usr/lib/$triple")
				flags=$(echo "$*" | sed -e 's/-fcanon-prefix-map//g' -e 's/-Wl,[^ ]*//g' -e 's/-fmacro-prefix-map=[^ ]*//g')
				cl="$CLANGXX --target=$triple $flags --gcc-install-dir=$sysroot/usr/lib/$triple/$gccver"
				cl="$cl -stdlib++-isystem $sysroot/usr/include/c++/$gccver -stdlib++-isystem $sysroot/usr/include/c++/$gccver/$triple"
				# qmake cannot probe Clang's default paths: it configures with
				# GCC, and make gets Clang
				printf '%s\n' "CXX=$cl" "LINK=$cl --ld-path=$wd/recipe-sysroot-native/usr/bin/$triple/$triple-ld" >"$dir/makevars"
				OE_QMAKE_CXXFLAGS=$(echo "$OE_QMAKE_CXXFLAGS" | sed 's/-fcanon-prefix-map//g')
				export OE_QMAKE_CXXFLAGS
			fi
			cd "$dir" && "$wd/recipe-sysroot-native/usr/bin/qmake" -qtconf "$wd/qt.conf" \
				-makefile "$SRC_DIR/qmltermwidget.pro" "QMAKE_CXXFLAGS+=-Wall -Wextra" >qmake.log 2>&1
		) || return 1
		if [ -e "$dir/makevars" ]; then
			mapfile -t makevars <"$dir/makevars"
		fi
	fi

	(
		[ -n "$wd" ] && . "$dir/env.sh"
		cd "$dir" && capped make -j"$J" "${makevars[@]}" >make.log 2>&1
	) || return 1
}

for cc in gcc clang; do
	targets="host"
	for arch in $ARCHES; do targets="$targets $arch"; done

	for target in $targets; do
		wd=""
		if [ "$target" != host ]; then
			if ! wd=$(find_workdir "$target"); then
				echo "$target/$cc: no qmake Qt 6 recipe workdir under $OE_TMP/work/$target"
				fail=1
				continue
			fi
		fi

		if ! build_one "$target" "$cc" "$wd"; then
			echo "$target/$cc: BUILD FAILED, see $out/$target-$cc"
			fail=1
			continue
		fi

		ours=$(grep -E 'warning:' "$out/$target-$cc/make.log" | grep -E "$SRC_DIR/(lib|src)/|^\.\./.*(lib|src)/" | sort -u)
		theirs=$(grep -cE 'warning:' "$out/$target-$cc/make.log")
		if [ -n "$ours" ]; then
			echo "$target/$cc: warnings in this library:"
			printf '%s\n' "$ours" | sed 's/^/  /'
			fail=1
		else
			echo "$target/$cc: clean ($theirs warnings in total, none in this library)"
		fi
	done
done

[ $fail -eq 0 ] && echo "== build matrix: clean" || echo "== build matrix: FAILED"
exit $fail
