#include "linuxbackends.h"

#include <xkbcommon/xkbcommon.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

namespace linuxutil {

QStringList idsForPid(qint64 pid)
{
    QStringList ids;
    if (pid <= 0)
        return ids;
    const QString base = QStringLiteral("/proc/%1/").arg(pid);
    const QString exe = QFileInfo(base + QStringLiteral("exe")).symLinkTarget();
    if (!exe.isEmpty())
        ids << QFileInfo(exe).fileName();
    QFile comm(base + QStringLiteral("comm"));
    if (comm.open(QIODevice::ReadOnly))
        ids << QString::fromUtf8(comm.readAll()).trimmed();
    QStringList result;
    addIds(result, ids);
    return result;
}

void addIds(QStringList &ids, const QStringList &more)
{
    for (QString id : more) {
        id = id.trimmed().toLower();
        if (id.endsWith(QLatin1String(".desktop")))
            id.chop(8);
        if (!id.isEmpty() && !ids.contains(id))
            ids << id;
    }
}

namespace {

QString langForXkb(const QString &layout)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("us"), QStringLiteral("en")}, {QStringLiteral("gb"), QStringLiteral("en")},
        {QStringLiteral("au"), QStringLiteral("en")}, {QStringLiteral("ie"), QStringLiteral("en")},
        {QStringLiteral("nz"), QStringLiteral("en")}, {QStringLiteral("za"), QStringLiteral("en")},
        {QStringLiteral("ru"), QStringLiteral("ru")}, {QStringLiteral("ua"), QStringLiteral("uk")},
        {QStringLiteral("by"), QStringLiteral("be")}, {QStringLiteral("kz"), QStringLiteral("kk")},
        {QStringLiteral("de"), QStringLiteral("de")}, {QStringLiteral("fr"), QStringLiteral("fr")},
        {QStringLiteral("es"), QStringLiteral("es")}, {QStringLiteral("it"), QStringLiteral("it")},
        {QStringLiteral("pl"), QStringLiteral("pl")}, {QStringLiteral("cz"), QStringLiteral("cs")},
        {QStringLiteral("pt"), QStringLiteral("pt")}, {QStringLiteral("br"), QStringLiteral("pt")},
        {QStringLiteral("tr"), QStringLiteral("tr")}, {QStringLiteral("gr"), QStringLiteral("el")},
        {QStringLiteral("il"), QStringLiteral("he")}, {QStringLiteral("jp"), QStringLiteral("ja")},
        {QStringLiteral("cn"), QStringLiteral("zh")}, {QStringLiteral("kr"), QStringLiteral("ko")},
        {QStringLiteral("ge"), QStringLiteral("ka")}, {QStringLiteral("am"), QStringLiteral("hy")},
    };
    return map.value(layout);
}

QString langForName(const QString &longName)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("english"), QStringLiteral("en")},  {QStringLiteral("russian"), QStringLiteral("ru")},
        {QStringLiteral("ukrainian"), QStringLiteral("uk")}, {QStringLiteral("belarusian"), QStringLiteral("be")},
        {QStringLiteral("kazakh"), QStringLiteral("kk")},   {QStringLiteral("german"), QStringLiteral("de")},
        {QStringLiteral("french"), QStringLiteral("fr")},   {QStringLiteral("spanish"), QStringLiteral("es")},
        {QStringLiteral("italian"), QStringLiteral("it")},  {QStringLiteral("polish"), QStringLiteral("pl")},
        {QStringLiteral("czech"), QStringLiteral("cs")},    {QStringLiteral("portuguese"), QStringLiteral("pt")},
        {QStringLiteral("turkish"), QStringLiteral("tr")},  {QStringLiteral("greek"), QStringLiteral("el")},
        {QStringLiteral("hebrew"), QStringLiteral("he")},   {QStringLiteral("japanese"), QStringLiteral("ja")},
        {QStringLiteral("georgian"), QStringLiteral("ka")}, {QStringLiteral("armenian"), QStringLiteral("hy")},
    };
    return map.value(longName.section(QLatin1Char(' '), 0, 0).toLower());
}

} // namespace

LayoutInfo xkbLayout(const QString &layout, const QString &variant, const QString &longName)
{
    LayoutInfo info;
    info.id = variant.isEmpty() ? layout : QStringLiteral("%1(%2)").arg(layout, variant);
    info.code = layout.toUpper();
    info.name = longName.isEmpty() ? info.id : longName;
    info.lang = langForXkb(layout);
    if (info.lang.isEmpty())
        info.lang = langForName(longName);
    return info;
}

LayoutInfo namedLayout(const QString &longName)
{
    LayoutInfo info;
    info.id = longName;
    info.name = longName;
    info.lang = langForName(longName);
    info.code = info.lang.isEmpty() ? longName.left(2).toUpper() : info.lang.toUpper();
    return info;
}

namespace {
QString autostartFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/autostart/io.github.KeyboardChanger.desktop");
}
} // namespace

bool autostartEnabled()
{
    return QFile::exists(autostartFile());
}

AutostartResult setAutostart(bool enabled)
{
    const QString path = autostartFile();
    if (!enabled)
        return (!QFile::exists(path) || QFile::remove(path)) ? AutostartResult::Ok : AutostartResult::Failed;

    // An AppImage runs from a temporary mount: start the .AppImage file itself.
    QString exe = qEnvironmentVariable("APPIMAGE");
    if (exe.isEmpty())
        exe = QCoreApplication::applicationFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return AutostartResult::Failed;
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Keyboard Changer\n"
                           "Exec=\"%1\" --autostart\n"
                           "Icon=io.github.KeyboardChanger\n"
                           "X-GNOME-Autostart-enabled=true\n"
                           "X-KDE-autostart-after=panel\n")
                .arg(exe)
                .toUtf8());
    return AutostartResult::Ok;
}


bool keyMapFor(const QString &layoutId, KeyMap *out)
{
    QString layout = layoutId, variant;
    const int open = layoutId.indexOf(QLatin1Char('('));
    if (open > 0 && layoutId.endsWith(QLatin1Char(')'))) {
        layout = layoutId.left(open);
        variant = layoutId.mid(open + 1, layoutId.size() - open - 2);
    }
    if (layout.isEmpty())
        return false;
    const QByteArray l = layout.toLatin1(), v = variant.toLatin1();

    xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx)
        return false;
    xkb_rule_names names{};
    names.rules = "evdev";
    names.model = "pc105";
    names.layout = l.constData();
    names.variant = v.isEmpty() ? nullptr : v.constData();
    xkb_keymap *km = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!km) {
        xkb_context_unref(ctx);
        return false;
    }
    const xkb_keycode_t lo = xkb_keymap_min_keycode(km), hi = xkb_keymap_max_keycode(km);
    for (xkb_keycode_t key = lo; key <= hi; ++key) {
        const xkb_level_index_t levels = xkb_keymap_num_levels_for_key(km, key, 0);
        KeyLevels k;
        bool any = false;
        for (xkb_level_index_t level = 0; level < levels && level < 4; ++level) {
            const xkb_keysym_t *syms = nullptr;
            if (xkb_keymap_key_get_syms_by_level(km, key, 0, level, &syms) != 1)
                continue;
            const uint32_t cp = xkb_keysym_to_utf32(syms[0]);
            if (cp < 0x20 || cp == 0x7f)
                continue;
            k.chars[level] = QString::fromUcs4(reinterpret_cast<const char32_t *>(&cp), 1);
            any = true;
        }
        if (any)
            out->insert(quint32(key), k);
    }
    xkb_keymap_unref(km);
    xkb_context_unref(ctx);
    return !out->isEmpty();
}

} // namespace linuxutil

QString UnsupportedBackend::problem() const
{
    return QCoreApplication::translate("Backend",
                                       "This desktop session (%1) is not supported. Supported: X11 desktops, "
                                       "KDE Plasma, GNOME, Sway, Hyprland.")
        .arg(m_session);
}

PlatformBackend *PlatformBackend::create(QObject *parent)
{
    const QString forced = qEnvironmentVariable("KEYBOARD_CHANGER_BACKEND").toLower();
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    const bool wayland = qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")
        || qEnvironmentVariableIsSet("WAYLAND_DISPLAY");

    if (forced == QLatin1String("x11"))
        return new X11Backend(parent);
    if (forced == QLatin1String("kde"))
        return new KdeBackend(parent);
    if (forced == QLatin1String("gnome"))
        return new GnomeBackend(parent);
    if (forced == QLatin1String("sway"))
        return new SwayBackend(parent);
    if (forced == QLatin1String("hyprland"))
        return new HyprlandBackend(parent);

    if (qEnvironmentVariableIsSet("HYPRLAND_INSTANCE_SIGNATURE"))
        return new HyprlandBackend(parent);
    if (qEnvironmentVariableIsSet("SWAYSOCK"))
        return new SwayBackend(parent);
    if (desktop.contains(QLatin1String("kde")))
        return new KdeBackend(parent);
    // GNOME manages input sources itself (also on X11), so it always needs the extension.
    if (desktop.contains(QLatin1String("gnome")))
        return new GnomeBackend(parent);
    if (!wayland && qEnvironmentVariableIsSet("DISPLAY"))
        return new X11Backend(parent);
    return new UnsupportedBackend(desktop.isEmpty() ? QStringLiteral("Wayland") : desktop, parent);
}
