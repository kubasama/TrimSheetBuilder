#pragma once
// Small line icons drawn in code (no icon theme or SVG plugin needed).

#include <QColor>
#include <QIcon>
#include <QRectF>

class QPainter;

namespace Icons {
// draws into `r` with stroke `color`
void lock(QPainter &p, const QRectF &r, const QColor &color, bool closed);
void cross(QPainter &p, const QRectF &r, const QColor &color);
void link(QPainter &p, const QRectF &r, const QColor &color, bool linked);

QIcon linkIcon(); // on = linked, off = broken link
} // namespace Icons
