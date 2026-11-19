#include "textconvert.h"

#include <QChar>

namespace TextConvert {

namespace {
struct Slot {
    quint32 key;
    int level;
};

// Several keys may type the same character (numpad, duplicates); the lowest key code wins
// so that the main block is preferred over rarely used keys.
QHash<QString, Slot> reverse(const KeyMap &map)
{
    QHash<QString, Slot> r;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        for (int level = 0; level < 4; ++level) {
            const QString &c = it.value().chars[level];
            if (c.isEmpty())
                continue;
            auto found = r.find(c);
            if (found == r.end() || it.key() < found->key || (it.key() == found->key && level < found->level))
                r.insert(c, {it.key(), level});
        }
    }
    return r;
}

QString typed(const KeyMap &map, Slot s)
{
    const auto it = map.constFind(s.key);
    if (it == map.constEnd())
        return {};
    const KeyLevels &k = it.value();
    if (!k.chars[s.level].isEmpty())
        return k.chars[s.level];
    // The target layout may lack AltGr symbols on this key: fall back to the same Shift state.
    const int shifted = s.level & 1;
    return k.chars[shifted];
}
}  // namespace

QString convert(const QString &text, const KeyMap &from, const KeyMap &to)
{
    const QHash<QString, Slot> src = reverse(from);
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        // Keep surrogate pairs together.
        QString ch = text.mid(i, QChar::isHighSurrogate(text[i].unicode()) && i + 1 < text.size() ? 2 : 1);
        if (ch.size() == 2)
            ++i;
        const auto it = src.constFind(ch);
        if (it == src.constEnd()) {
            out += ch;
            continue;
        }
        const QString t = typed(to, it.value());
        out += t.isEmpty() ? ch : t;
    }
    return out;
}

}  // namespace TextConvert
