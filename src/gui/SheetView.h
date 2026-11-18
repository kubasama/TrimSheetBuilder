#pragma once

#include "gui/Snapper.h"

#include <QGraphicsView>
#include <QHash>
#include <QMimeData>
#include <QPointer>
#include <QContextMenuEvent>
#include <QTimer>

class Project;
class ZoneItem;
class OverlayBar;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QToolButton;

enum class EditMode { Layout, Fill };

class SheetView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit SheetView(QWidget *parent = nullptr);

    void setProject(Project *project);

    EditMode mode() const { return m_mode; }
    void setMode(EditMode mode);
    QString previewChannel() const { return m_channel; }
    void setPreviewChannel(const QString &channelId);
    void setPreviewComponent(int component);
    SnapSettings snapSettings() const { return m_snap; }
    void setSnapSettings(const SnapSettings &s);
    void setShowGrid(bool show);
    void setShowZoneNames(bool show);

    QString selectedZone() const;
    void selectZone(const QString &id);

public slots:
    void zoomToFit();

signals:
    void modeChanged(EditMode mode);
    void previewChannelChanged(const QString &channelId);
    void snapSettingsChanged(const SnapSettings &s);
    void zoneSelected(const QString &zoneId);   // empty = none
    void textureDropped(const QString &zoneId, const QMimeData *mime);
    void statusMessage(const QString &text);

protected:
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void drawBackground(QPainter *p, const QRectF &r) override;
    void drawForeground(QPainter *p, const QRectF &r) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override { e->accept(); } // right = pan
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void rebuild();
    void addZoneItem(const QString &id);
    void removeZoneItem(const QString &id);
    void syncZoneItem(const QString &id);
    void scheduleComposite();
    void recomposite();
    void rebuildChannelBar();
    void placeOverlays();
    void syncOverlays();
    ZoneItem *zoneItemAt(const QPoint &viewPos) const;
    QRect constrain(const QString &id, const QRect &start, int edges, QPointF delta,
                    Qt::KeyboardModifiers mods, const QRect &last);

    QPointer<Project> m_project;
    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_preview = nullptr;
    QGraphicsRectItem *m_creating = nullptr;
    QHash<QString, ZoneItem *> m_items;
    EditMode m_mode = EditMode::Layout;
    QString m_channel;
    SnapSettings m_snap;
    QVector<QLineF> m_guides;
    bool m_showGrid = true;
    bool m_showNames = true;
    bool m_fitPending = true;

    OverlayBar *m_channelBar;
    OverlayBar *m_componentBar;
    int m_component = -1; // -1 = RGB, 0..3 = R/G/B/A shown as gray
    OverlayBar *m_modeBar;
    QToolButton *m_layoutBtn;
    QToolButton *m_fillBtn;
    QToolButton *m_snapBtn;
    QToolButton *m_overlapBtn;

    QTimer m_compositeTimer;
    bool m_panning = false;
    Qt::MouseButton m_panButton = Qt::RightButton;
    QPoint m_panLast;
    QPointF m_createStart;
    QRect m_createLast;
};
