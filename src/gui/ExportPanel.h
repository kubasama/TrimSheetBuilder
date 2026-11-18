#pragma once
// Export settings + the big Export button, pinned under the inspector.
// The settings part collapses; the button always stays visible.

#include <QPointer>
#include <QWidget>

class Project;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

class ExportPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ExportPanel(QWidget *parent = nullptr);
    void setProject(Project *project);

signals:
    void exportRequested();

private:
    void load();
    void store();
    void rebuildFiles();
    void updateSummary();
    void setCollapsed(bool collapsed);

    QPointer<Project> m_project;
    QToolButton *m_header;
    QLabel *m_summary;
    QWidget *m_body;
    QLineEdit *m_dir;
    QLineEdit *m_name;
    QVBoxLayout *m_files;
    QCheckBox *m_zones;
    QPushButton *m_button;
    bool m_collapsed = false;
    bool m_updating = false;
};
