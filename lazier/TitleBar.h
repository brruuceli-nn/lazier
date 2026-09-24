#pragma once

#include <QtCore/QPoint>
#include <QtGui/QPixmap>
#include <QtWidgets/QWidget>

class QButtonGroup;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QTimer;

class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    bool stayOnTop() const;
    bool ghostMode() const;
    bool ghostEnhanced() const;
    int ghostModifiers() const;
    int ghostVirtualKey() const;
    int displayOpacity() const;
    bool addressBarVisible() const;
    void resetToDefaults();

signals:
    void stayOnTopChanged(bool on);
    void ghostSettingsChanged(bool enabled, bool enhanced, int modifiers, int virtualKey);
    void displayOpacityChanged(int percent);
    void addressBarVisibleChanged(bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    void showGhostPopup();
    void createGhostPopup();
    void onGhostOption(int id);
    void toggleGhostRecord();
    void pollGhostRecord();
    void stopGhostRecord(bool commit);
    void loadGhostSettings();
    void saveGhostSettings() const;
    void emitGhostSettings();
    void updateGhostKeyLabel();
    void updateGhostTooltip();

    QPixmap m_appIcon;
    QPixmap m_pinIcon;
    QPixmap m_ghostIcon;
    QPixmap m_opacityIcon;
    QWidget *m_opacityPopup = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityLabel = nullptr;
    QWidget *m_ghostPopup = nullptr;
    QButtonGroup *m_ghostGroup = nullptr;
    QRadioButton *m_ghostOff = nullptr;
    QRadioButton *m_ghostMouse = nullptr;
    QRadioButton *m_ghostEnhancedButton = nullptr;
    QLabel *m_ghostKeyLabel = nullptr;
    QPushButton *m_ghostRecordButton = nullptr;
    QTimer *m_ghostRecordTimer = nullptr;
    Button m_hover = None;
    Button m_pressed = None;
    bool m_dragging = false;
    bool m_stayOnTop = false;
    int m_ghostOption = 0;
    int m_ghostModifiers = 0;
    int m_ghostVirtualKey = 0;
    bool m_recording = false;
    bool m_recordSeen = false;
    int m_recordMods = 0;
    int m_recordVk = 0;
    int m_recordCount = 0;
    bool m_addressBarVisible = true;
    int m_displayOpacity = 100;
    QPoint m_dragOffset;
};
