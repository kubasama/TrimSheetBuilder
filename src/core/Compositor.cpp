#include "core/Compositor.h"
#include "core/Project.h"

#include <QPainter>
#include <QTransform>
#include <QtGlobal>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

// Splits a clockwise angle into whole quarter turns (done exactly, by
// rotating pixels) and a remainder in (0, 90) (done by the painter).
void splitRotation(double angle, int *quarters, double *residual)
{
    angle = std::fmod(angle, 360.0);
    if (angle < 0)
        angle += 360.0;
    int q = int(std::floor(angle / 90.0 + 1e-9));
    double r = angle - q * 90.0;
    if (r < 1e-6) r = 0;
    if (r > 90.0 - 1e-6) { r = 0; ++q; }
    *quarters = q % 4;
    *residual = r;
}

// 2x2 integer matrix acting on the (x, y) of a tangent-space normal.
struct Mat2 {
    int a = 1, b = 0, c = 0, d = 1; // [a b; c d]
    Mat2 operator*(const Mat2 &o) const
    {
        return {a * o.a + b * o.c, a * o.b + b * o.d, c * o.a + d * o.c, c * o.b + d * o.d};
    }
};

const Mat2 kFlipY{1, 0, 0, -1};

// Builds the vector transform matching the image transform, working in
// OpenGL (Y up) space in the middle.
Mat2 normalMatrix(const FillSettings &f, int quarters, NormalConvention src, NormalConvention dst)
{
    Mat2 m; // applied right-to-left
    if (src == NormalConvention::DirectX)
        m = kFlipY * m;                        // DX -> GL
    if (f.flipX)
        m = Mat2{-1, 0, 0, 1} * m;
    if (f.flipY)
        m = kFlipY * m;
    for (int i = 0; i < quarters; ++i)
        m = Mat2{0, 1, -1, 0} * m;             // 90° clockwise on screen: (x,y) -> (y,-x)
    if (dst == NormalConvention::DirectX)
        m = kFlipY * m;                        // GL -> DX
    return m;
}

// Matrix entries are 0/±1, so every output component is either an input
// component or its negation. Negation is 256 - v (clamped) so 128 stays 128
// and flat normals stay exactly flat after any rotation.
void applyNormalMatrix(QImage &img, const Mat2 &m)
{
    if (m.a == 1 && m.b == 0 && m.c == 0 && m.d == 1)
        return;
    img = img.convertToFormat(QImage::Format_ARGB32);
    auto neg = [](int v) { return qMin(255, 256 - v); };
    auto component = [neg](int ca, int cb, int r, int g) -> int {
        if (ca == 1)  return r;
        if (ca == -1) return neg(r);
        if (cb == 1)  return g;
        if (cb == -1) return neg(g);
        return 128;
    };
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            const int r = qRed(p), g = qGreen(p);
            line[x] = qRgba(component(m.a, m.b, r, g), component(m.c, m.d, r, g), qBlue(p),
                            qAlpha(p));
        }
    }
}

// Free angle: (x, y) rotated by -angle in Y-up space (= clockwise on screen),
// with flips and convention changes around it. Done in floating point.
void applyNormalRotation(QImage &img, const FillSettings &f, double angleDeg,
                         NormalConvention src, NormalConvention dst)
{
    const double t = qDegreesToRadians(angleDeg);
    const double c = std::cos(t), s = std::sin(t);
    const double inY = src == NormalConvention::DirectX ? -1 : 1;
    const double outY = dst == NormalConvention::DirectX ? -1 : 1;
    const double fx = f.flipX ? -1 : 1, fy = f.flipY ? -1 : 1;
    auto dec = [](int v) { return qBound(-1.0, (v - 128) / 127.0, 1.0); };
    auto enc = [](double x) { return qBound(0, int(std::lround(x * 127.0 + 128.0)), 255); };

    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            const double nx = dec(qRed(p)) * fx;
            const double ny = dec(qGreen(p)) * inY * fy;
            const double rx = nx * c + ny * s;
            const double ry = -nx * s + ny * c;
            line[x] = qRgba(enc(rx), enc(ry * outY), qBlue(p), qAlpha(p));
        }
    }
}

QImage flipped(const QImage &img, bool h, bool v)
{
    if (!h && !v)
        return img;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    Qt::Orientations o;
    if (h) o |= Qt::Horizontal;
    if (v) o |= Qt::Vertical;
    return img.flipped(o);
#else
    return img.mirrored(h, v);
#endif
}

} // namespace

QImage Compositor::prepareSource(const QImage &src, const FillSettings &fill,
                                 const Channel &channel, NormalConvention sourceConvention)
{
    QImage img = flipped(src.convertToFormat(QImage::Format_ARGB32), fill.flipX, fill.flipY);
    int quarters;
    double residual;
    splitRotation(fill.rotation, &quarters, &residual);
    if (quarters != 0)
        img = img.transformed(QTransform().rotate(quarters * 90));
    if (channel.isNormal()) {
        // vectors turn by the full angle; pixels by the quarters here and the
        // remainder when painted
        if (residual == 0)
            applyNormalMatrix(img, normalMatrix(fill, quarters, sourceConvention, channel.convention));
        else
            applyNormalRotation(img, fill, fill.rotation, sourceConvention, channel.convention);
    }
    return img;
}

namespace {

// Paints `src` into `out` (zone-sized) the way the fill settings say.
// `src` is already flipped / rotated by whole quarter turns.
void paintFill(QImage &out, const QImage &src, const FillSettings &f)
{
    const QSize size = out.size();
    const double w = size.width(), h = size.height();
    int quarters;
    double residual;
    splitRotation(f.rotation, &quarters, &residual);
    // pivot for free rotation; whole pixels so 0° stays pixel exact
    const QPointF center(std::floor(w / 2), std::floor(h / 2));

    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (residual != 0 && f.mode != FillMode::Tile) {
        p.translate(center);
        p.rotate(residual);
        p.translate(-center);
    }
    switch (f.mode) {
    case FillMode::Stretch:
        p.drawImage(QRectF(0, 0, w, h), src);
        break;
    case FillMode::Fit:
    case FillMode::Crop: {
        QSizeF s = QSizeF(src.size()).scaled(
            size, f.mode == FillMode::Fit ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding);
        const QPointF origin((w - s.width()) / 2.0 + f.offset.x() * w,
                             (h - s.height()) / 2.0 + f.offset.y() * h);
        p.drawImage(QRectF(origin, s), src);
        break;
    }
    case FillMode::Tile: {
        QBrush brush(src);
        const double sx = f.scale.x() > 0 ? f.scale.x() : 1.0;
        const double sy = f.scale.y() > 0 ? f.scale.y() : 1.0;
        // scale, then rotate the remainder, then move to center + offset
        brush.setTransform(QTransform::fromScale(sx, sy) * QTransform().rotate(residual)
                           * QTransform::fromTranslate(center.x() + std::round(f.offset.x() * w),
                                                       center.y() + std::round(f.offset.y() * h)));
        p.fillRect(QRectF(0, 0, w, h), brush);
        break;
    }
    }
}

inline int componentOf(QRgb p, int c)
{
    switch (c) {
    case 0: return qRed(p);
    case 1: return qGreen(p);
    case 2: return qBlue(p);
    case 3: return qAlpha(p);
    default: return qGray(p);
    }
}

inline QRgb withComponent(QRgb p, int c, int v)
{
    switch (c) {
    case 0: return qRgba(v, qGreen(p), qBlue(p), qAlpha(p));
    case 1: return qRgba(qRed(p), v, qBlue(p), qAlpha(p));
    case 2: return qRgba(qRed(p), qGreen(p), v, qAlpha(p));
    default: return qRgba(qRed(p), qGreen(p), qBlue(p), v);
    }
}

// One component of `src` as an opaque gray image. Working on opaque images
// keeps color under transparent pixels intact through painting.
QImage opaqueGray(const QImage &src, int component)
{
    QImage in = src.convertToFormat(QImage::Format_ARGB32);
    QImage out(in.size(), QImage::Format_ARGB32);
    for (int y = 0; y < in.height(); ++y) {
        const auto *s = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        auto *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            const int v = componentOf(s[x], component);
            d[x] = qRgb(v, v, v);
        }
    }
    return out;
}

QImage opaque(const QImage &src)
{
    return src.convertToFormat(QImage::Format_RGB32).convertToFormat(QImage::Format_ARGB32);
}

// Fills component `target` of `out` from `component` of `texture`, laid out
// with the zone's fill settings. Outside the texture: the channel default.
void packComponent(QImage &out, const QImage &texture, int component, int target,
                   const FillSettings &f, const Channel &plain, int fallback)
{
    QImage part(out.size(), QImage::Format_ARGB32);
    part.fill(qRgb(fallback, fallback, fallback));
    paintFill(part, Compositor::prepareSource(opaqueGray(texture, component), f, plain,
                                              NormalConvention::DirectX),
              f);
    for (int y = 0; y < out.height(); ++y) {
        const auto *s = reinterpret_cast<const QRgb *>(part.constScanLine(y));
        auto *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
            d[x] = withComponent(d[x], target, qRed(s[x]));
    }
}

void forceOpaque(QImage &img)
{
    for (int y = 0; y < img.height(); ++y) {
        auto *d = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            d[x] |= 0xff000000u;
    }
}

// Copies `src` into `dst` at `pos`, clipped. Plain pixel copy: no blending,
// so alpha and the color under it survive.
void blit(QImage &dst, const QImage &src, QPoint pos, QRect srcRect = {})
{
    if (srcRect.isNull())
        srcRect = src.rect();
    const QRect target = QRect(pos, srcRect.size()).intersected(dst.rect());
    if (target.isEmpty())
        return;
    const int sx = srcRect.x() + (target.x() - pos.x());
    const int sy = srcRect.y() + (target.y() - pos.y());
    for (int y = 0; y < target.height(); ++y) {
        const auto *s = reinterpret_cast<const QRgb *>(src.constScanLine(sy + y)) + sx;
        auto *d = reinterpret_cast<QRgb *>(dst.scanLine(target.y() + y)) + target.x();
        std::copy(s, s + target.width(), d);
    }
}

// Extends a zone's border pixels outward by `pad`.
void bleed(QImage &dst, const QImage &z, const QRect &r, int pad)
{
    const int w = r.width(), h = r.height();
    if (w <= 0 || h <= 0 || pad <= 0)
        return;
    for (int i = 1; i <= pad; ++i) {
        blit(dst, z, QPoint(r.x(), r.y() - i), QRect(0, 0, w, 1));
        blit(dst, z, QPoint(r.x(), r.y() + h - 1 + i), QRect(0, h - 1, w, 1));
        blit(dst, z, QPoint(r.x() - i, r.y()), QRect(0, 0, 1, h));
        blit(dst, z, QPoint(r.x() + w - 1 + i, r.y()), QRect(w - 1, 0, 1, h));
    }
    auto corner = [&](int x, int y, QRgb c) {
        const QRect cr = QRect(x, y, pad, pad).intersected(dst.rect());
        for (int yy = cr.top(); yy <= cr.bottom(); ++yy) {
            auto *d = reinterpret_cast<QRgb *>(dst.scanLine(yy));
            std::fill(d + cr.left(), d + cr.right() + 1, c);
        }
    };
    corner(r.x() - pad, r.y() - pad, z.pixel(0, 0));
    corner(r.x() + w, r.y() - pad, z.pixel(w - 1, 0));
    corner(r.x() - pad, r.y() + h, z.pixel(0, h - 1));
    corner(r.x() + w, r.y() + h, z.pixel(w - 1, h - 1));
}

} // namespace

QImage Compositor::renderZone(const Project &project, const Zone &zone, const Channel &channel)
{
    const QSize size = zone.rect.size();
    QImage out(size, QImage::Format_ARGB32);
    if (size.isEmpty() || out.isNull()) // empty zone, or out of memory
        return out;
    out.fill(channel.defaultColor);

    const Material *mat = project.material(zone.materialId);
    auto mapIt = mat ? mat->maps.constFind(channel.id) : QHash<QString, MapRef>::const_iterator();
    if (!mat || mapIt == mat->maps.cend())
        return out;
    const MapRef &map = *mapIt;
    const FillSettings &f = zone.fill;

    Channel plain = channel; // for component textures: no normal-map handling
    plain.kind = ChannelKind::Data;

    const TextureEntry *tex = project.texture(map.textureId);
    if (tex && !tex->image.isNull()) {
        const QImage src = prepareSource(tex->image, f, channel, map.convention);
        if (channel.alpha && !channel.isNormal() && !channel.isGrayscale()) {
            // color and alpha separately, so color under transparency survives
            paintFill(out, opaque(src), f);
            packComponent(out, tex->image, 3, 3, f, plain, channel.defaultColor.alpha());
        } else {
            paintFill(out, src, f); // transparent parts show the channel default
        }
    }

    if (!channel.isNormal() && !channel.isGrayscale()) {
        const int defaults[4] = {channel.defaultColor.red(), channel.defaultColor.green(),
                                 channel.defaultColor.blue(), channel.defaultColor.alpha()};
        for (int c = 0; c < 4; ++c) {
            const ComponentRef &cr = map.components[c];
            if (cr.textureId.isEmpty() || (c == 3 && !channel.alpha))
                continue;
            const TextureEntry *ct = project.texture(cr.textureId);
            if (ct && !ct->image.isNull())
                packComponent(out, ct->image, cr.source, c, f, plain, defaults[c]);
        }
    }
    if (!channel.alpha)
        forceOpaque(out);
    return out;
}

QImage Compositor::renderChannel(const Project &project, const QString &channelId,
                                 bool withPadding)
{
    const Channel *ch = project.channel(channelId);
    QImage sheet(project.sheetSize(), QImage::Format_ARGB32);
    if (sheet.isNull()) // out of memory for a huge sheet
        return sheet;
    sheet.fill(ch ? ch->defaultColor : QColor(Qt::black));
    if (!ch)
        return sheet;

    QVector<QImage> rendered;
    rendered.reserve(project.zones().size());
    for (const Zone &z : project.zones())
        rendered.append(renderZone(project, z, *ch));

    if (withPadding && project.padding() > 0) {
        // all bleed first, then all content, so bleed never covers a neighbour
        for (int i = 0; i < rendered.size(); ++i)
            bleed(sheet, rendered[i], project.zones()[i].rect, project.padding());
    }
    for (int i = 0; i < rendered.size(); ++i)
        blit(sheet, rendered[i], project.zones()[i].rect.topLeft());

    if (ch->isGrayscale())
        sheet = sheet.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32);
    else if (!ch->alpha)
        forceOpaque(sheet);
    return sheet;
}
