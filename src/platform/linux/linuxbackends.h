#pragma once

#include "platform/backend.h"

#include <QByteArray>
#include <QJsonValue>
#include <QLocalSocket>
#include <QTimer>

class QSocketNotifier;

namespace linuxutil {
// Lower-case identifiers for a process: executable name and /proc/<pid>/comm.
QStringList idsForPid(qint64 pid);
// Adds lower-cased non-empty ids (".desktop" stripped), keeping order and uniqueness.
void addIds(QStringList &ids, const QStringList &more);
// "us" + "dvorak" -> LayoutInfo{id "us(dvorak)", code "US", lang "en"}.
LayoutInfo xkbLayout(const QString &layout, const QString &variant, const QString &longName);
// "English (US)" -> LayoutInfo{id = longName, code "EN", lang "en"} (Sway exposes only long names).
LayoutInfo namedLayout(const QString &longName);
bool autostartEnabled();
AutostartResult setAutostart(bool enabled);
} // namespace linuxutil

class LinuxBackend : public PlatformBackend
{
    Q_OBJECT
public:
    using PlatformBackend::PlatformBackend;
    bool autostartEnabled() const override { return linuxutil::autostartEnabled(); }
    AutostartResult setAutostart(bool enabled) override { return linuxutil::setAutostart(enabled); }
};

// --- X11: any EWMH window manager (XFCE, Cinnamon, MATE, LXQt, Budgie, i3, Openbox, ...) -----
class X11Backend : public LinuxBackend
{
    Q_OBJECT
public:
    explicit X11Backend(QObject *parent = nullptr);
    ~X11Backend() override;
    QString name() const override { return QStringLiteral("X11"); }
    QString problem() const override;
    void start() override;
    QList<LayoutInfo> layouts() const override;
    QString currentLayout(quint64 handle) const override;
    void setLayout(const QString &id, quint64 handle) override;
    QStringList runningApps() const override;

private:
    void processEvents();
    void reportActiveWindow();
    QStringList idsForWindow(unsigned long window, QString *title) const;

    void *m_display = nullptr;  // Display*
    unsigned long m_root = 0;
    unsigned long m_atomActive = 0, m_atomPid = 0, m_atomName = 0, m_atomUtf8 = 0, m_atomClients = 0, m_atomRules = 0;
    QSocketNotifier *m_notifier = nullptr;
};

// --- Session D-Bus endpoint used by the KWin script and the GNOME Shell extension ----------
class DBusFocusBridge : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.KeyboardChanger")
public:
    using QObject::QObject;
    bool registerOnBus();

public slots:
    Q_SCRIPTABLE void focus(const QString &pid, const QString &windowClass, const QString &appId, const QString &title);

signals:
    void focusReported(const FocusInfo &info);
};

// --- KDE Plasma 5/6 (X11 and Wayland): KWin script + org.kde.keyboard --------------------
class KdeBackend : public LinuxBackend
{
    Q_OBJECT
public:
    explicit KdeBackend(QObject *parent = nullptr);
    ~KdeBackend() override;
    QString name() const override { return QStringLiteral("KDE Plasma"); }
    QString problem() const override { return m_problem; }
    void start() override;
    QList<LayoutInfo> layouts() const override;
    QString currentLayout(quint64 handle) const override;
    void setLayout(const QString &id, quint64 handle) override;

private:
    DBusFocusBridge m_bridge;
    QString m_problem;
    bool m_scriptLoaded = false;
};

// --- GNOME 45+ (X11 and Wayland): companion GNOME Shell extension --------------------------
class GnomeBackend : public LinuxBackend
{
    Q_OBJECT
public:
    explicit GnomeBackend(QObject *parent = nullptr);
    QString name() const override { return QStringLiteral("GNOME"); }
    QString problem() const override { return m_problem; }
    void start() override;
    QList<LayoutInfo> layouts() const override;
    QString currentLayout(quint64 handle) const override;
    void setLayout(const QString &id, quint64 handle) override;
    QStringList runningApps() const override;

private:
    bool probeExtension();
    QJsonValue callJson(const QString &method) const;

    DBusFocusBridge m_bridge;
    QString m_problem;
    QTimer m_retry;
};

// --- Sway (i3 IPC over $SWAYSOCK) ------------------------------------------------------------
class SwayBackend : public LinuxBackend
{
    Q_OBJECT
public:
    explicit SwayBackend(QObject *parent = nullptr);
    QString name() const override { return QStringLiteral("Sway"); }
    QString problem() const override;
    void start() override;
    QList<LayoutInfo> layouts() const override;
    QString currentLayout(quint64 handle) const override;
    void setLayout(const QString &id, quint64 handle) override;
    QStringList runningApps() const override;

private:
    QJsonValue request(quint32 type, const QByteArray &payload = {}) const;
    void onEvents();
    void reportNode(const QJsonObject &node);
    QJsonObject keyboard() const;

    QString m_path;
    QLocalSocket m_events;
    QByteArray m_buffer;
};

// --- Hyprland (sockets in $XDG_RUNTIME_DIR/hypr/<instance>) -------------------------------
class HyprlandBackend : public LinuxBackend
{
    Q_OBJECT
public:
    explicit HyprlandBackend(QObject *parent = nullptr);
    QString name() const override { return QStringLiteral("Hyprland"); }
    QString problem() const override;
    void start() override;
    QList<LayoutInfo> layouts() const override;
    QString currentLayout(quint64 handle) const override;
    void setLayout(const QString &id, quint64 handle) override;
    QStringList runningApps() const override;

private:
    QByteArray command(const QByteArray &cmd) const;
    QJsonValue json(const QByteArray &cmd) const;
    void onEvents();
    void reportActive();
    QJsonObject keyboard() const;

    QString m_dir;
    QLocalSocket m_events;
    QByteArray m_buffer;
};

// --- Anything else (e.g. other Wayland compositors) ---------------------------------------
class UnsupportedBackend : public LinuxBackend
{
    Q_OBJECT
public:
    UnsupportedBackend(const QString &session, QObject *parent = nullptr)
        : LinuxBackend(parent), m_session(session) {}
    QString name() const override { return m_session; }
    QString problem() const override;
    void start() override {}
    QList<LayoutInfo> layouts() const override { return {}; }
    QString currentLayout(quint64) const override { return {}; }
    void setLayout(const QString &, quint64) override {}

private:
    QString m_session;
};
