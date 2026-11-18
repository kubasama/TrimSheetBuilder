#pragma once

#include <QGraphicsRectItem>
#include <functional>

class QGraphicsSimpleTextItem;

// One zone on the canvas. Its rect is in sheet pixels (scene coords); pos()
// stays at 0,0. In layout mode it can be moved and resized; the view decides
// the final rect (snapping, overlap) through the constrain callback.
class ZoneItem : public QGraphicsRectItem
{
public:
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    struct Callbacks {
        // edges: SnapEdge mask (Move for moving). Returns the rect to show.
        std::function<QRect(const QString &id, const QRect &start, int edges, QPointF delta,
                            Qt::KeyboardModifiers mods, const QRect &last)> constrain;
        std::function<void(const QString &id, const QRect &start, const QRect &end)> finished;
        std::function<void(const QString &id)> toggleLock;
        std::function<void(const QString &id)> remove;
    };

    ZoneItem(const QString &zoneId, Callbacks cb);

    QString zoneId() const { return m_id; }
    void setZoneRect(const QRect &r);
    void setLabel(const QString &text);
    void setLabelVisible(bool visible);
    void setEditable(bool editable);
    void setLocked(bool locked);
    bool isLocked() const { return m_locked; }
    bool isEditable() const { return m_editable; }
    bool isDragging() const { return m_drag != 0; }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) override;
    QRectF boundingRect() const override;

protected:
    void hoverMoveEvent(QGraphicsSceneHoverEvent *e) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *e) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *e) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;

private:
    enum Button { NoButton, LockButton, DeleteButton };
    int hitTest(const QPointF &scenePos) const;
    bool showButtons() const;
    QRectF buttonRect(Button b) const;      // viewport pixels
    Button buttonAt(const QPointF &scenePos) const;
    QPointF toView(const QPointF &scenePos) const;
    double viewScale() const;
    void updateCursor(int edges);

    QString m_id;
    Callbacks m_cb;
    QGraphicsSimpleTextItem *m_label;
    QColor m_color;
    bool m_editable = true;
    bool m_locked = false;
    bool m_hover = false;
    Button m_hoverButton = NoButton;

    int m_drag = 0;
    QPointF m_pressPos;
    QRect m_startRect;
};
