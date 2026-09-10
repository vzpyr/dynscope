# dynscope — ROADMAP

Nested gaming scope compositor. Gamescope's behavior, minimal surface.
`dynscope -- %command%` in Steam launch options runs the game on an internal
virtual screen, presented letterboxed inside a normal, freely resizable window
in your session.

## Design contract (frozen, no heuristics)

| Concern | Rule |
| --- | --- |
| Internal resolution | The game's current X screen resolution, live-switchable without restart |
| Window size | Owned by the host DE / user, freely resizable at all times |
| Presentation | Uniform scale `min(winW/gameW, winH/gameH)`, centered, black bars, never stretched |
| Pointer, no grab | Host pointer maps into game coordinates while inside the window; cursor glides out of the window like any normal cursor |
| Pointer, game grab | X11 `XGrabPointer` → XWayland → pointer constraint → host pointer locked to our window |
| Pointer, grab with confine_to | → host pointer confined to our window |
| Cursor image | Game's X cursor rendered as the host cursor (scaled, hotspot scaled); game hides cursor → host cursor hidden; while locked, cursor is drawn in-frame at the virtual position |
| Keyboard | Host keymap/state forwarded to the game; game has keyboard focus iff our window is focused |
| Clipboard | Bridged both directions (clipboard + primary selection) |
| Launch | `dynscope -- CMD...`; child env wired (DISPLAY); exit code passthrough |

Non-goals: no CLI flags at all, no FSR/NIS/shaders, no HDR/CTM, no
fullscreen/exclusive, no tearing controls, no multi-monitor inside, no
screenshots/replay, no Steam/overlay integration.

## Verified findings (research)

References: gamescope @ 50c8d74 (`~/Code/refs/gamescope`), wlroots 0.21-dev
(`~/Code/refs/wlroots`), Xwayland 24.1.13 (system), Arch wlroots0.20 0.20.2.

1. Gamescope's nested backend is a hand-rolled Wayland client
   (`src/Backends/WaylandBackend.cpp`: xdg_toplevel, wl_seat listeners,
   zwp_pointer_constraints, zwp_relative_pointer, wl_data_device,
   zwp_primary_selection, zwp_linux_dmabuf). It does NOT use wlroots' nested
   wayland backend. We do the same in C for exact control over cursor semantics.
2. X11 grab detection is not needed on our side: Xwayland 24.x (confirmed
   present: strings show zwp_pointer_constraints_v1 + zwp_relative_pointer_v1)
   translates X11 pointer grabs into `wp_pointer_constraints_v1` requests
   toward the compositor, and `XWarpPointer` into cursor position hints.
3. Host-pointer lock rule, steamcompmgr.cpp:10402:
   `relativeMouseMode = cursorImageEmpty && hasPointerConstraint` (game hid its
   cursor AND holds an active constraint from XWayland). Protocol state only.
   Confinement maps through wlserver's constraint handling
   (`wlserver_constrain_cursor`, `wlserver_update_cursor_constraint`).
4. Warp: game `XWarpPointer` → XWayland → `wp_locked_pointer_v1
   .set_cursor_position_hint` → `wlserver_warp_to_constraint_hint` moves the
   virtual cursor (wlserver.cpp:2847).
5. Locked motion: host `zwp_relative_pointer_v1` unaccelerated deltas →
   `wlserver_mousemotion` → constraint clamp → virtual cursor accumulation →
   `wlr_seat_pointer_notify_motion` → XWayland → game (sensitivity 1:1 in game
   pixels, wlserver.cpp:wlserver_mousemotion).
6. Unlocked motion: absolute mapping, host surface coords → game coords via
   the fit transform → `wlserver_touchmotion` → `wlserver_mousewarp`
   (WaylandBackend.cpp:3100).
7. Cursor image: XWayland/xwm forwards X cursors via the seat cursor path
   (wlroots xwm.c `xwm_set_cursor` → wlr_seat). Gamescope scales the game
   cursor by the present scale and pushes it as the host cursor
   (`wl_pointer_set_cursor`), hides the host cursor while locked and draws the
   cursor in-frame at the virtual position instead.
8. Resolution: the game's X screen is a headless wlroots output
   (`wlr_headless_add_output` + `wlr_output_state_set_custom_mode`,
   `wlserver_set_xwayland_server_mode`); a mode change is a live
   output-commit operation, X screen and clients adapt immediately.
9. Gamescope vendors STOCK wlroots (submodule → upstream wlroots.git). No
   patched wlroots needed.
10. wlroots ships server-side implementations of pointer-constraints-v1 and
    relative-pointer-v1 (`include/wlr/types/wlr_pointer_constraints_v1.h`,
    `wlr_relative_pointer_v1.h`), plus headless backend and xwayland glue.

## Architecture

- C99, meson/ninja, system wlroots 0.20 + wayland-client + xkbcommon + pixman;
  client protocol headers generated with wayland-scanner.
- One process, single event loop: server wl_display is primary; the parent
  client wl_display fd is multiplexed into it via wl_event_loop fd sources.
- Server side: wlr_headless backend (one virtual output at game res),
  wlr_xwayland (game + XWM), wlr_seat, wlr_compositor/allocator/renderer
  (GLES2), wlr_data_device, wlr_pointer_constraints_v1,
  wlr_relative_pointer_v1.
- Parent side: hand-written Wayland client: xdg_toplevel + xdg-decoration
  (SSD), wl_seat (pointer/keyboard), zwp_relative_pointer_manager_v1,
  zwp_pointer_constraints_v1, wp_fractional_scale_manager_v1 + wp_viewport,
  wl_data_device + zwp_primary_selection_device_manager.
- Render: manual render pass, no scene graph: per-surface texture from X
  client buffers, dst box = fitted rect on the window output, cursor drawn
  last (in-frame while locked, host-cursor otherwise).
- No winit, no SDL, no client toolkits anywhere.

## Phases

### Phase 0 — Skeleton and child lifecycle
- [ ] meson/ninja project `dynscope`, C99, deps via pkg-config, zero warnings
- [ ] argv: only `dynscope -- CMD...` accepted; fork/exec CMD with DISPLAY
      pointing at our Xwayland; waitpid; exit with child's exit code
- [ ] parent window: connect host compositor, xdg_toplevel + xdg_surface,
      black frame, frame-callback driven, clean close handling
- [ ] shutdown both ways: window close → SIGTERM child (2 s grace → SIGKILL);
      child exit → compositor exits
- [ ] README with usage
Exit criteria: `dynscope -- glxgears` opens the window and the child, and
closing either side tears everything down cleanly.

### Phase 1 — XWayland and the game on the virtual screen
- [ ] wlr stack: headless backend + renderer/allocator; server globals
      (compositor, shm, seat, data-device-manager)
- [ ] virtual output: headless output, default mode 1280x720, name "dynscope"
- [ ] wlr_xwayland: start, wire DISPLAY env for the child
- [ ] XWM surface tracking: focused top-level rendered fullscreen on the
      virtual output; override-redirect windows at their X positions
- [ ] render pass: surface textures → window (1:1 for now), frame callbacks
- [ ] keyboard focus: host kb enter/leave → seat focus → XWM focused window
Exit criteria: glxgears renders inside our window at 1:1, focused, no input
behavior yet beyond not crashing.

### Phase 2 — Fit scaling, pointer, keyboard, cursor basics
- [ ] host configure/resize → recompute fit (scale + offsets), re-render;
      window title from game
- [ ] pointer enter/motion/leave: host coords → virtual coords →
      wlr_seat absolute motion, clamped to bounds, no edge artifacts
- [ ] pointer buttons + axis forwarding
- [ ] keyboard: host keymap and state → wlr_seat keyboard
- [ ] cursor: X cursor surface (seat cursor event) → host cursor with scaled
      image + scaled hotspot; hidden cursor → empty host cursor; default
      cursor restored on leave
Exit criteria: window freely resizable with correct letterboxing; pointer
sits exactly under the scaled cursor; keyboard works; cursor glides in and
out of the window.

### Phase 3 — Live resolution switching
- [ ] detect game resolution change (focused game window resize; spike: the
      xrandr path through Xwayland RR emulation)
- [ ] live virtual output mode switch (custom mode + commit); X screen and
      clients adapt; letterbox and pointer mapping follow
- [ ] verify a mid-run resolution change (game menu / xrandr from a shell
      inside the session)
Exit criteria: game resolution switch without restart; window just
re-letterboxes.

### Phase 4 — Cursor lock semantics (gamescope parity)
- [ ] server: wlr_pointer_constraints_v1 + wlr_relative_pointer_v1 wired to
      the seat; activate/deactivate on the focused surface
- [ ] confine region + cursor hint handling (warp-to-hint equivalent)
- [ ] host lock rule: game constraint active && game cursor hidden →
      zwp_locked_pointer (persistent) + zwp_relative_pointer; unlock +
      warp-to-hint on release (steamcompmgr.cpp:10402 rule)
- [ ] relative motion: unaccelerated host deltas → virtual cursor, 1:1 game
      pixel sensitivity, constraint clamping
- [ ] visibility sync: locked → host cursor hidden + in-frame cursor at
      virtual position; unlocked → host cursor = game cursor
- [ ] verify: SDL2/SDL3 camera-look games (raw XI2 and warp-loop styles),
      menus release the pointer, glide out/in, no snap-back, no drift
Exit criteria: camera games behave exactly like nested gamescope.

### Phase 5 — Clipboard and primary selection
- [ ] game → host: X selection change → data source / primary source to host
- [ ] host → game: wl_data_device offer → seat selection → xwm bridges to X
- [ ] verify both directions against the host DE clipboard
Exit criteria: copy/paste works both ways, clipboard + primary.

### Phase 6 — Polish and release
- [ ] fractional-scale + wp_viewport on the parent, HiDPI-correct cursors
- [ ] xdg-decoration SSD; initial window size = game resolution
- [ ] frame pacing: frame callbacks both sides, no busy loops
- [ ] signal-correct exit codes; logging via DYNSCOPE_DEBUG=1; Steam launch
      options docs in README
- [ ] test matrix run on Hyprland: glxgears, SDL2 game, resolution switch,
      camera lock, clipboard
- [ ] tag v0.1.0

## Risks / open items
- xrandr-driven resolution changes (vs window resize): Xwayland RR emulation
  limits; spike in phase 3. Fallback: window-resize trigger only, which is
  the dominant path for real games.
- Hyprland pointer-constraint support for our client window: verify early in
  phase 4 with a minimal lock probe before building on it.
- wlroots 0.20 vs 0.21-dev API drift: code against system wlroots-0.20.

## Testing protocol (every phase)
1. Clean build, zero warnings
2. Run the phase's verification items in the live Hyprland session
3. Tick boxes, commit, report, ask permission before the next phase
