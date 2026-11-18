#pragma once
// Undo commands. They store full before/after snapshots of the thing they
// change, which keeps them trivial and robust.

#include "core/Model.h"

#include <QUndoCommand>
#include <optional>

class Project;

class AddZoneCommand : public QUndoCommand
{
public:
    AddZoneCommand(Project *p, const Zone &zone, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
private:
    Project *m_p;
    Zone m_zone;
    int m_index;
};

class RemoveZoneCommand : public QUndoCommand
{
public:
    RemoveZoneCommand(Project *p, const QString &zoneId, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
private:
    Project *m_p;
    Zone m_zone;
    int m_index;
};

// Any change to a zone: rect, name, material, fill settings.
class SetZoneCommand : public QUndoCommand
{
public:
    // Commands with the same non-empty mergeKey on the same zone merge
    // (e.g. consecutive spin box nudges).
    SetZoneCommand(Project *p, const Zone &before, const Zone &after, const QString &text,
                   const QString &mergeKey = {}, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return m_mergeKey.isEmpty() ? -1 : 1001; }
    bool mergeWith(const QUndoCommand *other) override;
private:
    Project *m_p;
    Zone m_before, m_after;
    QString m_mergeKey;
};

// Insert, change or (after = nullopt) remove a material.
class SetMaterialCommand : public QUndoCommand
{
public:
    SetMaterialCommand(Project *p, std::optional<Material> before, std::optional<Material> after,
                       const QString &text, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
private:
    void apply(const std::optional<Material> &state, const QString &id);
    Project *m_p;
    std::optional<Material> m_before, m_after;
    QString m_id;
};

// Higher-level edits built from the commands above (each is one undo step).
namespace Edit {
// Puts a texture into one channel of the zone's material. Creates a material
// for the zone if it has none.
void assignTexture(Project *p, const QString &zoneId, const QString &channelId,
                   const QString &textureId);
void clearTexture(Project *p, const QString &zoneId, const QString &channelId);
void setMapConvention(Project *p, const QString &zoneId, const QString &channelId,
                      NormalConvention convention);
// Packing: one component (0..3 = R/G/B/A) of a channel from its own texture.
void assignComponent(Project *p, const QString &zoneId, const QString &channelId, int component,
                     const QString &textureId);
void clearComponent(Project *p, const QString &zoneId, const QString &channelId, int component);
void setComponentSource(Project *p, const QString &zoneId, const QString &channelId,
                        int component, int source);
// Gives the zone its own new, empty material.
QString newMaterialForZone(Project *p, const QString &zoneId);
} // namespace Edit

// Replace the channel list (add / remove / reorder / edit channels).
class SetChannelsCommand : public QUndoCommand
{
public:
    SetChannelsCommand(Project *p, QVector<Channel> before, QVector<Channel> after,
                       const QString &text, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
private:
    Project *m_p;
    QVector<Channel> m_before, m_after;
};

class SetPaddingCommand : public QUndoCommand
{
public:
    SetPaddingCommand(Project *p, int before, int after, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
private:
    Project *m_p;
    int m_before, m_after;
};
