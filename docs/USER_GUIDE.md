# mnKord user guide

**English** · [Magyar](USER_GUIDE.hu.md) · [Documentation home](README.md)

mnKord is a MIDI effect: you play one key, and it sends a whole chord. It does not make sound by
itself. Put it in front of a synth, and the synth plays the chords.

```text
MIDI keyboard  ->  mnKord  ->  synth / instrument
   (one key)      (maps)         (plays the chord)
```

## Install

**Windows:** run `mnKord-<version>-win-x64-setup.exe`. It puts the plugin (VST3) into
`C:\Program Files\Common Files\VST3\MNAudio\` (a folder of its own inside the standard VST3 folder, which DAWs scan). Rescan the
plugins in your DAW if it does not show up. The Standalone program is not part of the installer.

The user guide (PDF) and the licenses are installed into `C:\Program Files\Common Files\MNAudio\mnKord`, with
shortcuts in the Start menu. Installing a newer version over an old one works: Setup shows the version it found
("Upgrade: v1.2.0 -> v1.3.0"), asks before going back to an older one, and asks you to close your DAW if it has
mnKord loaded.

To remove it, use *Settings → Apps → mnKord → Uninstall*. Your presets (see below) are kept.

## Use it in a DAW

1. Create an **instrument track** and add **mnKord** to it. (mnKord is a VST3 *instrument* so that hosts
   let it receive MIDI and pass MIDI on. Some DAWs show it among the instruments.)
2. Put the synth that should play the chords **after** mnKord on the same track (or route mnKord's MIDI
   output to the synth's track). How this is done depends on the DAW: look for "MIDI output" or "plugin chain".
3. Play your keyboard. A key that has a chord in the current preset plays that chord. A key without a
   mapping plays nothing, and every other kind of MIDI (pitch bend, modulation, sustain pedal, ...) passes
   through to the synth unchanged.

## The window

| Area | What it is |
|------|------------|
| Header | The mnKord title with the plugin's version under it, the preset name with **< >** arrows, **New**, **Save**, **Menu** button, and **Undo / Redo**. A dot at the left of the name means the preset has unsaved changes. |
| **INPUT** keyboard | The keys you play. Keys that have a chord are coloured. The strip above the keys shows octaves. |
| **CHORD** keyboard | The notes of the chord of the selected INPUT key. |
| Chord name field | Type a chord symbol (`Dm9/G`, `Bbmaj7`, `C6/9`, ...) to replace the selected chord. Above it you see the chord's name and its notes. |

![The mnKord window](images/main.png)

### Make a chord

1. Click a key on the **INPUT** keyboard. It is selected.
2. Click keys on the **CHORD** keyboard to add or remove notes of its chord.

Faster ways:
- **Right-click an INPUT key** for chord templates (triads and sevenths). A template that would leave the
  selected scale is greyed out. The menu also has *Octave up / down*, *Clear chord* and *Fit to scale*.
- **Type a chord symbol** in the field above the CHORD keyboard and press Enter.
- **Right-click a CHORD key** to invert the chord up or down.
- **Learn:** press **Learn** and play a key on your controller. It selects that key's mapping (or creates it).

![Chord templates: right-click an INPUT key](images/chord-templates.png)

### Copy a chord set to another octave

Click an octave in the strip above the INPUT keyboard, then **Copy up** or **Copy down**. It copies that
octave's chords one octave higher or lower.


### Scale

The scale button (it says **Full range** until you choose one) limits the keyboard to the notes of a scale
(major, minor, pentatonic, ...) and fills the whole preset with triads or seventh chords of that scale.

### Velocity layers

The layer menu above the INPUT keyboard chooses which layer you are editing:

- **Soft layer:** plays at any velocity.
- **Hard layer from velocity 80 / 90 / 100 / 110 / 120:** a second chord for the same key, played instead
  when you hit the key at least that hard.

So one key can play a plain triad when played softly and a fuller chord when played hard.

![The velocity layer menu](images/velocity-layers.png)

### Undo

Every edit of the mappings can be undone and redone with the arrows, or **Ctrl+Z** / **Ctrl+Y**.

## Presets

A preset is a set of key-to-chord mappings, with a name, a group, an author and style tags.

- **Factory presets** (about 30) are read only. Edit one and use **Save as** to keep your version.
- **Your presets** are saved as files and are shared by all projects and all copies of the plugin.
- Click the preset name to open the **browser**: columns for group, author and style, a search field and
  a star to mark favourites. Right-click a preset to load, duplicate, rename, show it in its folder or
  delete it (deleted files go to the recycle bin).
- **Save** writes the changes. When you leave a preset with unsaved changes, mnKord asks whether to save
  them. For a factory preset the choices are *Discard* or *Cancel* (use *Save as* first to keep them).
- **New** creates a preset. You can fill it with the chords of a scale (chord size, inversion, octave and the
  velocity of each chord role).
- The **Menu** button has *Save as*, *Revert to saved*, *Rename / edit info*, **Transpose** (the keys and
  chords together, or only the chords), *Import* (a preset or a whole bank file), *Export* and *Show in folder*.

![The preset browser](images/preset-browser.png)

![The Menu button](images/preset-menu.png)

Presets live in `Documents\MNAudio\mnKord`: the `Factory` folder is managed by the plugin, and the `User`
folder holds your own presets. You can copy preset files in or out of it.

### Projects

A DAW project stores the preset you were using **with its current edits**, not just a link to it. The project
sounds the same on another computer even without your presets. If the stored preset differs from the one in
the library (for example it was edited, or a later version changed a factory preset), it comes back marked
as unsaved. **Revert to saved** loads the library's version, **Save as** keeps the project's version.

### Program change

The host sees a fixed list of 128 programs: the presets, then empty slots named "-". Your DAW can switch
presets with program changes and automation. User presets keep their numbers because they stay in the order
you added them.

## Standalone

The Standalone program (built from the source, not part of the installer) runs mnKord without a DAW.
Open **Options** (top left) to choose the MIDI input and output and the audio device. Send mnKord's MIDI output
to a synth, for example through a virtual MIDI cable.

## Tips and problems

- **No sound:** mnKord makes no sound. Check that a synth is after it and that it receives MIDI.
- **A key plays nothing:** it has no chord in the current preset (it is dropped on purpose). Select the key
  and add notes.
- **Stuck notes:** transport stop and "all notes off" release the notes. If the synth still hangs a note,
  press the same key again.
- **The preset name has a dot:** it has unsaved changes. See *Projects* above.
- **Where is the plugin?** `C:\Program Files\Common Files\VST3\MNAudio\mnKord.vst3`. Rescan the plugins in the DAW.

## Keyboard

| Key | Action |
|-----|--------|
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Enter in the chord field | Apply the chord symbol |
| Esc in the chord field | Clear it |

## License

mnKord is free software under the GNU Affero General Public License v3.0 (see [LICENSE](../LICENSE)).
The bundled fonts keep their own licenses, in [Resources/Fonts](../Resources/Fonts).
