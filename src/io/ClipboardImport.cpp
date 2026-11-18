#include "io/ClipboardImport.h"
#include "core/Project.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QUrl>

namespace {

QString localPathFrom(const QMimeData *mime)
{
    if (mime->hasUrls()) {
        for (const QUrl &u : mime->urls())
            if (u.isLocalFile() && QFileInfo(u.toLocalFile()).isFile())
                return u.toLocalFile();
    }
    if (mime->hasText()) {
        QString t = mime->text().trimmed();
        if (t.contains(u'\n'))
            t = t.section(u'\n', 0, 0).trimmed();
        if (t.startsWith(u"file://"))
            t = QUrl(t).toLocalFile();
        if (t.startsWith(u"~/"))
            t.replace(0, 1, qEnvironmentVariable("HOME"));
        if (!t.isEmpty() && QFileInfo(t).isFile())
            return t;
    }
    return {};
}

} // namespace

bool ClipboardImport::canImport(const QMimeData *mime)
{
    return mime && (mime->hasImage() || !localPathFrom(mime).isEmpty());
}

QString ClipboardImport::importMime(Project *project, const QMimeData *mime, QString *error)
{
    if (!mime) {
        if (error)
            *error = QObject::tr("Clipboard is empty.");
        return {};
    }
    const QString path = localPathFrom(mime);
    if (!path.isEmpty())
        return project->addTextureFromFile(path, error);
    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull())
            return project->addTextureFromImage(img, QStringLiteral("clipboard"));
    }
    if (error)
        *error = QObject::tr("Clipboard has no image or image file path.");
    return {};
}

QString ClipboardImport::importClipboard(Project *project, QString *error)
{
    return importMime(project, QGuiApplication::clipboard()->mimeData(), error);
}
