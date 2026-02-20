project('swl', 'c',
  version: '1.0.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ])

cc = meson.get_compiler('c')

# Required dependencies
wlroots = dependency('wlroots-0.19', required: false)
if not wlroots.found()
  wlroots = dependency('wlroots')
endif

wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')
pixman = dependency('pixman-1')
scenefx = dependency('scenefx-0.4', required: false)
if not scenefx.found()
  scenefx = dependency('scenefx')
endif

# Required for wlroots unstable API
add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

# Optional XWayland
xwayland_opt = get_option('xwayland')
if xwayland_opt
  xcb = dependency('xcb')
  xcb_icccm = dependency('xcb-icccm')
  add_project_arguments('-DSWL_XWAYLAND', language: 'c')
endif

# Wayland protocols
wayland_scanner = find_program('wayland-scanner')
wayland_protos_dir = wayland_protos.get_variable('pkgdatadir')

protocols = [
  wayland_protos_dir / 'stable/xdg-shell/xdg-shell.xml',
  wayland_protos_dir / 'stable/tablet/tablet-v2.xml',
  wayland_protos_dir / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml',
  wayland_protos_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml',
  wayland_protos_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
  # wlr-protocols (not in wayland-protocols)
  meson.current_source_dir() / 'protocols/wlr-layer-shell-unstable-v1.xml',
  meson.current_source_dir() / 'protocols/wlr-output-power-management-unstable-v1.xml',
]

protocol_headers = []
protocol_sources = []

foreach p : protocols
  name = p.split('/')[-1].split('.')[0]
  protocol_headers += custom_target(name + '-header',
    input: p,
    output: name + '-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])
  protocol_sources += custom_target(name + '-code',
    input: p,
    output: name + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

swl_inc = include_directories('include', '.', 'lib/tomlc99')

swl_sources = files(
  'src/main.c',
  # Vendored libraries
  'lib/tomlc99/toml.c',
  # Core
  'src/core/compositor.c',
  'src/core/events.c',
  'src/core/error.c',
  'src/core/signal.c',
  # Client
  'src/client/client.c',
  'src/client/rules.c',
  # Input
  'src/input/input.c',
  'src/input/keyboard.c',
  'src/input/pointer.c',
  'src/input/keybindings.c',
  'src/input/switch.c',
  # Output
  'src/output/monitor.c',
  # Layout
  'src/layout/registry.c',
  'src/layout/scroller.c',
  'src/layout/floating.c',
  # Config
  'src/config/config.c',
  # Render
  'src/render/renderer.c',
  'src/render/scene.c',
  # IPC
  'src/ipc/ipc.c',
  'src/ipc/socket.c',
  'src/ipc/commands.c',
  # Protocols
  'src/protocols/decoration.c',
  'src/protocols/xdg_shell.c',
  'src/protocols/session_lock.c',
  # Layer shell
  'src/layer/layer.c',
  # Foreign toplevel manager
  'src/toplevel/toplevel.c',
)

swl_deps = [
  scenefx,
  wlroots,
  wayland_server,
  wayland_client,
  xkbcommon,
  libinput,
  pixman,
]

if xwayland_opt
  swl_sources += files(
    'src/xwayland/xwayland.c',
    'src/client/client_x11.c',
  )
  swl_deps += [xcb, xcb_icccm]
endif

executable('swl',
  sources: [swl_sources, protocol_headers, protocol_sources],
  dependencies: swl_deps,
  include_directories: swl_inc,
  install: true)

executable('swlctl',
  sources: ['tools/swlctl.c'],
  install: true)

install_data('swl.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions')

# Create static library for testable code (no main.c, no wlroots-dependent code)
swl_testable_sources = files(
  'lib/tomlc99/toml.c',
  'src/core/events.c',
  'src/core/error.c',
  'src/config/config.c',
  'src/layout/registry.c',
  'src/layout/scroller.c',
  'src/layout/floating.c',
  'src/client/rules.c',
)

swl_testable = static_library('swl_testable',
  sources: swl_testable_sources,
  include_directories: swl_inc)

subdir('tests')
