#include "winbackend.h"

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
