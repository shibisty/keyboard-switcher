#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QStyle>

#include "core/types.h"
#include "ui/trayapp.h"
#include "version.h"

namespace {

QString instanceName()
{
    // Per user: several users can run their own copy on one machine.
    const QString user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
    return QStringLiteral("KeyboardChanger-") + user;
}

// Asks a running copy to show its settings. True if one answered.
bool pingRunningInstance()
{
    QLocalSocket socket;
    socket.connectToServer(instanceName());
    if (!socket.waitForConnected(300))
        return false;
    socket.write("show\n");
    socket.waitForBytesWritten(300);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 5 (legacy builds for Windows 7/8 and 32-bit systems): enable what Qt 6 does by default.
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#  if defined(Q_OS_WIN)
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "windows:darkmode=2"); // dark palette on Windows 10 dark mode
#  endif
#endif
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(QStringLiteral("KeyboardChanger"));
    QCoreApplication::setApplicationName(QStringLiteral("KeyboardChanger"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KC_VERSION_STR));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.KeyboardChanger"));
    // Windows: %APPDATA%\KeyboardChanger\KeyboardChanger.ini; macOS / Linux: ~/.config/KeyboardChanger/
    QSettings::setDefaultFormat(QSettings::IniFormat);
    qRegisterMetaType<FocusInfo>();

    if (pingRunningInstance())
        return 0;
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(instanceName())) {
        QLocalServer::removeServer(instanceName()); // stale socket after a crash (Unix)
        server.listen(instanceName());
    }

#if defined(Q_OS_WIN)
    // Light/dark follows Windows: "windows11" style on Win 11, Fusion elsewhere
    // (the classic "windowsvista" style cannot draw a dark palette).
#  if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    const QString styleName = QApplication::style()->name();
#  else
    const QString styleName = QApplication::style()->objectName();
#  endif
    if (styleName.compare(QLatin1String("windows11"), Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStringLiteral("fusion"));
#endif

    const bool atStartup = QCoreApplication::arguments().contains(QLatin1String("--autostart"));

    TrayApp tray;
    QObject::connect(&server, &QLocalServer::newConnection, &tray, [&server, &tray] {
        while (QLocalSocket *s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            tray.showSettings();
        }
    });
    tray.start(atStartup);
    return QApplication::exec();
}
