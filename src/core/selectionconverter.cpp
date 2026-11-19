#include "selectionconverter.h"

#include "core/engine.h"
#include "core/presets.h"
#include "platform/backend.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QUuid>

namespace {
constexpr int kPollMs = 25;
constexpr int kModifierTimeoutMs = 700;   // the user may still hold the hotkey
constexpr int kCopyTimeoutMs = 400;
constexpr int kPasteSettleMs = 60;        // let the clipboard owner change settle before Ctrl+V
constexpr int kRestoreDelayMs = 350;      // the target application reads the clipboard asynchronously

std::unique_ptr<QMimeData> cloneMime(const QMimeData *src)
{
    auto copy = std::make_unique<QMimeData>();
    if (src) {
        const QStringList formats = src->formats();
        for (const QString &f : formats)
            copy->setData(f, src->data(f));
    }
    return copy;
}

bool appInPreset(const QStringList &ids, const QString &presetId)
{
    const PresetGroup *g = Presets::instance().group(presetId);
    if (!g)
        return false;
    for (const QString &id : ids)
        if (g->apps.contains(id))
            return true;
    return false;
}
}  // namespace

SelectionConverter::SelectionConverter(PlatformBackend *backend, LayoutEngine *engine, QObject *parent)
    : QObject(parent), m_backend(backend), m_engine(engine)
{
    m_timer.setSingleShot(true);
    connect(m_backend, &PlatformBackend::convertHotkeyPressed, this, &SelectionConverter::onHotkey);
    connect(m_backend, &PlatformBackend::layoutSwitchedByUser, this, &SelectionConverter::onManualSwitch);
}

void SelectionConverter::splitHotkey(const QKeySequence &sequence, int *key, int *mods)
{
    *key = 0;
    *mods = 0;
    if (sequence.isEmpty())
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QKeyCombination combo = sequence[0];
    *key = int(combo.key());
    *mods = int(combo.keyboardModifiers().toInt());
#else
    const int value = sequence[0];
    *mods = value & int(Qt::KeyboardModifierMask);
    *key = value & ~int(Qt::KeyboardModifierMask);
#endif
}

bool SelectionConverter::supported() const
{
    return m_backend->canConvertText();
}

bool SelectionConverter::configure(int hotkeyKey, int hotkeyMods, bool onSwitch)
{
    if (!supported()) {
        m_backend->setConvertHotkey(0, 0);
        m_backend->setWatchManualSwitch(false);
        return false;
    }
    m_backend->setWatchManualSwitch(onSwitch);
    return m_backend->setConvertHotkey(hotkeyKey, hotkeyMods) || hotkeyKey == 0;
}

const KeyMap *SelectionConverter::mapFor(const QString &layoutId)
{
    auto it = m_maps.find(layoutId);
    if (it == m_maps.end()) {
        KeyMap map;
        if (!m_backend->keyMap(layoutId, &map))
            return nullptr;
        it = m_maps.insert(layoutId, map);
    }
    return &it.value();
}

void SelectionConverter::onHotkey()
{
    const QList<LayoutInfo> all = m_backend->layouts();
    if (all.size() < 2)
        return;
    const QString from = m_backend->currentLayout(m_engine->focus().handle);
    int index = -1;
    for (int i = 0; i < all.size(); ++i)
        if (all[i].id == from)
            index = i;
    // With two layouts: the other one. With more: the next one, like the system switch hotkey does.
    const QString to = all[(index + 1) % all.size()].id;
    start(from, to, Trigger::Hotkey);
}

void SelectionConverter::onManualSwitch(const QString &from, const QString &to)
{
    if (!from.isEmpty() && !to.isEmpty() && from != to)
        start(from, to, Trigger::LayoutSwitch);
}

bool SelectionConverter::blockedHere(Trigger trigger) const
{
    const QStringList &ids = m_engine->focus().appIds;
    // Ctrl+C interrupts the running program in a terminal: never inject it there.
    if (appInPreset(ids, QStringLiteral("terminals")))
        return true;
    // Editors copy the whole line when nothing is selected, so an automatic Ctrl+C would paste a line.
    if (trigger == Trigger::LayoutSwitch && appInPreset(ids, QStringLiteral("editors")))
        return true;
    return false;
}

void SelectionConverter::start(const QString &from, const QString &to, Trigger trigger)
{
    if (m_busy || !supported() || blockedHere(trigger))
        return;
    if (!mapFor(from) || !mapFor(to))
        return;
    m_busy = true;
    m_trigger = trigger;
    m_from = from;
    m_to = to;
    m_handle = m_engine->focus().handle;
    m_waited = 0;
    waitForModifiers();
}

void SelectionConverter::waitForModifiers()
{
    if (!m_backend->modifiersReleased()) {
        if (m_waited >= kModifierTimeoutMs) {
            m_busy = false;
            return;
        }
        m_waited += kPollMs;
        QTimer::singleShot(kPollMs, this, &SelectionConverter::waitForModifiers);
        return;
    }
    copySelection();
}

void SelectionConverter::copySelection()
{
    QClipboard *cb = QGuiApplication::clipboard();
    m_saved = cloneMime(cb->mimeData());
    m_sentinel = QStringLiteral("kc-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    cb->setText(m_sentinel);
    if (!m_backend->sendCopy()) {
        finish(true);
        return;
    }
    m_waited = 0;
    QTimer::singleShot(kPollMs, this, &SelectionConverter::pollCopy);
}

void SelectionConverter::pollCopy()
{
    QClipboard *cb = QGuiApplication::clipboard();
    const QString text = cb->text();
    if (text == m_sentinel) {
        m_waited += kPollMs;
        if (m_waited >= kCopyTimeoutMs) {
            finish(true);  // nothing was selected
            return;
        }
        QTimer::singleShot(kPollMs, this, &SelectionConverter::pollCopy);
        return;
    }
    const QString converted = TextConvert::convert(text, *mapFor(m_from), *mapFor(m_to));
    if (text.isEmpty() || converted == text) {
        finish(true);
        return;
    }
    cb->setText(converted);
    QTimer::singleShot(kPasteSettleMs, this, [this] {
        m_backend->sendPaste();
        if (m_trigger == Trigger::Hotkey)
            m_backend->setLayout(m_to, m_handle);
        finish(false);
    });
}

// `restoreOnly`: nothing was pasted, so the clipboard can go back immediately.
void SelectionConverter::finish(bool restoreOnly)
{
    if (restoreOnly) {
        restoreClipboard();
        return;
    }
    QTimer::singleShot(kRestoreDelayMs, this, &SelectionConverter::restoreClipboard);
}

void SelectionConverter::restoreClipboard()
{
    QClipboard *cb = QGuiApplication::clipboard();
    if (m_saved && !m_saved->formats().isEmpty())
        cb->setMimeData(m_saved.release());
    else
        cb->clear();
    m_saved.reset();
    m_busy = false;
}
