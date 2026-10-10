#include "tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QtDebug>


Q_LOGGING_CATEGORY(qtermwidgetLogger, "qtermwidget", QtWarningMsg)

/*! Helper function to get possible location of layout files.
By default the KB_LAYOUT_DIR is used (linux/BSD/macports).
But in some cases (apple bundle) there can be more locations).
*/
namespace {
    QString kb_layout_dir;
}
void set_kb_layout_dir(const QString& dir)
{
    kb_layout_dir = dir;
}

QString get_kb_layout_dir()
{
//    qDebug() << __FILE__ << __FUNCTION__;

    // An unset variable is "", which QDir takes for the current directory
    QString k = qEnvironmentVariable("KB_LAYOUT_DIR");
    if (!k.isEmpty() && QDir(k).exists())
        return k.append(QLatin1Char('/'));

    QDir d(kb_layout_dir);
    if (!kb_layout_dir.isEmpty() && d.exists())
        return kb_layout_dir + QLatin1Char('/');

#ifdef Q_OS_MAC
    // subdir in the app location
    d.setPath(QCoreApplication::applicationDirPath() + QLatin1String("/kb-layouts/"));
    //qDebug() << d.path();
    if (d.exists())
        return QCoreApplication::applicationDirPath() + QLatin1String("/kb-layouts/");

    d.setPath(QCoreApplication::applicationDirPath() + QLatin1String("/../Resources/kb-layouts/"));
    if (d.exists())
        return QCoreApplication::applicationDirPath() + QLatin1String("/../Resources/kb-layouts/");
#endif
    //qDebug() << "Cannot find KB_LAYOUT_DIR. Default:" << k;
    return QString();
}

/*! Helper function to add custom location of color schemes.
*/
namespace {
    QStringList custom_color_schemes_dirs;
}
void add_custom_color_scheme_dir(const QString& custom_dir)
{
    if (!custom_color_schemes_dirs.contains(custom_dir))
        custom_color_schemes_dirs << custom_dir;
}

/*! Helper function to get possible locations of color schemes.
By default the COLORSCHEMES_DIR is used (linux/BSD/macports).
But in some cases (apple bundle) there can be more locations).
*/
const QStringList get_color_schemes_dirs()
{
//    qDebug() << __FILE__ << __FUNCTION__;

    QStringList rval;
    QString k = qEnvironmentVariable("COLORSCHEMES_DIR");
    QDir d(k);

//    qDebug() << "default COLORSCHEMES_DIR: " << k;

    // An unset variable is "", which QDir takes for the current directory
    if (!k.isEmpty() && d.exists())
        rval << k.append(QLatin1Char('/'));

#ifdef Q_OS_MAC
    // subdir in the app location
    d.setPath(QCoreApplication::applicationDirPath() + QLatin1String("/color-schemes/"));
    //qDebug() << d.path();
    if (d.exists())
    {
        if (!rval.isEmpty())
            rval.clear();
        rval << (QCoreApplication::applicationDirPath() + QLatin1String("/color-schemes/"));
    }
    d.setPath(QCoreApplication::applicationDirPath() + QLatin1String("/../Resources/color-schemes/"));
    if (d.exists())
    {
        if (!rval.isEmpty())
            rval.clear();
        rval << (QCoreApplication::applicationDirPath() + QLatin1String("/../Resources/color-schemes/"));
    }
#endif

    for (const QString& custom_dir : std::as_const(custom_color_schemes_dirs))
    {
        d.setPath(custom_dir);
        if (d.exists())
            rval << custom_dir;
    }
#ifdef QT_DEBUG
    if(!rval.isEmpty()) {
        qDebug() << "Using color-schemes: " << rval;
    } else {
        qDebug() << "Cannot find color-schemes in any location!";
    }
#endif
    return rval;
}
