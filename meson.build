project(
  'dynscope',
  'c',
  version: '0.1.0',
  meson_version: '>=0.60.0',
  default_options: [
    'c_std=c99',
    'warning_level=3',
    'werror=true',
  ],
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')

wayland_client_dep = dependency('wayland-client', version: '>=1.22.0')
wayland_server_dep = dependency('wayland-server', version: '>=1.22.0')
wayland_protos_dep = dependency('wayland-protocols')

wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dir = wayland_protos_dep.get_variable(pkgconfig: 'pkgdatadir')
xdg_shell_xml = join_paths(wayland_protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml')

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

xdg_shell_lib = static_library(
  'xdg-shell-protocol',
  xdg_shell_code,
  c_args: ['-w'],
)

executable(
  'dynscope',
  'src/main.c',
  'src/child.c',
  'src/host.c',
  xdg_shell_header,
  dependencies: [wayland_client_dep, wayland_server_dep],
  link_with: xdg_shell_lib,
  include_directories: include_directories('src'),
)
