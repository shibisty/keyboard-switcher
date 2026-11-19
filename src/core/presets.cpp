#include "presets.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QLatin1String platformKey()
{
#if defined(Q_OS_WIN)
    return QLatin1String("windows");
#elif defined(Q_OS_MACOS)
    return QLatin1String("macos");
#else
    return QLatin1String("linux");
#endif
}

QSet<QString> toSet(const QJsonValue &v)
{
    QSet<QString> set;
    const QJsonArray arr = v.toArray();
    for (const QJsonValue &item : arr)
        set.insert(item.toString().toLower());
    return set;
}

} // namespace

const Presets &Presets::instance()
{
    static const Presets presets;
    return presets;
}

Presets::Presets()
{
    QFile file(QStringLiteral(":/presets.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    const QJsonArray categories = root.value(QLatin1String("categories")).toArray();
    for (const QJsonValue &c : categories) {
        const QJsonObject co = c.toObject();
        PresetCategory cat;
        cat.id = co.value(QLatin1String("id")).toString();
        cat.nameEn = co.value(QLatin1String("name")).toObject().value(QLatin1String("en")).toString();

        PresetGroup whole;  // legacy rule target: every application of the category
        whole.id = cat.id;
        whole.nameEn = cat.nameEn;

        const QJsonArray apps = co.value(QLatin1String("apps")).toArray();
        for (const QJsonValue &av : apps) {
            const QJsonObject ao = av.toObject();
            PresetGroup g;
            g.id = ao.value(QLatin1String("id")).toString();
            g.nameEn = ao.value(QLatin1String("name")).toString();
            g.category = cat.id;
            g.apps = toSet(ao.value(platformKey()));
            if (g.id.isEmpty() || g.apps.isEmpty())
                continue;  // not available on this OS
            whole.apps.unite(g.apps);
            cat.groupIds << g.id;
            m_groups.append(g);
        }
        if (cat.id.isEmpty() || cat.groupIds.isEmpty())
            continue;
        m_groups.append(whole);
        m_categories.append(cat);
    }
    m_browsers = toSet(root.value(QLatin1String("browsers")).toObject().value(platformKey()));
}

const PresetGroup *Presets::group(const QString &id) const
{
    for (const PresetGroup &g : m_groups)
        if (g.id == id)
            return &g;
    return nullptr;
}

bool Presets::isBrowser(const QStringList &appIds) const
{
    for (const QString &id : appIds)
        if (m_browsers.contains(id))
            return true;
    return false;
}
