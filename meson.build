project(
  'dynscope',
  'c',
  version: '0.1.1',
  meson_version: '>=0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
    'buildtype=release',
    'strip=true',
    'default_library=static',
  ],
)

add_project_arguments('-D_GNU_SOURCE', '-DWLR_USE_UNSTABLE', language: 'c')

wayland_client_dep = dependency('wayland-client', version: '>=1.22.0')
wayland_server_dep = dependency('wayland-server', version: '>=1.22.0')
wayland_protos_dep = dependency('wayland-protocols')
glesv2_dep = dependency('glesv2')
xkbcommon_dep = dependency('xkbcommon')
xcb_dep = dependency('xcb')
xcb_xfixes_dep = dependency('xcb-xfixes')
pixman_dep = dependency('pixman-1')

wlroots_proj = subproject(
  'wlroots',
  default_options: ['default_library=static', 'examples=false'],
)
wlroots_dep = wlroots_proj.get_variable('wlroots')

wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dir = wayland_protos_dep.get_variable(pkgconfig: 'pkgdatadir')

client_protocols = [
  ['xdg-shell', 'stable/xdg-shell/xdg-shell.xml'],
  ['linux-dmabuf-v1', 'stable/linux-dmabuf/linux-dmabuf-v1.xml'],
  ['cursor-shape-v1', 'staging/cursor-shape/cursor-shape-v1.xml'],
  ['pointer-constraints-unstable-v1', 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
  ['relative-pointer-unstable-v1', 'unstable/relative-pointer/relative-pointer-unstable-v1.xml'],
  ['primary-selection-unstable-v1', 'unstable/primary-selection/primary-selection-unstable-v1.xml'],
  ['fractional-scale-v1', 'staging/fractional-scale/fractional-scale-v1.xml'],
  ['viewporter', 'stable/viewporter/viewporter.xml'],
  ['xdg-decoration-unstable-v1', 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
]

proto_src = []
proto_headers = []

foreach p : client_protocols
  proto_headers += custom_target(
    p[0] + ' client header',
    input: join_paths(wayland_protocols_dir, p[1]),
    output: p[0] + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )

  proto_src += custom_target(
    p[0] + ' private code',
    input: join_paths(wayland_protocols_dir, p[1]),
    output: p[0] + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

protocol_lib = static_library(
  'dynscope-protocols',
  proto_src,
  c_args: ['-w'],
)

executable(
  'dynscope',
  'src/main.c',
  'src/child.c',
  'src/host.c',
  'src/server.c',
  'src/xwm.c',
  'src/xcursor.c',
  'src/clipboard.c',
  proto_headers,
  dependencies: [
    wayland_client_dep,
    wayland_server_dep,
    glesv2_dep,
    xkbcommon_dep,
    wlroots_dep,
    xcb_dep,
    xcb_xfixes_dep,
    pixman_dep,
  ],
  link_with: protocol_lib,
  include_directories: include_directories('src'),
  install: true,
)
