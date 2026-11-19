#include "winbackend.h"

#include "core/textconvert.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QDir>
#include <QSet>
#include <QSettings>

#include <dwmapi.h>
#include <iterator>
#include <vector>

// Implemented in autostart_winrt.cpp (StartupTask inside an MSIX package).
// Codes: 0 disabled, 1 enabled, 2 disabled by user, 3 disabled by policy, -1 error / unsupported.
namespace autostart_winrt {
int state();
int setEnabled(bool enabled);
} // namespace autostart_winrt

PlatformBackend *PlatformBackend::create(QObject *parent)
{
    return new WinBackend(parent);
}

namespace {

quint64 hklToInt(HKL h) { return static_cast<quint64>(reinterpret_cast<quintptr>(h)); }
QString hklToId(HKL h) { return QString::number(hklToInt(h), 16); }
LANGID langOf(quint64 hkl) { return LANGID(hkl & 0xFFFF); }

QString exeNameForPid(DWORD pid)
{
    if (!pid)
        return {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    QString result;
    wchar_t buf[1024];
    DWORD size = DWORD(std::size(buf));
    if (QueryFullProcessImageNameW(h, 0, buf, &size)) {
        const QString path = QString::fromWCharArray(buf, int(size));
        result = path.mid(path.lastIndexOf(QLatin1Char('\\')) + 1).toLower();
    }
    CloseHandle(h);
    return result;
}

struct ChildSearch {
    DWORD hostPid = 0;
    DWORD foundPid = 0;
};

BOOL CALLBACK findForeignChild(HWND child, LPARAM lp)
{
    auto *ctx = reinterpret_cast<ChildSearch *>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(child, &pid);
    if (pid && pid != ctx->hostPid) {
        ctx->foundPid = pid;
        return FALSE;
    }
    return TRUE;
}

// Lower-case exe name; UWP windows hosted by ApplicationFrameHost.exe resolve to the real app.
QString processNameForWindow(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    QString name = exeNameForPid(pid);
    if (name == QLatin1String("applicationframehost.exe")) {
        ChildSearch ctx{pid, 0};
        EnumChildWindows(hwnd, findForeignChild, reinterpret_cast<LPARAM>(&ctx));
        const QString real = exeNameForPid(ctx.foundPid);
        if (!real.isEmpty())
            name = real;
    }
    return name;
}

BOOL CALLBACK collectWindow(HWND hwnd, LPARAM lp)
{
    auto *names = reinterpret_cast<QSet<QString> *>(lp);
    if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) == 0)
        return TRUE;
    if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, 14 /* DWMWA_CLOAKED, Windows 8+ */, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;
    const QString name = processNameForWindow(hwnd);
    if (!name.isEmpty())
        names->insert(name);
    return TRUE;
}

std::vector<HKL> layoutList()
{
    const int n = GetKeyboardLayoutList(0, nullptr);
    std::vector<HKL> v(n > 0 ? size_t(n) : 0);
    if (n > 0)
        v.resize(size_t(GetKeyboardLayoutList(n, v.data())));
    return v;
}

QString layoutCode(quint64 hkl)
{
    wchar_t buf[16] = {};
    if (GetLocaleInfoW(MAKELCID(langOf(hkl), SORT_DEFAULT), LOCALE_SISO639LANGNAME, buf, int(std::size(buf))) > 0)
        return QString::fromWCharArray(buf).toUpper();
    return QString::number(langOf(hkl), 16).rightJustified(4, QLatin1Char('0'));
}

QString layoutDisplayName(quint64 hkl)
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(langOf(hkl), SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
        wchar_t name[256] = {};
        if (GetLocaleInfoEx(locale, LOCALE_SLOCALIZEDDISPLAYNAME, name, int(std::size(name))) > 0)
            return QString::fromWCharArray(name);
        return QString::fromWCharArray(locale);
    }
    return layoutCode(hkl);
}

LayoutInfo makeInfo(quint64 hkl)
{
    LayoutInfo info;
    info.id = QString::number(hkl, 16);
    info.code = layoutCode(hkl);
    info.lang = info.code.toLower();
    info.name = layoutDisplayName(hkl);
    return info;
}

// Exact HKL, or another installed layout of the same language.
HKL resolveLayout(const QString &id)
{
    bool ok = false;
    const quint64 wanted = id.toULongLong(&ok, 16);
    if (!ok)
        return nullptr;
    const auto list = layoutList();
    for (HKL h : list)
        if (hklToInt(h) == wanted)
            return h;
    for (HKL h : list)
        if (langOf(hklToInt(h)) == langOf(wanted))
            return h;
    return nullptr;
}

bool isPackaged()
{
    using Fn = LONG(WINAPI *)(UINT32 *, PWSTR);
    static const Fn fn = reinterpret_cast<Fn>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFullName")));
    if (!fn)
        return false;
    UINT32 len = 0;
    return fn(&len, nullptr) == ERROR_INSUFFICIENT_BUFFER;
}

const char kRunKey[] = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char kRunValue[] = "KeyboardChanger";

} // namespace

WinBackend *WinBackend::s_instance = nullptr;

WinBackend::WinBackend(QObject *parent)
    : PlatformBackend(parent)
{
    s_instance = this;
}

WinBackend::~WinBackend()
{
    setConvertHotkey(0, 0);
    if (m_filterInstalled)
        QCoreApplication::instance()->removeNativeEventFilter(this);
    if (m_hook)
        UnhookWinEvent(m_hook);
    s_instance = nullptr;
}

void WinBackend::start()
{
    // WINEVENT_OUTOFCONTEXT: delivered through this thread's message loop (Qt's), no DLL injection.
    m_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                             &WinBackend::winEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    emitFocus(GetForegroundWindow());
}

void CALLBACK WinBackend::winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
{
    if (event == EVENT_SYSTEM_FOREGROUND && idObject == OBJID_WINDOW && s_instance)
        s_instance->emitFocus(hwnd);
}

void WinBackend::emitFocus(HWND hwnd)
{
    if (!hwnd)
        return;
    FocusInfo info;
    const QString exe = processNameForWindow(hwnd);
    if (!exe.isEmpty())
        info.appIds << exe;
    info.handle = static_cast<quint64>(reinterpret_cast<quintptr>(hwnd));
    emit focusChanged(info);
}

QList<LayoutInfo> WinBackend::layouts() const
{
    QList<LayoutInfo> result;
    for (HKL h : layoutList())
        result << makeInfo(hklToInt(h));
    // Several layouts of one language (US + Dvorak): make them distinguishable.
    for (int i = 0; i < result.size(); ++i) {
        for (int j = 0; j < result.size(); ++j) {
            if (i != j && result[i].code == result[j].code) {
                const quint64 hkl = result[i].id.toULongLong(nullptr, 16);
                result[i].name += QStringLiteral(" [%1]").arg(quint32((hkl >> 16) & 0xFFFF), 4, 16, QLatin1Char('0'));
                break;
            }
        }
    }
    return result;
}

LayoutInfo WinBackend::describeLayout(const QString &id, bool *installed) const
{
    if (installed)
        *installed = resolveLayout(id) != nullptr;
    return makeInfo(id.toULongLong(nullptr, 16));
}

QString WinBackend::currentLayout(quint64 handle) const
{
    HWND hwnd = handle ? reinterpret_cast<HWND>(static_cast<quintptr>(handle)) : GetForegroundWindow();
    if (!IsWindow(hwnd))
        hwnd = GetForegroundWindow();
    return hklToId(GetKeyboardLayout(GetWindowThreadProcessId(hwnd, nullptr)));
}

void WinBackend::setLayout(const QString &id, quint64 handle)
{
    HKL hkl = resolveLayout(id);
    HWND hwnd = handle ? reinterpret_cast<HWND>(static_cast<quintptr>(handle)) : GetForegroundWindow();
    // Always post: GetKeyboardLayout() of a just-activated thread can be stale when Windows
    // uses one input method for all apps (the default since Windows 8).
    if (hkl && hwnd)
        m_ownSwitch.restart();
    if (hkl && hwnd)
        PostMessageW(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(hkl));
}

QStringList WinBackend::runningApps() const
{
    QSet<QString> names;
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&names));
    names.remove(exeNameForPid(GetCurrentProcessId()));
    return QStringList(names.begin(), names.end());
}

bool WinBackend::autostartEnabled() const
{
    if (isPackaged())
        return autostart_winrt::state() == 1;
    return QSettings(QLatin1String(kRunKey), QSettings::NativeFormat).contains(QLatin1String(kRunValue));
}

AutostartResult WinBackend::setAutostart(bool enabled)
{
    if (isPackaged()) {
        // Registry writes of packaged apps are virtualized, so MSIX uses windows.startupTask.
        switch (autostart_winrt::setEnabled(enabled)) {
        case 0: return enabled ? AutostartResult::Failed : AutostartResult::Ok;
        case 1: return enabled ? AutostartResult::Ok : AutostartResult::Failed;
        case 2: return AutostartResult::DisabledByUser;
        case 3: return AutostartResult::DisabledByPolicy;
        default: return AutostartResult::Failed;
        }
    }
    QSettings reg(QLatin1String(kRunKey), QSettings::NativeFormat);
    if (enabled)
        reg.setValue(QLatin1String(kRunValue),
                     QStringLiteral("\"%1\" --autostart").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else
        reg.remove(QLatin1String(kRunValue));
    reg.sync();
    return reg.status() == QSettings::NoError ? AutostartResult::Ok : AutostartResult::Failed;
}


// --- Converting selected text ---------------------------------------------------------------

namespace {

constexpr int kHotkeyId = 0x4B43;  // "KC"

UINT vkForQtKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return UINT('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return UINT('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return UINT(VK_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_Pause:      return VK_PAUSE;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    case Qt::Key_Print:      return VK_SNAPSHOT;
    case Qt::Key_Insert:     return VK_INSERT;
    case Qt::Key_Delete:     return VK_DELETE;
    case Qt::Key_Home:       return VK_HOME;
    case Qt::Key_End:        return VK_END;
    case Qt::Key_PageUp:     return VK_PRIOR;
    case Qt::Key_PageDown:   return VK_NEXT;
    case Qt::Key_Space:      return VK_SPACE;
    case Qt::Key_Tab:        return VK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter:      return VK_RETURN;
    case Qt::Key_Escape:     return VK_ESCAPE;
    case Qt::Key_Backspace:  return VK_BACK;
    case Qt::Key_Left:       return VK_LEFT;
    case Qt::Key_Right:      return VK_RIGHT;
    case Qt::Key_Up:         return VK_UP;
    case Qt::Key_Down:       return VK_DOWN;
    default:                 return 0;
    }
}

bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

}  // namespace

bool WinBackend::keyMap(const QString &layoutId, KeyMap *out) const
{
    const HKL hkl = resolveLayout(layoutId);
    if (!hkl)
        return false;
    // Level index: bit0 = Shift, bit1 = AltGr (Ctrl+Alt, how Windows models it).
    for (UINT scan = 0x02; scan <= 0x53; ++scan) {
        const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, hkl);
        if (!vk)
            continue;
        KeyLevels k;
        bool any = false;
        for (int level = 0; level < 4; ++level) {
            BYTE state[256] = {};
            if (level & 1)
                state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
            if (level & 2)
                state[VK_CONTROL] = state[VK_LCONTROL] = state[VK_MENU] = state[VK_RMENU] = 0x80;
            wchar_t buf[8] = {};
            // Flag 4: do not change the keyboard state (dead keys stay untouched).
            const int n = ToUnicodeEx(vk, scan, state, buf, int(std::size(buf)), 4, hkl);
            if (n < 0) {  // dead key: it was queued by the call, flush it
                BYTE none[256] = {};
                wchar_t flush[8];
                ToUnicodeEx(VK_SPACE, 0x39, none, flush, int(std::size(flush)), 4, hkl);
                continue;
            }
            if (n >= 1 && buf[0] >= 0x20 && buf[0] != 0x7F) {
                k.chars[level] = QString::fromWCharArray(buf, n);
                any = true;
            }
        }
        if (any)
            out->insert(quint32(scan), k);
    }
    return !out->isEmpty();
}

bool WinBackend::sendCtrlKey(WORD scanCode)
{
    // Physical keys by scan code: Ctrl+C / Ctrl+V keep working in any layout.
    constexpr WORD kCtrlScan = 0x1D;
    INPUT in[4] = {};
    const WORD scans[4] = {kCtrlScan, scanCode, scanCode, kCtrlScan};
    for (int i = 0; i < 4; ++i) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wScan = scans[i];
        in[i].ki.dwFlags = KEYEVENTF_SCANCODE | (i >= 2 ? KEYEVENTF_KEYUP : 0);
    }
    return SendInput(4, in, sizeof(INPUT)) == 4;
}

bool WinBackend::sendCopy() { return sendCtrlKey(0x2E); }
bool WinBackend::sendPaste() { return sendCtrlKey(0x2F); }

bool WinBackend::modifiersReleased() const
{
    return !keyDown(VK_SHIFT) && !keyDown(VK_CONTROL) && !keyDown(VK_MENU) && !keyDown(VK_LWIN) && !keyDown(VK_RWIN);
}

bool WinBackend::setConvertHotkey(int qtKey, int qtMods)
{
    if (m_hotkeyRegistered) {
        UnregisterHotKey(nullptr, kHotkeyId);
        m_hotkeyRegistered = false;
    }
    if (qtKey == 0)
        return true;
    const UINT vk = vkForQtKey(qtKey);
    if (!vk)
        return false;
    UINT mods = MOD_NOREPEAT;
    if (qtMods & Qt::ShiftModifier)   mods |= MOD_SHIFT;
    if (qtMods & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (qtMods & Qt::AltModifier)     mods |= MOD_ALT;
    if (qtMods & Qt::MetaModifier)    mods |= MOD_WIN;
    // hWnd = null: WM_HOTKEY is posted to this thread's queue and seen by the native event filter.
    if (!RegisterHotKey(nullptr, kHotkeyId, mods, vk))
        return false;
    m_hotkeyRegistered = true;
    if (!m_filterInstalled) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_filterInstalled = true;
    }
    return true;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool WinBackend::nativeEventFilter(const QByteArray &, void *message, qintptr *)
#else
bool WinBackend::nativeEventFilter(const QByteArray &, void *message, long *)
#endif
{
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == WPARAM(kHotkeyId)) {
        emit convertHotkeyPressed();
        return true;
    }
    return false;
}

// Windows has no "layout changed" notification for other applications, so the foreground
// window's layout is compared periodically. Changes right after our own switch, and when the
// foreground window changes, are not user switches.
void WinBackend::setWatchManualSwitch(bool on)
{
    if (!on) {
        m_layoutTimer.stop();
        return;
    }
    if (!m_layoutTimer.isActive()) {
        m_layoutTimer.setInterval(120);
        connect(&m_layoutTimer, &QTimer::timeout, this, &WinBackend::pollLayout, Qt::UniqueConnection);
    }
    m_watchedWindow = nullptr;
    m_knownLayout = nullptr;
    m_layoutTimer.start();
}

void WinBackend::pollLayout()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return;
    const HKL now = GetKeyboardLayout(GetWindowThreadProcessId(hwnd, nullptr));
    if (hwnd != m_watchedWindow || !m_knownLayout) {
        m_watchedWindow = hwnd;
        m_knownLayout = now;
        return;
    }
    if (now == m_knownLayout)
        return;
    const HKL previous = m_knownLayout;
    m_knownLayout = now;
    if (m_ownSwitch.isValid() && m_ownSwitch.elapsed() < 600)
        return;
    emit layoutSwitchedByUser(hklToId(previous), hklToId(now));
}
