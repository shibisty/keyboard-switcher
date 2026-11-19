// Compiled with -fobjc-arc (see CMakeLists.txt).
#include "macbackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>

PlatformBackend *PlatformBackend::create(QObject *parent)
{
    return new MacBackend(parent);
}

namespace {

NSArray *selectableKeyboardSources()
{
    NSDictionary *filter = @{
        (__bridge NSString *)kTISPropertyInputSourceCategory : (__bridge NSString *)kTISCategoryKeyboardInputSource,
        (__bridge NSString *)kTISPropertyInputSourceIsSelectCapable : @YES,
    };
    return CFBridgingRelease(TISCreateInputSourceList((__bridge CFDictionaryRef)filter, false));
}

QString sourceString(TISInputSourceRef src, CFStringRef key)
{
    auto value = static_cast<CFStringRef>(TISGetInputSourceProperty(src, key));
    return value ? QString::fromCFString(value) : QString();
}

LayoutInfo makeInfo(TISInputSourceRef src)
{
    LayoutInfo info;
    info.id = sourceString(src, kTISPropertyInputSourceID);
    info.name = sourceString(src, kTISPropertyLocalizedName);
    auto langs = static_cast<CFArrayRef>(TISGetInputSourceProperty(src, kTISPropertyInputSourceLanguages));
    if (langs && CFArrayGetCount(langs) > 0)
        info.lang = QString::fromCFString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(langs, 0))).left(2).toLower();
    info.code = info.lang.isEmpty() ? info.id.section(QLatin1Char('.'), -1).left(3).toUpper() : info.lang.toUpper();
    return info;
}

TISInputSourceRef findSource(NSArray *sources, const QString &sourceId)
{
    for (id obj in sources) {
        auto src = (__bridge TISInputSourceRef)obj;
        if (sourceString(src, kTISPropertyInputSourceID) == sourceId)
            return src;
    }
    return nullptr;
}

QString launchAgentPath()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/io.github.keyboardswitcher.plist");
}

} // namespace

MacBackend::MacBackend(QObject *parent)
    : PlatformBackend(parent)
{
}

MacBackend::~MacBackend()
{
    if (m_observer) {
        id observer = (__bridge_transfer id)m_observer;
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:observer];
        m_observer = nullptr;
    }
}

void MacBackend::start()
{
    MacBackend *backend = this;
    id observer = [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceDidActivateApplicationNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
                    NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
                    backend->reportFrontmost((__bridge void *)app);
                }];
    m_observer = (__bridge_retained void *)observer;
    reportFrontmost((__bridge void *)[NSWorkspace sharedWorkspace].frontmostApplication);
}

void MacBackend::reportFrontmost(void *runningApplication)
{
    NSRunningApplication *app = (__bridge NSRunningApplication *)runningApplication;
    if (!app)
        return;
    FocusInfo info;
    if (app.bundleIdentifier)
        info.appIds << QString::fromNSString(app.bundleIdentifier).toLower();
    if (app.executableURL)
        info.appIds << QString::fromNSString(app.executableURL.lastPathComponent).toLower();
    info.appIds.removeDuplicates();
    if (app.localizedName)
        info.title = QString::fromNSString(app.localizedName);
    emit focusChanged(info);
}

QList<LayoutInfo> MacBackend::layouts() const
{
    QList<LayoutInfo> result;
    for (id obj in selectableKeyboardSources())
        result << makeInfo((__bridge TISInputSourceRef)obj);
    return result;
}

QString MacBackend::currentLayout(quint64) const
{
    TISInputSourceRef src = TISCopyCurrentKeyboardInputSource();
    if (!src)
        return {};
    const QString sourceId = sourceString(src, kTISPropertyInputSourceID);
    CFRelease(src);
    return sourceId;
}

void MacBackend::setLayout(const QString &layoutId, quint64)
{
    NSArray *sources = selectableKeyboardSources(); // keeps the refs alive during the call
    if (TISInputSourceRef src = findSource(sources, layoutId))
        TISSelectInputSource(src);
}

QStringList MacBackend::runningApps() const
{
    QSet<QString> ids;
    const QString self = QString::fromNSString([NSBundle mainBundle].bundleIdentifier ?: @"").toLower();
    for (NSRunningApplication *app in [NSWorkspace sharedWorkspace].runningApplications) {
        if (app.activationPolicy != NSApplicationActivationPolicyRegular || !app.bundleIdentifier)
            continue;
        const QString bundleId = QString::fromNSString(app.bundleIdentifier).toLower();
        if (bundleId != self)
            ids.insert(bundleId);
    }
    return QStringList(ids.begin(), ids.end());
}

void MacBackend::activateApp()
{
    [NSApp activateIgnoringOtherApps:YES];
}

bool MacBackend::autostartEnabled() const
{
    if (@available(macOS 13.0, *))
        return SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
    return QFile::exists(launchAgentPath());
}

AutostartResult MacBackend::setAutostart(bool enabled)
{
    if (@available(macOS 13.0, *)) {
        NSError *error = nil;
        SMAppService *service = SMAppService.mainAppService;
        const BOOL ok = enabled ? [service registerAndReturnError:&error] : [service unregisterAndReturnError:&error];
        if (ok)
            return AutostartResult::Ok;
        if (service.status == SMAppServiceStatusRequiresApproval) {
            [SMAppService openSystemSettingsLoginItems];
            return AutostartResult::DisabledByUser;
        }
        return AutostartResult::Failed;
    }

    const QString path = launchAgentPath();
    if (!enabled)
        return (!QFile::exists(path) || QFile::remove(path)) ? AutostartResult::Ok : AutostartResult::Failed;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return AutostartResult::Failed;
    const QString exe = QCoreApplication::applicationFilePath().toHtmlEscaped();
    f.write(QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\"><dict>\n"
                "  <key>Label</key><string>io.github.keyboardswitcher</string>\n"
                "  <key>ProgramArguments</key><array><string>%1</string><string>--autostart</string></array>\n"
                "  <key>RunAtLoad</key><true/>\n"
                "</dict></plist>\n").arg(exe).toUtf8());
    return AutostartResult::Ok;
}
