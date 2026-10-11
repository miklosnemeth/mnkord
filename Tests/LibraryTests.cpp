// Console tests for the preset library, project state, chord symbols, velocity layers and the
// MIDI engine.
// Built with -DMNKORD_BUILD_TESTS=ON (target mnKordTests); uses a temporary library folder,
// never the real Documents\MNAudio\mnKord.
#include "PluginProcessor.h"
#include "ChordSymbol.h"
#include "MnKordVersion.h"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <new>
#include <thread>

// Counts heap allocations while countAllocations is set, to check that processBlock doesn't allocate.
static std::atomic<bool> countAllocations { false };
static std::atomic<int> allocations { 0 };

void* operator new(std::size_t size)
{
    if (countAllocations.load())
        ++allocations;

    if (auto* p = std::malloc(size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static int failures = 0, checks = 0;
static int factoryCount = 0;   // number of built-in presets, read from a fresh library

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (! (cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

static int indexOfName(MnKordAudioProcessor& p, const juce::String& name, bool factory)
{
    auto& list = p.getPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == name && list[(size_t) i].isFactory == factory)
            return i;
    return -1;
}

static int countXml(const juce::File& dir)
{
    return dir.findChildFiles(juce::File::findFiles, true, "*.xml").size();
}

static void editActive(MnKordAudioProcessor& p)
{
    const juce::ScopedLock lock(p.getStateLock());
    auto& preset = p.getPresets()[(size_t) p.getActivePresetIndex()];
    preset.mappings.front().outputNotes.push_back(120);
    preset.mappings.front().outputVelocities[120] = 55;
}

static juce::MemoryBlock state(MnKordAudioProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation(block);
    return block;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mnKordLibTest");
    root.deleteRecursively();
    MnKordAudioProcessor::overrideLibraryFolder(root);

    // 1. Fresh library: factory presets in memory and mirrored to disk, empty User folder.
    {
        MnKordAudioProcessor p;
        factoryCount = (int) p.getPresets().size();
        CHECK(factoryCount >= 6);
        CHECK(p.getActivePresetIndex() == 0);
        CHECK(p.getPresets()[0].isFactory);
        CHECK(countXml(MnKordAudioProcessor::getFactoryFolder()) == factoryCount);
        CHECK(countXml(MnKordAudioProcessor::getUserFolder()) == 0);
        CHECK(! p.isPresetDirty(0));
        CHECK(p.canRevertPreset(0));
    }

    // 2. Dirty detection and revert on a factory preset.
    {
        MnKordAudioProcessor p;
        const auto before = p.getPresets()[0].mappings.front().outputNotes.size();
        editActive(p);
        CHECK(p.isPresetDirty(0));
        CHECK(p.revertPreset(0));
        CHECK(! p.isPresetDirty(0));
        CHECK(p.getPresets()[0].mappings.front().outputNotes.size() == before);
        CHECK(! p.saveUserPreset(0));      // factory presets can't be overwritten
        CHECK(! p.deletePreset(0));
    }

    // 3. Save as: new user file, original goes back to its saved state, names are made unique.
    {
        MnKordAudioProcessor p;
        editActive(p);
        auto copy = p.getPresets()[0];
        copy.name = "My Copy";
        copy.group = "Mine";
        CHECK(p.addPreset(copy));
        CHECK((int) p.getPresets().size() == factoryCount + 1);
        CHECK(p.getActivePresetIndex() == factoryCount);
        CHECK(! p.getPresets()[(size_t) factoryCount].isFactory);
        CHECK(p.getPresets()[(size_t) factoryCount].file.existsAsFile());
        CHECK(! p.isPresetDirty(6));
        CHECK(p.getPresets()[(size_t) factoryCount].mappings.front().outputNotes.back() == 120);   // kept the edit
        CHECK(! p.isPresetDirty(0));                                           // original reverted
        CHECK(p.getPresets()[0].mappings.front().outputNotes.back() != 120);

        CHECK(p.addPreset(copy));
        CHECK(p.getPresets()[(size_t) factoryCount + 1].file.getFileName() == "My Copy 2.xml");
        CHECK(countXml(MnKordAudioProcessor::getUserFolder()) == 2);
    }

    // 4. A new instance (a restart) finds the user presets; editing + saving persists.
    {
        MnKordAudioProcessor p;
        CHECK((int) p.getPresets().size() == factoryCount + 2);
        const int i = indexOfName(p, "My Copy", false);
        CHECK(i >= 0);
        p.setActivePresetIndex(i);
        const auto notes = p.getPresets()[(size_t) i].mappings.front().outputNotes.size();
        editActive(p);
        CHECK(p.isPresetDirty(i));
        CHECK(p.saveUserPreset(i));
        CHECK(! p.isPresetDirty(i));

        MnKordAudioProcessor again;
        const int j = indexOfName(again, "My Copy", false);
        CHECK(again.getPresets()[(size_t) j].mappings.front().outputNotes.size() == notes + 1);
        CHECK(again.getPresets()[(size_t) j].mappings.front().velocityFor(120) == 55);
    }

    // 5. Leaving a dirty preset discards its edits.
    {
        MnKordAudioProcessor p;
        const int i = indexOfName(p, "My Copy", false);
        p.setActivePresetIndex(i);
        const auto notes = p.getPresets()[(size_t) i].mappings.front().outputNotes.size();
        editActive(p);
        p.setActivePresetIndex(0);
        CHECK(! p.isPresetDirty(i));
        CHECK(p.getPresets()[(size_t) i].mappings.front().outputNotes.size() == notes);
    }

    // 6. State round trips: the active preset's working copy, edits and all.
    {
        MnKordAudioProcessor a;
        const int i = indexOfName(a, "My Copy", false);
        a.setActivePresetIndex(i);
        editActive(a);
        const auto saved = state(a);

        MnKordAudioProcessor b;
        b.setStateInformation(saved.getData(), (int) saved.getSize());
        CHECK(b.getActivePresetIndex() == indexOfName(b, "My Copy", false));
        CHECK(b.isPresetDirty(b.getActivePresetIndex()));
        CHECK(b.getPresets()[(size_t) b.getActivePresetIndex()].mappings.front().outputNotes.back() == 120);

        // ...and a factory preset with edits.
        MnKordAudioProcessor c;
        c.setActivePresetIndex(2);
        editActive(c);
        const auto savedFactory = state(c);
        MnKordAudioProcessor d;
        d.setStateInformation(savedFactory.getData(), (int) savedFactory.getSize());
        CHECK(d.getActivePresetIndex() == 2);
        CHECK(d.getPresets()[2].isFactory);
        CHECK(d.isPresetDirty(2));

        // ...and a preset that isn't in this machine's library comes back as an unsaved one.
        const auto userFile = a.getPresets()[(size_t) i].file;
        MnKordAudioProcessor e;
        const int before = (int) e.getPresets().size();
        e.setActivePresetIndex(indexOfName(e, "My Copy", false));
        const auto savedUser = state(e);
        MnKordAudioProcessor::overrideLibraryFolder(root.getSiblingFile("mnKordLibTestOther"));
        MnKordAudioProcessor f;
        f.setStateInformation(savedUser.getData(), (int) savedUser.getSize());
        CHECK((int) f.getPresets().size() == factoryCount + 1);
        CHECK(f.getPresets().back().name == "My Copy");
        CHECK(f.getPresets().back().file == juce::File());
        CHECK(f.isPresetDirty((int) f.getPresets().size() - 1));
        CHECK(f.saveUserPreset((int) f.getPresets().size() - 1));
        CHECK(! f.isPresetDirty((int) f.getPresets().size() - 1));
        juce::ignoreUnused(before, userFile);
        MnKordAudioProcessor::getLibraryFolder().deleteRecursively();
        MnKordAudioProcessor::overrideLibraryFolder(root);
    }

    // 7. Version 1 state (the whole preset list) is migrated into the User folder, once.
    {
        MnKordAudioProcessor src;
        const int before = countXml(MnKordAudioProcessor::getUserFolder());

        juce::XmlElement legacy("mnKordState");
        legacy.setAttribute("version", 1);
        legacy.setAttribute("activePreset", 1);
        auto* list = legacy.createNewChildElement("Presets");
        for (const char* name : { "Default", "Old Pad", "Old Lead" })
        {
            auto* preset = list->createNewChildElement("Preset");
            preset->setAttribute("name", name);
            preset->setAttribute("group", "Old");
            preset->setAttribute("author", "me");
            auto* mapping = preset->createNewChildElement("Mapping");
            mapping->setAttribute("input", 60);
            mapping->setAttribute("output", "60 64 67");
        }
        const auto text = legacy.toString();

        MnKordAudioProcessor p;
        p.setStateInformation(text.toRawUTF8(), (int) text.getNumBytesAsUTF8());
        CHECK(countXml(MnKordAudioProcessor::getUserFolder()) == before + 2);   // "Default" is built in
        CHECK(p.getPresets()[(size_t) p.getActivePresetIndex()].name == "Old Pad");

        MnKordAudioProcessor q;
        q.setStateInformation(text.toRawUTF8(), (int) text.getNumBytesAsUTF8());
        CHECK(countXml(MnKordAudioProcessor::getUserFolder()) == before + 2);   // not imported twice
    }

    // 8. Delete a user preset (to the recycle bin); external additions are picked up.
    {
        MnKordAudioProcessor p;
        const int n = (int) p.getPresets().size();
        const int i = indexOfName(p, "Old Lead", false);
        CHECK(i >= 0);
        const auto file = p.getPresets()[(size_t) i].file;
        CHECK(p.deletePreset(i));
        CHECK(! file.exists());
        CHECK((int) p.getPresets().size() == n - 1);

        auto extra = MnKordAudioProcessor::getUserFolder().getChildFile("Dropped In.xml");
        extra.replaceWithText("<mnKordPreset version=\"1\"><Preset name=\"Dropped In\" group=\"\" author=\"\"><Mapping input=\"60\" output=\"60 64 67\"/></Preset></mnKordPreset>");
        p.refreshUserLibrary();
        CHECK(indexOfName(p, "Dropped In", false) >= 0);
        extra.deleteFile();
        p.refreshUserLibrary();
        CHECK(indexOfName(p, "Dropped In", false) < 0);
    }

    // 9. Import splits a multi-preset file into one user file per preset.
    {
        MnKordAudioProcessor p;
        const int before = countXml(MnKordAudioProcessor::getUserFolder());
        auto bank = root.getChildFile("bank.xml");
        bank.replaceWithText("<mnKordBank version=\"1\"><Preset name=\"B1\" group=\"\" author=\"\"><Mapping input=\"60\" output=\"60 64\"/></Preset><Preset name=\"B2\" group=\"\" author=\"\"><Mapping input=\"61\" output=\"61 65\"/></Preset></mnKordBank>");
        CHECK(p.importPresetsFromFile(bank) == 2);
        CHECK(countXml(MnKordAudioProcessor::getUserFolder()) == before + 2);
        CHECK(p.getPresets()[(size_t) p.getActivePresetIndex()].name == "B2");
    }


    // 10. Chord symbols: parsing, voicing and naming round-trip.
    {
        const auto slash = ChordSymbol::parseChord("Dm9/G");
        CHECK(slash.valid && slash.rootPc == 2 && slash.bassPc == 7);
        const auto sixNine = ChordSymbol::parseChord("C6/9");
        CHECK(sixNine.valid && sixNine.bassPc == -1 && sixNine.intervals.size() == 5);
        CHECK(! ChordSymbol::parseChord("").valid);
        CHECK(! ChordSymbol::parseChord("H").valid);

        for (const char* symbol : { "C", "Am7", "F#m7b5", "Bbmaj9", "G/B", "Dsus4", "C6/9", "Ebaug", "A7", "Dm9" })
        {
            const auto mapping = ChordSymbol::voicedMapping(60, ChordSymbol::parseChord(symbol));
            CHECK(! mapping.outputNotes.empty());
            CHECK(ChordSymbol::nameFor(mapping.outputNotes) == juce::String(symbol));
        }
    }

    // 11. Velocity layers: the hardest layer the velocity reaches plays; the choice survives project state.
    {
        MnKordAudioProcessor p;
        p.prepareToPlay(44100.0, 512);

        {
            const juce::ScopedLock lock(p.getStateLock());
            auto& preset = p.getPresets()[(size_t) p.getActivePresetIndex()];
            preset.mappings.clear();

            NoteMapping soft;
            soft.inputNote = 60;
            soft.outputNotes = { 60 };
            NoteMapping hard;
            hard.inputNote = 60;
            hard.minVelocity = 100;
            hard.outputNotes = { 72 };
            preset.mappings = { soft, hard };
        }

        auto firstOutputNote = [&p](int velocity)
        {
            juce::AudioBuffer<float> audio(0, 512);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) velocity), 0);
            p.processBlock(audio, midi);

            int note = -1;
            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    note = metadata.getMessage().getNoteNumber();

            juce::AudioBuffer<float> audio2(0, 512);
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            p.processBlock(audio2, off);
            return note;
        };

        CHECK(firstOutputNote(40) == 60);
        CHECK(firstOutputNote(99) == 60);
        CHECK(firstOutputNote(100) == 72);
        CHECK(firstOutputNote(127) == 72);

        const auto saved = state(p);
        MnKordAudioProcessor q;
        q.setStateInformation(saved.getData(), (int) saved.getSize());
        const auto& restored = q.getPresets()[(size_t) q.getActivePresetIndex()].mappings;
        CHECK(restored.size() == 2 && restored[0].minVelocity == 1 && restored[1].minVelocity == 100);
    }

    // 12. Project state with non-ASCII text survives a save/load round trip.
    {
        MnKordAudioProcessor p;
        Preset preset = p.getPresets()[0];
        preset.name = juce::String::fromUTF8("Akkordok \xc3\xa1rv\xc3\xadzt\xc5\xb1r\xc5\x91");
        preset.author = juce::String::fromUTF8("Mikl\xc3\xb3s");
        CHECK(p.addPreset(preset));

        const auto saved = state(p);
        MnKordAudioProcessor q;
        q.setStateInformation(saved.getData(), (int) saved.getSize());
        const auto& active = q.getPresets()[(size_t) q.getActivePresetIndex()];
        CHECK(active.name == preset.name);
        CHECK(active.author == preset.author);
        CHECK(active.file.existsAsFile());   // matched to its file, not restored as a stray copy
        CHECK(! q.isPresetDirty(q.getActivePresetIndex()));
    }

    // 13. MIDI engine: shared notes, the sustain pedal passes through, all notes off.
    {
        MnKordAudioProcessor p;
        {
            const juce::ScopedLock lock(p.getStateLock());
            auto& preset = p.getPresets()[(size_t) p.getActivePresetIndex()];
            NoteMapping c, a;
            c.inputNote = 60;
            c.outputNotes = { 60, 64, 67 };
            a.inputNote = 62;
            a.outputNotes = { 57, 60, 64 };
            preset.mappings = { c, a };
        }

        auto run = [&p](std::initializer_list<juce::MidiMessage> messages)
        {
            juce::AudioBuffer<float> audio(0, 512);
            juce::MidiBuffer midi;
            for (const auto& m : messages)
                midi.addEvent(m, 0);
            p.processBlock(audio, midi);

            std::vector<juce::MidiMessage> out;
            for (const auto metadata : midi)
                out.push_back(metadata.getMessage());
            return out;
        };

        auto count = [](const std::vector<juce::MidiMessage>& out, bool on, int note)
        {
            return (int) std::count_if(out.begin(), out.end(), [on, note](const juce::MidiMessage& m)
            {
                return (on ? m.isNoteOn() : m.isNoteOff()) && m.getNoteNumber() == note;
            });
        };

        // Overlapping chords share note 60/64: it starts once and stops with the last key.
        auto out = run({ juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), juce::MidiMessage::noteOn(1, 62, (juce::uint8) 100) });
        CHECK(count(out, true, 60) == 1 && count(out, true, 57) == 1);
        out = run({ juce::MidiMessage::noteOff(1, 60) });
        CHECK(count(out, false, 60) == 0 && count(out, false, 67) == 1);
        out = run({ juce::MidiMessage::noteOff(1, 62) });
        CHECK(count(out, false, 60) == 1 && count(out, false, 57) == 1);

        // The sustain pedal is left to the synth: it passes through, and note-offs aren't held back.
        out = run({ juce::MidiMessage::controllerEvent(1, 64, 127), juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100) });
        CHECK(std::any_of(out.begin(), out.end(), [](const juce::MidiMessage& m) { return m.isSustainPedalOn(); }));
        out = run({ juce::MidiMessage::noteOff(1, 60) });
        CHECK(count(out, false, 60) == 1 && count(out, false, 64) == 1 && count(out, false, 67) == 1);
        out = run({ juce::MidiMessage::controllerEvent(1, 64, 0) });
        CHECK(out.size() == 1 && out[0].isSustainPedalOff());

        // All notes off (transport stop) releases everything, so the chord plays again afterwards.
        run({ juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100) });
        out = run({ juce::MidiMessage::allNotesOff(1) });
        CHECK(count(out, false, 60) == 1 && count(out, false, 64) == 1 && count(out, false, 67) == 1);
        CHECK(p.getActiveInputNotes() == MnKordAudioProcessor::NoteBits {});
        out = run({ juce::MidiMessage::noteOn(1, 62, (juce::uint8) 100) });
        CHECK(count(out, true, 60) == 1 && count(out, true, 64) == 1);
        run({ juce::MidiMessage::noteOff(1, 62) });
    }

    // 14. MIDI engine: channels are independent, duplicate chord notes count once, a preset
    //     switch releases what's sounding, and processBlock doesn't allocate.
    {
        MnKordAudioProcessor p;
        p.prepareToPlay(48000.0, 512);
        {
            const juce::ScopedLock lock(p.getStateLock());
            auto& preset = p.getPresets()[(size_t) p.getActivePresetIndex()];
            NoteMapping c;
            c.inputNote = 60;
            c.outputNotes = { 60, 64, 64, 67 };
            preset.mappings = { c };
        }

        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        auto run = [&](std::initializer_list<juce::MidiMessage> messages)
        {
            midi.clear();
            for (const auto& m : messages)
                midi.addEvent(m, 0);
            p.processBlock(audio, midi);
        };

        auto count = [&midi](bool on, int channel, int note)
        {
            int n = 0;
            for (const auto metadata : midi)
            {
                const auto m = metadata.getMessage();
                if ((on ? m.isNoteOn() : m.isNoteOff()) && m.getChannel() == channel && m.getNoteNumber() == note)
                    ++n;
            }
            return n;
        };

        run({ juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), juce::MidiMessage::noteOn(2, 60, (juce::uint8) 100) });
        CHECK(count(true, 1, 64) == 1 && count(true, 2, 64) == 1);
        run({ juce::MidiMessage::noteOff(1, 60) });
        CHECK(count(false, 1, 64) == 1 && count(false, 2, 64) == 0);

        // Channel 2 is still sounding: switching presets releases it on the next block.
        p.setActivePresetIndex(1);
        run({});
        CHECK(count(false, 2, 60) == 1 && count(false, 2, 64) == 1 && count(false, 2, 67) == 1);
        CHECK(p.getActiveOutputNotes() == MnKordAudioProcessor::NoteBits {});

        // Warm-up, then a block full of chord traffic must not touch the heap.
        p.setActivePresetIndex(indexOfName(p, "Chromatic Sevenths", true));
        run({});
        juce::MidiBuffer busy;
        for (int i = 0; i < 64; ++i)
        {
            busy.addEvent(juce::MidiMessage::noteOn(1 + i % 16, 60 + i % 12, (juce::uint8) 100), i);
            busy.addEvent(juce::MidiMessage::controllerEvent(1 + i % 16, 64, i % 2 == 0 ? 127 : 0), i);
            busy.addEvent(juce::MidiMessage::noteOff(1 + i % 16, 60 + i % 12), i);
        }
        busy.addEvent(juce::MidiMessage::allNotesOff(1), 100);
        midi = busy;

        allocations = 0;
        countAllocations = true;
        p.processBlock(audio, midi);
        countAllocations = false;
        CHECK(allocations.load() == 0);
        CHECK(! midi.isEmpty());
    }

    // 15. A program change from another thread (VST3 hosts send it on the audio thread) waits
    //     for the message thread; the host sees the requested program meanwhile.
    {
        MnKordAudioProcessor p;
        CHECK(p.getActivePresetIndex() == 0);

        std::thread audioThread([&p] { p.setCurrentProgram(2); });
        audioThread.join();

        CHECK(p.getActivePresetIndex() == 0);
        CHECK(p.getCurrentProgram() == 2);

        p.applyPendingProgramChange();
        CHECK(p.getActivePresetIndex() == 2);
        CHECK(p.getCurrentProgram() == 2);

        // On the message thread it switches at once.
        p.setCurrentProgram(3);
        CHECK(p.getActivePresetIndex() == 3);
        p.applyPendingProgramChange();
        CHECK(p.getActivePresetIndex() == 3);
    }

    // 16. Host program list: a fixed count (VST3 hosts read it once), empty slots past the presets,
    //     and the host is told whenever the list or the current program changes.
    {
        struct HostListener : juce::AudioProcessorListener
        {
            int programChanges = 0;
            void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
            void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
            {
                if (details.programChanged)
                    ++programChanges;
            }
        };

        MnKordAudioProcessor p;
        HostListener host;
        p.addListener(&host);

        const int count = p.getNumPrograms();
        const int presetCount = (int) p.getPresets().size();
        CHECK(count == juce::jmax(MnKordAudioProcessor::minHostPrograms, presetCount));
        CHECK(p.getProgramName(0) == p.getPresets()[0].name);
        CHECK(p.getProgramName(presetCount) == "-");

        p.setCurrentProgram(1);
        CHECK(host.programChanges == 1 && p.getCurrentProgram() == 1);

        Preset added = p.getPresets()[0];
        added.name = "Host List Test";
        CHECK(p.addPreset(added));
        CHECK(host.programChanges == 2);
        CHECK(p.getNumPrograms() == count);
        CHECK(p.getProgramName(presetCount) == "Host List Test");
        CHECK(p.getCurrentProgram() == presetCount);

        // An empty slot keeps the current preset and points the host back at it.
        p.setCurrentProgram(count - 1);
        CHECK(p.getActivePresetIndex() == presetCount);
        CHECK(host.programChanges == 3);

        CHECK(p.deletePreset(presetCount));
        CHECK(host.programChanges == 4);
        CHECK(p.getProgramName(presetCount) == "-");
        CHECK(p.getNumPrograms() == count);

        p.removeListener(&host);
    }

    // 17. Version: the plugin reports the CMake project version, the display string extends it.
    {
        const juce::String version(MNKORD_VERSION), display(MNKORD_VERSION_STRING);
        CHECK(juce::String(JucePlugin_VersionString) == version);
        CHECK(juce::StringArray::fromTokens(version, ".", "").size() == 3);
        CHECK(display == version || display.startsWith(version + "-dev"));
    }

    // 18. Chord symbols: unknown ones are rejected (the chord field passes user text here).
    {
        CHECK(! ChordSymbol::parseChord("Cxyz").valid);
        CHECK(! ChordSymbol::parseChord("H7").valid);
        CHECK(ChordSymbol::parseChord("F#m7b5/C").valid);
    }

    // 19. Chord names: slash chords over a bass outside the chord, the bass the editor's chord
    //     label shows (C2 under Cm), no doubled notes in power chords, and every typed symbol
    //     reads back as the same chord.
    {
        auto nameOf = [](const char* symbol)
        {
            return ChordSymbol::nameFor(ChordSymbol::voicedMapping(60, ChordSymbol::parseChord(symbol)).outputNotes);
        };

        CHECK(nameOf("C/D") == "C/D");
        CHECK(nameOf("Am/G") == "Am/G");
        CHECK(nameOf("Gm7/E") == "Gm7/E");
        CHECK(nameOf("C/E") == "C/E");
        CHECK(ChordSymbol::nameFor({ 36, 60, 63, 67 }) == "Cm");
        CHECK(ChordSymbol::nameFor({ 59, 60, 64, 67 }) == "C/B");          // a bass line under C, as typed
        CHECK(ChordSymbol::nameFor({ 47, 60, 64, 67, 71 }) == "Cmaj7/B"); // B in the chord too

        for (const auto* symbol : { "C5", "G5", "E5" })
        {
            auto notes = ChordSymbol::voicedMapping(60, ChordSymbol::parseChord(symbol)).outputNotes;
            std::sort(notes.begin(), notes.end());
            CHECK(std::adjacent_find(notes.begin(), notes.end()) == notes.end());
        }

        auto pitchClasses = [](const std::vector<int>& notes)
        {
            unsigned mask = 0;
            for (const int note : notes)
                mask |= 1u << (note % 12);
            return mask;
        };

        int mismatches = 0;
        for (const auto* root : { "C", "C#", "Db", "D", "Eb", "E", "F", "F#", "Gb", "G", "Ab", "A", "Bb", "B" })
            for (const auto* suffix : { "", "m", "dim", "aug", "sus2", "sus4", "5", "6", "m6", "maj7", "m7", "7", "m7b5",
                                        "dim7", "add9", "madd9", "maj9", "m9", "9", "7sus4", "mmaj7", "7#9", "7b9", "6/9", "4th" })
                for (const auto* bass : { "", "/E", "/G" })
                {
                    const auto typed = ChordSymbol::voicedMapping(60, ChordSymbol::parseChord(juce::String(root) + suffix + bass)).outputNotes;
                    const auto named = ChordSymbol::parseChord(ChordSymbol::nameFor(typed));
                    const auto back = ChordSymbol::voicedMapping(60, named).outputNotes;
                    if (! named.valid || pitchClasses(typed) != pitchClasses(back) || typed.front() % 12 != back.front() % 12)
                        ++mismatches;
                }
        CHECK(mismatches == 0);
    }

    root.deleteRecursively();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
