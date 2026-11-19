#pragma once

#include <QColor>
#include <QIcon>

// Small monochrome icons drawn in code: identical on Windows, macOS and Linux,
// no icon fonts or themes needed, recoloured for light/dark palettes.
namespace ui {

enum class Icon { Add, Delete, Pause, Play, Refresh, Globe, Stack, App, Keyboard };

QIcon icon(Icon kind, const QColor &color);
QIcon icon(Icon kind);  // palette text colour

QIcon trayIcon(bool paused);

} // namespace ui
