#include "gui/ZoneItem.h"
#include "gui/Snapper.h"
#include "gui/Icons.h"
#include "gui/Theme.h"

#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>
#include <cmath>

namespace {
constexpr double kHandlePx = 7.0; // grab distance / handle size, screen pixels
using namespace SnapEdge;
} // namespace

ZoneItem::ZoneItem(const QString &zoneId, Callbacks cb)
    : m_id(zoneId), m_cb(std::move(cb)), m_label(new QGraphicsSimpleTextItem(this))
{
    setFlag(ItemIsSelectable);
    setAcceptHoverEvents(true);
    m_label->setFlag(ItemIgnoresTransformations);
    m_label->setBrush(QColor(255, 255, 255, 220));

    // stable, muted color per zone
    const uint h = qHash(zoneId);
    m_color = QColor::fromHsv(int(h % 360), 110, 210);
}

void ZoneItem::setZoneRect(const QRect &r)
{
    prepareGeometryChange();
    setRect(QRectF(r));
    m_label->setPos(r.topLeft());
}

void ZoneItem::setLabel(const QString &text)
{
    m_label->setText(QStringLiteral(" ") + text);
}

void ZoneItem::setLabelVisible(bool visible)
{
    m_label->setVisible(visible);
}

void ZoneItem::setLocked(bool locked)
{
    m_locked = locked;
    if (locked && m_drag != None)
        m_drag = None;
    update();
}

QPointF ZoneItem::toView(const QPointF &scenePos) const
{
    if (!scene() || scene()->views().isEmpty())
        return scenePos;
    return scene()->views().first()->mapFromScene(scenePos);
}

bool ZoneItem::showButtons() const
{
    if (!m_editable || !isSelected())
        return false;
    const QPointF tl = toView(rect().topLeft()), br = toView(rect().bottomRight());
    return br.x() - tl.x() >= 64 && br.y() - tl.y() >= 30; // too small: use the inspector
}

QRectF ZoneItem::buttonRect(Button b) const
{
    const QPointF tr = toView(rect().topRight());
    const double size = 20, gap = 4, margin = 5;
    const double x = tr.x() - margin - size - (b == LockButton ? size + gap : 0);
    return QRectF(x, tr.y() + margin, size, size);
}

ZoneItem::Button ZoneItem::buttonAt(const QPointF &scenePos) const
{
    if (!showButtons())
        return NoButton;
    const QPointF v = toView(scenePos);
    if (buttonRect(LockButton).contains(v))
        return LockButton;
    if (buttonRect(DeleteButton).contains(v) && !m_locked)
        return DeleteButton;
    return NoButton;
}

void ZoneItem::setEditable(bool editable)
{
    m_editable = editable;
    if (!editable)
        unsetCursor();
    update();
}

double ZoneItem::viewScale() const
{
    if (!scene() || scene()->views().isEmpty())
        return 1.0;
    const double s = scene()->views().first()->transform().m11();
    return s > 0 ? s : 1.0;
}

QRectF ZoneItem::boundingRect() const
{
    const double m = (kHandlePx + 2) / viewScale();
    return rect().adjusted(-m, -m, m, m);
}

void ZoneItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QRectF r = rect();
    const bool sel = isSelected();
    const QColor accent = Theme::accent();

    if (m_editable) {
        QColor fill = sel ? accent : m_color;
        fill.setAlpha(sel ? 70 : 45);
        p->fillRect(r, fill);
    }

    QPen pen(sel ? accent : (m_editable ? m_color : QColor(255, 255, 255, 70)));
    pen.setCosmetic(true);
    pen.setWidthF(sel ? 2.0 : 1.0);
    if (m_locked && m_editable)
        pen.setStyle(Qt::DashLine);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRect(r);

    // buttons are drawn in screen pixels, only on the selected zone
    if (showButtons()) {
        p->save();
        p->resetTransform(); // viewport pixels from here
        p->setRenderHint(QPainter::Antialiasing);
        auto drawButton = [&](Button b) {
            const QRectF br = buttonRect(b);
            const bool hot = m_hoverButton == b;
            QColor bg, fg(240, 240, 240, hot ? 255 : 170);
            if (b == DeleteButton) {
                bg = hot ? QColor(205, 55, 55, 235) : QColor(120, 40, 40, 120);
            } else if (m_locked) {
                bg = accent;
                bg.setAlpha(hot ? 240 : 150);
                fg = Theme::accentText();
                fg.setAlpha(hot ? 255 : 200);
            } else {
                bg = QColor(20, 20, 20, hot ? 225 : 110);
            }
            p->setPen(Qt::NoPen);
            p->setBrush(bg);
            p->drawRoundedRect(br, 4, 4);
            const QRectF icon = br.adjusted(3, 3, -3, -3);
            if (b == LockButton)
                Icons::lock(*p, icon, fg, m_locked);
            else
                Icons::cross(*p, icon, fg);
        };
        drawButton(LockButton);
        if (!m_locked)
            drawButton(DeleteButton);
        p->restore();
    }

    if (sel && m_editable && !m_locked) {
        const double hs = kHandlePx / viewScale();
        QPen hp(QColor(20, 20, 20));
        hp.setCosmetic(true);
        p->setPen(hp);
        p->setBrush(accent);
        const QPointF pts[] = {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight(),
                               QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom()),
                               QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y())};
        for (const QPointF &c : pts)
            p->drawRect(QRectF(c.x() - hs / 2, c.y() - hs / 2, hs, hs));
    }
}

int ZoneItem::hitTest(const QPointF &pos) const
{
    const QRectF r = rect();
    const double tol = kHandlePx / viewScale();
    if (!r.adjusted(-tol, -tol, tol, tol).contains(pos))
        return None;
    int e = None;
    if (std::abs(pos.x() - r.left()) <= tol)   e |= Left;
    if (std::abs(pos.x() - r.right()) <= tol)  e |= Right;
    if (std::abs(pos.y() - r.top()) <= tol)    e |= Top;
    if (std::abs(pos.y() - r.bottom()) <= tol) e |= Bottom;
    // tiny zones: prefer moving over grabbing both opposite edges
    if ((e & Left) && (e & Right)) e &= ~(Left | Right);
    if ((e & Top) && (e & Bottom)) e &= ~(Top | Bottom);
    if (e == None && r.contains(pos))
        e = Move;
    return e;
}

void ZoneItem::updateCursor(int e)
{
    if (!m_editable || e == None) {
        unsetCursor();
        return;
    }
    if (e == Move)                                       setCursor(Qt::SizeAllCursor);
    else if (e == (Left | Top) || e == (Right | Bottom)) setCursor(Qt::SizeFDiagCursor);
    else if (e == (Right | Top) || e == (Left | Bottom)) setCursor(Qt::SizeBDiagCursor);
    else if (e & (Left | Right))                         setCursor(Qt::SizeHorCursor);
    else                                                 setCursor(Qt::SizeVerCursor);
}

void ZoneItem::hoverEnterEvent(QGraphicsSceneHoverEvent *e)
{
    m_hover = true;
    update();
    QGraphicsRectItem::hoverEnterEvent(e);
}

void ZoneItem::hoverMoveEvent(QGraphicsSceneHoverEvent *e)
{
    const Button b = buttonAt(e->scenePos());
    if (b != m_hoverButton) {
        m_hoverButton = b;
        update();
    }
    if (b != NoButton)
        setCursor(Qt::PointingHandCursor);
    else if (m_locked)
        unsetCursor();
    else
        updateCursor(isSelected() ? hitTest(e->scenePos()) : (m_editable ? int(Move) : int(None)));
    QGraphicsRectItem::hoverMoveEvent(e);
}

void ZoneItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *e)
{
    m_hover = false;
    m_hoverButton = NoButton;
    unsetCursor();
    update();
    QGraphicsRectItem::hoverLeaveEvent(e);
}

void ZoneItem::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const Button b = buttonAt(e->scenePos());
        if (b == LockButton && m_cb.toggleLock) {
            m_cb.toggleLock(m_id);
            e->accept();
            return;
        }
        if (b == DeleteButton && m_cb.remove) {
            m_cb.remove(m_id); // deletes this item: return right away
            e->accept();
            return;
        }
    }
    QGraphicsRectItem::mousePressEvent(e); // selection
    if (!m_editable || m_locked || e->button() != Qt::LeftButton)
        return;
    m_drag = isSelected() ? hitTest(e->scenePos()) : int(Move);
    if (m_drag == None)
        m_drag = Move;
    m_pressPos = e->scenePos();
    m_startRect = rect().toRect();
    e->accept();
}

void ZoneItem::mouseMoveEvent(QGraphicsSceneMouseEvent *e)
{
    if (!m_editable || m_drag == None || !m_cb.constrain)
        return;
    const QRect r = m_cb.constrain(m_id, m_startRect, m_drag, e->scenePos() - m_pressPos,
                                   e->modifiers(), rect().toRect());
    setZoneRect(r);
}

void ZoneItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *e)
{
    QGraphicsRectItem::mouseReleaseEvent(e);
    if (m_drag == None)
        return;
    m_drag = None;
    if (m_cb.finished)
        m_cb.finished(m_id, m_startRect, rect().toRect()); // view turns this into an undo command
}
