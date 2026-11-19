#include "languages.h"

#include "languages_gen.h"

#include <QLocale>
#include <QStringList>

namespace Languages {

const QList<Info> &all()
{
    static const QList<Info> list = [] {
        QList<Info> l;
        for (const LangRow &r : kLangRows)
            l.append({QString::fromLatin1(r.code), QString::fromUtf8(r.name), r.rtl});
        return l;
    }();
    return list;
}

static bool known(const QString &code)
{
    for (const Info &i : all())
        if (i.code == code)
            return true;
    return false;
}

static QString alias(const QString &code)
{
    for (const AliasRow &a : kAliasRows)
        if (code == QLatin1String(a.from))
            return QString::fromLatin1(a.to);
    return {};
}

// "zh-Hant-TW", "pt_BR", "sr-Latn", "tl" ... -> one of our codes, or an empty string.
static QString fromTag(const QString &tagIn)
{
    const QString tag = QString(tagIn).replace(QLatin1Char('_'), QLatin1Char('-')).toLower();
    const QStringList parts = tag.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};
    QString base = parts.first();

    if (base == QLatin1String("zh") || base == QLatin1String("yue") || base == QLatin1String("cmn")) {
        const bool trad = parts.contains(QLatin1String("hant")) || parts.contains(QLatin1String("tw"))
                          || parts.contains(QLatin1String("hk")) || parts.contains(QLatin1String("mo"));
        return trad ? QStringLiteral("zh_TW") : QStringLiteral("zh_CN");
    }
    static const struct { const char *from; const char *to; } rename[] = {
        {"tl", "fil"}, {"jw", "jv"}, {"sr", "sh"}, {"hr", "sh"}, {"bs", "sh"}, {"cnr", "sh"},
        {"zlm", "ms"}, {"zsm", "ms"}, {"prs", "fa"}, {"pes", "fa"}, {"arb", "ar"}, {"uzn", "uz"},
        {"azj", "az"}, {"gom", "mr"}, {"ory", "or"}, {"orm", "om"}, {"mlg", "mg"}};
    for (const auto &r : rename)
        if (base == QLatin1String(r.from))
            base = QString::fromLatin1(r.to);

    if (known(base))
        return base;
    const QString a = alias(base);
    return a.isEmpty() ? QString() : a;
}

QString detectSystem()
{
    const QLocale sys = QLocale::system();
    QStringList tags = sys.uiLanguages();
    tags.append(sys.name());
    for (const QString &tag : std::as_const(tags)) {
        const QString code = fromTag(tag);
        if (!code.isEmpty())
            return code;
    }
    return QStringLiteral("en");
}

QString resolve(const QString &setting)
{
    if (setting.isEmpty() || setting == QLatin1String("auto"))
        return detectSystem();
    if (known(setting))
        return setting;
    const QString a = alias(setting);
    return a.isEmpty() ? detectSystem() : a;
}

bool isRtl(const QString &code)
{
    for (const Info &i : all())
        if (i.code == code)
            return i.rtl;
    return false;
}

}  // namespace Languages
