#include "backend.h"

LayoutInfo PlatformBackend::describeLayout(const QString &id, bool *installed) const
{
    const QList<LayoutInfo> all = layouts();
    for (const LayoutInfo &l : all) {
        if (l.id == id) {
            if (installed)
                *installed = true;
            return l;
        }
    }
    if (installed)
        *installed = false;
    LayoutInfo info;
    info.id = id;
    info.code = id.left(2).toUpper();
    info.name = id;
    return info;
}
