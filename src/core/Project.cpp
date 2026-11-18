#include "core/Project.h"
#include "core/ChannelPresets.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QUuid>

namespace {
QString shortHash(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(16));
}

QImage readImage(const QString &path, QString *error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull() && error)
        *error = reader.errorString();
    return img;
}
} // namespace

Project::Project(QObject *parent)
    : QObject(parent)
{
    connect(this, &Project::sheetChanged, this, &Project::contentChanged);
    connect(this, &Project::channelsChanged, this, &Project::contentChanged);
    connect(this, &Project::zoneAdded, this, &Project::contentChanged);
    connect(this, &Project::zoneRemoved, this, &Project::contentChanged);
    connect(this, &Project::zoneChanged, this, &Project::contentChanged);
    connect(this, &Project::materialsChanged, this, &Project::contentChanged);
    connect(this, &Project::textureChanged, this, &Project::contentChanged);
}

Project *Project::createDefault(QSize size, QObject *parent, const QVector<Channel> &channels)
{
    auto *p = new Project(parent);
    p->m_size = size;
    p->m_gridStep = qMax(1, qMin(size.width(), size.height()) / 128);
    p->m_channels = channels.isEmpty() ? ChannelPresets::defaultChannels() : channels;
    return p;
}

// ---------------------------------------------------------------- sheet
void Project::setSheetSize(QSize size)
{
    if (size == m_size || size.isEmpty())
        return;
    m_size = size;
    emit sheetChanged();
}

void Project::setPadding(int px)
{
    px = qMax(0, px);
    if (px == m_padding)
        return;
    m_padding = px;
    emit sheetChanged();
}

void Project::setGridStep(int px)
{
    px = qMax(1, px);
    if (px == m_gridStep)
        return;
    m_gridStep = px;
    emit sheetChanged();
}

// ---------------------------------------------------------------- channels
void Project::setChannels(const QVector<Channel> &channels)
{
    m_channels = channels;
    emit channelsChanged();
}

const Channel *Project::channel(const QString &id) const
{
    for (const Channel &c : m_channels)
        if (c.id == id)
            return &c;
    return nullptr;
}

// ---------------------------------------------------------------- zones
int Project::zoneIndex(const QString &id) const
{
    for (int i = 0; i < m_zones.size(); ++i)
        if (m_zones[i].id == id)
            return i;
    return -1;
}

const Zone *Project::zone(const QString &id) const
{
    const int i = zoneIndex(id);
    return i >= 0 ? &m_zones[i] : nullptr;
}

void Project::insertZone(int index, const Zone &zone)
{
    index = qBound(0, index, int(m_zones.size()));
    m_zones.insert(index, zone);
    emit zoneAdded(zone.id);
}

void Project::removeZone(const QString &id)
{
    const int i = zoneIndex(id);
    if (i < 0)
        return;
    m_zones.removeAt(i);
    emit zoneRemoved(id);
}

void Project::setZone(const Zone &zone)
{
    const int i = zoneIndex(zone.id);
    if (i < 0 || m_zones[i] == zone)
        return;
    m_zones[i] = zone;
    emit zoneChanged(zone.id);
}

// ---------------------------------------------------------------- materials
const Material *Project::material(const QString &id) const
{
    auto it = m_materials.constFind(id);
    return it == m_materials.cend() ? nullptr : &it.value();
}

void Project::setMaterial(const Material &material)
{
    m_materials.insert(material.id, material);
    emit materialsChanged();
}

void Project::removeMaterial(const QString &id)
{
    if (m_materials.remove(id))
        emit materialsChanged();
}

// ---------------------------------------------------------------- textures
const TextureEntry *Project::texture(const QString &id) const
{
    auto it = m_textures.constFind(id);
    return it == m_textures.cend() ? nullptr : &it.value();
}

QString Project::addTextureFromFile(const QString &path, QString *error)
{
    const QString abs = QFileInfo(path).canonicalFilePath();
    if (abs.isEmpty()) {
        if (error)
            *error = tr("File not found: %1").arg(path);
        return {};
    }
    // Linked textures are keyed by path, so the same file is shared and
    // reloads keep their identity.
    const QString id = shortHash(abs.toUtf8());
    if (m_textures.contains(id))
        return id;

    QImage img = readImage(abs, error);
    if (img.isNull())
        return {};

    TextureEntry e;
    e.id = id;
    e.source = TextureSource::Linked;
    e.path = abs;
    e.origin = QStringLiteral("file");
    e.image = img;
    m_textures.insert(id, e);
    emit textureChanged(id);
    return id;
}

QString Project::addTextureFromImage(const QImage &image, const QString &origin)
{
    if (image.isNull())
        return {};
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    image.save(&buf, "PNG");
    // Embedded textures are keyed by content, so pasting twice stores once.
    const QString id = shortHash(png);
    if (m_textures.contains(id))
        return id;

    TextureEntry e;
    e.id = id;
    e.source = TextureSource::Embedded;
    e.origin = origin;
    e.image = image;
    m_textures.insert(id, e);
    emit textureChanged(id);
    return id;
}

void Project::insertTexture(const TextureEntry &entry)
{
    m_textures.insert(entry.id, entry);
    emit textureChanged(entry.id);
}

bool Project::reloadTexture(const QString &id)
{
    auto it = m_textures.find(id);
    if (it == m_textures.end() || it->source != TextureSource::Linked)
        return false;
    QImage img = readImage(it->path, nullptr);
    if (img.isNull()) {
        // Keep showing the last good pixels; mark as missing.
        if (!it->missing) {
            it->missing = true;
            emit textureChanged(id);
        }
        return false;
    }
    it->image = img;
    it->missing = false;
    emit textureChanged(id);
    return true;
}

void Project::embedTexture(const QString &id)
{
    auto it = m_textures.find(id);
    if (it == m_textures.end() || it->source == TextureSource::Embedded)
        return;
    it->source = TextureSource::Embedded;
    it->origin = QStringLiteral("embedded:") + it->path;
    it->path.clear();
    it->missing = false;
    m_undo.resetClean(); // not undoable, but the file needs saving
    emit textureChanged(id);
}

void Project::embedAllTextures()
{
    const auto ids = m_textures.keys();
    for (const QString &id : ids)
        embedTexture(id);
}

QStringList Project::unusedTextureIds() const
{
    QSet<QString> used;
    for (const Material &m : m_materials)
        for (const MapRef &r : m.maps) {
            used.insert(r.textureId);
            for (const ComponentRef &c : r.components)
                used.insert(c.textureId);
        }
    QStringList out;
    for (auto it = m_textures.cbegin(); it != m_textures.cend(); ++it)
        if (!used.contains(it.key()))
            out << it.key();
    return out;
}

// ---------------------------------------------------------------- misc
void Project::setFilePath(const QString &path)
{
    if (path == m_filePath)
        return;
    m_filePath = path;
    emit filePathChanged();
}

QString Project::newId(const QString &prefix) const
{
    for (;;) {
        const QString id = prefix
            + QUuid::createUuid().toString(QUuid::Id128).left(8);
        if (zoneIndex(id) < 0 && !m_materials.contains(id))
            return id;
    }
}
