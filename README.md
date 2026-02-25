# sfxDesigner

Standalone sound effect designer for your game pipeline.

## Features (prototype milestone)

- GLUT + ImGui editor UI
- All 17 SfxDef parameters exposed in sliders/controls
- Hotkeys: `SPACE` play, `S` save JSON + WAV, `R` randomize, `Z` undo, `Y` redo
- Preset categories: Weapons, UI, Combat, Movement, Pickups
- Live plots: envelope, frequency trajectory, output waveform
- JSON export compatible with `SfxDef`
- WAV export with configurable duration
- WAV/MP3 import with best-effort fitting into `SfxDef` constraints

## Build Dependencies (Linux)

- `g++` (C++17)
- `freeglut` development package
- OpenGL development package
- ImGui sources vendored at `third_party/imgui`

Ubuntu/Debian example:

```bash
sudo apt-get update
sudo apt-get install -y build-essential freeglut3-dev libgl1-mesa-dev
```

Vendor ImGui sources:

```bash
make setup-imgui
```

Expected ImGui layout:

- `third_party/imgui/imgui.h`
- `third_party/imgui/imgui.cpp`
- `third_party/imgui/imgui_draw.cpp`
- `third_party/imgui/imgui_tables.cpp`
- `third_party/imgui/imgui_widgets.cpp`
- `third_party/imgui/backends/imgui_impl_glut.cpp`
- `third_party/imgui/backends/imgui_impl_opengl2.cpp`

## Build

```bash
make
```

## Run

```bash
./sfxDesigner
```

## Notes

- Playback uses `aplay` when available.
- Exported files are written under `exports/`.
- Use `Import WAV Path` + `Browse...` + `Import WAV/MP3 -> Fit SfxDef` to convert external audio into editable `SfxDef` values.
- `zenity` enables GUI file browse on Linux, and `ffmpeg` enables MP3 conversion.
- This is the first incremental implementation and is structured for fast iteration.
