#pragma once
// The app icon: cascading squares, yellow in the top-left corner fading to
// orange in the bottom-right. Drawn in code so it needs no SVG plugin.

#include <QIcon>
#include <QRectF>

class QPainter;

namespace AppIcon {
void paint(QPainter &p, const QRectF &target);
QIcon icon();
} // namespace AppIcon
