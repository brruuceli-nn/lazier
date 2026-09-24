#include "TitleBar.h"

#include "Icons.h"

#include <QtCore/QEvent>
#include <QtCore/QSettings>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
const int kHeight = 30;
const int kButtonWidth = 28;
const int kIcon = 8;
const int kButtonRightPad = 6;

QString virtualKeyText(int virtualKey)
{
#ifdef Q_OS_WIN
    if (virtualKey <= 0)
        return QString();
    const UINT scan = MapVirtualKeyW(UINT(virtualKey), MAPVK_VK_TO_VSC);
    LONG param = LONG(scan) << 16;
    switch (virtualKey) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
    case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
        param |= 1 << 24;
        break;
    default:
        break;
    }
    wchar_t name[64] = {};
    if (GetKeyNameTextW(param, name, 64) > 0)
        return QString::fromWCharArray(name);
#else
    Q_UNUSED(virtualKey)
#endif
    return QString();
}

QString ghostHotkeyText(int modifiers, int virtualKey)
{
    QStringList parts;
    if (modifiers & Qt::ControlModifier)
        parts << QStringLiteral("Ctrl");
    if (modifiers & Qt::ShiftModifier)
        parts << QStringLiteral("Shift");
    if (modifiers & Qt::AltModifier)
        parts << QStringLiteral("Alt");
    if (modifiers & Qt::MetaModifier)
        parts << QStringLiteral("Win");
    const QString key = virtualKeyText(virtualKey);
    if (!key.isEmpty())
        parts << key;
    return parts.join(QStringLiteral("+"));
}

#ifdef Q_OS_WIN
bool isKeyDown(int virtualKey)
{
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool isIgnoredRecordKey(int virtualKey)
{
    switch (virtualKey) {
    case VK_LBUTTON: case VK_RBUTTON: case VK_MBUTTON:
    case VK_XBUTTON1: case VK_XBUTTON2:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
    case VK_ESCAPE:
        return true;
    default:
        return false;
    }
}

int findRecordedVirtualKey()
{
    for (int virtualKey = 1; virtualKey < 256; ++virtualKey) {
        if (isIgnoredRecordKey(virtualKey))
            continue;
        if (isKeyDown(virtualKey))
            return virtualKey;
    }
    return 0;
}

int recordedModifierMask()
{
    int modifiers = 0;
    if (isKeyDown(VK_CONTROL))
        modifiers |= Qt::ControlModifier;
    if (isKeyDown(VK_SHIFT))
        modifiers |= Qt::ShiftModifier;
    if (isKeyDown(VK_MENU))
        modifiers |= Qt::AltModifier;
    if (isKeyDown(VK_LWIN) || isKeyDown(VK_RWIN))
        modifiers |= Qt::MetaModifier;
    return modifiers;
}

int modifierCount(int modifiers, int virtualKey)
{
    int count = virtualKey != 0 ? 1 : 0;
    if (modifiers & Qt::ControlModifier)
        ++count;
    if (modifiers & Qt::ShiftModifier)
        ++count;
    if (modifiers & Qt::AltModifier)
        ++count;
    if (modifiers & Qt::MetaModifier)
        ++count;
    return count;
}
#endif
}

const TitleBar::Button TitleBar::kButtonOrder[TitleBar::kButtonCount] = {
    TitleBar::Close,
    TitleBar::Maximize,
    TitleBar::Minimize,
    TitleBar::Ghost,
    TitleBar::Pin,
    TitleBar::Opacity,
    TitleBar::ToggleAddress
};

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(kHeight);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    m_appIcon = lazierAppPixmap(16);
    m_pinIcon.load(QStringLiteral(":/lazier/image/fix.png"));
    m_ghostIcon.load(QStringLiteral(":/lazier/image/transparent.png"));
    m_opacityIcon.load(QStringLiteral(":/lazier/image/percent.png"));
    loadGhostSettings();
}

QSize TitleBar::sizeHint() const
{
    return QSize(200, kHeight);
}

bool TitleBar::stayOnTop() const
{
    return m_stayOnTop;
}

bool TitleBar::ghostMode() const
{
    return m_ghostOption != 0;
}

bool TitleBar::ghostEnhanced() const
{
    return m_ghostOption == 2;
}

int TitleBar::ghostModifiers() const
{
    return m_ghostModifiers;
}

int TitleBar::ghostVirtualKey() const
{
    return m_ghostVirtualKey;
}

int TitleBar::displayOpacity() const
{
    return m_displayOpacity;
}

bool TitleBar::addressBarVisible() const
{
    return m_addressBarVisible;
}

void TitleBar::resetToDefaults()
{
    if (m_stayOnTop) {
        m_stayOnTop = false;
        emit stayOnTopChanged(false);
    }
    if (m_ghostOption != 0) {
        m_ghostOption = 0;
        saveGhostSettings();
        emitGhostSettings();
    }
    setDisplayOpacity(100);
    if (!m_addressBarVisible) {
        m_addressBarVisible = true;
        emit addressBarVisibleChanged(true);
    }
    update();
}

TitleBar::Button TitleBar::hitTest(const QPoint &pos) const
{
    for (int i = 0; i < kButtonCount; ++i) {
        if (buttonRect(kButtonOrder[i]).contains(pos))
            return kButtonOrder[i];
    }
    return None;
}

QRect TitleBar::buttonRect(Button button) const
{
    for (int i = 0; i < kButtonCount; ++i) {
        if (kButtonOrder[i] == button)
            return QRect(width() - kButtonRightPad - (i + 1) * kButtonWidth, 0, kButtonWidth, height());
    }
    return QRect();
}

void TitleBar::setHover(Button button)
{
    if (m_hover == button)
        return;
    m_hover = button;
    updateTooltip(button);
    update();
}

void TitleBar::updateTooltip(Button button)
{
    if (button == Pin)
        setToolTip(QStringLiteral("置顶"));
    else if (button == Ghost) {
        const QString hotkey = ghostHotkeyText(m_ghostModifiers, m_ghostVirtualKey);
        if (m_ghostOption == 2 && !hotkey.isEmpty())
            setToolTip(QStringLiteral("按住 %1，鼠标在窗口上才显示").arg(hotkey));
        else if (m_ghostOption == 2)
            setToolTip(QStringLiteral("增强：请先录入按键，未设置时仍按鼠标显示"));
        else if (m_ghostOption == 1)
            setToolTip(QStringLiteral("鼠标在窗口上才显示"));
        else
            setToolTip(QStringLiteral("透明设置"));
    }
    else if (button == Opacity)
        setToolTip(QStringLiteral("正常显示透明度: %1").arg(m_displayOpacity));
    else if (button == ToggleAddress)
        setToolTip(m_addressBarVisible ? QStringLiteral("隐藏地址栏") : QStringLiteral("显示地址栏"));
    else
        setToolTip(QString());
}

bool TitleBar::isToggled(Button button) const
{
    return (button == Pin && m_stayOnTop)
        || (button == Ghost && m_ghostOption != 0)
        || (button == Opacity && m_displayOpacity < 100)
        || (button == ToggleAddress && !m_addressBarVisible);
}

void TitleBar::toggleMaximize()
{
    QWidget *win = window();
    if (!win)
        return;
    if (win->isMaximized())
        win->showNormal();
    else
        win->showMaximized();
    update();
}

void TitleBar::drawButtonIcon(QPainter &painter, const QRect &rc, const QPixmap &icon, bool dimmed) const
{
    if (icon.isNull())
        return;
    const int size = 16;
    const QRect dest(rc.center().x() - size / 2, rc.center().y() - size / 2, size, size);
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setOpacity(dimmed ? 0.45 : 1.0);
    painter.drawPixmap(dest, icon);
    painter.restore();
}

void TitleBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor background(QStringLiteral("#FFFFFF"));
    painter.fillRect(rect(), background);

    const int appSize = 16;
    const QRect appRect(8, (height() - appSize) / 2, appSize, appSize);
    if (!m_appIcon.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawPixmap(appRect, m_appIcon);
    }

    painter.setPen(QColor(QStringLiteral("#222222")));
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);
    const int textLeft = appRect.right() + 8;
    const int textRight = buttonRect(ToggleAddress).left() - 8;
    painter.drawText(QRect(textLeft, 0, qMax(0, textRight - textLeft), height()),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     window() ? window()->windowTitle() : QStringLiteral("lazier"));

    const bool maximized = window() && window()->isMaximized();
    for (int i = 0; i < kButtonCount; ++i) {
        const Button button = kButtonOrder[i];
        const QRect rc = buttonRect(button);
        const bool hovered = (m_hover == button || m_pressed == button);
        const bool toggled = isToggled(button);
        QColor back;
        if (button == Close && hovered)
            back = QColor(QStringLiteral("#C42B1C"));
        else if (hovered)
            back = QColor(QStringLiteral("#E5E5E5"));
        else if (toggled)
            back = QColor(QStringLiteral("#DCE8F5"));
        if (back.isValid())
            painter.fillRect(rc, back);

        const QColor iconColor = (button == Close && hovered)
            ? QColor(QStringLiteral("#FFFFFF"))
            : (toggled ? QColor(QStringLiteral("#1B6EC2")) : QColor(QStringLiteral("#333333")));
        painter.setPen(QPen(iconColor, 1.0));
        const QPoint c = rc.center();
        const int half = kIcon / 2;
        if (button == Pin) {
            drawButtonIcon(painter, rc, m_pinIcon, !toggled && !hovered);
        } else if (button == Ghost) {
            drawButtonIcon(painter, rc, m_ghostIcon, !toggled && !hovered);
        } else if (button == Opacity) {
            drawButtonIcon(painter, rc, m_opacityIcon, !toggled && !hovered);
        } else if (button == ToggleAddress) {
            painter.setBrush(iconColor);
            painter.setPen(Qt::NoPen);
            const int half = 4;
            QPoint points[3];
            if (m_addressBarVisible) {
                points[0] = QPoint(c.x() - half, c.y() - 2);
                points[1] = QPoint(c.x() + half, c.y() - 2);
                points[2] = QPoint(c.x(), c.y() + 3);
            } else {
                points[0] = QPoint(c.x() - half, c.y() + 2);
                points[1] = QPoint(c.x() + half, c.y() + 2);
                points[2] = QPoint(c.x(), c.y() - 3);
            }
            painter.drawPolygon(points, 3);
            painter.setBrush(Qt::NoBrush);
        } else if (button == Minimize) {
            painter.drawLine(c.x() - half, c.y(), c.x() + half, c.y());
        } else if (button == Maximize) {
            if (maximized) {
                const QRect backRect(c.x() - 2, c.y() - 4, 6, 6);
                const QRect frontRect(c.x() - 4, c.y() - 2, 6, 6);
                painter.drawRect(backRect);
                painter.fillRect(frontRect, hovered ? back : background);
                painter.drawRect(frontRect);
            } else {
                painter.drawRect(QRect(c.x() - half, c.y() - half, kIcon, kIcon));
            }
        } else {
            painter.drawLine(c.x() - half, c.y() - half, c.x() + half, c.y() + half);
            painter.drawLine(c.x() + half, c.y() - half, c.x() - half, c.y() + half);
        }
    }
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_pressed = hitTest(event->pos());
    if (m_pressed != None) {
        update();
        return;
    }

    QWidget *win = window();
    if (!win)
        return;

    if (win->isMaximized()) {
        const QRect normal = win->normalGeometry();
        const QPoint global = event->globalPos();
        const qreal ratio = width() > 0
            ? qBound(0.0, static_cast<qreal>(event->x()) / width(), 1.0)
            : 0.5;
        win->showNormal();
        const int nx = global.x() - static_cast<int>(normal.width() * ratio);
        const int ny = global.y() - event->y();
        win->move(nx, ny);
        m_dragOffset = global - win->frameGeometry().topLeft();
    } else {
        m_dragOffset = event->globalPos() - win->frameGeometry().topLeft();
    }
    m_dragging = true;
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && window()) {
        window()->move(event->globalPos() - m_dragOffset);
        return;
    }

    if (m_pressed != None)
        setHover(hitTest(event->pos()) == m_pressed ? m_pressed : None);
    else
        setHover(hitTest(event->pos()));
}

void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const Button released = m_pressed;
    const bool inside = hitTest(event->pos()) == released;
    m_pressed = None;
    m_dragging = false;
    setHover(hitTest(event->pos()));

    if (!inside || !window())
        return;

    if (released == Minimize) {
        window()->showMinimized();
    } else if (released == Maximize) {
        toggleMaximize();
    } else if (released == Close) {
        window()->close();
    } else if (released == Pin) {
        m_stayOnTop = !m_stayOnTop;
        update();
        emit stayOnTopChanged(m_stayOnTop);
    } else if (released == Ghost) {
        showGhostPopup();
    } else if (released == Opacity) {
        showOpacityPopup();
    } else if (released == ToggleAddress) {
        m_addressBarVisible = !m_addressBarVisible;
        updateTooltip(ToggleAddress);
        update();
        emit addressBarVisibleChanged(m_addressBarVisible);
    }
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && hitTest(event->pos()) == None)
        toggleMaximize();
}

void TitleBar::leaveEvent(QEvent *)
{
    if (!m_dragging) {
        m_hover = None;
        setToolTip(QString());
        update();
    }
}

void TitleBar::wheelEvent(QWheelEvent *event)
{
    if (hitTest(event->pos()) != Opacity) {
        QWidget::wheelEvent(event);
        return;
    }
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0)
        setDisplayOpacity(m_displayOpacity + steps * 5);
    event->accept();
}

void TitleBar::setDisplayOpacity(int percent)
{
    const int clamped = qBound(1, percent, 100);
    if (m_displayOpacity == clamped)
        return;
    m_displayOpacity = clamped;
    if (m_opacitySlider && m_opacitySlider->value() != clamped)
        m_opacitySlider->setValue(clamped);
    if (m_opacityLabel)
        m_opacityLabel->setText(QString::number(clamped));
    updateTooltip(m_hover);
    update();
    emit displayOpacityChanged(m_displayOpacity);
}

void TitleBar::showOpacityPopup()
{
    if (!m_opacityPopup) {
        m_opacityPopup = new QWidget(window(), Qt::Popup | Qt::FramelessWindowHint);
        m_opacityPopup->setFixedSize(176, 36);
        m_opacityPopup->setStyleSheet(QStringLiteral(
            "QWidget { background: #FFFFFF; border: 1px solid #D0D0D0; }"
            "QLabel { border: none; color: #222222; }"));

        m_opacitySlider = new QSlider(Qt::Horizontal, m_opacityPopup);
        m_opacitySlider->setRange(1, 100);
        m_opacitySlider->setValue(m_displayOpacity);
        m_opacityLabel = new QLabel(QString::number(m_displayOpacity), m_opacityPopup);
        m_opacityLabel->setFixedWidth(28);
        m_opacityLabel->setAlignment(Qt::AlignCenter);

        QHBoxLayout *layout = new QHBoxLayout(m_opacityPopup);
        layout->setContentsMargins(8, 4, 8, 4);
        layout->addWidget(m_opacitySlider, 1);
        layout->addWidget(m_opacityLabel);

        connect(m_opacitySlider, &QSlider::valueChanged, this, &TitleBar::setDisplayOpacity);
    }

    const QRect rc = buttonRect(Opacity);
    QPoint pos = mapToGlobal(QPoint(rc.right() - m_opacityPopup->width(), rc.bottom()));
    m_opacityPopup->move(pos);
    m_opacityPopup->show();
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_ghostPopup && event->type() == QEvent::Hide)
        stopGhostRecord(false);
    return QWidget::eventFilter(watched, event);
}

void TitleBar::showGhostPopup()
{
    if (!m_ghostPopup)
        createGhostPopup();

    m_ghostGroup->blockSignals(true);
    if (QAbstractButton *button = m_ghostGroup->button(m_ghostOption))
        button->setChecked(true);
    m_ghostGroup->blockSignals(false);
    m_ghostRecordButton->setEnabled(m_ghostOption == 2);
    updateGhostKeyLabel();

    const QRect rc = buttonRect(Ghost);
    QPoint pos = mapToGlobal(QPoint(rc.right() - m_ghostPopup->width(), rc.bottom()));
    if (pos.x() < 0)
        pos.setX(0);
    m_ghostPopup->move(pos);
    m_ghostPopup->show();
}

void TitleBar::createGhostPopup()
{
    m_ghostPopup = new QWidget(window(), Qt::Popup | Qt::FramelessWindowHint);
    m_ghostPopup->setFixedSize(268, 112);
    m_ghostPopup->setStyleSheet(QStringLiteral(
        "QWidget { background: #FFFFFF; border: 1px solid #D0D0D0; }"
        "QRadioButton, QLabel, QPushButton { border: none; color: #222222; background: transparent; }"
        "QPushButton#recordButton { background: #F3F3F3; padding: 2px 8px; }"
        "QPushButton#recordButton:disabled { color: #AAAAAA; background: #F7F7F7; }"));
    m_ghostPopup->installEventFilter(this);

    m_ghostOff = new QRadioButton(QStringLiteral("关闭"), m_ghostPopup);
    m_ghostMouse = new QRadioButton(QStringLiteral("鼠标（默认）"), m_ghostPopup);
    m_ghostEnhancedButton = new QRadioButton(QStringLiteral("增强"), m_ghostPopup);
    m_ghostMouse->setToolTip(QStringLiteral("鼠标在窗口上才显示，离开后透明"));
    m_ghostEnhancedButton->setToolTip(QStringLiteral("按住录入的键，并且鼠标在窗口上才显示"));

    m_ghostGroup = new QButtonGroup(m_ghostPopup);
    m_ghostGroup->addButton(m_ghostOff, 0);
    m_ghostGroup->addButton(m_ghostMouse, 1);
    m_ghostGroup->addButton(m_ghostEnhancedButton, 2);

    m_ghostKeyLabel = new QLabel(m_ghostPopup);
    m_ghostKeyLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_ghostRecordButton = new QPushButton(QStringLiteral("录入"), m_ghostPopup);
    m_ghostRecordButton->setObjectName(QStringLiteral("recordButton"));
    m_ghostRecordButton->setFixedWidth(52);
    m_ghostRecordButton->setEnabled(false);

    QVBoxLayout *layout = new QVBoxLayout(m_ghostPopup);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);
    layout->addWidget(m_ghostOff);
    layout->addWidget(m_ghostMouse);
    QHBoxLayout *enhancedRow = new QHBoxLayout;
    enhancedRow->setContentsMargins(0, 0, 0, 0);
    enhancedRow->setSpacing(6);
    enhancedRow->addWidget(m_ghostEnhancedButton);
    enhancedRow->addWidget(m_ghostKeyLabel, 1);
    enhancedRow->addWidget(m_ghostRecordButton);
    layout->addLayout(enhancedRow);

    connect(m_ghostGroup, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, &TitleBar::onGhostOption);
    connect(m_ghostRecordButton, &QPushButton::clicked, this, &TitleBar::toggleGhostRecord);

    m_ghostRecordTimer = new QTimer(this);
    m_ghostRecordTimer->setInterval(30);
    connect(m_ghostRecordTimer, &QTimer::timeout, this, &TitleBar::pollGhostRecord);
}

void TitleBar::onGhostOption(int id)
{
    if (id != 2)
        stopGhostRecord(false);
    m_ghostOption = id;
    if (m_ghostRecordButton)
        m_ghostRecordButton->setEnabled(id == 2);
    saveGhostSettings();
    updateGhostKeyLabel();
    emitGhostSettings();
}

void TitleBar::toggleGhostRecord()
{
    if (m_recording) {
        stopGhostRecord(false);
        return;
    }
    if (m_ghostOption != 2)
        onGhostOption(2);
    m_recording = true;
    m_recordSeen = false;
    m_recordCount = 0;
    m_recordMods = 0;
    m_recordVk = 0;
    if (m_ghostRecordButton)
        m_ghostRecordButton->setText(QStringLiteral("取消"));
    if (m_ghostKeyLabel)
        m_ghostKeyLabel->setText(QStringLiteral("请按键"));
    if (m_ghostRecordTimer)
        m_ghostRecordTimer->start();
}

void TitleBar::pollGhostRecord()
{
#ifndef Q_OS_WIN
    stopGhostRecord(false);
#else
    if (isKeyDown(VK_ESCAPE)) {
        stopGhostRecord(false);
        return;
    }
    const int modifiers = recordedModifierMask();
    const int virtualKey = findRecordedVirtualKey();
    const int count = modifierCount(modifiers, virtualKey);
    if (count > 0 && count >= m_recordCount) {
        m_recordSeen = true;
        m_recordCount = count;
        m_recordMods = modifiers;
        m_recordVk = virtualKey;
        if (m_ghostKeyLabel)
            m_ghostKeyLabel->setText(ghostHotkeyText(modifiers, virtualKey));
        return;
    }
    if (m_recordSeen && count == 0)
        stopGhostRecord(true);
#endif
}

void TitleBar::stopGhostRecord(bool commit)
{
    if (!m_recording)
        return;
    m_recording = false;
    if (m_ghostRecordTimer)
        m_ghostRecordTimer->stop();
    if (m_ghostRecordButton)
        m_ghostRecordButton->setText(QStringLiteral("录入"));
    const bool changed = commit && m_recordSeen && m_recordCount > 0;
    if (changed) {
        m_ghostModifiers = m_recordMods;
        m_ghostVirtualKey = m_recordVk;
        saveGhostSettings();
    }
    m_recordSeen = false;
    m_recordCount = 0;
    updateGhostKeyLabel();
    if (changed)
        emitGhostSettings();
}

void TitleBar::loadGhostSettings()
{
    QSettings settings;
    m_ghostOption = settings.value(QStringLiteral("ghost/mode"), 0).toInt();
    if (m_ghostOption < 0 || m_ghostOption > 2)
        m_ghostOption = 0;
    m_ghostModifiers = settings.value(QStringLiteral("ghost/modifiers"), 0).toInt();
    m_ghostVirtualKey = settings.value(QStringLiteral("ghost/virtualKey"), 0).toInt();
}

void TitleBar::saveGhostSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("ghost/mode"), m_ghostOption);
    settings.setValue(QStringLiteral("ghost/modifiers"), m_ghostModifiers);
    settings.setValue(QStringLiteral("ghost/virtualKey"), m_ghostVirtualKey);
}

void TitleBar::emitGhostSettings()
{
    emit ghostSettingsChanged(m_ghostOption != 0, m_ghostOption == 2, m_ghostModifiers, m_ghostVirtualKey);
    updateGhostTooltip();
    update();
}

void TitleBar::updateGhostKeyLabel()
{
    if (!m_ghostKeyLabel || m_recording)
        return;
    const QString text = ghostHotkeyText(m_ghostModifiers, m_ghostVirtualKey);
    m_ghostKeyLabel->setText(text.isEmpty() ? QStringLiteral("未设置") : text);
}

void TitleBar::updateGhostTooltip()
{
    if (m_hover == Ghost)
        updateTooltip(Ghost);
}
