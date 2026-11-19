// Keyboard Changer helper for GNOME Shell 45+.
//  * tells the Keyboard Changer app (D-Bus io.github.KeyboardChanger /Bridge) which window got focus;
//  * exports io.github.KeyboardChanger.Shell on org.gnome.Shell /io/github/KeyboardChanger
//    so the app can list and switch input sources (GNOME manages them itself, on Wayland and X11).
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import {getInputSourceManager} from 'resource:///org/gnome/shell/ui/status/keyboard.js';

const SHELL_IFACE = `
<node>
  <interface name="io.github.KeyboardChanger.Shell">
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

const APP_NAME = 'io.github.KeyboardChanger';
const APP_PATH = '/Bridge';
const EXPORT_PATH = '/io/github/KeyboardChanger';

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
        // GNOME 48+ moved the list to global.compositor; older versions have global.get_window_actors().
        const actors = global.compositor?.get_window_actors?.() ?? global.get_window_actors();
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

export default class KeyboardChangerExtension extends Extension {
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
