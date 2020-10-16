pkg = import('pkgconfig')

libdecoration_includepath = include_directories('.')

libdecoration_includes = [
  libdecoration_includepath,
  top_includepath,
]

libdecoration_sources = [
  'libdecoration.c',
  'libdecoration-fallback.c',
]

libdecoration_headers = [
  'libdecoration.h',
]
install_headers(libdecoration_headers,
  subdir: '@0@'.format(libdecoration_full_name),
)

libdecoration_built_sources = []

wayland_scanner = find_program('wayland-scanner')

# Format:
#  - protocol stability
#  - protocol name
#  - optional: protocol version, if unstable
wayland_protocols = [
  ['stable', 'xdg-shell'],
  ['unstable', 'xdg-decoration', '1'],
]

protocols_dir = wayland_protocols_dep.get_pkgconfig_variable('pkgdatadir')
assert(protocols_dir != '', 'Could not get pkgdatadir from wayland-protocols.pc')

foreach p: wayland_protocols
  stability = p.get(0)
  name = p.get(1)

  assert(stability in ['stable', 'unstable'],
         'protocol \'@0@\' must be \'stable\' or \'unstable\''.format(name))

  suffix = stability=='unstable' ? '-unstable-v@0@'.format(p.get(2)) : ''

  output_base = name
  input = join_paths(protocols_dir, stability, name, name+suffix+'.xml')

  libdecoration_built_sources += custom_target('@0@ client header'.format(output_base),
    input: input,
    output: '@0@-client-protocol.h'.format(output_base),
    command: [
      wayland_scanner,
      'client-header',
      '@INPUT@', '@OUTPUT@',
    ]
  )
  libdecoration_built_sources += custom_target('@0@ source'.format(output_base),
    input: input,
    output: '@0@-protocol.c'.format(output_base),
    command: [
      wayland_scanner,
      'private-code',
      '@INPUT@', '@OUTPUT@',
    ]
  )
endforeach

## cursor settings
cursor_settings = static_library('cursor_settings',
  sources: ['cursor-settings.c'],
  include_directories: [top_includepath],
  dependencies: [dbus_dep],
)

cursor_settings_dep = declare_dependency(
  link_with: cursor_settings,
  dependencies: [dbus_dep],
)

## core decoration library
libdecoration = both_libraries(libdecoration_name,
  sources: [
    libdecoration_sources,
    libdecoration_built_sources,
  ],
  include_directories: libdecoration_includes,
  c_args: libdecoration_c_args,
  dependencies: [
    wayland_client_dep,
    dl_dep,
    cursor_settings_dep,
  ],
  version: '@0@.@1@'.format(version_array[0], version_array[1]),
  install: true
)

libdecoration_dep = declare_dependency(
  link_with: libdecoration,
  dependencies: [
    wayland_client_dep,
  ],
)

libdecoration_static_dep = declare_dependency(
  link_with: libdecoration.get_static_lib(),
  dependencies: [
    wayland_client_dep,
  ],
)

## decoration plugins
subdir('plugins')

## pkg-config file
pkg.generate(
  name: libdecoration_full_name,
  description: 'library for Wayland client-side window decorations',
  libraries: libdecoration,
  subdirs: libdecoration_full_name,
  version: meson.project_version()
)
