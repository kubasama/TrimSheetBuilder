#include "io/TextureWatcher.h"
#include "core/Project.h"

#include <QFileInfo>

#include <utility>

TextureWatcher::TextureWatcher(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250); // let the writer finish
    connect(&m_debounce, &QTimer::timeout, this, &TextureWatcher::flush);
    connect(&m_fs, &QFileSystemWatcher::fileChanged, this, &TextureWatcher::schedule);
    connect(&m_fs, &QFileSystemWatcher::directoryChanged, this, &TextureWatcher::schedule);
}

void TextureWatcher::setProject(Project *project)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);
    m_project = project;
    m_stamps.clear();
    if (!m_fs.files().isEmpty())
        m_fs.removePaths(m_fs.files());
    if (!m_fs.directories().isEmpty())
        m_fs.removePaths(m_fs.directories());
    if (m_project)
        connect(m_project, &Project::textureChanged, this, &TextureWatcher::sync);
    sync();
}

TextureWatcher::Stamp TextureWatcher::stampOf(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return {};
    return {fi.lastModified(), fi.size()};
}

void TextureWatcher::sync()
{
    if (!m_project)
        return;
    QSet<QString> wantFiles, wantDirs;
    for (const TextureEntry &t : m_project->textures()) {
        if (t.source != TextureSource::Linked)
            continue;
        if (!m_stamps.contains(t.id))
            m_stamps.insert(t.id, stampOf(t.path));
        wantDirs.insert(QFileInfo(t.path).absolutePath());
        if (QFileInfo::exists(t.path))
            wantFiles.insert(t.path);
    }
    const QStringList files = m_fs.files(), dirs = m_fs.directories();
    for (const QString &f : files)
        if (!wantFiles.contains(f))
            m_fs.removePath(f);
    for (const QString &d : dirs)
        if (!wantDirs.contains(d))
            m_fs.removePath(d);
    for (const QString &f : std::as_const(wantFiles))
        if (!files.contains(f))
            m_fs.addPath(f);
    for (const QString &d : std::as_const(wantDirs))
        if (!dirs.contains(d))
            m_fs.addPath(d);
}

void TextureWatcher::schedule(const QString &path)
{
    m_dirty.insert(path);
    m_debounce.start();
}

void TextureWatcher::flush()
{
    if (!m_project)
        return;
    const QSet<QString> dirty = std::exchange(m_dirty, {});

    QStringList toReload;
    for (const TextureEntry &t : m_project->textures()) {
        if (t.source != TextureSource::Linked)
            continue;
        const QString dir = QFileInfo(t.path).absolutePath();
        if (!dirty.contains(t.path) && !dirty.contains(dir))
            continue;
        const Stamp now = stampOf(t.path);
        if (now == m_stamps.value(t.id) && !(t.missing && now.size >= 0))
            continue;
        m_stamps.insert(t.id, now);
        toReload << t.id;
    }
    for (const QString &id : toReload)
        emit textureReloaded(id, m_project->reloadTexture(id));
    sync(); // re-arm file watches dropped by atomic saves
}
