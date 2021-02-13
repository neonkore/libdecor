shared_module('decor-dummy',
  sources: [
    'libdecor-dummy.c',
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
  ],
  c_args: libdecor_c_args,
  dependencies: [
    libdecor_dep,
  ],
  install_dir: join_paths(plugindir),
  install: true,
)
