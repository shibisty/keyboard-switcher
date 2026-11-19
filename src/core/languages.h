#pragma once

#include <QList>
#include <QString>

namespace Languages {

struct Info {
    QString code;  // "ru", "zh_CN", ...
    QString name;  // autonym
    bool rtl;
};

const QList<Info> &all();

// Picks the first language of the system UI list that we ship, otherwise "en".
QString detectSystem();

// "auto" / unknown -> detectSystem(); known code -> itself; aliases -> their base language.
QString resolve(const QString &setting);

bool isRtl(const QString &code);

}  // namespace Languages
