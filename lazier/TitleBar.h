#pragma once

#include <QtCore/QPoint>
#include <QtGui/QPixmap>
#include <QtWidgets/QWidget>

class QSlider;
class QLabel;

class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    bool stayOnTop() const;
    bool ghostMode() const;
    int displayOpacity() const;
    bool addressBarVisible() const;
    void resetToDefaults();

signals:
    void stayOnTopChanged(bool on);
    void ghostModeChanged(bool on);
    void displayOpacityChanged(int percent);
    void addressBarVisibleChanged(bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum Button {
        None = 0,
        ToggleAddress,
        Opacity,
        Pin,
        Ghost,
        Minimize,
        Maximize,
        Close
    };

    static const int kButtonCount = 7;
    static const Button kButtonOrder[kButtonCount];

    Button hitTest(const QPoint &pos) const;
    QRect buttonRect(Button button) const;
    void setHover(Button button);
    void toggleMaximize();
    void updateTooltip(Button button);
    bool isToggled(Button button) const;
    void drawButtonIcon(QPainter &painter, const QRect &rc, const QPixmap &icon, bool dimmed) const;
    void setDisplayOpacity(int percent);
    void showOpacityPopup();

    QPixmap m_appIcon;
    QPixmap m_pinIcon;
    QPixmap m_ghostIcon;
    QPixmap m_opacityIcon;
    QWidget *m_opacityPopup = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityLabel = nullptr;
    Button m_hover = None;
    Button m_pressed = None;
    bool m_dragging = false;
    bool m_stayOnTop = false;
    bool m_ghostMode = false;
    bool m_addressBarVisible = true;
    int m_displayOpacity = 100;
    QPoint m_dragOffset;
};
