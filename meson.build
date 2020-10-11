project('libdecoration', 'c',
  version: '0.0.1',
  meson_version: '>= 0.47.0',
  license: 'MIT'
)

version_array = meson.project_version().split('.')

libdecoration_api_version = version_array[0]
libdecoration_api_version_string = '@0@'.format(libdecoration_api_version)

libdecoration_name = 'decoration-' + libdecoration_api_version_string
libdecoration_full_name = 'libdecoration-' + libdecoration_api_version_string

cc = meson.get_compiler('c')

add_project_arguments('-D_GNU_SOURCE', language: 'c')

add_project_arguments(cc.get_supported_arguments([
  '-Wno-unused-parameter',
  '-Wno-missing-field-initializers',
  ]), language: 'c')

wayland_client_req = '>= 1.18'
wayland_protocols_req = '>= 1.15'
dbus_req = '>= 1.0'

wayland_client_dep = dependency('wayland-client', version: wayland_client_req)
wayland_protocols_dep = dependency('wayland-protocols', version: wayland_protocols_req)
dl_dep = cc.find_library('dl', required: true)

dbus_dep = dependency('dbus-1', version: dbus_req, required: get_option('dbus'))

# list of (function, prefix)
required_functions = [
  ['memfd_create', '#define _GNU_SOURCE\n#include <sys/mman.h>'],
  ['posix_fallocate', '#include <fcntl.h>'],
  ['asprintf', '#define _GNU_SOURCE\n#include <stdio.h>'],
]
foreach function : required_functions
  if not cc.has_function(function[0], prefix: function[1])
    error('Required function ' + function[0] + ' missing')
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
if dbus_dep.found()
  cdata.set('HAS_DBUS', true)
endif

config_h = configure_file(
  input: 'config.h.meson',
  output: 'config.h',
  configuration: cdata
)

top_includepath = include_directories('.')

subdir('src')

if get_option('demo')
  subdir('demo')
endif
