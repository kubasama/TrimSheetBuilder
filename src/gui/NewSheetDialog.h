#pragma once

#include "core/ChannelPresets.h"
#include "core/LayoutPresets.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QSpinBox;
class LayoutPreview;
class SliderField;

class NewSheetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewSheetDialog(QWidget *parent = nullptr);

    QSize sheetSize() const;
    int padding() const;
    QVector<LayoutPresets::GeneratedZone> zones() const;
    QVector<Channel> channels() const { return m_channels; }

private:
    void sizeEdited();
    void presetChanged();
    void updatePreview();
    void channelPresetChanged();
    void customizeChannels();
    void updateChannelSummary();
    QString presetId() const;

    QButtonGroup *m_sizeButtons;
    QSpinBox *m_width;
    QSpinBox *m_height;
    QCheckBox *m_square;
    QListWidget *m_presets;
    QLabel *m_description;
    SliderField *m_count;
    SliderField *m_count2;
    SliderField *m_gap;
    SliderField *m_padding;
    QLabel *m_stats;
    LayoutPreview *m_preview;
    QComboBox *m_channelPreset;
    QLabel *m_channelSummary;
    QVector<Channel> m_channels;
    bool m_updating = false;
};
