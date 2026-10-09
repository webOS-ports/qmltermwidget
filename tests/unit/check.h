// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef QMLTERMWIDGET_TEST_CHECK_H
#define QMLTERMWIDGET_TEST_CHECK_H

/*
 * A test runner small enough to cross-compile with the library and run on a
 * phone. gtest is not in the LuneOS sysroot, and QtTest would tie the suite
 * to a Qt module the images do not ship.
 *
 * Register with TEST(suite, name) { ... } and assert with CHECK/CHECK_EQ.
 * Every failure is reported; the run does not stop at the first one. Pass
 * a suite ("Vt") or one case ("Vt.resizeRequestsAreCapped") to run only
 * that. A test
 * that cannot run here (no shell, no plugin build) calls SKIP(), which is
 * reported as such and never counted as a pass.
 */

#include <QByteArray>
#include <QString>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace testing
{

struct TestCase
{
    const char *suite;
    const char *name;
    void (*fn)();
};

std::vector<TestCase>& registry();
int runAll(const char *suiteFilter);

//! Set by the CHECK macros and SKIP; read by the runner after each case.
extern int g_failuresInCase;
extern bool g_skippedCase;

void reportFailure(const char *file, int line, const std::string &what);
void reportSkip(const std::string &why);

//! Runs the event loop until cond() holds or timeoutMs passes.
bool waitFor(const std::function<bool()> &cond, int timeoutMs = 5000);

//! Runs the event loop for ms, for work that has nothing to wait on.
void spin(int ms);

struct Registrar
{
    Registrar(const char *suite, const char *name, void (*fn)())
    {
        TestCase tc = { suite, name, fn };
        registry().push_back(tc);
    }
};

//! Printable form for the values CHECK_EQ compares.
inline std::string show(const std::string &v) { return "\"" + v + "\""; }
inline std::string show(const QString &v) { return show(v.toStdString()); }
inline std::string show(const QByteArray &v) { return show(v.toStdString()); }
inline std::string show(const char *v) { return v ? show(std::string(v)) : std::string("(null)"); }
inline std::string show(bool v) { return v ? "true" : "false"; }
inline std::string show(long long v) { char b[32]; snprintf(b, sizeof(b), "%lld", v); return b; }
inline std::string show(unsigned long long v) { return show(static_cast<long long>(v)); }
inline std::string show(int v) { return show(static_cast<long long>(v)); }
inline std::string show(unsigned int v) { return show(static_cast<long long>(v)); }
inline std::string show(long v) { return show(static_cast<long long>(v)); }
inline std::string show(unsigned long v) { return show(static_cast<long long>(v)); }
inline std::string show(const void *v) { char b[32]; snprintf(b, sizeof(b), "%p", v); return b; }

} // namespace testing

#define TEST(suite, name)                                                      \
    static void suite##_##name##_body();                                       \
    static testing::Registrar suite##_##name##_reg(#suite, #name,              \
                                                   suite##_##name##_body);     \
    static void suite##_##name##_body()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond))                                                           \
            testing::reportFailure(__FILE__, __LINE__,                         \
                std::string("CHECK(") + #cond + ") is false");                 \
    } while (0)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        auto _a = (actual);                                                    \
        auto _e = (expected);                                                  \
        if (!(_a == _e))                                                       \
            testing::reportFailure(__FILE__, __LINE__,                         \
                std::string(#actual " == " #expected " : got ")                \
                + testing::show(_a) + ", wanted " + testing::show(_e));        \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                       \
    do {                                                                       \
        QString _h = (haystack);                                               \
        QString _n = (needle);                                                 \
        if (!_h.contains(_n))                                                  \
            testing::reportFailure(__FILE__, __LINE__,                         \
                std::string("expected ") + testing::show(_h)                   \
                + " to contain " + testing::show(_n));                         \
    } while (0)

#define CHECK_LACKS(haystack, needle)                                          \
    do {                                                                       \
        QString _h = (haystack);                                               \
        QString _n = (needle);                                                 \
        if (_h.contains(_n))                                                   \
            testing::reportFailure(__FILE__, __LINE__,                         \
                std::string("expected ") + testing::show(_h)                   \
                + " not to contain " + testing::show(_n));                     \
    } while (0)

#define SKIP(why)                                                              \
    do {                                                                       \
        testing::reportSkip(why);                                              \
        return;                                                                \
    } while (0)

#endif
