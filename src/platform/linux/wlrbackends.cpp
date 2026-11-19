#include "linuxbackends.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtEndian>

// ============================== Sway: i3 IPC ===============================================
namespace {

constexpr quint32 kRunCommand = 0;
constexpr quint32 kSubscribe = 2;
constexpr quint32 kGetTree = 4;
constexpr quint32 kGetInputs = 100;
constexpr quint32 kEventWindow = 0x80000003;
const QByteArray kMagic = QByteArrayLiteral("i3-ipc");
constexpr int kHeader = 14; // magic(6) + length(4) + type(4), native byte order

QByteArray i3Pack(quint32 type, const QByteArray &payload)
{
    QByteArray msg = kMagic;
    const quint32 len = quint32(payload.size());
    msg.append(reinterpret_cast<const char *>(&len), 4);
    msg.append(reinterpret_cast<const char *>(&type), 4);
    return msg + payload;
}

// Extracts complete frames from `buf`.
QList<QPair<quint32, QByteArray>> i3Unpack(QByteArray &buf)
{
    QList<QPair<quint32, QByteArray>> frames;
    while (buf.size() >= kHeader && buf.startsWith(kMagic)) {
        quint32 len = 0, type = 0;
        memcpy(&len, buf.constData() + 6, 4);
        memcpy(&type, buf.constData() + 10, 4);
        if (buf.size() < kHeader + int(len))
            break;
        frames.append({type, buf.mid(kHeader, int(len))});
        buf.remove(0, kHeader + int(len));
    }
    return frames;
}

void collectSwayApps(const QJsonObject &node, QSet<QString> &apps)
{
    QStringList ids = linuxutil::idsForPid(node.value(QLatin1String("pid")).toVariant().toLongLong());
    linuxutil::addIds(ids, {node.value(QLatin1String("app_id")).toString(),
                            node.value(QLatin1String("window_properties")).toObject().value(QLatin1String("class")).toString()});
    if (node.contains(QLatin1String("pid")) && !ids.isEmpty())
        apps.insert(ids.first());
    for (const char *key : {"nodes", "floating_nodes"})
        for (const QJsonValue &child : node.value(QLatin1String(key)).toArray())
            collectSwayApps(child.toObject(), apps);
}

QJsonObject findFocused(const QJsonObject &node)
{
    if (node.value(QLatin1String("focused")).toBool() && node.contains(QLatin1String("pid")))
        return node;
    for (const char *key : {"nodes", "floating_nodes"}) {
        for (const QJsonValue &child : node.value(QLatin1String(key)).toArray()) {
            const QJsonObject found = findFocused(child.toObject());
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}

} // namespace

SwayBackend::SwayBackend(QObject *parent)
    : LinuxBackend(parent)
    , m_path(qEnvironmentVariable("SWAYSOCK"))
{
    connect(&m_events, &QLocalSocket::readyRead, this, &SwayBackend::onEvents);
}

QString SwayBackend::problem() const
{
    if (m_path.isEmpty() || m_events.state() != QLocalSocket::ConnectedState)
        return QCoreApplication::translate("Backend", "Cannot connect to Sway IPC ($SWAYSOCK).");
    return {};
}

void SwayBackend::start()
{
    m_events.connectToServer(m_path);
    if (!m_events.waitForConnected(1000)) {
        emit problemChanged();
        return;
    }
    m_events.write(i3Pack(kSubscribe, R"(["window"])"));
    m_events.flush();
    reportNode(findFocused(request(kGetTree).toObject()));
}

QJsonValue SwayBackend::request(quint32 type, const QByteArray &payload) const
{
    QLocalSocket s;
    s.connectToServer(m_path);
    if (!s.waitForConnected(500))
        return {};
    s.write(i3Pack(type, payload));
    s.flush();
    QByteArray buf;
    while (s.waitForReadyRead(1000)) {
        buf += s.readAll();
        const auto frames = i3Unpack(buf);
        if (!frames.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(frames.first().second);
            return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
        }
    }
    return {};
}

void SwayBackend::onEvents()
{
    m_buffer += m_events.readAll();
    for (const auto &frame : i3Unpack(m_buffer)) {
        if (frame.first != kEventWindow)
            continue;
        const QJsonObject ev = QJsonDocument::fromJson(frame.second).object();
        if (ev.value(QLatin1String("change")).toString() == QLatin1String("focus"))
            reportNode(ev.value(QLatin1String("container")).toObject());
    }
}

void SwayBackend::reportNode(const QJsonObject &node)
{
    if (node.isEmpty())
        return;
    FocusInfo info;
    info.appIds = linuxutil::idsForPid(node.value(QLatin1String("pid")).toVariant().toLongLong());
    linuxutil::addIds(info.appIds,
                      {node.value(QLatin1String("app_id")).toString(),
                       node.value(QLatin1String("window_properties")).toObject().value(QLatin1String("class")).toString()});
    info.title = node.value(QLatin1String("name")).toString();
    emit focusChanged(info);
}

QJsonObject SwayBackend::keyboard() const
{
    for (const QJsonValue &v : request(kGetInputs).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("type")).toString() == QLatin1String("keyboard")
            && !o.value(QLatin1String("xkb_layout_names")).toArray().isEmpty())
            return o;
    }
    return {};
}

QList<LayoutInfo> SwayBackend::layouts() const
{
    QList<LayoutInfo> result;
    for (const QJsonValue &v : keyboard().value(QLatin1String("xkb_layout_names")).toArray())
        result << linuxutil::namedLayout(v.toString());
    return result;
}

QString SwayBackend::currentLayout(quint64) const
{
    return keyboard().value(QLatin1String("xkb_active_layout_name")).toString();
}

void SwayBackend::setLayout(const QString &id, quint64)
{
    const QJsonArray names = keyboard().value(QLatin1String("xkb_layout_names")).toArray();
    for (int i = 0; i < names.size(); ++i) {
        if (names[i].toString() == id) {
            request(kRunCommand, QStringLiteral("input type:keyboard xkb_switch_layout %1").arg(i).toUtf8());
            return;
        }
    }
}

QStringList SwayBackend::runningApps() const
{
    QSet<QString> apps;
    collectSwayApps(request(kGetTree).toObject(), apps);
    return QStringList(apps.begin(), apps.end());
}

// ============================== Hyprland ===================================================
HyprlandBackend::HyprlandBackend(QObject *parent)
    : LinuxBackend(parent)
{
    const QString sig = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    m_dir = runtime + QStringLiteral("/hypr/") + sig;            // Hyprland >= 0.40
    if (!QDir(m_dir).exists())
        m_dir = QStringLiteral("/tmp/hypr/") + sig;              // older versions
    connect(&m_events, &QLocalSocket::readyRead, this, &HyprlandBackend::onEvents);
}

QString HyprlandBackend::problem() const
{
    if (m_events.state() != QLocalSocket::ConnectedState)
        return QCoreApplication::translate("Backend", "Cannot connect to the Hyprland event socket.");
    return {};
}

void HyprlandBackend::start()
{
    m_events.connectToServer(m_dir + QStringLiteral("/.socket2.sock"));
    if (!m_events.waitForConnected(1000)) {
        emit problemChanged();
        return;
    }
    reportActive();
}

QByteArray HyprlandBackend::command(const QByteArray &cmd) const
{
    QLocalSocket s;
    s.connectToServer(m_dir + QStringLiteral("/.socket.sock"));
    if (!s.waitForConnected(500))
        return {};
    s.write(cmd);
    s.flush();
    QByteArray out;
    while (s.waitForReadyRead(1000))
        out += s.readAll();   // Hyprland closes the connection after the reply
    return out;
}

QJsonValue HyprlandBackend::json(const QByteArray &cmd) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(command(cmd));
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

void HyprlandBackend::onEvents()
{
    m_buffer += m_events.readAll();
    bool activeChanged = false;
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line.startsWith("activewindowv2>>") || line.startsWith("activewindow>>"))
            activeChanged = true;
    }
    if (activeChanged)
        reportActive();
}

void HyprlandBackend::reportActive()
{
    const QJsonObject w = json("j/activewindow").toObject();
    if (w.isEmpty())
        return;
    FocusInfo info;
    info.appIds = linuxutil::idsForPid(w.value(QLatin1String("pid")).toVariant().toLongLong());
    linuxutil::addIds(info.appIds, {w.value(QLatin1String("class")).toString(),
                                    w.value(QLatin1String("initialClass")).toString()});
    info.title = w.value(QLatin1String("title")).toString();
    emit focusChanged(info);
}

QJsonObject HyprlandBackend::keyboard() const
{
    const QJsonArray keyboards = json("j/devices").toObject().value(QLatin1String("keyboards")).toArray();
    for (const QJsonValue &v : keyboards)
        if (v.toObject().value(QLatin1String("main")).toBool())
            return v.toObject();
    return keyboards.isEmpty() ? QJsonObject() : keyboards.first().toObject();
}

QList<LayoutInfo> HyprlandBackend::layouts() const
{
    QList<LayoutInfo> result;
    const QJsonObject kb = keyboard();
    const QStringList layouts = kb.value(QLatin1String("layout")).toString().split(QLatin1Char(','));
    const QStringList variants = kb.value(QLatin1String("variant")).toString().split(QLatin1Char(','));
    for (int i = 0; i < layouts.size(); ++i) {
        const QString l = layouts[i].trimmed();
        if (!l.isEmpty())
            result << linuxutil::xkbLayout(l, variants.value(i).trimmed(), QString());
    }
    return result;
}

QString HyprlandBackend::currentLayout(quint64) const
{
    const QJsonObject kb = keyboard();
    const int index = kb.value(QLatin1String("active_layout_index")).toInt(-1);
    return index >= 0 ? layouts().value(index).id : QString();
}

void HyprlandBackend::setLayout(const QString &id, quint64)
{
    const QList<LayoutInfo> all = layouts();
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].id != id)
            continue;
        if (command("switchxkblayout all " + QByteArray::number(i)).trimmed() != "ok") {
            // Older Hyprland: no "all", address each keyboard by name.
            const QJsonArray keyboards = json("j/devices").toObject().value(QLatin1String("keyboards")).toArray();
            for (const QJsonValue &kb : keyboards)
                command("switchxkblayout " + kb.toObject().value(QLatin1String("name")).toString().toUtf8()
                        + ' ' + QByteArray::number(i));
        }
        return;
    }
}

QStringList HyprlandBackend::runningApps() const
{
    QSet<QString> apps;
    for (const QJsonValue &v : json("j/clients").toArray()) {
        const QJsonObject c = v.toObject();
        QStringList ids = linuxutil::idsForPid(c.value(QLatin1String("pid")).toVariant().toLongLong());
        linuxutil::addIds(ids, {c.value(QLatin1String("class")).toString()});
        if (!ids.isEmpty())
            apps.insert(ids.first());
    }
    return QStringList(apps.begin(), apps.end());
}
