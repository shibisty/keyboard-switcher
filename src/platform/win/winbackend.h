#pragma once

#include "platform/backend.h"

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QTimer>

#include <windows.h>

// Foreground tracking: SetWinEventHook(EVENT_SYSTEM_FOREGROUND), out-of-context, no polling.
// Layout switching: WM_INPUTLANGCHANGEREQUEST posted to the target window.
class WinBackend : public PlatformBackend, public QAbstractNativeEventFilter
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

    bool canConvertText() const override { return true; }
    bool keyMap(const QString &layoutId, KeyMap *out) const override;
    bool sendCopy() override;
    bool sendPaste() override;
    bool modifiersReleased() const override;
    bool setConvertHotkey(int qtKey, int qtMods) override;
    void setWatchManualSwitch(bool on) override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;
#else
    bool nativeEventFilter(const QByteArray &type, void *message, long *result) override;
#endif

private:
    static void CALLBACK winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD);
    void emitFocus(HWND hwnd);

    bool sendCtrlKey(WORD scanCode);
    void pollLayout();

    static WinBackend *s_instance;
    HWINEVENTHOOK m_hook = nullptr;
    bool m_hotkeyRegistered = false;
    bool m_filterInstalled = false;
    QTimer m_layoutTimer;
    QElapsedTimer m_ownSwitch;
    HWND m_watchedWindow = nullptr;
    HKL m_knownLayout = nullptr;
};
