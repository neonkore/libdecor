wayland_cursor_dep = dependency('wayland-cursor')

shared_module('decor-gtk',
  sources: [
    'libdecor-gtk.c',
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
    plugin_include_path,
  ],
  c_args: libdecor_c_args,
  dependencies: [
    libdecor_dep,
    cairo_dep,
    math_dep,
    wayland_cursor_dep,
    cursor_settings_dep,
    os_compatibility_dep,
    gtk_dep,
  ],
  link_with: plugin_common,
  install_dir: join_paths(plugindir),
  install: true,
)
