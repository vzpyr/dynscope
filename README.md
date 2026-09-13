# dynscope

Lightweight nested Wayland compositor for games

## Features

- Runs fullscreen and fixed-resolution games in freely resizable windows
- Dynamic aspect ratio fitting and centering
- Zero-copy DMA-BUF pipeline with explicit sync and frame pacing
- Automatic pointer locking, confinement, and relative motion
- Host display mode forwarding to Wine, Proton, and X11 games
- Live resolution switching without game restarts
- Cursor passthrough with HiDPI and fractional scale support
- Bidirectional clipboard and primary selection synchronization
- Isolated nested session with rootless XWayland

## Why not Gamescope?

- Minimal C codebase with a small dependency footprint
- Dynamic canvas: automatically fits the game resolution instead of requiring `-w` and `-h` flags
- Native window sizing: resize freely through your window manager instead of setting static `-W` and `-H` dimensions
- Zero configuration: no flags, config files, or launch wrappers

## Requirements

- A Wayland compositor
- XWayland

## Usage

> Set the game to **windowed mode** in its in-game video settings. dynscope scales and centers the canvas inside a freely resizable Wayland window.

```sh
dynscope -- <command> [args...]
```

### Steam

Set the launch options in Steam:

```sh
dynscope -- %command%
```

## Building

Dependencies: `meson`, `ninja`, `egl`, `gbm`, `glesv2`, `libdrm`, `pixman-1`, `wayland-client`, `wayland-protocols`, `wayland-server`, `xcb`, `xcb-xfixes`, `xkbcommon`

```sh
meson setup build
ninja -C build
```

## License

MIT
