#pragma once
// Clipboard and drag & drop import.
//   file URL / path text  -> linked texture (watched for changes)
//   raw image data        -> embedded texture

#include <QString>

class Project;
class QMimeData;

namespace ClipboardImport {

bool canImport(const QMimeData *mime);
QString importMime(Project *project, const QMimeData *mime, QString *error);
QString importClipboard(Project *project, QString *error);

} // namespace ClipboardImport
