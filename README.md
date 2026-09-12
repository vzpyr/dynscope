# dynscope

Lightweight nested gaming scope compositor. Gamescope's behavior, minimal surface.

`dynscope -- %command%` in Steam launch options (or any launcher) runs the game
on an internal virtual screen and presents it inside a normal, freely resizable
window in your Wayland session:

- internal game resolution switches live, no restart
- uniform-scale letterboxing, never stretched, black bars handled for you
- gamescope-identical cursor behavior: no heuristics, driven purely by
  pointer-constraints protocol state from Xwayland
  (game hides cursor + grabs pointer → camera lock with raw relative motion;
  menus → cursor glides back out of the window)
- clipboard and primary selection bridged both directions
- no flags, no FSR, no winit, no SDL

## Usage

```
dynscope -- COMMAND [ARG...]
```

### Steam Launch Options

Right-click a game in your Steam Library, select Properties, and enter the following into Launch Options:

```
/path/to/dynscope -- %command%
```

Or if dynscope is installed in your system PATH:

```
dynscope -- %command%
```

The window can be resized freely at any time; the game keeps its own
resolution and is letterboxed into the window. Closing the window
terminates the game. dynscope exits with the game's exact exit code
or signal status.

Requirements: a Wayland compositor, Xwayland (bundled logic via wlroots),
wlroots 0.20, meson, ninja.

## Build

```
meson setup build
ninja -C build
```

## Debugging

Set `DYNSCOPE_DEBUG=1` for verbose diagnostic logging.

## Status

Phase-based roadmap: see [ROADMAP.md](ROADMAP.md).

## License

MIT
