project(
  'dynscope',
  'c',
  version: '0.1.0',
  meson_version: '>=0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
  ],
)

add_project_arguments('-D_GNU_SOURCE', '-DWLR_USE_UNSTABLE', language: 'c')

wayland_client_dep = dependency('wayland-client', version: '>=1.22.0')
wayland_server_dep = dependency('wayland-server', version: '>=1.22.0')
wayland_protos_dep = dependency('wayland-protocols')
glesv2_dep = dependency('glesv2')
xkbcommon_dep = dependency('xkbcommon')

wlroots_proj = subproject('wlroots')
wlroots_dep = wlroots_proj.get_variable('wlroots')

wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dir = wayland_protos_dep.get_variable(pkgconfig: 'pkgdatadir')
xdg_shell_xml = join_paths(wayland_protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml')
linux_dmabuf_xml = join_paths(wayland_protocols_dir, 'stable', 'linux-dmabuf', 'linux-dmabuf-v1.xml')

xdg_shell_header = custom_target(
  'xdg-shell client header',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_code = custom_target(
  'xdg-shell private code',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

linux_dmabuf_header = custom_target(
  'linux-dmabuf client header',
  input: linux_dmabuf_xml,
  output: 'linux-dmabuf-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

linux_dmabuf_code = custom_target(
  'linux-dmabuf private code',
  input: linux_dmabuf_xml,
  output: 'linux-dmabuf-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

protocol_lib = static_library(
  'dynscope-protocols',
  xdg_shell_code,
  linux_dmabuf_code,
  c_args: ['-w'],
)

executable(
  'dynscope',
  'src/main.c',
  'src/child.c',
  'src/host.c',
  'src/server.c',
  'src/xwm.c',
  xdg_shell_header,
  linux_dmabuf_header,
  dependencies: [wayland_client_dep, wayland_server_dep, glesv2_dep, xkbcommon_dep, wlroots_dep],
  link_with: protocol_lib,
  include_directories: include_directories('src'),
)
