# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

dwl (dwm for Wayland) is a compact, hackable Wayland compositor based on wlroots and scenefx. It follows the suckless philosophy - configuration is done by editing `config.h` and recompiling.

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

Configuration follows the suckless model:
1. Copy `src/config.def.h` to `src/config.h` (done automatically on first build)
2. Edit `src/config.h` to customize
3. Rebuild with `meson compile -C build`

Key configuration sections in `src/config.def.h`:
- **Appearance**: Border pixels, colors (border, focus, urgency)
- **Gaps**: smartgaps, monoclegaps, gappih/gappiv/gappoh/gappov for inner/outer horizontal/vertical gaps
- **Opacity**: Active/inactive window opacity settings
- **Shadows**: Shadow color and configuration
- **Corner radius**: Rounded corner settings for windows
- **Blur**: Blur effect settings for windows and backgrounds
- **Numlock**: Enable numlock on start
- **Keyboard**: XKB rules, repeat rate/delay
- **Trackpad**: libinput settings (natural scroll, tap-to-click, acceleration)
- **Keybindings**: All shortcuts (default modifier is Alt)
- **Rules**: Per-application window management rules
- **Layouts**: tile, floating, monocle, scroller
- **Scroller**: scroller_center_mode, scroller_proportions for scroller layout behavior
- **MonitorRules**: Per-monitor settings including scale

## Architecture

### Source Layout

All source files live in `src/`.

- **src/dwl.c** (~3800 lines): Main compositor implementation
- **src/client.h**: Inline helper functions for client/window management
- **src/util.c/h**: Small utilities (die, ecalloc, fd_set_nonblock)
- **src/config.def.h**: Default configuration template
- **src/visual.c/h**: Visual effects (opacity, shadows, corners, blur)
- **src/session.c/h**: Session lock handling
- **src/client_funcs.c/h**: Client management functions
- **src/pointer.c/h**: Pointer/cursor handling
- **src/input.c/h**: Keyboard input handling
- **src/layout.c/h**: Layout algorithms (tile, monocle, scroller)
- **src/monitor.c/h**: Monitor management

### Key Data Structures (src/dwl.c)

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

dwl implements numerous protocols including: xdg-shell, wlr-layer-shell, pointer-constraints, session-lock, idle-inhibit, xdg-activation, and virtual keyboard/pointer.

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
dwl                           # Start with default settings
dwl -s 'status-bar-command'   # Start with a status bar
```

Environment variables for status bar scripts: `WAYLAND_DISPLAY`, `DISPLAY` (if XWayland enabled)

## Patches

Community patches are maintained separately in the dwl-patches repository. Patch files in this repo (*.patch) can be applied with `git apply` or `patch -p1`.
