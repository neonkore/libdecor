cairo_dep = dependency('cairo')
math_dep = cc.find_library('m', required: true)

plugin_common = static_library('cairo-blur',
  sources: [
    'libdecor-cairo-blur.c',
  ],
  dependencies: [
    cairo_dep,
    math_dep,
  ],
  install: false,
)

os_utils = static_library('os-utils',
  sources: [
    'libdecor-os-utils.c',
    'libdecor-process-utils.c',
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
  ],
  dependencies: [
  ],
  install: false,
)

os_utils_dep = declare_dependency(
  link_with: os_utils,
)
