# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Unit tests for the library, linked against its sources directly. Built
# with asserts on: several of the fixes they pin were Q_ASSERTs that fired
# on input from the terminal.
#
#   qmake6 tests/tests.pro && make && ./qmltermwidget-tests [suite]
#
# Sanitizers come from qmake's own switches, as the hardening scripts use:
#   qmake6 tests/tests.pro CONFIG+=sanitizer CONFIG+=sanitize_address CONFIG+=sanitize_undefined

TEMPLATE = app
TARGET = qmltermwidget-tests
QT += qml quick widgets
CONFIG += c++17 console debug
CONFIG -= app_bundle release

include(../lib.pri)

INCLUDEPATH += $$PWD/../lib $$PWD/unit
DEFINES += HAVE_POSIX_OPENPT HAVE_SYS_TIME_H
DEFINES += TESTS_SOURCE_DIR=\\\"$$PWD\\\"

QMAKE_CXXFLAGS += -Wall -Wextra

HEADERS += $$PWD/unit/check.h
SOURCES += $$files($$PWD/unit/*.cpp)
