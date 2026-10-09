#include "qmltermwidget_plugin.h"

#include "TerminalDisplay.h"
#include "ksession.h"

#include <qqml.h>
#include <QQmlEngine>
#include <QDir>
#include <QFileInfo>

#include <dlfcn.h>

#include "tools.h"

using namespace Konsole;

// The directory this plugin was loaded from, which is the module's
static QString moduleDirectory()
{
    static const char anchor = 0;
    Dl_info info{};

    if (dladdr(&anchor, &info) == 0 || info.dli_fname == nullptr)
        return QString();

    return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath();
}

void QmltermwidgetPlugin::registerTypes(const char *uri)
{
    // @uri org.qterminal.qmlterminal
    qmlRegisterType<TerminalDisplay>(uri, 2, 0, "QMLTermWidget");
    qmlRegisterType<KSession>(uri, 2, 0, "QMLTermSession");
}

void QmltermwidgetPlugin::initializeEngine(QQmlEngine *engine, const char *uri)
{
    QQmlExtensionPlugin::initializeEngine(engine, uri);

    // The colour schemes and keyboard layouts are installed next to the
    // plugin. KB_LAYOUT_DIR and COLORSCHEMES_DIR still override them, but
    // the plugin no longer rewrites the environment of the whole process
    // (or points it at a directory that does not exist).
    const QString moduleDir = moduleDirectory();
    if (moduleDir.isEmpty())
        return;

    add_custom_color_scheme_dir(moduleDir + QStringLiteral("/color-schemes"));
    set_kb_layout_dir(moduleDir + QStringLiteral("/kb-layouts"));
}
