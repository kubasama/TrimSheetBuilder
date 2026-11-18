#include "gui/AppIcon.h"
#include "gui/MainWindow.h"
#include "gui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TrimSheet"));
    QApplication::setApplicationVersion(QStringLiteral(TRIMSHEET_VERSION));
    QApplication::setOrganizationName(QStringLiteral("TrimSheet"));
    QApplication::setApplicationDisplayName(QStringLiteral("Trim Sheet Builder"));
    // Wayland app id: must match the .desktop file name for the right icon.
    QApplication::setDesktopFileName(QStringLiteral("trimsheet"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("trimsheet"), AppIcon::icon()));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Trim sheet builder"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("A .trimsheet file to open"));
    parser.process(app);

    // Accent: user override, else whatever the desktop uses.
    // (Detect before Theme::apply replaces the platform palette.)
    const QString saved = QSettings().value(QStringLiteral("appearance/accent")).toString();
    const QColor accent = saved.isEmpty() ? Theme::detectSystemAccent() : QColor(saved);
    Theme::apply(accent);

    MainWindow w;
    if (!parser.positionalArguments().isEmpty())
        w.openFile(parser.positionalArguments().first());
    w.show();
    return app.exec();
}
