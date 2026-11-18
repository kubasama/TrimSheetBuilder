#pragma once
// Small translucent button strip floating over the canvas.

#include <QWidget>

class QHBoxLayout;
class QToolButton;

class OverlayBar : public QWidget
{
    Q_OBJECT
public:
    explicit OverlayBar(QWidget *parent = nullptr);

    QToolButton *addButton(const QString &text, bool checkable = true);
    void addSeparator();
    void clear();
    // smaller, fainter variant for secondary controls
    void setCompact(bool compact);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QHBoxLayout *m_layout;
    bool m_compact = false;
};
