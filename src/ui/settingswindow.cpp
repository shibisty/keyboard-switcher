#include "settingswindow.h"

#include "core/browserbridge.h"
#include "core/engine.h"
#include "core/languages.h"
#include "core/presets.h"
#include "core/selectionconverter.h"
#include "icons.h"
#include "platform/backend.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <memory>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kMaxRulesHeight = 280;

QToolButton *makeIconButton(QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setAutoRaise(true);
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(30, 30);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QLabel *makeSectionTitle(QWidget *parent)
{
    auto *l = new QLabel(parent);
    QFont f = l->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.1);
    l->setFont(f);
    return l;
}

QFrame *makeSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

QFrame *makePanel(QWidget *parent)
{
    auto *panel = new QFrame(parent);
    panel->setFrameShape(QFrame::StyledPanel);
    panel->hide();
    return panel;
}

// "https://www.GitHub.com/foo" -> "github.com"
QString normalizeDomain(QString text)
{
    text = text.trimmed().toLower();
    if (text.contains(QLatin1Char('/')) || text.contains(QLatin1Char(':')))
        text = QUrl::fromUserInput(text).host();
    if (text.startsWith(QLatin1String("*.")))
        text.remove(0, 2);
    if (text.startsWith(QLatin1String("www.")))
        text.remove(0, 4);
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9-]+(\\.[a-z0-9-]+)*$"));
    return valid.match(text).hasMatch() ? text : QString();
}

} // namespace

SettingsWindow::SettingsWindow(LayoutEngine *engine, BrowserBridge *bridge, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_engine(engine)
    , m_bridge(bridge)
{
    setAttribute(Qt::WA_DeleteOnClose);

    auto *root = new QVBoxLayout(this);
    root->setSizeConstraint(QLayout::SetFixedSize); // window always fits its content
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    m_problem = new QLabel(this);
    m_problem->setWordWrap(true);
    m_problem->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_problem->setStyleSheet(QStringLiteral("QLabel { color: #d9822b; }"));
    root->addWidget(m_problem);

    // --- Rules header ---------------------------------------------------
    auto *header = new QHBoxLayout;
    m_rulesTitle = makeSectionTitle(this);
    m_presetButton = makeIconButton(this);
    m_addButton = makeIconButton(this);
    header->addWidget(m_rulesTitle);
    header->addStretch();
    header->addWidget(m_presetButton);
    header->addWidget(m_addButton);
    root->addLayout(header);

    // --- Rules list -----------------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setMinimumWidth(440);
    m_rulesHost = new QWidget(m_scroll);
    m_rulesLayout = new QVBoxLayout(m_rulesHost);
    m_rulesLayout->setContentsMargins(0, 0, 0, 0);
    m_rulesLayout->setSpacing(2);
    m_scroll->setWidget(m_rulesHost);
    root->addWidget(m_scroll);

    // --- Add-rule panel -------------------------------------------------
    m_addPanel = makePanel(this);
    auto *grid = new QGridLayout(m_addPanel);
    grid->setContentsMargins(10, 10, 10, 10);
    grid->setHorizontalSpacing(8);

    m_typeLabel = new QLabel(m_addPanel);
    m_typeCombo = new QComboBox(m_addPanel);
    m_targetLabel = new QLabel(m_addPanel);
    m_targetStack = new QStackedWidget(m_addPanel);

    auto *appPage = new QWidget(m_targetStack);
    auto *appRow = new QHBoxLayout(appPage);
    appRow->setContentsMargins(0, 0, 0, 0);
    m_appCombo = new QComboBox(appPage);
    m_appCombo->setEditable(true);
    m_appCombo->setInsertPolicy(QComboBox::NoInsert);
    m_appCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_refreshButton = makeIconButton(appPage);
    appRow->addWidget(m_appCombo);
    appRow->addWidget(m_refreshButton);
    m_targetStack->addWidget(appPage);

    m_domainEdit = new QLineEdit(m_targetStack);
    m_domainEdit->setPlaceholderText(QStringLiteral("github.com"));
    m_targetStack->addWidget(m_domainEdit);

    m_layoutLabel = new QLabel(m_addPanel);
    m_layoutCombo = new QComboBox(m_addPanel);
    m_okButton = new QPushButton(m_addPanel);
    m_okButton->setDefault(true);
    m_cancelButton = new QPushButton(m_addPanel);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_okButton);
    buttons->addWidget(m_cancelButton);

    grid->addWidget(m_typeLabel, 0, 0);
    grid->addWidget(m_typeCombo, 0, 1);
    grid->addWidget(m_targetLabel, 1, 0);
    grid->addWidget(m_targetStack, 1, 1);
    grid->addWidget(m_layoutLabel, 2, 0);
    grid->addWidget(m_layoutCombo, 2, 1);
    grid->addLayout(buttons, 3, 0, 1, 2);
    grid->setColumnStretch(1, 1);
    root->addWidget(m_addPanel);

    // --- Preset panel ---------------------------------------------------
    m_presetPanel = makePanel(this);
    auto *pv = new QVBoxLayout(m_presetPanel);
    pv->setContentsMargins(10, 10, 10, 10);
    m_presetHint = new QLabel(m_presetPanel);
    m_presetHint->setWordWrap(true);
    pv->addWidget(m_presetHint);
    m_presetScroll = new QScrollArea(m_presetPanel);
    m_presetScroll->setFrameShape(QFrame::NoFrame);
    m_presetScroll->setWidgetResizable(true);
    m_presetScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_presetScroll->setFixedHeight(230);
    auto *presetHost = new QWidget(m_presetScroll);
    m_presetChecks = new QVBoxLayout(presetHost);
    m_presetChecks->setContentsMargins(0, 0, 6, 0);
    m_presetChecks->setSpacing(4);
    m_presetScroll->setWidget(presetHost);
    pv->addWidget(m_presetScroll);
    auto *presetLayoutRow = new QHBoxLayout;
    m_presetLayoutLabel = new QLabel(m_presetPanel);
    m_presetLayoutCombo = new QComboBox(m_presetPanel);
    presetLayoutRow->addWidget(m_presetLayoutLabel);
    presetLayoutRow->addWidget(m_presetLayoutCombo, 1);
    pv->addLayout(presetLayoutRow);
    auto *presetButtons = new QHBoxLayout;
    m_presetOk = new QPushButton(m_presetPanel);
    m_presetOk->setDefault(true);
    m_presetCancel = new QPushButton(m_presetPanel);
    presetButtons->addStretch();
    presetButtons->addWidget(m_presetOk);
    presetButtons->addWidget(m_presetCancel);
    pv->addLayout(presetButtons);
    root->addWidget(m_presetPanel);

    root->addWidget(makeSeparator(this));

    // --- Global settings ------------------------------------------------
    m_settingsTitle = makeSectionTitle(this);
    root->addWidget(m_settingsTitle);
    m_autostart = new QCheckBox(this);
    m_autostart->setChecked(m_engine->backend()->autostartEnabled());
    root->addWidget(m_autostart);

    auto *langRow = new QHBoxLayout;
    m_langLabel = new QLabel(this);
    m_langCombo = new QComboBox(this);
    m_langCombo->setMaxVisibleItems(15);
    m_langCombo->addItem(QString(), QStringLiteral("auto"));  // text set in retranslate()
    for (const Languages::Info &l : Languages::all())
        m_langCombo->addItem(l.name, l.code);
    m_langCombo->setCurrentIndex(qMax(0, m_langCombo->findData(language())));
    langRow->addWidget(m_langLabel);
    langRow->addWidget(m_langCombo);
    langRow->addStretch();
    root->addLayout(langRow);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->hide();
    root->addWidget(m_status);

    // --- Text conversion ------------------------------------------------
    root->addWidget(makeSeparator(this));
    m_convertTitle = makeSectionTitle(this);
    root->addWidget(m_convertTitle);
    auto *convertRow = new QHBoxLayout;
    m_convertLabel = new QLabel(this);
    m_convertKey = new QKeySequenceEdit(
        QKeySequence(QSettings().value(QStringLiteral("convertHotkey"), QStringLiteral("Pause")).toString()), this);
    m_convertClear = new QToolButton(this);
    m_convertClear->setText(QStringLiteral("\u00d7"));
    m_convertClear->setAutoRaise(true);
    convertRow->addWidget(m_convertLabel);
    convertRow->addWidget(m_convertKey, 1);
    convertRow->addWidget(m_convertClear);
    root->addLayout(convertRow);
    m_convertOnSwitch = new QCheckBox(this);
    m_convertOnSwitch->setChecked(QSettings().value(QStringLiteral("convertOnSwitch"), false).toBool());
    root->addWidget(m_convertOnSwitch);
    m_convertNote = new QLabel(this);
    m_convertNote->setWordWrap(true);
    root->addWidget(m_convertNote);
    const bool canConvert = m_engine->backend()->canConvertText();
    m_convertLabel->setVisible(canConvert);
    m_convertKey->setVisible(canConvert);
    m_convertClear->setVisible(canConvert);
    m_convertOnSwitch->setVisible(canConvert);

    // --- Browser --------------------------------------------------------
    root->addWidget(makeSeparator(this));
    m_browserTitle = makeSectionTitle(this);
    root->addWidget(m_browserTitle);
    m_browserStatus = new QLabel(this);
    m_browserStatus->setWordWrap(true);
    m_browserStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_browserStatus);

    // --- Signals --------------------------------------------------------
    connect(m_addButton, &QToolButton::clicked, this, [this] { showAddPanel(!m_addPanel->isVisible()); });
    connect(m_presetButton, &QToolButton::clicked, this, [this] { showPresetPanel(!m_presetPanel->isVisible()); });
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_targetStack->setCurrentIndex(qMax(0, index));
        m_targetLabel->setText(index == 1 ? tr("Website") : tr("Application"));
    });
    connect(m_refreshButton, &QToolButton::clicked, this, &SettingsWindow::refreshApps);
    connect(m_okButton, &QPushButton::clicked, this, &SettingsWindow::commitAdd);
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { showAddPanel(false); });
    connect(m_domainEdit, &QLineEdit::returnPressed, this, &SettingsWindow::commitAdd);
    connect(m_presetOk, &QPushButton::clicked, this, &SettingsWindow::commitPresets);
    connect(m_presetCancel, &QPushButton::clicked, this, [this] { showPresetPanel(false); });
    connect(m_autostart, &QCheckBox::toggled, this, &SettingsWindow::onAutostartToggled);
    connect(m_convertKey, &QKeySequenceEdit::editingFinished, this, [this] {
        // One shortcut only: keep the first combination of what was recorded.
        const QKeySequence seq = m_convertKey->keySequence();
        if (seq.count() > 1) {
            const QSignalBlocker block(m_convertKey);
            m_convertKey->setKeySequence(QKeySequence(seq.toString().section(QStringLiteral(", "), 0, 0)));
        }
        saveConversion();
    });
    connect(m_convertClear, &QToolButton::clicked, this, [this] {
        m_convertKey->clear();
        saveConversion();
    });
    connect(m_convertOnSwitch, &QCheckBox::toggled, this, [this] { saveConversion(); });
    connect(m_langCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        emit languageRequested(m_langCombo->currentData().toString());
    });
    connect(m_engine, &LayoutEngine::rulesChanged, this, &SettingsWindow::rebuildRules);
    connect(m_engine, &LayoutEngine::browserReported, this, &SettingsWindow::updateBrowserStatus);
    connect(m_engine->backend(), &PlatformBackend::problemChanged, this, &SettingsWindow::updateProblem);

    refreshIcons();
    retranslate();
}

QString SettingsWindow::language() const
{
    return QSettings().value(QStringLiteral("language"), QStringLiteral("auto")).toString();
}

void SettingsWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    else if (event->type() == QEvent::PaletteChange) {
        refreshIcons();
        rebuildRules();
    }
    QWidget::changeEvent(event);
}

void SettingsWindow::refreshIcons()
{
    m_addButton->setIcon(ui::icon(ui::Icon::Add));
    m_presetButton->setIcon(ui::icon(ui::Icon::Stack));
    m_refreshButton->setIcon(ui::icon(ui::Icon::Refresh));
    setWindowIcon(QIcon(QStringLiteral(":/app.png")));
}

void SettingsWindow::retranslate()
{
    setWindowTitle(QStringLiteral("Keyboard Switcher"));
    m_rulesTitle->setText(tr("Rules"));
    m_addButton->setToolTip(tr("Add rule"));
    m_presetButton->setToolTip(tr("Add preset"));

    {
        const QSignalBlocker block(m_typeCombo);
        const int type = qMax(0, m_typeCombo->currentIndex());
        m_typeCombo->clear();
        m_typeCombo->addItem(tr("Application"));
        m_typeCombo->addItem(tr("Website"));
        m_typeCombo->setCurrentIndex(type);
        m_targetLabel->setText(type == 1 ? tr("Website") : tr("Application"));
    }
    m_typeLabel->setText(tr("Type"));
    m_layoutLabel->setText(tr("Layout"));
    m_refreshButton->setToolTip(tr("Refresh application list"));
    m_appCombo->lineEdit()->setPlaceholderText(tr("process name"));
    m_okButton->setText(tr("Add"));
    m_cancelButton->setText(tr("Cancel"));

    m_presetHint->setText(tr("Switch to this layout in the selected applications:"));
    m_presetLayoutLabel->setText(tr("Layout"));
    m_presetOk->setText(tr("Add"));
    m_presetCancel->setText(tr("Cancel"));
    if (m_presetPanel->isVisible())
        rebuildPresetChoices();

    m_settingsTitle->setText(tr("Settings"));
    m_autostart->setText(tr("Start automatically at login"));
    m_langLabel->setText(tr("Language"));
    m_langCombo->setItemText(0, tr("System language"));
    m_convertTitle->setText(tr("Text conversion"));
    m_convertLabel->setText(tr("Convert selected text:"));
    m_convertOnSwitch->setText(tr("Also convert after I switch the layout manually"));
    updateConvertNote();
    m_browserTitle->setText(tr("Browser"));
    m_status->hide();

    updateProblem();
    updateBrowserStatus();
    rebuildRules();
}

void SettingsWindow::updateProblem()
{
    const QString problem = m_engine->backend()->problem();
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
}

void SettingsWindow::updateBrowserStatus()
{
    QString text;
    if (!m_bridge->isListening()) {
        text = tr("Website rules are off: port %1 on 127.0.0.1 is busy.").arg(m_bridge->port());
    } else if (!m_engine->lastBrowserReport().isValid()) {
        text = tr("To switch by website, install the Keyboard Switcher extension in your browser "
                  "(Chrome, Edge, Brave, Opera, Vivaldi, Yandex, Firefox). It sends only the domain "
                  "of the active tab to this app on port %1.").arg(m_bridge->port());
    } else {
        text = tr("Browser extension connected. Last tab change: %1.")
                   .arg(QLocale().toString(m_engine->lastBrowserReport().time(), QLocale::ShortFormat));
    }
    m_browserStatus->setText(text);
}

QString SettingsWindow::describeLayout(const QString &id) const
{
    bool installed = false;
    const LayoutInfo info = m_engine->backend()->describeLayout(id, &installed);
    const QString base = QStringLiteral("%1 · %2").arg(info.code, info.name);
    return installed ? base : tr("%1 (not installed)").arg(base);
}

void SettingsWindow::rebuildRules()
{
    while (QLayoutItem *item = m_rulesLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater(); // may be the sender of the click being handled right now
        }
        delete item;
    }

    const QList<Rule> &rules = m_engine->rules();
    if (rules.isEmpty()) {
        auto *empty = new QLabel(tr("No rules yet. Press + to add one, or add a preset."), m_rulesHost);
        empty->setEnabled(false);
        empty->setContentsMargins(4, 6, 4, 6);
        m_rulesLayout->addWidget(empty);
        empty->show();
    }

    for (int i = 0; i < rules.size(); ++i) {
        const Rule &rule = rules[i];
        auto *row = new QWidget(m_rulesHost);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(4, 0, 0, 0);
        h->setSpacing(8);

        auto *kindIcon = new QLabel(row);
        const ui::Icon kind = rule.kind == Rule::Kind::Domain ? ui::Icon::Globe
                            : rule.kind == Rule::Kind::Preset ? ui::Icon::Stack
                                                              : ui::Icon::App;
        kindIcon->setPixmap(ui::icon(kind).pixmap(16, 16));

        QString title = rule.key;
        QString tooltip;
        if (rule.kind == Rule::Kind::Preset) {
            if (const PresetGroup *g = Presets::instance().group(rule.key)) {
                title = g->isApplication() ? g->name() : tr("%1 (%2)").arg(g->name()).arg(g->apps.size());
                QStringList apps(g->apps.begin(), g->apps.end());
                apps.sort();
                tooltip = apps.join(QStringLiteral(", "));
            }
        } else if (rule.kind == Rule::Kind::Domain) {
            tooltip = tr("%1 and its subdomains").arg(rule.key);
        }
        auto *name = new QLabel(title, row);
        QFont bold = name->font();
        bold.setBold(true);
        name->setFont(bold);
        name->setToolTip(tooltip);
        auto *arrow = new QLabel(QStringLiteral("→"), row);
        auto *layout = new QLabel(describeLayout(rule.layout), row);
        layout->setToolTip(layout->text());
        layout->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        for (QLabel *l : {kindIcon, name, arrow, layout})
            l->setEnabled(rule.enabled);

        auto *pause = makeIconButton(row);
        pause->setIcon(ui::icon(rule.enabled ? ui::Icon::Pause : ui::Icon::Play));
        pause->setToolTip(rule.enabled ? tr("Pause this rule") : tr("Resume this rule"));
        auto *del = makeIconButton(row);
        del->setIcon(ui::icon(ui::Icon::Delete));
        del->setToolTip(tr("Delete rule"));

        const bool enabled = rule.enabled;
        connect(pause, &QToolButton::clicked, this, [this, i, enabled] { m_engine->setRuleEnabled(i, !enabled); });
        connect(del, &QToolButton::clicked, this, [this, i] { m_engine->removeRule(i); });

        h->addWidget(kindIcon);
        h->addWidget(name);
        h->addWidget(arrow);
        h->addWidget(layout, 1);
        h->addWidget(pause);
        h->addWidget(del);
        m_rulesLayout->addWidget(row);
        // Children created inside an already visible window stay hidden until shown,
        // and hidden widgets do not count in the layout's size hint.
        row->show();
    }
    m_rulesLayout->addStretch();

    // Grow with the list up to a limit, then scroll.
    m_rulesLayout->activate();
    m_scroll->setFixedHeight(qMin(m_rulesLayout->sizeHint().height(), kMaxRulesHeight));
}

void SettingsWindow::fillLayouts(QComboBox *combo, bool preferEnglish)
{
    combo->clear();
    int english = -1;
    for (const LayoutInfo &l : m_engine->backend()->layouts()) {
        if (english < 0 && l.lang == QLatin1String("en"))
            english = combo->count();
        combo->addItem(QStringLiteral("%1 · %2").arg(l.code, l.name), l.id);
    }
    if (preferEnglish && english >= 0)
        combo->setCurrentIndex(english);
}

void SettingsWindow::showAddPanel(bool show)
{
    if (show) {
        showPresetPanel(false);
        refreshApps();
        fillLayouts(m_layoutCombo, false);
        m_appCombo->setCurrentIndex(-1);
        m_appCombo->setEditText(QString());
        m_domainEdit->clear();
    }
    m_addPanel->setVisible(show);
    if (show)
        (m_typeCombo->currentIndex() == 1 ? static_cast<QWidget *>(m_domainEdit) : m_appCombo)->setFocus();
}

void SettingsWindow::refreshApps()
{
    const QString current = m_appCombo->currentText();
    m_appCombo->clear();
    m_appCombo->addItems(m_engine->knownApps());
    m_appCombo->setEditText(current);
}

void SettingsWindow::commitAdd()
{
    Rule rule;
    if (m_typeCombo->currentIndex() == 1) {
        rule.kind = Rule::Kind::Domain;
        rule.key = normalizeDomain(m_domainEdit->text());
        if (rule.key.isEmpty()) {
            m_domainEdit->setFocus();
            m_domainEdit->selectAll();
            return;
        }
    } else {
        rule.kind = Rule::Kind::App;
        QString app = m_appCombo->currentText().trimmed().toLower();
        app = app.mid(qMax(app.lastIndexOf(QLatin1Char('\\')), app.lastIndexOf(QLatin1Char('/'))) + 1);
#if defined(Q_OS_WIN)
        if (!app.isEmpty() && !app.contains(QLatin1Char('.')))
            app += QStringLiteral(".exe");
#endif
        rule.key = app;
        if (rule.key.isEmpty()) {
            m_appCombo->setFocus();
            return;
        }
    }
    if (m_layoutCombo->currentIndex() < 0)
        return;
    rule.layout = m_layoutCombo->currentData().toString();
    showAddPanel(false);
    m_engine->addRule(rule);
}

void SettingsWindow::showPresetPanel(bool show)
{
    if (show) {
        showAddPanel(false);
        rebuildPresetChoices();
        fillLayouts(m_presetLayoutCombo, true);
    }
    m_presetPanel->setVisible(show);
}

void SettingsWindow::rebuildPresetChoices()
{
    qDeleteAll(m_presetBoxes);
    m_presetBoxes.clear();
    // Remove the previous content (category headers, spacers).
    while (QLayoutItem *item = m_presetChecks->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    QWidget *host = m_presetScroll->widget();

    const Presets &presets = Presets::instance();
    for (const PresetCategory &cat : presets.categories()) {
        // A rule from an older version may already cover the whole category.
        const bool wholeAdded = m_engine->hasRule(Rule::Kind::Preset, cat.id);

        auto *header = new QCheckBox(cat.name().replace(QLatin1Char('&'), QStringLiteral("&&")), host);
        QFont bold = header->font();
        bold.setBold(true);
        header->setFont(bold);
        header->setEnabled(!wholeAdded);
        m_presetChecks->addWidget(header);
        header->show();

        const auto children = std::make_shared<QList<QCheckBox *>>();
        for (const QString &id : cat.groupIds) {
            const PresetGroup *g = presets.group(id);
            if (!g)
                continue;
            auto *box = new QCheckBox(host);
            box->setProperty("presetId", g->id);
            QStringList apps(g->apps.begin(), g->apps.end());
            apps.sort();
            box->setToolTip(apps.join(QStringLiteral(", ")));
            const QString name = g->name().replace(QLatin1Char('&'), QStringLiteral("&&"));
            if (wholeAdded || m_engine->hasRule(Rule::Kind::Preset, g->id)) {
                box->setText(tr("%1 — already added").arg(name));
                box->setChecked(true);
                box->setEnabled(false);
            } else {
                box->setText(name);
            }
            // Indent under the category header.
            auto *row = new QHBoxLayout;
            row->setContentsMargins(22, 0, 0, 0);
            row->addWidget(box);
            m_presetChecks->addLayout(row);
            box->show();
            m_presetBoxes << box;
            children->append(box);
            connect(box, &QCheckBox::toggled, this, [this, header, children](bool) {
                // The header is checked when every selectable application below it is.
                bool all = true, any = false;
                for (QCheckBox *b : std::as_const(*children)) {
                    if (!b->isEnabled())
                        continue;
                    any = true;
                    all = all && b->isChecked();
                }
                const QSignalBlocker block(header);
                header->setChecked(any && all);
                updatePresetOk();
            });
        }
        connect(header, &QCheckBox::clicked, this, [this, children](bool on) {
            for (QCheckBox *b : std::as_const(*children))
                if (b->isEnabled())
                    b->setChecked(on);
            updatePresetOk();
        });
        m_presetChecks->addSpacing(6);
    }
    m_presetChecks->addStretch();
    m_presetScroll->verticalScrollBar()->setValue(0);
    updatePresetOk();
}

void SettingsWindow::updatePresetOk()
{
    bool any = false;
    for (QCheckBox *box : std::as_const(m_presetBoxes))
        if (box->isEnabled() && box->isChecked()) {
            any = true;
            break;
        }
    m_presetOk->setEnabled(any);
}

void SettingsWindow::commitPresets()
{
    if (m_presetLayoutCombo->currentIndex() < 0)
        return;
    const QString layout = m_presetLayoutCombo->currentData().toString();
    QList<Rule> toAdd;
    for (QCheckBox *box : std::as_const(m_presetBoxes)) {
        if (box->isEnabled() && box->isChecked()) {
            Rule r;
            r.kind = Rule::Kind::Preset;
            r.key = box->property("presetId").toString();
            r.layout = layout;
            toAdd << r;
        }
    }
    showPresetPanel(false);
    for (const Rule &r : toAdd)
        m_engine->addRule(r);
}

void SettingsWindow::updateConvertNote()
{
    if (!m_engine->backend()->canConvertText())
        m_convertNote->setText(tr("Not available in this session."));
    else if (m_hotkeyConflict)
        m_convertNote->setText(tr("This shortcut is already in use."));
    else
        m_convertNote->clear();
    m_convertNote->setVisible(!m_convertNote->text().isEmpty());
}

void SettingsWindow::setHotkeyConflict(bool conflict)
{
    m_hotkeyConflict = conflict;
    updateConvertNote();
}

void SettingsWindow::saveConversion()
{
    QSettings s;
    s.setValue(QStringLiteral("convertHotkey"), m_convertKey->keySequence().toString(QKeySequence::PortableText));
    s.setValue(QStringLiteral("convertOnSwitch"), m_convertOnSwitch->isChecked());
    emit conversionChanged();
}

void SettingsWindow::onAutostartToggled(bool on)
{
    const AutostartResult r = m_engine->backend()->setAutostart(on);
    if (r == AutostartResult::Ok) {
        m_status->hide();
        return;
    }
    {
        const QSignalBlocker block(m_autostart);
        m_autostart->setChecked(m_engine->backend()->autostartEnabled());
    }
    switch (r) {
    case AutostartResult::DisabledByUser:
        m_status->setText(tr("Start at login is turned off in the system settings. Allow Keyboard Switcher there."));
        break;
    case AutostartResult::DisabledByPolicy:
        m_status->setText(tr("Start at login is disabled by your administrator."));
        break;
    default:
        m_status->setText(tr("Could not change the startup setting."));
        break;
    }
    m_status->show();
}
