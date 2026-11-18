#pragma once

#include "core/Model.h"

#include <QObject>
#include <QUndoStack>

// The document. All mutations that should be undoable go through commands
// (core/Commands.h), which call the set*/add*/remove* methods below.
class Project : public QObject
{
    Q_OBJECT
public:
    explicit Project(QObject *parent = nullptr);

    // channels empty = Base Color + Normal + ORM
    static Project *createDefault(QSize size, QObject *parent = nullptr,
                                  const QVector<Channel> &channels = {});

    // --- sheet ---
    QSize sheetSize() const { return m_size; }
    void setSheetSize(QSize size);
    int padding() const { return m_padding; }
    void setPadding(int px);
    int gridStep() const { return m_gridStep; }
    void setGridStep(int px);

    // --- channels ---
    const QVector<Channel> &channels() const { return m_channels; }
    void setChannels(const QVector<Channel> &channels);
    const Channel *channel(const QString &id) const;

    // --- zones ---
    const QVector<Zone> &zones() const { return m_zones; }
    const Zone *zone(const QString &id) const;
    void insertZone(int index, const Zone &zone);
    void removeZone(const QString &id);
    void setZone(const Zone &zone);
    int zoneIndex(const QString &id) const;

    // --- materials ---
    const QHash<QString, Material> &materials() const { return m_materials; }
    const Material *material(const QString &id) const;
    void setMaterial(const Material &material);   // insert or replace
    void removeMaterial(const QString &id);

    // --- textures (not undoable; content-addressed and add-only) ---
    const QHash<QString, TextureEntry> &textures() const { return m_textures; }
    const TextureEntry *texture(const QString &id) const;
    QString addTextureFromFile(const QString &path, QString *error = nullptr);
    QString addTextureFromImage(const QImage &image, const QString &origin);
    void insertTexture(const TextureEntry &entry); // used by the loader
    bool reloadTexture(const QString &id);
    void embedTexture(const QString &id);
    void embedAllTextures();
    QStringList unusedTextureIds() const;

    // --- export ---
    const ExportSettings &exportSettings() const { return m_export; }
    void setExportSettings(const ExportSettings &s) { m_export = s; }

    // --- file ---
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path);
    QUndoStack *undoStack() { return &m_undo; }

    QString newId(const QString &prefix) const;

signals:
    void sheetChanged();
    void channelsChanged();
    void zoneAdded(const QString &id);
    void zoneRemoved(const QString &id);
    void zoneChanged(const QString &id);
    void materialsChanged();
    void textureChanged(const QString &id);   // added, reloaded or embedded
    void filePathChanged();
    void contentChanged();                    // any of the above that affects pixels

private:
    QSize m_size{2048, 2048};
    int m_padding = 8;
    int m_gridStep = 16;
    QVector<Channel> m_channels;
    QVector<Zone> m_zones;
    QHash<QString, Material> m_materials;
    QHash<QString, TextureEntry> m_textures;
    ExportSettings m_export;
    QString m_filePath;
    QUndoStack m_undo;
};
