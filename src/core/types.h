#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

struct Rule {
    enum class Kind { App, Domain, Preset };
    Kind kind = Kind::App;
    QString key;     // lower-case: app id ("code.exe", "com.apple.terminal"), domain ("github.com") or preset id
    QString layout;  // platform layout id (see LayoutInfo::id)
    bool enabled = true;
};

// Window that just received focus.
struct FocusInfo {
    QStringList appIds;  // lower-case identifiers: exe name, bundle id, WM_CLASS, Wayland app_id
    QString title;
    quint64 handle = 0;  // native window handle where it matters (Windows HWND), else 0
};
Q_DECLARE_METATYPE(FocusInfo)

struct LayoutInfo {
    QString id;    // stable id stored in settings: Windows HKL hex, macOS input source id, Linux "us" / "us(dvorak)"
    QString code;  // short label, "EN"
    QString name;  // human readable, "English (US)"
    QString lang;  // ISO 639-1 if known, "en"
};

enum class AutostartResult { Ok, DisabledByUser, DisabledByPolicy, Failed };
