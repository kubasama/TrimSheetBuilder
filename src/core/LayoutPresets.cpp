#include "core/LayoutPresets.h"

#include <QCoreApplication>
#include <QHash>
#include <QRectF>
#include <cmath>

namespace {

QString tr(const char *s) { return QCoreApplication::translate("LayoutPresets", s); }

// Layouts are built in unit space (0..1) and converted at the end.
struct Unit {
    QString prefix;
    QRectF r;
};

void addGrid(QVector<Unit> &out, const QRectF &area, int cols, int rows, const QString &prefix)
{
    const double w = area.width() / cols, h = area.height() / rows;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x)
            out.append({prefix, QRectF(area.x() + x * w, area.y() + y * h, w, h)});
}

void addStrips(QVector<Unit> &out, const QRectF &area, const QVector<double> &heights,
               const QString &prefix)
{
    double y = area.y();
    for (double h : heights) {
        out.append({prefix, QRectF(area.x(), y, area.width(), h)});
        y += h;
    }
}

// Biggest square in the top-left corner, getting smaller towards the
// bottom-right: TL quadrant = one square, TR and BL = 2x2 smaller squares,
// BR = the same pattern again, `levels` deep.
void addCascade(QVector<Unit> &out, const QRectF &area, int level, int levels)
{
    const double h = area.width() / 2;
    out.append({QStringLiteral("square"), QRectF(area.topLeft(), QSizeF(h, h))});
    addGrid(out, QRectF(area.x() + h, area.y(), h, h), 2, 2, QStringLiteral("square"));
    addGrid(out, QRectF(area.x(), area.y() + h, h, h), 2, 2, QStringLiteral("square"));
    const QRectF br(area.x() + h, area.y() + h, h, h);
    if (level < levels)
        addCascade(out, br, level + 1, levels);
    else
        addGrid(out, br, 2, 2, QStringLiteral("square"));
}

QVector<LayoutPresets::GeneratedZone> toPixels(const QVector<Unit> &units, QSize sheet, int gap)
{
    const int W = sheet.width(), H = sheet.height();
    QVector<LayoutPresets::GeneratedZone> out;
    QHash<QString, int> counters;
    for (const Unit &u : units) {
        int x0 = int(std::lround(u.r.left() * W)), x1 = int(std::lround(u.r.right() * W));
        int y0 = int(std::lround(u.r.top() * H)), y1 = int(std::lround(u.r.bottom() * H));
        // gap only between zones, never at the sheet border
        if (x0 > 0) x0 += gap / 2;
        if (y0 > 0) y0 += gap / 2;
        if (x1 < W) x1 -= gap - gap / 2;
        if (y1 < H) y1 -= gap - gap / 2;
        if (x1 - x0 < 1 || y1 - y0 < 1)
            continue;
        const int n = ++counters[u.prefix];
        out.append({QStringLiteral("%1_%2").arg(u.prefix).arg(n, 2, 10, QLatin1Char('0')),
                    QRect(x0, y0, x1 - x0, y1 - y0)});
    }
    return out;
}

} // namespace

const QVector<LayoutPresets::Preset> &LayoutPresets::all()
{
    static const QVector<Preset> presets = [] {
        QVector<Preset> p;
        auto add = [&](const char *id, const QString &name, const QString &desc,
                       const QString &c1 = {}, int d1 = 0, int min1 = 1, int max1 = 32,
                       const QString &c2 = {}, int d2 = 0, int min2 = 1, int max2 = 32) {
            p.append({QString::fromLatin1(id), name, desc, c1, d1, min1, max1, c2, d2, min2, max2});
        };
        add("empty", tr("Empty"), tr("Start from a blank sheet."));
        add("strips", tr("Equal strips"), tr("Full-width strips of equal height. They tile horizontally."),
            tr("Strips"), 8, 1, 64);
        add("halving", tr("Halving strips"),
            tr("Full-width strips, each half the height of the one above: 1/2, 1/4, 1/8 …"),
            tr("Strips"), 5, 2, 10);
        add("columns", tr("Equal columns"), tr("Full-height columns, for vertical trims."),
            tr("Columns"), 8, 1, 64);
        add("grid", tr("Square grid"), tr("N × N equal tiles."), tr("Tiles per side"), 4, 1, 32);
        add("cascade", tr("Cascading squares"),
            tr("One big square in the corner, getting smaller towards the opposite corner. "
               "Levels sets how small the smallest ones get."),
            tr("Levels"), 2, 1, 4);
        add("strips_tiles", tr("Strips + tiles"),
            tr("Strips across the top half, square tiles in the bottom half."),
            tr("Strips"), 4, 1, 32, tr("Tile columns"), 4, 1, 16);
        add("strips_panels", tr("Strips + panels"),
            tr("Strips across the top half, a few large panels in the bottom half."),
            tr("Strips"), 4, 1, 32, tr("Panels"), 2, 1, 8);
        add("hero_strips", tr("Hero panel + strips"),
            tr("A big panel on the left, full-height strips on the right."),
            tr("Strips"), 6, 1, 32);
        return p;
    }();
    return presets;
}

const LayoutPresets::Preset *LayoutPresets::find(const QString &id)
{
    for (const Preset &p : all())
        if (p.id == id)
            return &p;
    return nullptr;
}

QVector<LayoutPresets::GeneratedZone> LayoutPresets::generate(const QString &id, QSize sheet,
                                                              int count, int count2, int gap)
{
    count = qMax(1, count);
    count2 = qMax(1, count2);
    gap = qMax(0, gap);
    const QRectF unit(0, 0, 1, 1);
    const double aspect = double(sheet.width()) / qMax(1, sheet.height());
    QVector<Unit> u;

    if (id == u"strips") {
        addStrips(u, unit, QVector<double>(count, 1.0 / count), QStringLiteral("strip"));
    } else if (id == u"columns") {
        addGrid(u, unit, count, 1, QStringLiteral("column"));
    } else if (id == u"halving") {
        QVector<double> h;
        double f = 0.5;
        for (int i = 0; i < count - 1; ++i, f /= 2)
            h.append(f);
        h.append(f * 2); // last one repeats so they add up to 1
        addStrips(u, unit, h, QStringLiteral("strip"));
    } else if (id == u"grid") {
        addGrid(u, unit, count, count, QStringLiteral("tile"));
    } else if (id == u"cascade") {
        // square in pixels: work in the largest square that fits, stack if not square
        if (aspect >= 1) {
            const double w = 1.0 / aspect;
            for (double x = 0; x + 1e-9 < 1.0; x += w)
                addCascade(u, QRectF(x, 0, qMin(w, 1.0 - x), 1), 1, count);
        } else {
            for (double y = 0; y + 1e-9 < 1.0; y += aspect)
                addCascade(u, QRectF(0, y, 1, qMin(aspect, 1.0 - y)), 1, count);
        }
    } else if (id == u"strips_tiles") {
        addStrips(u, QRectF(0, 0, 1, 0.5), QVector<double>(count, 0.5 / count), QStringLiteral("strip"));
        const double tileW = 1.0 / count2;
        const int rows = qMax(1, int(std::lround(0.5 / (tileW * aspect))));
        addGrid(u, QRectF(0, 0.5, 1, 0.5), count2, rows, QStringLiteral("tile"));
    } else if (id == u"strips_panels") {
        addStrips(u, QRectF(0, 0, 1, 0.5), QVector<double>(count, 0.5 / count), QStringLiteral("strip"));
        addGrid(u, QRectF(0, 0.5, 1, 0.5), count2, 1, QStringLiteral("panel"));
    } else if (id == u"hero_strips") {
        u.append({QStringLiteral("panel"), QRectF(0, 0, 0.5, 1)});
        addGrid(u, QRectF(0.5, 0, 0.5, 1), count, 1, QStringLiteral("strip"));
    }
    return toPixels(u, sheet, gap);
}
