# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

macwc is a compact, hackable Wayland compositor based on wlroots and scenefx. Configuration is done via a runtime config file at `$XDG_CONFIG_HOME/macwc/config` with live reload via SIGHUP.

**Note**: This project is unmaintained as of August 2025.

**Note**: The traditional tagging system has been removed. This fork uses a single-workspace model where all windows share one workspace per monitor.

## Build Commands

```bash
# Configure
meson setup build

# Build
meson compile -C build

# Install (may require sudo)
meson install -C build

# Clean (remove build directory)
rm -rf build

# Build with XWayland support
meson setup build -Dxwayland=enabled
meson compile -C build
```

**Nix users**:
```bash
nix develop    # Enter development shell with all dependencies
nix build      # Build the package
```

## Configuration

Configuration uses a runtime config file:
- **Path**: `$XDG_CONFIG_HOME/macwc/config` (default: `~/.config/macwc/config`)
- **Format**: `key = value`, one per line, `#` comments, blank lines ignored
- **Reload**: `kill -SIGHUP $(pidof macwc)` to apply changes without restarting
- **Reference**: See `config/config` for all settings with documentation

If no config file exists, built-in defaults are used. All settings in `config/config`:
- **Appearance**: `borderpx`, `sloppyfocus`, colors (`bordercolor`, `focuscolor`, `urgentcolor`, `rootcolor`)
- **Gaps**: `smartgaps`, `monoclegaps`, `gappih`/`gappiv`/`gappoh`/`gappov`
- **Opacity**: `opacity`, `opacity_inactive`, `opacity_active`
- **Shadows**: `shadow`, `shadow_color`, `shadow_color_focus`, `shadow_blur_sigma`
- **Corner radius**: `corner_radius`, `corner_radius_inner`, `corner_radius_only_floating`
- **Blur**: `blur`, `blur_num_passes`, `blur_radius`, `blur_noise`, `blur_brightness`, etc.
- **Keyboard**: `xkb_layout`, `repeat_rate`, `repeat_delay`, `numlock`
- **Trackpad**: `tap_to_click`, `natural_scrolling`, `accel_profile`, etc.
- **Keybindings**: `bind = mod+shift Return spawn alacritty` (default modifier is Alt)
- **Mouse buttons**: `button = mod BTN_LEFT moveresize move`
- **Rules**: `rule = app_id, title, isfloating, monitor`
- **Monitor rules**: `monrule = name, mfact, nmaster, scale, layout, transform, x, y`
- **Scroller**: `scroller_center_mode`, `scroller_proportions`
- **Commands**: `termcmd`, `menucmd`

## Architecture

### Source Layout

All source files live in `src/`.

- **src/macwc.c** (~3800 lines): Main compositor implementation
- **src/client.h**: Inline helper functions for client/window management
- **src/util.c/h**: Small utilities (die, ecalloc, fd_set_nonblock)
- **src/config_parser.c/h**: Runtime configuration parser with SIGHUP reload
- **config/config**: Default/reference configuration file
- **src/visual.c/h**: Visual effects (opacity, shadows, corners, blur)
- **src/session.c/h**: Session lock handling
- **src/client_funcs.c/h**: Client management functions
- **src/pointer.c/h**: Pointer/cursor handling
- **src/input.c/h**: Keyboard input handling
- **src/layout.c/h**: Layout algorithms (tile, monocle, scroller)
- **src/monitor.c/h**: Monitor management

### Key Data Structures (src/macwc.c)

- **Client**: Represents a window - contains scene tree, borders, surface (XDG or XWayland), geometry, event listeners, and properties (floating, fullscreen, opacity, corner_radius, shadow, scroller_col)
- **Monitor**: Represents a display - contains output, layers array, layout state, master factor, gap settings (gappih, gappiv, gappoh, gappov), scroller state (scroller_proportion_idx, scroller_viewport_x), and blur layer
- **LayerSurface**: Layer shell surfaces (bars, notifications, overlays)
- **KeyboardGroup**: Unified keyboard input handling

### Layer System

Monitors have 9 layers in stacking order:
1. LyrBg (background)
2. LyrBlur (blur effects)
3. LyrBottom
4. LyrTile (tiled windows)
5. LyrFloat (floating windows)
6. LyrTop
7. LyrFS (fullscreen)
8. LyrOverlay
9. LyrBlock (input blocking surfaces)

### Wayland Protocols

macwc implements numerous protocols including: xdg-shell, wlr-layer-shell, pointer-constraints, session-lock, idle-inhibit, xdg-activation, and virtual keyboard/pointer.

Custom protocol XMLs are in `protocols/`.

## Build Configuration (meson)

- **Build system**: Meson (`meson.build` at project root, options in `meson_options.txt`)
- **Dependencies**: Requires wlroots-0.19 and scenefx-0.4
- **XWayland**: Enable with `meson setup build -Dxwayland=enabled` (disabled by default)
- Protocol headers are generated automatically by `wayland-scanner` via meson custom targets

## scenefx Integration

This fork integrates scenefx for visual effects:
- **Window shadows**: Configurable shadow color and appearance
- **Blur effects**: Background blur for windows and layer surfaces
- **Corner radius**: Rounded corners for windows
- **Opacity controls**: Per-window active/inactive opacity

## Key Functions

- **movetomon**: Move focused client to another monitor (renamed from tagmon)
- **focusdir**: Directional focus navigation (up/down/left/right)

## Running

```bash
macwc                           # Start with default settings
macwc -s 'status-bar-command'   # Start with a status bar
```

Environment variables for status bar scripts: `WAYLAND_DISPLAY`, `DISPLAY` (if XWayland enabled)

## Patches

Community patches are maintained separately in the upstream dwl-patches repository. Patch files in this repo (*.patch) can be applied with `git apply` or `patch -p1`.
