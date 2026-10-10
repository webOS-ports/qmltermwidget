// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "check.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QThread>

#include <cstring>

namespace testing
{

int g_failuresInCase = 0;
bool g_skippedCase = false;

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

void reportFailure(const char *file, int line, const std::string &what)
{
    ++g_failuresInCase;
    fprintf(stderr, "    %s:%d: %s\n", file, line, what.c_str());
}

void reportSkip(const std::string &why)
{
    g_skippedCase = true;
    fprintf(stderr, "    skipped: %s\n", why.c_str());
}

bool waitFor(const std::function<bool()> &cond, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (!cond()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }

    return true;
}

void spin(int ms)
{
    waitFor([]() { return false; }, ms);
}

// "Suite" runs a whole suite, "Suite.name" one case of it
static bool matches(const char *filter, const TestCase &tc)
{
    const std::string full = std::string(tc.suite) + "." + tc.name;
    return filter == full || strcmp(filter, tc.suite) == 0;
}

int runAll(const char *suiteFilter)
{
    int run = 0;
    int failed = 0;
    int skipped = 0;

    for (const TestCase &tc : registry()) {
        if (suiteFilter && !matches(suiteFilter, tc))
            continue;

        ++run;
        g_failuresInCase = 0;
        g_skippedCase = false;
        fprintf(stderr, "[ run  ] %s.%s\n", tc.suite, tc.name);
        tc.fn();
        // Let deleteLater() and queued signals of this case finish here,
        // not in the next one
        spin(20);

        if (g_failuresInCase != 0) {
            ++failed;
            fprintf(stderr, "[ FAIL ] %s.%s (%d)\n", tc.suite, tc.name, g_failuresInCase);
        } else if (g_skippedCase) {
            ++skipped;
            fprintf(stderr, "[ skip ] %s.%s\n", tc.suite, tc.name);
        } else {
            fprintf(stderr, "[  ok  ] %s.%s\n", tc.suite, tc.name);
        }
    }

    fprintf(stderr, "\n%d run, %d failed, %d skipped\n", run, failed, skipped);
    return failed == 0 ? 0 : 1;
}

} // namespace testing

int main(int argc, char **argv)
{
    // The library still reaches for QApplication (clipboard, palette); no
    // display is needed for any of this
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    return testing::runAll(argc > 1 ? argv[1] : nullptr);
}
