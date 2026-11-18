#pragma once

#include "gui/Dialogs.h"
#include "gui/SheetView.h"

#include <QMainWindow>
#include <QPointer>

class Project;
class InspectorPanel;
class ExportPanel;
class TextureWatcher;
class QActionGroup;
class QMimeData;
class QSplitter;
class QUndoGroup;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildActions();
    void setProject(Project *project);
    void setMode(EditMode mode);
    void modeChanged(EditMode mode);
    void updateTitle();
    bool maybeSave();
    void loadSettings();
    void saveSettings();

    void newSheet();
    void open();
    bool save();
    bool saveAs();
    void exportSheet();
    void deleteZone();
    void pasteTexture();
    void assignFromMime(const QString &zoneId, const QMimeData *mime);
    void editChannels();
    void showPreferences();
    void applyPreferences();
    void applyShortcuts();
    void saveShortcuts(const QVector<ShortcutEntry> &entries);

    QPointer<Project> m_project;
    SheetView *m_view;
    InspectorPanel *m_inspector;
    ExportPanel *m_export;
    QSplitter *m_splitter;
    TextureWatcher *m_watcher;
    QUndoGroup *m_undoGroup;
    QAction *m_layoutAct;
    QAction *m_fillAct;
    QAction *m_snapAct;
    QAction *m_gridAct;
    QVector<ShortcutEntry> m_shortcuts;
};
