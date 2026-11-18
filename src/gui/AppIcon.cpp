#include "gui/AppIcon.h"
#include "core/LayoutPresets.h"

#include <QPainter>
#include <QPixmap>

namespace {
const QColor kYellow(0xff, 0xd4, 0x3b);
const QColor kOrange(0xff, 0x6a, 0x13);
const QColor kBackground(0x2a, 0x25, 0x21);

QColor lerp(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}
} // namespace

void AppIcon::paint(QPainter &p, const QRectF &target)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const double s = qMin(target.width(), target.height());
    const QRectF r(target.center() - QPointF(s / 2, s / 2), QSizeF(s, s));
    p.setPen(Qt::NoPen);
    p.setBrush(kBackground);
    p.drawRoundedRect(r.adjusted(s * 0.03, s * 0.03, -s * 0.03, -s * 0.03), s * 0.2, s * 0.2);

    // fewer, bigger squares at small sizes
    const int levels = s <= 32 ? 1 : 2;
    const int N = 1000, gap = s <= 32 ? 70 : 44;
    const auto zones = LayoutPresets::generate(QStringLiteral("cascade"), QSize(N, N), levels, 0, gap);
    const QRectF inner = r.adjusted(s * 0.17, s * 0.17, -s * 0.17, -s * 0.17);
    const double k = inner.width() / N;
    for (const auto &z : zones) {
        const QRectF zr(inner.x() + z.rect.x() * k, inner.y() + z.rect.y() * k,
                        z.rect.width() * k, z.rect.height() * k);
        const double t = (z.rect.center().x() + z.rect.center().y()) / double(2 * N);
        p.setBrush(lerp(kYellow, kOrange, qBound(0.0, (t - 0.15) / 0.75, 1.0)));
        p.drawRoundedRect(zr, zr.width() * 0.12, zr.width() * 0.12);
    }
    p.restore();
}

QIcon AppIcon::icon()
{
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(p, QRectF(0, 0, size, size));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}
