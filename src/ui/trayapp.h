#pragma once

#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTranslator>

#include "core/browserbridge.h"

class LayoutEngine;
class PlatformBackend;
class SelectionConverter;
class SettingsWindow;

class TrayApp : public QObject
{
    Q_OBJECT
public:
    explicit TrayApp(QObject *parent = nullptr);
    ~TrayApp() override;

    void start(bool launchedAtStartup);
    void showSettings();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void toggleSettings();
    void setLanguage(const QString &code);
    void retranslate();
    void updateIcon();
    void placeWindow();
    void applyConversion();

    PlatformBackend *m_backend;   // owned (QObject child); outlives m_engine
    LayoutEngine *m_engine;       // owned (QObject child)
    SelectionConverter *m_converter;  // owned (QObject child)
    bool m_hotkeyConflict = false;
    BrowserBridge m_bridge;
    QTranslator m_translator;
    QString m_language;

    QMenu m_menu;
    QAction *m_actSettings = nullptr;
    QAction *m_actPause = nullptr;
    QAction *m_actQuit = nullptr;
    QSystemTrayIcon m_tray;   // declared after m_menu: destroyed first

    QPointer<SettingsWindow> m_window;
};
