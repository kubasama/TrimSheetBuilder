#pragma once

#include "gui/SheetView.h" // EditMode

#include <QPointer>
#include <QWidget>

class Project;
struct Zone;
#include <QSet>
class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

// Side panel: sheet settings, zone geometry (layout mode) and fill + maps
// (fill mode) for the selected zone.
class InspectorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget *parent = nullptr);

    void setProject(Project *project);
    void setZone(const QString &zoneId);
    void setMode(EditMode mode);
    void setSnapSettings(const SnapSettings &s);

signals:
    void statusMessage(const QString &text);
    void snapSettingsEdited(const SnapSettings &s);
    void editChannelsRequested();

private:
    struct MapRow {
        QString channelId;
        QLabel *thumb;
        QLabel *info;
        QComboBox *convention; // normal channels only
        QAction *embedBtn;
        QAction *clearBtn;
        // per-component textures (R/G/B/A packing); not for normal / grayscale
        QToolButton *expand = nullptr;
        QWidget *compPanel = nullptr;
        struct Comp {
            QWidget *frame = nullptr;
            QLabel *thumb = nullptr;
            QLabel *info = nullptr;
            QComboBox *source = nullptr;
            QToolButton *clear = nullptr;
        } comps[4];
    };

    void buildMapRows();
    void refresh();
    void refreshSheet();
    const Zone *zone() const;
    void pushZone(const Zone &after, const QString &text, const QString &mergeKey = {});

    // component -1 = the channel's main texture, 0..3 = R/G/B/A
    void browseTexture(const QString &channelId, int component = -1);
    void pasteTexture(const QString &channelId, int component = -1);
    void assign(const QString &channelId, int component, const QString &textureId);
    QString textureLabel(const QString &textureId) const;

    QPointer<Project> m_project;
    QString m_zoneId;
    EditMode m_mode = EditMode::Layout;
    bool m_updating = false;

    // sheet
    QLabel *m_sheetSize;
    QSpinBox *m_padding;
    QLabel *m_channelSummary;
    // snapping
    QGroupBox *m_snapBox;
    QCheckBox *m_snapOn, *m_snapGrid, *m_snapEdges, *m_overlap;
    QSpinBox *m_grid;
    SnapSettings m_snap;
    // zone
    QGroupBox *m_zoneBox;
    QLineEdit *m_name;
    QSpinBox *m_x, *m_y, *m_w, *m_h;
    QCheckBox *m_locked;
    QToolButton *m_deleteZone;
    QToolButton *m_renameMat;
    // fill
    QGroupBox *m_fillBox;
    QComboBox *m_material;
    QLabel *m_materialUsage;
    QComboBox *m_fillMode;
    QDoubleSpinBox *m_rotation;
    QCheckBox *m_flipX, *m_flipY;
    QDoubleSpinBox *m_scaleX, *m_scaleY, *m_offX, *m_offY;
    QToolButton *m_linkScale, *m_linkOffset;
    // maps
    QGroupBox *m_mapsBox;
    QVBoxLayout *m_mapsLayout;
    QVector<MapRow> m_rows;
    QSet<QString> m_expanded; // channels with the R/G/B/A section open
    QLabel *m_noZone;
};
