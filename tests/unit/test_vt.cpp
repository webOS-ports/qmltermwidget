// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The escape-sequence parser and the screen model take whatever bytes the
 * program in the terminal prints, so a crafted file shown with cat is enough
 * to reach any of these. The memory errors here only show as crashes under
 * the sanitizer builds of tests/hardening; the checks on the screen content
 * hold in every build.
 */

#include "check.h"

#include "History.h"
#include "Screen.h"
#include "ScreenWindow.h"
#include "TerminalCharacterDecoder.h"
#include "Vt102Emulation.h"

#include <QTextStream>

using namespace Konsole;

namespace
{

struct Terminal
{
    Vt102Emulation emulation;
    ScreenWindow *window;

    explicit Terminal(int historyLines = 1000)
    {
        emulation.setHistory(HistoryTypeBuffer(historyLines));
        window = emulation.createWindow();
        window->setWindowLines(24);
        emulation.setImageSize(24, 80);
    }

    void feed(const QByteArray &bytes)
    {
        emulation.receiveData(bytes.constData(), bytes.size());
    }

    QString allText(int fromLine = 0, int toLine = -1)
    {
        QString out;
        QTextStream stream(&out);
        PlainTextDecoder decoder;
        decoder.begin(&stream);
        emulation.writeToStream(&decoder, fromLine, toLine < 0 ? emulation.lineCount() - 1 : toLine);
        decoder.end();
        return out;
    }

    QString selectEverything()
    {
        Screen *screen = window->screen();
        screen->setSelectionStart(0, 0, false);
        screen->setSelectionEnd(screen->getColumns() - 1, screen->getHistLines() + screen->getLines() - 1);
        return screen->selectedText(true);
    }
};

const QString flag = QString::fromUtf8("\xf0\x9f\x87\xbe\xf0\x9f\x87\xaa"); // U+1F1FE U+1F1EA

} // namespace

/*
 * Combining a pair of regional indicators (any flag emoji) kept a reference
 * into the line while the line was resized, and read it afterwards: a
 * heap-use-after-free from printing a flag. These are the bytes that hit it
 * every time, then the same in insert mode, which shifts the line instead.
 */
TEST(Vt, flagEmojiDoesNotUseAFreedLine)
{
    Terminal t;
    const QByteArray stray("\x87\xac");

    for (int i = 0; i < 50; ++i)
        t.feed(stray + flag.toUtf8());
    t.feed("\r\n\033[4h");
    for (int i = 0; i < 50; ++i)
        t.feed(flag.toUtf8());

    CHECK_CONTAINS(t.allText(), flag);
}

/*
 * Selecting and copying went through a static 1024-cell buffer, guarded by
 * a Q_ASSERT only, so a line wider than that overflowed it in release
 * builds. 1024 columns plus the line break is already one too many.
 */
TEST(Vt, copyingAFullWidthLineKeepsAllOfIt)
{
    Terminal t;
    t.feed("\033[8;3;1024t");
    t.feed(QByteArray(1024, 'A') + "\r\n" + QByteArray(1024, 'B') + "\r\n");

    const QString text = t.selectEverything();
    CHECK_EQ(static_cast<int>(text.count(QLatin1Char('A'))), 1024);
    CHECK_EQ(static_cast<int>(text.count(QLatin1Char('B'))), 1024);
}

/*
 * CSI 8 t lets the program pick the screen size; 40000x40000 asked for two
 * 1.6 billion cell screens and the process ran out of memory.
 */
TEST(Vt, resizeRequestsAreCapped)
{
    Terminal t;
    t.feed("\033[8;40000;40000t");

    const QSize size = t.emulation.imageSize();
    CHECK(size.width() >= 1 && size.width() <= 1024);
    CHECK(size.height() >= 1 && size.height() <= 1024);

    // still a working terminal afterwards
    t.feed("\033[8;24;80tafter\r\n");
    CHECK_CONTAINS(t.allText(), QStringLiteral("after"));
}

/*
 * A program can make the screen shorter than the window showing it; the
 * window then asked qBound() for a range whose maximum was below its
 * minimum, an assertion that terminal output could fire in debug builds.
 */
TEST(Vt, screenShorterThanItsWindow)
{
    Terminal t;
    t.feed("\033[8;3;80tshort\r\n");

    CHECK(t.window->getImage() != nullptr);
    CHECK_EQ(t.window->currentLine(), 0);
    CHECK_CONTAINS(t.allText(), QStringLiteral("short"));
}

// Line ranges reach the screen straight from callers such as a search
TEST(Vt, writingOutOfRangeLinesIsClamped)
{
    Terminal t;
    t.feed("first\r\nsecond\r\n");

    const QString text = t.allText(-50, 1000000);
    CHECK_CONTAINS(text, QStringLiteral("first"));
    CHECK_CONTAINS(text, QStringLiteral("second"));

    CHECK_EQ(t.allText(500, 400), QString());
}
