// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Feeds the escape-sequence parser and screen model what a hostile program
 * could print, and exercises what reads the screen back (the display laying
 * it out, the image, a select-all copy). Meant to run under ASan+UBSan: a crash or a sanitizer
 * report is the finding; there is nothing to compare.
 *
 *   fuzz-vt --seed=N [--rounds=R]   R generated chunks from seed N
 *   fuzz-vt FILE...                 replay inputs, one emulator per file
 *
 * The generator mixes valid sequences with hostile parameters (huge counts,
 * window resizes, long OSC strings, regional indicators and combining
 * marks, random code points and bytes), which is what found the flag-emoji
 * use-after-free and the 1024-cell copy overflow. A seed is reproducible.
 */

#include "History.h"
#include "Screen.h"
#include "ScreenWindow.h"
#include "TerminalDisplay.h"
#include "Vt102Emulation.h"

#include <QApplication>
#include <QFile>

#include <cstdio>
#include <initializer_list>
#include <random>

using namespace Konsole;

namespace
{

struct Terminal
{
    Vt102Emulation emulation;
    ScreenWindow *window;
    // Lays the screen out the way the QML item does, character widths and all
    TerminalDisplay display;

    Terminal()
    {
        emulation.setHistory(HistoryTypeBuffer(1000));
        window = emulation.createWindow();
        window->setWindowLines(24);
        emulation.setImageSize(24, 80);
        display.setWidth(640);
        display.setHeight(480);
        display.setScreenWindow(window);
    }

    void feed(const QByteArray &bytes)
    {
        emulation.receiveData(bytes.constData(), bytes.size());
    }

    // What a view and a copy do with the screen
    void readBack()
    {
        display.updateImage();
        (void)window->getImage();
        Screen *screen = window->screen();
        screen->setSelectionStart(0, 0, false);
        screen->setSelectionEnd(screen->getColumns() - 1, screen->getHistLines() + screen->getLines() - 1);
        (void)screen->selectedText(true);
    }
};

QByteArray utf8(char32_t codePoint)
{
    return QString::fromUcs4(&codePoint, 1).toUtf8();
}

QByteArray chunk(std::mt19937 &rng)
{
    auto pick = [&rng](std::initializer_list<const char *> list) {
        auto it = list.begin();
        std::advance(it, rng() % list.size());
        return QByteArray(*it);
    };
    auto number = [&rng](unsigned limit) { return QByteArray::number(static_cast<int>(rng() % limit)); };

    QByteArray d;
    const int ops = rng() % 6 + 1;
    for (int o = 0; o < ops; ++o) {
        switch (rng() % 12) {
        case 0: // CSI with up to three large parameters
            d += "\033[";
            for (int a = rng() % 4; a > 0; --a) {
                d += number(5000);
                if (a > 1)
                    d += ';';
            }
            d += "ABCDHJKLMPSTXbdfghlmnrt@"[rng() % 24];
            break;
        case 1: // private modes
            d += "\033[?" + number(5000) + "hl"[rng() % 2];
            break;
        case 2: // window resize, also beyond any screen
            d += "\033[8;" + number(rng() % 8 ? 120 : 50000) + ';' + number(rng() % 8 ? 300 : 50000) + 't';
            break;
        case 3: // OSC with a printable payload
            d += "\033]" + number(60) + ';';
            for (int a = rng() % 300; a > 0; --a)
                d += static_cast<char>(32 + rng() % 94);
            d += pick({ "\007", "\033\\" });
            break;
        case 4: // combining marks
            for (int n = rng() % 8; n > 0; --n)
                d += utf8(0x300 + rng() % 0x30);
            break;
        case 5: // true colour with out-of-range components
            d += "\033[38;2;" + number(40000) + ';' + number(40000) + ';' + number(40000) + 'm';
            break;
        case 6: // 256 colours, out of range too
            d += "\033[38;5;" + number(2000) + 'm';
            break;
        case 7: // regional indicators: flags, and odd ones out
            for (int n = rng() % 6; n > 0; --n)
                d += utf8(0x1F1E6 + rng() % 26);
            break;
        case 8: // cursor save/restore, reset, index, insert mode
            d += pick({ "\033[s", "\033[u", "\0338", "\0337", "\033c", "\033D", "\033M", "\033E", "\033[4h", "\033[4l" });
            break;
        case 9: // plain text
            for (int a = rng() % 10; a > 0; --a)
                d += static_cast<char>(32 + rng() % 95);
            break;
        case 10: // any code point, wide ones included
            d += utf8(static_cast<char32_t>(rng() % 0x110000));
            break;
        default: // any byte, broken UTF-8 included
            d += static_cast<char>(rng() % 256);
            break;
        }
    }
    return d;
}

} // namespace

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    unsigned seed = 0;
    int rounds = 4000;
    QStringList files;
    bool haveSeed = false;

    for (int i = 1; i < argc; ++i) {
        const QByteArray arg = argv[i];
        if (arg.startsWith("--seed=")) {
            seed = arg.mid(7).toUInt();
            haveSeed = true;
        } else if (arg.startsWith("--rounds=")) {
            rounds = arg.mid(9).toInt();
        } else {
            files << QString::fromLocal8Bit(arg);
        }
    }

    if (!haveSeed && files.isEmpty()) {
        fprintf(stderr, "usage: %s --seed=N [--rounds=R] | FILE...\n", argv[0]);
        return 2;
    }

    for (const QString &path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            fprintf(stderr, "cannot read %s\n", qPrintable(path));
            return 2;
        }
        Terminal t;
        t.feed(f.readAll());
        t.readBack();
        fprintf(stderr, "replayed %s\n", qPrintable(path));
    }

    if (haveSeed) {
        std::mt19937 rng(seed);
        Terminal t;
        for (int i = 0; i < rounds; ++i) {
            t.feed(chunk(rng));
            if (i % 97 == 0)
                t.readBack();
        }
        t.readBack();
        fprintf(stderr, "seed %u: %d chunks\n", seed, rounds);
    }

    return 0;
}
