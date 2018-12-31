project('libdecoration', 'c',
  version: '0.0.1',
  meson_version: '>= 0.46.0',
  license: 'MIT'
)

cc = meson.get_compiler('c')

add_project_arguments('-D_GNU_SOURCE', language: 'c')

wayland_client_req = '>= 1.0'
wayland_protocols_req = '>= 1.14'

wayland_client_dep = dependency('wayland-client', version: wayland_client_req)
wayland_protocols_dep = dependency('wayland-protocols', version: wayland_protocols_req)

required_functions = [
  'mkostemp',
  'posix_fallocate',
]
foreach function : required_functions
  if not cc.has_function(function)
    error('Required function ' + function + ' missing')
  endif
endforeach

cdata = configuration_data()
cdata.set_quoted('VERSION', meson.project_version())

config_h = configure_file(
  input: 'config.h.meson',
  output: 'config.h',
  configuration: cdata
)

top_includepath = include_directories('.')

subdir('src')
subdir('demo')
