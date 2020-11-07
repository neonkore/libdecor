wayland_cursor_dep = dependency('wayland-cursor')

math_dep = cc.find_library('m')

egl_dep = dependency('egl')
gl_dep = dependency('gl')
wayland_egl_dep = dependency('wayland-egl')

executable('libdecor-demo',
  sources: [
    'demo.c',
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
  ],
  dependencies: [libdecor_dep, wayland_cursor_dep, cursor_settings_dep],
  install: false,
)

executable('libdecor-egl',
  sources: 'egl.c',
  include_directories: libdecor_includepath,
  dependencies: [libdecor_dep, wayland_egl_dep, egl_dep, gl_dep, math_dep],
  install: false,
)