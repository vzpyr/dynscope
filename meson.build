project(
  'dynscope',
  'c',
  version: '0.1.0',
  meson_version: '>=0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
    'default_library=static',
  ],
)

add_project_arguments('-D_GNU_SOURCE', '-DWLR_USE_UNSTABLE', language: 'c')

wayland_client_dep = dependency('wayland-client', version: '>=1.22.0')
wayland_server_dep = dependency('wayland-server', version: '>=1.22.0')
wayland_protos_dep = dependency('wayland-protocols')
glesv2_dep = dependency('glesv2')
xkbcommon_dep = dependency('xkbcommon')

wlroots_proj = subproject(
  'wlroots',
  default_options: ['default_library=static', 'examples=false'],
)
wlroots_dep = wlroots_proj.get_variable('wlroots')

wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dir = wayland_protos_dep.get_variable(pkgconfig: 'pkgdatadir')
xdg_shell_xml = join_paths(wayland_protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml')
linux_dmabuf_xml = join_paths(wayland_protocols_dir, 'stable', 'linux-dmabuf', 'linux-dmabuf-v1.xml')
cursor_shape_xml = join_paths(wayland_protocols_dir, 'staging', 'cursor-shape', 'cursor-shape-v1.xml')
tablet_xml = join_paths(wayland_protocols_dir, 'unstable', 'tablet', 'tablet-unstable-v2.xml')
pointer_constraints_xml = join_paths(wayland_protocols_dir, 'unstable', 'pointer-constraints', 'pointer-constraints-unstable-v1.xml')
relative_pointer_xml = join_paths(wayland_protocols_dir, 'unstable', 'relative-pointer', 'relative-pointer-unstable-v1.xml')
primary_selection_xml = join_paths(wayland_protocols_dir, 'unstable', 'primary-selection', 'primary-selection-unstable-v1.xml')
fractional_scale_xml = join_paths(wayland_protocols_dir, 'staging', 'fractional-scale', 'fractional-scale-v1.xml')
viewporter_xml = join_paths(wayland_protocols_dir, 'stable', 'viewporter', 'viewporter.xml')
xdg_decoration_xml = join_paths(wayland_protocols_dir, 'unstable', 'xdg-decoration', 'xdg-decoration-unstable-v1.xml')


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

cursor_shape_header = custom_target(
  'cursor-shape client header',
  input: cursor_shape_xml,
  output: 'cursor-shape-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

cursor_shape_code = custom_target(
  'cursor-shape private code',
  input: cursor_shape_xml,
  output: 'cursor-shape-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

tablet_code = custom_target(
  'tablet private code',
  input: tablet_xml,
  output: 'tablet-unstable-v2-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

pointer_constraints_header = custom_target(
  'pointer-constraints client header',
  input: pointer_constraints_xml,
  output: 'pointer-constraints-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

pointer_constraints_code = custom_target(
  'pointer-constraints private code',
  input: pointer_constraints_xml,
  output: 'pointer-constraints-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

relative_pointer_header = custom_target(
  'relative-pointer client header',
  input: relative_pointer_xml,
  output: 'relative-pointer-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

relative_pointer_code = custom_target(
  'relative-pointer private code',
  input: relative_pointer_xml,
  output: 'relative-pointer-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

primary_selection_header = custom_target(
  'primary-selection client header',
  input: primary_selection_xml,
  output: 'primary-selection-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

primary_selection_code = custom_target(
  'primary-selection private code',
  input: primary_selection_xml,
  output: 'primary-selection-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

fractional_scale_header = custom_target(
  'fractional-scale client header',
  input: fractional_scale_xml,
  output: 'fractional-scale-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

fractional_scale_code = custom_target(
  'fractional-scale private code',
  input: fractional_scale_xml,
  output: 'fractional-scale-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

viewporter_header = custom_target(
  'viewporter client header',
  input: viewporter_xml,
  output: 'viewporter-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

viewporter_code = custom_target(
  'viewporter private code',
  input: viewporter_xml,
  output: 'viewporter-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

xdg_decoration_header = custom_target(
  'xdg-decoration client header',
  input: xdg_decoration_xml,
  output: 'xdg-decoration-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

xdg_decoration_code = custom_target(
  'xdg-decoration private code',
  input: xdg_decoration_xml,
  output: 'xdg-decoration-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

protocol_lib = static_library(
  'dynscope-protocols',
  xdg_shell_code,
  linux_dmabuf_code,
  cursor_shape_code,
  tablet_code,
  pointer_constraints_code,
  relative_pointer_code,
  primary_selection_code,
  fractional_scale_code,
  viewporter_code,
  xdg_decoration_code,
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
  xdg_shell_header,
  linux_dmabuf_header,
  cursor_shape_header,
  pointer_constraints_header,
  relative_pointer_header,
  primary_selection_header,
  fractional_scale_header,
  viewporter_header,
  xdg_decoration_header,
  dependencies: [wayland_client_dep, wayland_server_dep, glesv2_dep, xkbcommon_dep, wlroots_dep, dependency('xcb'), dependency('xcb-xfixes'), dependency('pixman-1')],
  link_with: protocol_lib,
  include_directories: include_directories('src'),
)

