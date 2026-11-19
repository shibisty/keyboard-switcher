#pragma once

#include "core/textconvert.h"

#include <QHash>
#include <QMimeData>
#include <QKeySequence>
#include <QObject>
#include <QTimer>

#include <memory>

class LayoutEngine;
class PlatformBackend;

// Re-types the selected text of the focused application in another layout ("руддщ" -> "hello"):
// copies the selection, converts it with the physical key maps of the two layouts, pastes it
// back and restores the clipboard. Triggered by a hotkey or after a manual layout switch.
class SelectionConverter : public QObject
{
    Q_OBJECT
public:
    // First key combination of a sequence as Qt::Key + Qt::KeyboardModifier bits (key 0 when empty).
    static void splitHotkey(const QKeySequence &sequence, int *key, int *mods);

    SelectionConverter(PlatformBackend *backend, LayoutEngine *engine, QObject *parent = nullptr);

    // Qt key + modifiers of the hotkey (key 0 = none); `onSwitch` converts after manual layout switches.
    // Returns false when the hotkey could not be registered (taken by another program / unsupported).
    bool configure(int hotkeyKey, int hotkeyMods, bool onSwitch);
    bool supported() const;

private:
    enum class Trigger { Hotkey, LayoutSwitch };

    void onHotkey();
    void onManualSwitch(const QString &from, const QString &to);
    void start(const QString &from, const QString &to, Trigger trigger);
    bool blockedHere(Trigger trigger) const;
    void waitForModifiers();
    void copySelection();
    void pollCopy();
    void finish(bool restoreOnly);
    void restoreClipboard();
    const KeyMap *mapFor(const QString &layoutId);

    PlatformBackend *m_backend;
    LayoutEngine *m_engine;
    QHash<QString, KeyMap> m_maps;

    bool m_busy = false;
    Trigger m_trigger = Trigger::Hotkey;
    QString m_from, m_to;
    quint64 m_handle = 0;
    QString m_sentinel;
    int m_waited = 0;
    std::unique_ptr<QMimeData> m_saved;
    QTimer m_timer;
};
