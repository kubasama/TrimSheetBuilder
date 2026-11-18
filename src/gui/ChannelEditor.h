#pragma once
// Editing the list of channels ("layers") of a sheet, and channel presets.

#include "core/ChannelPresets.h"

#include <QDialog>
#include <QToolButton>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Custom channel presets, stored in the app settings.
namespace ChannelPresetStore {
QVector<ChannelPresets::Preset> custom();
QVector<ChannelPresets::Preset> all();        // built-ins first, then custom
void save(const QString &name, const QVector<Channel> &channels);
void remove(const QString &name);
} // namespace ChannelPresetStore

// Small button showing a color swatch; opens a color dialog.
class ColorButton : public QToolButton
{
    Q_OBJECT
public:
    explicit ColorButton(QWidget *parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
signals:
    void colorChanged(const QColor &c);
protected:
    void paintEvent(QPaintEvent *e) override;
private:
    QColor m_color{128, 128, 128};
};

class ChannelListEditor : public QWidget
{
    Q_OBJECT
public:
    explicit ChannelListEditor(QWidget *parent = nullptr);

    QVector<Channel> channels() const { return m_channels; }
    void setChannels(const QVector<Channel> &channels);
    QString validate() const; // empty = ok, else a message

signals:
    void changed();

private:
    void rebuildList();
    void showCurrent();
    void editCurrent();
    int current() const;

    QVector<Channel> m_channels;
    QListWidget *m_list;
    QPushButton *m_remove, *m_up, *m_down;
    QWidget *m_form;
    QLineEdit *m_name;
    QComboBox *m_kind;
    QLineEdit *m_suffix;
    ColorButton *m_default;
    QLabel *m_conventionLabel;
    QComboBox *m_convention;
    QCheckBox *m_export;
    QCheckBox *m_alpha;
    bool m_updating = false;
};

// Channel list editor with preset load / save, OK / Cancel.
class ChannelsDialog : public QDialog
{
    Q_OBJECT
public:
    ChannelsDialog(const QVector<Channel> &channels, bool editingProject, QWidget *parent = nullptr);
    QVector<Channel> channels() const;

    void accept() override;

private:
    void refillPresets();

    ChannelListEditor *m_editor;
    QComboBox *m_presets;
    QPushButton *m_deletePreset;
    bool m_editingProject;
};
