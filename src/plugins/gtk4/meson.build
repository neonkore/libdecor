
# Format:
#  - protocol stability
#  - protocol name
#  - optional: protocol version, if unstable
wayland_protocols = [
  ['stable', 'xdg-shell'],
  ['unstable', 'linux-dmabuf', '1'],
  ['local', 'wayland-drm'],
  ['local', 'libdecor-shell'],
]

built_gtk4_sources = {}

foreach p: wayland_protocols
  stability = p.get(0)
  name = p.get(1)

  assert(stability in ['stable', 'unstable', 'local'],
         'protocol \'@0@\' must be \'stable\', \'unstable\' or \'local\''.format(name))

  suffix = stability == 'unstable' ? '-unstable-v@0@'.format(p.get(2)) : ''

  output_base = name
  if stability == 'local'
    input = name+'.xml'
  else
    input = join_paths(protocols_dir, stability, name, name+suffix+'.xml')
  endif

  client_header = custom_target('@0@ client header'.format(output_base),
    input: input,
    output: '@0@-client-protocol.h'.format(output_base),
    command: [
      wayland_scanner,
      'client-header',
      '@INPUT@', '@OUTPUT@',
    ]
  )
  server_header = custom_target('@0@ server header'.format(output_base),
    input: input,
    output: '@0@-server-protocol.h'.format(output_base),
    command: [
      wayland_scanner,
      'server-header',
      '@INPUT@', '@OUTPUT@',
    ]
  )
  protocol_source = custom_target('@0@ source'.format(output_base),
    input: input,
    output: '@0@-protocol.c'.format(output_base),
    command: [
      wayland_scanner,
      'private-code',
      '@INPUT@', '@OUTPUT@',
    ]
  )
  built_gtk4_sources += {name: [client_header, server_header, protocol_source]}
endforeach

wayland_server_dep = dependency('wayland-server', version: '>=1.21')
#wayland_cursor_dep = dependency('wayland-cursor')

built_plugin_sources = [
  built_gtk4_sources['linux-dmabuf'],
  built_gtk4_sources['xdg-shell'],
  built_gtk4_sources['wayland-drm'],
  built_gtk4_sources['libdecor-shell'][0],
  built_gtk4_sources['libdecor-shell'][2],
]

shared_module('decor-gtk4',
  sources: [
    'libdecor-gtk4.c',
    'libdecor-gtk4-tunnels.c',
    built_plugin_sources,
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
    plugin_include_path,
  ],
  c_args: libdecor_c_args,
  dependencies: [
    libdecor_dep,
    #cairo_dep,
    #math_dep,
    #wayland_cursor_dep,
    wayland_client_dep,
    wayland_server_dep,
    #cursor_settings_dep,
    os_utils_dep,
  ],
  install_dir: join_paths(plugindir),
  install: true,
)

built_client_sources = [
  built_gtk4_sources['linux-dmabuf'][0],
  built_gtk4_sources['linux-dmabuf'][2],
  built_gtk4_sources['xdg-shell'][0],
  built_gtk4_sources['xdg-shell'][2],
  built_gtk4_sources['libdecor-shell'][0],
  built_gtk4_sources['libdecor-shell'][2],
]

executable('libdecor-gtk4-client',
  sources: [
    'libdecor-gtk4-client.c',
    built_client_sources,
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
    plugin_include_path,
  ],
  c_args: libdecor_c_args,
  dependencies: [
    gtk4_dep,
    wayland_client_dep,
  ],
  install_dir: join_paths(plugindir),
  install: true,
)
