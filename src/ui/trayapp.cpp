#include "trayapp.h"

#include "core/engine.h"
#include "core/languages.h"
#include "icons.h"
#include "platform/backend.h"
#include "settingswindow.h"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLocale>
#include <QScreen>
#include <QSettings>
#include <QStyleHints>
#include <QTimer>

#if defined(Q_OS_WIN)
#  include <windows.h>
#endif

namespace {

void trimMemory()
{
#if defined(Q_OS_WIN)
    // The settings window is gone: give its pages back to Windows.
    SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
#endif
}

} // namespace

TrayApp::TrayApp(QObject *parent)
    : QObject(parent)
    , m_backend(PlatformBackend::create(this))
    , m_engine(new LayoutEngine(m_backend, this))
{
    m_actSettings = m_menu.addAction(QString(), this, &TrayApp::showSettings);
    m_actPause = m_menu.addAction(QString());
    m_actPause->setCheckable(true);
    m_actPause->setChecked(m_engine->isPaused());
    m_menu.addSeparator();
    m_actQuit = m_menu.addAction(QString(), qApp, &QCoreApplication::quit);

    connect(m_actPause, &QAction::toggled, m_engine, &LayoutEngine::setPaused);
    connect(m_engine, &LayoutEngine::pausedChanged, this, [this](bool paused) {
        m_actPause->setChecked(paused);
        updateIcon();
        retranslate();
    });
    connect(m_engine, &LayoutEngine::rulesChanged, this, &TrayApp::retranslate);
    connect(&m_bridge, &BrowserBridge::domainReported, m_engine, &LayoutEngine::onBrowserDomain);

    // On macOS a click on a menu-bar item opens the menu; elsewhere left click opens settings.
    m_tray.setContextMenu(&m_menu);
    connect(&m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            toggleSettings();
    });

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Taskbar / panel colour can change with the system theme.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &TrayApp::updateIcon);
#endif

    setLanguage(QSettings().value(QStringLiteral("language"), QStringLiteral("auto")).toString());
    updateIcon();
}

TrayApp::~TrayApp()
{
    delete m_window;
    delete m_engine;  // restores the layout through the backend, so before the backend goes
    m_engine = nullptr;
}

void TrayApp::start(bool launchedAtStartup)
{
    const quint16 port = quint16(QSettings().value(QStringLiteral("browserPort"), BrowserBridge::DefaultPort).toUInt());
    m_bridge.listen(port);
    m_backend->start();
    m_tray.show();
    // First manual launch: show the window so the user sees where the app lives.
    if (!launchedAtStartup && (m_engine->rules().isEmpty() || !m_backend->problem().isEmpty()))
        QTimer::singleShot(300, this, &TrayApp::showSettings);
}

void TrayApp::setLanguage(const QString &code)
{
    // "auto" is stored as is, so the app keeps following the system language.
    QSettings().setValue(QStringLiteral("language"), code.isEmpty() ? QStringLiteral("auto") : code);
    m_language = Languages::resolve(code);

    QCoreApplication::removeTranslator(&m_translator);
    if (m_language != QLatin1String("en")
        && m_translator.load(QStringLiteral(":/i18n/keyboard_changer_%1.qm").arg(m_language)))
        QCoreApplication::installTranslator(&m_translator); // sends LanguageChange to widgets
    QGuiApplication::setLayoutDirection(Languages::isRtl(m_language) ? Qt::RightToLeft : Qt::LeftToRight);
    retranslate();
}

void TrayApp::retranslate()
{
    m_actSettings->setText(tr("Settings…"));
    m_actPause->setText(tr("Pause"));
    m_actQuit->setText(tr("Exit"));
    m_tray.setToolTip(m_engine->isPaused()
                          ? tr("Keyboard Changer — paused")
                          : tr("Keyboard Changer — active rules: %1").arg(m_engine->activeRuleCount()));
}

void TrayApp::updateIcon()
{
    m_tray.setIcon(ui::trayIcon(m_engine->isPaused()));
}

void TrayApp::toggleSettings()
{
    if (m_window && m_window->isVisible() && m_window->isActiveWindow())
        m_window->close();
    else
        showSettings();
}

void TrayApp::showSettings()
{
    m_backend->activateApp();
    if (!m_window) {
        m_window = new SettingsWindow(m_engine, &m_bridge);
        connect(m_window, &SettingsWindow::languageRequested, this, &TrayApp::setLanguage);
        connect(m_window, &QObject::destroyed, this, [] { QTimer::singleShot(0, trimMemory); });
        m_window->show();
        placeWindow();
        m_window->installEventFilter(this);
    } else {
        m_window->showNormal();
    }
    m_window->raise();
    m_window->activateWindow();
}

void TrayApp::placeWindow()
{
    const QRect trayRect = m_tray.geometry();
    QScreen *screen = trayRect.isValid() ? QGuiApplication::screenAt(trayRect.center()) : nullptr;
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen->availableGeometry();
    const QSize frame = m_window->frameGeometry().size();
    constexpr int margin = 12;

    int x = avail.right() - frame.width() - margin;
    int y = avail.bottom() - frame.height() - margin;
    if (trayRect.isValid()) {
        x = qBound(avail.left() + margin, trayRect.center().x() - frame.width() / 2,
                   avail.right() - frame.width() - margin);
        if (trayRect.center().y() < avail.center().y()) // panel / menu bar at the top
            y = avail.top() + margin;
    }
    m_window->move(x, y);
}

bool TrayApp::eventFilter(QObject *watched, QEvent *event)
{
    // The window grows when a panel opens: keep it on screen.
    if (watched == m_window && event->type() == QEvent::Resize) {
        QScreen *screen = m_window->screen();
        const QRect avail = screen ? screen->availableGeometry() : QRect();
        const QRect frame = m_window->frameGeometry();
        if (avail.isValid() && frame.bottom() > avail.bottom() - 12)
            m_window->move(frame.left(), qMax(avail.top(), avail.bottom() - 12 - frame.height()));
    }
    return QObject::eventFilter(watched, event);
}
