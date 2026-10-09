// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Sessions, their pty and the processes in them. These start real shells
 * (/bin/sh) on real ptys, and skip when there is none.
 */

#include "check.h"

#include "Pty.h"
#include "ProcessInfo.h"
#include "TerminalDisplay.h"
#include "kptydevice.h"
#include "kptyprocess.h"
#include "ksession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <memory>

using namespace Konsole;

namespace
{

void setSession(TerminalDisplay &display, KSession *session)
{
    display.setProperty("session", QVariant::fromValue(session));
}

const QString shell = QStringLiteral("/bin/sh");

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

std::unique_ptr<KSession> startShell(const QString &workDir, const QStringList &args = {})
{
    auto session = std::make_unique<KSession>();
    session->setShellProgram(shell);
    if (!args.isEmpty())
        session->setArgs(args);
    session->setInitialWorkingDirectory(workDir);
    session->startShellProgram();
    return session;
}

} // namespace

/*
 * Moving a display to another session left it connected to the first: every
 * key went to both shells, a password typed after the switch included.
 */
TEST(Session, switchingSessionsMovesTheInput)
{
    if (!QFileInfo(shell).isExecutable())
        SKIP("no /bin/sh");

    QTemporaryDir dir;
    const QString first = dir.filePath(QStringLiteral("first"));
    const QString second = dir.filePath(QStringLiteral("second"));
    auto s1 = startShell(dir.path(), { QStringLiteral("-c"), QStringLiteral("stty raw -echo; cat > ") + first });
    auto s2 = startShell(dir.path(), { QStringLiteral("-c"), QStringLiteral("stty raw -echo; cat > ") + second });

    TerminalDisplay display;
    setSession(display, s1.get());
    setSession(display, s2.get());
    testing::spin(300);

    display.simulateKeyPress(Qt::Key_X, Qt::NoModifier, true, 0, QStringLiteral("x"));
    CHECK(testing::waitFor([&]() { return readAll(second) == "x"; }));
    testing::spin(200);
    CHECK_EQ(readAll(first), QByteArray());

    setSession(display, nullptr);
}

/*
 * changeDir() typed "cd <dir>" into the shell unquoted: a directory called
 * "x; touch INJECTED" ran the touch.
 */
TEST(Session, changeDirTreatsTheNameAsOneDirectory)
{
    if (!QFileInfo(shell).isExecutable())
        SKIP("no /bin/sh");

    QTemporaryDir dir;
    const QString odd = dir.filePath(QStringLiteral("it's here; touch INJECTED"));
    QDir().mkpath(odd);

    auto session = startShell(dir.path());
    testing::spin(300);

    session->changeDir(odd);
    session->sendText(QStringLiteral("pwd > ") + dir.filePath(QStringLiteral("pwd.out")) + QStringLiteral("\n"));

    CHECK(testing::waitFor([&]() { return readAll(dir.filePath(QStringLiteral("pwd.out"))).size() > 0; }));
    CHECK_EQ(readAll(dir.filePath(QStringLiteral("pwd.out"))).trimmed(), odd.toUtf8());
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("INJECTED"))));
    CHECK(!QFile::exists(odd + QStringLiteral("/../INJECTED")));
}

/*
 * The path is typed into the terminal, and the pty acts on its editing
 * characters before the shell sees any quotes: ^U throws away what was
 * typed so far ("cd '..."), and whatever follows runs as a command.
 */
TEST(Session, changeDirRefusesControlCharacters)
{
    if (!QFileInfo(shell).isExecutable())
        SKIP("no /bin/sh");

    QTemporaryDir dir;
    auto session = startShell(dir.path());
    testing::spin(300);

    session->changeDir(dir.path() + QStringLiteral("\x15touch ") + dir.filePath(QStringLiteral("INJECTED")) + QStringLiteral(" #"));
    session->sendText(QStringLiteral("touch ") + dir.filePath(QStringLiteral("done")) + QStringLiteral("\n"));

    CHECK(testing::waitFor([&]() { return QFile::exists(dir.filePath(QStringLiteral("done"))); }));
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("INJECTED"))));
}

/*
 * OSC 11 (set the background colour) hit a Q_ASSERT(0), so any program that
 * printed it aborted a debug build. This suite is built with asserts on.
 */
TEST(Session, backgroundColourRequestIsHarmless)
{
    if (!QFileInfo(shell).isExecutable())
        SKIP("no /bin/sh");

    QTemporaryDir dir;
    auto session = startShell(dir.path(), { QStringLiteral("-c"), QStringLiteral("printf '\\033]11;red\\007'; sleep 0.3") });

    TerminalDisplay display;
    setSession(display, session.get());

    bool finished = false;
    QObject::connect(session.get(), &KSession::finished, [&finished]() { finished = true; });
    CHECK(testing::waitFor([&]() { return finished; }));

    setSession(display, nullptr);
}

/*
 * With an empty environment the TERM fallback was never applied: it sat
 * inside the loop over the variables.
 */
TEST(Pty, emptyEnvironmentStillGetsTerm)
{
    if (!QFileInfo(QStringLiteral("/bin/true")).isExecutable())
        SKIP("no /bin/true");

    // Otherwise our own TERM would be inherited and hide the fallback
    const QByteArray savedTerm = qgetenv("TERM");
    qunsetenv("TERM");

    Pty pty;
    pty.start(QStringLiteral("/bin/true"), { QStringLiteral("true") }, {}, 0, false);
    CHECK_EQ(pty.processEnvironment().value(QStringLiteral("TERM")), QStringLiteral("xterm-256color"));
    pty.waitForFinished(2000);

    if (!savedTerm.isNull())
        qputenv("TERM", savedTerm);
}

/*
 * Deleting a KPtyProcess whose child still ran: ~QProcess kills it and
 * emits stateChanged(), which ran a lambda reading the already destroyed
 * private data (ASan: SEGV).
 */
TEST(Pty, deletingARunningProcess)
{
    if (!QFileInfo(QStringLiteral("/bin/sleep")).isExecutable())
        SKIP("no /bin/sleep");

    auto *process = new KPtyProcess();
    process->setProgram(QStringLiteral("/bin/sleep"), { QStringLiteral("10") });
    process->start();
    CHECK(process->waitForStarted());
    delete process;
}

/*
 * close() deleted the notifiers without clearing them. The freed notifier is
 * read inside Qt, which is not built with ASan: run-valgrind.sh is what
 * reports it.
 */
TEST(Pty, deviceAfterClose)
{
    KPtyDevice device;
    CHECK(device.open());
    device.close();

    CHECK(device.isSuspended());
    device.setSuspended(false);
}

/*
 * A process names itself, and the name in /proc/<pid>/stat is bracketed,
 * not escaped: counting brackets let "a) b c" shift every field after it.
 */
TEST(ProcessInfo, nameWithBracketsAndSpaces)
{
    if (!QFileInfo(QStringLiteral("/bin/sleep")).isExecutable())
        SKIP("no /bin/sleep");

    QTemporaryDir dir;
    const QString odd = dir.filePath(QStringLiteral("a) b c"));
    CHECK(QFile::link(QStringLiteral("/bin/sleep"), odd));

    QProcess process;
    process.start(odd, { QStringLiteral("10") });
    CHECK(process.waitForStarted());

    std::unique_ptr<ProcessInfo> info(ProcessInfo::newInstance(static_cast<int>(process.processId())));
    info->update();

    bool ok = false;
    CHECK_EQ(info->name(&ok), QStringLiteral("a) b c"));
    CHECK(ok);
    CHECK_EQ(info->parentPid(&ok), static_cast<int>(QCoreApplication::applicationPid()));
    CHECK(ok);

    process.kill();
    process.waitForFinished();
}
