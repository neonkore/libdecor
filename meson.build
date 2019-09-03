project('libdecoration', 'c',
  version: '0.0.1',
  meson_version: '>= 0.45.0',
  license: 'MIT'
)

cc = meson.get_compiler('c')

add_project_arguments('-D_GNU_SOURCE', language: 'c')

wayland_client_req = '>= 1.0'
wayland_protocols_req = '>= 1.14'

wayland_client_dep = dependency('wayland-client', version: wayland_client_req)
wayland_protocols_dep = dependency('wayland-protocols', version: wayland_protocols_req)
dl_dep = cc.find_library('dl', required: true)

required_functions = [
  'mkostemp',
  'posix_fallocate',
  'asprintf',
]
foreach function : required_functions
  if not cc.has_function(function)
    error('Required function ' + function + ' missing')
  endif
endforeach

prefix = get_option('prefix')
libdir = join_paths(prefix, get_option('libdir'))

pkgname = meson.project_name()
pkglibdir = join_paths(libdir, pkgname)

plugindir = join_paths(pkglibdir, 'plugins')

libdecoration_c_args = [
  '-fvisibility=hidden',
]

cdata = configuration_data()
cdata.set_quoted('VERSION', meson.project_version())
cdata.set_quoted('LIBDECOR_PLUGIN_DIR', plugindir)

config_h = configure_file(
  input: 'config.h.meson',
  output: 'config.h',
  configuration: cdata
)

top_includepath = include_directories('.')

subdir('src')
subdir('demo')
