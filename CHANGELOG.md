# Changelog

All notable changes to mnKord. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the version numbers follow [Semantic Versioning](https://semver.org/) as described in the README
("Versioning").

## [Unreleased] - 0.1.0

The first version, still in initial development (0.x): chord mapping with templates and chord symbols,
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
