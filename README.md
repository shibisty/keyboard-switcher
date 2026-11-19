# Keyboard Switcher

**English** · [Русский](README.ru.md)

A tray / menu-bar utility. It switches the keyboard layout when a chosen application's window
or a chosen website's tab gets focus, and restores the previous layout when focus leaves.

Runs on Windows (x64, ARM64), macOS (Apple Silicon, Intel) and Linux.
Written in C++20 with Qt 6 Widgets (no QML). It uses no CPU while idle: the program receives
focus events from the system and never polls on a timer.

## Rules

| Type | Example | When it fires |
|---|---|---|
| Website | `github.com → EN` | a browser is in the foreground and its active tab is this domain or a subdomain (needs the browser extension) |
| Application | `cmd.exe → EN` | a window of this process got focus |
| Preset | "Code editors and IDEs → EN" | a window of any program in the group |

If several rules match, a website wins over an application, and an application over a preset.
Each rule can be paused or deleted. The "Pause" item in the tray menu stops all rules at once.

### Presets

The program lists live in `res/presets.json`, separately for Windows, macOS and Linux:

* **Code editors and IDEs**: VS Code, Cursor, Windsurf, Zed, Visual Studio, all JetBrains IDEs,
  Android Studio, Qt Creator, Xcode, Eclipse, NetBeans, Sublime Text, **Notepad++**, Vim, Emacs, Kate and more.
* **Terminals and consoles**: Windows Terminal, cmd, PowerShell, Git Bash (mintty), ConEmu, PuTTY,
  Terminal.app, iTerm2, GNOME Terminal, Konsole, Alacritty, kitty, WezTerm, Ghostty and more.
* **Database and API clients**: HeidiSQL, DbSchema, DBeaver, DataGrip, SSMS, Oracle SQL Developer,
  pgAdmin, TablePlus, Postman, Insomnia, Bruno and more.

Presets are added with the stack button next to "+": tick individual applications (a category
header ticks all of its applications) and press "Add". The default layout is English.
The identifiers of an application are shown in its tooltip.

## Linux support

The backend is chosen automatically from session variables. You can force one with
`KEYBOARD_SWITCHER_BACKEND=x11|kde|gnome|sway|hyprland`.

| Session | How it works | What you need |
|---|---|---|
| **Ubuntu** (main edition: GNOME with Ubuntu theming, Wayland) | same as GNOME | GNOME extension (the .deb package installs it) |
| **GNOME 45–51** (Wayland and X11): Ubuntu 24.04–26.10, Fedora, Debian 13 | GNOME Shell extension `gnome-extension/` | enable the extension (see below) |
| **GNOME 42–44**: Ubuntu 22.04, Debian 12 | extension in the old format `gnome-extension-legacy/`; the app is built with Qt 5.15 | enable the extension |
| **KDE Plasma 5/6** (X11 and Wayland): Kubuntu, KDE neon, Fedora KDE | KWin script (loaded automatically) and D-Bus `org.kde.keyboard` | nothing |
| **X11**: Xubuntu (XFCE), Ubuntu MATE, Ubuntu Cinnamon, Linux Mint, Lubuntu and Ubuntu Budgie in an X11 session, i3, Openbox… | EWMH `_NET_ACTIVE_WINDOW` and XKB | nothing |
| **Sway** | IPC `$SWAYSOCK` | nothing |
| **Hyprland** | Hyprland sockets | nothing |

Other Wayland compositors do not let an ordinary program learn the active window, so they are not supported.
This applies, for example, to Lubuntu (LXQt) and Ubuntu Budgie running a Wayland session: choose the X11 session on the login screen.

**GNOME extension.** The .deb and .rpm packages install it themselves (`build-linux.sh` picks the right
version). Manually (for GNOME 42–44 use `gnome-extension-legacy` instead of `gnome-extension`):
```bash
cp -r gnome-extension/keyboard-switcher@keyboardswitcher.github.io ~/.local/share/gnome-shell/extensions/
# log out and back in, then:
gnome-extensions enable keyboard-switcher@keyboardswitcher.github.io
```
The tray icon in GNOME needs the AppIndicator extension. Ubuntu ships it by default; on plain GNOME (Fedora, Debian) install it.

## Browser extension (per-website rules)

Folder `browser-extension/`. Supported: Chrome, Edge, Brave, Opera, Vivaldi, Yandex Browser and Firefox.
The extension sends the program **only the domain** of the active tab to `http://127.0.0.1:47863/tab`;
no data leaves the computer. The program accepts requests only from extensions: requests
from web pages (an `Origin: http(s)://…` header) are rejected.

* Development in Chromium browsers: `chrome://extensions` → "Developer mode" →
  "Load unpacked" → the `browser-extension` folder.
* Development in Firefox: `about:debugging` → "This Firefox" → "Load Temporary Add-on" →
  first build `dist/extension/keyboard-switcher-firefox.zip` (see below) and select it.
* Publishing: `python scripts/pack_extension.py` creates archives for the Chrome Web Store,
  Edge Add-ons and addons.mozilla.org in `dist/extension/`.

The port is changed in the settings file (`browserPort=`) and in the extension options.

## Building

### Windows (Qt Creator, MinGW) for development
Open `CMakeLists.txt`, run *Build → Clear CMake Configuration*, then build the project.

### Microsoft Store (production)

Requires: Qt **MSVC 2022** (`msvc2022_64` and `msvc2022_arm64`), Visual Studio 2022 with C++
and *MSVC ARM64 build tools*, Windows SDK.

1. Once, copy `scripts\store.config.example.json` to `scripts\store.config.json`.
   Fill it in with the values from Partner Center → your app → *Product management → Product identity*:
   `Package/Identity/Name`, `Package/Identity/Publisher` (`CN=…`), `PublisherDisplayName`.
   Set the Qt paths there too. This file is not committed to git.
2. Build the release:

   ```powershell
   .\scripts\release-store.ps1 -Version 1.0.0.0
   ```

   The script checks the config and the version. The Store requires 4 numbers with the last one `0`,
   and every upload must be higher than the previous one. It then makes clean Release builds for x64 and ARM64,
   verifies every package (identity, exe version, no debug DLLs) and produces
   `dist\store\<version>\KeyboardSwitcher_<version>_arm64_x64.msixupload`:
   the `.msixbundle` together with `.appxsym` symbols for crash reports.
3. Upload the `.msixupload` in Partner Center → *Submission → Packages*.
   For the `runFullTrust` capability the Store asks for a short justification, for example
   "desktop tray utility that changes keyboard layout per app".

Packages are not signed: the Store signs them itself.

#### Single builds and local testing

```powershell
.\scripts\build.ps1 -Arch x64   -QtDir C:\Qt\6.11.1\msvc2022_64 -Version 1.0.0.0
.\scripts\build.ps1 -Arch arm64 -QtDir C:\Qt\6.11.1\msvc2022_arm64 -QtHostDir C:\Qt\6.11.1\msvc2022_64
.\scripts\bundle.ps1
```

To install a package locally without the Store, sign it with a test certificate
(`-CertPfx`, `-CertPassword`) and add that certificate to "Trusted People".

### Windows 7 / 8 / 8.1 and 32-bit systems (legacy)
Qt 6 runs only on Windows 10+ x64/ARM64. For older systems the same code builds with Qt 5.15
into a portable zip (no installation or administrator rights needed):
```powershell
.\scripts\build-legacy.ps1 -Arch x86   # 32-bit: Windows 7–11
.\scripts\build-legacy.ps1 -Arch x64   # 64-bit: Windows 7–8.1
```
Requires Qt 5.15.2 "MinGW 8.1.0 32-bit/64-bit" and MinGW 8.1.0 (Qt Maintenance Tool → Archive).
Differences from the main version: no autostart through the Microsoft Store, dark theme only on Windows 10.
Windows XP and Vista are not supported: the last Qt for them is 5.6 (2016), and the code would have to be
rewritten in old C++ with part of the WinAPI calls replaced.

### macOS
```bash
QT_DIR=~/Qt/6.8.3/macos bash scripts/build-macos.sh 1.1.0.0
# signing and notarization (otherwise Gatekeeper warns on first launch):
SIGN_ID="Developer ID Application: Name (TEAMID)" NOTARY_PROFILE=profile QT_DIR=… bash scripts/build-macos.sh 1.1.0.0
```
The output is `dist/macos/KeyboardSwitcher-<version>.dmg`, a universal arm64 + x86_64 build.
The program lives in the menu bar without a Dock icon (`LSUIElement`). No special permissions are needed
for switching layouts.

### Linux
```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev libx11-dev libxtst-dev libxkbcommon-dev   # Debian/Ubuntu
bash scripts/build-linux.sh 1.1.0.0   # dist/linux: .deb, .rpm (if rpmbuild exists), .tar.gz
```
Minimum Qt version: 6.4 (Ubuntu 24.04); on older distributions it also builds with Qt 5.15.

### CI
`ci/github-build.yml` builds MSIX x64/ARM64, dmg, deb/rpm and the extension archives.
To enable it, copy the file to `.github/workflows/`.

## Converting selected text

Typed "руддщ" in the wrong layout? Select the text and press the hotkey (**Pause** by default): the program
copies the selection, re-types it with the keys of the other layout ("руддщ" → "hello"), pastes it back,
switches the layout and restores the previous clipboard contents. It works on physical keys, so any installed
layouts work (including Shift and AltGr characters).

In the settings ("Text conversion") you can change or remove the shortcut and turn on **automatic conversion
after a manual layout switch** (off by default).

* Conversion is disabled in terminals (Ctrl+C would interrupt the running program); in code editors the automatic
  mode does not fire (without a selection Ctrl+C copies the whole line).
* Windows: hotkey through `RegisterHotKey`, input through `SendInput`. Linux: X11 only (needs libxkbcommon and XTest).
  Wayland and macOS are not supported yet — the settings show "Not available in this session".

## Interface languages

57 languages (list in `i18n/languages.json`). Rule: the first system UI language that is in the list is used;
if none is, English. In the settings you can pick a language manually or "System language" (the default, which
follows the system). Arabic, Persian, Urdu, Pashto and Sindhi switch the interface to right-to-left.

Languages without their own translation (Bhojpuri, Maithili, Awadhi, Magahi, Marwari, Haryanvi, Chhattisgarhi → Hindi;
Chittagonian → Bengali; Saraiki, Deccani → Urdu; Zhuang → Simplified Chinese; Fula → French; Madurese → Indonesian)
use the text of a related language during auto-detection and are not shown as separate list items.

The translations were made by a model and have not been proofread by native speakers — rarer languages (Yoruba, Igbo,
Oromo, Shona, Malagasy, Assamese, Odia, Pashto, Sindhi and others) especially deserve a review.

To change texts: edit the English strings in code (`tr()`), add them to `i18n/strings/_source.json` and to all
`i18n/strings/<code>.json` (the order of strings matters), then run `python scripts/gen_i18n.py`. It generates the `.ts`
files, the language table `src/core/languages_gen.h`, the extension `_locales`, the `Comment[..]` lines in `.desktop`
and the language list in the MSIX manifest.

## Where settings are stored

| OS | File |
|---|---|
| Windows | `%APPDATA%\KeyboardSwitcher\KeyboardSwitcher.ini` (Store version: `%LOCALAPPDATA%\Packages\<package>\LocalCache\Roaming\…`) |
| macOS, Linux | `~/.config/KeyboardSwitcher/KeyboardSwitcher.ini` |

Rules from version 1.0 (`process=`) are read automatically.

## Limitations

* Windows: windows running as administrator do not accept messages from an ordinary program (UIPI),
  so the layout will not switch in them.
* On Windows 11 `cmd` usually opens inside Windows Terminal (`windowsterminal.exe`).
  It is already in the terminals preset.
* Safari is not supported: its extensions need a separate build through Xcode.
