#include "linuxbackends.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

const QString kService = QStringLiteral("io.github.KeyboardSwitcher");
const QString kBridgePath = QStringLiteral("/Bridge");

// KWin script: reports every window activation to DBusFocusBridge::focus().
// Plasma 6 uses windowActivated/activeWindow, Plasma 5 clientActivated/activeClient.
const char kKWinScript[] = R"JS(
function report(w) {
    if (!w) return;
    callDBus("io.github.KeyboardSwitcher", "/Bridge", "io.github.KeyboardSwitcher", "focus",
             String(w.pid), String(w.resourceClass || ""), String(w.desktopFileName || w.resourceName || ""),
             String(w.caption || ""));
}
if (workspace.windowActivated) workspace.windowActivated.connect(report);
else workspace.clientActivated.connect(report);
report(workspace.activeWindow || workspace.activeClient);
)JS";

const QString kKWinScriptName = QStringLiteral("keyboardswitcher");

// GNOME Shell extension object (gnome-extension/ in the source tree).
const QString kShellService = QStringLiteral("org.gnome.Shell");
const QString kShellPath = QStringLiteral("/io/github/KeyboardSwitcher");
const QString kShellIface = QStringLiteral("io.github.KeyboardSwitcher.Shell");

} // namespace

// ---------------------------------------------------------------------------------------------
bool DBusFocusBridge::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    bus.registerService(kService);
    return bus.registerObject(kBridgePath, this, QDBusConnection::ExportScriptableSlots);
}

void DBusFocusBridge::focus(const QString &pid, const QString &windowClass, const QString &appId, const QString &title)
{
    FocusInfo info;
    info.appIds = linuxutil::idsForPid(pid.toLongLong());
    linuxutil::addIds(info.appIds, {appId, windowClass});
    info.title = title;
    emit focusReported(info);
}

// ---------------------------------------------------------------------------------------------
KdeBackend::KdeBackend(QObject *parent)
    : LinuxBackend(parent)
{
    connect(&m_bridge, &DBusFocusBridge::focusReported, this, &PlatformBackend::focusChanged);
}

KdeBackend::~KdeBackend()
{
    if (m_scriptLoaded) {
        QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                                 QStringLiteral("org.kde.kwin.Scripting"));
        scripting.call(QStringLiteral("unloadScript"), kKWinScriptName);
    }
}

void KdeBackend::start()
{
    if (!m_bridge.registerOnBus()) {
        m_problem = QCoreApplication::translate("Backend", "Cannot connect to the D-Bus session bus.");
        emit problemChanged();
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/kwin-focus.js");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(kKWinScript);
        f.close();
    }

    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                             QStringLiteral("org.kde.kwin.Scripting"));
    if (QDBusReply<bool>(scripting.call(QStringLiteral("isScriptLoaded"), kKWinScriptName)).value())
        scripting.call(QStringLiteral("unloadScript"), kKWinScriptName);
    const QDBusReply<int> id = scripting.call(QStringLiteral("loadScript"), path, kKWinScriptName);
    if (!id.isValid() || id.value() < 0) {
        m_problem = QCoreApplication::translate("Backend", "Could not load the KWin script: %1")
                        .arg(id.error().message());
        emit problemChanged();
        return;
    }
    scripting.call(QStringLiteral("start"));
    m_scriptLoaded = true;
}

namespace {
struct KdeLayout {
    QString shortName, displayName, longName;
};

QList<KdeLayout> kdeLayouts()
{
    QList<KdeLayout> result;
    QDBusInterface iface(QStringLiteral("org.kde.keyboard"), QStringLiteral("/Layouts"),
                         QStringLiteral("org.kde.KeyboardLayouts"));
    const QDBusMessage reply = iface.call(QStringLiteral("getLayoutsList"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return result;
    const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        KdeLayout l;
        arg.beginStructure();
        arg >> l.shortName >> l.displayName >> l.longName;
        arg.endStructure();
        result << l;
    }
    arg.endArray();
    return result;
}

int kdeCurrentIndex()
{
    QDBusInterface iface(QStringLiteral("org.kde.keyboard"), QStringLiteral("/Layouts"),
                         QStringLiteral("org.kde.KeyboardLayouts"));
    const QDBusReply<uint> reply = iface.call(QStringLiteral("getLayout"));
    return reply.isValid() ? int(reply.value()) : -1;
}
} // namespace

QList<LayoutInfo> KdeBackend::layouts() const
{
    QList<LayoutInfo> result;
    for (const KdeLayout &l : kdeLayouts()) {
        // KDE has no variant in the list, but the long name is unique ("English (Dvorak)").
        LayoutInfo info = linuxutil::xkbLayout(l.shortName, QString(), l.longName);
        info.id = l.longName;
        if (!l.displayName.isEmpty())
            info.code = l.displayName.toUpper();
        result << info;
    }
    return result;
}

QString KdeBackend::currentLayout(quint64) const
{
    return kdeLayouts().value(kdeCurrentIndex()).longName;
}

void KdeBackend::setLayout(const QString &id, quint64)
{
    const QList<KdeLayout> all = kdeLayouts();
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].longName == id) {
            QDBusInterface iface(QStringLiteral("org.kde.keyboard"), QStringLiteral("/Layouts"),
                                 QStringLiteral("org.kde.KeyboardLayouts"));
            iface.call(QStringLiteral("setLayout"), QVariant::fromValue(uint(i)));
            return;
        }
    }
}

// ---------------------------------------------------------------------------------------------
GnomeBackend::GnomeBackend(QObject *parent)
    : LinuxBackend(parent)
{
    connect(&m_bridge, &DBusFocusBridge::focusReported, this, [this](const FocusInfo &info) {
        if (!m_problem.isEmpty()) { // the extension talks to us, so it is enabled now
            m_problem.clear();
            m_retry.stop();
            emit problemChanged();
        }
        emit focusChanged(info);
    });
    m_retry.setInterval(15000);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (probeExtension()) {
            m_retry.stop();
            callJson(QStringLiteral("ReportFocus"));
        }
    });
}

void GnomeBackend::start()
{
    if (!m_bridge.registerOnBus()) {
        m_problem = QCoreApplication::translate("Backend", "Cannot connect to the D-Bus session bus.");
        emit problemChanged();
        return;
    }
    if (probeExtension())
        callJson(QStringLiteral("ReportFocus")); // extension calls focus() with the current window
    else
        m_retry.start();
}

bool GnomeBackend::probeExtension()
{
    QDBusInterface shell(kShellService, kShellPath, kShellIface);
    const QDBusReply<QString> reply = shell.call(QStringLiteral("ListInputSources"));
    const QString problem = reply.isValid()
        ? QString()
        : QCoreApplication::translate("Backend",
                                      "Enable the \"Keyboard Switcher\" GNOME Shell extension "
                                      "(see gnome-extension/ in the project), then log out and back in.");
    if (problem != m_problem) {
        m_problem = problem;
        emit problemChanged();
    }
    return reply.isValid();
}

QJsonValue GnomeBackend::callJson(const QString &method) const
{
    QDBusInterface shell(kShellService, kShellPath, kShellIface);
    const QDBusReply<QString> reply = shell.call(method);
    if (!reply.isValid())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

QList<LayoutInfo> GnomeBackend::layouts() const
{
    QList<LayoutInfo> result;
    const QJsonArray sources = callJson(QStringLiteral("ListInputSources")).toArray();
    for (const QJsonValue &v : sources) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QLatin1String("id")).toString(); // "us", "ru", "us+dvorak"
        LayoutInfo info = linuxutil::xkbLayout(id.section(QLatin1Char('+'), 0, 0), QString(),
                                               o.value(QLatin1String("name")).toString());
        info.id = id;
        const QString shortName = o.value(QLatin1String("short")).toString();
        if (!shortName.isEmpty())
            info.code = shortName.toUpper();
        result << info;
    }
    return result;
}

QString GnomeBackend::currentLayout(quint64) const
{
    return callJson(QStringLiteral("CurrentInputSource")).toObject().value(QLatin1String("id")).toString();
}

void GnomeBackend::setLayout(const QString &id, quint64)
{
    QDBusInterface shell(kShellService, kShellPath, kShellIface);
    shell.call(QDBus::NoBlock, QStringLiteral("SetInputSource"), id);
}

QStringList GnomeBackend::runningApps() const
{
    QStringList apps;
    const QJsonArray windows = callJson(QStringLiteral("ListWindows")).toArray();
    for (const QJsonValue &v : windows) {
        const QJsonObject o = v.toObject();
        QStringList ids = linuxutil::idsForPid(o.value(QLatin1String("pid")).toVariant().toLongLong());
        linuxutil::addIds(ids, {o.value(QLatin1String("appId")).toString(), o.value(QLatin1String("wmClass")).toString()});
        if (!ids.isEmpty())
            apps << ids.first();
    }
    apps.removeDuplicates();
    return apps;
}
