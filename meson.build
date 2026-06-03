project('ME3 TOCFix', 'cpp',
  version: '0.2',
  default_options: [
    'cpp_std=c++23',
    'warning_level=2'
  ]
)

cc = meson.get_compiler('cpp')

sources = [
  'main.cpp',
  'ini_parser.cpp',
  'toc_bin_file.cpp',
  'toc_creator.cpp',
]

inc_dirs = include_directories('.')

if cc.get_id() == 'msvc'
  add_project_arguments(['/DUNICODE', '/D_WIN32_WINNT=0x0601'], language: 'cpp')
  add_project_link_arguments(['/DEFAULTLIB:advapi32.lib'], language: 'cpp')
elif host_machine.system() == 'windows'
  add_project_arguments(['-DUNICODE', '-D_WIN32_WINNT=0x0601'], language: 'cpp')
  add_project_link_arguments(['-ladvapi32'], language: 'cpp')
endif

exe = executable('me3-tocfix',
  sources,
  include_directories: inc_dirs,
  install: true
)

test('basic', exe)
