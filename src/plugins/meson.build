gtk_dep = dependency('gtk+-3.0', required: get_option('gtk'))
gtk4_dep = dependency('gtk4', required: get_option('gtk4'))

plugin_include_path = include_directories('.')

subdir('common')
subdir('cairo')
subdir('dummy')

if gtk_dep.found()
  subdir('gtk')
endif

if gtk4_dep.found()
  subdir('gtk4')
endif
