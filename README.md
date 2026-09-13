# dynscope

Lightweight nested Wayland and XWayland gaming scope compositor

## Features

- Isolated nested session with rootless XWayland integration
- Dynamic aspect ratio fitting and centering without distorted stretching
- Zero-copy DMA-BUF pipeline with frame pacing and explicit sync for near-zero compositor overhead
- Automatic pointer lock, confinement, and raw relative motion for FPS and camera controls
- Dynamic host display mode forwarding to Wine/Proton and X11 games
- Seamless live resolution switching without restarting the game
- Transparent cursor passthrough with hardware-scaled cursor images
- Bidirectional clipboard and primary selection bridging
- Tames stubborn fullscreen or fixed-size games into freely resizable windows

## Why not Gamescope?

- Minimal dependency footprint and a tiny, clean C codebase
- No fixed internal resolution: Gamescope's `-w`/`-h` flags are replaced by dynamic fitting to the game's actual resolution
- No fixed window size: Gamescope's `-W`/`-H` flags are replaced by allowing free window resizing through your desktop environment
- Zero configuration: no flags, no wrappers, no SDL, and no winit

## Requirements

- A Wayland compositor with `linux-dmabuf-v1` support
- XWayland
- Hardware-accelerated GPU driver with GLES2 and GBM support

## Usage

```sh
dynscope -- COMMAND [ARG...]
```

### Steam Launch Options

Right-click a game in your Steam Library, select Properties, and set Launch Options:

```sh
dynscope -- %command%
```

## Building from Source

Build dependencies:

- A C11 compiler (GCC or Clang)
- Meson (>= 0.60.0) and Ninja
- `pkg-config`
- `wayland-client` and `wayland-server` (>= 1.22.0)
- `wayland-protocols`
- `xkbcommon`
- `xcb`, `xcb-xfixes`, and `pixman-1`
- `glesv2`
- `wlroots` (>= 0.20, bundled via Meson wrap if not installed on system)

```sh
meson setup build
ninja -C build
```

## License

MIT
