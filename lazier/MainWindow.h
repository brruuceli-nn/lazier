#pragma once

#include <QtCore/Qt>
#include <QtWidgets/QMainWindow>

class QComboBox;
class QLineEdit;
class QPushButton;
class QSystemTrayIcon;
class QTimer;
class QWebEngineView;
class TitleBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool isCursorOverWeb() const;
    bool shouldHandleWebZoomHotkey() const;

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void setStayOnTop(bool on);
    void setGhostSettings(bool enabled, bool enhanced, int modifiers, int virtualKey);
    void setDisplayOpacity(int percent);
    void updateGhostVisual();
    void minimizeForSwitcher();
    void restoreFromTray();
    void zoomWebByDelta(int delta);
    void resetWebZoom();
    void setAddressBarVisible(bool visible);
    void updateHistoryButtons();
    void openLocalText(const QString &pathText, int line = 1);
    void showBookmarkPopup();
    void saveBookmark(int index);
    void openBookmark(int index);

private:
    enum { BorderWidth = 6 };

    Qt::Edges edgeAt(const QPoint &pos) const;
    void updateFrame();
    bool isCursorInside() const;
    void hideFromTaskbar();
    void installSwitcherHook();
    void removeSwitcherHook();
    void loadBookmarks();
    void saveBookmarks();
    void refreshBookmarkPopup();
    QString bookmarkLabel(int index) const;
    void createBookmarkPopup();

    struct ReadingBookmark {
        int kind = 0;
        QString target;
        int position = 0;
    };

    TitleBar *m_titleBar = nullptr;
    QWebEngineView *m_web = nullptr;
    QWidget *m_frame = nullptr;
    QWidget *m_addressBar = nullptr;
    QComboBox *m_sourceCombo = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QPushButton *m_backButton = nullptr;
    QPushButton *m_forwardButton = nullptr;
    QWidget *m_bookmarkPopup = nullptr;
    QPushButton *m_bookmarkButtons[10] = {};
    ReadingBookmark m_bookmarks[10];
    QString m_currentLocalPath;
    int m_pendingLocalLine = 0;
    int m_pendingWebScroll = -1;
    QTimer *m_ghostTimer = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    bool m_ghostMode = false;
    bool m_ghostArmed = false;
    bool m_ghostWatchingEnter = false;
    bool m_ghostCursorWasInside = false;
    bool m_ghostEnhanced = false;
    int m_ghostModifiers = 0;
    int m_ghostVirtualKey = 0;
    int m_displayOpacity = 100;
};
