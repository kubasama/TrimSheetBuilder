#include "gui/NewSheetDialog.h"
#include "gui/ChannelEditor.h"
#include "gui/SliderField.h"
#include "gui/Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const int kSizes[] = {512, 1024, 2048, 4096, 8192};

void paintLayout(QPainter &p, const QRectF &area, QSize sheet,
                 const QVector<LayoutPresets::GeneratedZone> &zones, bool labels)
{
    if (sheet.isEmpty())
        return;
    QSizeF s = QSizeF(sheet).scaled(area.size(), Qt::KeepAspectRatio);
    const QRectF r(area.center() - QPointF(s.width() / 2, s.height() / 2), s);
    const double k = s.width() / sheet.width();
    p.fillRect(r, QColor(0x14, 0x14, 0x14));
    const QColor acc = Theme::accent();
    for (int i = 0; i < zones.size(); ++i) {
        const QRect &z = zones[i].rect;
        const QRectF zr(r.x() + z.x() * k, r.y() + z.y() * k, z.width() * k, z.height() * k);
        QColor c = QColor::fromHsv((acc.hsvHue() + i * 23) % 360, 90, 150 + (i % 3) * 25);
        p.fillRect(zr, c);
        const QString label = QStringLiteral("%1 × %2").arg(z.width()).arg(z.height());
        if (labels && zr.height() >= 12
            && p.fontMetrics().horizontalAdvance(label) <= zr.width() - 6) {
            p.setPen(QColor(0, 0, 0, 160));
            p.drawText(zr.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, label);
        }
    }
    p.setPen(QColor(255, 255, 255, 50));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
}
} // namespace

class LayoutPreview : public QWidget
{
public:
    explicit LayoutPreview(QWidget *parent) : QWidget(parent) { setMinimumSize(300, 300); }
    void set(QSize sheet, QVector<LayoutPresets::GeneratedZone> zones)
    {
        m_sheet = sheet;
        m_zones = std::move(zones);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.8);
        p.setFont(f);
        paintLayout(p, QRectF(rect()).adjusted(4, 4, -4, -4), m_sheet, m_zones, true);
    }

private:
    QSize m_sheet;
    QVector<LayoutPresets::GeneratedZone> m_zones;
};

NewSheetDialog::NewSheetDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Trim Sheet"));
    resize(1120, 800);

    // ---- size
    auto *sizeBox = new QGroupBox(tr("Size"), this);
    auto *sizeLay = new QVBoxLayout(sizeBox);
    auto *sizeRow = new QHBoxLayout;
    sizeRow->setSpacing(4);
    m_sizeButtons = new QButtonGroup(this);
    m_sizeButtons->setExclusive(true);
    for (int s : kSizes) {
        auto *b = new QToolButton(sizeBox);
        b->setText(s >= 1024 ? QStringLiteral("%1K").arg(s / 1024) : QString::number(s));
        b->setCheckable(true);
        b->setMinimumWidth(48);
        m_sizeButtons->addButton(b, s);
        sizeRow->addWidget(b);
    }
    sizeRow->addStretch(1);
    sizeLay->addLayout(sizeRow);

    m_width = new QSpinBox(sizeBox);
    m_height = new QSpinBox(sizeBox);
    for (QSpinBox *s : {m_width, m_height}) {
        s->setRange(1, 16384);
        s->setSuffix(tr(" px"));
        s->setValue(2048);
    }
    m_square = new QCheckBox(tr("Square"), sizeBox);
    m_square->setChecked(true);
    auto *whRow = new QHBoxLayout;
    whRow->addWidget(m_width, 1);
    whRow->addWidget(new QLabel(QStringLiteral("×"), sizeBox));
    whRow->addWidget(m_height, 1);
    whRow->addWidget(m_square);
    sizeLay->addLayout(whRow);

    // ---- channels
    auto *chBox = new QGroupBox(tr("Channels"), this);
    auto *chLay = new QVBoxLayout(chBox);
    m_channelPreset = new QComboBox(chBox);
    auto *customize = new QPushButton(tr("Customize…"), chBox);
    auto *chRow = new QHBoxLayout;
    chRow->addWidget(m_channelPreset, 1);
    chRow->addWidget(customize);
    chLay->addLayout(chRow);
    m_channelSummary = new QLabel(chBox);
    m_channelSummary->setObjectName(QStringLiteral("dim"));
    m_channelSummary->setWordWrap(true);
    chLay->addWidget(m_channelSummary);
    for (const auto &p : ChannelPresetStore::all())
        m_channelPreset->addItem(p.builtIn ? p.name : p.name + tr("  (custom)"));
    m_channelPreset->setCurrentIndex(2); // Base Color + Normal + ORM
    m_channels = ChannelPresetStore::all()[2].channels;
    connect(m_channelPreset, &QComboBox::currentIndexChanged, this,
            &NewSheetDialog::channelPresetChanged);
    connect(customize, &QPushButton::clicked, this, &NewSheetDialog::customizeChannels);

    // ---- layout
    auto *layoutBox = new QGroupBox(tr("Layout"), this);
    auto *layoutLay = new QVBoxLayout(layoutBox);
    m_presets = new QListWidget(layoutBox);
    m_presets->setIconSize(QSize(40, 40));
    m_presets->setMinimumHeight(260);
    for (const LayoutPresets::Preset &pr : LayoutPresets::all()) {
        QPixmap icon(72, 72);
        icon.fill(Qt::transparent);
        QPainter ip(&icon);
        paintLayout(ip, QRectF(2, 2, 68, 68), QSize(1024, 1024),
                    LayoutPresets::generate(pr.id, QSize(1024, 1024), qMin(pr.defaultCount, 8),
                                            qMin(pr.defaultCount2, 8), 16),
                    false);
        ip.end();
        auto *item = new QListWidgetItem(QIcon(icon), pr.name, m_presets);
        item->setData(Qt::UserRole, pr.id);
    }
    layoutLay->addWidget(m_presets, 1);

    // ---- preview (right column): picture, description, stats, options, buttons
    auto *previewBox = new QGroupBox(tr("Preview"), this);
    auto *previewLay = new QVBoxLayout(previewBox);
    m_preview = new LayoutPreview(previewBox);
    m_description = new QLabel(previewBox);
    m_description->setObjectName(QStringLiteral("dim"));
    m_description->setWordWrap(true);
    m_stats = new QLabel(previewBox);
    m_stats->setObjectName(QStringLiteral("dim"));
    previewLay->addWidget(m_preview, 1);
    previewLay->addWidget(m_description);
    previewLay->addWidget(m_stats);

    // options under the preview; unused ones are hidden, and the list
    // scrolls if a layout ever has more than fit
    auto *optBox = new QGroupBox(tr("Options"), this);
    auto *optBoxLay = new QVBoxLayout(optBox);
    optBoxLay->setContentsMargins(0, 0, 0, 0);
    auto *optInner = new QWidget;
    auto *optLay = new QVBoxLayout(optInner);
    optLay->setContentsMargins(0, 0, 8, 0);
    optLay->setSpacing(10);
    m_count = new SliderField(tr("Count"), optInner);
    m_count2 = new SliderField(tr("Count"), optInner);
    m_gap = new SliderField(tr("Gap"), optInner);
    m_gap->setRange(0, 256);
    m_gap->setSuffix(tr(" px"));
    m_gap->setValue(16);
    m_gap->setToolTip(tr("Empty space between neighbouring zones, so the edge padding has room."));
    m_padding = new SliderField(tr("Edge padding"), optInner);
    m_padding->setRange(0, 128);
    m_padding->setSuffix(tr(" px"));
    m_padding->setValue(8);
    m_padding->setToolTip(tr("Edge bleed around each zone, to stop mipmaps mixing neighbours."));
    for (SliderField *f : {m_count, m_count2, m_gap, m_padding})
        optLay->addWidget(f);
    optLay->addStretch(1);
    auto *optScroll = new QScrollArea(optBox);
    optScroll->setWidget(optInner);
    optScroll->setWidgetResizable(true);
    optScroll->setFrameShape(QFrame::NoFrame);
    optScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    optScroll->setFixedHeight(4 * 34); // four rows; more rows scroll
    optBoxLay->addWidget(optScroll);

    auto *cancel = new QPushButton(tr("Cancel"), this);
    auto *create = new QPushButton(tr("Create"), this);
    create->setObjectName(QStringLiteral("primary"));
    create->setMinimumWidth(160);
    create->setDefault(true);
    connect(create, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(create);

    auto *left = new QVBoxLayout;
    left->addWidget(sizeBox);
    left->addWidget(chBox);
    left->addWidget(layoutBox, 1);
    auto *right = new QVBoxLayout;
    right->addWidget(previewBox, 1);
    right->addWidget(optBox);
    right->addLayout(buttons);

    auto *root = new QHBoxLayout(this);
    root->setSpacing(12);
    root->addLayout(left, 5);
    root->addLayout(right, 6);

    // ---- wiring
    connect(m_sizeButtons, &QButtonGroup::idClicked, this, [this](int s) {
        m_updating = true;
        m_width->setValue(s);
        m_height->setValue(s);
        m_square->setChecked(true);
        m_updating = false;
        updatePreview();
    });
    connect(m_width, &QSpinBox::valueChanged, this, [this] {
        if (!m_updating && m_square->isChecked()) {
            m_updating = true;
            m_height->setValue(m_width->value());
            m_updating = false;
        }
        sizeEdited();
    });
    connect(m_height, &QSpinBox::valueChanged, this, [this] {
        if (!m_updating && m_square->isChecked()) {
            m_updating = true;
            m_width->setValue(m_height->value());
            m_updating = false;
        }
        sizeEdited();
    });
    connect(m_square, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            m_height->setValue(m_width->value());
    });
    connect(m_presets, &QListWidget::currentRowChanged, this, &NewSheetDialog::presetChanged);
    connect(m_count, &SliderField::valueChanged, this, &NewSheetDialog::updatePreview);
    connect(m_count2, &SliderField::valueChanged, this, &NewSheetDialog::updatePreview);
    connect(m_gap, &SliderField::valueChanged, this, &NewSheetDialog::updatePreview);
    connect(m_padding, &SliderField::valueChanged, this, [this](int v) {
        // keep the gap big enough for the bleed on both sides
        if (m_gap->value() < 2 * v)
            m_gap->setValue(2 * v);
    });

    m_sizeButtons->button(2048)->setChecked(true);
    m_presets->setCurrentRow(0);
    m_presets->setFocus();
    updateChannelSummary();
    sizeEdited();
}

void NewSheetDialog::channelPresetChanged()
{
    const auto all = ChannelPresetStore::all();
    const int i = m_channelPreset->currentIndex();
    if (i >= 0 && i < all.size())
        m_channels = all[i].channels;
    updateChannelSummary();
}

void NewSheetDialog::customizeChannels()
{
    ChannelsDialog dlg(m_channels, false, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_channels = dlg.channels();
    // show "Custom" unless it matches a preset exactly
    const auto all = ChannelPresetStore::all();
    int match = -1;
    for (int i = 0; i < all.size(); ++i)
        if (all[i].channels == m_channels)
            match = i;
    const QSignalBlocker b(m_channelPreset);
    const int customIdx = m_channelPreset->findText(tr("Custom"));
    if (match >= 0) {
        if (customIdx >= 0)
            m_channelPreset->removeItem(customIdx);
        m_channelPreset->setCurrentIndex(match);
    } else {
        if (customIdx < 0)
            m_channelPreset->addItem(tr("Custom"));
        m_channelPreset->setCurrentIndex(m_channelPreset->findText(tr("Custom")));
    }
    updateChannelSummary();
}

void NewSheetDialog::updateChannelSummary()
{
    QStringList parts;
    for (const Channel &c : std::as_const(m_channels))
        parts << c.name;
    m_channelSummary->setText(parts.join(QStringLiteral(" · ")));
}

QString NewSheetDialog::presetId() const
{
    const QListWidgetItem *it = m_presets->currentItem();
    return it ? it->data(Qt::UserRole).toString() : QStringLiteral("empty");
}

void NewSheetDialog::sizeEdited()
{
    if (m_updating)
        return;
    const bool sq = m_width->value() == m_height->value();
    if (QAbstractButton *b = m_sizeButtons->button(sq ? m_width->value() : -1)) {
        b->setChecked(true);
    } else {
        // uncheck all
        m_sizeButtons->setExclusive(false);
        for (QAbstractButton *x : m_sizeButtons->buttons())
            x->setChecked(false);
        m_sizeButtons->setExclusive(true);
    }
    updatePreview();
}

void NewSheetDialog::presetChanged()
{
    const LayoutPresets::Preset *p = LayoutPresets::find(presetId());
    if (!p)
        return;
    m_description->setText(p->description);
    // rows stay in place; unused ones are just greyed out
    const bool c1 = !p->countLabel.isEmpty(), c2 = !p->count2Label.isEmpty();
    {
        const QSignalBlocker b(m_count), b2(m_count2);
        m_count->setLabel(c1 ? p->countLabel : tr("Count"));
        m_count->setRange(c1 ? p->minCount : 0, c1 ? p->maxCount : 0);
        m_count->setValue(c1 ? p->defaultCount : 0);
        m_count->setVisible(c1);
        m_count2->setLabel(c2 ? p->count2Label : tr("Count"));
        m_count2->setRange(c2 ? p->minCount2 : 0, c2 ? p->maxCount2 : 0);
        m_count2->setValue(c2 ? p->defaultCount2 : 0);
        m_count2->setVisible(c2);
    }
    updatePreview();
}

void NewSheetDialog::updatePreview()
{
    const auto z = zones();
    m_preview->set(sheetSize(), z);
    if (z.isEmpty()) {
        m_stats->setText(tr("No zones: draw your own in Layout mode."));
        return;
    }
    QSize biggest, smallest;
    qint64 big = -1, small = -1;
    for (const auto &g : z) {
        const qint64 a = qint64(g.rect.width()) * g.rect.height();
        if (a > big) { big = a; biggest = g.rect.size(); }
        if (small < 0 || a < small) { small = a; smallest = g.rect.size(); }
    }
    m_stats->setText(tr("%1 zones · largest %2 × %3 · smallest %4 × %5")
                         .arg(z.size())
                         .arg(biggest.width()).arg(biggest.height())
                         .arg(smallest.width()).arg(smallest.height()));
}

QSize NewSheetDialog::sheetSize() const { return {m_width->value(), m_height->value()}; }
int NewSheetDialog::padding() const { return m_padding->value(); }

QVector<LayoutPresets::GeneratedZone> NewSheetDialog::zones() const
{
    return LayoutPresets::generate(presetId(), sheetSize(), m_count->value(), m_count2->value(),
                                   m_gap->value());
}
