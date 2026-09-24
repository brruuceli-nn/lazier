#include "MainWindow.h"

#include "Icons.h"
#include "TitleBar.h"

#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QTextCodec>
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
#include <QtWebEngineWidgets/QWebEnginePage>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QToolTip>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
const qreal kGhostOpacity = 0.0;

QString pageHtml(const QString &body)
{
    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>body{margin:16px;background:#ffffff;color:#222222;"
        "font-family:'Microsoft YaHei',sans-serif;font-size:16px;line-height:1.7;"
        "white-space:pre-wrap;word-wrap:break-word;}</style></head><body>%1</body></html>")
        .arg(body.toHtmlEscaped());
}

QString localTextHtml(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QString body;
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        body += QStringLiteral("<div id=\"ln-%1\">%2</div>")
                    .arg(i + 1)
                    .arg(line.isEmpty() ? QStringLiteral("&nbsp;") : line.toHtmlEscaped());
    }
    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>body{margin:16px;background:#ffffff;color:#222222;"
        "font-family:'Microsoft YaHei',sans-serif;font-size:16px;line-height:1.7;}"
        "#reader div{white-space:pre-wrap;word-wrap:break-word;min-height:1.7em;}</style>"
        "</head><body><div id=\"reader\">%1</div></body></html>")
        .arg(body);
}

QString decodeTextFile(const QByteArray &bytes)
{
    if (QTextCodec *bom = QTextCodec::codecForUtfText(bytes, nullptr))
        return bom->toUnicode(bytes);
    QTextCodec::ConverterState state;
    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    const QString utf = utf8->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars == 0)
        return utf;
    if (QTextCodec *gbk = QTextCodec::codecForName("GB18030"))
        return gbk->toUnicode(bytes);
    return QString::fromLocal8Bit(bytes);
}

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
    loadBookmarks();

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
        "QPushButton#navButton:disabled { color: #BBBBBB; }"
        "QComboBox#sourceCombo { background: #FFFFFF; border: 1px solid #D0D0D0; padding: 0 2px; min-height: 24px; }"
        "QLineEdit { min-height: 24px; }"
        "QPushButton#goButton, QPushButton#bookmarkButton { padding: 0 4px; min-height: 24px; }"));
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
    m_sourceCombo = new QComboBox(m_addressBar);
    m_sourceCombo->setObjectName(QStringLiteral("sourceCombo"));
    m_sourceCombo->addItem(QStringLiteral("网络"));
    m_sourceCombo->addItem(QStringLiteral("本地"));
    m_sourceCombo->setFixedSize(52, 26);
    m_sourceCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_sourceCombo->setToolTip(QStringLiteral("网络打开网址，本地打开 txt 文件"));
    m_urlEdit = new QLineEdit(m_addressBar);
    m_urlEdit->setPlaceholderText(QStringLiteral("输入网址后回车"));
    m_urlEdit->setFixedHeight(26);
    m_urlEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QPushButton *goButton = new QPushButton(QStringLiteral("前往"), m_addressBar);
    goButton->setObjectName(QStringLiteral("goButton"));
    goButton->setFixedSize(40, 26);
    QPushButton *bookmarkButton = new QPushButton(QStringLiteral("书签"), m_addressBar);
    bookmarkButton->setObjectName(QStringLiteral("bookmarkButton"));
    bookmarkButton->setFixedSize(40, 26);
    bookmarkButton->setToolTip(QStringLiteral("点开后，点某一条右侧的「记下」保存当前位置"));
    QHBoxLayout *addressLayout = new QHBoxLayout(m_addressBar);
    addressLayout->setContentsMargins(6, 4, 8, 4);
    addressLayout->setSpacing(2);
    addressLayout->addWidget(m_backButton);
    addressLayout->addWidget(m_forwardButton);
    addressLayout->addWidget(m_sourceCombo);
    addressLayout->addWidget(m_urlEdit, 1);
    addressLayout->addWidget(goButton);
    addressLayout->addWidget(bookmarkButton);

    m_web = new QWebEngineView(m_frame);
    m_web->setUrl(QUrl(QStringLiteral("about:blank")));

    const auto navigate = [this]() {
        m_pendingLocalLine = 0;
        m_pendingWebScroll = -1;
        if (m_sourceCombo->currentIndex() == 1) {
            openLocalText(m_urlEdit->text());
            return;
        }
        m_currentLocalPath.clear();
        QString text = m_urlEdit->text().trimmed();
        if (text.isEmpty()) {
            m_web->setUrl(QUrl(QStringLiteral("about:blank")));
            return;
        }
        if (!text.contains(QStringLiteral("://")))
            text.prepend(QStringLiteral("https://"));
        m_web->setUrl(QUrl::fromUserInput(text));
    };
    connect(m_urlEdit, &QLineEdit::returnPressed, this, navigate);
    connect(goButton, &QPushButton::clicked, this, navigate);
    connect(bookmarkButton, &QPushButton::clicked, this, &MainWindow::showBookmarkPopup);
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_urlEdit->setPlaceholderText(index == 1
            ? QStringLiteral("输入本地 txt 路径后回车")
            : QStringLiteral("输入网址后回车"));
    });
    connect(m_backButton, &QPushButton::clicked, m_web, &QWebEngineView::back);
    connect(m_forwardButton, &QPushButton::clicked, m_web, &QWebEngineView::forward);
    connect(m_web, &QWebEngineView::urlChanged, this, [this](const QUrl &url) {
        if (m_sourceCombo->currentIndex() == 0)
            m_urlEdit->setText(url.toString());
        updateHistoryButtons();
    });
    connect(m_web, &QWebEngineView::loadFinished, this, [this](bool ok) {
        updateHistoryButtons();
        if (!ok || !m_web->page())
            return;
        if (m_pendingLocalLine > 1) {
            const int line = m_pendingLocalLine;
            const QString probe = QStringLiteral(
                "(function(){var el=document.getElementById('ln-%1');"
                "if(el){el.scrollIntoView({block:'start'});return 1;}"
                "if(document.getElementById('ln-1'))return 0;return -1;})()").arg(line);
            m_web->page()->runJavaScript(probe, [this, line](const QVariant &result) {
                const int state = result.toInt();
                if (state < 0 || m_pendingLocalLine != line)
                    return;
                m_pendingLocalLine = 0;
            });
        }
        if (m_pendingWebScroll >= 0) {
            const int y = m_pendingWebScroll;
            m_pendingWebScroll = -1;
            const QString js = QStringLiteral("window.scrollTo(0,%1);").arg(y);
            m_web->page()->runJavaScript(js);
            QTimer::singleShot(500, this, [this, js]() {
                if (m_web && m_web->page())
                    m_web->page()->runJavaScript(js);
            });
        }
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
    const bool wasEnabled = m_ghostMode;
    m_ghostMode = enabled;
    m_ghostEnhanced = enhanced;
    m_ghostModifiers = modifiers;
    m_ghostVirtualKey = virtualKey;
    if (!m_ghostMode) {
        m_ghostArmed = false;
        m_ghostWatchingEnter = false;
        m_ghostTimer->stop();
    } else {
        if (!wasEnabled && isVisible())
            m_ghostArmed = true;
        m_ghostTimer->start();
    }
    updateGhostVisual();
}

void MainWindow::setDisplayOpacity(int percent)
{
    m_displayOpacity = qBound(1, percent, 100);
    updateGhostVisual();
}

void MainWindow::updateGhostVisual()
{
    const bool overBookmark = m_bookmarkPopup && m_bookmarkPopup->isVisible()
        && m_bookmarkPopup->geometry().contains(QCursor::pos());
    const bool inside = isCursorInside() || overBookmark;
    const bool hotkeyMode = m_ghostEnhanced && (m_ghostModifiers != 0 || m_ghostVirtualKey != 0);
    if (m_ghostMode && !m_ghostArmed && isVisible()) {
        if (!m_ghostWatchingEnter) {
            m_ghostCursorWasInside = inside;
            m_ghostWatchingEnter = true;
        } else if (hotkeyMode) {
#ifdef Q_OS_WIN
            if (inside && ghostHotkeyHeld(m_ghostModifiers, m_ghostVirtualKey))
                m_ghostArmed = true;
#endif
        } else if (inside && !m_ghostCursorWasInside) {
            m_ghostArmed = true;
        }
    }
    m_ghostCursorWasInside = inside;

    bool show = true;
    if (m_ghostMode && m_ghostArmed) {
        show = inside;
        if (show && hotkeyMode) {
#ifdef Q_OS_WIN
            show = ghostHotkeyHeld(m_ghostModifiers, m_ghostVirtualKey);
#else
            show = true;
#endif
        }
    }
    if (!show) {
        if (m_bookmarkPopup && m_bookmarkPopup->isVisible())
            m_bookmarkPopup->hide();
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

void MainWindow::openLocalText(const QString &pathText, int line)
{
    m_pendingWebScroll = -1;
    QString path = pathText.trimmed();
    if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
        path = path.mid(1, path.size() - 2).trimmed();
    if (path.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
        path = QUrl(path).toLocalFile();
    if (path.isEmpty()) {
        m_currentLocalPath.clear();
        m_pendingLocalLine = 0;
        m_web->setHtml(pageHtml(QStringLiteral("请输入 txt 文件路径")));
        return;
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        m_currentLocalPath.clear();
        m_pendingLocalLine = 0;
        m_web->setHtml(pageHtml(QStringLiteral("找不到文件：%1").arg(path)));
        return;
    }
    if (info.suffix().compare(QLatin1String("txt"), Qt::CaseInsensitive) != 0) {
        m_currentLocalPath.clear();
        m_pendingLocalLine = 0;
        m_web->setHtml(pageHtml(QStringLiteral("本地模式只打开 txt 文件")));
        return;
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        m_currentLocalPath.clear();
        m_pendingLocalLine = 0;
        m_web->setHtml(pageHtml(QStringLiteral("无法打开文件：%1").arg(info.absoluteFilePath())));
        return;
    }
    m_currentLocalPath = info.absoluteFilePath();
    m_pendingLocalLine = line;
    m_web->setHtml(localTextHtml(decodeTextFile(file.readAll())),
                   QUrl::fromLocalFile(info.absolutePath() + QLatin1Char('/')));
}

void MainWindow::loadBookmarks()
{
    QSettings settings;
    for (int i = 0; i < 10; ++i) {
        const QString key = QStringLiteral("bookmarks/%1/").arg(i);
        const int kind = settings.value(key + QStringLiteral("kind"), 0).toInt();
        m_bookmarks[i].kind = (kind == 1 || kind == 2) ? kind : 0;
        m_bookmarks[i].target = settings.value(key + QStringLiteral("target")).toString();
        m_bookmarks[i].position = settings.value(key + QStringLiteral("position"), 0).toInt();
        if (m_bookmarks[i].target.isEmpty())
            m_bookmarks[i].kind = 0;
    }
}

void MainWindow::saveBookmarks()
{
    QSettings settings;
    for (int i = 0; i < 10; ++i) {
        const QString key = QStringLiteral("bookmarks/%1/").arg(i);
        settings.setValue(key + QStringLiteral("kind"), m_bookmarks[i].kind);
        settings.setValue(key + QStringLiteral("target"), m_bookmarks[i].target);
        settings.setValue(key + QStringLiteral("position"), m_bookmarks[i].position);
    }
}

QString MainWindow::bookmarkLabel(int index) const
{
    const ReadingBookmark &mark = m_bookmarks[index];
    if (mark.kind == 2) {
        return QStringLiteral("%1  本地  %2  第%3行")
            .arg(index + 1)
            .arg(QFileInfo(mark.target).fileName())
            .arg(qMax(1, mark.position));
    }
    if (mark.kind == 1) {
        const QUrl url(mark.target);
        QString name = url.host();
        if (name.isEmpty())
            name = mark.target;
        return QStringLiteral("%1  网络  %2").arg(index + 1).arg(name);
    }
    return QStringLiteral("%1  空").arg(index + 1);
}

void MainWindow::refreshBookmarkPopup()
{
    for (int i = 0; i < 10; ++i) {
        if (!m_bookmarkButtons[i])
            continue;
        m_bookmarkButtons[i]->setText(bookmarkLabel(i));
        m_bookmarkButtons[i]->setEnabled(m_bookmarks[i].kind != 0);
        QString tip = m_bookmarks[i].target;
        if (m_bookmarks[i].kind == 1)
            tip += QStringLiteral("\n滚动位置 %1").arg(qMax(0, m_bookmarks[i].position));
        else if (m_bookmarks[i].kind == 2)
            tip += QStringLiteral("\n第 %1 行").arg(qMax(1, m_bookmarks[i].position));
        m_bookmarkButtons[i]->setToolTip(tip);
    }
}

void MainWindow::createBookmarkPopup()
{
    m_bookmarkPopup = new QWidget(window(), Qt::Popup | Qt::FramelessWindowHint);
    m_bookmarkPopup->setFixedSize(340, 342);
    m_bookmarkPopup->setStyleSheet(QStringLiteral(
        "QWidget { background: #FFFFFF; border: 1px solid #D0D0D0; }"
        "QLabel { border: none; color: #666666; }"
        "QPushButton { border: none; color: #222222; background: transparent; text-align: left; padding: 2px 8px; }"
        "QPushButton:disabled { color: #AAAAAA; }"
        "QPushButton#markSave { background: #F3F3F3; text-align: center; padding: 0 4px; min-height: 26px; }"));

    QVBoxLayout *layout = new QVBoxLayout(m_bookmarkPopup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(2);
    QLabel *hint = new QLabel(QStringLiteral("点右侧「记下」保存当前页，点左侧打开"), m_bookmarkPopup);
    layout->addWidget(hint);
    for (int i = 0; i < 10; ++i) {
        QWidget *row = new QWidget(m_bookmarkPopup);
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);
        QPushButton *openButton = new QPushButton(row);
        openButton->setFixedHeight(26);
        m_bookmarkButtons[i] = openButton;
        QPushButton *saveButton = new QPushButton(QStringLiteral("记下"), row);
        saveButton->setObjectName(QStringLiteral("markSave"));
        saveButton->setFixedSize(44, 26);
        saveButton->setToolTip(QStringLiteral("把当前阅读位置记到这一条"));
        rowLayout->addWidget(openButton, 1);
        rowLayout->addWidget(saveButton);
        layout->addWidget(row);
        connect(openButton, &QPushButton::clicked, this, [this, i]() { openBookmark(i); });
        connect(saveButton, &QPushButton::clicked, this, [this, i]() { saveBookmark(i); });
    }
}

void MainWindow::showBookmarkPopup()
{
    if (!m_bookmarkPopup)
        createBookmarkPopup();
    refreshBookmarkPopup();
    QPoint pos = m_addressBar->mapToGlobal(QPoint(m_addressBar->width() - m_bookmarkPopup->width(), m_addressBar->height()));
    if (pos.x() < 0)
        pos.setX(0);
    m_bookmarkPopup->move(pos);
    m_bookmarkPopup->show();
}

void MainWindow::saveBookmark(int index)
{
    if (index < 0 || index >= 10 || !m_web || !m_web->page())
        return;
    const bool localMode = m_sourceCombo && m_sourceCombo->currentIndex() == 1;
    if (localMode) {
        if (m_currentLocalPath.isEmpty()) {
            QToolTip::showText(QCursor::pos(), QStringLiteral("请先打开 txt"));
            return;
        }
        const QString path = m_currentLocalPath;
        m_web->page()->runJavaScript(QStringLiteral(
            "(function(){var y=window.pageYOffset||0;"
            "var nodes=document.querySelectorAll('[id^=ln-]');"
            "if(!nodes.length)return 0;"
            "var line=1;for(var i=0;i<nodes.length;i++){"
            "if(nodes[i].offsetTop<=y+4)line=i+1;else break;}return line;})()"),
            [this, index, path](const QVariant &result) {
                const int line = qMax(1, result.toInt());
                m_bookmarks[index].kind = 2;
                m_bookmarks[index].target = path;
                m_bookmarks[index].position = line;
                saveBookmarks();
                refreshBookmarkPopup();
            });
        return;
    }

    const QUrl url = m_web->url();
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")) {
        QToolTip::showText(QCursor::pos(), QStringLiteral("请先打开网页"));
        return;
    }
    const QString target = url.toString();
    m_web->page()->runJavaScript(QStringLiteral("(window.pageYOffset||window.scrollY||0)"),
        [this, index, target](const QVariant &result) {
            m_bookmarks[index].kind = 1;
            m_bookmarks[index].target = target;
            m_bookmarks[index].position = qMax(0, result.toInt());
            saveBookmarks();
            refreshBookmarkPopup();
        });
}

void MainWindow::openBookmark(int index)
{
    if (index < 0 || index >= 10)
        return;
    const ReadingBookmark mark = m_bookmarks[index];
    if (mark.kind == 0 || mark.target.isEmpty())
        return;
    if (m_bookmarkPopup)
        m_bookmarkPopup->hide();
    if (mark.kind == 2) {
        if (m_sourceCombo)
            m_sourceCombo->setCurrentIndex(1);
        if (m_urlEdit)
            m_urlEdit->setText(mark.target);
        openLocalText(mark.target, qMax(1, mark.position));
        return;
    }
    m_currentLocalPath.clear();
    m_pendingLocalLine = 0;
    m_pendingWebScroll = qMax(0, mark.position);
    if (m_sourceCombo)
        m_sourceCombo->setCurrentIndex(0);
    if (m_urlEdit)
        m_urlEdit->setText(mark.target);
    const QUrl url(mark.target);
    if (m_web->url() == url)
        m_web->reload();
    else
        m_web->setUrl(url);
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
