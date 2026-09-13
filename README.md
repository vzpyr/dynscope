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

Dependencies:

- C11 compiler (GCC or Clang)
- Meson (>= 0.60.0), Ninja, and `pkg-config`
- `wayland-client`, `wayland-server` (>= 1.22.0), and `wayland-protocols`
- `xkbcommon`, `pixman-1`, `xcb`, and `xcb-xfixes`
- `glesv2`, `egl`, `gbm`, and `libdrm`

```sh
meson setup build
ninja -C build
```

## License

MIT
