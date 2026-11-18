#include "gui/DragValue.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QSpinBox>
#include <cmath>

namespace {

class DragFilter : public QObject
{
public:
    using QObject::QObject;

    bool eventFilter(QObject *o, QEvent *e) override
    {
        auto *edit = qobject_cast<QLineEdit *>(o);
        auto *spin = edit ? qobject_cast<QAbstractSpinBox *>(edit->parentWidget()) : nullptr;
        if (!spin || !spin->isEnabled())
            return false;

        switch (e->type()) {
        case QEvent::FocusIn:
            edit->setCursor(Qt::IBeamCursor);
            return false;
        case QEvent::FocusOut:
            edit->setCursor(Qt::SizeHorCursor);
            return false;
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(e);
            if (me->button() != Qt::LeftButton || edit->hasFocus())
                return false; // already typing: normal text editing
            m_spin = spin;
            m_pressX = me->globalPosition().x();
            m_start = value(spin);
            m_dragging = false;
            return true;
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(e);
            if (m_spin != spin || !(me->buttons() & Qt::LeftButton))
                return false;
            const double dx = me->globalPosition().x() - m_pressX;
            if (!m_dragging && std::abs(dx) < 3)
                return true;
            m_dragging = true;
            double step = spin->property("dragStep").toDouble();
            if (step <= 0)
                step = singleStep(spin) * 0.5;
            if (me->modifiers() & Qt::ShiftModifier)
                step *= 0.1;
            double v = m_start + dx * step;
            if (me->modifiers() & Qt::ControlModifier) {
                const double snap = singleStep(spin) * 10;
                v = std::round(v / snap) * snap;
            }
            setValue(spin, v);
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto *me = static_cast<QMouseEvent *>(e);
            if (m_spin != spin || me->button() != Qt::LeftButton)
                return false;
            const bool dragged = m_dragging;
            m_spin = nullptr;
            m_dragging = false;
            if (dragged) {
                emit spin->editingFinished();
            } else {
                // plain click: start typing
                spin->setFocus(Qt::MouseFocusReason);
                edit->selectAll();
            }
            return true;
        }
        default:
            return false;
        }
    }

private:
    static double value(QAbstractSpinBox *s)
    {
        if (auto *d = qobject_cast<QDoubleSpinBox *>(s)) return d->value();
        if (auto *i = qobject_cast<QSpinBox *>(s)) return i->value();
        return 0;
    }
    static double singleStep(QAbstractSpinBox *s)
    {
        if (auto *d = qobject_cast<QDoubleSpinBox *>(s)) return d->singleStep();
        if (auto *i = qobject_cast<QSpinBox *>(s)) return i->singleStep();
        return 1;
    }
    static void setValue(QAbstractSpinBox *s, double v)
    {
        if (auto *d = qobject_cast<QDoubleSpinBox *>(s)) {
            if (d->wrapping()) {
                const double range = d->maximum() - d->minimum();
                v = d->minimum() + std::fmod(std::fmod(v - d->minimum(), range) + range, range);
            }
            d->setValue(v);
        } else if (auto *i = qobject_cast<QSpinBox *>(s)) {
            i->setValue(int(std::lround(v)));
        }
    }

    QPointer<QAbstractSpinBox> m_spin;
    double m_pressX = 0;
    double m_start = 0;
    bool m_dragging = false;
};

DragFilter *filter()
{
    static DragFilter *f = new DragFilter(qApp);
    return f;
}

} // namespace

void DragValue::enable(QAbstractSpinBox *spin)
{
    if (!spin || spin->property("_dragValue").toBool())
        return;
    auto *edit = spin->findChild<QLineEdit *>();
    if (!edit)
        return;
    spin->setProperty("_dragValue", true);
    // no focus on press, or every click would start text editing; we focus
    // on release when the click wasn't a drag
    spin->setFocusPolicy(Qt::TabFocus);
    edit->setFocusPolicy(Qt::TabFocus);
    edit->installEventFilter(filter());
    edit->setCursor(Qt::SizeHorCursor);
}
