# Changelog

All notable changes to mnKord. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the version numbers follow [Semantic Versioning](https://semver.org/) as described in the README
("Versioning").

## [1.5.0] - 2026-10-20

### Changed
- Installer: the plugin is installed into `Common Files\VST3\MNAudio\mnKord.vst3`, a folder of its own for MNAudio (as mnArpee does), instead of directly into `Common Files\VST3`. An earlier install in the old place is removed.
- Installer: the installed version is uninstalled before the new one goes in, so nothing of it is left behind, and the "Ready to install" page says whether this is a new installation, an upgrade, a reinstall or a downgrade.
- Installer: the part it shares with the mnArpee installer is one file in both projects, so the two installers behave the same.

## [1.4.1] - 2026-10-20

### Fixed
- The two keyboards are the same height again (1.4.0 made the CHORD keyboard taller than the INPUT one).

## [1.4.0] - 2026-10-20

### Changed
- The look now matches mnArpee: a slightly green dark palette, a green title with the version under it, cards with a border, and the same header with the arrows, New, Save, the undo and redo buttons.
- The Preset button in the header is called Menu now, and unsaved changes show as a dot at the left of the preset name (not a bullet after it).
- The keyboards are taller, to keep the window at the same height as the header is slimmer.

## [1.3.0] - 2026-10-17

### Changed
- Installer: it asks to close your DAW when the plugin is in use (the check now covers the VST3 file), instead of stopping with a file error.
- Installer: the "Ready to install" page shows the installed version ("Upgrade: v1.2.0 -> v1.3.0"), and going back to an older version asks first.
- Installer: the user guide and the licenses go to `Common Files\MNAudio\mnKord` (there is no folder page any more), and the old `Program Files\MNAudio\mnKord` folder of earlier installs is removed.

## [1.2.0] - 2026-10-16

### Removed
- The dark / light theme switch in the header: the plugin is always dark. The theme was never saved in projects or presets, so nothing else changes.

## [1.1.0] - 2026-10-16

### Changed
- The header no longer shows the preset's group and author under the title; the version of the plugin is there instead.

## [1.0.1] - 2026-10-16

### Fixed
- The installer's folder page no longer suggests that the plugin goes into Program Files: it now says that the
  plugin (VST3) is installed into the shared VST3 folder, and that the page only chooses where the user guide and
  the licenses go.

## [1.0.0] - 2026-10-16

The first release: chord mapping with templates and chord symbols,
velocity layers, octave copy, MIDI learn, undo/redo, transpose, the factory and user preset library with
the preset browser, and the dark and light themes.

### Added
- The version is shown next to the title in the plugin window header.

### Changed
- The sustain pedal is left to the synth: CC 64 passes through and note-offs are no longer held back
  while it is down.
- The host sees a fixed program list of 128 entries (more if the library is larger when the plugin
  loads): the presets, then empty slots named "-". VST3 hosts read the program count only once, so
  program numbers no longer shift when presets are added or deleted.
- The plugin no longer writes a debug log (`mnKord.log`) of every click.

### Fixed
- Notes stuck after transport stop: All notes off / All sound off now release the mapped notes.
- Deleting a preset other than the active one no longer cuts the notes that are sounding.
- A chord that lists the same note twice no longer sends two note-offs for it on a preset switch.
- The host is told when the preset list or the current preset changes.
- A host program change sent from the audio thread (VST3) no longer reads preset files there.
- The MIDI engine no longer allocates memory on the audio thread, and the editor holds the state
  lock for less time, so the audio thread waits less.
- Typing an unknown chord symbol no longer triggers an assertion in Debug builds.
- Builds with GCC on Linux.
- The chord name above the CHORD keyboard reads right with a bass note: "Cm" instead of "C chord" when
  the chord has its root added below, and slash chords over a bass outside the chord get their name
  ("C/D", "Am/G", "Gm7/E") instead of none or a misleading one ("Cadd9/D", "C6/G").
- Power chords typed as symbols (`C5`) no longer get the same note twice.
- Switching to the hard (or soft) velocity layer, or deleting a mapping, no longer selects a mapping of
  the other layer, whose chord the CHORD keys would then have edited.
- A preset change from the host clears the mapping selection instead of selecting an arbitrary mapping
  of the new preset.
- User presets keep their program numbers when a project is reopened: they stay in the order they were
  added (kept in `user-order.txt` in the library folder) instead of being re-sorted by name, so host
  program-change automation still picks the same presets. The preset browser still lists them by name.
- Undo and redo keep the selected key and velocity layer selected, instead of whatever mapping ends up
  at the same position.
