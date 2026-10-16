# mnKord

**mnKord** is a MIDI chord-mapping effect: one input key triggers a chord of output notes.  
**Home page and download:** https://miklosnemeth.github.io/mnkord/ · [Latest release](https://github.com/miklosnemeth/mnkord/releases/latest)  
Built with [JUCE](https://juce.com) 9 as **VST3** and **Standalone**.

**Documentation:** [English](docs/README.md) · [Magyar](docs/README.hu.md). **User guide:** [English](docs/USER_GUIDE.md) ([PDF](docs/mnKord-User-Guide.pdf)) · [Magyar](docs/USER_GUIDE.hu.md) ([PDF](docs/mnKord-Felhasznaloi-utmutato.pdf))

## Features

- Map any MIDI note to an arbitrary chord (click INPUT key, then CHORD keys)
- Chord templates: Maj, Min, Sus2, Sus4, 7, Maj7, Min7, Dim, Add9
- Invert a chord (right-click a CHORD key)
- Octave strip above the INPUT keyboard: click an octave (it starts on the scale root), then **Copy up** / **Copy down** to duplicate its chords one octave higher or lower
- Type a chord symbol (`Dm9/G`, `Bbmaj7`, `C6/9`, ...) in the field above the CHORD keyboard to replace the selected chord; the chord's name is shown for the selection
- **Learn**: play a key on your MIDI controller to select (or create) its mapping
- **Undo / Redo** (buttons or Ctrl+Z / Ctrl+Y) for mapping edits
- **Velocity layers**: a key can have a second, hard layer that plays when the input velocity reaches a threshold (pick Soft / Hard from the layer menu above the INPUT keyboard)
- **Transpose** the whole preset (keys and chords) or only its chords, from the Preset menu
- Preset library: read-only factory presets (about 30: diatonic, pop, jazz and soul, cinematic, dark, quartal, chromatic utility sets)
  plus your own presets, saved as files shared by every project (see below)
- Preset browser with GROUP / AUTHOR / STYLE (tag) columns, search and favourites (star, shared by all projects, kept in `favorites.txt`); unsaved-changes marker, Save, Revert
- Preset metadata: name, group, author, style tags
- Previous / next preset arrows next to the preset name; right-click a preset in the browser for Load, Duplicate, Rename, Show in folder and Delete
- XML preset **Export**; **Import** accepts a single preset or a multi-preset bank file
- Host program list mirrors the preset list (a fixed 128 programs, so VST3 hosts keep their indices; unused slots show "-")
- Overlapping chords share notes with ref-counting (no stuck or cut-off notes); the sustain pedal passes through to the synth
- Non-note MIDI (CC, pitch bend, aftertouch, …) passes through
- Dark / light theme switch

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

## Build (Linux)

```bash
sudo apt install build-essential cmake git libasound2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxcomposite-dev libxrender-dev
cmake -S . -B build
cmake --build build -j
```

## Usage

1. Load **mnKord** as a MIDI effect (or run the Standalone and route MIDI in/out).
2. Pick a preset from the chip in the header, or create one with **New**.
3. Click a key on the **INPUT** keyboard to select or create a mapping.
4. Click keys on the **CHORD** keyboard to add/remove chord tones, or right-click an INPUT key for chord templates.
5. **Save** writes your changes (a dot next to the preset name marks unsaved ones). The **Preset** menu has Save as, Revert, Rename, Import, Export, Show in folder and Delete.

Unmapped input notes are dropped. Mapped notes expand to their chord; overlapping chords share output notes via reference counting so notes do not cut each other off incorrectly.

## Preset library

Presets live in `Documents\MNAudio\mnKord`:

| Folder | Contents |
|--------|----------|
| `Factory` | The built-in presets, written by the plugin and rewritten when they change. Read only: edit and **Save as** to keep a variant. |
| `User` | One `.xml` file per preset you saved, shared by every project and plugin instance. Drop a file in and it shows up the next time the browser opens. |

The User presets keep the order they were added in (it's kept in `user-order.txt`), which is also the order of the host's program list; files dropped in from outside go to the end. The preset browser lists them by name.

A DAW project stores the **active preset's current state**, not a reference, so it plays back the same even if the files change or the project is opened on another machine; in that case the preset comes back as an unsaved one that **Save** writes into that machine's `User` folder. Leaving a preset with unsaved changes asks whether to save them, and **Delete** moves the file to the recycle bin.

Projects saved by earlier versions (which stored the whole preset list) are migrated: their own presets are written to `User` the first time they open.

## Preset file format

Single preset (one per file in the `User` folder):

```xml
<mnKordPreset version="1">
  <Preset name="My Patch" group="User" author="you" tags="Pop,Ballad">
    <Mapping input="60" output="60 64 67"/>
  </Preset>
</mnKordPreset>
```

Bank (accepted by **Import**, which saves each preset in it as its own file):

```xml
<mnKordBank version="1">
  <Preset name="A" group="User" author="you">
    <Mapping input="60" output="60 64 67"/>
  </Preset>
</mnKordBank>
```

`input` / `output` values are MIDI note numbers `0–127`. A mapping may also carry `minVelocity` (a hard velocity layer) and `velocity` (per output note velocities).

## Project layout

```text
CMakeLists.txt
CHANGELOG.md
AGENTS.md                  # rules for AI agents (build, tests, versioning); CLAUDE.md points to it
docs/                      # documentation home (README.md, README.hu.md), user guide in English and Hungarian
                           # (+ PDFs, images/, build-pdf.py) and index.html, the web home page
cmake/
  Version.cmake            # dev suffix (commit) for the displayed version
  MnKordVersion.h.in       # template of the generated MnKordVersion.h
Source/
  PluginProcessor.h/.cpp   # MIDI engine, preset library, state, file I/O
  PluginEditor.h/.cpp      # UI, keyboards, themes, preset browser/menu
  NoteName.h/.cpp          # MIDI note ↔ name helpers
  ChordSymbol.h/.cpp       # chord symbols ↔ notes, chord voicing and naming
Tests/
  LibraryTests.cpp         # console tests (library, state, chord symbols, velocity layers)
```

## Tests

```powershell
cmake -S . -B build-tests -DMNKORD_BUILD_TESTS=ON
cmake --build build-tests --config Release --target mnKordTests
build-tests\Release\mnKordTests.exe
```

The tests use a temporary library folder and never touch `Documents\MNAudio\mnKord`.

## Versioning

mnKord follows [Semantic Versioning](https://semver.org/): **MAJOR.MINOR.PATCH**. The first release is `1.0.0`:

| Part | Goes up when | Example |
|------|--------------|---------|
| MAJOR | A change is incompatible: projects or preset files from earlier versions don't load or play the same, or a feature is removed | `1.4.2` → `2.0.0` |
| MINOR | A feature is added or changed, and older projects and presets still work | `1.4.2` → `1.5.0` |
| PATCH | Only bugs are fixed | `1.4.2` → `1.4.3` |

The version is set in one place, `project(mnKord VERSION x.y.z)` in `CMakeLists.txt`; JUCE builds it into the
plugin (VST3 metadata, `JucePlugin_VersionString`) and the window shows it next to the title.

Keeping the version and the changelog up to date is done by the AI agents working on the project, as part of
every change, following [AGENTS.md](AGENTS.md); the rules are summarised here.

**During development** the version in `CMakeLists.txt` is the *next* release. Raise it with the first change
after a release, by the largest kind of change since then (a fix after `1.0.0` makes it `1.0.1`; a feature
added later makes that `1.1.0`). Every change also goes under **Unreleased** in [CHANGELOG.md](CHANGELOG.md).
Builds between releases show which commit they came from, e.g. `v1.0.0-dev+037de27`; the suffix updates by
itself when the commit changes.

**To release** a version (ask the agent to do it, or by hand):

1. Check that the version in `CMakeLists.txt` is right for the changes in the changelog.
2. In `CHANGELOG.md`, turn `## [Unreleased] - x.y.z` into `## [x.y.z] - YYYY-MM-DD`.
3. Merge to `master`, then tag that commit and push the tag:

   ```bash
   git tag -a vx.y.z -m "mnKord x.y.z"
   git push origin vx.y.z
   ```

A build of the tagged commit shows the plain version (`v1.0.0`).

## Installer (Windows)

The installer puts the VST3 into `%CommonProgramFiles%\VST3` (the Standalone is not included) and the
licenses into `Program Files\MNAudio\mnKord`. It is built with [Inno Setup 6](https://jrsoftware.org/isinfo.php)
from a Release build of the tagged commit:

```powershell
cmake -S . -B build
cmake --build build --config Release --target mnKord_VST3
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DMyAppVersion=1.0.0 installer\mnKord.iss
```

The result is `build\installer\mnKord-1.0.0-win-x64-setup.exe`. Use the version in `CMakeLists.txt` for `MyAppVersion`.

## License

Copyright (C) 2026 Miklós Németh.

mnKord is free software under the **GNU Affero General Public License v3.0** (see [LICENSE](LICENSE)):
you may use, study, change and share it, but every modified version you distribute (or offer to
users over a network) must be released under the same license, with its source code.

It is built with [JUCE](https://juce.com) under JUCE's AGPLv3 option, which is what makes this license
a good fit.

The bundled fonts keep their own licenses, whose texts are in [Resources/Fonts](Resources/Fonts) and must
travel with any binary release:

- Montserrat: SIL Open Font License 1.1 ([Montserrat-OFL.txt](Resources/Fonts/Montserrat-OFL.txt))
- Roboto Condensed: Apache License 2.0 ([RobotoCondensed-LICENSE.txt](Resources/Fonts/RobotoCondensed-LICENSE.txt))
