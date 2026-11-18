#pragma once
// Preferences and About windows.

#include <QDialog>
#include <QKeySequence>
#include <QVector>

class QAction;
class QKeySequenceEdit;

class ColorButton;
class QCheckBox;
class QLabel;
class QRadioButton;

struct AppPreferences {
    QColor customAccent;      // invalid = use the desktop accent
    bool showGrid = true;
    bool showZoneNames = true;

    static AppPreferences load();
    void save() const;
};

// One rebindable action.
struct ShortcutEntry {
    QString id;                     // settings key, e.g. "file.save"
    QString category;               // "File", "Edit", "View"
    QString label;
    QList<QKeySequence> defaults;
    QList<QKeySequence> current;
    QAction *action = nullptr;
};

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    PreferencesDialog(const AppPreferences &prefs, const QVector<ShortcutEntry> &shortcuts,
                      QWidget *parent = nullptr);
    AppPreferences preferences() const;
    QVector<ShortcutEntry> shortcuts() const;

    void accept() override;

private:
    QWidget *buildGeneralTab(const AppPreferences &prefs);
    QWidget *buildShortcutsTab();

    QVector<ShortcutEntry> m_shortcuts;
    QVector<QKeySequenceEdit *> m_editors;

    QRadioButton *m_systemAccent;
    QRadioButton *m_customAccent;
    ColorButton *m_accentColor;
    QLabel *m_detected;
    QCheckBox *m_grid;
    QCheckBox *m_names;
};

class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

// "The document 'X' has been modified. Do you want to save it?"
// Buttons, left to right: Cancel, No, Yes.
enum class SaveAnswer { Yes, No, Cancel };
SaveAnswer askToSave(QWidget *parent, const QString &documentName);
