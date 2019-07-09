cairo_dep = dependency('cairo')
wayland_cursor_dep = dependency('wayland-cursor')

shared_module('decoration-cairo',
  sources: [
    'libdecoration-cairo.c',
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
    wayland_cursor_dep,
  ],
  install_dir: join_paths(plugindir, 'cairo'),
  install: true,
)
