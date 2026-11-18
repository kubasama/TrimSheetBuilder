#include "gui/ChannelEditor.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QVBoxLayout>

// ================================================================ preset store
namespace {
const char *kPresetKey = "channelPresets";

QString kindLabel(ChannelKind k)
{
    switch (k) {
    case ChannelKind::Color:     return QObject::tr("Color (sRGB)");
    case ChannelKind::Data:      return QObject::tr("Data (linear RGB)");
    case ChannelKind::Normal:    return QObject::tr("Normal map");
    case ChannelKind::Grayscale: return QObject::tr("Grayscale");
    }
    return {};
}
} // namespace

QVector<ChannelPresets::Preset> ChannelPresetStore::custom()
{
    QVector<ChannelPresets::Preset> out;
    const QJsonArray arr =
        QJsonDocument::fromJson(QSettings().value(QString::fromLatin1(kPresetKey)).toByteArray())
            .array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        ChannelPresets::Preset p;
        p.name = o["name"].toString();
        p.channels = ChannelPresets::fromJson(o["channels"].toArray());
        p.builtIn = false;
        if (!p.name.isEmpty() && !p.channels.isEmpty())
            out.append(p);
    }
    return out;
}

QVector<ChannelPresets::Preset> ChannelPresetStore::all()
{
    return ChannelPresets::builtIns() + custom();
}

static void writeCustom(const QVector<ChannelPresets::Preset> &presets)
{
    QJsonArray arr;
    for (const auto &p : presets)
        arr.append(QJsonObject{{"name", p.name}, {"channels", ChannelPresets::toJson(p.channels)}});
    QSettings().setValue(QString::fromLatin1(kPresetKey), QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void ChannelPresetStore::save(const QString &name, const QVector<Channel> &channels)
{
    auto list = custom();
    bool replaced = false;
    for (auto &p : list)
        if (p.name == name) {
            p.channels = channels;
            replaced = true;
        }
    if (!replaced)
        list.append({name, channels, false});
    writeCustom(list);
}

void ChannelPresetStore::remove(const QString &name)
{
    auto list = custom();
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const auto &p) { return p.name == name; }),
               list.end());
    writeCustom(list);
}

// ================================================================ ColorButton
ColorButton::ColorButton(QWidget *parent)
    : QToolButton(parent)
{
    setMinimumSize(56, 24);
    setCursor(Qt::PointingHandCursor);
    connect(this, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, tr("Default Color"),
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid())
            setColor(c);
    });
}

void ColorButton::setColor(const QColor &c)
{
    if (c == m_color)
        return;
    m_color = c;
    update();
    emit colorChanged(c);
}

void ColorButton::paintEvent(QPaintEvent *e)
{
    QToolButton::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(0, 0, 0, 120));
    p.setBrush(m_color);
    p.drawRoundedRect(QRectF(rect()).adjusted(5, 5, -5, -5), 3, 3);
}

// ================================================================ ChannelListEditor
ChannelListEditor::ChannelListEditor(QWidget *parent)
    : QWidget(parent)
{
    // left: list + buttons
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(200);
    auto *add = new QPushButton(tr("Add"), this);
    m_remove = new QPushButton(tr("Remove"), this);
    m_up = new QPushButton(QStringLiteral("↑"), this);
    m_down = new QPushButton(QStringLiteral("↓"), this);
    m_up->setToolTip(tr("Move up"));
    m_down->setToolTip(tr("Move down"));
    auto *btns = new QHBoxLayout;
    btns->setSpacing(4);
    btns->addWidget(add);
    btns->addWidget(m_remove);
    btns->addStretch(1);
    btns->addWidget(m_up);
    btns->addWidget(m_down);
    auto *left = new QVBoxLayout;
    left->addWidget(m_list, 1);
    left->addLayout(btns);

    // right: properties of the selected channel
    m_form = new QWidget(this);
    auto *form = new QFormLayout(m_form);
    form->setContentsMargins(0, 0, 0, 0);
    m_name = new QLineEdit(m_form);
    m_kind = new QComboBox(m_form);
    for (ChannelKind k : {ChannelKind::Color, ChannelKind::Data, ChannelKind::Normal,
                          ChannelKind::Grayscale})
        m_kind->addItem(kindLabel(k), int(k));
    m_kind->setToolTip(tr("Color: sRGB albedo / emissive\n"
                          "Data: linear RGB, e.g. packed ORM or masks\n"
                          "Normal map: rotated/flipped correctly per zone\n"
                          "Grayscale: one channel (roughness, height…), saved as 8-bit gray"));
    m_suffix = new QLineEdit(m_form);
    m_suffix->setToolTip(tr("Appended to the export name: MySheet + _BC = MySheet_BC.png"));
    m_default = new ColorButton(m_form);
    m_default->setToolTip(tr("Fills empty areas and zones without a texture for this channel"));
    m_conventionLabel = new QLabel(tr("Normal format"), m_form);
    m_convention = new QComboBox(m_form);
    m_convention->addItem(tr("DirectX (Y−)"), int(NormalConvention::DirectX));
    m_convention->addItem(tr("OpenGL (Y+)"), int(NormalConvention::OpenGL));
    m_convention->setToolTip(tr("Format of the exported normal map. Sources are converted."));
    m_export = new QCheckBox(tr("Export this channel"), m_form);
    m_alpha = new QCheckBox(tr("Alpha channel (RGBA)"), m_form);
    m_alpha->setToolTip(tr("Keep transparency and allow packing a texture into A. Exports RGBA PNG."));
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Type"), m_kind);
    form->addRow(tr("File suffix"), m_suffix);
    form->addRow(tr("Empty color"), m_default);
    form->addRow(m_conventionLabel, m_convention);
    form->addRow(QString(), m_alpha);
    form->addRow(QString(), m_export);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addLayout(left, 2);
    root->addSpacing(10);
    root->addWidget(m_form, 3, Qt::AlignTop);

    connect(m_list, &QListWidget::currentRowChanged, this, &ChannelListEditor::showCurrent);
    connect(add, &QPushButton::clicked, this, [this] {
        Channel c = ChannelPresets::make(tr("New Channel"), ChannelKind::Grayscale,
                                         QString(), m_channels);
        c.suffix = QStringLiteral("_") + c.id;
        m_channels.append(c);
        rebuildList();
        m_list->setCurrentRow(int(m_channels.size()) - 1);
        m_name->setFocus();
        m_name->selectAll();
        emit changed();
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const int i = current();
        if (i < 0 || m_channels.size() <= 1)
            return;
        m_channels.removeAt(i);
        rebuildList();
        m_list->setCurrentRow(qMin(i, int(m_channels.size()) - 1));
        emit changed();
    });
    auto moveBy = [this](int d) {
        const int i = current(), j = i + d;
        if (i < 0 || j < 0 || j >= m_channels.size())
            return;
        m_channels.swapItemsAt(i, j);
        rebuildList();
        m_list->setCurrentRow(j);
        emit changed();
    };
    connect(m_up, &QPushButton::clicked, this, [moveBy] { moveBy(-1); });
    connect(m_down, &QPushButton::clicked, this, [moveBy] { moveBy(1); });

    connect(m_name, &QLineEdit::textEdited, this, &ChannelListEditor::editCurrent);
    connect(m_suffix, &QLineEdit::textEdited, this, &ChannelListEditor::editCurrent);
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating)
            return;
        const int i = current();
        if (i < 0)
            return;
        // switching type also switches to that type's usual empty color
        m_updating = true;
        m_default->setColor(ChannelPresets::defaultColorFor(ChannelKind(m_kind->currentData().toInt())));
        m_updating = false;
        editCurrent();
    });
    connect(m_default, &ColorButton::colorChanged, this, &ChannelListEditor::editCurrent);
    connect(m_convention, &QComboBox::currentIndexChanged, this, &ChannelListEditor::editCurrent);
    connect(m_export, &QCheckBox::toggled, this, &ChannelListEditor::editCurrent);
    connect(m_alpha, &QCheckBox::toggled, this, &ChannelListEditor::editCurrent);
}

int ChannelListEditor::current() const
{
    const int r = m_list->currentRow();
    return r >= 0 && r < m_channels.size() ? r : -1;
}

void ChannelListEditor::setChannels(const QVector<Channel> &channels)
{
    m_channels = channels;
    rebuildList();
    m_list->setCurrentRow(m_channels.isEmpty() ? -1 : 0);
}

void ChannelListEditor::rebuildList()
{
    const QSignalBlocker b(m_list);
    const int row = m_list->currentRow();
    m_list->clear();
    for (const Channel &c : std::as_const(m_channels)) {
        auto *it = new QListWidgetItem(
            QStringLiteral("%1\n%2 · %3%4").arg(c.name, kindLabel(c.kind), c.fileSuffix(),
                                               c.exportEnabled ? QString() : tr(" · not exported")),
            m_list);
        QPixmap sw(14, 14);
        sw.fill(c.defaultColor);
        it->setIcon(QIcon(sw));
    }
    m_list->setCurrentRow(qMin(row, m_list->count() - 1));
    showCurrent();
}

void ChannelListEditor::showCurrent()
{
    const int i = current();
    m_form->setEnabled(i >= 0);
    m_remove->setEnabled(i >= 0 && m_channels.size() > 1);
    m_up->setEnabled(i > 0);
    m_down->setEnabled(i >= 0 && i < m_channels.size() - 1);
    if (i < 0)
        return;
    const Channel &c = m_channels[i];
    m_updating = true;
    m_name->setText(c.name);
    m_kind->setCurrentIndex(m_kind->findData(int(c.kind)));
    m_suffix->setText(c.suffix);
    m_suffix->setPlaceholderText(QStringLiteral("_") + c.id);
    m_default->setColor(c.defaultColor);
    m_convention->setCurrentIndex(m_convention->findData(int(c.convention)));
    m_convention->setVisible(c.isNormal());
    m_conventionLabel->setVisible(c.isNormal());
    m_export->setChecked(c.exportEnabled);
    m_alpha->setChecked(c.alpha);
    m_alpha->setVisible(c.kind == ChannelKind::Color || c.kind == ChannelKind::Data);
    m_updating = false;
}

void ChannelListEditor::editCurrent()
{
    const int i = current();
    if (m_updating || i < 0)
        return;
    Channel &c = m_channels[i];
    c.name = m_name->text();
    c.kind = ChannelKind(m_kind->currentData().toInt());
    c.suffix = m_suffix->text().trimmed();
    c.defaultColor = m_default->color();
    c.convention = NormalConvention(m_convention->currentData().toInt());
    c.exportEnabled = m_export->isChecked();
    c.alpha = (c.kind == ChannelKind::Color || c.kind == ChannelKind::Data) && m_alpha->isChecked();
    m_alpha->setVisible(c.kind == ChannelKind::Color || c.kind == ChannelKind::Data);
    // keep the list text in sync without losing focus
    if (QListWidgetItem *it = m_list->item(i)) {
        it->setText(QStringLiteral("%1\n%2 · %3%4").arg(c.name, kindLabel(c.kind), c.fileSuffix(),
                                                        c.exportEnabled ? QString() : tr(" · not exported")));
        QPixmap sw(14, 14);
        sw.fill(c.defaultColor);
        it->setIcon(QIcon(sw));
    }
    m_convention->setVisible(c.isNormal());
    m_conventionLabel->setVisible(c.isNormal());
    emit changed();
}

QString ChannelListEditor::validate() const
{
    if (m_channels.isEmpty())
        return tr("A sheet needs at least one channel.");
    QSet<QString> suffixes;
    for (const Channel &c : m_channels) {
        if (c.name.trimmed().isEmpty())
            return tr("Every channel needs a name.");
        if (!c.exportEnabled)
            continue;
        const QString s = c.fileSuffix().toLower();
        if (suffixes.contains(s))
            return tr("Two exported channels use the file suffix \"%1\"; their files would "
                      "overwrite each other.").arg(c.fileSuffix());
        suffixes.insert(s);
    }
    return {};
}

// ================================================================ ChannelsDialog
ChannelsDialog::ChannelsDialog(const QVector<Channel> &channels, bool editingProject,
                               QWidget *parent)
    : QDialog(parent), m_editingProject(editingProject)
{
    setWindowTitle(editingProject ? tr("Channels") : tr("Customize Channels"));
    resize(680, 420);

    m_presets = new QComboBox(this);
    auto *load = new QPushButton(tr("Load"), this);
    auto *savePreset = new QPushButton(tr("Save as Preset…"), this);
    m_deletePreset = new QPushButton(tr("Delete Preset"), this);
    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(tr("Preset"), this));
    presetRow->addWidget(m_presets, 1);
    presetRow->addWidget(load);
    presetRow->addWidget(m_deletePreset);
    presetRow->addSpacing(12);
    presetRow->addWidget(savePreset);

    m_editor = new ChannelListEditor(this);
    m_editor->setChannels(channels);

    auto *hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    hint->setText(editingProject
                      ? tr("Each channel is exported as its own file. Removing a channel hides "
                           "its textures; Undo brings them back.")
                      : tr("Each channel is exported as its own file."));

    auto *buttons = new QDialogButtonBox(this);
    auto *ok = new QPushButton(editingProject ? tr("Apply") : tr("OK"));
    ok->setObjectName(QStringLiteral("primary"));
    ok->setMinimumWidth(120);
    ok->setDefault(true);
    buttons->addButton(ok, QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ChannelsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(presetRow);
    root->addSpacing(6);
    root->addWidget(m_editor, 1);
    root->addWidget(hint);
    root->addWidget(buttons);

    refillPresets();

    connect(m_presets, &QComboBox::currentIndexChanged, this, [this] {
        m_deletePreset->setEnabled(!m_presets->currentData().toBool());
    });
    connect(load, &QPushButton::clicked, this, [this] {
        const auto all = ChannelPresetStore::all();
        const int i = m_presets->currentIndex();
        if (i >= 0 && i < all.size())
            m_editor->setChannels(all[i].channels);
    });
    connect(savePreset, &QPushButton::clicked, this, [this] {
        const QString err = m_editor->validate();
        if (!err.isEmpty()) {
            QMessageBox::warning(this, tr("Channels"), err);
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Preset"), tr("Preset name"),
                                                   QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        for (const auto &p : ChannelPresets::builtIns())
            if (p.name == name) {
                QMessageBox::warning(this, tr("Save Preset"),
                                     tr("\"%1\" is a built-in preset. Pick another name.").arg(name));
                return;
            }
        ChannelPresetStore::save(name, m_editor->channels());
        refillPresets();
        m_presets->setCurrentIndex(m_presets->findText(name + tr("  (custom)")));
    });
    connect(m_deletePreset, &QPushButton::clicked, this, [this] {
        if (m_presets->currentData().toBool())
            return;
        const QString name = m_presets->currentText();
        if (QMessageBox::question(this, tr("Delete Preset"), tr("Delete preset \"%1\"?").arg(name))
            == QMessageBox::Yes) {
            ChannelPresetStore::remove(name);
            refillPresets();
        }
    });
}

void ChannelsDialog::refillPresets()
{
    const QSignalBlocker b(m_presets);
    m_presets->clear();
    for (const auto &p : ChannelPresetStore::all())
        m_presets->addItem(p.builtIn ? p.name : p.name + tr("  (custom)"), p.builtIn);
    m_deletePreset->setEnabled(m_presets->count() && !m_presets->currentData().toBool());
}

QVector<Channel> ChannelsDialog::channels() const { return m_editor->channels(); }

void ChannelsDialog::accept()
{
    const QString err = m_editor->validate();
    if (!err.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    QDialog::accept();
}
