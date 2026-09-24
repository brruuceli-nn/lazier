#include "MainWindow.h"

#include "Icons.h"
#include "TitleBar.h"

#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QColor>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QIcon>
#include <QtGui/QPixmap>
#include <QtGui/QShowEvent>
#include <QtGui/QWheelEvent>
#include <QtWebEngineWidgets/QWebEngineHistory>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
const qreal kGhostOpacity = 0.0;

#ifdef Q_OS_WIN
MainWindow *g_mainWindow = nullptr;
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;

LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_mainWindow
        && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        const KBDLLHOOKSTRUCT *info = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        if (info->vkCode == VK_TAB) {
            const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            if (ctrl || alt)
                QMetaObject::invokeMethod(g_mainWindow, "minimizeForSwitcher", Qt::QueuedConnection);
        } else if ((GetAsyncKeyState(VK_CONTROL) & 0x8000)
                   && g_mainWindow->shouldHandleWebZoomHotkey()) {
            if (info->vkCode == VK_OEM_PLUS || info->vkCode == VK_ADD) {
                QMetaObject::invokeMethod(g_mainWindow, "zoomWebByDelta",
                                          Qt::QueuedConnection, Q_ARG(int, 120));
                return 1;
            }
            if (info->vkCode == VK_OEM_MINUS || info->vkCode == VK_SUBTRACT) {
                QMetaObject::invokeMethod(g_mainWindow, "zoomWebByDelta",
                                          Qt::QueuedConnection, Q_ARG(int, -120));
                return 1;
            }
            if (info->vkCode == '0' || info->vkCode == VK_NUMPAD0) {
                QMetaObject::invokeMethod(g_mainWindow, "resetWebZoom", Qt::QueuedConnection);
                return 1;
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_mainWindow && wParam == WM_MOUSEWHEEL) {
        const MSLLHOOKSTRUCT *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && g_mainWindow->isCursorOverWeb()) {
            const short delta = GET_WHEEL_DELTA_WPARAM(info->mouseData);
            QMetaObject::invokeMethod(g_mainWindow, "zoomWebByDelta",
                                      Qt::QueuedConnection, Q_ARG(int, int(delta)));
            return 1;
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

bool isVirtualKeyDown(int virtualKey)
{
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool ghostHotkeyHeld(int modifiers, int virtualKey)
{
    if ((modifiers & Qt::ControlModifier) && !isVirtualKeyDown(VK_CONTROL))
        return false;
    if ((modifiers & Qt::ShiftModifier) && !isVirtualKeyDown(VK_SHIFT))
        return false;
    if ((modifiers & Qt::AltModifier) && !isVirtualKeyDown(VK_MENU))
        return false;
    if ((modifiers & Qt::MetaModifier) && !isVirtualKeyDown(VK_LWIN) && !isVirtualKeyDown(VK_RWIN))
        return false;
    if (virtualKey != 0 && !isVirtualKeyDown(virtualKey))
        return false;
    return modifiers != 0 || virtualKey != 0;
}
#endif
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("lazier"));
    setWindowIcon(lazierAppIcon());
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
    resize(400, 500);

    m_frame = new QWidget(this);
    m_frame->setObjectName(QStringLiteral("windowFrame"));
    m_frame->setStyleSheet(QStringLiteral("#windowFrame { background: #1A1A1A; }"));

    m_titleBar = new TitleBar(m_frame);
    connect(m_titleBar, &TitleBar::stayOnTopChanged, this, &MainWindow::setStayOnTop);
    connect(m_titleBar, &TitleBar::ghostSettingsChanged, this, &MainWindow::setGhostSettings);
    connect(m_titleBar, &TitleBar::displayOpacityChanged, this, &MainWindow::setDisplayOpacity);
    connect(m_titleBar, &TitleBar::addressBarVisibleChanged, this, &MainWindow::setAddressBarVisible);

    m_addressBar = new QWidget(m_frame);
    m_addressBar->setObjectName(QStringLiteral("addressBar"));
    m_addressBar->setFixedHeight(36);
    m_addressBar->setStyleSheet(QStringLiteral(
        "#addressBar { background: #F3F3F3; }"
        "QPushButton#navButton { background: transparent; border: none; color: #333333; font-size: 14px; }"
        "QPushButton#navButton:hover { background: #E5E5E5; }"
        "QPushButton#navButton:pressed { background: #D5D5D5; }"
        "QPushButton#navButton:disabled { color: #BBBBBB; }"));
    m_backButton = new QPushButton(QStringLiteral("<"), m_addressBar);
    m_backButton->setObjectName(QStringLiteral("navButton"));
    m_backButton->setFixedSize(26, 26);
    m_backButton->setToolTip(QStringLiteral("后退"));
    m_backButton->setEnabled(false);
    m_forwardButton = new QPushButton(QStringLiteral(">"), m_addressBar);
    m_forwardButton->setObjectName(QStringLiteral("navButton"));
    m_forwardButton->setFixedSize(26, 26);
    m_forwardButton->setToolTip(QStringLiteral("前进"));
    m_forwardButton->setEnabled(false);
    QLineEdit *urlEdit = new QLineEdit(m_addressBar);
    urlEdit->setPlaceholderText(QStringLiteral("输入网址后回车"));
    QPushButton *goButton = new QPushButton(QStringLiteral("前往"), m_addressBar);
    QHBoxLayout *addressLayout = new QHBoxLayout(m_addressBar);
    addressLayout->setContentsMargins(6, 4, 8, 4);
    addressLayout->setSpacing(2);
    addressLayout->addWidget(m_backButton);
    addressLayout->addWidget(m_forwardButton);
    addressLayout->addWidget(urlEdit, 1);
    addressLayout->addWidget(goButton);

    m_web = new QWebEngineView(m_frame);
    m_web->setUrl(QUrl(QStringLiteral("about:blank")));

    const auto navigate = [this, urlEdit]() {
        QString text = urlEdit->text().trimmed();
        if (text.isEmpty()) {
            m_web->setUrl(QUrl(QStringLiteral("about:blank")));
            return;
        }
        if (!text.contains(QStringLiteral("://")))
            text.prepend(QStringLiteral("https://"));
        m_web->setUrl(QUrl::fromUserInput(text));
    };
    connect(urlEdit, &QLineEdit::returnPressed, this, navigate);
    connect(goButton, &QPushButton::clicked, this, navigate);
    connect(m_backButton, &QPushButton::clicked, m_web, &QWebEngineView::back);
    connect(m_forwardButton, &QPushButton::clicked, m_web, &QWebEngineView::forward);
    connect(m_web, &QWebEngineView::urlChanged, this, [this, urlEdit](const QUrl &url) {
        urlEdit->setText(url.toString());
        updateHistoryButtons();
    });
    connect(m_web, &QWebEngineView::loadFinished, this, [this](bool) {
        updateHistoryButtons();
    });

    QVBoxLayout *layout = new QVBoxLayout(m_frame);
    layout->setSpacing(0);
    layout->addWidget(m_titleBar);
    layout->addWidget(m_addressBar);
    layout->addWidget(m_web, 1);
    setCentralWidget(m_frame);
    updateFrame();

    m_ghostTimer = new QTimer(this);
    m_ghostTimer->setInterval(50);
    connect(m_ghostTimer, &QTimer::timeout, this, &MainWindow::updateGhostVisual);
    setGhostSettings(m_titleBar->ghostMode(), m_titleBar->ghostEnhanced(),
                     m_titleBar->ghostModifiers(), m_titleBar->ghostVirtualKey());

    m_tray = new QSystemTrayIcon(lazierTrayIcon(), this);
    m_tray->setToolTip(QStringLiteral("lazier"));
    QMenu *trayMenu = new QMenu(this);
    trayMenu->addAction(QStringLiteral("显示"), this, &MainWindow::restoreFromTray);
    trayMenu->addAction(QStringLiteral("退出"), qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            restoreFromTray();
    });
    m_tray->show();

    qApp->installEventFilter(this);
    installSwitcherHook();
}

MainWindow::~MainWindow()
{
    removeSwitcherHook();
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" && !isMaximized()) {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_NCHITTEST) {
            const QPoint global(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
            const Qt::Edges edges = edgeAt(mapFromGlobal(global));
            if (edges) {
                if (edges == (Qt::LeftEdge | Qt::TopEdge))
                    *result = HTTOPLEFT;
                else if (edges == (Qt::RightEdge | Qt::TopEdge))
                    *result = HTTOPRIGHT;
                else if (edges == (Qt::LeftEdge | Qt::BottomEdge))
                    *result = HTBOTTOMLEFT;
                else if (edges == (Qt::RightEdge | Qt::BottomEdge))
                    *result = HTBOTTOMRIGHT;
                else if (edges & Qt::LeftEdge)
                    *result = HTLEFT;
                else if (edges & Qt::RightEdge)
                    *result = HTRIGHT;
                else if (edges & Qt::TopEdge)
                    *result = HTTOP;
                else
                    *result = HTBOTTOM;
                return true;
            }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel && m_web) {
        QWidget *widget = qobject_cast<QWidget *>(watched);
        if (widget && (widget == m_web || m_web->isAncestorOf(widget))) {
            QWheelEvent *wheel = static_cast<QWheelEvent *>(event);
            if (wheel->modifiers() & Qt::ControlModifier) {
                zoomWebByDelta(wheel->angleDelta().y());
                return true;
            }
        }
    }
    if (m_ghostMode
        && (event->type() == QEvent::Enter || event->type() == QEvent::Leave
            || event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)) {
        QWidget *widget = qobject_cast<QWidget *>(watched);
        if (widget && (widget == this || isAncestorOf(widget)))
            QTimer::singleShot(0, this, &MainWindow::updateGhostVisual);
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateFrame();
        if (m_titleBar)
            m_titleBar->update();
        updateGhostVisual();
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    hideFromTaskbar();
}

void MainWindow::setStayOnTop(bool on)
{
    Qt::WindowFlags flags = windowFlags();
    if (on)
        flags |= Qt::WindowStaysOnTopHint;
    else
        flags &= ~Qt::WindowStaysOnTopHint;
    const QRect geom = geometry();
    setWindowFlags(flags);
    setGeometry(geom);
    show();
    hideFromTaskbar();
    updateGhostVisual();
}

void MainWindow::setGhostSettings(bool enabled, bool enhanced, int modifiers, int virtualKey)
{
    m_ghostMode = enabled;
    m_ghostEnhanced = enhanced;
    m_ghostModifiers = modifiers;
    m_ghostVirtualKey = virtualKey;
    if (m_ghostMode)
        m_ghostTimer->start();
    else
        m_ghostTimer->stop();
    updateGhostVisual();
}

void MainWindow::setDisplayOpacity(int percent)
{
    m_displayOpacity = qBound(1, percent, 100);
    updateGhostVisual();
}

void MainWindow::updateGhostVisual()
{
    bool show = true;
    if (m_ghostMode) {
        show = isCursorInside();
        if (show && m_ghostEnhanced && (m_ghostModifiers != 0 || m_ghostVirtualKey != 0)) {
#ifdef Q_OS_WIN
            show = ghostHotkeyHeld(m_ghostModifiers, m_ghostVirtualKey);
#else
            show = true;
#endif
        }
    }
    if (!show) {
        setWindowOpacity(kGhostOpacity);
        return;
    }
    setWindowOpacity(m_displayOpacity / 100.0);
}

void MainWindow::minimizeForSwitcher()
{
    if (!isMinimized())
        showMinimized();
}

void MainWindow::restoreFromTray()
{
    setWindowState(windowState() & ~Qt::WindowMinimized);
    show();

    if (!QGuiApplication::screenAt(geometry().center())) {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect geom = geometry();
            if (geom.width() < 160 || geom.height() < 120)
                geom.setSize(QSize(400, 500));
            geom.moveCenter(screen->availableGeometry().center());
            setGeometry(geom);
        }
    }

    raise();
    activateWindow();
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        ShowWindow(hwnd, isMaximized() ? SW_SHOWMAXIMIZED : SW_SHOW);
        SetForegroundWindow(hwnd);
    }
#endif
    hideFromTaskbar();
    updateGhostVisual();
}

bool MainWindow::isCursorInside() const
{
    return frameGeometry().contains(QCursor::pos());
}

bool MainWindow::isCursorOverWeb() const
{
    if (!m_web || !isVisible() || isMinimized())
        return false;
#ifdef Q_OS_WIN
    POINT pt;
    if (!GetCursorPos(&pt))
        return false;
    HWND under = WindowFromPoint(pt);
    HWND self = reinterpret_cast<HWND>(winId());
    if (!under || !self || (under != self && !IsChild(self, under)))
        return false;
#endif
    return m_web->rect().contains(m_web->mapFromGlobal(QCursor::pos()));
}

bool MainWindow::shouldHandleWebZoomHotkey() const
{
    if (!m_web || !isVisible() || isMinimized())
        return false;
#ifdef Q_OS_WIN
    HWND fg = GetForegroundWindow();
    HWND self = reinterpret_cast<HWND>(winId());
    if (fg && self && (fg == self || IsChild(self, fg)))
        return true;
    return isCursorOverWeb();
#endif
    return isActiveWindow();
}

void MainWindow::zoomWebByDelta(int delta)
{
    if (!m_web || delta == 0)
        return;
    qreal factor = m_web->zoomFactor();
    if (delta > 0)
        factor *= 1.1;
    else
        factor /= 1.1;
    m_web->setZoomFactor(qBound(0.25, factor, 5.0));
}

void MainWindow::resetWebZoom()
{
    if (m_web)
        m_web->setZoomFactor(1.0);
}

void MainWindow::setAddressBarVisible(bool visible)
{
    if (m_addressBar)
        m_addressBar->setVisible(visible);
}

void MainWindow::updateHistoryButtons()
{
    QWebEngineHistory *history = m_web ? m_web->history() : nullptr;
    if (m_backButton)
        m_backButton->setEnabled(history && history->canGoBack());
    if (m_forwardButton)
        m_forwardButton->setEnabled(history && history->canGoForward());
}

void MainWindow::hideFromTaskbar()
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TOOLWINDOW;
    ex &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
#endif
}

void MainWindow::installSwitcherHook()
{
#ifdef Q_OS_WIN
    g_mainWindow = this;
    if (!g_keyboardHook)
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    if (!g_mouseHook)
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
#endif
}

void MainWindow::removeSwitcherHook()
{
#ifdef Q_OS_WIN
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    if (g_mainWindow == this)
        g_mainWindow = nullptr;
#endif
}

Qt::Edges MainWindow::edgeAt(const QPoint &pos) const
{
    Qt::Edges edges;
    if (pos.x() <= BorderWidth)
        edges |= Qt::LeftEdge;
    if (pos.x() >= width() - BorderWidth)
        edges |= Qt::RightEdge;
    if (pos.y() <= BorderWidth)
        edges |= Qt::TopEdge;
    if (pos.y() >= height() - BorderWidth)
        edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::updateFrame()
{
    if (!m_frame || !m_frame->layout())
        return;
    const int border = isMaximized() ? 0 : 1;
    m_frame->layout()->setContentsMargins(border, border, border, border);
}
