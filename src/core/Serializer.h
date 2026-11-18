#pragma once
// .trimsheet files: a zip archive (like .ora / .kra)
//
//   mimetype            "application/x-trimsheet" (stored, first entry)
//   project.json        sheet, channels, materials, zones, export settings
//   textures/<id>.png   embedded textures, and cached copies of linked ones

#include <QString>

class Project;
class QObject;

namespace Serializer {

inline constexpr int kFormatVersion = 2; // v2: channel kind / suffix / export flag

bool save(const Project &project, const QString &path, QString *error);
Project *load(const QString &path, QString *error, QObject *parent = nullptr);

} // namespace Serializer
