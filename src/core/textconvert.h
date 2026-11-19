#pragma once

#include <QHash>
#include <QString>

// What one physical key types in a layout. Level bits: 1 = Shift, 2 = AltGr.
// Keys are identified by a platform key code (scan code / evdev code), the same for every layout.
struct KeyLevels {
    QString chars[4];
};
using KeyMap = QHash<quint32, KeyLevels>;

namespace TextConvert {

// Re-types `text` as if the same physical keys had been pressed in `to` instead of `from`:
// "руддщ" (from ru) -> "hello" (to us). Characters the source layout cannot type stay as they are.
QString convert(const QString &text, const KeyMap &from, const KeyMap &to);

}  // namespace TextConvert
