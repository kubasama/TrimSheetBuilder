#pragma once
// Starting layouts for new sheets.

#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace LayoutPresets {

struct Preset {
    QString id;
    QString name;
    QString description;
    // up to two numeric parameters; empty label = unused
    QString countLabel;
    int defaultCount = 0, minCount = 1, maxCount = 32;
    QString count2Label;
    int defaultCount2 = 0, minCount2 = 1, maxCount2 = 32;
};

struct GeneratedZone {
    QString name;
    QRect rect;
};

const QVector<Preset> &all();
const Preset *find(const QString &id);

// gap = pixels left empty between neighbouring zones (room for edge padding).
// Zones touching the sheet border stay flush with it, so full-width strips
// still tile horizontally.
QVector<GeneratedZone> generate(const QString &presetId, QSize sheet, int count, int count2,
                                int gap);

} // namespace LayoutPresets
