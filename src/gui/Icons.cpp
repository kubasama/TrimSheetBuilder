#include "gui/Icons.h"
#include "gui/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {
QPen stroke(const QColor &c, double w)
{
    return QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}
} // namespace

void Icons::lock(QPainter &p, const QRectF &r, const QColor &color, bool closed)
{
    // laid out on a 16x16 grid
    p.save();
    p.translate(r.topLeft());
    p.scale(r.width() / 16.0, r.height() / 16.0);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(stroke(color, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(3.5, 7.5, 9, 6.5), 1.5, 1.5);
    QPainterPath shackle;
    if (closed) {
        shackle.moveTo(5.5, 7.5);
        shackle.lineTo(5.5, 5.5);
        shackle.arcTo(QRectF(5.5, 3, 5, 5), 180, -180);
        shackle.lineTo(10.5, 7.5);
    } else {
        // open: left leg in the body, right side lifted free
        shackle.moveTo(5.5, 7.5);
        shackle.lineTo(5.5, 4);
        shackle.arcTo(QRectF(5.5, 1.5, 5, 5), 180, -150);
    }
    p.drawPath(shackle);
    p.restore();
}

void Icons::cross(QPainter &p, const QRectF &r, const QColor &color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(stroke(color, 1.6));
    const QRectF in = r.adjusted(r.width() * 0.3, r.height() * 0.3, -r.width() * 0.3, -r.height() * 0.3);
    p.drawLine(in.topLeft(), in.bottomRight());
    p.drawLine(in.topRight(), in.bottomLeft());
    p.restore();
}

void Icons::link(QPainter &p, const QRectF &r, const QColor &color, bool linked)
{
    // two chain links, vertical; broken = pulled apart
    p.save();
    p.translate(r.topLeft());
    p.scale(r.width() / 16.0, r.height() / 16.0);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(stroke(color, 1.5));
    p.setBrush(Qt::NoBrush);
    const double gap = linked ? 0 : 1.5;
    p.drawRoundedRect(QRectF(5, 1.5 - gap, 6, 7), 3, 3);
    p.drawRoundedRect(QRectF(5, 7.5 + gap, 6, 7), 3, 3);
    if (linked)
        p.drawLine(QPointF(8, 6), QPointF(8, 10));
    p.restore();
}

QIcon Icons::linkIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32}) {
        for (bool on : {false, true}) {
            QPixmap pm(size, size);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            link(p, QRectF(0, 0, size, size), on ? Theme::accentText() : QColor(170, 170, 170), on);
            p.end();
            icon.addPixmap(pm, QIcon::Normal, on ? QIcon::On : QIcon::Off);
        }
    }
    return icon;
}
