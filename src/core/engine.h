#pragma once

#include "types.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>

class PlatformBackend;

// Platform-independent switching logic.
//
// Priority when a window gets focus:
//   1. website rule  (browser in front + domain reported by the browser extension)
//   2. application rule
//   3. preset group  (e.g. "Code editors & IDEs")
// When focus moves to a window without a matching rule, the layout the user had
// before entering the first matched window is restored.
class LayoutEngine : public QObject
{
    Q_OBJECT
public:
    explicit LayoutEngine(PlatformBackend *backend, QObject *parent = nullptr);
    ~LayoutEngine() override;

    PlatformBackend *backend() const { return m_backend; }
    const FocusInfo &focus() const { return m_focus; }

    const QList<Rule> &rules() const { return m_rules; }
    void addRule(const Rule &rule);  // replaces a rule with the same kind+key
    void removeRule(int index);
    void setRuleEnabled(int index, bool enabled);
    bool hasRule(Rule::Kind kind, const QString &key) const;

    bool isPaused() const { return m_paused; }
    void setPaused(bool paused);
    int activeRuleCount() const;

    // Apps with windows now + apps focused during this session (Wayland can't enumerate windows).
    QStringList knownApps() const;

    void onBrowserDomain(const QString &domain);
    QDateTime lastBrowserReport() const { return m_lastBrowserReport; }

signals:
    void rulesChanged();
    void pausedChanged(bool paused);
    void browserReported();

private:
    void onFocus(const FocusInfo &info);
    void evaluate();
    const Rule *match() const;
    void restore();
    void load();
    void save() const;

    PlatformBackend *m_backend;
    QList<Rule> m_rules;
    bool m_paused = false;

    FocusInfo m_focus;
    QString m_focusKey;          // primary id of the focused app
    bool m_focusIsBrowser = false;
    quint64 m_prevHandle = 0;

    QHash<QString, QString> m_domainByBrowser;  // browser id -> active tab domain
    QString m_pendingDomain;                    // arrived just before the browser got focus
    QElapsedTimer m_pendingTimer;
    QDateTime m_lastBrowserReport;

    bool m_active = false;       // a rule is applied right now
    QString m_savedLayout;       // restored when leaving matched windows
    QStringList m_history;
};
