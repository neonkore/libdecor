cairo_dep = dependency('cairo')

shared_module('decoration-cairo',
  sources: [
    'libdecoration-cairo.c',
  ],
  include_directories: [
    top_includepath,
    libdecoration_includepath,
    include_directories('.'),
  ],
  c_args: libdecoration_c_args,
  dependencies: [
    libdecoration_dep,
    cairo_dep,
  ],
  install_dir: join_paths(plugindir, 'cairo'),
  install: true,
)
