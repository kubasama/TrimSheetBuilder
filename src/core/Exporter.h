#pragma once

#include <QString>
#include <QStringList>

class Project;
struct Channel;

namespace Exporter {

// <baseName><suffix>.<ext>, e.g. MySheet_BC.png
QString fileName(const Channel &ch, const QString &baseName, const QString &ext = {});

// Writes one image per exported channel, plus
// <baseName>_zones.json with pixel rects and UVs when enabled.
// Returns written files; on failure returns what was written so far and sets error.
QStringList exportSheet(const Project &project, const QString &directory,
                        const QString &baseName, QString *error);

} // namespace Exporter
