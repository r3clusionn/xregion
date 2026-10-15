# xregion

A screen region selector for X11 in one C file, for screenshot key bindings: drag a rectangle or click a window, and it prints the geometry (`1280x720+400+300`) for a screenshot tool such as `grab`, `maim` or `import`. It replaces hacksaw, which is written in Rust, with the same output format and options. It is a 21 KB binary that needs only libX11, draws its lines as plain X windows (no compositor, any colour depth, 10-bit X included), and keeps the rectangle outside the selected area, so the lines never end up in the screenshot.

**Status:** in daily use on an HP OmniBook 7 with Gentoo, Xorg at depth 30 and dwm, bound to the screenshot key in place of hacksaw.

![Terminal output on a black background. "xregion -h" prints the usage: xregion [-f format] [-c colour] [-t thickness] [-g guide-thickness] [-n], then one line per option: -f output format with %x %y %w %h, %g = %wx%h+%x+%y (the default), %i = window id, %% = %; -c line colour, #rrggbb (default #7f7f7f); -t rectangle line thickness in pixels (default 2); -g guide line thickness in pixels (default 1); -n no guide lines.](docs/images/terminal.png)

## Features

- **Drag** to select a rectangle, from any corner.
- **Click** without dragging to select the window under the pointer (its inside, without the window manager's border); a click on the desktop selects the whole screen.
- **Esc or the right button** cancels: exit status 1 and no output, so `g=$(xregion) && ...` stops there.
- **Guide lines** across the whole screen follow the pointer until the drag starts (`-n` turns them off), as in hacksaw.
- **hacksaw's output codes:** `%x %y %w %h %g %i %%`; `%g` (`WxH+X+Y`) is the default.
- **Any X depth.** The lines are filled windows in the root visual, so they work at depth 30, where tools that draw with 8-bit assumptions fail.
- **The themed cursor:** a crosshair from your Xcursor theme at `XCURSOR_SIZE` (libX11 loads libXcursor for it when it is installed).

## How to install

Requires a C compiler, make and libX11.

```sh
git clone https://github.com/r3clusionn/xregion
cd xregion
make install            # to ~/.local/bin; PREFIX=/usr/local for system-wide
```

## How to use

A screenshot of a region to the clipboard, as a dwm binding (with `grab`, which writes a PNG to standard output):

```c
{ MODKEY|ShiftMask, XK_s, spawn, SHCMD("g=$(xregion) && grab -g \"$g\" - | xclip -selection clipboard -t image/png") },
```

Other uses:

```sh
xregion                          # 640x480+100+200
xregion -f '%x %y %w %h'         # 100 200 640 480
xregion -c '#ff5f00' -t 3 -n     # orange lines, 3 px, no guides
```

## How it works

- Opens the display, allocates the line colour in the default colormap (so the pixel is right for the root visual, 24- or 30-bit) and creates six 1x1 override-redirect windows: four edges and two guides. The window manager doesn't manage them.
- Grabs the pointer (with the crosshair cursor) and the keyboard. Started from a key binding, the window manager may still hold the keyboard until the key is released, so both grabs are retried every 10 ms for up to a second.
- Moves the windows on pointer motion, using only the latest position when several motions are queued.
- The rectangle is drawn just outside the selection; the guides are hidden as soon as the button goes down. By the time the button is released, the windows under them have had the whole drag to repaint, so the area being captured is clean.
- On release, a move of 3 pixels or less counts as a click: the selection becomes the topmost window at the press point (from the root window's stacking order, leaving out xregion's own lines), inside its border.
- The windows are destroyed and the server synced before the geometry is printed, so the screenshot that follows doesn't see them.

## Measurements

| | xregion | hacksaw 1.0.4 |
|---|---|---|
| Binary | 21 KB | 821 KB |
| Libraries | libX11 (libXcursor if present) | libxcb, libxcb-shape, libxcb-xkb, libgcc_s |
| Builds with | a C compiler | a Rust toolchain |

## Verification

- On the laptop (Xorg at depth 30, dwm), through the screenshot binding above, which pipes into `grab` and `xclip`:
  - a dragged region pasted into another program was exactly that region, with no selection lines in it;
  - a click on a window gave exactly that window, without dwm's border;
  - Esc and the right button cancelled, and nothing was copied.
- That test found one bug, now fixed: the guide lines cross right under the pointer, so a click picked xregion's own 1-pixel guide window. The window under the click is now found from the root's stacking order with xregion's windows left out.
- Bad options and unknown format codes (`%q`, a trailing `%`), a zero thickness and an unknown colour are rejected before anything is grabbed (usage, exit 2); without a display it exits 1.

## Limits

- X11 only.
- The selection is clipped to the screen; there is no keyboard adjustment or resize after the drag.
- `-r` (hacksaw's "remove window manager decorations") isn't there: for a non-reparenting window manager such as dwm, the clicked window is already the client, and its border is left out.

## License

MIT (see `LICENSE`).
