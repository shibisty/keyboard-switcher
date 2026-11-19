#include "linuxbackends.h"

#include <QCoreApplication>
#include <QSet>
#include <QSocketNotifier>

// Xlib last: it defines macros (None, Bool, Status, ...) that clash with Qt headers.
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace {

int ignoreXErrors(Display *, XErrorEvent *)
{
    return 0; // windows can vanish between the event and our query (BadWindow)
}

Display *dpy(void *p) { return static_cast<Display *>(p); }

// Reads a property as raw items; caller frees with XFree.
unsigned char *getProperty(Display *d, Window w, Atom prop, Atom type, unsigned long *count, int *format = nullptr)
{
    Atom actualType = 0;
    int actualFormat = 0;
    unsigned long items = 0, after = 0;
    unsigned char *data = nullptr;
    if (XGetWindowProperty(d, w, prop, 0, 1 << 16, False, type, &actualType, &actualFormat, &items, &after, &data) != Success
        || !data) {
        *count = 0;
        return nullptr;
    }
    *count = items;
    if (format)
        *format = actualFormat;
    return data;
}

struct XkbNames {
    QStringList layouts, variants;
};

XkbNames rulesNames(Display *d, Window root, Atom rulesAtom)
{
    XkbNames names;
    unsigned long n = 0;
    unsigned char *data = getProperty(d, root, rulesAtom, XA_STRING, &n);
    if (!data)
        return names;
    // NUL-separated: rules, model, layout, variant, options
    const QList<QByteArray> parts = QByteArray(reinterpret_cast<const char *>(data), int(n)).split('\0');
    XFree(data);
    names.layouts = QString::fromLatin1(parts.value(2)).split(QLatin1Char(','));
    names.variants = QString::fromLatin1(parts.value(3)).split(QLatin1Char(','));
    return names;
}

} // namespace

X11Backend::X11Backend(QObject *parent)
    : LinuxBackend(parent)
{
    Display *d = XOpenDisplay(nullptr);
    m_display = d;
    if (!d)
        return;
    XSetErrorHandler(ignoreXErrors);
    m_root = DefaultRootWindow(d);
    m_atomActive = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    m_atomPid = XInternAtom(d, "_NET_WM_PID", False);
    m_atomName = XInternAtom(d, "_NET_WM_NAME", False);
    m_atomUtf8 = XInternAtom(d, "UTF8_STRING", False);
    m_atomClients = XInternAtom(d, "_NET_CLIENT_LIST", False);
    m_atomRules = XInternAtom(d, "_XKB_RULES_NAMES", False);
}

X11Backend::~X11Backend()
{
    delete m_notifier;
    if (m_display)
        XCloseDisplay(dpy(m_display));
}

QString X11Backend::problem() const
{
    if (!m_display)
        return QCoreApplication::translate("Backend", "Cannot connect to the X server.");
    return {};
}

void X11Backend::start()
{
    if (!m_display)
        return;
    Display *d = dpy(m_display);
    XSelectInput(d, m_root, PropertyChangeMask);
    XFlush(d);
    m_notifier = new QSocketNotifier(ConnectionNumber(d), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &X11Backend::processEvents);
    reportActiveWindow();
}

void X11Backend::processEvents()
{
    Display *d = dpy(m_display);
    bool activeChanged = false;
    while (XPending(d)) {
        XEvent ev;
        XNextEvent(d, &ev);
        if (ev.type == PropertyNotify && ev.xproperty.atom == m_atomActive)
            activeChanged = true;
    }
    if (activeChanged)
        reportActiveWindow();
}

QStringList X11Backend::idsForWindow(unsigned long window, QString *title) const
{
    Display *d = dpy(m_display);
    QStringList ids;

    unsigned long n = 0;
    if (unsigned char *data = getProperty(d, window, m_atomPid, XA_CARDINAL, &n)) {
        if (n > 0)
            linuxutil::addIds(ids, linuxutil::idsForPid(qint64(*reinterpret_cast<unsigned long *>(data))));
        XFree(data);
    }
    XClassHint hint{};
    if (XGetClassHint(d, window, &hint)) {
        linuxutil::addIds(ids, {QString::fromLocal8Bit(hint.res_class), QString::fromLocal8Bit(hint.res_name)});
        if (hint.res_class)
            XFree(hint.res_class);
        if (hint.res_name)
            XFree(hint.res_name);
    }
    if (title) {
        if (unsigned char *data = getProperty(d, window, m_atomName, m_atomUtf8, &n)) {
            *title = QString::fromUtf8(reinterpret_cast<const char *>(data), int(n));
            XFree(data);
        }
    }
    return ids;
}

void X11Backend::reportActiveWindow()
{
    Display *d = dpy(m_display);
    unsigned long n = 0;
    unsigned char *data = getProperty(d, m_root, m_atomActive, XA_WINDOW, &n);
    if (!data)
        return;
    const Window window = n > 0 ? *reinterpret_cast<Window *>(data) : 0;
    XFree(data);
    if (!window)
        return;
    FocusInfo info;
    info.appIds = idsForWindow(window, &info.title);
    emit focusChanged(info);
}

QList<LayoutInfo> X11Backend::layouts() const
{
    QList<LayoutInfo> result;
    if (!m_display)
        return result;
    Display *d = dpy(m_display);
    const XkbNames names = rulesNames(d, m_root, m_atomRules);

    QStringList longNames;
    if (XkbDescPtr desc = XkbAllocKeyboard()) {
        if (XkbGetNames(d, XkbGroupNamesMask, desc) == Success && desc->names) {
            for (int i = 0; i < XkbNumKbdGroups; ++i) {
                const Atom a = desc->names->groups[i];
                char *s = a ? XGetAtomName(d, a) : nullptr;
                longNames << (s ? QString::fromUtf8(s) : QString());
                if (s)
                    XFree(s);
            }
        }
        XkbFreeKeyboard(desc, 0, True);
    }
    for (int i = 0; i < names.layouts.size(); ++i) {
        if (!names.layouts[i].isEmpty())
            result << linuxutil::xkbLayout(names.layouts[i], names.variants.value(i), longNames.value(i));
    }
    return result;
}

QString X11Backend::currentLayout(quint64) const
{
    if (!m_display)
        return {};
    XkbStateRec state{};
    XkbGetState(dpy(m_display), XkbUseCoreKbd, &state);
    return layouts().value(state.group).id;
}

void X11Backend::setLayout(const QString &id, quint64)
{
    if (!m_display)
        return;
    const QList<LayoutInfo> all = layouts();
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].id == id) {
            XkbLockGroup(dpy(m_display), XkbUseCoreKbd, unsigned(i));
            XFlush(dpy(m_display));
            return;
        }
    }
}

QStringList X11Backend::runningApps() const
{
    QSet<QString> apps;
    if (!m_display)
        return {};
    unsigned long n = 0;
    unsigned char *data = getProperty(dpy(m_display), m_root, m_atomClients, XA_WINDOW, &n);
    if (!data)
        return {};
    const Window *windows = reinterpret_cast<Window *>(data);
    for (unsigned long i = 0; i < n; ++i) {
        const QStringList ids = idsForWindow(windows[i], nullptr);
        if (!ids.isEmpty())
            apps.insert(ids.first());
    }
    XFree(data);
    apps.remove(QCoreApplication::applicationFilePath().section(QLatin1Char('/'), -1).toLower());
    return QStringList(apps.begin(), apps.end());
}
