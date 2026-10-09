// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Scroll-back storage and searching it. The search takes its start line and
 * column straight from QML (KSession::search).
 */

#include "check.h"

#include "History.h"
#include "HistorySearch.h"
#include "ScreenWindow.h"
#include "Vt102Emulation.h"

#include <QPointer>
#include <QRegularExpression>

#include <cstring>

using namespace Konsole;

namespace
{

QVector<Character> lineOf(const QString &text)
{
    QVector<Character> cells;
    for (const QChar c : text)
        cells.append(Character(c.unicode()));
    return cells;
}

QString lineAt(const HistoryScroll &history, int line)
{
    const int len = history.getLineLen(line);
    QVector<Character> cells(len);
    history.getCells(line, 0, len, cells.data());

    QString text;
    for (const Character &c : cells)
        text.append(QChar(static_cast<char16_t>(c.character)));
    return text;
}

struct SearchResult
{
    bool done = false;
    bool found = false;
    int startLine = -1;
    int startColumn = -1;
};

/*
 * Runs a HistorySearch the way KSession::search does: it deletes itself
 * once it has answered.
 */
SearchResult runSearch(Emulation *emulation, const QString &pattern, bool forwards,
                       int startColumn, int startLine)
{
    SearchResult result;
    auto *search = new HistorySearch(QPointer<Emulation>(emulation), QRegularExpression(pattern),
                                     forwards, startColumn, startLine, nullptr);

    QObject::connect(search, &HistorySearch::matchFound, [&result](int sc, int sl, int, int) {
        result.done = true;
        result.found = true;
        result.startColumn = sc;
        result.startLine = sl;
    });
    QObject::connect(search, &HistorySearch::noMatchFound, [&result]() { result.done = true; });

    search->search();
    testing::spin(10);
    return result;
}

struct Terminal
{
    Vt102Emulation emulation;

    explicit Terminal(int historyLines)
    {
        emulation.setHistory(HistoryTypeBuffer(historyLines));
        emulation.createWindow()->setWindowLines(24);
        emulation.setImageSize(24, 80);
    }

    void feed(const QByteArray &bytes)
    {
        emulation.receiveData(bytes.constData(), bytes.size());
    }
};

} // namespace

// A zero-line buffer divided by zero on the first line that scrolled off
TEST(HistoryBuffer, zeroLinesKeepsNothing)
{
    HistoryScrollBuffer history(0);
    history.addCellsVector(lineOf(QStringLiteral("gone")));
    history.addLine(true);

    CHECK_EQ(history.getLines(), 0);
}

/*
 * Shrinking a full buffer kept the oldest lines, rotated, and left the wrap
 * flags where they were rather than with their lines.
 */
TEST(HistoryBuffer, shrinkingKeepsTheNewestLinesInOrder)
{
    HistoryScrollBuffer history(10);
    for (int i = 0; i < 15; ++i) {
        history.addCellsVector(lineOf(QStringLiteral("L%1").arg(i)));
        history.addLine(i == 12);
    }

    history.setMaxNbLines(5);

    CHECK_EQ(history.getLines(), 5);
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(lineAt(history, i), QStringLiteral("L%1").arg(10 + i));
        CHECK_EQ(history.isWrappedLine(i), i == 2);
    }

    // and the next line still pushes out the oldest
    history.addCellsVector(lineOf(QStringLiteral("L15")));
    history.addLine(false);
    CHECK_EQ(lineAt(history, 0), QStringLiteral("L11"));
    CHECK_EQ(lineAt(history, 4), QStringLiteral("L15"));
}

/*
 * A read beyond the file used to be reported and then done anyway, leaving
 * whatever the caller's buffer held where the file ended.
 */
TEST(HistoryFile, readsBeyondTheEndComeBackEmpty)
{
    HistoryFile file;
    const unsigned char data[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    file.add(data, sizeof(data));

    unsigned char buffer[8];
    memset(buffer, 0xaa, sizeof(buffer));
    file.get(buffer, sizeof(buffer), 6);

    bool allZero = true;
    for (unsigned char b : buffer)
        allZero = allZero && b == 0;
    CHECK(allZero);

    // in range still reads
    file.get(buffer, 4, 2);
    CHECK_EQ(static_cast<int>(buffer[0]), 3);
    CHECK_EQ(static_cast<int>(buffer[3]), 6);
}

// isWrappedLine() accepted the line one past the last
TEST(HistoryFile, noWrapFlagPastTheLastLine)
{
    HistoryScrollFile history(QStringLiteral("test"));
    for (int i = 0; i < 3; ++i) {
        const QVector<Character> cells = lineOf(QStringLiteral("line"));
        history.addCells(cells.constData(), cells.size());
        history.addLine(true);
    }

    CHECK(history.isWrappedLine(2));
    CHECK(!history.isWrappedLine(3));
    CHECK(!history.isWrappedLine(-1));
}

/*
 * A start line past the end read past the screen buffer, a negative one
 * crashed in the history buffer. Both now search from the nearest line.
 */
TEST(HistorySearch, startOutsideTheTextIsClamped)
{
    Terminal t(1000);
    t.feed("alpha\r\nbeta\r\n");

    SearchResult past = runSearch(&t.emulation, QStringLiteral("beta"), true, 0, 100000);
    CHECK(past.done);
    CHECK(past.found);
    CHECK_EQ(past.startLine, 1);

    SearchResult before = runSearch(&t.emulation, QStringLiteral("alpha"), false, -5, -50);
    CHECK(before.done);
    CHECK(before.found);
    CHECK_EQ(before.startLine, 0);
}

/*
 * The text is searched in blocks of 10000 lines; backwards, a match was
 * reported relative to the wrong end of its block.
 */
TEST(HistorySearch, backwardsMatchReportsItsOwnLine)
{
    Terminal t(30000);
    QByteArray text;
    for (int i = 0; i < 25000; ++i)
        text += (i == 3 ? QByteArray("needle") : "line " + QByteArray::number(i)) + "\r\n";
    t.feed(text);

    SearchResult result = runSearch(&t.emulation, QStringLiteral("needle"), false, 0,
                                    t.emulation.lineCount() - 1);
    CHECK(result.found);
    CHECK_EQ(result.startLine, 3);
    CHECK_EQ(result.startColumn, 0);
}
