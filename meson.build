project('libaudvd', 'c',
  version : '0.2.0',
  license : 'LGPL-2.1-or-later',
  meson_version : '>= 0.60.0',
  default_options : ['c_std=c99', 'warning_level=2', 'buildtype=debugoptimized'])

cc = meson.get_compiler('c')
ver = meson.project_version().split('.')

conf = configuration_data()
conf.set('AUDVD_VERSION_MAJOR', ver[0])
conf.set('AUDVD_VERSION_MINOR', ver[1])
conf.set('AUDVD_VERSION_MICRO', ver[2])
version_h = configure_file(input : 'include/audvd/audvd_version.h.in',
                           output : 'audvd_version.h',
                           configuration : conf)

m_dep = cc.find_library('m', required : false)

inc = include_directories('include')
gen_inc = include_directories('.')

c_args = ['-DAUDVD_BUILDING']
if get_option('default_library') != 'static'
  c_args += '-DAUDVD_SHARED'
endif

sources = files(
  'src/aob.c',
  'src/attr.c',
  'src/disc.c',
  'src/ifo_amg.c',
  'src/ifo_ats.c',
  'src/ifo_samg.c',
  'src/io.c',
  'src/log.c',
  'src/pack.c',
  'src/reader.c',
)

libaudvd = library('audvd', sources, version_h,
  include_directories : [inc, gen_inc],
  c_args : c_args,
  gnu_symbol_visibility : 'hidden',
  dependencies : m_dep,
  version : meson.project_version(),
  install : true)

audvd_dep = declare_dependency(link_with : libaudvd,
  include_directories : [inc, gen_inc],
  sources : version_h)
meson.override_dependency('audvd', audvd_dep)

install_headers('include/audvd/audvd.h', subdir : 'audvd')
install_headers(version_h, subdir : 'audvd')

pkg = import('pkgconfig')
pkg.generate(libaudvd,
  name : 'audvd',
  filebase : 'audvd',
  description : 'DVD-Audio navigation library',
  libraries_private : m_dep)

if get_option('tools')
  executable('audvd-info', 'tools/audvd-info.c',
    dependencies : audvd_dep, install : true)
endif

if get_option('tests')
  subdir('tests')
endif
