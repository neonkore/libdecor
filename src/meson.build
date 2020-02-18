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
  'libdecoration-plugin.h',
]
install_headers(libdecoration_headers, subdir : 'libdecoration')

libdecoration_built_sources = []

wayland_scanner = find_program('wayland-scanner')

# Format:
#  - protocol name
wayland_protocols = [
  ['xdg-shell'],
]

protocols_dir = wayland_protocols_dep.get_pkgconfig_variable('pkgdatadir')
assert(protocols_dir != '', 'Could not get pkgdatadir from wayland-protocols.pc')

foreach p: wayland_protocols
  protocol_name = p.get(0)

  output_base = protocol_name
  input = join_paths(protocols_dir,
                     'stable/@0@/@0@.xml'.format(protocol_name))

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

libdecoration = shared_library('decoration',
  sources: [
    libdecoration_sources,
    libdecoration_built_sources,
  ],
  include_directories: libdecoration_includes,
  c_args: libdecoration_c_args,
  dependencies: [
    wayland_client_dep,
    dl_dep,
  ],
  install: true
)

libdecoration_dep = declare_dependency(
  link_with: libdecoration,
  dependencies: [
    wayland_client_dep,
  ],
)

subdir('plugins')

pkg.generate(
  name: 'decoration',
  description: 'library for Wayland client-side window decorations',
  libraries: libdecoration,
  version: meson.project_version())