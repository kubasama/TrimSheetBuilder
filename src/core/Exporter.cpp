#include "core/Exporter.h"
#include "core/Compositor.h"
#include "core/Project.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

QString Exporter::fileName(const Channel &ch, const QString &baseName, const QString &ext)
{
    return baseName + ch.fileSuffix() + u'.' + (ext.isEmpty() ? QStringLiteral("png") : ext);
}

QStringList Exporter::exportSheet(const Project &project, const QString &directory,
                                  const QString &baseName, QString *error)
{
    QStringList written;
    QDir dir(directory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QObject::tr("Cannot create %1").arg(directory);
        return written;
    }

    const ExportSettings &es = project.exportSettings();
    const QString ext = es.format.isEmpty() ? QStringLiteral("png") : es.format;

    for (const Channel &ch : project.channels()) {
        if (!ch.exportEnabled)
            continue;
        const QImage img = Compositor::renderChannel(project, ch.id, true)
                               .convertToFormat(ch.isGrayscale() ? QImage::Format_Grayscale8
                                                : ch.alpha        ? QImage::Format_RGBA8888
                                                                  : QImage::Format_RGB888);
        if (img.isNull()) {
            if (error)
                *error = QObject::tr("Not enough memory to render %1").arg(ch.name);
            return written;
        }
        const QString file = dir.filePath(fileName(ch, baseName, ext));
        if (!img.save(file)) {
            if (error)
                *error = QObject::tr("Failed to write %1").arg(file);
            return written;
        }
        written << file;
    }

    if (es.writeZoneMetadata) {
        const double W = project.sheetSize().width(), H = project.sheetSize().height();
        QJsonArray zones;
        for (const Zone &z : project.zones()) {
            const QRect r = z.rect;
            const Material *m = project.material(z.materialId);
            zones.append(QJsonObject{
                {"id", z.id},
                {"name", z.name},
                {"material", m ? m->name : QString()},
                {"rect", QJsonArray{r.x(), r.y(), r.width(), r.height()}},
                // top-left origin (DirectX-style); flip V for bottom-left conventions
                {"uv", QJsonArray{r.x() / W, r.y() / H, (r.x() + r.width()) / W,
                                  (r.y() + r.height()) / H}},
            });
        }
        const QJsonObject root{
            {"sheet", QJsonObject{{"width", int(W)}, {"height", int(H)}}},
            {"padding", project.padding()},
            {"zones", zones},
        };
        const QString file = dir.filePath(baseName + QStringLiteral("_zones.json"));
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly)
            || f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !f.commit()) {
            if (error)
                *error = QObject::tr("Failed to write %1").arg(file);
            return written;
        }
        written << file;
    }
    return written;
}
