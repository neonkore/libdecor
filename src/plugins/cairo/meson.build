cairo_dep = dependency('cairo')
wayland_cursor_dep = dependency('wayland-cursor')
math_dep = cc.find_library('m', required: true)

shared_module('decoration-cairo',
  sources: [
    'libdecoration-cairo.c',
    'libdecoration-cairo-blur.c',
  ],
  include_directories: [
    top_includepath,
    libdecoration_includepath,
    include_directories('.'),
  ],
  c_args: libdecoration_c_args,
  dependencies: [
    libdecoration_dep,
    cairo_dep,
    math_dep,
    wayland_cursor_dep,
    cursor_settings_dep,
  ],
  install_dir: join_paths(plugindir),
  install: true,
)
