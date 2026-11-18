#include "gui/SliderField.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>

SliderField::SliderField(const QString &label, QWidget *parent)
    : QWidget(parent)
    , m_label(new QLabel(label, this))
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_spin(new QSpinBox(this))
{
    m_label->setFixedWidth(110);
    m_spin->setFixedWidth(84);
    m_spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);
    lay->addWidget(m_label);
    lay->addWidget(m_slider, 1);
    lay->addWidget(m_spin);

    connect(m_slider, &QSlider::valueChanged, m_spin, &QSpinBox::setValue);
    connect(m_spin, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_slider->value() != v)
            m_slider->setValue(v);
        emit valueChanged(v);
    });
}

void SliderField::setLabel(const QString &text) { m_label->setText(text); }

void SliderField::setRange(int min, int max)
{
    const QSignalBlocker b(m_slider), b2(m_spin);
    m_slider->setRange(min, max);
    m_spin->setRange(min, max);
    m_slider->setPageStep(qMax(1, (max - min) / 8));
}

void SliderField::setSuffix(const QString &suffix) { m_spin->setSuffix(suffix); }
int SliderField::value() const { return m_spin->value(); }

void SliderField::setValue(int v)
{
    {
        const QSignalBlocker b(m_slider);
        m_slider->setValue(v);
    }
    m_spin->setValue(v); // emits valueChanged if it changed
}

void SliderField::setActive(bool active, const QString &reason)
{
    m_slider->setEnabled(active);
    m_spin->setEnabled(active);
    m_label->setEnabled(active);
    setToolTip(active ? QString() : reason);
}
