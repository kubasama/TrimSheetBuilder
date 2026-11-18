#pragma once
// Reloads linked textures when they change on disk.
//
// Watches each file *and* its parent folder: most editors save by writing a
// temp file and renaming it over the original, which silently drops an
// inotify watch on the file itself.

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

class Project;

class TextureWatcher : public QObject
{
    Q_OBJECT
public:
    explicit TextureWatcher(QObject *parent = nullptr);
    void setProject(Project *project);

signals:
    void textureReloaded(const QString &textureId, bool ok);

private:
    struct Stamp {
        QDateTime mtime;
        qint64 size = -1;
        bool operator==(const Stamp &o) const { return mtime == o.mtime && size == o.size; }
    };
    static Stamp stampOf(const QString &path);
    void sync();
    void schedule(const QString &path);
    void flush();

    QPointer<Project> m_project;
    QFileSystemWatcher m_fs;
    QHash<QString, Stamp> m_stamps;        // textureId -> last seen state
    QSet<QString> m_dirty;                 // paths (files or dirs) touched
    QTimer m_debounce;
};
