add_languages('cpp')

wayland_cursor_dep = dependency('wayland-cursor')

math_dep = cc.find_library('m')

egl_dep = dependency('egl')
gl_dep = dependency('opengl')
wayland_egl_dep = dependency('wayland-egl')

xkb_dep = dependency('xkbcommon')

xdg_shell_path = join_paths(protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml')

xdg_shell_client_header = custom_target('xdg-shell client header (demo)',
  input: xdg_shell_path,
  output: 'xdg-shell-client-protocol.h',
  command: [
    wayland_scanner,
    'client-header',
    '@INPUT@', '@OUTPUT@',
  ]
)

xdg_shell_source = custom_target('xdg-shell source (demo)',
  input: xdg_shell_path,
  output: 'xdg-shell-protocol.c',
  command: [
    wayland_scanner,
    'private-code',
    '@INPUT@', '@OUTPUT@',
  ]
)

executable('libdecor-demo',
  sources: [
    'demo.c',
    xdg_shell_client_header,
    xdg_shell_source,
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
  ],
  dependencies: [
    libdecor_dep,
    wayland_cursor_dep,
    cursor_settings_dep,
    os_compatibility_dep,
    xkb_dep],
  install: get_option('install_demo'),
)

executable('libdecor-egl',
  sources: 'egl.c',
  include_directories: libdecor_includepath,
  dependencies: [
    libdecor_dep,
    wayland_egl_dep,
    egl_dep,
    gl_dep,
    math_dep],
  install: get_option('install_demo'),
)

executable('libdecor-c++-demo',
  sources: [
    'c++-demo.cc',
  ],
  cpp_args: [
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
  ],
  dependencies: [
    libdecor_dep,
    wayland_cursor_dep,
    cursor_settings_dep,
    os_compatibility_dep,
    xkb_dep],
  install: get_option('install_demo'),
)
