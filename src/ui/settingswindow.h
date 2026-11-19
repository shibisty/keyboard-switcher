#pragma once

#include <QList>
#include <QWidget>

class BrowserBridge;
class LayoutEngine;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

class SettingsWindow : public QWidget
{
    Q_OBJECT
public:
    SettingsWindow(LayoutEngine *engine, BrowserBridge *bridge, QWidget *parent = nullptr);

signals:
    void languageRequested(const QString &code);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();
    void refreshIcons();
    void rebuildRules();
    void updateBrowserStatus();
    void updateProblem();
    void showAddPanel(bool show);
    void showPresetPanel(bool show);
    void rebuildPresetChoices();
    void refreshApps();
    void fillLayouts(QComboBox *combo, bool preferEnglish);
    void commitAdd();
    void commitPresets();
    void onAutostartToggled(bool on);
    QString describeLayout(const QString &id) const;
    QString language() const;

    LayoutEngine *m_engine;
    BrowserBridge *m_bridge;

    QLabel *m_problem = nullptr;
    QLabel *m_rulesTitle = nullptr;
    QToolButton *m_presetButton = nullptr;
    QToolButton *m_addButton = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_rulesHost = nullptr;
    QVBoxLayout *m_rulesLayout = nullptr;

    // add rule
    QFrame *m_addPanel = nullptr;
    QLabel *m_typeLabel = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QLabel *m_targetLabel = nullptr;
    QStackedWidget *m_targetStack = nullptr;
    QComboBox *m_appCombo = nullptr;
    QToolButton *m_refreshButton = nullptr;
    QLineEdit *m_domainEdit = nullptr;
    QLabel *m_layoutLabel = nullptr;
    QComboBox *m_layoutCombo = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    // presets
    QFrame *m_presetPanel = nullptr;
    QLabel *m_presetHint = nullptr;
    QScrollArea *m_presetScroll = nullptr;
    QVBoxLayout *m_presetChecks = nullptr;
    void updatePresetOk();
    QList<QCheckBox *> m_presetBoxes;
    QLabel *m_presetLayoutLabel = nullptr;
    QComboBox *m_presetLayoutCombo = nullptr;
    QPushButton *m_presetOk = nullptr;
    QPushButton *m_presetCancel = nullptr;

    // settings
    QLabel *m_settingsTitle = nullptr;
    QCheckBox *m_autostart = nullptr;
    QLabel *m_langLabel = nullptr;
    QComboBox *m_langCombo = nullptr;
    QLabel *m_status = nullptr;

    // browser
    QLabel *m_browserTitle = nullptr;
    QLabel *m_browserStatus = nullptr;
};
