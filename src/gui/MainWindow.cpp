#include "gui/MainWindow.h"
#include "gui/ChannelEditor.h"
#include "gui/Dialogs.h"
#include "gui/ExportPanel.h"
#include "gui/InspectorPanel.h"
#include "gui/NewSheetDialog.h"
#include "gui/Theme.h"

#include "core/Commands.h"
#include "core/Exporter.h"
#include "core/Project.h"
#include "core/Serializer.h"
#include "io/ClipboardImport.h"
#include "io/TextureWatcher.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QUndoGroup>
#include <QVBoxLayout>

namespace {
QString fileFilter() { return QObject::tr("Trim sheets (*.trimsheet)"); }
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_view(new SheetView(this))
    , m_inspector(new InspectorPanel(this))
    , m_export(new ExportPanel(this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
    , m_watcher(new TextureWatcher(this))
    , m_undoGroup(new QUndoGroup(this))
{
    // canvas | side panel (inspector scrolls, export pinned at the bottom)
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(m_inspector);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *side = new QWidget(this);
    side->setMinimumWidth(340);
    auto *sideLay = new QVBoxLayout(side);
    sideLay->setContentsMargins(0, 0, 0, 0);
    sideLay->setSpacing(0);
    sideLay->addWidget(scroll, 1);
    sideLay->addWidget(m_export);

    m_splitter->addWidget(m_view);
    m_splitter->addWidget(side);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_splitter->setSizes({1000, 380});
    setCentralWidget(m_splitter);

    buildActions();

    connect(m_view, &SheetView::zoneSelected, m_inspector, &InspectorPanel::setZone);
    connect(m_view, &SheetView::modeChanged, this, &MainWindow::modeChanged);
    connect(m_view, &SheetView::textureDropped, this, &MainWindow::assignFromMime);
    connect(m_view, &SheetView::statusMessage, this,
            [this](const QString &t) { statusBar()->showMessage(t); });
    connect(m_view, &SheetView::snapSettingsChanged, this, [this](const SnapSettings &s) {
        m_inspector->setSnapSettings(s);
        m_snapAct->setChecked(s.enabled);
    });
    connect(m_inspector, &InspectorPanel::snapSettingsEdited, m_view, &SheetView::setSnapSettings);
    connect(m_inspector, &InspectorPanel::statusMessage, this,
            [this](const QString &t) { statusBar()->showMessage(t, 5000); });
    connect(m_export, &ExportPanel::exportRequested, this, &MainWindow::exportSheet);
    connect(m_inspector, &InspectorPanel::editChannelsRequested, this, &MainWindow::editChannels);
    connect(m_watcher, &TextureWatcher::textureReloaded, this, [this](const QString &, bool ok) {
        statusBar()->showMessage(ok ? tr("Texture reloaded from disk")
                                    : tr("Linked texture missing, using cached copy"), 3000);
    });

    resize(1440, 900);
    loadSettings();
    {
        const AppPreferences prefs = AppPreferences::load();
        const QSignalBlocker b(m_gridAct);
        m_gridAct->setChecked(prefs.showGrid);
        m_view->setShowGrid(prefs.showGrid);
        m_view->setShowZoneNames(prefs.showZoneNames);
    }
    setProject(Project::createDefault(QSize(2048, 2048), this));
    modeChanged(m_view->mode());
}

// ---------------------------------------------------------------- actions
void MainWindow::buildActions()
{
    auto act = [this](const QString &text, QKeySequence key, auto slot) {
        auto *a = new QAction(text, this);
        if (!key.isEmpty())
            a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        addAction(a); // shortcuts work even with the menu bar hidden
        return a;
    };

    QAction *newAct = act(tr("&New…"), QKeySequence::New, &MainWindow::newSheet);
    QAction *openAct = act(tr("&Open…"), QKeySequence::Open, &MainWindow::open);
    QAction *saveAct = act(tr("&Save"), QKeySequence::Save, [this] { save(); });
    QAction *saveAsAct = act(tr("Save &As…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), [this] { saveAs(); });
    QAction *exportAct = act(tr("&Export"), QKeySequence(Qt::CTRL | Qt::Key_E),
                             &MainWindow::exportSheet);
    QAction *embedAllAct = act(tr("Embed All Textures"), {}, [this] {
        if (m_project)
            m_project->embedAllTextures();
    });
    QAction *quitAct = act(tr("&Quit"), QKeySequence::Quit, &QWidget::close);

    QAction *undoAct = m_undoGroup->createUndoAction(this, tr("&Undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    QAction *redoAct = m_undoGroup->createRedoAction(this, tr("&Redo"));
    redoAct->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    addAction(undoAct);
    addAction(redoAct);
    QAction *pasteAct = act(tr("&Paste Texture"), QKeySequence::Paste, &MainWindow::pasteTexture);
    QAction *deleteAct = act(tr("&Delete Zone"), QKeySequence::Delete, &MainWindow::deleteZone);

    auto *modes = new QActionGroup(this);
    m_layoutAct = act(tr("&Layout Mode"), QKeySequence(Qt::CTRL | Qt::Key_1),
                      [this] { setMode(EditMode::Layout); });
    m_fillAct = act(tr("&Fill Mode"), QKeySequence(Qt::CTRL | Qt::Key_2),
                    [this] { setMode(EditMode::Fill); });
    for (QAction *a : {m_layoutAct, m_fillAct}) {
        a->setCheckable(true);
        modes->addAction(a);
    }
    m_snapAct = act(tr("&Snapping"), QKeySequence(Qt::SHIFT | Qt::Key_Tab), [this](bool on) {
        SnapSettings s = m_view->snapSettings();
        s.enabled = on;
        m_view->setSnapSettings(s);
    });
    m_snapAct->setCheckable(true);
    QAction *fitAct = act(tr("Zoom to &Fit"), QKeySequence(Qt::CTRL | Qt::Key_0),
                          [this] { m_view->zoomToFit(); });
    fitAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_0), QKeySequence(Qt::Key_Home)});
    m_gridAct = new QAction(tr("Show &Grid"), this);
    m_gridAct->setCheckable(true);
    connect(m_gridAct, &QAction::toggled, this, [this](bool on) {
        AppPreferences p = AppPreferences::load();
        p.showGrid = on;
        p.save();
        m_view->setShowGrid(on);
    });

    QAction *channelsAct = act(tr("&Channels…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                               &MainWindow::editChannels);
    QAction *prefsAct = act(tr("&Preferences…"), {}, &MainWindow::showPreferences);
    prefsAct->setMenuRole(QAction::PreferencesRole);
    QAction *aboutAct = act(tr("&About Trim Sheet Builder"), {}, [this] { AboutDialog(this).exec(); });
    aboutAct->setMenuRole(QAction::AboutRole);

    // everything that can get a shortcut (editable in Preferences)
    const QString file_ = tr("File"), edit_ = tr("Edit"), view_ = tr("View");
    auto reg = [this](QAction *a, const QString &id, const QString &cat, const QString &label) {
        m_shortcuts.append({id, cat, label, a->shortcuts(), {}, a});
    };
    reg(newAct, "file.new", file_, tr("New"));
    reg(openAct, "file.open", file_, tr("Open"));
    reg(saveAct, "file.save", file_, tr("Save"));
    reg(saveAsAct, "file.saveAs", file_, tr("Save As"));
    reg(exportAct, "file.export", file_, tr("Export"));
    reg(embedAllAct, "file.embedAll", file_, tr("Embed All Textures"));
    reg(quitAct, "file.quit", file_, tr("Quit"));
    reg(undoAct, "edit.undo", edit_, tr("Undo"));
    reg(redoAct, "edit.redo", edit_, tr("Redo"));
    reg(pasteAct, "edit.paste", edit_, tr("Paste Texture"));
    reg(deleteAct, "edit.delete", edit_, tr("Delete Zone"));
    reg(channelsAct, "edit.channels", edit_, tr("Channels"));
    reg(prefsAct, "edit.preferences", edit_, tr("Preferences"));
    reg(m_layoutAct, "view.layout", view_, tr("Layout Mode"));
    reg(m_fillAct, "view.fill", view_, tr("Fill Mode"));
    reg(m_snapAct, "view.snap", view_, tr("Snapping"));
    reg(m_gridAct, "view.grid", view_, tr("Show Grid"));
    reg(fitAct, "view.fit", view_, tr("Zoom to Fit"));
    addAction(m_gridAct);
    applyShortcuts();

    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addActions({newAct, openAct, saveAct, saveAsAct});
    file->addSeparator();
    file->addActions({exportAct, embedAllAct});
    file->addSeparator();
    file->addAction(quitAct);
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({undoAct, redoAct});
    edit->addSeparator();
    edit->addActions({pasteAct, deleteAct});
    edit->addSeparator();
    edit->addAction(channelsAct);
    edit->addSeparator();
    edit->addAction(prefsAct);
    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addActions({m_layoutAct, m_fillAct});
    view->addSeparator();
    view->addActions({m_snapAct, m_gridAct, fitAct});
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(aboutAct);
}

// ---------------------------------------------------------------- settings
void MainWindow::loadSettings()
{
    QSettings s;
    SnapSettings snap;
    snap.enabled = s.value(QStringLiteral("snap/enabled"), true).toBool();
    snap.grid = s.value(QStringLiteral("snap/grid"), true).toBool();
    snap.edges = s.value(QStringLiteral("snap/edges"), true).toBool();
    snap.allowOverlap = s.value(QStringLiteral("snap/allowOverlap"), false).toBool();
    m_view->setSnapSettings(snap);
    m_inspector->setSnapSettings(snap);
    m_snapAct->setChecked(snap.enabled);
    if (s.contains(QStringLiteral("window/geometry")))
        restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    if (s.contains(QStringLiteral("window/splitter")))
        m_splitter->restoreState(s.value(QStringLiteral("window/splitter")).toByteArray());
}

void MainWindow::saveSettings()
{
    QSettings s;
    const SnapSettings snap = m_view->snapSettings();
    s.setValue(QStringLiteral("snap/enabled"), snap.enabled);
    s.setValue(QStringLiteral("snap/grid"), snap.grid);
    s.setValue(QStringLiteral("snap/edges"), snap.edges);
    s.setValue(QStringLiteral("snap/allowOverlap"), snap.allowOverlap);
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/splitter"), m_splitter->saveState());
}

void MainWindow::applyPreferences()
{
    const AppPreferences p = AppPreferences::load();
    Theme::apply(p.customAccent.isValid() ? p.customAccent : Theme::detectSystemAccent());
    {
        const QSignalBlocker b(m_gridAct);
        m_gridAct->setChecked(p.showGrid);
    }
    m_view->setShowGrid(p.showGrid);
    m_view->setShowZoneNames(p.showZoneNames);
}

void MainWindow::showPreferences()
{
    for (ShortcutEntry &e : m_shortcuts)
        e.current = e.action->shortcuts();
    PreferencesDialog dlg(AppPreferences::load(), m_shortcuts, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    dlg.preferences().save();
    saveShortcuts(dlg.shortcuts());
    applyShortcuts();
    applyPreferences();
}

void MainWindow::saveShortcuts(const QVector<ShortcutEntry> &entries)
{
    QSettings s;
    for (const ShortcutEntry &e : entries) {
        const QString key = QStringLiteral("shortcuts/") + e.id;
        if (e.current == e.defaults)
            s.remove(key);   // default: follow future default changes
        else
            s.setValue(key, QKeySequence::listToString(e.current, QKeySequence::PortableText));
    }
}

void MainWindow::applyShortcuts()
{
    QSettings s;
    for (const ShortcutEntry &e : std::as_const(m_shortcuts)) {
        const QString key = QStringLiteral("shortcuts/") + e.id;
        e.action->setShortcuts(s.contains(key)
                                   ? QKeySequence::listFromString(s.value(key).toString(),
                                                                  QKeySequence::PortableText)
                                   : e.defaults);
    }
}

void MainWindow::editChannels()
{
    if (!m_project)
        return;
    ChannelsDialog dlg(m_project->channels(), true, this);
    if (dlg.exec() != QDialog::Accepted || dlg.channels() == m_project->channels())
        return;
    m_project->undoStack()->push(
        new SetChannelsCommand(m_project, m_project->channels(), dlg.channels(), tr("Edit Channels")));
}

// ---------------------------------------------------------------- project
void MainWindow::setProject(Project *project)
{
    Project *old = m_project;
    if (old) {
        m_undoGroup->removeStack(old->undoStack());
        disconnect(old, nullptr, this, nullptr);
    }
    m_project = project;
    project->setParent(this);
    m_undoGroup->addStack(project->undoStack());
    m_undoGroup->setActiveStack(project->undoStack());

    m_view->setProject(project);
    m_inspector->setProject(project);
    m_export->setProject(project);
    m_watcher->setProject(project);

    connect(project->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
    connect(project, &Project::filePathChanged, this, &MainWindow::updateTitle);
    updateTitle();

    if (old)
        old->deleteLater();
}

void MainWindow::setMode(EditMode mode)
{
    m_view->setMode(mode);
    modeChanged(mode);
}

void MainWindow::modeChanged(EditMode mode)
{
    (mode == EditMode::Layout ? m_layoutAct : m_fillAct)->setChecked(true);
    m_inspector->setMode(mode);
    statusBar()->showMessage(mode == EditMode::Layout
                                 ? tr("Layout: drag on the sheet to draw a zone. Ctrl inverts snapping. Right-drag pans.")
                                 : tr("Fill: drop or paste images onto zones."),
                             4000);
}

void MainWindow::updateTitle()
{
    const QString name = m_project && !m_project->filePath().isEmpty()
        ? QFileInfo(m_project->filePath()).fileName()
        : tr("Untitled");
    setWindowTitle(QStringLiteral("%1[*] — Trim Sheet Builder").arg(name));
    setWindowModified(m_project && !m_project->undoStack()->isClean());
}

// ---------------------------------------------------------------- file
bool MainWindow::maybeSave()
{
    if (!m_project || m_project->undoStack()->isClean())
        return true;
    const QString name = m_project->filePath().isEmpty()
        ? tr("Untitled")
        : QFileInfo(m_project->filePath()).fileName();
    switch (askToSave(this, name)) {
    case SaveAnswer::Yes: return save();
    case SaveAnswer::No: return true;
    case SaveAnswer::Cancel: break;
    }
    return false;
}

void MainWindow::newSheet()
{
    if (!maybeSave())
        return;
    NewSheetDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    Project *p = Project::createDefault(dlg.sheetSize(), this, dlg.channels());
    p->setPadding(dlg.padding());
    for (const LayoutPresets::GeneratedZone &g : dlg.zones()) {
        Zone z;
        z.id = p->newId(QStringLiteral("z_"));
        z.name = g.name;
        z.rect = g.rect;
        p->insertZone(int(p->zones().size()), z);
    }
    p->undoStack()->clear();
    setProject(p);
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Trim Sheet"), QString(),
                                                      fileFilter());
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::openFile(const QString &path)
{
    QString err;
    Project *p = Serializer::load(path, &err, this);
    if (!p) {
        QMessageBox::critical(this, tr("Open Failed"), tr("Could not open %1:\n%2").arg(path, err));
        return false;
    }
    setProject(p);
    int missing = 0;
    for (const TextureEntry &t : p->textures())
        missing += t.missing;
    if (missing)
        statusBar()->showMessage(tr("%n linked texture(s) not found, using cached copies", "", missing));
    return true;
}

bool MainWindow::save()
{
    if (!m_project)
        return false;
    if (m_project->filePath().isEmpty())
        return saveAs();
    QString err;
    if (!Serializer::save(*m_project, m_project->filePath(), &err)) {
        QMessageBox::critical(this, tr("Save Failed"), err);
        return false;
    }
    m_project->undoStack()->setClean();
    statusBar()->showMessage(tr("Saved"), 2000);
    return true;
}

bool MainWindow::saveAs()
{
    if (!m_project)
        return false;
    QString path = QFileDialog::getSaveFileName(this, tr("Save Trim Sheet"),
                                                m_project->filePath(), fileFilter());
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".trimsheet")))
        path += QStringLiteral(".trimsheet");
    m_project->setFilePath(path);
    return save();
}

void MainWindow::exportSheet()
{
    if (!m_project)
        return;
    ExportSettings es = m_project->exportSettings();
    QString dir = es.directory;
    if (dir.isEmpty() && !m_project->filePath().isEmpty())
        dir = QFileInfo(m_project->filePath()).absolutePath();
    if (dir.isEmpty()) {
        dir = QFileDialog::getExistingDirectory(this, tr("Export Folder"));
        if (dir.isEmpty())
            return;
        es.directory = dir;
        m_project->setExportSettings(es);
        m_export->setProject(m_project); // refresh fields
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const QStringList files = Exporter::exportSheet(*m_project, dir, es.baseName, &err);
    QApplication::restoreOverrideCursor();
    if (!err.isEmpty())
        QMessageBox::critical(this, tr("Export Failed"), err);
    else
        statusBar()->showMessage(tr("Exported %n file(s) to %1", "", int(files.size())).arg(dir), 5000);
}

// ---------------------------------------------------------------- edit
void MainWindow::deleteZone()
{
    if (!m_project || m_view->mode() != EditMode::Layout)
        return;
    const QString id = m_view->selectedZone();
    const Zone *z = m_project->zone(id);
    if (!z)
        return;
    if (z->locked) {
        statusBar()->showMessage(tr("Zone is locked. Unlock it to delete it."), 3000);
        return;
    }
    m_project->undoStack()->push(new RemoveZoneCommand(m_project, id));
}

void MainWindow::pasteTexture()
{
    const QString zoneId = m_view->selectedZone();
    if (!m_project || zoneId.isEmpty()) {
        statusBar()->showMessage(tr("Select a zone to paste a texture into"), 3000);
        return;
    }
    QString err;
    const QString tex = ClipboardImport::importClipboard(m_project, &err);
    if (tex.isEmpty()) {
        statusBar()->showMessage(err, 4000);
        return;
    }
    Edit::assignTexture(m_project, zoneId, m_view->previewChannel(), tex);
}

void MainWindow::assignFromMime(const QString &zoneId, const QMimeData *mime)
{
    QString err;
    const QString tex = ClipboardImport::importMime(m_project, mime, &err);
    if (tex.isEmpty()) {
        statusBar()->showMessage(err, 4000);
        return;
    }
    Edit::assignTexture(m_project, zoneId, m_view->previewChannel(), tex);
    if (m_view->mode() != EditMode::Fill)
        setMode(EditMode::Fill);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (maybeSave()) {
        saveSettings();
        e->accept();
    } else {
        e->ignore();
    }
}
