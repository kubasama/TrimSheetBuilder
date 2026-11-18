#include "gui/Theme.h"
#include "gui/DragValue.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QtGlobal>
#ifdef Q_OS_LINUX
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#endif
#include <QDir>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProcess>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QWidget>

#include <functional>

namespace {

// Combo boxes and spin boxes in scrolling panels shouldn't steal the wheel
// (and silently change values) unless they have keyboard focus.
class WheelGuard : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Polish || e->type() == QEvent::Wheel) {
            auto *w = qobject_cast<QWidget *>(o);
            if (w && (qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractSpinBox *>(w))) {
                if (e->type() == QEvent::Polish) {
                    w->setFocusPolicy(Qt::StrongFocus);
                    if (auto *spin = qobject_cast<QAbstractSpinBox *>(w))
                        DragValue::enable(spin);
                } else if (!w->hasFocus()) {
                    e->ignore();   // propagates to the parent (scroll area)
                    return true;
                }
            }
        }
        return QObject::eventFilter(o, e);
    }
};

class TrimStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption *opt, const QWidget *w,
                  QStyleHintReturn *ret) const override
    {
        if (hint == SH_DialogButtonBox_ButtonsHaveIcons)
            return 0;
        return QProxyStyle::styleHint(hint, opt, w, ret);
    }
};

QColor g_accent(0x2f, 0x86, 0xb8);
// The platform theme's highlight, captured before we replace the palette.
QColor g_platformHighlight;
bool g_paletteReplaced = false;

bool usable(const QColor &c)
{
    if (!c.isValid())
        return false;
    // reject near-grey and near-black/white: not an accent
    return c.hsvSaturation() > 40 && c.value() > 60;
}

#ifdef Q_OS_LINUX
// ---------------------------------------------------------------- portal
QColor portalAccent()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"), QStringLiteral("ReadOne"));
    msg << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("accent-color");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 400);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    QVariant v = reply.arguments().first();
    if (v.canConvert<QDBusVariant>())
        v = v.value<QDBusVariant>().variant();
    if (!v.canConvert<QDBusArgument>())
        return {};
    const QDBusArgument arg = v.value<QDBusArgument>();
    double r = -1, g = -1, b = -1;
    arg.beginStructure();
    arg >> r >> g >> b;
    arg.endStructure();
    if (r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1)
        return {}; // portal's "not set" value
    return QColor::fromRgbF(float(r), float(g), float(b));
}

// ---------------------------------------------------------------- GTK theme
QString gtkThemeName()
{
    QProcess p;
    p.start(QStringLiteral("gsettings"),
            {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
             QStringLiteral("gtk-theme")});
    if (p.waitForFinished(600) && p.exitCode() == 0) {
        QString name = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        name.remove(u'\'');
        if (!name.isEmpty())
            return name;
    }
    QSettings ini(QDir::homePath() + QStringLiteral("/.config/gtk-3.0/settings.ini"),
                  QSettings::IniFormat);
    return ini.value(QStringLiteral("Settings/gtk-theme-name")).toString();
}

QColor parseCssColor(QString v, const QHash<QString, QString> &defs, int depth = 0)
{
    v = v.trimmed();
    if (v.startsWith(u'@') && depth < 8)
        return parseCssColor(defs.value(v.mid(1)), defs, depth + 1);
    static const QRegularExpression rgb(
        QStringLiteral(R"(rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+))"));
    const auto m = rgb.match(v);
    if (m.hasMatch())
        return QColor(int(m.captured(1).toDouble()), int(m.captured(2).toDouble()),
                      int(m.captured(3).toDouble()));
    return QColor::fromString(v);
}

QColor gtkThemeAccent()
{
    const QString name = gtkThemeName();
    if (name.isEmpty())
        return {};
    const QStringList roots = {
        QDir::homePath() + QStringLiteral("/.local/share/themes"),
        QDir::homePath() + QStringLiteral("/.themes"),
        QStringLiteral("/usr/local/share/themes"),
        QStringLiteral("/usr/share/themes"),
    };
    for (const QString &root : roots) {
        const QDir dir(root + u'/' + name + QStringLiteral("/gtk-3.0"));
        if (!dir.exists())
            continue;
        // read gtk-dark.css, gtk.css and anything they @import from the same folder
        QStringList queue = {QStringLiteral("gtk-dark.css"), QStringLiteral("gtk.css")};
        QHash<QString, QString> defs;
        static const QRegularExpression defRe(
            QStringLiteral(R"(@define-color\s+([\w-]+)\s+([^;]+);)"));
        static const QRegularExpression importRe(
            QStringLiteral(R"(@import\s+url\(\s*["']?([^"')]+)["']?\s*\))"));
        for (int i = 0; i < queue.size() && i < 12; ++i) {
            QFile f(dir.filePath(queue[i]));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QString css = QString::fromUtf8(f.readAll());
            for (auto it = defRe.globalMatch(css); it.hasNext();) {
                const auto m = it.next();
                if (!defs.contains(m.captured(1)))
                    defs.insert(m.captured(1), m.captured(2));
            }
            for (auto it = importRe.globalMatch(css); it.hasNext();) {
                const QString file = it.next().captured(1);
                if (!file.contains(u':') && !queue.contains(file))
                    queue << file;
            }
        }
        for (const char *key : {"accent_bg_color", "accent_color", "theme_selected_bg_color",
                                "selected_bg_color"}) {
            const QColor c = parseCssColor(defs.value(QString::fromLatin1(key)), defs);
            if (usable(c))
                return c;
        }
    }
    return {};
}
#endif // Q_OS_LINUX

#ifdef Q_OS_WIN
// ---------------------------------------------------------------- Windows
QColor windowsAccent()
{
    // DWM accent color, stored as 0xAABBGGRR
    const QSettings dwm(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"),
                        QSettings::NativeFormat);
    bool ok = false;
    const auto v = quint32(dwm.value(QStringLiteral("AccentColor")).toLongLong(&ok));
    if (!ok)
        return {};
    return QColor(int(v & 0xff), int((v >> 8) & 0xff), int((v >> 16) & 0xff));
}
#endif

// ---------------------------------------------------------------- assets
QString writeAsset(const QString &name, const std::function<void(QPainter &)> &draw, int size = 32)
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/theme");
    QDir().mkpath(dir);
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    draw(p);
    p.end();
    const QString path = dir + u'/' + name;
    img.save(path, "PNG");
    return path;
}

QString styleSheet(const QColor &accent, const QColor &accentText, const QString &arrow,
                   const QString &check)
{
    const QString hover = accent.lighter(115).name();
    const QString pressed = accent.darker(115).name();
    QString css = QStringLiteral(R"(
QToolTip { background: #2b2b2b; color: #dcdcdc; border: 1px solid #3d3d3d; padding: 4px 6px; }

QMenuBar { background: #1b1b1b; padding: 2px 4px; border: none; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 4px; }
QMenuBar::item:selected { background: #333333; }
QMenu { background: #262626; border: 1px solid #3a3a3a; padding: 4px; }
QMenu::item { padding: 5px 28px 5px 22px; border-radius: 4px; }
QMenu::item:selected { background: @ACCENT; color: @ACCENTTEXT; }
QMenu::item:disabled { color: #707070; }
QMenu::separator { height: 1px; background: #3a3a3a; margin: 4px 8px; }

QStatusBar { background: #1b1b1b; color: #8c8c8c; }
QStatusBar::item { border: none; }
QSplitter::handle { background: #141414; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

QGroupBox {
    background: #282828; border: none; border-radius: 8px;
    margin-top: 0px; padding: 30px 10px 10px 10px; font-weight: bold;
}
QGroupBox::title {
    subcontrol-origin: padding; subcontrol-position: top left;
    left: 12px; top: 9px; color: #e6e6e6;
}
QGroupBox QLabel, QGroupBox QCheckBox { font-weight: normal; }
QLabel#hint { color: #8c8c8c; }
QToolButton#linkButton { padding: 2px; background: transparent; border: 1px solid transparent; }
QToolButton#linkButton:hover { background: #333333; }
QToolButton#linkButton:checked { background: @ACCENT; border-color: @ACCENT; }
QFrame#mapRow { background: #212121; border-radius: 6px; }
QFrame#compPanel { background: @COMPBG; border-radius: 6px; }
QToolButton#disclosure {
    padding: 0px; font-size: 11pt; color: #c8c8c8; background: transparent;
    border: none; border-radius: 3px;
}
QToolButton#disclosure:hover { color: #ffffff; background: #2c2c2c; }
QToolButton#disclosure:checked { color: @ACCENT; background: transparent; }
QToolButton#disclosure[hasContent="true"] { color: @ACCENT; }
QToolButton#disclosure:disabled { color: #3c3c3c; }
QFrame#compRow { background: #1d1f22; border-radius: 4px; }
QLabel#thumb { background: #1a1a1a; border: 1px solid #383838; border-radius: 4px; color: #666666; }
QWidget#exportPanel { background: #1f1f1f; }
QFrame#exportCard { background: @TINT; border: 1px solid @TINTBORDER; border-radius: 8px; }
QFrame#exportCard QLabel, QFrame#exportCard QCheckBox { background: transparent; }
QToolButton#sectionHeader {
    background: transparent; border: none; padding: 2px 0px; font-weight: bold; color: #e6e6e6;
}
QToolButton#sectionHeader:hover { color: #ffffff; background: transparent; }
QLabel#dim { color: #8c8c8c; font-weight: normal; }

QPushButton, QToolButton {
    background: #353535; color: #dcdcdc; border: 1px solid #404040;
    border-radius: 4px; padding: 4px 10px; font-weight: normal;
}
QPushButton:hover, QToolButton:hover { background: #3e3e3e; }
QPushButton:pressed, QToolButton:pressed { background: #2b2b2b; }
QPushButton:checked, QToolButton:checked { background: @ACCENT; color: @ACCENTTEXT; border-color: @ACCENT; }
QPushButton:disabled, QToolButton:disabled { background: #2c2c2c; color: #666666; border-color: #333333; }
QPushButton#primary {
    background: @ACCENT; color: @ACCENTTEXT; border: none; border-radius: 6px;
    padding: 10px; font-weight: bold; font-size: 11pt;
}
QPushButton#accentButton { background: @ACCENT; color: @ACCENTTEXT; border: 1px solid @ACCENT; }
QPushButton#accentButton:hover { background: @HOVER; border-color: @HOVER; }
QPushButton#accentButton:pressed { background: @PRESSED; border-color: @PRESSED; }
QPushButton#primary:hover { background: @HOVER; }
QPushButton#primary:pressed { background: @PRESSED; }

QLineEdit, QAbstractSpinBox, QComboBox {
    background: #1c1c1c; color: #dcdcdc; border: 1px solid #3a3a3a;
    border-radius: 4px; padding: 3px 6px; min-height: 18px;
    selection-background-color: @ACCENT; selection-color: @ACCENTTEXT; font-weight: normal;
}
QLineEdit:hover, QAbstractSpinBox:hover, QComboBox:hover { border-color: #4a4a4a; }
QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: @ACCENT; }
QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled { color: #666666; background: #222222; border-color: #2e2e2e; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 0px; border: none; }
QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow { image: none; width: 0px; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox::down-arrow { image: url(@ARROW); width: 10px; height: 10px; }
QComboBox QAbstractItemView {
    background: #262626; border: 1px solid #3a3a3a; outline: 0;
    selection-background-color: @ACCENT; selection-color: @ACCENTTEXT;
}

QCheckBox { spacing: 6px; }
QCheckBox::indicator {
    width: 14px; height: 14px; border: 1px solid #4a4a4a; border-radius: 3px; background: #1c1c1c;
}
QCheckBox::indicator:hover { border-color: #5a5a5a; }
QCheckBox::indicator:checked { background: @ACCENT; border-color: @ACCENT; image: url(@CHECK); }
QCheckBox:disabled { color: #666666; }
QRadioButton { spacing: 6px; }
QRadioButton::indicator {
    width: 14px; height: 14px; border: 1px solid #4a4a4a; border-radius: 8px; background: #1c1c1c;
}
QRadioButton::indicator:hover { border-color: #5a5a5a; }
QRadioButton::indicator:checked { border: 4px solid @ACCENT; background: #f0f0f0; width: 8px; height: 8px; }

QListWidget { background: #1c1c1c; border: 1px solid #3a3a3a; border-radius: 6px; outline: 0; padding: 4px; }
QListWidget::item { padding: 6px; border-radius: 4px; color: #dcdcdc; }
QListWidget::item:hover { background: #2c2c2c; }
QListWidget::item:selected { background: @ACCENT; color: @ACCENTTEXT; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: #3d3d3d; border-radius: 3px; margin: 2px; }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::handle:hover { background: #4b4b4b; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QDialog { background: #1f1f1f; }
QSlider::groove:horizontal { height: 4px; background: #3a3a3a; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @ACCENT; border-radius: 2px; }
QSlider::handle:horizontal {
    background: #e6e6e6; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px;
}
QSlider::handle:horizontal:hover { background: #ffffff; }
QSlider::sub-page:horizontal:disabled { background: #444444; }
QSlider::handle:horizontal:disabled { background: #555555; }
QTabWidget::pane { border: none; }
QTabBar::tab {
    background: transparent; color: #9a9a9a; padding: 6px 14px;
    border-radius: 4px; margin-right: 4px;
}
QTabBar::tab:hover { color: #e6e6e6; }
QTabBar::tab:selected { background: #333333; color: #e6e6e6; }

/* canvas overlays */
QToolButton[overlay="true"] {
    background: transparent; border: none; color: #d6d6d6;
    padding: 4px 11px; border-radius: 4px;
}
QToolButton[overlay="true"]:hover { background: rgba(255,255,255,22); }
QToolButton[overlay="true"]:checked { background: @ACCENT; color: @ACCENTTEXT; }
QToolButton[overlay="true"][compact="true"] {
    padding: 1px 7px; font-size: 8pt; color: #8f8f8f; border-radius: 3px;
}
QToolButton[overlay="true"][compact="true"]:hover { color: #d6d6d6; }
QToolButton[overlay="true"][compact="true"]:checked { background: @ACCENTSOFT; color: #f0f0f0; }
)");
    auto mix = [](const QColor &a, const QColor &b, double t) {
        return QColor::fromRgbF(float(a.redF() * t + b.redF() * (1 - t)),
                                float(a.greenF() * t + b.greenF() * (1 - t)),
                                float(a.blueF() * t + b.blueF() * (1 - t)));
    };
    css.replace(QStringLiteral("@TINTBORDER"), mix(accent, QColor(0x30, 0x30, 0x30), 0.28).name());
    css.replace(QStringLiteral("@TINT"), mix(accent, QColor(0x26, 0x26, 0x26), 0.11).name());
    css.replace(QStringLiteral("@COMPBG"), mix(accent, QColor(0x1b, 0x1b, 0x1b), 0.16).name());
    css.replace(QStringLiteral("@ACCENTTEXT"), accentText.name());
    css.replace(QStringLiteral("@ACCENTSOFT"), QStringLiteral("rgba(%1, %2, %3, 120)")
                                                   .arg(accent.red()).arg(accent.green()).arg(accent.blue()));
    css.replace(QStringLiteral("@ACCENT"), accent.name());
    css.replace(QStringLiteral("@HOVER"), hover);
    css.replace(QStringLiteral("@PRESSED"), pressed);
    css.replace(QStringLiteral("@ARROW"), arrow);
    css.replace(QStringLiteral("@CHECK"), check);
    return css;
}

} // namespace

// ================================================================ public
QColor Theme::detectSystemAccent(QString *source)
{
    auto found = [&](const QColor &c, const char *src) {
        if (source)
            *source = QString::fromLatin1(src);
        return c;
    };
    QColor c;
#ifdef Q_OS_LINUX
    c = portalAccent();
    if (usable(c))
        return found(c, "desktop portal");
    c = gtkThemeAccent();
    if (usable(c))
        return found(c, "GTK theme");
#elif defined(Q_OS_WIN)
    c = windowsAccent();
    if (usable(c))
        return found(c, "Windows accent color");
#endif
    // platform theme (e.g. gtk3) palette, if it's not just Fusion's default blue
    c = g_paletteReplaced ? g_platformHighlight
                          : QGuiApplication::palette().color(QPalette::Highlight);
    if (usable(c) && c != QColor(48, 140, 198))
        return found(c, "platform palette");
    return found(QColor(0x2f, 0x86, 0xb8), "default");
}

QColor Theme::accent() { return g_accent; }

QColor Theme::accentText()
{
    // relative luminance
    const double l = 0.2126 * g_accent.redF() + 0.7152 * g_accent.greenF()
                   + 0.0722 * g_accent.blueF();
    return l > 0.6 ? QColor(0x14, 0x14, 0x14) : QColor(Qt::white);
}

void Theme::apply(const QColor &accent)
{
    g_accent = accent.isValid() ? accent : QColor(0x2f, 0x86, 0xb8);
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return;

    if (!g_paletteReplaced) {
        g_platformHighlight = QGuiApplication::palette().color(QPalette::Highlight);
        g_paletteReplaced = true;
    }

    app->setStyle(new TrimStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    static WheelGuard *guard = nullptr;
    if (!guard) {
        guard = new WheelGuard(app);
        app->installEventFilter(guard);
    }

    QPalette p;
    p.setColor(QPalette::Window, window());
    p.setColor(QPalette::WindowText, text());
    p.setColor(QPalette::Base, field());
    p.setColor(QPalette::AlternateBase, card());
    p.setColor(QPalette::Text, text());
    p.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
    p.setColor(QPalette::ButtonText, text());
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, g_accent);
    p.setColor(QPalette::HighlightedText, accentText());
    p.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x2b, 0x2b));
    p.setColor(QPalette::ToolTipText, text());
    p.setColor(QPalette::Link, g_accent.lighter(130));
    p.setColor(QPalette::PlaceholderText, QColor(0x70, 0x70, 0x70));
    p.setColor(QPalette::Light, QColor(0x45, 0x45, 0x45));
    p.setColor(QPalette::Midlight, QColor(0x3a, 0x3a, 0x3a));
    p.setColor(QPalette::Mid, QColor(0x2a, 0x2a, 0x2a));
    p.setColor(QPalette::Dark, QColor(0x1a, 0x1a, 0x1a));
    p.setColor(QPalette::Shadow, QColor(0x0a, 0x0a, 0x0a));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(0x66, 0x66, 0x66));
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x44, 0x44, 0x44));
    app->setPalette(p);

    const QColor fg = text();
    const QString arrow = writeAsset(QStringLiteral("arrow-down.png"), [fg](QPainter &pt) {
        QPen pen(fg, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pt.setPen(pen);
        pt.drawPolyline(QPolygonF({{8, 12}, {16, 20}, {24, 12}}));
    });
    const QColor on = accentText();
    const QString check = writeAsset(QStringLiteral("check.png"), [on](QPainter &pt) {
        QPen pen(on, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pt.setPen(pen);
        pt.drawPolyline(QPolygonF({{7, 17}, {13, 23}, {25, 9}}));
    });
    app->setStyleSheet(styleSheet(g_accent, accentText(), arrow, check));

    for (QWidget *w : QApplication::allWidgets())
        w->update();
}
