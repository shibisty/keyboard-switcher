#pragma once

#include <QCoreApplication>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// One preset = one application (category non-empty) or, for rules saved by older versions,
// a whole category (category empty, id = category id, hidden from the add panel).
struct PresetGroup {
    QString id;
    QString nameEn;
    QString category;    // category id for single applications
    QSet<QString> apps;  // identifiers for the current platform, lower-case

    bool isApplication() const { return !category.isEmpty(); }
    // Application names are proper nouns; category names go through the "Presets" context.
    QString name() const
    {
        return isApplication() ? nameEn : QCoreApplication::translate("Presets", nameEn.toUtf8().constData());
    }
};

struct PresetCategory {
    QString id;
    QString nameEn;
    QStringList groupIds;  // applications available on this platform
    QString name() const { return QCoreApplication::translate("Presets", nameEn.toUtf8().constData()); }
};

// Built-in app lists (res/presets.json), filtered for the current OS.
class Presets
{
public:
    static const Presets &instance();

    const QList<PresetCategory> &categories() const { return m_categories; }
    const PresetGroup *group(const QString &id) const;
    bool isBrowser(const QStringList &appIds) const;

private:
    Presets();
    QList<PresetGroup> m_groups;
    QList<PresetCategory> m_categories;
    QSet<QString> m_browsers;
};
