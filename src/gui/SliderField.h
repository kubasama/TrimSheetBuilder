#pragma once
// Label + slider + small number field in one row, with a fixed layout so
// rows don't jump around when the range or label changes.

#include <QWidget>

class QLabel;
class QSlider;
class QSpinBox;

class SliderField : public QWidget
{
    Q_OBJECT
public:
    explicit SliderField(const QString &label, QWidget *parent = nullptr);

    void setLabel(const QString &text);
    void setRange(int min, int max);
    void setSuffix(const QString &suffix);
    int value() const;
    void setValue(int v);
    // greys out the row and shows `reason` in place of the label tooltip
    void setActive(bool active, const QString &reason = {});

signals:
    void valueChanged(int value);

private:
    QLabel *m_label;
    QSlider *m_slider;
    QSpinBox *m_spin;
};
