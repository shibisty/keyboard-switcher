# Keyboard Switcher

[English](README.md) · **Русский**

Утилита для трея и строки меню. Переключает раскладку клавиатуры, когда фокус получает окно
выбранного приложения или вкладка выбранного сайта. Когда фокус уходит, возвращает прежнюю раскладку.

Работает на Windows (x64, ARM64), macOS (Apple Silicon, Intel) и Linux.
Написана на Qt 6 Widgets и C++20, без QML. В простое процессор не используется: программа
получает события фокуса от системы и ничего не опрашивает по таймеру.

## Правила

| Тип | Пример | Как срабатывает |
|---|---|---|
| Сайт | `github.com → EN` | браузер на переднем плане и в активной вкладке этот домен или его поддомен (нужно расширение браузера) |
| Приложение | `cmd.exe → EN` | окно этого процесса получило фокус |
| Пресет | «Редакторы кода и IDE → EN» | окно любой программы из группы |

Если подходит несколько правил, побеждает сайт, затем приложение, затем пресет.
Каждое правило можно поставить на паузу или удалить. Кнопка «Пауза» в меню трея останавливает все правила сразу.

### Пресеты

Списки программ лежат в `res/presets.json`, отдельно для Windows, macOS и Linux:

* **Редакторы кода и IDE**: VS Code, Cursor, Windsurf, Zed, Visual Studio, все IDE JetBrains,
  Android Studio, Qt Creator, Xcode, Eclipse, NetBeans, Sublime Text, **Notepad++**, Vim, Emacs, Kate и др.
* **Терминалы и консоли**: Windows Terminal, cmd, PowerShell, Git Bash (mintty), ConEmu, PuTTY,
  Terminal.app, iTerm2, GNOME Terminal, Konsole, Alacritty, kitty, WezTerm, Ghostty и др.
* **Клиенты БД и API**: HeidiSQL, DbSchema, DBeaver, DataGrip, SSMS, Oracle SQL Developer,
  pgAdmin, TablePlus, Postman, Insomnia, Bruno и др.

Пресет добавляется кнопкой со стопкой рядом с «+». Раскладка по умолчанию английская.
Список программ пресета виден в подсказке при наведении.

## Поддержка Linux

Бэкенд выбирается автоматически по переменным сеанса. Принудительно его можно задать через
`KEYBOARD_SWITCHER_BACKEND=x11|kde|gnome|sway|hyprland`.

| Сеанс | Как работает | Что нужно |
|---|---|---|
| **Ubuntu** (основная редакция: GNOME в оформлении Ubuntu, Wayland) | как GNOME | расширение GNOME (пакет .deb ставит его сам) |
| **GNOME 45–51** (Wayland и X11): Ubuntu 24.04–26.10, Fedora, Debian 13 | расширение GNOME Shell `gnome-extension/` | включить расширение (см. ниже) |
| **GNOME 42–44**: Ubuntu 22.04, Debian 12 | расширение в старом формате `gnome-extension-legacy/`; программа собирается на Qt 5.15 | включить расширение |
| **KDE Plasma 5/6** (X11 и Wayland): Kubuntu, KDE neon, Fedora KDE | скрипт KWin (загружается автоматически) и D-Bus `org.kde.keyboard` | ничего |
| **X11**: Xubuntu (XFCE), Ubuntu MATE, Ubuntu Cinnamon, Linux Mint, Lubuntu и Ubuntu Budgie в сеансе X11, i3, Openbox… | EWMH `_NET_ACTIVE_WINDOW` и XKB | ничего |
| **Sway** | IPC `$SWAYSOCK` | ничего |
| **Hyprland** | сокеты Hyprland | ничего |

Другие Wayland-композиторы не дают обычной программе узнать активное окно, поэтому не поддерживаются. Это касается, например, Lubuntu (LXQt) и Ubuntu Budgie, если они запущены в сеансе Wayland: там нужно выбрать сеанс X11 на экране входа.

**Расширение GNOME.** Пакеты .deb и .rpm ставят его сами (нужную версию выбирает `build-linux.sh`). Вручную
(для GNOME 42–44 вместо `gnome-extension` взять `gnome-extension-legacy`):
```bash
cp -r gnome-extension/keyboard-switcher@keyboardswitcher.github.io ~/.local/share/gnome-shell/extensions/
# выйти из сеанса и войти снова, затем:
gnome-extensions enable keyboard-switcher@keyboardswitcher.github.io
```
Чтобы иконка появилась в трее GNOME, нужно расширение AppIndicator. В Ubuntu оно есть по умолчанию, в чистом GNOME (Fedora, Debian) его нужно поставить.

## Расширение браузера (правила по сайтам)

Папка `browser-extension/`. Поддерживаются Chrome, Edge, Brave, Opera, Vivaldi, Яндекс Браузер и Firefox.
Расширение отправляет программе **только домен** активной вкладки на `http://127.0.0.1:47863/tab`,
данные не покидают компьютер. Программа принимает запросы только от расширений: запросы
с веб-страниц (заголовок `Origin: http(s)://…`) отклоняются.

* Для разработки в Chromium-браузерах: `chrome://extensions` → «Режим разработчика» →
  «Загрузить распакованное» → папка `browser-extension`.
* Для разработки в Firefox: `about:debugging` → «Этот Firefox» → «Загрузить временное дополнение» →
  сначала собрать `dist/extension/keyboard-switcher-firefox.zip` (см. ниже) и выбрать его.
* Для публикации: `python scripts/pack_extension.py` создаёт архивы для Chrome Web Store,
  Edge Add-ons и addons.mozilla.org в `dist/extension/`.

Порт меняется в файле настроек (`browserPort=`) и в параметрах расширения.

## Сборка

### Windows (Qt Creator, MinGW) для разработки
Откройте `CMakeLists.txt` и выполните *Build → Clear CMake Configuration*, затем соберите проект.

### Microsoft Store (prod)

Нужно: Qt **MSVC 2022** (`msvc2022_64` и `msvc2022_arm64`), Visual Studio 2022 с C++
и *MSVC ARM64 build tools*, Windows SDK.

1. Один раз скопируйте `scripts\store.config.example.json` в `scripts\store.config.json`.
   Заполните его значениями из Partner Center → приложение → *Product management → Product identity*:
   `Package/Identity/Name`, `Package/Identity/Publisher` (`CN=…`), `PublisherDisplayName`.
   Там же укажите пути к Qt. Этот файл в git не попадает.
2. Соберите релиз:

   ```powershell
   .\scripts\release-store.ps1 -Version 1.0.0.0
   ```

   Скрипт проверяет конфиг и версию. Store требует 4 числа с последним `0`, и каждая загрузка
   должна быть выше предыдущей. Затем он делает чистые Release-сборки x64 и ARM64, проверяет
   каждый пакет (identity, версия exe, отсутствие debug-DLL) и собирает
   `dist\store\<версия>\KeyboardSwitcher_<версия>_arm64_x64.msixupload`:
   `.msixbundle` вместе с символами `.appxsym` для отчётов о сбоях.
3. Загрузите `.msixupload` в Partner Center → *Submission → Packages*.
   Для возможности `runFullTrust` Store попросит короткое обоснование, например
   «desktop tray utility that changes keyboard layout per app».

Пакеты не подписываются: Store подписывает их сам.

#### Отдельные сборки и локальная проверка

```powershell
.\scripts\build.ps1 -Arch x64   -QtDir C:\Qt\6.11.1\msvc2022_64 -Version 1.0.0.0
.\scripts\build.ps1 -Arch arm64 -QtDir C:\Qt\6.11.1\msvc2022_arm64 -QtHostDir C:\Qt\6.11.1\msvc2022_64
.\scripts\bundle.ps1
```

Чтобы установить пакет локально без Store, подпишите его тестовым сертификатом
(`-CertPfx`, `-CertPassword`) и добавьте этот сертификат в «Доверенные лица».



### Windows 7 / 8 / 8.1 и 32-битные системы (legacy)
Qt 6 работает только на Windows 10+ x64/ARM64. Для более старых систем тот же код собирается на Qt 5.15
в портативный zip (без установки и прав администратора):
```powershell
.\scripts\build-legacy.ps1 -Arch x86   # 32 бита: Windows 7–11
.\scripts\build-legacy.ps1 -Arch x64   # 64 бита: Windows 7–8.1
```
Нужны Qt 5.15.2 «MinGW 8.1.0 32-bit/64-bit» и MinGW 8.1.0 (Qt Maintenance Tool → Archive).
Отличия от основной версии: нет автозапуска через Microsoft Store, тёмная тема только на Windows 10.
Windows XP и Vista не поддерживаются: последняя Qt для них — 5.6 (2016 год), под неё код пришлось бы
переписать на старый C++ и заменить часть функций WinAPI.

### macOS
```bash
QT_DIR=~/Qt/6.8.3/macos bash scripts/build-macos.sh 1.1.0.0
# подпись и нотаризация (иначе Gatekeeper предупредит при первом запуске):
SIGN_ID="Developer ID Application: Имя (TEAMID)" NOTARY_PROFILE=профиль QT_DIR=… bash scripts/build-macos.sh 1.1.0.0
```
На выходе `dist/macos/KeyboardSwitcher-<версия>.dmg`, универсальная сборка arm64 и x86_64.
Программа живёт в строке меню без иконки в Dock (`LSUIElement`). Специальных разрешений не нужно.

### Linux
```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev libx11-dev   # Debian/Ubuntu
bash scripts/build-linux.sh 1.1.0.0   # dist/linux: .deb, .rpm (если есть rpmbuild), .tar.gz
```
Минимальная версия Qt: 6.4 (Ubuntu 24.04); на старых дистрибутивах собирается и с Qt 5.15.

### CI
`ci/github-build.yml` собирает MSIX x64/ARM64, dmg, deb/rpm и архивы расширения.
Чтобы включить, скопируйте файл в `.github/workflows/`.

## Конвертация выделенного текста

Набрали «руддщ» не в той раскладке? Выделите текст и нажмите горячую клавишу (по умолчанию **Pause**): программа
скопирует выделение, перепечатает его клавишами другой раскладки («руддщ» → «hello»), вставит обратно, переключит
раскладку и вернёт прежнее содержимое буфера обмена. Работает по физическим клавишам, поэтому подходят любые
установленные раскладки (включая Shift и AltGr-символы).

В настройках («Конвертация текста») можно сменить или убрать сочетание, а также включить **автоматическую
конвертацию после ручного переключения раскладки** (по умолчанию выключена).

* В терминалах конвертация отключена (Ctrl+C прервал бы запущенную программу); в редакторах кода автоматический режим
  не срабатывает (без выделения Ctrl+C копирует строку).
* Windows: хоткей через `RegisterHotKey`, ввод через `SendInput`. Linux: только X11 (нужны libxkbcommon и XTest);
  Wayland и macOS пока не поддерживаются — в настройках будет «Недоступно в этом сеансе».

## Языки интерфейса

57 языков (список в `i18n/languages.json`). Правило: берётся первый язык из системных языков интерфейса, который есть в списке; если ни одного нет — английский. В настройках можно выбрать язык вручную или «Язык системы» (по умолчанию — следует за системой). Арабский, персидский, урду, пушту и синдхи включают зеркальную раскладку (RTL).

Языки без собственного перевода (бходжпури, майтхили, авадхи, магахи, марвари, харьянви, чхаттисгархи → хинди; читтагонгский → бенгальский; сарайки, деккани → урду; чжуанский → китайский упрощённый; фула → французский; мадурский → индонезийский) подхватывают текст близкого языка при автоопределении, отдельным пунктом в списке не показываются.

Переводы выполнены моделью и не вычитаны носителями — редкие языки (йоруба, игбо, оромо, шона, малагасийский, ассамский, одия, пушту, синдхи и др.) особенно стоит проверить.

Как менять тексты: правьте английские строки в коде (`tr()`), добавьте их в `i18n/strings/_source.json` и во все `i18n/strings/<код>.json` (порядок строк важен), затем `python scripts/gen_i18n.py` — он генерирует `.ts`, таблицу языков `src/core/languages_gen.h`, `_locales` расширения, строки `Comment[..]` в `.desktop` и список языков в манифесте MSIX.

## Где хранятся настройки

| ОС | Файл |
|---|---|
| Windows | `%APPDATA%\KeyboardSwitcher\KeyboardSwitcher.ini` (из Store: `%LOCALAPPDATA%\Packages\<пакет>\LocalCache\Roaming\…`) |
| macOS, Linux | `~/.config/KeyboardSwitcher/KeyboardSwitcher.ini` |

Правила версии 1.0 (`process=`) читаются автоматически.

## Ограничения

* Windows: окна, запущенные от администратора, не принимают сообщения от обычной программы (UIPI),
  поэтому раскладка в них не переключится.
* В Windows 11 `cmd` обычно открывается внутри Windows Terminal (`windowsterminal.exe`).
  Он уже есть в пресете терминалов.
* Safari не поддерживается: для его расширений нужна отдельная сборка через Xcode.
