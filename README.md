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

- Minimal C codebase: smaller dependency footprint than Gamescope
- Dynamic canvas: automatically fits the game resolution instead of requiring `-w` and `-h` flags
- Native window sizing: resize freely through your window manager instead of setting static `-W` and `-H` dimensions

## Installation

You can install the latest release using this command:

```bash
curl -sL https://github.com/vzpyr/dynscope/releases/latest/download/dynscope-linux-x86_64.tar.gz | tar -xz -C ~/.local/bin dynscope
```

## Usage

You need to set your game to windowed mode in its video settings for fitting to work reliably.

```bash
dynscope -- COMMAND [ARG...]
```

### Steam

Set this in your game launch options in Steam:

```bash
dynscope -- %command%
```

## Build

You need these packages: `meson ninja git wayland-scanner glesv2 pixman-1 wayland-client wayland-protocols wayland-server xcb xcb-xfixe xkbcommon`

```bash
meson setup build --prefix ~/.local
ninja -C build
```

Install with:

```bash
meson install -C build --skip-subprojects
```

## License

[MIT](LICENSE)
