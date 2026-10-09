# qmltermwidget test and hardening suite

Everything here runs on the build host against a host Qt 6, apart from the
cross builds in `build-matrix.sh`, which use a LuneOS OE tree. The library
parses whatever the program in the terminal prints, so most of the suite is
about input nobody should have to trust.

## Unit tests (`tests/unit/`)

Regression tests for the audit on the `herrie/audit` branch. Each one pins a
fix and reproduces the original failure, directly or under the sanitizer
builds below:

- **Vt**: the flag-emoji use-after-free, the 1024-cell copy overflow, the
  `CSI 8 t` resize to 40000x40000, a screen shorter than its window, and
  out-of-range line ranges.
- **HistoryBuffer**, **HistoryFile**, **HistorySearch**: zero-line buffers,
  shrinking keeping the newest lines, reads past the end of the history
  file, and searches started outside the text or matching more than 10000
  lines back.
- **ColorScheme**, **Keytab**: invalid colours (a use-after-free in the
  warning), dotted scheme names (endless recursion), names that are paths,
  the fallback keytab leak, an unset `KB_LAYOUT_DIR`.
- **Display**: `session: null`, a destroyed session, key releases typed
  twice, paste breaking out of bracketed paste, the trailing-newline flag,
  negative `lineSpacing`, extreme mouse positions, scroll bar range
  notification, code points beyond U+FFFF.
- **Session**, **Pty**, **ProcessInfo**: keys reaching the previous shell
  after a session switch, `changeDir()` injection, OSC 11 aborting debug
  builds, the `TERM` fallback, deleting a running process, a pty device
  after `close()`, process names with brackets.

The runner in `check.h` is deliberately tiny: gtest is not in the LuneOS
sysroot. The binary is built with asserts on, since several of the fixes
were `Q_ASSERT`s that terminal output could fire.

    qmake6 tests/tests.pro && make
    ./qmltermwidget-tests                 # everything
    ./qmltermwidget-tests Display         # one suite
    ./qmltermwidget-tests Vt.resizeRequestsAreCapped

The Session, Pty and ProcessInfo tests start `/bin/sh`, `/bin/true` and
`/bin/sleep` on real ptys and skip (never pass) where those are missing.

## Fuzzing (`tests/fuzz/`)

`fuzz-vt` feeds the parser, the screen and a `TerminalDisplay` laying it out
what a hostile program could print: sequences with huge parameters, window
resizes, long OSC strings, regional indicators and combining marks, any code
point and any byte. A seed is reproducible:

    fuzz-vt --seed=N [--rounds=R]
    fuzz-vt tests/data/*.bin              # replay the corpus

`tests/data/` holds inputs that crashed the library before the audit.

## Hardening (`tests/hardening/`)

- `run-sanitizers.sh`: unit tests, corpus and fuzz seeds under ASan+UBSan
  with GCC and with Clang (every report fatal), and the unit tests under
  TSan. Qt is not instrumented for TSan, so only reports with a frame in
  this library fail.
- `run-valgrind.sh`: unit tests and corpus under memcheck, failing on any
  error or definite leak. The QRegularExpression JIT is switched off for
  it, and `valgrind.supp` holds two of Qt's vectorised string overreads.
- `run-fuzz.sh`: a seed range, in parallel; `run-sanitizers.sh` calls it.

Every heavy step runs in a memory-capped systemd scope (`MEMORY_MAX`,
default 16G) so a runaway compile or seed fails the run instead of freezing
the machine. `BUILD_ROOT` (default `./build-hardening`) and `JOBS` (default
8) can be set too.

## Static analysis (`tests/static-analysis.sh`)

gcc `-fanalyzer` at -O0 and -O2, the clang analyzer with alpha checkers,
clang-tidy, cppcheck and flawfinder over `lib/` and `src/`. The code comes
from Konsole and carries hundreds of older findings, so this is a baseline
gate: `static-analysis.baseline` lists the accepted ones, anything new fails,
and `--update-baseline` shrinks it once findings are fixed. sparse (C only)
and Coccinelle (no usable C++ support for this code) do not apply.

## Build matrix (`tests/build-matrix.sh`)

GCC and Clang for armv7, aarch64 and x86-64 from an OE tree, plus the host,
with `-Wall -Wextra`; any warning in this library's lines fails it.

    tests/build-matrix.sh /path/to/oe/tmp
