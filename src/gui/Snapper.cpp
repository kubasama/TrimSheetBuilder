#include "gui/Snapper.h"
#include "core/Project.h"

#include <cmath>

namespace {
// exclusive right / bottom
inline int R(const QRect &r) { return r.x() + r.width(); }
inline int B(const QRect &r) { return r.y() + r.height(); }
} // namespace

Snapper::Snapper(const Project &project, const SnapSettings &settings, double pixelSize,
                 bool invert)
    : m_project(project)
    , m_settings(settings)
    , m_radius(8.0 * pixelSize)
    , m_snap(settings.enabled != invert)
{
}

Snapper::Snapped Snapper::snapValue(double v, Qt::Orientation axis, const QString &zoneId) const
{
    if (!m_snap)
        return {double(std::lround(v)), false};

    if (m_settings.edges) {
        const QSize s = m_project.sheetSize();
        double best = 0, bestDist = m_radius + 1;
        auto consider = [&](int c) {
            const double d = std::abs(v - c);
            if (d < bestDist) {
                bestDist = d;
                best = c;
            }
        };
        consider(0);
        consider(axis == Qt::Horizontal ? s.width() : s.height());
        for (const Zone &z : m_project.zones()) {
            if (z.id == zoneId)
                continue;
            if (axis == Qt::Horizontal) {
                consider(z.rect.x());
                consider(R(z.rect));
            } else {
                consider(z.rect.y());
                consider(B(z.rect));
            }
        }
        if (bestDist <= m_radius)
            return {best, true};
    }
    if (m_settings.grid) {
        const int step = qMax(1, m_project.gridStep());
        return {double(std::lround(v / step) * step), false};
    }
    return {double(std::lround(v)), false};
}

bool Snapper::overlapsOthers(const QString &zoneId, const QRect &r) const
{
    if (r.isEmpty())
        return false;
    for (const Zone &z : m_project.zones())
        if (z.id != zoneId && z.rect.intersects(r))
            return true;
    return false;
}

void Snapper::addGuide(Qt::Orientation axis, int value)
{
    const QSize s = m_project.sheetSize();
    if (axis == Qt::Horizontal)
        m_guides.append(QLineF(value, 0, value, s.height()));
    else
        m_guides.append(QLineF(0, value, s.width(), value));
}

QPoint Snapper::snapPoint(QPointF p)
{
    m_guides.clear();
    const QSize s = m_project.sheetSize();
    const Snapped x = snapValue(p.x(), Qt::Horizontal, {});
    const Snapped y = snapValue(p.y(), Qt::Vertical, {});
    if (x.toEdge) addGuide(Qt::Horizontal, int(x.value));
    if (y.toEdge) addGuide(Qt::Vertical, int(y.value));
    return {qBound(0, int(x.value), s.width()), qBound(0, int(y.value), s.height())};
}

QRect Snapper::move(const QString &zoneId, const QRect &start, QPointF delta, const QRect &last)
{
    m_guides.clear();
    const QSize sheet = m_project.sheetSize();
    const int w = start.width(), h = start.height();

    auto axisMove = [&](double rawLo, int size, Qt::Orientation axis, int limit) -> int {
        const Snapped lo = snapValue(rawLo, axis, zoneId);
        const Snapped hi = snapValue(rawLo + size, axis, zoneId);
        int result;
        if (lo.toEdge && (!hi.toEdge || std::abs(lo.value - rawLo) <= std::abs(hi.value - rawLo - size))) {
            result = int(lo.value);
        } else if (hi.toEdge) {
            result = int(hi.value) - size;
        } else {
            result = int(lo.value);
        }
        result = qBound(0, result, qMax(0, limit - size));
        if (lo.toEdge && int(lo.value) == result)
            addGuide(axis, result);
        if (hi.toEdge && int(hi.value) == result + size)
            addGuide(axis, result + size);
        return result;
    };

    const int x = axisMove(start.x() + delta.x(), w, Qt::Horizontal, sheet.width());
    const int y = axisMove(start.y() + delta.y(), h, Qt::Vertical, sheet.height());
    QRect cand(x, y, w, h);

    if (!m_settings.allowOverlap && !overlapsOthers(zoneId, start) && overlapsOthers(zoneId, cand)) {
        m_guides.clear();
        // slide along whatever we hit
        const QRect onlyX(cand.x(), last.y(), w, h);
        const QRect onlyY(last.x(), cand.y(), w, h);
        if (!overlapsOthers(zoneId, onlyX))
            return onlyX;
        if (!overlapsOthers(zoneId, onlyY))
            return onlyY;
        return last;
    }
    return cand;
}

QRect Snapper::resize(const QString &zoneId, const QRect &start, int edges, QPointF delta,
                      const QRect &last)
{
    m_guides.clear();
    const QSize sheet = m_project.sheetSize();
    int l = start.x(), t = start.y(), r = R(start), b = B(start);

    struct EdgeSnap { bool snapped = false; int value = 0; };
    EdgeSnap sl, sr, st, sb;
    auto apply = [&](int &edge, double raw, Qt::Orientation axis, EdgeSnap &es) {
        const Snapped s = snapValue(raw, axis, zoneId);
        edge = int(s.value);
        es = {s.toEdge, edge};
    };
    if (edges & SnapEdge::Left)   apply(l, start.x() + delta.x(), Qt::Horizontal, sl);
    if (edges & SnapEdge::Right)  apply(r, R(start) + delta.x(), Qt::Horizontal, sr);
    if (edges & SnapEdge::Top)    apply(t, start.y() + delta.y(), Qt::Vertical, st);
    if (edges & SnapEdge::Bottom) apply(b, B(start) + delta.y(), Qt::Vertical, sb);

    l = qBound(0, l, sheet.width());
    r = qBound(0, r, sheet.width());
    t = qBound(0, t, sheet.height());
    b = qBound(0, b, sheet.height());
    if (edges & SnapEdge::Left)   l = qMin(l, r - 1);
    if (edges & SnapEdge::Right)  r = qMax(r, l + 1);
    if (edges & SnapEdge::Top)    t = qMin(t, b - 1);
    if (edges & SnapEdge::Bottom) b = qMax(b, t + 1);

    if (!m_settings.allowOverlap && !overlapsOthers(zoneId, start)) {
        // stop moving edges at the first neighbour in their way
        for (const Zone &z : m_project.zones()) {
            if (z.id == zoneId)
                continue;
            const QRect &o = z.rect;
            if (o.y() < b && B(o) > t) {
                if ((edges & SnapEdge::Right) && o.x() >= R(start) && o.x() < r)
                    r = o.x();
                if ((edges & SnapEdge::Left) && R(o) <= start.x() && R(o) > l)
                    l = R(o);
            }
        }
        for (const Zone &z : m_project.zones()) {
            if (z.id == zoneId)
                continue;
            const QRect &o = z.rect;
            if (o.x() < r && R(o) > l) {
                if ((edges & SnapEdge::Bottom) && o.y() >= B(start) && o.y() < b)
                    b = o.y();
                if ((edges & SnapEdge::Top) && B(o) <= start.y() && B(o) > t)
                    t = B(o);
            }
        }
        const QRect cand(l, t, r - l, b - t);
        if (r <= l || b <= t || overlapsOthers(zoneId, cand))
            return last;
    }

    if (r <= l || b <= t)
        return last;
    if (sl.snapped && sl.value == l) addGuide(Qt::Horizontal, l);
    if (sr.snapped && sr.value == r) addGuide(Qt::Horizontal, r);
    if (st.snapped && st.value == t) addGuide(Qt::Vertical, t);
    if (sb.snapped && sb.value == b) addGuide(Qt::Vertical, b);
    return QRect(l, t, r - l, b - t);
}
