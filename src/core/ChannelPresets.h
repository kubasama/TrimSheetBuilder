#pragma once
// Sets of channels ("layers") a sheet can start with, plus JSON helpers
// shared by the project file and the custom preset store.

#include "core/Model.h"

#include <QJsonArray>
#include <QJsonObject>

namespace ChannelPresets {

struct Preset {
    QString name;
    QVector<Channel> channels;
    bool builtIn = true;
};

const QVector<Preset> &builtIns();
QVector<Channel> defaultChannels();   // Base Color + Normal + ORM

// Channel with sensible defaults for its kind.
Channel make(const QString &name, ChannelKind kind, const QString &suffix,
             const QVector<Channel> &existing = {});

// Lowercase id from a name, unique among `existing`.
QString uniqueId(const QString &name, const QVector<Channel> &existing);

QColor defaultColorFor(ChannelKind kind);

QJsonObject toJson(const Channel &c);
Channel fromJson(const QJsonObject &o);
QJsonArray toJson(const QVector<Channel> &channels);
QVector<Channel> fromJson(const QJsonArray &a);

} // namespace ChannelPresets
