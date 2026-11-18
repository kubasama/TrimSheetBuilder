#include "gui/Dialogs.h"
#include "gui/ChannelEditor.h" // ColorButton
#include "gui/AppIcon.h"
#include "gui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>

// ================================================================ preferences
AppPreferences AppPreferences::load()
{
    QSettings s;
    AppPreferences p;
    const QString accent = s.value(QStringLiteral("appearance/accent")).toString();
    if (!accent.isEmpty())
        p.customAccent = QColor(accent);
    p.showGrid = s.value(QStringLiteral("canvas/showGrid"), true).toBool();
    p.showZoneNames = s.value(QStringLiteral("canvas/showZoneNames"), true).toBool();
    return p;
}

void AppPreferences::save() const
{
    QSettings s;
    if (customAccent.isValid())
        s.setValue(QStringLiteral("appearance/accent"), customAccent.name());
    else
        s.remove(QStringLiteral("appearance/accent"));
    s.setValue(QStringLiteral("canvas/showGrid"), showGrid);
    s.setValue(QStringLiteral("canvas/showZoneNames"), showZoneNames);
}

PreferencesDialog::PreferencesDialog(const AppPreferences &prefs,
                                     const QVector<ShortcutEntry> &shortcuts, QWidget *parent)
    : QDialog(parent), m_shortcuts(shortcuts)
{
    setWindowTitle(tr("Preferences"));
    resize(560, 560);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneralTab(prefs), tr("General"));
    tabs->addTab(buildShortcutsTab(), tr("Keyboard Shortcuts"));

    auto *buttons = new QDialogButtonBox(this);
    auto *ok = new QPushButton(tr("OK"));
    ok->setObjectName(QStringLiteral("primary"));
    ok->setMinimumWidth(110);
    ok->setDefault(true);
    buttons->addButton(ok, QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addWidget(tabs, 1);
    root->addWidget(buttons);
}

QWidget *PreferencesDialog::buildGeneralTab(const AppPreferences &prefs)
{
    auto *page = new QWidget(this);

    // ---- appearance
    auto *look = new QGroupBox(tr("Appearance"), page);
    auto *lookLay = new QVBoxLayout(look);
    m_systemAccent = new QRadioButton(tr("Use the desktop's accent color"), look);
    m_detected = new QLabel(look);
    m_detected->setObjectName(QStringLiteral("dim"));
    m_detected->setContentsMargins(24, 0, 0, 4);
    m_customAccent = new QRadioButton(tr("Custom accent color"), look);
    m_accentColor = new ColorButton(look);
    auto *customRow = new QHBoxLayout;
    customRow->addWidget(m_customAccent);
    customRow->addWidget(m_accentColor);
    customRow->addStretch(1);
    lookLay->addWidget(m_systemAccent);
    lookLay->addWidget(m_detected);
    lookLay->addLayout(customRow);

    QString source;
    const QColor detected = Theme::detectSystemAccent(&source);
    m_detected->setText(tr("Detected: %1 (from %2)").arg(detected.name(), source));
    m_accentColor->setColor(prefs.customAccent.isValid() ? prefs.customAccent : detected);
    (prefs.customAccent.isValid() ? m_customAccent : m_systemAccent)->setChecked(true);
    m_accentColor->setEnabled(prefs.customAccent.isValid());
    connect(m_customAccent, &QRadioButton::toggled, m_accentColor, &QWidget::setEnabled);

    // ---- canvas
    auto *canvas = new QGroupBox(tr("Canvas"), page);
    auto *canvasLay = new QVBoxLayout(canvas);
    m_grid = new QCheckBox(tr("Show grid in layout mode"), canvas);
    m_names = new QCheckBox(tr("Show zone names"), canvas);
    m_grid->setChecked(prefs.showGrid);
    m_names->setChecked(prefs.showZoneNames);
    canvasLay->addWidget(m_grid);
    canvasLay->addWidget(m_names);

    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 8, 0, 0);
    lay->addWidget(look);
    lay->addWidget(canvas);
    lay->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildShortcutsTab()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 8, 6, 0);

    QString category;
    QFormLayout *form = nullptr;
    for (int i = 0; i < m_shortcuts.size(); ++i) {
        const ShortcutEntry &e = m_shortcuts[i];
        if (e.category != category || !form) {
            category = e.category;
            auto *box = new QGroupBox(category, inner);
            form = new QFormLayout(box);
            lay->addWidget(box);
        }
        auto *edit = new QKeySequenceEdit(e.current.isEmpty() ? QKeySequence() : e.current.first(),
                                          inner);
        edit->setClearButtonEnabled(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        edit->setMaximumSequenceLength(1);
#endif
        auto *reset = new QToolButton(inner);
        reset->setText(QStringLiteral("↺"));
        reset->setToolTip(tr("Reset to default (%1)")
                              .arg(e.defaults.isEmpty()
                                       ? tr("none")
                                       : QKeySequence::listToString(e.defaults, QKeySequence::NativeText)));
        const QList<QKeySequence> defaults = e.defaults;
        connect(reset, &QToolButton::clicked, edit, [edit, defaults] {
            edit->setKeySequence(defaults.isEmpty() ? QKeySequence() : defaults.first());
        });
        auto *row = new QHBoxLayout;
        row->addWidget(edit, 1);
        row->addWidget(reset);
        auto *label = new QLabel(e.label, inner);
        label->setMinimumWidth(170); // align columns across groups
        form->addRow(label, row);
        m_editors.append(edit);
    }
    lay->addStretch(1);

    auto *resetAll = new QPushButton(tr("Reset All to Defaults"));
    connect(resetAll, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_editors.size(); ++i)
            m_editors[i]->setKeySequence(m_shortcuts[i].defaults.isEmpty()
                                             ? QKeySequence()
                                             : m_shortcuts[i].defaults.first());
    });

    auto *scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *page = new QWidget(this);
    auto *pageLay = new QVBoxLayout(page);
    pageLay->setContentsMargins(0, 0, 0, 0);
    auto *hint = new QLabel(tr("Click a field and press the new key combination."), page);
    hint->setObjectName(QStringLiteral("hint"));
    pageLay->addWidget(hint);
    pageLay->addWidget(scroll, 1);
    auto *bottom = new QHBoxLayout;
    bottom->addStretch(1);
    bottom->addWidget(resetAll);
    pageLay->addLayout(bottom);
    return page;
}

QVector<ShortcutEntry> PreferencesDialog::shortcuts() const
{
    QVector<ShortcutEntry> out = m_shortcuts;
    for (int i = 0; i < out.size(); ++i) {
        const QKeySequence seq = m_editors[i]->keySequence();
        const QKeySequence was = out[i].current.isEmpty() ? QKeySequence() : out[i].current.first();
        if (seq == was)
            continue; // untouched: keep any extra alternates
        // back on the default primary key: restore the full default list
        if (!out[i].defaults.isEmpty() && seq == out[i].defaults.first())
            out[i].current = out[i].defaults;
        else
            out[i].current = seq.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{seq};
    }
    return out;
}

void PreferencesDialog::accept()
{
    // refuse duplicate shortcuts
    QHash<QString, QString> used;
    const auto list = shortcuts();
    for (const ShortcutEntry &e : list)
        for (const QKeySequence &k : e.current) {
            const QString key = k.toString(QKeySequence::PortableText);
            if (key.isEmpty())
                continue;
            if (used.contains(key)) {
                QMessageBox::warning(this, tr("Keyboard Shortcuts"),
                                     tr("%1 is used by both \"%2\" and \"%3\".")
                                         .arg(k.toString(QKeySequence::NativeText), used.value(key),
                                              e.label));
                return;
            }
            used.insert(key, e.label);
        }
    QDialog::accept();
}

AppPreferences PreferencesDialog::preferences() const
{
    AppPreferences p;
    p.customAccent = m_customAccent->isChecked() ? m_accentColor->color() : QColor();
    p.showGrid = m_grid->isChecked();
    p.showZoneNames = m_names->isChecked();
    return p;
}

// ================================================================ about
namespace {
class IconWidget : public QWidget
{
public:
    explicit IconWidget(QWidget *parent) : QWidget(parent) { setFixedSize(76, 76); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        AppIcon::paint(p, QRectF(rect()));
    }
};
} // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About Trim Sheet Builder"));
    setMinimumWidth(420);

    auto *title = new QLabel(QStringLiteral("<span style='font-size:18pt; font-weight:bold'>Trim Sheet Builder</span>"
                                            "<br><span style='color:#8c8c8c'>%1 %2</span>")
                                 .arg(tr("Version"), QApplication::applicationVersion()),
                             this);
    auto *head = new QHBoxLayout;
    head->addWidget(new IconWidget(this));
    head->addSpacing(12);
    head->addWidget(title, 1);

    auto *body = new QLabel(this);
    body->setWordWrap(true);
    body->setTextFormat(Qt::RichText);
    body->setText(tr("<p>Build texture trim sheets: lay out zones, fill them with tiling "
                     "textures per channel, and export maps ready for your engine.</p>"
                     "<p>Free and open source under the MIT License.</p>"
                     "<p style='color:#8c8c8c'>Built with Qt %1 (running on %2) · libzip</p>")
                      .arg(QStringLiteral(QT_VERSION_STR), QString::fromLatin1(qVersion())));

    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->addLayout(head);
    root->addWidget(body);
    root->addWidget(buttons);
}

// ================================================================ save prompt
SaveAnswer askToSave(QWidget *parent, const QString &documentName)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QApplication::applicationDisplayName());
    dlg.setModal(true);

    auto *icon = new QLabel(&dlg);
    const int s = 48;
    icon->setPixmap(dlg.style()->standardIcon(QStyle::SP_MessageBoxWarning, nullptr, &dlg)
                        .pixmap(s, s));
    icon->setAlignment(Qt::AlignTop);

    auto *text = new QLabel(&dlg);
    text->setTextFormat(Qt::RichText);
    text->setText(QObject::tr("The document <b>'%1'</b> has been modified.<br><br>"
                              "Do you want to save it?")
                      .arg(documentName.toHtmlEscaped()));

    auto *cancel = new QPushButton(QObject::tr("&Cancel"), &dlg);
    auto *no = new QPushButton(QObject::tr("&No"), &dlg);
    auto *yes = new QPushButton(QObject::tr("&Yes"), &dlg);
    yes->setObjectName(QStringLiteral("accentButton"));
    yes->setDefault(true);
    for (QPushButton *b : {cancel, no, yes})
        b->setMinimumWidth(90);

    SaveAnswer answer = SaveAnswer::Cancel; // also Esc / closing the window
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(no, &QPushButton::clicked, &dlg, [&] { answer = SaveAnswer::No; dlg.accept(); });
    QObject::connect(yes, &QPushButton::clicked, &dlg, [&] { answer = SaveAnswer::Yes; dlg.accept(); });

    auto *top = new QHBoxLayout;
    top->setSpacing(16);
    top->addWidget(icon);
    top->addWidget(text, 1);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(no);
    buttons->addWidget(yes);
    auto *root = new QVBoxLayout(&dlg);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(18);
    root->addLayout(top);
    root->addLayout(buttons);

    yes->setFocus();
    dlg.setFixedSize(dlg.sizeHint());
    dlg.exec();
    return answer;
}
