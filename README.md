# dynscope

Lightweight nested Wayland compositor for games

## Features

- Runs fullscreen and fixed-resolution games in freely resizable windows
- Dynamic aspect ratio fitting and centering
- Automatic pointer locking, confinement, and relative motion
- Host display mode forwarding so games see the correct resolution and refresh rate
- Game-driven resolution switching without restarts
- Cursor passthrough with HiDPI and fractional scale support
- Window title passthrough from game to host compositor
- Bidirectional clipboard and primary selection synchronization
- Zero-copy DMA-BUF pipeline with explicit sync
- Isolated nested session with rootless XWayland

## Why not Gamescope?

- Minimal C codebase with a smaller dependency footprint than Gamescope
- Dynamic canvas: automatically fits the game resolution instead of requiring `-w` and `-h` flags
- Native window sizing: resize freely through your window manager instead of setting static `-W` and `-H` dimensions

## Requirements

Runtime: a Wayland compositor and XWayland

Build:

- `meson` >= 0.60.0, `ninja`, `git`, `wayland-scanner`
- `glesv2`, `pixman-1`, `wayland-client` >= 1.22.0, `wayland-protocols`,
  `wayland-server` >= 1.22.0, `xcb`, `xcb-xfixes`, `xkbcommon`

## Usage

You need to set your game to **windowed mode** in its video settings for fitting to work properly.

```sh
dynscope -- COMMAND [ARG...]
```

### Steam

Set this in your game launch options in Steam:

```sh
dynscope -- %command%
```

## Building

```sh
meson setup build --prefix ~/.local
ninja -C build
```

Install with:

```sh
meson install -C build --skip-subprojects
```

## License

MIT
