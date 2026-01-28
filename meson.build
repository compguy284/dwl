project('macwc', 'c',
  version: '0.8-dev',
  default_options: [
    'warning_level=3',
    'werror=false',
  ],
)

cc = meson.get_compiler('c')

# Version from git
version = vcs_tag(
  command: ['git', 'describe', '--tags', '--dirty'],
  input: 'version.h.in',
  output: 'version.h',
  fallback: meson.project_version(),
)

# --- Dependencies ---
wlroots    = dependency('wlroots-0.19')
scenefx    = dependency('scenefx-0.4', include_type: 'system')
wayland    = dependency('wayland-server')
xkbcommon  = dependency('xkbcommon')
libinput   = dependency('libinput')
math       = cc.find_library('m')

base_deps = [wlroots, scenefx, wayland, xkbcommon, libinput, math]

# XWayland (optional)
xwayland_option = get_option('xwayland')
if xwayland_option.enabled() or (xwayland_option.auto() and
    cc.has_header('xcb/xcb.h'))
  xcb     = dependency('xcb', required: xwayland_option)
  xcb_icccm = dependency('xcb-icccm', required: xwayland_option)
  if xcb.found() and xcb_icccm.found()
    base_deps += [xcb, xcb_icccm]
    add_project_arguments('-DXWAYLAND', language: 'c')
  endif
endif

# --- Compiler flags ---
add_project_arguments(
  '-DWLR_USE_UNSTABLE',
  '-D_POSIX_C_SOURCE=200809L',
  '-Wdeclaration-after-statement',
  '-Wno-unused-parameter',
  '-Wshadow',
  '-Wunused-macros',
  '-Werror=strict-prototypes',
  '-Werror=implicit',
  '-Werror=return-type',
  '-Werror=incompatible-pointer-types',
  '-Wfloat-conversion',
  language: 'c',
)

# --- AddressSanitizer ---
if get_option('asan')
  add_project_arguments(
    '-fsanitize=address',
    '-fno-omit-frame-pointer',
    '-fno-optimize-sibling-calls',
    language: 'c',
  )
  add_project_link_arguments('-fsanitize=address', language: 'c')
endif

# --- Protocol generation ---
wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner = find_program(
  wayland_scanner_dep.get_variable('wayland_scanner'),
)
wayland_protocols_dep = dependency('wayland-protocols')
wayland_protocols_dir = wayland_protocols_dep.get_variable('pkgdatadir')

protocols = [
  ['cursor-shape-v1', 'enum-header',
   wayland_protocols_dir / 'staging/cursor-shape/cursor-shape-v1.xml'],
  ['pointer-constraints-unstable-v1', 'enum-header',
   wayland_protocols_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
  ['wlr-layer-shell-unstable-v1', 'enum-header',
   'protocols/wlr-layer-shell-unstable-v1.xml'],
  ['wlr-output-power-management-unstable-v1', 'server-header',
   'protocols/wlr-output-power-management-unstable-v1.xml'],
  ['xdg-shell', 'server-header',
   wayland_protocols_dir / 'stable/xdg-shell/xdg-shell.xml'],
]

protocol_headers = []
foreach p : protocols
  protocol_headers += custom_target(
    p[0] + '-protocol.h',
    input: p[2],
    output: p[0] + '-protocol.h',
    command: [wayland_scanner, p[1], '@INPUT@', '@OUTPUT@'],
  )
endforeach

# --- Sources ---
src = files(
  'src/macwc.c',
  'src/util.c',
  'src/visual.c',
  'src/session.c',
  'src/client_funcs.c',
  'src/pointer.c',
  'src/input.c',
  'src/layout.c',
  'src/monitor.c',
  'src/config_parser.c',
)

# --- Build ---
executable('macwc',
  src, version, protocol_headers,
  dependencies: base_deps,
  include_directories: 'src',
  install: true,
)

# --- Install ---
install_man('macwc.1')
install_data('macwc.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions',
)
install_data('config/config',
  install_dir: get_option('datadir') / 'macwc',
)
