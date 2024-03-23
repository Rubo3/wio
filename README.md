_Note_: this repository contains the original wio [source code](https://git.sr.ht/~sircmpwn/wio) by Drew DeVault, with some of the [patches](https://lists.sr.ht/~sircmpwn/wio/patches) which were not mainlined, and some fixes to build it against the latest version of [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) (currently: 17.2).

It is available on the Arch User Repository as [`wio-wl`](https://aur.archlinux.org/packages/wio-wl).

# wio

Wio is a Wayland compositor for Linux & FreeBSD which has a similar look & feel
to plan9's rio.

This software is incomplete. Notably missing is a Rio-esque FUSE filesystem and
Rio's built-in command line (we depend on an external, tty-style terminal
emulator).

## Installation

To build and install wio:

```sh
meson setup build
ninja -C build install
```

## Basic principles

Wio uses [wlroots](https://github.com/swaywm/wlroots) to run a simple Wayland
compositor. Right clicking (and holding) outside of a window will bring up the
menu. Drag the mouse to the command you'd like to execute and release the mouse.

- **New**: Opens a new window. Click and hold the mouse, then drag, to define
    the placement for the new window.
- **Resize**: Resizes a window. Click the window to resize, then click and drag
    somewhere else to define the new placement.
- **Move**: Moves a window. Click and drag a window to move it.
- **Delete**: Deletes a window. Click the window you want to close.
- **Hide**: Hides a window. Open the menu again to show the window.

Each window runs [cage](https://github.com/Hjdskes/cage) by default, and
instructs cage to run a terminal emulator
([alacritty](https://github.com/jwilm/alacritty) by default). This effectively
gives each window its own private Wayland compositor. If you want to run a
graphical application, simply run the command from the terminal and cage will
display it full-screen within its own window.

To access Wio's own Wayland compositor directly (for example, to take
screenshots with [grim](https://wayland.emersion.fr/grim)), set
`WAYLAND_DISPLAY=wayland-0` or similar. Note, however, that the only shell for
application windows which is supported directly by Wio is xdg-shell.

## Usage

Some minor customization options are available by passing command line arguments
to Wio:

```sh
wio [-c <cage>] [-t <terminal>] [-o <output config>...]
```

- **-c &lt;cage&gt;**: specifies the `cage` command to run new windows in
- **-t &lt;term&gt;**: specifies the terminal command to run new windows in

For the authentic rio experience, try the alacritty config in `contrib/`.

### Output configuration

Wio supports multihead configurations and HiDPI displays. In order to take
advantage of this, you must pass some output configurations on the command line.
You can specify the following options, in this order:

- Output name (`weston-info` can list output names & modes)
- X & Y coordinates in the multihead layout
- Width & height of the desired mode
- Scale factor
- Transform

Each property should be separated by a colon. Use -1 for X & Y to have Wio
automatically place the display, and 0 to have Wio select a mode or scale factor
automatically. You may omit any of these properties, but must include any
property which comes before the ones you're trying to set.

For example, to specify the X & Y coordinates of output `HDMI-A-1`, try the
following: `-o HDMI-A-1:1920:0`. If you also want to set its mode to 720p, use
`-o HDMI-A-1:1920:0:1280:720`. If you don't care about setting the mode but want
to flip it, use `-o HDMI-A-1:-1:-1:0:0:0:flipped`.

Available transforms are:

- `normal`
- `90`
- `180`
- `270`
- `flipped`
- `flipped-90`
- `flipped-180`
- `flipped-270`

### Environment

Wio recognizes the following environment variables for basic keyboard
configuration:

- `XKB_DEFAULT_RULES`
- `XKB_DEFAULT_MODEL`
- `XKB_DEFAULT_LAYOUT`
- `XKB_DEFAULT_VARIANT`
- `XKB_DEFAULT_OPTIONS`

Read `xkeyboard-config(7)` for details.
