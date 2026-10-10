// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * TerminalDisplay, the QML item. Everything here is reachable from QML:
 * the session property, the simulate*() invokables, lineSpacing, paste.
 * No shell is started; what the display would send is read off its
 * keyPressedSignal.
 */

#include "check.h"

#include "History.h"
#include "ScreenWindow.h"
#include "TerminalDisplay.h"
#include "Vt102Emulation.h"
#include "ksession.h"

#include <QApplication>
#include <QClipboard>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>

#include <climits>
#include <memory>

using namespace Konsole;

namespace
{

// Through the properties, as QML sets and reads them
void setSession(TerminalDisplay &display, KSession *session)
{
    display.setProperty("session", QVariant::fromValue(session));
}

KSession *sessionOf(const TerminalDisplay &display)
{
    return display.property("session").value<KSession *>();
}

int fontHeight(const TerminalDisplay &display)
{
    return display.property("fontMetrics").toSize().height();
}

void resize(TerminalDisplay &display, qreal width, qreal height)
{
    display.setWidth(width);
    display.setHeight(height);
}

// Collects the text of everything the display hands to the emulation
struct Typed
{
    QStringList texts;

    explicit Typed(TerminalDisplay &display)
    {
        QObject::connect(&display, &TerminalDisplay::keyPressedSignal, &display,
                         [this](QKeyEvent *e, bool) { texts << e->text(); });
    }
};

} // namespace

// Reading session before one was set handed QML an uninitialised pointer
TEST(Display, startsWithoutASession)
{
    TerminalDisplay display;
    CHECK(sessionOf(display) == nullptr);
}

// session = null dereferenced the null pointer
TEST(Display, sessionCanBeCleared)
{
    TerminalDisplay display;
    KSession session;

    setSession(display, &session);
    CHECK(sessionOf(display) == &session);

    setSession(display, nullptr);
    CHECK(sessionOf(display) == nullptr);

    // and input without a session goes nowhere, harmlessly
    display.simulateKeyPress(Qt::Key_A, Qt::NoModifier, true, 0, QStringLiteral("a"));
    display.simulateMousePress(10, 10, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    display.simulateMouseRelease(10, 10, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    display.pasteClipboard();
}

// A session destroyed before its display is not handed back to QML
TEST(Display, destroyedSessionIsForgotten)
{
    TerminalDisplay display;
    auto *session = new KSession();

    setSession(display, session);
    delete session;

    CHECK(sessionOf(display) == nullptr);
    display.simulateKeyPress(Qt::Key_A, Qt::NoModifier, true, 0, QStringLiteral("a"));
}

// A simulated key release was typed as a second key press
TEST(Display, keyReleaseIsNotTyped)
{
    TerminalDisplay display;
    Typed typed(display);

    display.simulateKeyPress(Qt::Key_A, Qt::NoModifier, true, 0, QStringLiteral("a"));
    display.simulateKeyPress(Qt::Key_A, Qt::NoModifier, false, 0, QStringLiteral("a"));

    CHECK_EQ(static_cast<int>(typed.texts.size()), 1);
}

/*
 * A pasted ESC could end a bracketed paste early ("\e[201~") and have the
 * rest run as typed commands. The program must see exactly one end marker,
 * its own.
 */
TEST(Display, pasteCannotEndABracketedPaste)
{
    TerminalDisplay display;
    KSession session;
    setSession(display, &session);
    display.setBracketedPasteMode(true);
    Typed typed(display);

    QApplication::clipboard()->setText(QStringLiteral("safe\x1b[201~echo pwned\r"));
    display.pasteClipboard();

    CHECK_EQ(static_cast<int>(typed.texts.size()), 1);
    if (typed.texts.size() != 1)
        return;

    const QString sent = typed.texts.first();
    CHECK(sent.startsWith(QStringLiteral("\x1b[200~")));
    CHECK(sent.endsWith(QStringLiteral("\x1b[201~")));
    CHECK_EQ(static_cast<int>(sent.count(QStringLiteral("\x1b"))), 2);
    CHECK_CONTAINS(sent, QStringLiteral("safe[201~echo pwned"));

    setSession(display, nullptr);
}

/*
 * Trimming trailing newlines from a paste is off unless asked for; the flag
 * was never initialised, so it was on or off at random.
 */
TEST(Display, pasteKeepsTrailingNewlinesByDefault)
{
    // On the heap, where an uninitialised flag would hold the allocator's
    // (or the sanitizer's) fill rather than a lucky zero from the stack
    auto display = std::make_unique<TerminalDisplay>();
    KSession session;
    setSession(*display, &session);
    Typed typed(*display);

    QApplication::clipboard()->setText(QStringLiteral("ls\n\n"));
    display->pasteClipboard();

    CHECK_EQ(static_cast<int>(typed.texts.size()), 1);
    if (!typed.texts.isEmpty())
        CHECK_EQ(typed.texts.first(), QStringLiteral("ls\r\r"));

    setSession(*display, nullptr);
}

/*
 * lineSpacing is an int property; minus the font height made the line
 * height 0, which the geometry code divides by (SIGFPE).
 */
TEST(Display, negativeLineSpacingIsClamped)
{
    TerminalDisplay display;
    const int height = fontHeight(display);

    display.setLineSpacing(-height);
    CHECK_EQ(display.lineSpacing(), 0);
    CHECK(fontHeight(display) >= 1);

    resize(display, 400, 300);
    CHECK(display.lines() >= 1);
}

/*
 * The simulate*() calls take any int from QML; converting a position
 * beyond the range of int after adding the margins was undefined (UBSan).
 */
TEST(Display, extremeMousePositionsAreClamped)
{
    TerminalDisplay display;
    KSession session;
    setSession(display, &session);
    resize(display, 400, 300);

    display.simulateMousePress(INT_MAX, INT_MAX, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    display.simulateMouseMove(-INT_MAX, -INT_MAX, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    display.simulateMouseRelease(INT_MAX, 5, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    display.simulateMouseDoubleClick(INT_MIN, INT_MAX, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);

    setSession(display, nullptr);
}

/*
 * The scroll bar never said when its range changed, so scrollbarMaximum
 * went stale in QML while scrolled back.
 */
TEST(Display, scrollBarAnnouncesRangeChanges)
{
    ScrollBar bar;
    int rangeChanges = 0;
    QList<int> values;
    QObject::connect(&bar, &ScrollBar::rangeChanged, [&rangeChanges]() { ++rangeChanges; });
    QObject::connect(&bar, &ScrollBar::valueChanged, [&values](int v) { values << v; });

    bar.setRange(0, 10);
    bar.setValue(10);
    bar.setRange(0, 10);   // unchanged: nothing to say
    bar.setRange(0, 4);    // the value has to follow

    CHECK_EQ(rangeChanges, 2);
    CHECK_EQ(bar.value(), 4);
    CHECK(values == QList<int>({ 10, 4 }));
}

/*
 * Cells hold whole code points; laying them out went through QChar, which
 * holds only up to U+FFFF: anything above was measured as another character
 * (and asserted in debug builds). Emoji, mathematical letters, CJK
 * extension B, and a combining mark from beyond the first plane.
 */
TEST(Display, codePointsBeyondTheFirstPlane)
{
    Vt102Emulation emulation;
    emulation.setHistory(HistoryTypeBuffer(100));
    ScreenWindow *window = emulation.createWindow();
    window->setWindowLines(24);
    emulation.setImageSize(24, 80);

    TerminalDisplay display;
    resize(display, 640, 480);
    display.setScreenWindow(window);

    const QByteArray text("\xf0\x9f\x98\x80 \xf0\x9d\x90\x80 \xf0\xa0\x80\x80 a\xf0\x9d\xaa\x9f end @\r\n");
    emulation.receiveData(text.constData(), text.size());
    display.updateImage();

    // and selecting a word over them measures them too
    display.simulateMouseDoubleClick(5, 5, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    CHECK_CONTAINS(window->selectedText(false), QString());
    CHECK(display.lines() >= 1);
}

/*
 * The input method asks for several properties in one query event. The
 * display answered only ImEnabled and ImHints and marked the event handled,
 * so the cursor rectangle, the surrounding text and the font never reached
 * the keyboard.
 */
TEST(Display, answersEveryInputMethodQuery)
{
    TerminalDisplay display;
    KSession session;
    setSession(display, &session);
    resize(display, 400, 300);

    QInputMethodQueryEvent query(Qt::ImEnabled | Qt::ImHints | Qt::ImCursorRectangle
                                 | Qt::ImSurroundingText | Qt::ImFont
                                 | Qt::ImCursorPosition | Qt::ImAnchorPosition);
    QCoreApplication::sendEvent(&display, &query);

    CHECK(query.value(Qt::ImEnabled).toBool());
    const int hints = query.value(Qt::ImHints).toInt();
    CHECK(hints & Qt::ImhNoAutoUppercase);
    CHECK(hints & Qt::ImhNoPredictiveText);
    CHECK(hints & Qt::ImhPreferLowercase);
    CHECK(query.value(Qt::ImCursorRectangle).toRect().isValid());
    CHECK(query.value(Qt::ImSurroundingText).isValid());
    CHECK(query.value(Qt::ImFont).isValid());
    CHECK(query.value(Qt::ImAnchorPosition).isValid());
    CHECK_EQ(query.value(Qt::ImAnchorPosition).toInt(), query.value(Qt::ImCursorPosition).toInt());

    setSession(display, nullptr);
}

// Select all reaches the lines scrolled into the history, not only those on
// the screen, and an empty display (no screen window yet) ignores it. The
// display announces whether there is anything to copy, which it never did:
// nothing connected it to the screen window's selection.
TEST(Display, selectAllIncludesTheHistory)
{
    TerminalDisplay unattached;
    unattached.selectAll();

    Vt102Emulation emulation;
    emulation.setHistory(HistoryTypeBuffer(100));
    ScreenWindow *window = emulation.createWindow();
    window->setWindowLines(4);
    emulation.setImageSize(4, 20);

    TerminalDisplay display;
    resize(display, 200, 100);
    display.setScreenWindow(window);

    QByteArray text;
    for (int i = 0; i < 10; ++i)
        text += "line" + QByteArray::number(i) + "\r\n";
    emulation.receiveData(text.constData(), text.size());
    display.updateImage();

    bool copyAvailable = false;
    QObject::connect(&display, &TerminalDisplay::copyAvailable, &display,
                     [&copyAvailable](bool available) { copyAvailable = available; });
    display.selectAll();

    const QString selected = window->selectedText(false);
    CHECK_CONTAINS(selected, QStringLiteral("line0"));
    CHECK_CONTAINS(selected, QStringLiteral("line9"));
    CHECK(copyAvailable);

    window->clearSelection();
    CHECK(!copyAvailable);
}

// The screen clears the selection on its own when it is resized (and when
// output reaches it); the display still said there was something to copy
TEST(Display, copyAvailableFollowsAClearedSelection)
{
    Vt102Emulation emulation;
    emulation.setHistory(HistoryTypeBuffer(100));
    ScreenWindow *window = emulation.createWindow();
    window->setWindowLines(4);
    emulation.setImageSize(4, 20);

    TerminalDisplay display;
    resize(display, 200, 100);
    display.setScreenWindow(window);

    const QByteArray text("hello\r\n");
    emulation.receiveData(text.constData(), text.size());
    display.updateImage();

    bool copyAvailable = false;
    QObject::connect(&display, &TerminalDisplay::copyAvailable, &display,
                     [&copyAvailable](bool available) { copyAvailable = available; });
    display.selectAll();
    CHECK(copyAvailable);

    emulation.setImageSize(5, 30);
    display.updateImage();
    CHECK(!window->hasSelection());
    CHECK(!copyAvailable);
}

// Selecting a word in blank space selects blanks, which are nothing to copy
TEST(Display, blanksAreNothingToCopy)
{
    Vt102Emulation emulation;
    ScreenWindow *window = emulation.createWindow();
    window->setWindowLines(4);
    emulation.setImageSize(4, 20);

    TerminalDisplay display;
    resize(display, 200, 100);
    display.setScreenWindow(window);
    display.updateImage();

    bool copyAvailable = true;
    QObject::connect(&display, &TerminalDisplay::copyAvailable, &display,
                     [&copyAvailable](bool available) { copyAvailable = available; });
    display.simulateMouseDoubleClick(100, 50, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    CHECK(window->hasSelection());
    CHECK(!copyAvailable);
}

// Selecting a word asks each line whether it wraps onto the next. The display
// fetches that only on output, so on a screen with none yet (or after a
// resize added lines) it read past the end of the list.
TEST(Display, selectingBeyondTheFetchedLinesStaysInBounds)
{
    Vt102Emulation emulation;
    ScreenWindow *window = emulation.createWindow();
    window->setWindowLines(4);
    emulation.setImageSize(4, 20);

    TerminalDisplay display;
    resize(display, 200, 100);
    display.setScreenWindow(window);
    display.updateImage();

    display.simulateMouseDoubleClick(100, 50, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    display.simulateMouseMove(150, 80, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    display.simulateMouseRelease(150, 80, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    CHECK_EQ(window->selectedText(false).trimmed(), QString());
}
