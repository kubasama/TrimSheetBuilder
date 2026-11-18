#include "core/Serializer.h"
#include "core/Project.h"
#include "core/ChannelPresets.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <zip.h>

#include <list>

namespace {

const char *kMime = "application/x-trimsheet";

// ---------------------------------------------------------------- JSON helpers
QJsonArray pointToJson(QPointF p) { return {p.x(), p.y()}; }
QPointF pointFromJson(const QJsonValue &v, QPointF fallback)
{
    const QJsonArray a = v.toArray();
    return a.size() == 2 ? QPointF(a[0].toDouble(), a[1].toDouble()) : fallback;
}

QByteArray toPng(const QImage &img)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

QString texturePathInZip(const QString &id) { return QStringLiteral("textures/%1.png").arg(id); }

// ---------------------------------------------------------------- to JSON
QJsonObject projectToJson(const Project &p, const QDir &baseDir, const QSet<QString> &usedTextures)
{
    QJsonObject root;
    root["format"] = "trimsheet";
    root["version"] = Serializer::kFormatVersion;

    root["sheet"] = QJsonObject{
        {"width", p.sheetSize().width()},
        {"height", p.sheetSize().height()},
        {"padding", p.padding()},
        {"grid", p.gridStep()},
    };

    root["channels"] = ChannelPresets::toJson(p.channels());

    QJsonObject textures;
    for (const TextureEntry &t : p.textures()) {
        if (!usedTextures.contains(t.id))
            continue;
        QJsonObject o;
        if (t.source == TextureSource::Linked) {
            o["source"] = "linked";
            o["path"] = baseDir.relativeFilePath(t.path);
            o["cache"] = texturePathInZip(t.id);
        } else {
            o["source"] = "embedded";
            o["file"] = texturePathInZip(t.id);
        }
        o["origin"] = t.origin;
        textures[t.id] = o;
    }
    root["textures"] = textures;

    QJsonObject materials;
    for (const Material &m : p.materials()) {
        QJsonObject maps;
        for (auto it = m.maps.cbegin(); it != m.maps.cend(); ++it) {
            QJsonObject r{{"texture", it->textureId}};
            const Channel *ch = p.channel(it.key());
            if (ch && ch->isNormal())
                r["convention"] = toString(it->convention);
            QJsonObject comps;
            for (int c = 0; c < 4; ++c) {
                const ComponentRef &cr = it->components[c];
                if (!cr.textureId.isEmpty())
                    comps[componentName(c)] = QJsonObject{{"texture", cr.textureId},
                                                          {"source", componentName(cr.source)}};
            }
            if (!comps.isEmpty())
                r["components"] = comps;
            maps[it.key()] = r;
        }
        materials[m.id] = QJsonObject{{"name", m.name}, {"maps", maps}};
    }
    root["materials"] = materials;

    QJsonArray zones;
    for (const Zone &z : p.zones()) {
        const FillSettings &f = z.fill;
        zones.append(QJsonObject{
            {"id", z.id},
            {"name", z.name},
            {"rect", QJsonArray{z.rect.x(), z.rect.y(), z.rect.width(), z.rect.height()}},
            {"material", z.materialId},
            {"locked", z.locked},
            {"fill", QJsonObject{
                {"mode", toString(f.mode)},
                {"rotation", f.rotation},
                {"flipX", f.flipX},
                {"flipY", f.flipY},
                {"scale", pointToJson(f.scale)},
                {"offset", pointToJson(f.offset)},
            }},
        });
    }
    root["zones"] = zones;

    const ExportSettings &e = p.exportSettings();
    root["export"] = QJsonObject{
        {"baseName", e.baseName},
        {"directory", e.directory.isEmpty() ? QString() : baseDir.relativeFilePath(e.directory)},
        {"format", e.format},
        {"zoneMetadata", e.writeZoneMetadata},
    };
    return root;
}

// ---------------------------------------------------------------- zip helpers
struct ZipReader {
    zip_t *za = nullptr;
    ~ZipReader() { if (za) zip_discard(za); }

    bool read(const QString &name, QByteArray *out) const
    {
        const QByteArray n = name.toUtf8();
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat(za, n.constData(), 0, &st) != 0)
            return false;
        zip_file_t *f = zip_fopen(za, n.constData(), 0);
        if (!f)
            return false;
        out->resize(qsizetype(st.size));
        const zip_int64_t got = zip_fread(f, out->data(), st.size);
        zip_fclose(f);
        return got == zip_int64_t(st.size);
    }
};

} // namespace

// ============================================================== save
bool Serializer::save(const Project &p, const QString &path, QString *error)
{
    const QDir baseDir = QFileInfo(path).absoluteDir();

    QSet<QString> used;
    for (const Material &m : p.materials())
        for (const MapRef &r : m.maps) {
            used.insert(r.textureId);
            for (const ComponentRef &c : r.components)
                used.insert(c.textureId);
        }

    const QByteArray json =
        QJsonDocument(projectToJson(p, baseDir, used)).toJson(QJsonDocument::Indented);

    // Write next to the target, then swap, so a failed save never eats the file.
    const QString tmpPath = path + QStringLiteral(".part");
    QFile::remove(tmpPath);

    int zerr = 0;
    zip_t *za = zip_open(QFile::encodeName(tmpPath).constData(), ZIP_CREATE | ZIP_TRUNCATE, &zerr);
    if (!za) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, zerr);
        if (error)
            *error = QString::fromUtf8(zip_error_strerror(&ze));
        zip_error_fini(&ze);
        return false;
    }

    std::list<QByteArray> keepAlive; // libzip reads buffers at zip_close()
    auto add = [&](const QString &name, QByteArray data, bool store) -> bool {
        keepAlive.push_back(std::move(data));
        const QByteArray &d = keepAlive.back();
        zip_source_t *src = zip_source_buffer(za, d.constData(), zip_uint64_t(d.size()), 0);
        if (!src)
            return false;
        const zip_int64_t idx =
            zip_file_add(za, name.toUtf8().constData(), src, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
        if (idx < 0) {
            zip_source_free(src);
            return false;
        }
        if (store)
            zip_set_file_compression(za, zip_uint64_t(idx), ZIP_CM_STORE, 0);
        return true;
    };

    bool ok = add(QStringLiteral("mimetype"), QByteArray(kMime), true)
           && add(QStringLiteral("project.json"), json, false);

    for (const TextureEntry &t : p.textures()) {
        if (!ok)
            break;
        if (!used.contains(t.id))
            continue;
        // PNG is already deflated; storing avoids burning CPU for nothing.
        ok = add(texturePathInZip(t.id), toPng(t.image), true);
    }

    if (!ok) {
        if (error)
            *error = QString::fromUtf8(zip_strerror(za));
        zip_discard(za);
        QFile::remove(tmpPath);
        return false;
    }
    if (zip_close(za) != 0) {
        if (error)
            *error = QString::fromUtf8(zip_strerror(za));
        zip_discard(za);
        QFile::remove(tmpPath);
        return false;
    }

    QFile::remove(path);
    if (!QFile::rename(tmpPath, path)) {
        if (error)
            *error = QObject::tr("Could not replace %1").arg(path);
        return false;
    }
    return true;
}

// ============================================================== load
Project *Serializer::load(const QString &path, QString *error, QObject *parent)
{
    ZipReader zr;
    int zerr = 0;
    zr.za = zip_open(QFile::encodeName(path).constData(), ZIP_RDONLY, &zerr);
    if (!zr.za) {
        if (error)
            *error = QObject::tr("Not a trim sheet file (cannot open archive).");
        return nullptr;
    }

    QByteArray jsonData;
    if (!zr.read(QStringLiteral("project.json"), &jsonData)) {
        if (error)
            *error = QObject::tr("project.json is missing.");
        return nullptr;
    }
    QJsonParseError perr;
    const QJsonObject root = QJsonDocument::fromJson(jsonData, &perr).object();
    if (perr.error != QJsonParseError::NoError || root["format"].toString() != u"trimsheet") {
        if (error)
            *error = QObject::tr("project.json is invalid: %1").arg(perr.errorString());
        return nullptr;
    }
    if (root["version"].toInt() > kFormatVersion) {
        if (error)
            *error = QObject::tr("File was made by a newer version (format %1).")
                         .arg(root["version"].toInt());
        return nullptr;
    }

    const QDir baseDir = QFileInfo(path).absoluteDir();
    auto *p = new Project(parent);

    const QJsonObject sheet = root["sheet"].toObject();
    p->setSheetSize(QSize(sheet["width"].toInt(2048), sheet["height"].toInt(2048)));
    p->setPadding(sheet["padding"].toInt(8));
    p->setGridStep(sheet["grid"].toInt(16));

    p->setChannels(ChannelPresets::fromJson(root["channels"].toArray()));

    const QJsonObject textures = root["textures"].toObject();
    for (auto it = textures.constBegin(); it != textures.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        TextureEntry t;
        t.id = it.key();
        t.origin = o["origin"].toString();
        QByteArray cached;
        if (o["source"].toString() == u"linked") {
            t.source = TextureSource::Linked;
            t.path = QDir::cleanPath(baseDir.absoluteFilePath(o["path"].toString()));
            QImageReader r(t.path);
            r.setAutoTransform(true);
            t.image = r.read();
            if (t.image.isNull()) {
                t.missing = true;
                if (zr.read(o["cache"].toString(), &cached))
                    t.image.loadFromData(cached, "PNG");
            }
        } else {
            t.source = TextureSource::Embedded;
            if (zr.read(o["file"].toString(), &cached))
                t.image.loadFromData(cached, "PNG");
        }
        p->insertTexture(t);
    }

    const QJsonObject materials = root["materials"].toObject();
    for (auto it = materials.constBegin(); it != materials.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Material m;
        m.id = it.key();
        m.name = o["name"].toString(m.id);
        const QJsonObject maps = o["maps"].toObject();
        for (auto mit = maps.constBegin(); mit != maps.constEnd(); ++mit) {
            const QJsonObject r = mit.value().toObject();
            MapRef ref;
            ref.textureId = r["texture"].toString();
            ref.convention = conventionFromString(r["convention"].toString());
            const QJsonObject comps = r["components"].toObject();
            for (int c = 0; c < 4; ++c) {
                const QJsonObject co = comps[componentName(c)].toObject();
                ref.components[c].textureId = co["texture"].toString();
                ref.components[c].source = componentFromName(co["source"].toString());
            }
            m.maps.insert(mit.key(), ref);
        }
        p->setMaterial(m);
    }

    for (const QJsonValue &v : root["zones"].toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonArray r = o["rect"].toArray();
        const QJsonObject f = o["fill"].toObject();
        Zone z;
        z.id = o["id"].toString();
        z.name = o["name"].toString();
        if (r.size() == 4)
            z.rect = QRect(r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt());
        z.materialId = o["material"].toString();
        z.locked = o["locked"].toBool(false);
        z.fill.mode = fillModeFromString(f["mode"].toString());
        z.fill.rotation = f["rotation"].toDouble();
        z.fill.flipX = f["flipX"].toBool();
        z.fill.flipY = f["flipY"].toBool();
        z.fill.scale = pointFromJson(f["scale"], {1, 1});
        z.fill.offset = pointFromJson(f["offset"], {0, 0});
        if (!z.id.isEmpty())
            p->insertZone(int(p->zones().size()), z);
    }

    const QJsonObject e = root["export"].toObject();
    ExportSettings es;
    es.baseName = e["baseName"].toString(es.baseName);
    const QString dir = e["directory"].toString();
    es.directory = dir.isEmpty() ? QString() : QDir::cleanPath(baseDir.absoluteFilePath(dir));
    es.format = e["format"].toString(es.format);
    es.writeZoneMetadata = e["zoneMetadata"].toBool(true);
    p->setExportSettings(es);

    p->setFilePath(QFileInfo(path).absoluteFilePath());
    p->undoStack()->clear();
    return p;
}
