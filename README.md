# Taskbar Player (Windhawk mod)

A now-playing widget on the left side of the Windows 11 taskbar with a popup
player and Spotify Connect support.

`taskbar-player.wh.cpp` is the file that goes into Windhawk. It is generated
from the parts in `src/` (do not edit it directly).

## Layout

| Path | What it is |
| --- | --- |
| `src/01_core.cpp` | Mod metadata, readme, settings, helpers, JSON, HTTP, worker threads |
| `src/02_media.cpp` | Media sessions (GSMTC), Spotify Web API + sign-in, controller, remote feed |
| `src/03_render.cpp` | Direct2D/DirectWrite helpers, layered surfaces, icons |
| `src/04_widget.cpp` | The taskbar widget |
| `src/05_popup.cpp` | The popup player |
| `src/06_main.cpp` | Taskbar attach/detach and the Windhawk entry points |
| `harness/` | Test harness that runs the UI outside explorer |

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
build/harness.exe render <outDir> <image> [x y size] # mock renders saved as PNG
build/harness.exe live <outDir> <seconds>            # real media sessions, saves PNG
```

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
