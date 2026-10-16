# mnKord documentation

**English** · [Magyar](README.hu.md)

**mnKord** is a MIDI effect: play one key, and it sends a whole chord to your synth. It makes no sound by
itself, so it goes in front of a synth on an instrument track.

## Quick start

1. **Install it.** Run `mnKord-<version>-win-x64-setup.exe` (Windows). The VST3 goes to
   `C:\Program Files\Common Files\VST3\`. Rescan the plugins in your DAW.
2. **Add it to a track.** Put mnKord on an instrument track, **before** the synth that should play the chords.
3. **Pick a preset.** Click the preset name in the header and choose one, for example *Pop Four Chords*.
4. **Play.** Every key that has a chord plays it. Other MIDI (pitch bend, sustain, ...) passes through.

To make your own chord: click a key on the **INPUT** keyboard, then click the notes on the **CHORD** keyboard
(or type a chord symbol such as `Dm9/G`).

## The user guide

| | |
|---|---|
| **User guide (English)** | [Read it here](USER_GUIDE.md) · [PDF](mnKord-User-Guide.pdf) |
| **Felhasználói útmutató (magyar)** | [Olvasd itt](USER_GUIDE.hu.md) · [PDF](mnKord-Felhasznaloi-utmutato.pdf) |

What is in it:

- [Install](USER_GUIDE.md#install) and [use it in a DAW](USER_GUIDE.md#use-it-in-a-daw)
- [The window](USER_GUIDE.md#the-window) and [making a chord](USER_GUIDE.md#make-a-chord)
- [Velocity layers](USER_GUIDE.md#velocity-layers)
- [Presets](USER_GUIDE.md#presets) and [projects](USER_GUIDE.md#projects)
- [The Standalone program](USER_GUIDE.md#standalone)
- [Tips and problems](USER_GUIDE.md#tips-and-problems)

## More

- [Download](https://github.com/miklosnemeth/mnKord/releases/latest) the latest release
- [Changelog](../CHANGELOG.md): what changed in each version
- [Source code and build instructions](../README.md) (Windows, Linux)
- [Report a bug or ask for a feature](https://github.com/miklosnemeth/mnKord/issues)
- [Home page](index.html) (a web page with screenshots)

mnKord is free software under the [AGPL-3.0](../LICENSE). Vibecoded by Miklós Németh.
