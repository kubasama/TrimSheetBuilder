#pragma once
// Turns the project into pixels. Used by both the preview and the exporter,
// so what you see is what you export.

#include "core/Model.h"

#include <QImage>

class Project;

namespace Compositor {

// Applies flip + rotation to a source image. For normal maps the vectors are
// rotated/flipped too and converted from the source convention to the
// channel's output convention.
QImage prepareSource(const QImage &src, const FillSettings &fill, const Channel &channel,
                     NormalConvention sourceConvention);

// The zone's content at zone size (no padding). Format_ARGB32 (straight alpha).
QImage renderZone(const Project &project, const Zone &zone, const Channel &channel);

// The full sheet for one channel, Format_ARGB32. withPadding = extend zone edges outward by
// the project's padding so mip levels don't bleed neighbours into each other.
QImage renderChannel(const Project &project, const QString &channelId, bool withPadding = true);

} // namespace Compositor
