# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The escape-sequence fuzzer; see fuzz-vt.cpp. Build it with sanitizers:
#   qmake6 tests/fuzz/fuzz.pro CONFIG+=sanitizer CONFIG+=sanitize_address CONFIG+=sanitize_undefined

TEMPLATE = app
TARGET = fuzz-vt
QT += qml quick widgets
CONFIG += c++17 console debug
CONFIG -= app_bundle release

include(../../lib.pri)

INCLUDEPATH += $$PWD/../../lib
DEFINES += HAVE_POSIX_OPENPT HAVE_SYS_TIME_H
QMAKE_CXXFLAGS += -Wall -Wextra

SOURCES += $$PWD/fuzz-vt.cpp
