#include "linuxbackends.h"

#include <QCoreApplication>
#include <QSet>
#include <QSocketNotifier>

// Xlib last: it defines macros (None, Bool, Status, ...) that clash with Qt headers.
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

namespace {

bool g_xError = false;

int ignoreXErrors(Display *, XErrorEvent *)
{
    g_xError = true; // windows can vanish between the event and our query (BadWindow); XGrabKey conflicts too
    return 0;
}

KeySym keysymForQtKey(int key)
{
    if (key >= 0x30 && key <= 0x39)
        return KeySym(key);                       // digits
    if (key >= 0x41 && key <= 0x5a)
        return KeySym(key - 0x41 + 0x61);         // letters: lower-case keysyms
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return XK_F1 + KeySym(key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Pause:      return XK_Pause;
    case Qt::Key_ScrollLock: return XK_Scroll_Lock;
    case Qt::Key_Print:      return XK_Print;
    case Qt::Key_Insert:     return XK_Insert;
    case Qt::Key_Delete:     return XK_Delete;
    case Qt::Key_Home:       return XK_Home;
    case Qt::Key_End:        return XK_End;
    case Qt::Key_PageUp:     return XK_Prior;
    case Qt::Key_PageDown:   return XK_Next;
    case Qt::Key_Space:      return XK_space;
    default:                 return NoSymbol;
    }
}

unsigned xModsForQt(int qtMods)
{
    unsigned m = 0;
    if (qtMods & Qt::ShiftModifier)   m |= ShiftMask;
    if (qtMods & Qt::ControlModifier) m |= ControlMask;
    if (qtMods & Qt::AltModifier)     m |= Mod1Mask;
    if (qtMods & Qt::MetaModifier)    m |= Mod4Mask;
    return m;
}

constexpr unsigned kRelevantMods = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;

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
    int opcode = 0, error = 0, major = XkbMajorVersion, minor = XkbMinorVersion;
    if (!XkbQueryExtension(d, &opcode, &m_xkbEventBase, &error, &major, &minor))
        m_xkbEventBase = -1;
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
        if (ev.type == PropertyNotify && ev.xproperty.atom == m_atomActive) {
            activeChanged = true;
        } else if (ev.type == KeyPress && m_hotkeyCode && ev.xkey.keycode == m_hotkeyCode
                   && (ev.xkey.state & kRelevantMods) == m_hotkeyMods) {
            emit convertHotkeyPressed();
        } else if (m_watchSwitch && m_xkbEventBase >= 0 && ev.type == m_xkbEventBase) {
            const XkbEvent *xe = reinterpret_cast<XkbEvent *>(&ev);
            if (xe->any.xkb_type == XkbStateNotify && (xe->state.changed & XkbGroupStateMask)) {
                const int previous = m_lastGroup;
                m_lastGroup = xe->state.group;
                const bool ours = m_ownSwitch.isValid() && m_ownSwitch.elapsed() < 400;
                if (!ours && previous != m_lastGroup) {
                    const QList<LayoutInfo> all = layouts();
                    emit layoutSwitchedByUser(all.value(previous).id, all.value(m_lastGroup).id);
                }
            }
        }
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
            m_ownSwitch.restart();
            m_lastGroup = i;
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

bool X11Backend::canConvertText() const
{
    if (!m_display)
        return false;
    int event = 0, error = 0, major = 0, minor = 0;
    return XTestQueryExtension(dpy(m_display), &event, &error, &major, &minor);
}

bool X11Backend::sendCtrlKey(unsigned keycode)
{
    if (!m_display)
        return false;
    Display *d = dpy(m_display);
    const KeyCode ctrl = XKeysymToKeycode(d, XK_Control_L);
    if (!ctrl)
        return false;
    XTestFakeKeyEvent(d, ctrl, True, 0);
    XTestFakeKeyEvent(d, keycode, True, 0);
    XTestFakeKeyEvent(d, keycode, False, 0);
    XTestFakeKeyEvent(d, ctrl, False, 0);
    XFlush(d);
    return true;
}

bool X11Backend::modifiersReleased() const
{
    if (!m_display)
        return true;
    XkbStateRec state{};
    XkbGetState(dpy(m_display), XkbUseCoreKbd, &state);
    return (state.mods & kRelevantMods) == 0;
}

bool X11Backend::setConvertHotkey(int qtKey, int qtMods)
{
    if (!m_display)
        return false;
    Display *d = dpy(m_display);
    if (m_hotkeyCode) {
        XUngrabKey(d, int(m_hotkeyCode), AnyModifier, m_root);
        m_hotkeyCode = 0;
    }
    if (qtKey == 0)
        return true;
    const KeySym sym = keysymForQtKey(qtKey);
    const KeyCode code = sym == NoSymbol ? 0 : XKeysymToKeycode(d, sym);
    if (!code)
        return false;
    const unsigned mods = xModsForQt(qtMods);
    // The grab must also match with Caps Lock / Num Lock on.
    const unsigned locks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    g_xError = false;
    for (unsigned lock : locks)
        XGrabKey(d, code, mods | lock, m_root, False, GrabModeAsync, GrabModeAsync);
    XSync(d, False);
    if (g_xError) {  // somebody else owns this combination
        XUngrabKey(d, code, AnyModifier, m_root);
        return false;
    }
    m_hotkeyCode = code;
    m_hotkeyMods = mods;
    return true;
}

void X11Backend::setWatchManualSwitch(bool on)
{
    m_watchSwitch = on;
    if (!m_display || m_xkbEventBase < 0)
        return;
    Display *d = dpy(m_display);
    XkbSelectEvents(d, XkbUseCoreKbd, XkbStateNotifyMask, on ? XkbStateNotifyMask : 0);
    XkbSelectEventDetails(d, XkbUseCoreKbd, XkbStateNotify, XkbGroupStateMask, on ? XkbGroupStateMask : 0);
    XkbStateRec state{};
    XkbGetState(d, XkbUseCoreKbd, &state);
    m_lastGroup = state.group;
    XFlush(d);
}
