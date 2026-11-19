// StartupTask support for the MSIX package. Kept in its own translation unit,
// without Qt headers, so C++/WinRT and Qt macros never meet.
namespace autostart_winrt { int state(); int setEnabled(bool enabled); }

#if defined(_MSC_VER) && __has_include(<winrt/Windows.ApplicationModel.h>)
#  define KC_HAVE_WINRT 1
#endif

#ifdef KC_HAVE_WINRT

#include <thread>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>

using namespace winrt::Windows::ApplicationModel;

namespace {

constexpr wchar_t kTaskId[] = L"KeyboardSwitcherStartup"; // must match AppxManifest.xml

int toCode(StartupTaskState s)
{
    switch (s) {
    case StartupTaskState::Enabled:
    case StartupTaskState::EnabledByPolicy:  return 1;
    case StartupTaskState::DisabledByUser:   return 2;
    case StartupTaskState::DisabledByPolicy: return 3;
    default:                                 return 0;
    }
}

// The Qt GUI thread is an STA; blocking .get() is not allowed there,
// so the short WinRT call runs on a temporary MTA thread.
template <class F>
int runInMta(F &&f)
{
    int result = -1;
    std::thread t([&] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            return;
        }
        try {
            result = f();
        } catch (...) {
            result = -1;
        }
        winrt::uninit_apartment();
    });
    t.join();
    return result;
}

} // namespace

namespace autostart_winrt {

int state()
{
    return runInMta([] { return toCode(StartupTask::GetAsync(kTaskId).get().State()); });
}

int setEnabled(bool enabled)
{
    return runInMta([enabled] {
        StartupTask task = StartupTask::GetAsync(kTaskId).get();
        if (enabled)
            return toCode(task.RequestEnableAsync().get());
        task.Disable();
        return toCode(task.State());
    });
}

} // namespace autostart_winrt

#else // MinGW or SDK without C++/WinRT: packaged autostart unavailable

namespace autostart_winrt {
int state() { return -1; }
int setEnabled(bool) { return -1; }
} // namespace autostart_winrt

#endif
