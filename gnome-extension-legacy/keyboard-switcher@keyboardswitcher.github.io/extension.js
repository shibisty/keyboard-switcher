// Keyboard Switcher helper for GNOME Shell 42-44 (legacy extension format).
// Same behaviour as gnome-extension/ (GNOME 45+), written with imports.* instead of ES modules.
//  * tells the Keyboard Switcher app (D-Bus io.github.KeyboardSwitcher /Bridge) which window got focus;
//  * exports io.github.KeyboardSwitcher.Shell on org.gnome.Shell /io/github/KeyboardSwitcher
//    so the app can list and switch input sources (GNOME manages them itself, on Wayland and X11).
const {Gio, GLib, Shell} = imports.gi;
const {getInputSourceManager} = imports.ui.status.keyboard;

const SHELL_IFACE = `
<node>
  <interface name="io.github.KeyboardSwitcher.Shell">
    <method name="ListInputSources"><arg type="s" direction="out" name="json"/></method>
    <method name="CurrentInputSource"><arg type="s" direction="out" name="json"/></method>
    <method name="SetInputSource">
      <arg type="s" direction="in" name="id"/>
      <arg type="b" direction="out" name="ok"/>
    </method>
    <method name="ListWindows"><arg type="s" direction="out" name="json"/></method>
    <method name="ReportFocus"><arg type="s" direction="out" name="json"/></method>
  </interface>
</node>`;

const APP_NAME = 'io.github.KeyboardSwitcher';
const APP_PATH = '/Bridge';
const EXPORT_PATH = '/io/github/KeyboardSwitcher';

function windowInfo(win) {
    const app = Shell.WindowTracker.get_default().get_window_app(win);
    return {
        pid: win.get_pid(),
        wmClass: win.get_wm_class() ?? '',
        appId: app?.get_id() ?? '',
        title: win.get_title() ?? '',
    };
}

function sources() {
    const manager = getInputSourceManager();
    return Object.values(manager.inputSources).sort((a, b) => a.index - b.index);
}

function report(win) {
    if (!win)
        return;
    const info = windowInfo(win);
    // Fire and forget: the app may not be running.
    Gio.DBus.session.call(APP_NAME, APP_PATH, APP_NAME, 'focus',
        new GLib.Variant('(ssss)', [String(info.pid), info.wmClass, info.appId, info.title]),
        null, Gio.DBusCallFlags.NO_AUTO_START, 1000, null,
        (connection, result) => {
            try {
                connection.call_finish(result);
            } catch (e) {
                // app not running
            }
        });
}

class ShellApi {
    ListInputSources() {
        return JSON.stringify(sources().map(s => ({
            index: s.index, id: s.id, type: s.type, name: s.displayName, short: s.shortName,
        })));
    }

    CurrentInputSource() {
        const s = getInputSourceManager().currentSource;
        return JSON.stringify(s ? {index: s.index, id: s.id} : {});
    }

    SetInputSource(id) {
        const source = sources().find(s => s.id === id);
        if (!source)
            return false;
        source.activate(false);
        return true;
    }

    ListWindows() {
        const actors = global.get_window_actors();
        const windows = actors
            .map(actor => actor.meta_window)
            .filter(w => w && !w.skip_taskbar);
        return JSON.stringify(windows.map(windowInfo));
    }

    ReportFocus() {
        report(global.display.focus_window);
        return '{}';
    }
}

class KeyboardSwitcherExtension {
    enable() {
        this._api = Gio.DBusExportedObject.wrapJSObject(SHELL_IFACE, new ShellApi());
        this._api.export(Gio.DBus.session, EXPORT_PATH);
        this._focusId = global.display.connect('notify::focus-window',
            () => report(global.display.focus_window));
        report(global.display.focus_window);
    }

    disable() {
        if (this._focusId) {
            global.display.disconnect(this._focusId);
            this._focusId = 0;
        }
        this._api?.unexport();
        this._api = null;
    }
}

// eslint-disable-next-line no-unused-vars
function init() {
    return new KeyboardSwitcherExtension();
}
