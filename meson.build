project('virtio-accel', 'c',
  meson_version : '>=1.1',
  version : run_command('scripts/generate-version.sh',
    '', '--no-dirty', check: false).stdout().strip())

virtio_accel_dep = declare_dependency(
  include_directories : include_directories('.'))

opt_virtio_accel = get_option('virtio-accel')
if opt_virtio_accel.enabled()
  virtio_accel_sources = files([
    'Makefile',
    'virtio_accel-mgr.c',
    'virtio_accel-reqs.c',
    'virtio_accel.h',
    'accel.c',
    'virtio_accel-common.h',
    'virtio_accel-prof.c',
    'virtio_accel-sess.c',
    'accel.h',
    'virtio_accel-core.c',
    'virtio_accel-prof.h',
    'virtio_accel-zc.c'])

  opt_kdir = get_option('kdir')
  opt_debug = get_option('debug_mode')
  opt_zc = get_option('zc')
  opt_profiling = get_option('profiling')
  
  env_vars = {
    'ZC': opt_zc.to_string(),
    'DEBUG': opt_debug.to_string(),
    'PROFILING': opt_profiling.to_string(),
  }
  if opt_kdir != ''
    env_vars += {'KDIR': opt_kdir}
  endif
  
  make_bin = find_program('make')
  make_sh = files(meson.project_source_root() + '/scripts/make.sh')
  virtio_accel = custom_target('virtio-accel',
    capture : false,
    console : true,
    build_by_default : true,
    depend_files : virtio_accel_sources,
    input : files('Makefile'),
    output : ['virtio_accel.ko'],
    env : env_vars,
    command : [make_sh, make_bin,
      meson.current_source_dir(), meson.current_build_dir(), 'virtio_accel.ko'])
  
  summary({
    'Kernel source directory' : opt_kdir,
    'Enable debug': opt_debug,
    'Enable guest zero-copy': opt_zc,
    'Enable profiling': opt_profiling,
    },
    section : 'Configuration',
    bool_yn : true)
endif 
