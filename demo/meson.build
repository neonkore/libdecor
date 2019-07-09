wayland_cursor_dep = dependency('wayland-cursor')

executable('libdecor-demo',
  sources: [
    'demo.c',
  ],
  include_directories: [
    top_includepath,
    libdecoration_includepath,
  ],
  dependencies: [libdecoration_dep, wayland_cursor_dep],
  install: false,
)
