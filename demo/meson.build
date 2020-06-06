wayland_cursor_dep = dependency('wayland-cursor')

executable('libdecor-demo',
  sources: [
    'demo.c',
  ],
  include_directories: [
    top_includepath,
    libdecoration_includepath,
  ],
  dependencies: [libdecoration_dep, wayland_cursor_dep, cursor_settings_dep],
  install: false,
)

executable('libdecor-egl',
  sources: 'egl.c',
  include_directories: libdecoration_includepath,
  dependencies: libdecoration_dep,
  install: false,
)