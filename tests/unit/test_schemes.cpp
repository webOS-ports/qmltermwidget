// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Colour schemes and keyboard layouts are files, picked by a name that QML
 * sets (colorScheme, kbScheme).
 */

#include "check.h"

#include "ColorScheme.h"
#include "KeyboardTranslator.h"
#include "tools.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace Konsole;

namespace
{

/*
 * The scheme directory of this run. The manager is a process-wide singleton
 * that looks names up in the first directory it knows of, so every scheme
 * test shares this one: <tmp>/schemes, with <tmp> itself outside it.
 */
QTemporaryDir &schemeRoot()
{
    static QTemporaryDir root;
    static bool registered = false;
    if (!registered) {
        QDir(root.path()).mkdir(QStringLiteral("schemes"));
        qunsetenv("COLORSCHEMES_DIR");
        add_custom_color_scheme_dir(root.path() + QStringLiteral("/schemes"));
        registered = true;
    }
    return root;
}

QString schemeDir()
{
    return schemeRoot().path() + QStringLiteral("/schemes");
}

void writeFile(const QString &path, const QByteArray &content)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(content);
}

QColor colorOf(const ColorScheme *scheme, int index)
{
    ColorEntry table[TABLE_COLORS];
    scheme->getColorTable(table);
    return table[index].color;
}

const QByteArray validScheme =
    "[General]\nDescription=test\n\n[Foreground]\nColor=0,255,0\n\n[Background]\nColor=10,20,30\n";

} // namespace

/*
 * The warning for an invalid colour printed a QStringView of a temporary
 * that was already freed (ASan: heap-use-after-free). The colour itself
 * falls back to black.
 */
TEST(ColorScheme, invalidColourFallsBackToBlack)
{
    writeFile(schemeDir() + QStringLiteral("/broken.colorscheme"),
              "[General]\nDescription=broken\n\n[Foreground]\nColor=1,2\n\n[Background]\nColor=300,0,0\n");

    const ColorScheme *scheme = ColorSchemeManager::instance()->findColorScheme(QStringLiteral("broken"));
    CHECK(scheme != nullptr);
    if (!scheme)
        return;

    CHECK(colorOf(scheme, 0) == QColor(0, 0, 0));
    CHECK(colorOf(scheme, 1) == QColor(0, 0, 0));
}

/*
 * "my.dotted.colorscheme" was registered as "my", so looking up "my.dotted"
 * loaded it again and recursed until the stack ran out.
 */
TEST(ColorScheme, dottedNamesLoad)
{
    writeFile(schemeDir() + QStringLiteral("/my.dotted.colorscheme"), validScheme);

    const ColorScheme *scheme = ColorSchemeManager::instance()->findColorScheme(QStringLiteral("my.dotted"));
    CHECK(scheme != nullptr);
    if (scheme) {
        CHECK_EQ(scheme->name(), QStringLiteral("my.dotted"));
        CHECK(colorOf(scheme, 1) == QColor(10, 20, 30));
    }
}

// The name picks a file in the scheme directory; it is not a path
TEST(ColorScheme, namesAreNotPaths)
{
    writeFile(schemeRoot().path() + QStringLiteral("/outside.colorscheme"), validScheme);

    ColorSchemeManager *manager = ColorSchemeManager::instance();
    CHECK(manager->findColorScheme(QStringLiteral("../outside")) == nullptr);
    // nor was the file outside loaded on the way, under its own name
    CHECK(manager->findColorScheme(QStringLiteral("outside")) == nullptr);
}

/*
 * Without a default.keytab the built-in fallback was built anew, and
 * leaked, on every lookup.
 */
TEST(Keytab, fallbackIsBuiltOnce)
{
    QTemporaryDir empty;
    qunsetenv("KB_LAYOUT_DIR");
    set_kb_layout_dir(empty.path());

    KeyboardTranslatorManager *manager = KeyboardTranslatorManager::instance();
    const KeyboardTranslator *first = manager->defaultTranslator();
    const KeyboardTranslator *second = manager->defaultTranslator();

    CHECK(first != nullptr);
    CHECK(first == second);

    set_kb_layout_dir(QStringLiteral(TESTS_SOURCE_DIR "/../lib/kb-layouts"));
}

// kbScheme picks a file in the layout directory; it is not a path
TEST(Keytab, namesAreNotPaths)
{
    qunsetenv("KB_LAYOUT_DIR");
    set_kb_layout_dir(QStringLiteral(TESTS_SOURCE_DIR "/../lib/kb-layouts"));

    KeyboardTranslatorManager *manager = KeyboardTranslatorManager::instance();
    CHECK(manager->findTranslator(QStringLiteral("default")) != nullptr);
    CHECK(manager->findTranslator(QStringLiteral("../kb-layouts/linux")) == nullptr);
}

/*
 * An unset KB_LAYOUT_DIR is an empty string, which QDir took for the
 * current directory: keytabs were then looked for in "/".
 */
TEST(Keytab, unsetVariableIsNotTheCurrentDirectory)
{
    qunsetenv("KB_LAYOUT_DIR");
    set_kb_layout_dir(QString());

    CHECK_EQ(get_kb_layout_dir(), QString());

    set_kb_layout_dir(QStringLiteral(TESTS_SOURCE_DIR "/../lib/kb-layouts"));
}
