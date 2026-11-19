#pragma once

#include "platform/backend.h"

// Focus: NSWorkspaceDidActivateApplicationNotification (no polling, no Accessibility permission).
// Layouts: Text Input Sources (TISSelectInputSource).
// Autostart: SMAppService (macOS 13+), LaunchAgent on older systems.
class MacBackend : public PlatformBackend
{
    Q_OBJECT
public:
    explicit MacBackend(QObject *parent = nullptr);
    ~MacBackend() override;

    QString name() const override { return QStringLiteral("macOS"); }
    void start() override;

    QList<LayoutInfo> layouts() const override;
    QString currentLayout(quint64 handle) const override;
    void setLayout(const QString &id, quint64 handle) override;
    QStringList runningApps() const override;
    void activateApp() override;

    bool autostartEnabled() const override;
    AutostartResult setAutostart(bool enabled) override;

    void reportFrontmost(void *runningApplication); // NSRunningApplication*

private:
    void *m_observer = nullptr;
};
