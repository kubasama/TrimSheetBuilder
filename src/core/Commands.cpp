#include "core/Commands.h"
#include "core/Project.h"

#include <QCoreApplication>

// ---------------------------------------------------------------- AddZone
AddZoneCommand::AddZoneCommand(Project *p, const Zone &zone, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Add Zone"), parent)
    , m_p(p), m_zone(zone), m_index(int(p->zones().size()))
{
}

void AddZoneCommand::redo() { m_p->insertZone(m_index, m_zone); }
void AddZoneCommand::undo() { m_p->removeZone(m_zone.id); }

// ---------------------------------------------------------------- RemoveZone
RemoveZoneCommand::RemoveZoneCommand(Project *p, const QString &zoneId, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Delete Zone"), parent)
    , m_p(p), m_index(p->zoneIndex(zoneId))
{
    if (const Zone *z = p->zone(zoneId))
        m_zone = *z;
}

void RemoveZoneCommand::redo() { m_p->removeZone(m_zone.id); }
void RemoveZoneCommand::undo() { m_p->insertZone(m_index, m_zone); }

// ---------------------------------------------------------------- SetZone
SetZoneCommand::SetZoneCommand(Project *p, const Zone &before, const Zone &after,
                               const QString &text, const QString &mergeKey,
                               QUndoCommand *parent)
    : QUndoCommand(text, parent), m_p(p), m_before(before), m_after(after), m_mergeKey(mergeKey)
{
}

void SetZoneCommand::redo() { m_p->setZone(m_after); }
void SetZoneCommand::undo() { m_p->setZone(m_before); }

bool SetZoneCommand::mergeWith(const QUndoCommand *other)
{
    auto *o = static_cast<const SetZoneCommand *>(other);
    if (o->m_mergeKey != m_mergeKey || o->m_after.id != m_after.id)
        return false;
    m_after = o->m_after;
    return true;
}

// ---------------------------------------------------------------- SetMaterial
SetMaterialCommand::SetMaterialCommand(Project *p, std::optional<Material> before,
                                       std::optional<Material> after, const QString &text,
                                       QUndoCommand *parent)
    : QUndoCommand(text, parent), m_p(p), m_before(std::move(before)), m_after(std::move(after))
{
    m_id = m_after ? m_after->id : (m_before ? m_before->id : QString());
}

void SetMaterialCommand::apply(const std::optional<Material> &state, const QString &id)
{
    if (state)
        m_p->setMaterial(*state);
    else
        m_p->removeMaterial(id);
}

void SetMaterialCommand::redo() { apply(m_after, m_id); }
void SetMaterialCommand::undo() { apply(m_before, m_id); }

// ---------------------------------------------------------------- Edit helpers
namespace {
QString tr(const char *s) { return QCoreApplication::translate("Commands", s); }

Material makeMaterialFor(Project *p, const Zone &z)
{
    Material m;
    m.id = p->newId(QStringLiteral("m_"));
    m.name = z.name.isEmpty() ? QStringLiteral("Material %1").arg(p->materials().size() + 1)
                              : z.name;
    return m;
}

// Replaces the material on the zone, creating it first if it doesn't exist.
void pushMaterialChange(Project *p, const Zone &z, const Material &after, const QString &text)
{
    QUndoStack *u = p->undoStack();
    const Material *before = p->material(after.id);
    u->beginMacro(text);
    u->push(new SetMaterialCommand(p, before ? std::optional<Material>(*before) : std::nullopt,
                                   after, text));
    if (z.materialId != after.id) {
        Zone zz = z;
        zz.materialId = after.id;
        u->push(new SetZoneCommand(p, z, zz, text));
    }
    u->endMacro();
}
} // namespace

void Edit::assignTexture(Project *p, const QString &zoneId, const QString &channelId,
                         const QString &textureId)
{
    const Zone *z = p->zone(zoneId);
    const Channel *ch = p->channel(channelId);
    if (!z || !ch || textureId.isEmpty())
        return;
    const Material *cur = p->material(z->materialId);
    Material m = cur ? *cur : makeMaterialFor(p, *z);
    MapRef ref = m.maps.value(channelId);
    if (!m.maps.contains(channelId))
        ref.convention = ch->convention;
    ref.textureId = textureId;
    m.maps.insert(channelId, ref);
    pushMaterialChange(p, *z, m, tr("Assign %1").arg(ch->name));
}

void Edit::clearTexture(Project *p, const QString &zoneId, const QString &channelId)
{
    const Zone *z = p->zone(zoneId);
    const Material *cur = z ? p->material(z->materialId) : nullptr;
    if (!cur || !cur->maps.contains(channelId) || cur->maps.value(channelId).textureId.isEmpty())
        return;
    Material m = *cur;
    m.maps[channelId].textureId.clear();
    if (m.maps.value(channelId).isEmpty())
        m.maps.remove(channelId);
    pushMaterialChange(p, *z, m, tr("Clear Texture"));
}

void Edit::assignComponent(Project *p, const QString &zoneId, const QString &channelId,
                           int component, const QString &textureId)
{
    const Zone *z = p->zone(zoneId);
    const Channel *ch = p->channel(channelId);
    if (!z || !ch || textureId.isEmpty() || component < 0 || component > 3)
        return;
    const Material *cur = p->material(z->materialId);
    Material m = cur ? *cur : makeMaterialFor(p, *z);
    MapRef &ref = m.maps[channelId];
    ref.components[component].textureId = textureId;
    pushMaterialChange(p, *z, m, tr("Assign %1 %2").arg(ch->name, componentName(component).toUpper()));
}

void Edit::clearComponent(Project *p, const QString &zoneId, const QString &channelId, int component)
{
    const Zone *z = p->zone(zoneId);
    const Material *cur = z ? p->material(z->materialId) : nullptr;
    if (!cur || !cur->maps.contains(channelId) || component < 0 || component > 3
        || cur->maps.value(channelId).components[component].textureId.isEmpty())
        return;
    Material m = *cur;
    m.maps[channelId].components[component] = ComponentRef{};
    if (m.maps.value(channelId).isEmpty())
        m.maps.remove(channelId);
    pushMaterialChange(p, *z, m, tr("Clear Texture"));
}

void Edit::setComponentSource(Project *p, const QString &zoneId, const QString &channelId,
                              int component, int source)
{
    const Zone *z = p->zone(zoneId);
    const Material *cur = z ? p->material(z->materialId) : nullptr;
    if (!cur || !cur->maps.contains(channelId) || component < 0 || component > 3
        || cur->maps.value(channelId).components[component].source == source)
        return;
    Material m = *cur;
    m.maps[channelId].components[component].source = source;
    pushMaterialChange(p, *z, m, tr("Change Source"));
}

void Edit::setMapConvention(Project *p, const QString &zoneId, const QString &channelId,
                            NormalConvention convention)
{
    const Zone *z = p->zone(zoneId);
    const Material *cur = z ? p->material(z->materialId) : nullptr;
    if (!cur || !cur->maps.contains(channelId)
        || cur->maps.value(channelId).convention == convention)
        return;
    Material m = *cur;
    m.maps[channelId].convention = convention;
    pushMaterialChange(p, *z, m, tr("Change Normal Convention"));
}

QString Edit::newMaterialForZone(Project *p, const QString &zoneId)
{
    const Zone *z = p->zone(zoneId);
    if (!z)
        return {};
    const Material m = makeMaterialFor(p, *z);
    pushMaterialChange(p, *z, m, tr("New Material"));
    return m.id;
}

// ---------------------------------------------------------------- SetPadding
SetPaddingCommand::SetPaddingCommand(Project *p, int before, int after, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Change Padding"), parent)
    , m_p(p), m_before(before), m_after(after)
{
}

void SetPaddingCommand::redo() { m_p->setPadding(m_after); }
void SetPaddingCommand::undo() { m_p->setPadding(m_before); }

// ---------------------------------------------------------------- SetChannels
SetChannelsCommand::SetChannelsCommand(Project *p, QVector<Channel> before,
                                       QVector<Channel> after, const QString &text,
                                       QUndoCommand *parent)
    : QUndoCommand(text, parent), m_p(p), m_before(std::move(before)), m_after(std::move(after))
{
}

void SetChannelsCommand::redo() { m_p->setChannels(m_after); }
void SetChannelsCommand::undo() { m_p->setChannels(m_before); }
