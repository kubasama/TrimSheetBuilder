#include "gui/SheetView.h"
#include "gui/OverlayBar.h"
#include "gui/Theme.h"
#include "gui/ZoneItem.h"

#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Project.h"
#include "io/ClipboardImport.h"

#include <QDragEnterEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QLayout>
#include <QScrollBar>
#include <QToolButton>
#include <QWheelEvent>
#include <cmath>

SheetView::SheetView(QWidget *parent)
    : QGraphicsView(parent), m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    setViewportUpdateMode(FullViewportUpdate);
    setAcceptDrops(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_preview = m_scene->addPixmap(QPixmap());
    m_preview->setZValue(-10);
    m_preview->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);

    m_compositeTimer.setSingleShot(true);
    m_compositeTimer.setInterval(30);
    connect(&m_compositeTimer, &QTimer::timeout, this, &SheetView::recomposite);

    connect(m_scene, &QGraphicsScene::selectionChanged, this,
            [this] { emit zoneSelected(selectedZone()); });

    // ---- overlays: channel pills (top-left), mode + snapping (top-right)
    m_channelBar = new OverlayBar(this);
    m_componentBar = new OverlayBar(this);
    m_componentBar->setCompact(true);
    {
        const char *labels[] = {"RGB", "R", "G", "B", "A"};
        for (int i = 0; i < 5; ++i) {
            QToolButton *b = m_componentBar->addButton(QString::fromLatin1(labels[i]));
            b->setProperty("component", i - 1);
            b->setToolTip(i == 0 ? tr("Show color") : tr("Show only %1, as gray").arg(QLatin1String(labels[i])));
            connect(b, &QToolButton::clicked, this, [this, i] { setPreviewComponent(i - 1); });
        }
    }

    m_modeBar = new OverlayBar(this);
    m_layoutBtn = m_modeBar->addButton(tr("Layout"));
    m_fillBtn = m_modeBar->addButton(tr("Fill"));
    m_modeBar->addSeparator();
    m_snapBtn = m_modeBar->addButton(tr("Snap"));
    m_overlapBtn = m_modeBar->addButton(tr("Overlap"));
    m_layoutBtn->setToolTip(tr("Layout: draw, move and resize zones (Ctrl+1)"));
    m_fillBtn->setToolTip(tr("Fill: shapes locked, assign textures (Ctrl+2)"));
    m_snapBtn->setToolTip(tr("Snapping to grid and zone edges (Shift+Tab)\nHold Ctrl while dragging to invert"));
    m_overlapBtn->setToolTip(tr("Allow zones to overlap"));
    connect(m_layoutBtn, &QToolButton::clicked, this, [this] { setMode(EditMode::Layout); });
    connect(m_fillBtn, &QToolButton::clicked, this, [this] { setMode(EditMode::Fill); });
    connect(m_snapBtn, &QToolButton::toggled, this, [this](bool on) {
        SnapSettings s = m_snap;
        s.enabled = on;
        setSnapSettings(s);
    });
    connect(m_overlapBtn, &QToolButton::toggled, this, [this](bool on) {
        SnapSettings s = m_snap;
        s.allowOverlap = on;
        setSnapSettings(s);
    });
    syncOverlays();
}

// ---------------------------------------------------------------- project
void SheetView::setProject(Project *project)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);
    m_project = project;
    if (m_project) {
        connect(m_project, &Project::zoneAdded, this, &SheetView::addZoneItem);
        connect(m_project, &Project::zoneRemoved, this, &SheetView::removeZoneItem);
        connect(m_project, &Project::zoneChanged, this, &SheetView::syncZoneItem);
        connect(m_project, &Project::sheetChanged, this, [this] {
            const QRectF r(QPointF(0, 0), QSizeF(m_project->sheetSize()));
            setSceneRect(r.adjusted(-r.width() * 4, -r.height() * 4, r.width() * 4, r.height() * 4));
            viewport()->update();
        });
        connect(m_project, &Project::channelsChanged, this, &SheetView::rebuildChannelBar);
        connect(m_project, &Project::contentChanged, this, &SheetView::scheduleComposite);
        if (m_channel.isEmpty() || !m_project->channel(m_channel))
            m_channel = m_project->channels().isEmpty() ? QString()
                                                        : m_project->channels().first().id;
    }
    rebuildChannelBar();
    rebuild();
    m_fitPending = true;
    if (isVisible())
        zoomToFit();
}

void SheetView::rebuild()
{
    for (ZoneItem *it : std::as_const(m_items))
        delete it;
    m_items.clear();
    if (!m_project)
        return;
    const QRectF r(QPointF(0, 0), QSizeF(m_project->sheetSize()));
    // generous scene rect so the sheet can be panned freely
    setSceneRect(r.adjusted(-r.width() * 4, -r.height() * 4, r.width() * 4, r.height() * 4));
    for (const Zone &z : m_project->zones())
        addZoneItem(z.id);
    recomposite();
}

void SheetView::addZoneItem(const QString &id)
{
    if (!m_project || m_items.contains(id))
        return;
    ZoneItem::Callbacks cb;
    cb.constrain = [this](const QString &zid, const QRect &start, int edges, QPointF delta,
                          Qt::KeyboardModifiers mods, const QRect &last) {
        return constrain(zid, start, edges, delta, mods, last);
    };
    cb.finished = [this](const QString &zid, const QRect &, const QRect &end) {
        m_guides.clear();
        viewport()->update();
        const Zone *z = m_project ? m_project->zone(zid) : nullptr;
        if (!z || z->rect == end)
            return;
        Zone after = *z;
        after.rect = end;
        m_project->undoStack()->push(
            new SetZoneCommand(m_project, *z, after, tr("Move/Resize Zone")));
    };
    cb.toggleLock = [this](const QString &zid) {
        const Zone *z = m_project ? m_project->zone(zid) : nullptr;
        if (!z)
            return;
        Zone after = *z;
        after.locked = !z->locked;
        m_project->undoStack()->push(new SetZoneCommand(
            m_project, *z, after, after.locked ? tr("Lock Zone") : tr("Unlock Zone")));
    };
    cb.remove = [this](const QString &zid) {
        // later: the item asking is the one being deleted
        QTimer::singleShot(0, this, [this, zid] {
            const Zone *z = m_project ? m_project->zone(zid) : nullptr;
            if (z && !z->locked)
                m_project->undoStack()->push(new RemoveZoneCommand(m_project, zid));
        });
    };
    auto *item = new ZoneItem(id, cb);
    m_scene->addItem(item);
    m_items.insert(id, item);
    syncZoneItem(id);
}

void SheetView::removeZoneItem(const QString &id)
{
    delete m_items.take(id);
}

void SheetView::syncZoneItem(const QString &id)
{
    ZoneItem *item = m_items.value(id);
    const Zone *z = m_project ? m_project->zone(id) : nullptr;
    if (!item || !z)
        return;
    item->setZoneRect(z->rect);
    item->setLabel(z->name);
    item->setLabelVisible(m_showNames);
    item->setLocked(z->locked);
    item->setEditable(m_mode == EditMode::Layout);
    item->setZValue(m_project->zoneIndex(id));
}

QRect SheetView::constrain(const QString &id, const QRect &start, int edges, QPointF delta,
                           Qt::KeyboardModifiers mods, const QRect &last)
{
    Snapper s(*m_project, m_snap, 1.0 / transform().m11(), mods & Qt::ControlModifier);
    const QRect r = edges == SnapEdge::Move ? s.move(id, start, delta, last)
                                            : s.resize(id, start, edges, delta, last);
    m_guides = s.guides();
    viewport()->update();
    emit statusMessage(QStringLiteral("%1, %2   %3 × %4 px")
                           .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
    return r;
}

// ---------------------------------------------------------------- mode / preview / snap
void SheetView::setMode(EditMode mode)
{
    const bool changed = mode != m_mode;
    m_mode = mode;
    for (ZoneItem *it : std::as_const(m_items))
        it->setEditable(mode == EditMode::Layout);
    m_preview->setOpacity(mode == EditMode::Layout ? 0.55 : 1.0);
    syncOverlays();
    viewport()->update();
    if (changed)
        emit modeChanged(mode);
}

void SheetView::setPreviewChannel(const QString &channelId)
{
    if (channelId == m_channel)
        return;
    m_channel = channelId;
    syncOverlays();
    recomposite();
    emit previewChannelChanged(channelId);
}

void SheetView::setSnapSettings(const SnapSettings &s)
{
    const bool changed = s.enabled != m_snap.enabled || s.grid != m_snap.grid
        || s.edges != m_snap.edges || s.allowOverlap != m_snap.allowOverlap;
    m_snap = s;
    syncOverlays();
    viewport()->update();
    if (changed)
        emit snapSettingsChanged(s);
}

void SheetView::setPreviewComponent(int component)
{
    m_component = component;
    syncOverlays();
    recomposite();
}

void SheetView::setShowGrid(bool show)
{
    m_showGrid = show;
    viewport()->update();
}

void SheetView::setShowZoneNames(bool show)
{
    m_showNames = show;
    for (ZoneItem *it : std::as_const(m_items))
        it->setLabelVisible(show);
}

void SheetView::rebuildChannelBar()
{
    // the previewed channel may have been removed
    if (m_project && !m_project->channels().isEmpty() && !m_project->channel(m_channel)) {
        m_channel = m_project->channels().first().id;
        emit previewChannelChanged(m_channel);
        scheduleComposite();
    }
    m_channelBar->clear();
    if (m_project) {
        for (const Channel &c : m_project->channels()) {
            QToolButton *b = m_channelBar->addButton(c.name);
            b->setProperty("channel", c.id);
            b->setToolTip(tr("Preview %1. Paste and drop go into this channel.").arg(c.name));
            const QString id = c.id;
            connect(b, &QToolButton::clicked, this, [this, id] { setPreviewChannel(id); });
        }
    }
    syncOverlays();
}

void SheetView::syncOverlays()
{
    {
        const QSignalBlocker b1(m_snapBtn), b2(m_overlapBtn);
        m_layoutBtn->setChecked(m_mode == EditMode::Layout);
        m_fillBtn->setChecked(m_mode == EditMode::Fill);
        m_snapBtn->setChecked(m_snap.enabled);
        m_overlapBtn->setChecked(m_snap.allowOverlap);
    }
    const bool layout = m_mode == EditMode::Layout;
    m_snapBtn->setVisible(layout);
    m_overlapBtn->setVisible(layout);
    // the separator is the widget right before the snap button
    if (auto *lay = m_modeBar->layout()) {
        const int idx = lay->indexOf(m_snapBtn);
        if (idx > 1)
            if (QWidget *sep = lay->itemAt(idx - 2)->widget())
                sep->setVisible(layout);
    }
    for (QToolButton *b : m_channelBar->findChildren<QToolButton *>())
        b->setChecked(b->property("channel").toString() == m_channel);

    // R/G/B/A view: not for grayscale; A only when the channel has alpha
    const Channel *ch = m_project ? m_project->channel(m_channel) : nullptr;
    const bool comps = ch && !ch->isGrayscale();
    if (!comps)
        m_component = -1;
    m_componentBar->setVisible(comps);
    for (QToolButton *b : m_componentBar->findChildren<QToolButton *>()) {
        const int c = b->property("component").toInt();
        b->setChecked(c == m_component);
        if (c == 3)
            b->setToolTip(ch && ch->alpha ? tr("Show only A, as gray")
                                          : tr("Show only A (this channel has no alpha, so it's all white)"));
    }
    m_componentBar->adjustSize();
    m_modeBar->adjustSize();
    m_channelBar->adjustSize();
    placeOverlays();
}

void SheetView::placeOverlays()
{
    const int m = 10;
    m_channelBar->move(m, m);
    m_componentBar->move(m, m + m_channelBar->height() + 6); // second row, under the channels
    m_modeBar->move(width() - m_modeBar->width() - m, m);
    m_channelBar->raise();
    m_componentBar->raise();
    m_modeBar->raise();
}

void SheetView::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    placeOverlays();
}

void SheetView::scheduleComposite() { m_compositeTimer.start(); }

void SheetView::recomposite()
{
    if (!m_project) {
        m_preview->setPixmap(QPixmap());
        return;
    }
    // TODO: cache per-zone renders; full recomposite is fine up to ~4K.
    QImage img = Compositor::renderChannel(*m_project, m_channel, true);
    if (m_component >= 0) {
        QImage gray(img.size(), QImage::Format_Grayscale8);
        for (int y = 0; y < img.height(); ++y) {
            const auto *s = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            uchar *d = gray.scanLine(y);
            for (int x = 0; x < img.width(); ++x) {
                const QRgb p = s[x];
                d[x] = uchar(m_component == 0 ? qRed(p) : m_component == 1 ? qGreen(p)
                             : m_component == 2 ? qBlue(p) : qAlpha(p));
            }
        }
        img = gray;
    } else {
        img = img.convertToFormat(QImage::Format_RGB32); // color only
    }
    m_preview->setPixmap(QPixmap::fromImage(img));
}

QString SheetView::selectedZone() const
{
    const auto sel = m_scene->selectedItems();
    for (QGraphicsItem *it : sel)
        if (it->type() == ZoneItem::Type)
            return static_cast<ZoneItem *>(it)->zoneId();
    return {};
}

void SheetView::selectZone(const QString &id)
{
    m_scene->clearSelection();
    if (ZoneItem *it = m_items.value(id))
        it->setSelected(true);
}

// ---------------------------------------------------------------- view nav
void SheetView::zoomToFit()
{
    if (!m_project)
        return;
    const QRectF r(QPointF(0, 0), QSizeF(m_project->sheetSize()));
    // leave room for the overlays at the top
    const double pad = 0.05;
    fitInView(r.adjusted(-r.width() * pad, -r.height() * (pad + 0.09), r.width() * pad,
                         r.height() * pad),
              Qt::KeepAspectRatio);
    m_preview->setTransformationMode(transform().m11() < 1.0 ? Qt::SmoothTransformation
                                                             : Qt::FastTransformation);
    m_fitPending = false;
}

void SheetView::showEvent(QShowEvent *e)
{
    QGraphicsView::showEvent(e);
    placeOverlays();
    if (m_fitPending)
        QTimer::singleShot(0, this, &SheetView::zoomToFit);
}

void SheetView::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0)
        return;
    double factor = std::pow(1.2, steps);
    const double cur = transform().m11();
    factor = qBound(0.02 / cur, factor, 64.0 / cur);
    scale(factor, factor);
    // nearest-neighbour when zoomed in so texels are visible
    m_preview->setTransformationMode(transform().m11() < 1.0 ? Qt::SmoothTransformation
                                                             : Qt::FastTransformation);
    e->accept();
}

ZoneItem *SheetView::zoneItemAt(const QPoint &viewPos) const
{
    const auto list = items(viewPos);
    for (QGraphicsItem *it : list)
        if (it->type() == ZoneItem::Type)
            return static_cast<ZoneItem *>(it);
    return nullptr;
}

void SheetView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panButton = e->button();
        m_panLast = e->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && m_mode == EditMode::Layout && m_project
        && !zoneItemAt(e->pos())) {
        // drag on empty canvas = draw a new zone
        const QPointF sp = mapToScene(e->pos());
        const QRectF sheet(QPointF(0, 0), QSizeF(m_project->sheetSize()));
        if (!sheet.contains(sp)) {
            m_scene->clearSelection();
            return;
        }
        m_scene->clearSelection();
        Snapper s(*m_project, m_snap, 1.0 / transform().m11(), e->modifiers() & Qt::ControlModifier);
        m_createStart = s.snapPoint(sp);
        m_createLast = QRect(m_createStart.toPoint(), QSize(1, 1));
        m_creating = m_scene->addRect(QRectF(m_createStart, QSizeF(0, 0)));
        QPen pen(Theme::accent());
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_creating->setPen(pen);
        QColor fill = Theme::accent();
        fill.setAlpha(40);
        m_creating->setBrush(fill);
        m_creating->setZValue(1e6);
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void SheetView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_panning) {
        const QPoint d = e->pos() - m_panLast;
        m_panLast = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        return;
    }
    if (m_creating) {
        const QPointF p = mapToScene(e->pos());
        const QPointF d = p - m_createStart;
        const int edges = (d.x() >= 0 ? SnapEdge::Right : SnapEdge::Left)
                        | (d.y() >= 0 ? SnapEdge::Bottom : SnapEdge::Top);
        const QRect start(m_createStart.toPoint(), QSize(0, 0));
        m_createLast = constrain(QString(), start, edges, d, e->modifiers(), m_createLast);
        m_creating->setRect(QRectF(m_createLast));
        return;
    }
    if (m_project && !(e->buttons() & Qt::LeftButton)) {
        const QPointF sp = mapToScene(e->pos());
        emit statusMessage(QStringLiteral("%1, %2 px").arg(int(std::floor(sp.x())))
                               .arg(int(std::floor(sp.y()))));
    }
    QGraphicsView::mouseMoveEvent(e);
}

void SheetView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning && e->button() == m_panButton) {
        m_panning = false;
        viewport()->unsetCursor();
        return;
    }
    if (m_creating && e->button() == Qt::LeftButton) {
        const QRect r = m_creating->rect().toRect();
        delete m_creating;
        m_creating = nullptr;
        m_guides.clear();
        viewport()->update();
        // ignore clicks that didn't really drag
        if (r.width() >= 2 && r.height() >= 2 && m_project) {
            Zone z;
            z.id = m_project->newId(QStringLiteral("z_"));
            z.name = tr("Zone %1").arg(m_project->zones().size() + 1);
            z.rect = r;
            m_project->undoStack()->push(new AddZoneCommand(m_project, z));
            selectZone(z.id);
        }
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

// ---------------------------------------------------------------- drawing
void SheetView::drawBackground(QPainter *p, const QRectF &r)
{
    p->fillRect(r, Theme::canvas());
    if (!m_project)
        return;
    // soft shadow under the sheet
    const QRectF sheet(QPointF(0, 0), QSizeF(m_project->sheetSize()));
    const double px = 1.0 / transform().m11();
    for (int i = 1; i <= 6; ++i) {
        const double o = i * 2 * px;
        p->fillRect(sheet.adjusted(-o, -o, o, o), QColor(0, 0, 0, 18));
    }
}

void SheetView::drawForeground(QPainter *p, const QRectF &r)
{
    if (!m_project)
        return;
    const QRectF sheet(QPointF(0, 0), QSizeF(m_project->sheetSize()));

    if (m_mode == EditMode::Layout && m_showGrid) {
        const int step = m_project->gridStep();
        const double px = step * transform().m11();
        if (px >= 6.0) {
            QPen pen(QColor(255, 255, 255, px >= 16 ? 26 : 14));
            pen.setCosmetic(true);
            p->setPen(pen);
            const QRectF vis = r.intersected(sheet);
            for (double x = std::ceil(vis.left() / step) * step; x <= vis.right(); x += step)
                p->drawLine(QPointF(x, vis.top()), QPointF(x, vis.bottom()));
            for (double y = std::ceil(vis.top() / step) * step; y <= vis.bottom(); y += step)
                p->drawLine(QPointF(vis.left(), y), QPointF(vis.right(), y));
        }
    }

    QPen border(QColor(255, 255, 255, 60));
    border.setCosmetic(true);
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawRect(sheet);

    if (!m_guides.isEmpty()) {
        QPen guide(Theme::accent().lighter(120));
        guide.setCosmetic(true);
        guide.setStyle(Qt::DashLine);
        p->setPen(guide);
        for (const QLineF &l : std::as_const(m_guides))
            p->drawLine(l);
    }
}

// ---------------------------------------------------------------- drag & drop
void SheetView::dragEnterEvent(QDragEnterEvent *e)
{
    if (ClipboardImport::canImport(e->mimeData()))
        e->acceptProposedAction();
    else
        e->ignore();
}

void SheetView::dragMoveEvent(QDragMoveEvent *e)
{
    if (ZoneItem *z = zoneItemAt(e->position().toPoint())) {
        selectZone(z->zoneId());
        e->acceptProposedAction();
    } else {
        e->ignore();
    }
}

void SheetView::dropEvent(QDropEvent *e)
{
    if (ZoneItem *z = zoneItemAt(e->position().toPoint())) {
        emit textureDropped(z->zoneId(), e->mimeData());
        e->acceptProposedAction();
    }
}
