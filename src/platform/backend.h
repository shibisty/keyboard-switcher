#pragma once

#include "core/textconvert.h"
#include "core/types.h"

#include <QList>
#include <QObject>

// Everything that differs between operating systems / desktop environments.
class PlatformBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~PlatformBackend() override = default;

    // Picks the right implementation for the current OS / session.
    static PlatformBackend *create(QObject *parent);

    virtual QString name() const = 0;                    // "Windows", "KDE Plasma (Wayland)", ...
    virtual QString problem() const { return {}; }       // non-empty when switching can't work yet
    virtual void start() = 0;                            // begin emitting focusChanged()

    virtual QList<LayoutInfo> layouts() const = 0;
    // Layout description for a stored id; `installed` tells whether it can be activated now.
    virtual LayoutInfo describeLayout(const QString &id, bool *installed) const;
    // Active layout. `handle` is the window whose layout matters (Windows only).
    virtual QString currentLayout(quint64 handle) const = 0;
    virtual void setLayout(const QString &id, quint64 handle) = 0;

    // Identifiers of applications that currently have windows (for the "add rule" list).
    virtual QStringList runningApps() const { return {}; }

    // Bring our process to the front before showing the settings window (macOS menu-bar apps).
    virtual void activateApp() {}

    virtual bool autostartEnabled() const = 0;
    virtual AutostartResult setAutostart(bool enabled) = 0;

    // --- Converting selected text typed in the wrong layout (optional per platform) ---------
    // True when this session can copy/paste into other applications and register a hotkey.
    virtual bool canConvertText() const { return false; }
    // What each physical key types in a layout (see TextConvert).
    virtual bool keyMap(const QString &layoutId, KeyMap *out) const
    {
        Q_UNUSED(layoutId);
        Q_UNUSED(out);
        return false;
    }
    // Press Ctrl+C / Ctrl+V (physical keys, so they work in any active layout) in the focused window.
    virtual bool sendCopy() { return false; }
    virtual bool sendPaste() { return false; }
    // True when no Shift/Ctrl/Alt/Win key is held (injected shortcuts would otherwise be mangled).
    virtual bool modifiersReleased() const { return true; }
    // qtKey = Qt::Key, qtMods = Qt::KeyboardModifier bits. qtKey 0 removes the hotkey.
    virtual bool setConvertHotkey(int qtKey, int qtMods)
    {
        Q_UNUSED(qtKey);
        Q_UNUSED(qtMods);
        return false;
    }
    // Report layout switches made by the user (not by this program) through layoutSwitchedByUser().
    virtual void setWatchManualSwitch(bool on) { Q_UNUSED(on); }

signals:
    void focusChanged(const FocusInfo &info);
    void problemChanged();
    void convertHotkeyPressed();
    void layoutSwitchedByUser(const QString &fromLayout, const QString &toLayout);
};
