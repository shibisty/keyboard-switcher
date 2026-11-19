#include "engine.h"

#include "platform/backend.h"
#include "presets.h"

#include <QCoreApplication>
#include <QSettings>

namespace {

constexpr int kPendingDomainMs = 1500;
constexpr int kHistorySize = 60;

QString kindToString(Rule::Kind k)
{
    switch (k) {
    case Rule::Kind::Domain: return QStringLiteral("domain");
    case Rule::Kind::Preset: return QStringLiteral("preset");
    default:                 return QStringLiteral("app");
    }
}

Rule::Kind kindFromString(const QString &s)
{
    if (s == QLatin1String("domain"))
        return Rule::Kind::Domain;
    if (s == QLatin1String("preset"))
        return Rule::Kind::Preset;
    return Rule::Kind::App;
}

} // namespace

LayoutEngine::LayoutEngine(PlatformBackend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
    load();
    connect(m_backend, &PlatformBackend::focusChanged, this, &LayoutEngine::onFocus);
}

LayoutEngine::~LayoutEngine()
{
    restore();
}

void LayoutEngine::onFocus(const FocusInfo &info)
{
    m_prevHandle = m_focus.handle;
    m_focus = info;
    m_focusKey = info.appIds.value(0);
    m_focusIsBrowser = Presets::instance().isBrowser(info.appIds);

    if (!m_focusKey.isEmpty()) {
        m_history.removeAll(m_focusKey);
        m_history.prepend(m_focusKey);
        if (m_history.size() > kHistorySize)
            m_history.removeLast();
    }
    // The extension's "window focused" message can race the OS focus event.
    if (m_focusIsBrowser && m_pendingTimer.isValid() && m_pendingTimer.elapsed() < kPendingDomainMs) {
        m_domainByBrowser.insert(m_focusKey, m_pendingDomain);
        m_pendingTimer.invalidate();
    }
    evaluate();
}

void LayoutEngine::onBrowserDomain(const QString &domain)
{
    m_lastBrowserReport = QDateTime::currentDateTime();
    emit browserReported();
    if (m_focusIsBrowser) {
        m_domainByBrowser.insert(m_focusKey, domain);
        evaluate();
    } else {
        m_pendingDomain = domain;
        m_pendingTimer.start();
    }
}

const Rule *LayoutEngine::match() const
{
    if (m_paused)
        return nullptr;

    if (m_focusIsBrowser) {
        const QString domain = m_domainByBrowser.value(m_focusKey);
        if (!domain.isEmpty()) {
            for (const Rule &r : m_rules) {
                if (r.enabled && r.kind == Rule::Kind::Domain
                    && (domain == r.key || domain.endsWith(QLatin1Char('.') + r.key)))
                    return &r;
            }
        }
    }
    for (const Rule &r : m_rules)
        if (r.enabled && r.kind == Rule::Kind::App && m_focus.appIds.contains(r.key))
            return &r;
    for (const Rule &r : m_rules) {
        if (!r.enabled || r.kind != Rule::Kind::Preset)
            continue;
        if (const PresetGroup *g = Presets::instance().group(r.key)) {
            for (const QString &id : m_focus.appIds)
                if (g->apps.contains(id))
                    return &r;
        }
    }
    return nullptr;
}

void LayoutEngine::evaluate()
{
    const Rule *rule = match();
    bool installed = false;
    if (rule)
        m_backend->describeLayout(rule->layout, &installed);

    if (rule && installed) {
        if (!m_active) {
            // Remember what the user had before the first matched window.
            const quint64 prev = (m_prevHandle && m_prevHandle != m_focus.handle) ? m_prevHandle : m_focus.handle;
            m_savedLayout = m_backend->currentLayout(prev);
            m_active = true;
        }
        m_backend->setLayout(rule->layout, m_focus.handle);
    } else {
        restore();
    }
}

void LayoutEngine::restore()
{
    if (!m_active)
        return;
    m_active = false;
    if (!m_savedLayout.isEmpty())
        m_backend->setLayout(m_savedLayout, m_focus.handle);
    m_savedLayout.clear();
}

bool LayoutEngine::hasRule(Rule::Kind kind, const QString &key) const
{
    for (const Rule &r : m_rules)
        if (r.kind == kind && r.key == key)
            return true;
    return false;
}

void LayoutEngine::addRule(const Rule &rule)
{
    bool replaced = false;
    for (Rule &r : m_rules) {
        if (r.kind == rule.kind && r.key == rule.key) {
            r = rule;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        m_rules.append(rule);
    save();
    emit rulesChanged();
    evaluate();
}

void LayoutEngine::removeRule(int index)
{
    if (index < 0 || index >= m_rules.size())
        return;
    m_rules.removeAt(index);
    save();
    emit rulesChanged();
    evaluate();
}

void LayoutEngine::setRuleEnabled(int index, bool enabled)
{
    if (index < 0 || index >= m_rules.size() || m_rules[index].enabled == enabled)
        return;
    m_rules[index].enabled = enabled;
    save();
    emit rulesChanged();
    evaluate();
}

void LayoutEngine::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    QSettings().setValue(QStringLiteral("paused"), m_paused);
    emit pausedChanged(m_paused);
    evaluate();
}

int LayoutEngine::activeRuleCount() const
{
    int n = 0;
    for (const Rule &r : m_rules)
        n += r.enabled ? 1 : 0;
    return n;
}

QStringList LayoutEngine::knownApps() const
{
    QStringList apps = m_backend->runningApps();
    apps += m_history;
    apps.removeDuplicates();
    apps.sort(Qt::CaseInsensitive);
    return apps;
}

void LayoutEngine::load()
{
    QSettings s;
    m_paused = s.value(QStringLiteral("paused"), false).toBool();
    const int n = s.beginReadArray(QStringLiteral("rules"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        Rule r;
        if (s.contains(QStringLiteral("process"))) { // format of version 1.0
            r.kind = Rule::Kind::App;
            r.key = s.value(QStringLiteral("process")).toString();
        } else {
            r.kind = kindFromString(s.value(QStringLiteral("kind")).toString());
            r.key = s.value(QStringLiteral("key")).toString();
        }
        r.key = r.key.trimmed().toLower();
        r.layout = s.value(QStringLiteral("layout")).toString();
        r.enabled = s.value(QStringLiteral("enabled"), true).toBool();
        if (!r.key.isEmpty() && !r.layout.isEmpty())
            m_rules.append(r);
    }
    s.endArray();
}

void LayoutEngine::save() const
{
    QSettings s;
    s.remove(QStringLiteral("rules"));
    s.beginWriteArray(QStringLiteral("rules"), int(m_rules.size()));
    for (int i = 0; i < m_rules.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("kind"), kindToString(m_rules[i].kind));
        s.setValue(QStringLiteral("key"), m_rules[i].key);
        s.setValue(QStringLiteral("layout"), m_rules[i].layout);
        s.setValue(QStringLiteral("enabled"), m_rules[i].enabled);
    }
    s.endArray();
}
