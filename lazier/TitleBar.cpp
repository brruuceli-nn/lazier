#include "TitleBar.h"

#include "Icons.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QWidget>

namespace {
const int kHeight = 30;
const int kButtonWidth = 28;
const int kIcon = 8;
const int kButtonRightPad = 6;
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
    return m_ghostMode;
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
    if (m_ghostMode) {
        m_ghostMode = false;
        emit ghostModeChanged(false);
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
    else if (button == Ghost)
        setToolTip(QStringLiteral("离开窗口后透明"));
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
        || (button == Ghost && m_ghostMode)
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
        m_ghostMode = !m_ghostMode;
        update();
        emit ghostModeChanged(m_ghostMode);
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
