#include "gui/ExportPanel.h"
#include "core/Commands.h"
#include "core/Exporter.h"
#include "core/Project.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

ExportPanel::ExportPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("exportPanel"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 4, 8, 8);
    root->setSpacing(8);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("exportCard"));
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(12, 8, 12, 10);
    cardLay->setSpacing(6);

    // header: click to collapse
    m_header = new QToolButton(card);
    m_header->setObjectName(QStringLiteral("sectionHeader"));
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setToolTip(tr("Show / hide export settings"));
    m_summary = new QLabel(card);
    m_summary->setObjectName(QStringLiteral("dim"));
    m_summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *headRow = new QHBoxLayout;
    headRow->addWidget(m_header);
    headRow->addWidget(m_summary, 1);
    cardLay->addLayout(headRow);

    // body
    m_body = new QWidget(card);
    auto *form = new QFormLayout(m_body);
    form->setContentsMargins(0, 4, 0, 0);

    m_dir = new QLineEdit(m_body);
    m_dir->setPlaceholderText(tr("Next to the .trimsheet file"));
    auto *browse = new QToolButton(m_body);
    browse->setText(QStringLiteral("…"));
    browse->setToolTip(tr("Choose export folder"));
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browse);

    m_name = new QLineEdit(m_body);
    m_name->setPlaceholderText(QStringLiteral("trimsheet"));

    m_files = new QVBoxLayout;
    m_files->setSpacing(3);
    m_zones = new QCheckBox(m_body);
    m_zones->setToolTip(tr("Pixel rects and UVs of every zone, for tools and engine scripts"));

    form->addRow(tr("Folder"), dirRow);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Files"), m_files);
    cardLay->addWidget(m_body);
    root->addWidget(card);

    m_button = new QPushButton(tr("Export"), this);
    m_button->setObjectName(QStringLiteral("primary"));
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setToolTip(tr("Write all checked files (Ctrl+E)"));
    root->addWidget(m_button);

    connect(m_header, &QToolButton::clicked, this, [this] { setCollapsed(!m_collapsed); });
    connect(m_button, &QPushButton::clicked, this, &ExportPanel::exportRequested);
    connect(browse, &QToolButton::clicked, this, [this] {
        QString start = m_dir->text();
        if (start.isEmpty() && m_project && !m_project->filePath().isEmpty())
            start = QFileInfo(m_project->filePath()).absolutePath();
        const QString d = QFileDialog::getExistingDirectory(this, tr("Export Folder"), start);
        if (!d.isEmpty()) {
            m_dir->setText(d);
            store();
        }
    });
    connect(m_dir, &QLineEdit::editingFinished, this, &ExportPanel::store);
    connect(m_name, &QLineEdit::editingFinished, this, &ExportPanel::store);
    connect(m_name, &QLineEdit::textChanged, this, &ExportPanel::rebuildFiles);
    connect(m_zones, &QCheckBox::toggled, this, &ExportPanel::store);

    setCollapsed(QSettings().value(QStringLiteral("ui/exportCollapsed"), false).toBool());
}

void ExportPanel::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    m_body->setVisible(!collapsed);
    m_header->setText((collapsed ? QStringLiteral("▸  ") : QStringLiteral("▾  ")) + tr("Export"));
    QSettings().setValue(QStringLiteral("ui/exportCollapsed"), collapsed);
    updateSummary();
}

void ExportPanel::setProject(Project *project)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);
    m_project = project;
    if (m_project)
        connect(m_project, &Project::channelsChanged, this, &ExportPanel::rebuildFiles);
    load();
}

void ExportPanel::load()
{
    m_updating = true;
    const ExportSettings es = m_project ? m_project->exportSettings() : ExportSettings{};
    m_dir->setText(es.directory);
    m_name->setText(es.baseName);
    m_zones->setChecked(es.writeZoneMetadata);
    m_updating = false;
    rebuildFiles();
}

void ExportPanel::store()
{
    if (m_updating || !m_project)
        return;
    ExportSettings es = m_project->exportSettings();
    const QString dir = m_dir->text().trimmed();
    const QString name = m_name->text().trimmed().isEmpty() ? QStringLiteral("trimsheet")
                                                            : m_name->text().trimmed();
    if (es.directory == dir && es.baseName == name && es.writeZoneMetadata == m_zones->isChecked())
        return;
    es.directory = dir;
    es.baseName = name;
    es.writeZoneMetadata = m_zones->isChecked();
    m_project->setExportSettings(es);
    m_project->undoStack()->resetClean(); // saved in the file
    updateSummary();
}

void ExportPanel::rebuildFiles()
{
    // one checkbox per output file
    while (QLayoutItem *it = m_files->takeAt(0)) {
        if (it->widget() != m_zones)
            delete it->widget();
        delete it;
    }
    if (!m_project) {
        updateSummary();
        return;
    }
    const QString base = m_name->text().trimmed().isEmpty() ? QStringLiteral("trimsheet")
                                                            : m_name->text().trimmed();
    const QVector<Channel> channels = m_project->channels();
    for (int i = 0; i < channels.size(); ++i) {
        const Channel &c = channels[i];
        auto *cb = new QCheckBox(Exporter::fileName(c, base), m_body);
        cb->setChecked(c.exportEnabled);
        cb->setToolTip(c.name);
        const QString id = c.id;
        connect(cb, &QCheckBox::toggled, this, [this, id](bool on) {
            if (!m_project)
                return;
            QVector<Channel> after = m_project->channels();
            for (Channel &ch : after)
                if (ch.id == id)
                    ch.exportEnabled = on;
            m_project->undoStack()->push(new SetChannelsCommand(
                m_project, m_project->channels(), after,
                on ? tr("Enable Export") : tr("Disable Export")));
        });
        m_files->addWidget(cb);
    }
    m_zones->setText(base + QStringLiteral("_zones.json"));
    m_files->addWidget(m_zones);
    updateSummary();
}

void ExportPanel::updateSummary()
{
    if (!m_project || !m_collapsed) {
        m_summary->clear();
        return;
    }
    int n = m_project->exportSettings().writeZoneMetadata ? 1 : 0;
    for (const Channel &c : m_project->channels())
        n += c.exportEnabled;
    const QString dir = m_project->exportSettings().directory;
    m_summary->setText((n == 1 ? tr("1 file → %1") : tr("%1 files → %2").arg(n))
                           .arg(dir.isEmpty() ? tr("project folder") : QDir(dir).dirName()));
}
