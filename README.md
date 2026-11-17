# Taskbar Player (Windhawk mod)

A now-playing widget on the left side of the Windows 11 taskbar with a popup
player and Spotify Connect support. Scrolling over the widget changes the
volume, and an optional audio visualizer sits next to the playback buttons.

`taskbar-player.wh.cpp` is the file that goes into Windhawk. It is generated
from the parts in `src/` (do not edit it directly).

## Layout

| Path | What it is |
| --- | --- |
| `src/01_core.cpp` | Mod metadata, readme, settings, helpers, JSON, HTTP, worker threads |
| `src/02_media.cpp` | Media sessions (GSMTC), Spotify Web API + sign-in, controller, remote feed |
| `src/03_render.cpp` | Direct2D/DirectWrite helpers, layered surfaces, icons |
| `src/04_audio.cpp` | Spectrum analysis, app/system volume, loopback capture thread, the visualizer window |
| `src/05_widget.cpp` | The taskbar widget (incl. scroll volume and the visualizer's slot) |
| `src/06_popup.cpp` | The popup player |
| `src/07_main.cpp` | Taskbar attach/detach and the Windhawk entry points |
| `harness/` | Test harness that runs the UI outside explorer |

The parts are joined in file name order, so a new part needs a number that
sorts where it belongs.

## Build

Requires Windhawk installed in `C:\Program Files\Windhawk` (its bundled clang
is used). From Git Bash:

```bash
./build.sh            # regenerate taskbar-player.wh.cpp and compile the mod DLL
./build.sh harness    # build build/harness.exe
```

## Test harness

```bash
build/harness.exe selftest <outDir>                 # unit tests + Spotify endpoint checks
build/harness.exe render <outDir> [image x y size]  # mock renders saved as PNG (+ visualizer_sheet.png, transport_*.png icon sheets)
build/harness.exe live <outDir> <seconds>            # real media sessions, saves PNG
build/harness.exe audio <outDir> <seconds> [style] [all|app]  # real visualizer: levels + CPU use
build/harness.exe tonetest <outDir>                  # capture check with a test tone (mutes the PC briefly)
build/harness.exe volume <outDir> [aumid...]         # reads app/system volumes (changes nothing)
```

`tonetest` only runs when nothing is playing: it mutes the master output for
a few seconds (Windows captures before the master volume, so nothing is
heard) and restores it afterwards.

## Audio notes

* "Only the app shown" uses process loopback capture, which ignores the
  requested buffer size (10 ms): the tap waits on the stream's event and
  drains it every time, and analyzes at the frame rate.
* Process loopback is captured after the app's own volume and mute, but before
  the master volume.
* The visualizer is its own small layered window (software Direct2D) above the
  widget, so the animation doesn't redraw the widget. Measured: about 2% of one
  core at 60 fps while animating, ~0.3% while waiting for sound.

`build/harness.exe.manifest` must sit next to the exe (layered child windows
need a Windows 8+ compatibility manifest).

## Remote feed

The mod shares what it shows with other mods (the Glyph Lock Screen mod in
`..\nothing-lockscreen` uses it for Spotify Connect on the lock screen): a
`FeedRecord` in the file mapping `Local\TaskbarPlayer.Feed.1`, rewritten on
every snapshot, and transport commands through the event
`Local\TaskbarPlayer.Command.1`. The struct is duplicated in the reader, so a
layout change needs new names on both sides. See "Remote feed" in
`src/02_media.cpp`.

## Install in Windhawk

1. Windhawk -> **Create a New Mod**.
2. Replace the editor contents with `taskbar-player.wh.cpp`.
3. **Compile Mod**, then **Exit Editing Mode**.
4. Optional: put a Spotify Client ID in the mod settings (see the mod's Details tab).
