# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Enter a Nix dev shell before building: `nix develop`

```bash
meson setup build                              # Configure (first time)
meson setup build --reconfigure                # Reconfigure after changing options
meson setup build -Dxwayland=true -Dtests=true # Enable XWayland and tests
ninja -C build                                 # Build
ninja -C build test                            # Run all tests
build/test_events                              # Run a single test binary
```

The `dwlctl` CLI tool is also built alongside dwl.

## Architecture

This is a modular rewrite of dwl (originally a single-file compositor). The code is split into subsystems that communicate through an event bus and are coordinated by a central `DwlCompositor` struct.

### Core Pattern

`DwlCompositor` (src/core/compositor.c) owns all subsystem managers and wlroots objects. Each subsystem gets a pointer back to the compositor to access other subsystems via `dwl_compositor_get_*()` accessors. This avoids global state.

All subsystem functions return `DwlError` (include/error.h) for error handling — check against `DWL_OK`.

### Subsystems

- **Event Bus** (src/core/events.c) — Pub/sub system decoupling subsystems. Components subscribe to `DwlEventType` events (client create/destroy, monitor add/remove, tag changes, etc.) and emit events that others can react to.
- **Config** (src/config/config.c) — TOML-based runtime configuration with key-value store, typed getters/setters, file watch support, and change notification callbacks. Config path: `$XDG_CONFIG_HOME/dwl/config.toml`. See `config.toml.example` for all options.
- **Client** (src/client/) — Window management. `client.c` handles XDG clients; `client_x11.c` handles XWayland clients. `rules.c` applies window rules from config.
- **Input** (src/input/) — Split into keyboard.c, pointer.c, and keybindings.c. Keybindings are parsed from TOML config at runtime (not compile-time).
- **Output/Monitor** (src/output/monitor.c) — Monitor management, layout arrangement triggering, usable area tracking (accounting for layer surfaces).
- **Layout** (src/layout/) — Registry pattern with pluggable layouts. Each layout (tile, monocle, scroller, floating) implements an `arrange()` function and optional `focus_next()` for directional focus. Register new layouts via `DwlLayoutRegistry`.
- **Workspace** (src/workspace/) — Tag-based workspace management per monitor (dwm-style bitmask tags, not numbered workspaces).
- **IPC** (src/ipc/) — Unix socket IPC with JSON responses. `socket.c` handles connections, `commands.c` registers built-in commands. `dwlctl` (tools/dwlctl.c) is the client-side CLI.
- **Render** (src/render/) — SceneFX-based rendering with blur, shadows, rounded corners. Uses wlroots scene graph API extended by scenefx.
- **Protocols** (src/protocols/) — Wayland protocol implementations (XDG shell, server decoration).
- **Layer** (src/layer/) — wlr-layer-shell implementation for panels, bars, overlays.

### Headers

All public headers are in `include/`. Each subsystem has a corresponding header that defines its opaque types and API. The `DwlCompositor` struct itself is only defined in compositor.c — other code interacts through the accessor functions declared in `include/compositor.h`.

### Testing

Tests use cmocka and link against `dwl_testable` — a static library of subsystems that don't depend on wlroots (events, config, layout, rules). Mock stubs for client functions are in `tests/mocks/`. Tests must be enabled with `-Dtests=true` at configure time.

### Configuration vs. Old dwl

Unlike upstream dwl which uses compile-time `config.h`, this fork uses runtime TOML configuration. The `config.toml.example` file documents all available settings. Keybindings, window rules, monitor rules, appearance, and input settings are all runtime-configurable.

## Dependencies

- wlroots-0.19, scenefx-0.4, wayland-server/client, wayland-protocols, xkbcommon, libinput, pixman
- Optional: libxcb, xcb-icccm (XWayland)
- Test: cmocka
