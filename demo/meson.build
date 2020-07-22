wayland_cursor_dep = dependency('wayland-cursor')

math_dep = cc.find_library('m')

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

executable('libdecor-demo-static',
  sources: ['demo.c',],
  include_directories: [
    top_includepath,
    libdecoration_includepath,
  ],
  c_args : '-DCUSTOM_PLUGIN',
  dependencies: [libdecoration_static_dep, wayland_cursor_dep, cursor_settings_dep],
  install: false,
)

executable('libdecor-egl',
  sources: 'egl.c',
  include_directories: libdecoration_includepath,
  dependencies: [libdecoration_dep, wayland_egl_dep, egl_dep, gl_dep, math_dep],
  install: false,
)
