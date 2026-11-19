#pragma once

#include "platform/backend.h"

#include <windows.h>

// Foreground tracking: SetWinEventHook(EVENT_SYSTEM_FOREGROUND), out-of-context, no polling.
// Layout switching: WM_INPUTLANGCHANGEREQUEST posted to the target window.
class WinBackend : public PlatformBackend
{
    Q_OBJECT
public:
    explicit WinBackend(QObject *parent = nullptr);
    ~WinBackend() override;

    QString name() const override { return QStringLiteral("Windows"); }
    void start() override;

    QList<LayoutInfo> layouts() const override;
    LayoutInfo describeLayout(const QString &id, bool *installed) const override;
    QString currentLayout(quint64 handle) const override;
    void setLayout(const QString &id, quint64 handle) override;
    QStringList runningApps() const override;

    bool autostartEnabled() const override;
    AutostartResult setAutostart(bool enabled) override;

private:
    static void CALLBACK winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD);
    void emitFocus(HWND hwnd);

    static WinBackend *s_instance;
    HWINEVENTHOOK m_hook = nullptr;
};
