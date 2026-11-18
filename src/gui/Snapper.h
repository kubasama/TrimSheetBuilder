#pragma once
// Snapping and overlap rules for moving, resizing and drawing zones.
// All rects are in sheet pixels.

#include <QLineF>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>

class Project;

struct SnapSettings {
    bool enabled = true;        // master toggle (Ctrl while dragging inverts it)
    bool grid = true;           // snap to the project grid
    bool edges = true;          // snap to other zones' edges and the sheet border
    bool allowOverlap = false;  // when off, zones stop at their neighbours
};

namespace SnapEdge {
enum : int { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8, Move = 16 };
}

class Snapper
{
public:
    // pixelSize = sheet pixels per screen pixel (for the magnet radius).
    Snapper(const Project &project, const SnapSettings &settings, double pixelSize, bool invert);

    // Move `start` by `delta`. `last` is the previous accepted rect.
    QRect move(const QString &zoneId, const QRect &start, QPointF delta, const QRect &last);

    // Drag the given edges of `start` by `delta`. For drawing a new zone, pass
    // an empty zoneId and a zero-size `start` at the anchor point.
    QRect resize(const QString &zoneId, const QRect &start, int edges, QPointF delta,
                 const QRect &last);

    // Snaps a single point (anchor for drawing a new zone).
    QPoint snapPoint(QPointF p);

    // Guide lines for edges that snapped to something (not the grid).
    const QVector<QLineF> &guides() const { return m_guides; }

private:
    struct Snapped {
        double value;
        bool toEdge;
    };
    Snapped snapValue(double v, Qt::Orientation axis, const QString &zoneId) const;
    bool overlapsOthers(const QString &zoneId, const QRect &r) const;
    void addGuide(Qt::Orientation axis, int value);

    const Project &m_project;
    SnapSettings m_settings;
    double m_radius;
    bool m_snap;
    QVector<QLineF> m_guides;
};
