#include "gui/InspectorPanel.h"
#include "gui/Icons.h"

#include "core/Commands.h"
#include "core/Project.h"
#include "io/ClipboardImport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QInputDialog>
#include <QSettings>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
QSpinBox *pixelSpin(QWidget *parent)
{
    auto *s = new QSpinBox(parent);
    s->setRange(0, 16384);
    s->setSuffix(QStringLiteral(" px"));
    s->setKeyboardTracking(false);
    return s;
}
QDoubleSpinBox *doubleSpin(QWidget *parent, double min, double max, double step)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(3);
    s->setKeyboardTracking(false);
    return s;
}
QString imageFilter()
{
    QStringList pats;
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        pats << QStringLiteral("*.") + QString::fromLatin1(f);
    return QObject::tr("Images (%1);;All files (*)").arg(pats.join(u' '));
}
} // namespace

InspectorPanel::InspectorPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 4);
    root->setSpacing(8);

    // ---- sheet
    auto *sheetBox = new QGroupBox(tr("Sheet"), this);
    auto *sheetForm = new QFormLayout(sheetBox);
    m_sheetSize = new QLabel(sheetBox);
    m_padding = pixelSpin(sheetBox);
    m_padding->setRange(0, 256);
    m_padding->setToolTip(tr("Edge padding (bleed): each zone's border pixels are smeared outward "
                             "this far on export.\nWithout it, mipmaps and texture filtering blend "
                             "neighbouring zones (or the background) into the edges of a trim."));
    m_channelSummary = new QLabel(sheetBox);
    m_channelSummary->setWordWrap(true);
    auto *editChannels = new QToolButton(sheetBox);
    editChannels->setText(tr("Edit…"));
    editChannels->setToolTip(tr("Add, remove or change channels (Ctrl+Shift+C)"));
    auto *chRow = new QHBoxLayout;
    chRow->addWidget(m_channelSummary, 1);
    chRow->addWidget(editChannels, 0, Qt::AlignTop);
    connect(editChannels, &QToolButton::clicked, this, &InspectorPanel::editChannelsRequested);
    sheetForm->addRow(tr("Size"), m_sheetSize);
    sheetForm->addRow(tr("Edge padding"), m_padding);
    sheetForm->addRow(tr("Channels"), chRow);
    root->addWidget(sheetBox);

    // ---- snapping (layout mode)
    m_snapBox = new QGroupBox(tr("Snapping"), this);
    auto *snapForm = new QFormLayout(m_snapBox);
    m_snapOn = new QCheckBox(tr("Enabled"), m_snapBox);
    m_snapOn->setToolTip(tr("Shift+Tab. Hold Ctrl while dragging to invert."));
    m_grid = pixelSpin(m_snapBox);
    m_grid->setRange(1, 4096);
    m_snapGrid = new QCheckBox(tr("Grid"), m_snapBox);
    auto *gridRow = new QHBoxLayout;
    gridRow->addWidget(m_snapGrid);
    gridRow->addWidget(m_grid, 1);
    m_snapEdges = new QCheckBox(tr("Zone and sheet edges"), m_snapBox);
    m_overlap = new QCheckBox(tr("Allow overlapping zones"), m_snapBox);
    snapForm->addRow(m_snapOn);
    snapForm->addRow(gridRow);
    snapForm->addRow(m_snapEdges);
    snapForm->addRow(m_overlap);
    root->addWidget(m_snapBox);
    for (QCheckBox *c : {m_snapOn, m_snapGrid, m_snapEdges, m_overlap}) {
        connect(c, &QCheckBox::toggled, this, [this] {
            if (m_updating)
                return;
            m_snap.enabled = m_snapOn->isChecked();
            m_snap.grid = m_snapGrid->isChecked();
            m_snap.edges = m_snapEdges->isChecked();
            m_snap.allowOverlap = m_overlap->isChecked();
            emit snapSettingsEdited(m_snap);
        });
    }

    m_noZone = new QLabel(this);
    m_noZone->setObjectName(QStringLiteral("hint"));
    m_noZone->setWordWrap(true);
    m_noZone->setContentsMargins(6, 4, 6, 4);
    root->addWidget(m_noZone);

    // ---- zone
    m_zoneBox = new QGroupBox(tr("Zone"), this);
    auto *zf = new QFormLayout(m_zoneBox);
    m_name = new QLineEdit(m_zoneBox);
    m_x = pixelSpin(m_zoneBox);
    m_y = pixelSpin(m_zoneBox);
    m_w = pixelSpin(m_zoneBox);
    m_h = pixelSpin(m_zoneBox);
    m_w->setMinimum(1);
    m_h->setMinimum(1);
    auto *pos = new QHBoxLayout;
    pos->addWidget(m_x);
    pos->addWidget(m_y);
    auto *size = new QHBoxLayout;
    size->addWidget(m_w);
    size->addWidget(m_h);
    m_locked = new QCheckBox(tr("Locked"), m_zoneBox);
    m_locked->setToolTip(tr("Locked zones can't be moved, resized or deleted"));
    m_deleteZone = new QToolButton(m_zoneBox);
    m_deleteZone->setText(tr("Delete Zone"));
    m_deleteZone->setToolTip(tr("Delete this zone (Delete)"));
    auto *lockRow = new QHBoxLayout;
    lockRow->addWidget(m_locked);
    lockRow->addStretch(1);
    lockRow->addWidget(m_deleteZone);
    zf->addRow(tr("Name"), m_name);
    zf->addRow(tr("Position"), pos);
    zf->addRow(tr("Size"), size);
    zf->addRow(QString(), lockRow);
    root->addWidget(m_zoneBox);
    connect(m_locked, &QCheckBox::toggled, this, [this](bool on) {
        const Zone *z = zone();
        if (m_updating || !z || z->locked == on)
            return;
        Zone after = *z;
        after.locked = on;
        pushZone(after, on ? tr("Lock Zone") : tr("Unlock Zone"));
    });
    connect(m_deleteZone, &QToolButton::clicked, this, [this] {
        const Zone *z = zone();
        if (z && !z->locked)
            m_project->undoStack()->push(new RemoveZoneCommand(m_project, z->id));
    });

    // ---- fill
    m_fillBox = new QGroupBox(tr("Fill"), this);
    auto *ff = new QFormLayout(m_fillBox);
    m_material = new QComboBox(m_fillBox);
    auto *newMat = new QToolButton(m_fillBox);
    newMat->setText(tr("New"));
    newMat->setToolTip(tr("Give this zone its own new material"));
    m_renameMat = new QToolButton(m_fillBox);
    m_renameMat->setText(tr("Rename"));
    m_renameMat->setToolTip(tr("Rename this material"));
    auto *matRow = new QHBoxLayout;
    matRow->setSpacing(4);
    matRow->addWidget(m_material, 1);
    matRow->addWidget(m_renameMat);
    matRow->addWidget(newMat);
    m_materialUsage = new QLabel(m_fillBox);
    m_materialUsage->setEnabled(false);
    m_fillMode = new QComboBox(m_fillBox);
    m_fillMode->addItem(tr("Stretch"), int(FillMode::Stretch));
    m_fillMode->addItem(tr("Tile"), int(FillMode::Tile));
    m_fillMode->addItem(tr("Fit"), int(FillMode::Fit));
    m_fillMode->addItem(tr("Crop"), int(FillMode::Crop));
    m_rotation = new QDoubleSpinBox(m_fillBox);
    m_rotation->setRange(0, 360);
    m_rotation->setWrapping(true);
    m_rotation->setDecimals(1);
    m_rotation->setSingleStep(1);
    m_rotation->setSuffix(QStringLiteral("°"));
    m_rotation->setKeyboardTracking(false);
    m_rotation->setToolTip(tr("Drag to rotate (Shift: fine, Ctrl: 10° steps).\n"
                              "90° steps are pixel exact; other angles are best with Tile."));
    auto *rot90 = new QToolButton(m_fillBox);
    rot90->setText(QStringLiteral("+90°"));
    rot90->setToolTip(tr("Rotate 90° clockwise"));
    connect(rot90, &QToolButton::clicked, this, [this] {
        m_rotation->setValue(std::fmod(std::round(m_rotation->value() / 90.0) * 90.0 + 90.0, 360.0));
    });
    auto *rotRow = new QHBoxLayout;
    rotRow->setSpacing(4);
    rotRow->addWidget(m_rotation, 1);
    rotRow->addWidget(rot90);
    m_flipX = new QCheckBox(tr("Flip X"), m_fillBox);
    m_flipY = new QCheckBox(tr("Flip Y"), m_fillBox);
    auto *flips = new QHBoxLayout;
    flips->addWidget(m_flipX);
    flips->addWidget(m_flipY);
    m_scaleX = doubleSpin(m_fillBox, 0.01, 64, 0.1);
    m_scaleY = doubleSpin(m_fillBox, 0.01, 64, 0.1);
    m_offX = doubleSpin(m_fillBox, -10, 10, 0.05);
    m_offY = doubleSpin(m_fillBox, -10, 10, 0.05);
    // chain buttons between X and Y: keep the ratio (scale) or move together (offset)
    auto linkButton = [this](const QString &key, bool def, const QString &tip) {
        auto *b = new QToolButton(m_fillBox);
        b->setCheckable(true);
        b->setIcon(Icons::linkIcon());
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setObjectName(QStringLiteral("linkButton"));
        b->setChecked(QSettings().value(key, def).toBool());
        connect(b, &QToolButton::toggled, this, [key](bool on) { QSettings().setValue(key, on); });
        return b;
    };
    m_linkScale = linkButton(QStringLiteral("ui/linkScale"), true,
                             tr("Linked: changing one keeps the X:Y ratio"));
    m_linkOffset = linkButton(QStringLiteral("ui/linkOffset"), false,
                              tr("Linked: changing one moves both by the same amount"));
    auto *scale = new QHBoxLayout;
    scale->setSpacing(4);
    scale->addWidget(m_scaleX);
    scale->addWidget(m_linkScale);
    scale->addWidget(m_scaleY);
    auto *off = new QHBoxLayout;
    off->setSpacing(4);
    off->addWidget(m_offX);
    off->addWidget(m_linkOffset);
    off->addWidget(m_offY);
    ff->addRow(tr("Material"), matRow);
    ff->addRow(QString(), m_materialUsage);
    ff->addRow(tr("Mode"), m_fillMode);
    ff->addRow(tr("Rotation"), rotRow);
    ff->addRow(QString(), flips);
    ff->addRow(tr("Tile scale"), scale);
    ff->addRow(tr("Offset"), off);
    root->addWidget(m_fillBox);

    // ---- maps
    m_mapsBox = new QGroupBox(tr("Textures"), this);
    m_mapsLayout = new QVBoxLayout(m_mapsBox);
    root->addWidget(m_mapsBox);
    root->addStretch(1);

    // ---- wiring
    connect(m_padding, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_updating || !m_project || v == m_project->padding())
            return;
        m_project->undoStack()->push(new SetPaddingCommand(m_project, m_project->padding(), v));
    });
    connect(m_grid, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_updating && m_project)
            m_project->setGridStep(v);
    });
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        const Zone *z = zone();
        if (m_updating || !z || m_name->text() == z->name)
            return;
        Zone after = *z;
        after.name = m_name->text();
        pushZone(after, tr("Rename Zone"));
    });
    for (QSpinBox *s : {m_x, m_y, m_w, m_h}) {
        connect(s, &QSpinBox::valueChanged, this, [this] {
            const Zone *z = zone();
            if (m_updating || !z)
                return;
            Zone after = *z;
            after.rect = QRect(m_x->value(), m_y->value(), m_w->value(), m_h->value());
            pushZone(after, tr("Move/Resize Zone"), QStringLiteral("rect"));
        });
    }

    auto fillEdited = [this] {
        const Zone *z = zone();
        if (m_updating || !z)
            return;
        Zone after = *z;
        FillSettings &f = after.fill;
        f.mode = FillMode(m_fillMode->currentData().toInt());
        f.rotation = std::fmod(m_rotation->value(), 360.0);
        f.flipX = m_flipX->isChecked();
        f.flipY = m_flipY->isChecked();
        f.scale = QPointF(m_scaleX->value(), m_scaleY->value());
        f.offset = QPointF(m_offX->value(), m_offY->value());
        pushZone(after, tr("Change Fill"), QStringLiteral("fill"));
    };
    connect(m_fillMode, &QComboBox::currentIndexChanged, this, fillEdited);
    connect(m_rotation, &QDoubleSpinBox::valueChanged, this, fillEdited);
    connect(m_flipX, &QCheckBox::toggled, this, fillEdited);
    connect(m_flipY, &QCheckBox::toggled, this, fillEdited);
    // linked pairs: update the partner first, then record one edit
    auto linked = [this, fillEdited](QDoubleSpinBox *self, QDoubleSpinBox *other, QToolButton *link,
                                     bool keepRatio, bool selfIsX) {
        connect(self, &QDoubleSpinBox::valueChanged, this, [=](double v) {
            const Zone *z = zone();
            if (!m_updating && z && link->isChecked()) {
                const QPointF cur = keepRatio ? z->fill.scale : z->fill.offset;
                const double was = selfIsX ? cur.x() : cur.y();
                const double partner = selfIsX ? cur.y() : cur.x();
                double next = partner;
                if (keepRatio) {
                    if (was > 0)
                        next = partner * (v / was);
                } else {
                    next = partner + (v - was);
                }
                const QSignalBlocker b(other);
                other->setValue(next);
            }
            fillEdited();
        });
    };
    linked(m_scaleX, m_scaleY, m_linkScale, true, true);
    linked(m_scaleY, m_scaleX, m_linkScale, true, false);
    linked(m_offX, m_offY, m_linkOffset, false, true);
    linked(m_offY, m_offX, m_linkOffset, false, false);

    connect(m_material, &QComboBox::currentIndexChanged, this, [this] {
        const Zone *z = zone();
        if (m_updating || !z)
            return;
        Zone after = *z;
        after.materialId = m_material->currentData().toString();
        pushZone(after, tr("Change Material"));
    });
    connect(m_renameMat, &QToolButton::clicked, this, [this] {
        const Zone *z = zone();
        const Material *m = z ? m_project->material(z->materialId) : nullptr;
        if (!m)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename Material"), tr("Name"),
                                                   QLineEdit::Normal, m->name, &ok).trimmed();
        if (!ok || name.isEmpty() || name == m->name)
            return;
        Material after = *m;
        after.name = name;
        m_project->undoStack()->push(
            new SetMaterialCommand(m_project, *m, after, tr("Rename Material")));
    });
    connect(newMat, &QToolButton::clicked, this, [this] {
        if (m_project && zone())
            Edit::newMaterialForZone(m_project, m_zoneId);
    });

    refresh();
}

// ---------------------------------------------------------------- state
void InspectorPanel::setProject(Project *project)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);
    m_project = project;
    m_zoneId.clear();
    if (m_project) {
        connect(m_project, &Project::sheetChanged, this, &InspectorPanel::refreshSheet);
        connect(m_project, &Project::channelsChanged, this, [this] {
            buildMapRows();
            refreshSheet();
            refresh();
        });
        connect(m_project, &Project::zoneChanged, this, [this](const QString &id) {
            if (id == m_zoneId)
                refresh();
        });
        connect(m_project, &Project::zoneRemoved, this, [this](const QString &id) {
            if (id == m_zoneId)
                setZone({});
        });
        connect(m_project, &Project::materialsChanged, this, &InspectorPanel::refresh);
        connect(m_project, &Project::textureChanged, this, &InspectorPanel::refresh);
    }
    buildMapRows();
    refreshSheet();
    refresh();
}

void InspectorPanel::setZone(const QString &zoneId)
{
    m_zoneId = zoneId;
    refresh();
}

void InspectorPanel::setMode(EditMode mode)
{
    m_mode = mode;
    refresh();
}

void InspectorPanel::setSnapSettings(const SnapSettings &s)
{
    m_snap = s;
    m_updating = true;
    m_snapOn->setChecked(s.enabled);
    m_snapGrid->setChecked(s.grid);
    m_snapEdges->setChecked(s.edges);
    m_overlap->setChecked(s.allowOverlap);
    m_updating = false;
}

const Zone *InspectorPanel::zone() const
{
    return m_project ? m_project->zone(m_zoneId) : nullptr;
}

void InspectorPanel::pushZone(const Zone &after, const QString &text, const QString &mergeKey)
{
    const Zone *z = zone();
    if (!z || *z == after)
        return;
    m_project->undoStack()->push(new SetZoneCommand(m_project, *z, after, text, mergeKey));
}

// ---------------------------------------------------------------- map rows
void InspectorPanel::buildMapRows()
{
    for (const MapRow &r : std::as_const(m_rows))
        delete r.thumb->parentWidget();
    m_rows.clear();
    if (!m_project)
        return;

    for (const Channel &ch : m_project->channels()) {
        auto *frame = new QFrame(m_mapsBox);
        frame->setObjectName(QStringLiteral("mapRow"));
        auto *g = new QGridLayout(frame);
        g->setContentsMargins(6, 6, 6, 6);

        MapRow row;
        row.channelId = ch.id;
        row.thumb = new QLabel(frame);
        row.thumb->setFixedSize(56, 56);
        row.thumb->setAlignment(Qt::AlignCenter);
        row.thumb->setObjectName(QStringLiteral("thumb"));
        auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(ch.name.toHtmlEscaped()), frame);
        row.info = new QLabel(frame);
        row.info->setWordWrap(true);
        row.info->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row.convention = nullptr;

        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(4);
        auto *browse = new QToolButton(frame);
        browse->setText(tr("Browse…"));
        auto *paste = new QToolButton(frame);
        paste->setText(tr("Paste"));
        paste->setToolTip(tr("Paste an image, or a copied image file path"));
        auto *more = new QToolButton(frame);
        more->setText(QStringLiteral("⋯"));
        more->setPopupMode(QToolButton::InstantPopup);
        more->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }"));
        auto *menu = new QMenu(more);
        QAction *embed = menu->addAction(tr("Embed in .trimsheet file"));
        embed->setToolTip(tr("Store this texture inside the project instead of linking it"));
        QAction *clear = menu->addAction(tr("Clear"));
        more->setMenu(menu);
        buttons->addWidget(browse);
        buttons->addWidget(paste);
        buttons->addWidget(more);
        buttons->addStretch(1);
        row.embedBtn = embed;
        row.clearBtn = clear;

        // column 0: disclosure arrow for the per-component cards
        g->addWidget(row.thumb, 0, 1, 3, 1);
        g->addWidget(title, 0, 2);
        if (ch.isNormal()) {
            row.convention = new QComboBox(frame);
            row.convention->addItem(tr("DirectX (Y−)"), int(NormalConvention::DirectX));
            row.convention->addItem(tr("OpenGL (Y+)"), int(NormalConvention::OpenGL));
            row.convention->setToolTip(tr("Green channel convention of the source image"));
            g->addWidget(row.convention, 0, 3);
            const QString cid = ch.id;
            connect(row.convention, &QComboBox::currentIndexChanged, this, [this, cid, row] {
                if (!m_updating && m_project)
                    Edit::setMapConvention(m_project, m_zoneId, cid,
                                           NormalConvention(row.convention->currentData().toInt()));
            });
        }
        g->addWidget(row.info, 1, 2, 1, 2);
        g->addLayout(buttons, 2, 2, 1, 2);
        g->setColumnStretch(2, 1);
        g->setHorizontalSpacing(8);

        const QString cid = ch.id;
        // arrow left of the thumbnail: opens one card per component
        row.expand = new QToolButton(frame);
        row.expand->setObjectName(QStringLiteral("disclosure"));
        row.expand->setCheckable(true);
        row.expand->setText(QStringLiteral("▸"));
        row.expand->setFixedSize(16, 56);
        g->addWidget(row.expand, 0, 0, 3, 1, Qt::AlignVCenter);
        if (ch.isNormal() || ch.isGrayscale()) {
            row.expand->setEnabled(false);
            row.expand->setToolTip(ch.isNormal() ? tr("Normal maps can't be built from separate channels")
                                                 : tr("Grayscale channels have only one component"));
        } else {
            row.expand->setCursor(Qt::PointingHandCursor);
            row.expand->setToolTip(tr("Fill R, G, B%1 from their own textures, "
                                      "e.g. pack AO / roughness / metallic into ORM")
                                       .arg(ch.alpha ? tr(" or A") : QString()));

            auto *panel = new QFrame(frame);
            panel->setObjectName(QStringLiteral("compPanel"));
            auto *pl = new QVBoxLayout(panel);
            pl->setContentsMargins(6, 6, 6, 6);
            pl->setSpacing(6);
            row.compPanel = panel;

            const char *names[] = {"Red", "Green", "Blue", "Alpha"};
            const char *colors[] = {"#e06c6c", "#7fcf7f", "#6fa8ef", "#c8c8c8"};
            for (int c = 0; c < 4; ++c) {
                auto &cr = row.comps[c];
                auto *card = new QFrame(panel);
                card->setObjectName(QStringLiteral("compRow"));
                card->setStyleSheet(QStringLiteral("QFrame#compRow { border-left: 3px solid %1; }")
                                        .arg(QLatin1String(colors[c])));
                auto *cg = new QGridLayout(card);
                cg->setContentsMargins(8, 6, 6, 6);
                cg->setHorizontalSpacing(8);
                cg->setVerticalSpacing(4);
                cr.frame = card;
                cr.thumb = new QLabel(card);
                cr.thumb->setFixedSize(44, 44);
                cr.thumb->setAlignment(Qt::AlignCenter);
                cr.thumb->setObjectName(QStringLiteral("thumb"));
                auto *title = new QLabel(QStringLiteral("<b style='color:%1'>%2</b>")
                                             .arg(QLatin1String(colors[c]), tr(names[c])),
                                         card);
                cr.source = new QComboBox(card);
                for (int sIdx : {4, 0, 1, 2, 3})
                    cr.source->addItem(sIdx == 4 ? tr("Gray")
                                                 : tr("from %1").arg(QLatin1String(names[sIdx]).left(1)),
                                       sIdx);
                cr.source->setToolTip(tr("Which part of the texture to use"));
                cr.info = new QLabel(card);
                cr.info->setObjectName(QStringLiteral("dim"));
                cr.info->setWordWrap(true);
                auto *btns = new QHBoxLayout;
                btns->setSpacing(4);
                auto *b = new QToolButton(card);
                b->setText(tr("Browse…"));
                auto *pst = new QToolButton(card);
                pst->setText(tr("Paste"));
                cr.clear = new QToolButton(card);
                cr.clear->setText(tr("Clear"));
                btns->addWidget(b);
                btns->addWidget(pst);
                btns->addWidget(cr.clear);
                btns->addStretch(1);
                cg->addWidget(cr.thumb, 0, 0, 3, 1, Qt::AlignTop);
                cg->addWidget(title, 0, 1);
                cg->addWidget(cr.source, 0, 2);
                cg->addWidget(cr.info, 1, 1, 1, 2);
                cg->addLayout(btns, 2, 1, 1, 2);
                cg->setColumnStretch(1, 1);
                pl->addWidget(card);

                connect(b, &QToolButton::clicked, this, [this, cid, c] { browseTexture(cid, c); });
                connect(pst, &QToolButton::clicked, this, [this, cid, c] { pasteTexture(cid, c); });
                connect(cr.clear, &QToolButton::clicked, this, [this, cid, c] {
                    if (m_project)
                        Edit::clearComponent(m_project, m_zoneId, cid, c);
                });
                QComboBox *src = cr.source;
                connect(src, &QComboBox::currentIndexChanged, this, [this, cid, c, src] {
                    if (!m_updating && m_project)
                        Edit::setComponentSource(m_project, m_zoneId, cid, c, src->currentData().toInt());
                });
            }
            if (!ch.alpha)
                row.comps[3].frame->hide(); // only show components that exist
            g->addWidget(panel, 3, 0, 1, 4);
            connect(row.expand, &QToolButton::clicked, this, [this, cid] {
                if (m_expanded.contains(cid))
                    m_expanded.remove(cid);
                else
                    m_expanded.insert(cid);
                refresh();
            });
        }

        connect(browse, &QToolButton::clicked, this, [this, cid] { browseTexture(cid); });
        connect(paste, &QToolButton::clicked, this, [this, cid] { pasteTexture(cid); });
        connect(clear, &QAction::triggered, this, [this, cid] {
            if (m_project)
                Edit::clearTexture(m_project, m_zoneId, cid);
        });
        connect(embed, &QAction::triggered, this, [this, cid] {
            const Zone *z = zone();
            const Material *m = z ? m_project->material(z->materialId) : nullptr;
            if (m && m->maps.contains(cid))
                m_project->embedTexture(m->maps.value(cid).textureId);
        });

        m_mapsLayout->addWidget(frame);
        m_rows.append(row);
    }
}

void InspectorPanel::assign(const QString &channelId, int component, const QString &textureId)
{
    if (component < 0)
        Edit::assignTexture(m_project, m_zoneId, channelId, textureId);
    else
        Edit::assignComponent(m_project, m_zoneId, channelId, component, textureId);
}

void InspectorPanel::browseTexture(const QString &channelId, int component)
{
    if (!m_project || !zone())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose Texture"), QString(),
                                                      imageFilter());
    if (path.isEmpty())
        return;
    QString err;
    const QString id = m_project->addTextureFromFile(path, &err);
    if (id.isEmpty()) {
        emit statusMessage(tr("Could not load %1: %2").arg(path, err));
        return;
    }
    assign(channelId, component, id);
}

void InspectorPanel::pasteTexture(const QString &channelId, int component)
{
    if (!m_project || !zone())
        return;
    QString err;
    const QString id = ClipboardImport::importClipboard(m_project, &err);
    if (id.isEmpty()) {
        emit statusMessage(err);
        return;
    }
    assign(channelId, component, id);
}

QString InspectorPanel::textureLabel(const QString &textureId) const
{
    const TextureEntry *t = m_project ? m_project->texture(textureId) : nullptr;
    if (!t)
        return QStringLiteral("—");
    if (t->source == TextureSource::Linked)
        return (t->missing ? QStringLiteral("⚠ ") : QString()) + QFileInfo(t->path).fileName();
    return tr("embedded");
}

// ---------------------------------------------------------------- refresh
void InspectorPanel::refreshSheet()
{
    m_updating = true;
    if (m_project) {
        QStringList names;
        for (const Channel &c : m_project->channels())
            names << c.name;
        m_channelSummary->setText(names.join(QStringLiteral(", ")));
        const QSize s = m_project->sheetSize();
        m_sheetSize->setText(QStringLiteral("%1 × %2").arg(s.width()).arg(s.height()));
        m_padding->setValue(m_project->padding());
        m_grid->setValue(m_project->gridStep());
    }
    m_updating = false;
}

void InspectorPanel::refresh()
{
    const Zone *z = zone();
    const bool fill = m_mode == EditMode::Fill;
    m_snapBox->setVisible(!fill);
    m_noZone->setText(fill ? tr("Select a zone to assign textures. You can also drop image "
                                "files on a zone, or paste with Ctrl+V.")
                           : tr("Drag on the sheet to draw a zone, or click one to edit it."));
    m_noZone->setVisible(!z);
    m_zoneBox->setVisible(z);
    m_fillBox->setVisible(z && fill);
    m_mapsBox->setVisible(z && fill);
    if (!z)
        return;

    m_updating = true;

    // zone: geometry is locked in fill mode
    m_name->setText(z->name);
    const QSize s = m_project->sheetSize();
    m_x->setMaximum(s.width() - 1);
    m_y->setMaximum(s.height() - 1);
    m_w->setMaximum(s.width());
    m_h->setMaximum(s.height());
    m_x->setValue(z->rect.x());
    m_y->setValue(z->rect.y());
    m_w->setValue(z->rect.width());
    m_h->setValue(z->rect.height());
    for (QSpinBox *sp : {m_x, m_y, m_w, m_h})
        sp->setEnabled(!fill && !z->locked);
    m_locked->setChecked(z->locked);
    m_locked->setVisible(!fill);
    m_deleteZone->setVisible(!fill);
    m_deleteZone->setEnabled(!z->locked);

    // fill
    m_material->clear();
    m_material->addItem(tr("(none)"), QString());
    QList<Material> mats = m_project->materials().values();
    std::sort(mats.begin(), mats.end(),
              [](const Material &a, const Material &b) { return a.name < b.name; });
    for (const Material &m : mats)
        m_material->addItem(m.name, m.id);
    m_material->setCurrentIndex(qMax(0, m_material->findData(z->materialId)));
    int users = 0;
    for (const Zone &o : m_project->zones())
        users += (!z->materialId.isEmpty() && o.materialId == z->materialId);
    m_materialUsage->setText(users > 1 ? tr("Shared by %1 zones").arg(users) : QString());
    m_materialUsage->setVisible(users > 1);

    const FillSettings &f = z->fill;
    m_fillMode->setCurrentIndex(m_fillMode->findData(int(f.mode)));
    m_rotation->setValue(f.rotation);
    m_renameMat->setEnabled(!z->materialId.isEmpty());
    m_flipX->setChecked(f.flipX);
    m_flipY->setChecked(f.flipY);
    m_scaleX->setValue(f.scale.x());
    m_scaleY->setValue(f.scale.y());
    m_offX->setValue(f.offset.x());
    m_offY->setValue(f.offset.y());
    m_scaleX->setEnabled(f.mode == FillMode::Tile);
    m_linkScale->setEnabled(f.mode == FillMode::Tile);
    m_scaleY->setEnabled(f.mode == FillMode::Tile);

    // maps
    const Material *mat = m_project->material(z->materialId);
    for (const MapRow &row : std::as_const(m_rows)) {
        const MapRef ref = mat ? mat->maps.value(row.channelId) : MapRef{};
        const TextureEntry *t = ref.textureId.isEmpty() ? nullptr : m_project->texture(ref.textureId);
        if (t && !t->image.isNull()) {
            row.thumb->setPixmap(QPixmap::fromImage(
                t->image.scaled(row.thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            const QString res = QStringLiteral("%1×%2").arg(t->image.width()).arg(t->image.height());
            if (t->source == TextureSource::Linked) {
                row.info->setText(QStringLiteral("%1%2 · %3")
                                      .arg(t->missing ? tr("⚠ missing, using cached copy · ") : QString(),
                                           QFileInfo(t->path).fileName(), res));
                row.info->setToolTip(t->path);
            } else {
                row.info->setText(tr("Embedded (%1) · %2").arg(t->origin.section(u':', 0, 0), res));
                row.info->setToolTip(QString());
            }
        } else {
            row.thumb->setPixmap(QPixmap());
            row.thumb->setText(QStringLiteral("—"));
            row.info->setText(tr("Empty: uses the channel default"));
            row.info->setToolTip(QString());
        }
        row.embedBtn->setEnabled(t && t->source == TextureSource::Linked);
        row.clearBtn->setEnabled(t != nullptr);
        if (row.convention) {
            row.convention->setEnabled(t != nullptr);
            row.convention->setCurrentIndex(row.convention->findData(int(ref.convention)));
        }
        if (row.expand && row.compPanel) {
            const Channel *chan = m_project->channel(row.channelId);
            const bool alpha = chan && chan->alpha;
            int used = 0;
            for (const ComponentRef &c : ref.components)
                used += !c.textureId.isEmpty();
            const bool open = m_expanded.contains(row.channelId);
            row.expand->setChecked(open);
            row.expand->setText(open ? QStringLiteral("▾") : QStringLiteral("▸"));
            row.expand->setProperty("hasContent", used > 0);
            row.expand->style()->unpolish(row.expand);
            row.expand->style()->polish(row.expand);
            row.expand->setToolTip(
                (alpha ? tr("Fill R, G, B or A from their own textures") : tr("Fill R, G or B from their own textures"))
                + (used ? tr(" (%n set)", "", used) : QString()));
            row.compPanel->setVisible(open);
            for (int c = 0; c < (alpha ? 4 : 3); ++c) {
                const ComponentRef &cr = ref.components[c];
                const bool set = !cr.textureId.isEmpty();
                const TextureEntry *ct = set ? m_project->texture(cr.textureId) : nullptr;
                auto &w = row.comps[c];
                if (ct) {
                    w.info->setText(textureLabel(cr.textureId) + QStringLiteral(" · %1×%2")
                                        .arg(ct->image.width()).arg(ct->image.height()));
                } else {
                    w.info->setText(tr("Empty: uses the main texture / default"));
                }
                w.info->setToolTip(ct ? ct->path : QString());
                if (ct && !ct->image.isNull()) {
                    w.thumb->setPixmap(QPixmap::fromImage(ct->image.scaled(
                        w.thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                } else {
                    w.thumb->setPixmap(QPixmap());
                    w.thumb->setText(QStringLiteral("—"));
                }
                w.source->setEnabled(set);
                w.source->setCurrentIndex(w.source->findData(cr.source));
                w.clear->setEnabled(set);
            }
        }
        if (!t && ref.hasComponents()) {
            row.thumb->setText(QStringLiteral("RGB"));
            row.info->setText(tr("Built from separate textures"));
        }
    }

    m_updating = false;
}
