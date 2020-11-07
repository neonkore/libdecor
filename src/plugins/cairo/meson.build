cairo_dep = dependency('cairo')
wayland_cursor_dep = dependency('wayland-cursor')
math_dep = cc.find_library('m', required: true)

shared_module('decor-cairo',
  sources: [
    'libdecor-cairo.c',
    'libdecor-cairo-blur.c',
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
  ],
  c_args: libdecor_c_args,
  dependencies: [
    libdecor_dep,
    cairo_dep,
    math_dep,
    wayland_cursor_dep,
    cursor_settings_dep,
  ],
  install_dir: join_paths(plugindir),
  install: true,
)
