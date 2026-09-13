project('drm_splash', 'c',
  version: '1.0.0',
  license: 'MIT',
  meson_version: '>=0.61.0')

cc = meson.get_compiler('c')

# libdrm 依赖：优先使用 pkg-config，找不到则回退到 -ldrm
drm_dep = dependency('libdrm', required: false)
if not drm_dep.found()
  drm_dep = cc.find_library('drm', required: true)
endif

libpng_dep = dependency('libpng', required: true)

sources = files(
  'src/main.c',
  'src/helper.c'
)

executable('drm_splash',
  sources,
  dependencies: [drm_dep, libpng_dep],
  install: true,
)

executable('unlock_vt',
  ['src/unlock_vt.c'],
  install: true,
)
