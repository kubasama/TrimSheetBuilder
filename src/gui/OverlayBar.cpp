#include "gui/OverlayBar.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>

OverlayBar::OverlayBar(QWidget *parent)
    : QWidget(parent), m_layout(new QHBoxLayout(this))
{
    m_layout->setContentsMargins(3, 3, 3, 3);
    m_layout->setSpacing(2);
    setAttribute(Qt::WA_TranslucentBackground);
}

QToolButton *OverlayBar::addButton(const QString &text, bool checkable)
{
    auto *b = new QToolButton(this);
    b->setText(text);
    b->setCheckable(checkable);
    b->setProperty("overlay", true);
    b->setProperty("compact", m_compact);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCursor(Qt::PointingHandCursor);
    m_layout->addWidget(b);
    b->show(); // children added to an already visible parent start hidden
    adjustSize();
    return b;
}

void OverlayBar::addSeparator()
{
    auto *line = new QWidget(this);
    line->setFixedSize(1, 16);
    line->setStyleSheet(QStringLiteral("background: rgba(255,255,255,40);"));
    m_layout->addSpacing(3);
    m_layout->addWidget(line, 0, Qt::AlignVCenter);
    line->show();
    m_layout->addSpacing(3);
}

void OverlayBar::clear()
{
    while (QLayoutItem *it = m_layout->takeAt(0)) {
        delete it->widget();
        delete it;
    }
}

void OverlayBar::setCompact(bool compact)
{
    m_compact = compact;
    m_layout->setContentsMargins(compact ? 2 : 3, compact ? 2 : 3, compact ? 2 : 3, compact ? 2 : 3);
    m_layout->setSpacing(compact ? 1 : 2);
    update();
}

void OverlayBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(255, 255, 255, m_compact ? 10 : 18));
    p.setBrush(QColor(30, 30, 30, m_compact ? 130 : 205));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_compact ? 5 : 6, m_compact ? 5 : 6);
}
