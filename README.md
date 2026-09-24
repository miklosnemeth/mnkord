# mnKord

**mnKord** is a MIDI chord-mapping effect: one input key triggers a chord of output notes.  
Built with [JUCE](https://juce.com) 9 as **VST3** and **Standalone**.

## Features

- Map any MIDI note to an arbitrary chord (click INPUT key, then CHORD keys)
- Chord templates: Maj, Min, Sus2, Sus4, 7, Maj7, Min7, Dim, Add9
- Invert a chord (right-click a CHORD key)
- Factory preset bank (diatonic, pop progression, power chords, jazz 7ths)
- Preset metadata: name, group, author
- XML preset / bank **Import** and **Export**
- Host program list mirrors user presets
- Sustain pedal handling with note ref-counting (no stuck notes)
- Non-note MIDI (CC, pitch bend, aftertouch, …) passes through
- Visual themes: Graphite, Frost, Neon

## Requirements

- CMake ≥ 3.22  
- C++17 compiler (MSVC / Visual Studio recommended on Windows)  
- Git (JUCE is fetched via `FetchContent`)  
- Network on first configure (downloads JUCE 9.0.1)

## Build (Windows)

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Artifacts (single `build/` tree, JUCE 9 via FetchContent):

| Target | Path |
|--------|------|
| Standalone | `build/mnKord_artefacts/Release/Standalone/mnKord.exe` |
| VST3 | `build/mnKord_artefacts/Release/VST3/mnKord.vst3` |

`COPY_PLUGIN_AFTER_BUILD` is off. To use the VST3 in a DAW, copy the `.vst3` bundle to e.g.:

```text
%CommonProgramFiles%\VST3\
```

or your DAW’s custom VST3 folder, then rescan plugins.

## Usage

1. Load **mnKord** as a MIDI effect (or run the Standalone and route MIDI in/out).
2. Choose a factory preset, or **New** / **Save as**.
3. Click a key on the **INPUT** keyboard to select or create a mapping.
4. Click keys on the **CHORD** keyboard to add/remove chord tones, or use the template buttons.
5. **Import** / **Export** share presets as `.xml` (single preset or full bank).

Unmapped input notes are dropped. Mapped notes expand to their chord; overlapping chords share output notes via reference counting so notes do not cut each other off incorrectly.

## Preset file format

Single preset:

```xml
<mnKordPreset version="1">
  <Preset name="My Patch" group="User" author="you">
    <Mapping input="60" output="60 64 67"/>
  </Preset>
</mnKordPreset>
```

Bank (also accepted: root with a `Presets` child, as used in DAW state):

```xml
<mnKordBank version="1">
  <Preset name="A" group="User" author="you">
    <Mapping input="60" output="60 64 67"/>
  </Preset>
</mnKordBank>
```

`input` / `output` values are MIDI note numbers `0–127`.

## Project layout

```text
CMakeLists.txt
Source/
  PluginProcessor.h/.cpp   # MIDI engine, presets, state, file I/O
  PluginEditor.h/.cpp      # UI, keyboards, themes, import/export
  NoteName.h/.cpp          # MIDI note ↔ name helpers
```

## License

Plugin code: your project license (add one if distributing).  
JUCE is subject to the [JUCE license](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md).
