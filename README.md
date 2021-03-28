# libdecor - A client-side decorations library for Wayland client

libdecor is a library that can help Wayland clients draw window
decorations for them. It aims to provide multiple backends that implements the
decoration drawing.


## Dependencies

Required:
- `meson` >= 0.47
- `ninja`
- `wayland-client` >= 1.18
- `wayland-protocols` >= 1.15
- `wayland-cursor`
- `cairo`

Recommended:
- `dbus-1` (to query current cursor theme)

Optional
- `egl` (to build EGL example)

Install via apt:
`sudo apt install meson ninja-build libwayland-dev wayland-protocols libcairo2-dev libdbus-1-dev libegl-dev`

Install via dnf:
`sudo dnf install meson ninja-build wayland-devel wayland-protocols-devel dbus-devel cairo-devel mesa-libEGL-devel`


## Build & Install

### Quick Start

To build and run the example program:
1. `meson build && meson compile -C build`
2. `LIBDECOR_PLUGIN_DIR=build/src/plugins/cairo/ ./build/demo/libdecor-demo`

### Release Builds

The library and default plugins can be built and installed via:
1. `meson build --buildtype release`
2. `meson install -C build`

where `build` is the build directory that will be created during this process.

This will install by default to `/usr/local/`. To change this set the `prefix` during built, e.g. `meson build --buildtype release -Dprefix=$HOME/.local/`.

Plugins will be installed into the same directory and from thereon will be selected automatically depending on their precedence. This behaviour can be overridden at runtime by setting the environment variable `LIBDECOR_PLUGIN_DIR` and pointing it to a directory with a valid plugin.

### Debug and Development Builds

During development and when debugging, it is recommended to enable the AddressSanitizer and increase the warning level:
1. `meson build -Db_sanitize=address -Dwarning_level=3`
2. `meson compile -C build`

You may have to install `libasan6` (apt) or `libasan` (dnf). Otherwise linking will fail.

By default `libdecor` will look for plugins in the target directory of the installation. Therefore, when running the demos directly from the `build` directory, no plugins will be found and the fallback plugin without any decorations will be used.

The search path for plugins can be overridden by the environment variable `LIBDECOR_PLUGIN_DIR`. To use the `cairo` plugin, point to the plugin directory:

`export LIBDECOR_PLUGIN_DIR=build/src/plugins/cairo/`

and run the demo:

`./build/demo/libdecor-demo`.
