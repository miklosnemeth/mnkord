#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ChordSymbol.h"

namespace
{
    NoteMapping makeMapping(int inputNote, std::initializer_list<int> outputNotes)
    {
        NoteMapping mapping;
        mapping.inputNote = inputNote;
        mapping.outputNotes.assign(outputNotes);
        return mapping;
    }

    Preset makePreset(const juce::String& name,
                      const juce::String& group,
                      std::vector<NoteMapping> mappings)
    {
        Preset preset;
        preset.name = name;
        preset.group = group;
        preset.author = "mnKord";

        if (name == "Default")
            preset.tags = { "Empty" };
        else if (name == "C Major Diatonic")
            preset.tags = { "Diatonic", "Triads" };
        else if (name == "A Minor Diatonic")
            preset.tags = { "Diatonic", "Minor", "Triads" };
        else if (name == "Pop I-V-vi-IV")
            preset.tags = { "Pop", "Triads" };
        else if (name == "Power Chords")
            preset.tags = { "Rock", "Power chords" };
        else if (name == "C Jazz 7ths")
            preset.tags = { "Jazz", "Sevenths" };

        preset.mappings = std::move(mappings);
        return preset;
    }

    //==========================================================================
    // Chord-symbol based factory presets: each preset lists a chord symbol ("Dm9", "G/B", "Bbmaj7",
    // empty = unmapped) for the 12 keys from C4 up, and the chords are voiced by ChordSymbol.
    using ChordSymbol::ParsedChord;
    using ChordSymbol::parseChord;
    using ChordSymbol::voicedMapping;

    Preset chordPreset(const juce::String& name, const juce::String& group, const juce::StringArray& tags,
                       const std::array<const char*, 12>& slots)
    {
        std::vector<NoteMapping> mappings;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            const auto chord = parseChord(slots[i]);
            if (chord.valid)
                mappings.push_back(voicedMapping(60 + (int) i, chord));
        }

        auto preset = makePreset(name, group, std::move(mappings));
        preset.tags = tags;
        return preset;
    }

    void appendChordPresets(std::vector<Preset>& factory)
    {
        //                                       C      C#     D       D#     E      F        F#       G       G#     A       A#      B
        factory.push_back(chordPreset("Pop Four Chords", "Pop & Rock", { "Pop" },
            { "C",  "Fm",  "G",    "Eb",  "Am",  "F",     "D7",    "Em",   "Ab",  "Dm",   "Bb",   "G7" }));
        factory.push_back(chordPreset("Indie Add9", "Pop & Rock", { "Indie", "Pop" },
            { "Cadd9", "", "Dsus2", "",   "Em7", "Fmaj7", "",      "Gadd9", "",   "Am7",  "",     "Bm7b5" }));
        factory.push_back(chordPreset("Singer-Songwriter", "Pop & Rock", { "Folk", "Pop" },
            { "C",  "",    "G/B",   "",   "Am",  "F",     "",      "G",    "",    "Dm",   "",     "Csus4" }));
        factory.push_back(chordPreset("Sus Riff Rock", "Pop & Rock", { "Rock" },
            { "Csus2", "", "Dsus4", "",   "Esus4", "Fsus2", "",    "Gsus4", "",   "Asus4", "Bbsus2", "" }));
        factory.push_back(chordPreset("Emo Minor Pop", "Pop & Rock", { "Pop", "Minor" },
            { "Am", "",    "F",     "",   "C",   "G",     "",      "Dm",   "",    "Em",   "",     "E7" }));

        factory.push_back(chordPreset("Jazz Turnarounds", "Jazz & Soul", { "Jazz", "Sevenths" },
            { "Cmaj7", "Db7", "Dm7", "Eb7", "Em7", "Fmaj7", "F#m7b5", "G7", "Ab7", "Am7", "Bb7", "Bm7b5" }));
        factory.push_back(chordPreset("Neo-Soul Nines", "Jazz & Soul", { "Neo-soul", "Ninths" },
            { "Cmaj9", "", "Dm9",   "",   "Em9", "Fmaj9", "",      "G9",   "",    "Am9",  "",     "Bm7b5" }));
        factory.push_back(chordPreset("Bossa Nova", "Jazz & Soul", { "Latin", "Jazz" },
            { "Cmaj9", "", "Dm9",   "",   "Em7", "Fmaj7", "",      "G7",   "",    "Am9",  "Bb7",  "Bm7b5" }));
        factory.push_back(chordPreset("Minor ii-V-i (A)", "Jazz & Soul", { "Jazz", "Minor" },
            { "Cmaj7", "", "Dm9",   "",   "E7b9", "Fmaj7", "",     "G7",   "",    "Am9",  "",     "Bm7b5" }));
        factory.push_back(chordPreset("Gospel Sixths", "Jazz & Soul", { "Gospel", "Soul" },
            { "C6", "",    "Dm7",   "",   "Em7", "F6",     "",      "G7",   "",    "Am7",  "",     "Bdim7" }));
        factory.push_back(chordPreset("Lo-fi Chill", "Jazz & Soul", { "Lo-fi", "Chill" },
            { "Cmaj7", "", "Dm9",   "",   "Em7", "Fmaj7", "",      "G7sus4", "",  "Am9",  "",     "Bm7b5" }));
        factory.push_back(chordPreset("Funk Dominant 9ths", "Jazz & Soul", { "Funk", "Ninths" },
            { "C9", "",    "D9",    "",   "E9",  "F9",    "",      "G9",   "",    "A9",   "Bb9",  "B9" }));

        factory.push_back(chordPreset("Lydian Dream", "Cinematic", { "Cinematic", "Lydian" },
            { "Cmaj7", "", "D",     "",   "Em7", "Fmaj7", "F#m7b5", "Gmaj7", "",  "Am7",  "",     "Bm7" }));
        factory.push_back(chordPreset("Epic Trailer Minor", "Cinematic", { "Cinematic", "Minor" },
            { "Am", "",    "F",     "",   "C",   "G",     "",      "Dm",   "",    "Em",   "",     "E" }));
        factory.push_back(chordPreset("Ambient Suspensions", "Cinematic", { "Ambient", "Suspended" },
            { "Csus2", "", "Dsus4", "",   "Esus4", "Fadd9", "",    "Gsus4", "",   "Asus2", "",    "Bsus4" }));
        factory.push_back(chordPreset("Dream Six-Nine", "Cinematic", { "Ambient", "Dream pop" },
            { "C6/9", "",  "Dm9",   "",   "Em7", "Fmaj9", "",      "G6/9", "",    "Am9",  "",     "Bm7b5" }));

        factory.push_back(chordPreset("Dark Phrygian", "Minor & Dark", { "Dark", "Phrygian" },
            { "Cm", "Db",  "",      "Eb", "",    "Fm",    "",      "Gdim", "Ab",  "",     "Bbm",  "" }));
        factory.push_back(chordPreset("Harmonic Minor Drama", "Minor & Dark", { "Cinematic", "Minor" },
            { "Cm", "",    "Ddim",  "",   "Ebaug", "Fm",  "",      "G",    "",    "Ab",   "",     "Bdim" }));
        factory.push_back(chordPreset("Synthwave Minor", "Minor & Dark", { "Synthwave", "Minor" },
            { "Cm", "",    "Ab",    "",   "Eb",  "Bb",    "",      "Fm",   "",    "Gm",   "",     "G" }));
        factory.push_back(chordPreset("Andalusian Cadence", "Minor & Dark", { "Flamenco", "Minor" },
            { "Am", "",    "G",     "",   "F",   "E",     "",      "Dm",   "",    "C",    "",     "Bdim" }));

        factory.push_back(chordPreset("Quartal Stacks", "Modern", { "Modern", "Quartal" },
            { "C4th", "",  "D4th",  "",   "E4th", "F4th", "",      "G4th", "",    "A4th", "",     "B4th" }));
        factory.push_back(chordPreset("Circle of Fifths", "Modern", { "Theory", "Triads" },
            { "C",  "G",   "D",     "A",  "E",   "B",     "F#",    "Db",   "Ab",  "Eb",   "Bb",   "F" }));

        factory.push_back(chordPreset("Chromatic Major Triads", "Utility", { "Triads", "Utility" },
            { "C",  "Db",  "D",     "Eb", "E",   "F",     "F#",    "G",    "Ab",  "A",    "Bb",   "B" }));
        factory.push_back(chordPreset("Chromatic Minor Triads", "Utility", { "Triads", "Minor", "Utility" },
            { "Cm", "C#m", "Dm",    "Ebm", "Em", "Fm",    "F#m",   "Gm",   "G#m", "Am",   "Bbm",  "Bm" }));
        factory.push_back(chordPreset("Chromatic Sevenths", "Utility", { "Sevenths", "Utility" },
            { "Cmaj7", "Db7", "Dm7", "Eb7", "Em7", "Fmaj7", "F#m7b5", "G7", "Abmaj7", "Am7", "Bbmaj7", "Bm7b5" }));
    }
}

//==============================================================================
// No audio buses: this is a MIDI effect. The default AudioProcessor() constructor adds a stereo
// in/out pair, which makes hosts treat the plugin as an audio effect.
MnKordAudioProcessor::MnKordAudioProcessor()
    : AudioProcessor(BusesProperties())
{
    presets = buildLibrary();
    loadFavorites();
    activePresetIndex = presets.empty() ? -1 : 0;
}

MnKordAudioProcessor::~MnKordAudioProcessor()
{
}

//==============================================================================
const juce::String MnKordAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MnKordAudioProcessor::acceptsMidi() const
{
    return true;
}

bool MnKordAudioProcessor::producesMidi() const
{
    return true;
}

bool MnKordAudioProcessor::isMidiEffect() const
{
    return true;
}

double MnKordAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

//==============================================================================
int MnKordAudioProcessor::getNumPrograms()
{
    const juce::ScopedLock lock(stateLock);
    return juce::jmax(1, (int) presets.size());
}

int MnKordAudioProcessor::getCurrentProgram()
{
    const juce::ScopedLock lock(stateLock);
    return juce::jmax(0, activePresetIndex);
}

void MnKordAudioProcessor::setCurrentProgram(int index)
{
    setActivePresetIndex(index);
}

const juce::String MnKordAudioProcessor::getProgramName(int index)
{
    const juce::ScopedLock lock(stateLock);
    if (index >= 0 && index < (int) presets.size())
        return presets[(size_t) index].name;

    return {};
}

void MnKordAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    const juce::ScopedLock lock(stateLock);

    // Factory presets are read only; renaming a user preset leaves it with unsaved changes.
    if (index >= 0 && index < (int) presets.size() && ! presets[(size_t) index].isFactory)
        presets[(size_t) index].name = newName;
}

//==============================================================================
void MnKordAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void MnKordAudioProcessor::releaseResources()
{
}

void MnKordAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(buffer);

    juce::MidiBuffer outputMessages;

    // Hold the state lock for the whole block so the audio thread never races with
    // GUI-thread preset edits or the clear/note-off flush.
    const juce::ScopedLock lock(stateLock);

    flushPendingNoteOffs(outputMessages);

    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();
        const auto samplePosition = metadata.samplePosition;
        const auto channel = message.getChannel();

        if (message.isNoteOn())
        {
            lastNoteOnStamp.store((((lastNoteOnStamp.load(std::memory_order_relaxed) >> 8) + 1) << 8)
                                      | (std::uint32_t) message.getNoteNumber(),
                                  std::memory_order_relaxed);
            handleNoteOn(channel, message.getNoteNumber(), samplePosition, outputMessages);
        }
        else if (message.isNoteOff())
        {
            handleNoteOff(channel, message.getNoteNumber(), samplePosition, outputMessages);
        }
        else if (message.isSustainPedalOn())
        {
            handleSustain(true, samplePosition, outputMessages);
            outputMessages.addEvent(message, samplePosition);
        }
        else if (message.isSustainPedalOff())
        {
            handleSustain(false, samplePosition, outputMessages);
            outputMessages.addEvent(message, samplePosition);
        }
        else
        {
            // CC, pitch bend, aftertouch, clock, etc. pass through unchanged.
            outputMessages.addEvent(message, samplePosition);
        }
    }

    publishActiveNotes();
    midiMessages.swapWith(outputMessages);
}

void MnKordAudioProcessor::publishActiveNotes()
{
    NoteBits input, output;

    for (const auto& kv : activeNotes)
        input.add(kv.first.second);

    for (const auto& kv : outputNoteRefCount)
        output.add(kv.first.second);

    for (const auto& key : pendingNoteOffs)
        output.add(key.second);

    for (size_t i = 0; i < input.words.size(); ++i)
    {
        activeInputWords[i].store(input.words[i], std::memory_order_relaxed);
        activeOutputWords[i].store(output.words[i], std::memory_order_relaxed);
    }
}

//==============================================================================
void MnKordAudioProcessor::handleNoteOn(int channel, int note, int samplePosition, juce::MidiBuffer& output)
{
    if (activePresetIndex < 0 || activePresetIndex >= (int) presets.size())
        return;

    const auto& preset = presets[activePresetIndex];

    // Find the mapping for this input note
    const NoteMapping* mapping = nullptr;
    for (const auto& m : preset.mappings)
    {
        if (m.inputNote == note)
        {
            mapping = &m;
            break;
        }
    }

    if (mapping == nullptr)
        return; // Unmapped note is dropped

    const NoteKey inputKey { channel, note };
    if (activeNotes.find(inputKey) != activeNotes.end())
        handleNoteOff(channel, note, samplePosition, output);

    // Store the active mapping
    activeNotes[inputKey] = mapping->outputNotes;

    // Send note-ons for the output notes
    for (int outNote : mapping->outputNotes)
    {
        if (outNote < 0 || outNote > 127)
            continue;

        const NoteKey outputKey { channel, outNote };
        int& refCount = outputNoteRefCount[outputKey];
        if (refCount == 0)
        {
            // Remove from pending note-offs if it was sustained
            pendingNoteOffs.erase(std::remove(pendingNoteOffs.begin(), pendingNoteOffs.end(), outputKey),
                                  pendingNoteOffs.end());

            const auto velocity = juce::jlimit(1, 127, mapping->velocityFor(outNote));
            output.addEvent(juce::MidiMessage::noteOn(channel, outNote, (juce::uint8) velocity), samplePosition);
        }
        refCount++;
    }
}

void MnKordAudioProcessor::handleNoteOff(int channel, int note, int samplePosition, juce::MidiBuffer& output)
{
    const NoteKey inputKey { channel, note };
    auto it = activeNotes.find(inputKey);
    if (it == activeNotes.end())
        return;

    for (int outNote : it->second)
    {
        const NoteKey outputKey { channel, outNote };
        auto refIt = outputNoteRefCount.find(outputKey);
        if (refIt == outputNoteRefCount.end())
            continue;

        refIt->second--;
        if (refIt->second <= 0)
        {
            outputNoteRefCount.erase(refIt);

            if (sustainPedalDown)
            {
                pendingNoteOffs.push_back(outputKey);
            }
            else
            {
                output.addEvent(juce::MidiMessage::noteOff(channel, outNote), samplePosition);
            }
        }
    }

    activeNotes.erase(it);
}

void MnKordAudioProcessor::handleSustain(bool down, int samplePosition, juce::MidiBuffer& output)
{
    sustainPedalDown = down;

    if (!down)
    {
        // Release all pending note-offs
        for (const auto& outputKey : pendingNoteOffs)
            output.addEvent(juce::MidiMessage::noteOff(outputKey.first, outputKey.second), samplePosition);

        pendingNoteOffs.clear();
    }
}

void MnKordAudioProcessor::flushPendingNoteOffs(juce::MidiBuffer& output)
{
    const juce::ScopedLock lock(flushLock);

    for (const auto& key : notesToFlush)
        output.addEvent(juce::MidiMessage::noteOff(key.first, key.second), 0);

    notesToFlush.clear();
}

void MnKordAudioProcessor::clearActiveNotes()
{
    // Queue note-offs for any output notes that are still sounding, so the DAW
    // does not end up with stuck/hanging notes when a preset is switched or deleted.
    {
        const juce::ScopedLock lock(flushLock);

        for (const auto& kv : activeNotes)
            for (const auto note : kv.second)
                if (note >= 0 && note < 128)
                    notesToFlush.emplace_back(kv.first.first, note);

        for (const auto& key : pendingNoteOffs)
            notesToFlush.push_back(key);
    }

    activeNotes.clear();
    outputNoteRefCount.clear();
    pendingNoteOffs.clear();
    sustainPedalDown = false;
    publishActiveNotes();
}

//==============================================================================
// Library folders
namespace
{
    juce::File& libraryFolderOverride()
    {
        static juce::File folder;
        return folder;
    }
}

void MnKordAudioProcessor::overrideLibraryFolder(const juce::File& folder)
{
    libraryFolderOverride() = folder;
}

juce::File MnKordAudioProcessor::getLibraryFolder()
{
    if (libraryFolderOverride() != juce::File())
        return libraryFolderOverride();

    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("MNAudio").getChildFile("mnKord");
}

juce::File MnKordAudioProcessor::getFactoryFolder() { return getLibraryFolder().getChildFile("Factory"); }
juce::File MnKordAudioProcessor::getUserFolder()    { return getLibraryFolder().getChildFile("User"); }

juce::String MnKordAudioProcessor::fingerprint(const Preset& preset)
{
    return presetToXml(preset)->toString(juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

bool MnKordAudioProcessor::writePresetFile(const Preset& preset, const juce::File& file, bool factoryMirror)
{
    if (! file.getParentDirectory().createDirectory().wasOk())
        return false;

    juce::XmlElement root("mnKordPreset");
    root.setAttribute("version", 1);
    if (factoryMirror)
        root.setAttribute("factory", 1);   // marks files the plugin generated, so stale ones can be cleaned up
    root.addChildElement(presetToXml(preset).release());
    return root.writeTo(file);
}

bool MnKordAudioProcessor::loadPresetFile(const juce::File& file, Preset& result)
{
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return false;

    auto loaded = presetsFromXmlRoot(*xml);
    if (loaded.size() != 1)
        return false;

    result = std::move(loaded.front());
    result.isFactory = false;
    result.file = file;
    result.savedXml = fingerprint(result);
    return true;
}

juce::File MnKordAudioProcessor::uniqueUserFile(const juce::String& presetName)
{
    auto legal = juce::File::createLegalFileName(presetName.trim());
    if (legal.isEmpty())
        legal = "Preset";

    const auto folder = getUserFolder();
    auto file = folder.getChildFile(legal + ".xml");

    for (int n = 2; file.exists(); ++n)
        file = folder.getChildFile(legal + " " + juce::String(n) + ".xml");

    return file;
}

Preset MnKordAudioProcessor::saveAsNewUserPreset(const Preset& preset, bool& written)
{
    Preset result = preset;
    result.isFactory = false;
    result.file = {};
    result.savedXml = {};

    const auto file = uniqueUserFile(result.name);
    written = writePresetFile(result, file, false);

    if (written)
    {
        result.file = file;
        result.savedXml = fingerprint(result);
    }

    return result;
}

std::vector<Preset> MnKordAudioProcessor::buildFactoryPresets()
{
    auto factory = createFactoryPresets();
    for (auto& preset : factory)
    {
        preset.isFactory = true;
        preset.file = getFactoryFolder().getChildFile(juce::File::createLegalFileName(preset.name) + ".xml");
        preset.savedXml = fingerprint(preset);
    }
    return factory;
}

void MnKordAudioProcessor::syncFactoryFolder(const std::vector<Preset>& factory)
{
    const auto folder = getFactoryFolder();
    if (! folder.createDirectory().wasOk())
        return;

    std::vector<juce::File> current;
    for (const auto& preset : factory)
    {
        current.push_back(preset.file);

        // Only touch the file when its content differs, so a plain start doesn't rewrite it.
        juce::XmlElement root("mnKordPreset");
        root.setAttribute("version", 1);
        root.setAttribute("factory", 1);
        root.addChildElement(presetToXml(preset).release());

        const auto text = root.toString();
        if (! preset.file.existsAsFile() || preset.file.loadFileAsString() != text)
            preset.file.replaceWithText(text);
    }

    // Remove files this plugin generated earlier for presets that no longer exist. Anything
    // without the marker is somebody else's file and stays.
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.xml"))
    {
        if (std::find(current.begin(), current.end(), file) != current.end())
            continue;

        const auto xml = juce::XmlDocument::parse(file);
        if (xml != nullptr && xml->getIntAttribute("factory") == 1)
            file.deleteFile();
    }
}

std::vector<Preset> MnKordAudioProcessor::loadUserPresets()
{
    std::vector<Preset> user;

    const auto folder = getUserFolder();
    folder.createDirectory();   // so the folder exists to drop presets into

    auto files = folder.findChildFiles(juce::File::findFiles, true, "*.xml");
    files.sort();

    for (const auto& file : files)
    {
        Preset preset;
        if (loadPresetFile(file, preset))
            user.push_back(std::move(preset));
    }

    std::stable_sort(user.begin(), user.end(), [](const Preset& a, const Preset& b)
    {
        return a.name.compareIgnoreCase(b.name) < 0;
    });

    return user;
}

std::vector<Preset> MnKordAudioProcessor::buildLibrary()
{
    auto library = buildFactoryPresets();
    syncFactoryFolder(library);

    auto user = loadUserPresets();
    library.insert(library.end(), std::make_move_iterator(user.begin()), std::make_move_iterator(user.end()));
    return library;
}

//==============================================================================
bool MnKordAudioProcessor::isPresetDirty(int index) const
{
    const juce::ScopedLock lock(stateLock);

    if (index < 0 || index >= (int) presets.size())
        return false;

    const auto& preset = presets[(size_t) index];
    return preset.savedXml.isEmpty() || fingerprint(preset) != preset.savedXml;
}

bool MnKordAudioProcessor::canRevertPreset(int index) const
{
    const juce::ScopedLock lock(stateLock);

    if (index < 0 || index >= (int) presets.size())
        return false;

    const auto& preset = presets[(size_t) index];
    return preset.isFactory || preset.file.existsAsFile();
}

bool MnKordAudioProcessor::revertPreset(int index)
{
    bool isFactory = false;
    juce::String name;
    juce::File file;

    {
        const juce::ScopedLock lock(stateLock);
        if (index < 0 || index >= (int) presets.size())
            return false;

        isFactory = presets[(size_t) index].isFactory;
        name = presets[(size_t) index].name;
        file = presets[(size_t) index].file;
    }

    // Read outside the lock, so the audio thread never waits on the disk.
    Preset fresh;
    if (isFactory)
    {
        bool found = false;
        for (auto& candidate : buildFactoryPresets())
        {
            if (candidate.name == name)
            {
                fresh = std::move(candidate);
                found = true;
                break;
            }
        }

        if (! found)
            return false;
    }
    else if (! loadPresetFile(file, fresh))
    {
        return false;
    }

    const juce::ScopedLock lock(stateLock);
    if (index >= (int) presets.size() || presets[(size_t) index].isFactory != isFactory || presets[(size_t) index].file != file)
        return false;

    auto& preset = presets[(size_t) index];
    preset.name = fresh.name;
    preset.group = fresh.group;
    preset.author = fresh.author;
    preset.tags = fresh.tags;
    preset.mappings = std::move(fresh.mappings);
    preset.savedXml = fingerprint(preset);

    if (index == activePresetIndex)
        clearActiveNotes();

    return true;
}

bool MnKordAudioProcessor::saveUserPreset(int index)
{
    Preset copy;

    {
        const juce::ScopedLock lock(stateLock);
        if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].isFactory)
            return false;

        copy = presets[(size_t) index];
    }

    const auto file = copy.file != juce::File() ? copy.file : uniqueUserFile(copy.name);
    if (! writePresetFile(copy, file, false))
        return false;

    const juce::ScopedLock lock(stateLock);
    if (index < (int) presets.size() && ! presets[(size_t) index].isFactory && presets[(size_t) index].name == copy.name)
    {
        presets[(size_t) index].file = file;
        // The content that was written, so edits made while saving still show as unsaved.
        presets[(size_t) index].savedXml = fingerprint(copy);
    }

    return true;
}

juce::String MnKordAudioProcessor::favoriteKeyFor(const Preset& preset) const
{
    if (preset.isFactory)
        return "F|" + preset.name;

    return preset.file == juce::File() ? juce::String()
                                       : "U|" + preset.file.getRelativePathFrom(getUserFolder());
}

void MnKordAudioProcessor::loadFavorites()
{
    favoriteKeys.clear();
    favoriteKeys.addLines(getLibraryFolder().getChildFile("favorites.txt").loadFileAsString());
    favoriteKeys.removeEmptyStrings();
}

bool MnKordAudioProcessor::isFavoritePreset(const Preset& preset) const
{
    const auto key = favoriteKeyFor(preset);
    return key.isNotEmpty() && favoriteKeys.contains(key);
}

void MnKordAudioProcessor::setFavoritePreset(const Preset& preset, bool shouldBeFavorite)
{
    const auto key = favoriteKeyFor(preset);
    if (key.isEmpty() || favoriteKeys.contains(key) == shouldBeFavorite)
        return;

    if (shouldBeFavorite)
        favoriteKeys.add(key);
    else
        favoriteKeys.removeString(key);

    const auto file = getLibraryFolder().getChildFile("favorites.txt");
    file.getParentDirectory().createDirectory();
    file.replaceWithText(favoriteKeys.joinIntoString("\n"));
}

void MnKordAudioProcessor::refreshUserLibrary()
{
    loadFavorites();
    auto onDisk = loadUserPresets();

    const juce::ScopedLock lock(stateLock);

    for (auto& candidate : onDisk)
    {
        const bool known = std::any_of(presets.begin(), presets.end(),
                                       [&candidate](const Preset& p) { return ! p.isFactory && p.file == candidate.file; });
        if (! known)
            presets.push_back(std::move(candidate));
    }

    // Drop user presets whose file was removed, unless they're the active one or have unsaved
    // changes -- those stay as the working copy.
    for (int i = (int) presets.size() - 1; i >= 0; --i)
    {
        const auto& preset = presets[(size_t) i];
        if (preset.isFactory || preset.file == juce::File() || preset.file.existsAsFile() || i == activePresetIndex)
            continue;

        if (fingerprint(preset) != preset.savedXml)
            continue;

        presets.erase(presets.begin() + i);
        if (i < activePresetIndex)
            --activePresetIndex;
    }
}

//==============================================================================
void MnKordAudioProcessor::setActivePresetIndex(int index)
{
    int previous = -1;
    {
        const juce::ScopedLock lock(stateLock);
        previous = activePresetIndex;
    }

    // Leaving a preset with unsaved changes discards them (the editor has asked by now; a host
    // program change just does it), so at most the active preset ever holds unsaved edits.
    if (previous != index && isPresetDirty(previous) && canRevertPreset(previous))
        revertPreset(previous);

    const juce::ScopedLock lock(stateLock);

    if (index >= 0 && index < (int) presets.size())
        activePresetIndex = index;
    else
        activePresetIndex = -1;

    clearActiveNotes();
}

bool MnKordAudioProcessor::addPreset(const Preset& preset)
{
    int previous = -1;
    {
        const juce::ScopedLock lock(stateLock);
        previous = activePresetIndex;
    }

    // The new preset is a copy of what was on screen; the one it was copied from goes back to
    // its saved state, as with any "save as".
    if (isPresetDirty(previous) && canRevertPreset(previous))
        revertPreset(previous);

    bool written = false;
    auto saved = saveAsNewUserPreset(preset, written);

    const juce::ScopedLock lock(stateLock);
    presets.push_back(std::move(saved));
    activePresetIndex = (int) presets.size() - 1;
    clearActiveNotes();
    return written;
}

bool MnKordAudioProcessor::deletePreset(int index)
{
    juce::File file;

    {
        const juce::ScopedLock lock(stateLock);
        if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].isFactory)
            return false;

        file = presets[(size_t) index].file;
    }

    // Recoverable: the file goes to the recycle bin rather than being deleted outright.
    if (file != juce::File() && file.exists() && ! file.moveToTrash())
        return false;

    const juce::ScopedLock lock(stateLock);
    if (index >= (int) presets.size() || presets[(size_t) index].isFactory || presets[(size_t) index].file != file)
        return false;

    presets.erase(presets.begin() + index);

    if (index < activePresetIndex)
        --activePresetIndex;
    else if (index == activePresetIndex)
        activePresetIndex = juce::jmin(index, (int) presets.size() - 1);

    clearActiveNotes();
    return true;
}

//==============================================================================
juce::AudioProcessorEditor* MnKordAudioProcessor::createEditor()
{
    return new MnKordAudioProcessorEditor(*this);
}

bool MnKordAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
// Project/DAW state holds the active preset's working copy, not the preset list: the library
// lives in the Factory/User folders, and a project must replay the same even if those change.
void MnKordAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock(stateLock);

    juce::XmlElement root("mnKordState");
    root.setAttribute("version", 2);

    if (activePresetIndex >= 0 && activePresetIndex < (int) presets.size())
    {
        const auto& active = presets[(size_t) activePresetIndex];
        root.setAttribute("activeFactory", active.isFactory);
        if (! active.isFactory && active.file != juce::File())
            root.setAttribute("activeFile", active.file.getRelativePathFrom(getUserFolder()));

        root.addChildElement(presetToXml(active).release());
    }

    juce::String xmlString = root.toString();
    destData.append(xmlString.toRawUTF8(), xmlString.getNumBytesAsUTF8());
}

void MnKordAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::String xmlString(static_cast<const char*> (data), sizeInBytes);
    auto root = juce::XmlDocument::parse(xmlString);
    if (root == nullptr)
        return;

    // All file I/O happens here, before the lock; the result is swapped in at the end.
    auto library = buildLibrary();
    int active = library.empty() ? -1 : 0;

    if (auto* presetXml = root->getChildByName("Preset"))
    {
        Preset working = presetFromXml(*presetXml);
        const bool wasFactory = root->getBoolAttribute("activeFactory");
        const auto activeFile = root->getStringAttribute("activeFile");

        int match = -1;
        for (int i = 0; i < (int) library.size() && match < 0; ++i)
        {
            const auto& candidate = library[(size_t) i];
            if (candidate.isFactory != wasFactory)
                continue;

            if (wasFactory ? candidate.name == working.name
                           : (activeFile.isNotEmpty() ? candidate.file == getUserFolder().getChildFile(activeFile)
                                                      : (candidate.file == juce::File() && candidate.name == working.name)))
                match = i;
        }

        if (match >= 0)
        {
            // The project's working copy wins over the library's: if they differ, the preset is
            // restored with those edits still unsaved.
            auto& entry = library[(size_t) match];

            // Projects saved before style tags existed carry none; that isn't an edit.
            if (! presetXml->hasAttribute("tags"))
                working.tags = entry.tags;

            if (fingerprint(working) != fingerprint(entry))
            {
                entry.name = working.name;
                entry.group = working.group;
                entry.author = working.author;
                entry.tags = working.tags;
                entry.mappings = std::move(working.mappings);
            }

            active = match;
        }
        else
        {
            // Not in this machine's library (or it was deleted): keep it playable as an unsaved
            // user preset, which "Save" will then write to the User folder.
            working.isFactory = false;
            working.file = {};
            working.savedXml = {};
            library.push_back(std::move(working));
            active = (int) library.size() - 1;
        }
    }
    else if (auto* legacyPresets = root->getChildByName("Presets"))
    {
        // Version 1 state stored the whole preset list. Keep the user's own presets by moving
        // them into the User folder; built-in ones are already in the library.
        const auto old = presetsFromXmlRoot(*root);
        const int oldActive = root->getIntAttribute("activePreset", 0);
        const juce::String activeName = oldActive >= 0 && oldActive < (int) old.size() ? old[(size_t) oldActive].name : juce::String();
        juce::ignoreUnused(legacyPresets);

        for (const auto& preset : old)
        {
            const bool builtIn = std::any_of(library.begin(), library.end(),
                                             [&preset](const Preset& p) { return p.isFactory && p.name == preset.name; });
            const bool alreadyMigrated = std::any_of(library.begin(), library.end(),
                                                     [&preset](const Preset& p) { return ! p.isFactory && p.name == preset.name
                                                                                                && fingerprint(p) == fingerprint(preset); });
            if (builtIn || alreadyMigrated)
                continue;

            bool written = false;
            library.push_back(saveAsNewUserPreset(preset, written));
        }

        for (int i = 0; i < (int) library.size(); ++i)
        {
            if (library[(size_t) i].name == activeName)
            {
                active = i;
                break;
            }
        }
    }

    const juce::ScopedLock lock(stateLock);
    presets = std::move(library);
    activePresetIndex = active;
    clearActiveNotes();
}

//==============================================================================
std::unique_ptr<juce::XmlElement> MnKordAudioProcessor::presetToXml(const Preset& preset)
{
    auto xml = std::make_unique<juce::XmlElement>("Preset");
    xml->setAttribute("name", preset.name);
    xml->setAttribute("group", preset.group);
    xml->setAttribute("author", preset.author);
    if (preset.tags.size() > 0)
        xml->setAttribute("tags", preset.tags.joinIntoString(","));

    for (const auto& mapping : preset.mappings)
    {
        auto* mappingXml = new juce::XmlElement("Mapping");
        mappingXml->setAttribute("input", mapping.inputNote);

        juce::StringArray outputs, velocities;
        bool anyCustomVelocity = false;

        for (const auto note : mapping.outputNotes)
        {
            outputs.add(juce::String(note));
            velocities.add(juce::String(mapping.velocityFor(note)));
            anyCustomVelocity |= mapping.velocityFor(note) != NoteMapping::defaultOutputVelocity;
        }

        mappingXml->setAttribute("output", outputs.joinIntoString(" "));

        // Parallel to "output"; omitted when every note uses the default velocity.
        if (anyCustomVelocity)
            mappingXml->setAttribute("velocity", velocities.joinIntoString(" "));

        xml->addChildElement(mappingXml);
    }

    return xml;
}

Preset MnKordAudioProcessor::presetFromXml(const juce::XmlElement& xml)
{
    Preset preset;
    preset.name = xml.getStringAttribute("name");
    preset.group = xml.getStringAttribute("group");
    preset.author = xml.getStringAttribute("author");
    preset.tags.addTokens(xml.getStringAttribute("tags"), ",;", "");
    preset.tags.trim();
    preset.tags.removeEmptyStrings();
    preset.tags.removeDuplicates(true);

    for (auto* mappingXml : xml.getChildIterator())
    {
        if (! mappingXml->hasTagName("Mapping"))
            continue;

        NoteMapping mapping;
        mapping.inputNote = mappingXml->getIntAttribute("input", -1);

        juce::StringArray tokens;
        tokens.addTokens(mappingXml->getStringAttribute("output"), " ", {});
        tokens.removeEmptyStrings();
        for (const auto& token : tokens)
            mapping.outputNotes.push_back(token.getIntValue());

        juce::StringArray velocityTokens;
        velocityTokens.addTokens(mappingXml->getStringAttribute("velocity"), " ", {});
        velocityTokens.removeEmptyStrings();
        for (int i = 0; i < juce::jmin(velocityTokens.size(), (int) mapping.outputNotes.size()); ++i)
            mapping.outputVelocities[mapping.outputNotes[(size_t) i]] = juce::jlimit(1, 127, velocityTokens[i].getIntValue());

        preset.mappings.push_back(std::move(mapping));
    }

    return preset;
}

std::vector<Preset> MnKordAudioProcessor::presetsFromXmlRoot(const juce::XmlElement& root)
{
    std::vector<Preset> loaded;

    auto appendPreset = [&loaded](const juce::XmlElement& xml)
    {
        auto preset = presetFromXml(xml);
        if (preset.name.isNotEmpty() || ! preset.mappings.empty())
            loaded.push_back(std::move(preset));
    };

    if (root.hasTagName("Preset"))
    {
        appendPreset(root);
        return loaded;
    }

    if (auto* presetsXml = root.getChildByName("Presets"))
    {
        for (auto* child : presetsXml->getChildIterator())
            if (child->hasTagName("Preset"))
                appendPreset(*child);
        return loaded;
    }

    for (auto* child : root.getChildIterator())
        if (child->hasTagName("Preset"))
            appendPreset(*child);

    return loaded;
}

int MnKordAudioProcessor::importPresetsFromFile(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return 0;

    auto loaded = presetsFromXmlRoot(*xml);
    if (loaded.empty())
        return 0;

    int previous = -1;
    {
        const juce::ScopedLock lock(stateLock);
        previous = activePresetIndex;
    }

    // Leaving the active preset for the imported one, like any other switch.
    if (isPresetDirty(previous) && canRevertPreset(previous))
        revertPreset(previous);

    // Every imported preset becomes its own file in the User folder.
    std::vector<Preset> imported;
    for (const auto& preset : loaded)
    {
        bool written = false;
        imported.push_back(saveAsNewUserPreset(preset, written));
    }

    const int added = (int) imported.size();

    const juce::ScopedLock lock(stateLock);
    for (auto& preset : imported)
        presets.push_back(std::move(preset));

    activePresetIndex = (int) presets.size() - 1;
    clearActiveNotes();
    return added;
}

bool MnKordAudioProcessor::exportPresetToFile(int index, const juce::File& file) const
{
    Preset copy;
    {
        const juce::ScopedLock lock(stateLock);
        if (index < 0 || index >= (int) presets.size())
            return false;
        copy = presets[(size_t) index];
    }

    juce::XmlElement root("mnKordPreset");
    root.setAttribute("version", 1);
    root.addChildElement(presetToXml(copy).release());
    return root.writeTo(file);
}

std::vector<Preset> MnKordAudioProcessor::createFactoryPresets()
{
    std::vector<Preset> factory;

    factory.push_back(makePreset("Default", "Factory", {
        makeMapping(60, { 60, 64, 67 }),  // C4 -> Cmaj
        makeMapping(62, { 62, 65, 69 }),  // D4 -> Dmin
        makeMapping(64, { 57, 64, 67 })   // E4 -> Amin/E
    }));

    factory.push_back(makePreset("C Major Diatonic", "Factory", {
        makeMapping(60, { 60, 64, 67 }),  // C
        makeMapping(62, { 62, 65, 69 }),  // Dm
        makeMapping(64, { 64, 67, 71 }),  // Em
        makeMapping(65, { 65, 69, 72 }),  // F
        makeMapping(67, { 67, 71, 74 }),  // G
        makeMapping(69, { 69, 72, 76 }),  // Am
        makeMapping(71, { 71, 74, 77 })   // Bdim
    }));

    factory.push_back(makePreset("A Minor Diatonic", "Factory", {
        makeMapping(57, { 57, 60, 64 }),  // Am
        makeMapping(59, { 59, 62, 65 }),  // Bdim
        makeMapping(60, { 60, 64, 67 }),  // C
        makeMapping(62, { 62, 65, 69 }),  // Dm
        makeMapping(64, { 64, 67, 71 }),  // Em
        makeMapping(65, { 65, 69, 72 }),  // F
        makeMapping(67, { 67, 71, 74 })   // G
    }));

    factory.push_back(makePreset("Pop I-V-vi-IV", "Factory", {
        makeMapping(60, { 60, 64, 67 }),  // I  C
        makeMapping(62, { 67, 71, 74 }),  // V  G
        makeMapping(64, { 57, 60, 64 }),  // vi Am
        makeMapping(65, { 65, 69, 72 })   // IV F
    }));

    {
        std::vector<NoteMapping> power;
        for (int note = 48; note <= 59; ++note)  // C3-B3: root + 5th + octave
            power.push_back(makeMapping(note, { note, note + 7, note + 12 }));
        factory.push_back(makePreset("Power Chords", "Factory", std::move(power)));
    }

    factory.push_back(makePreset("C Jazz 7ths", "Factory", {
        makeMapping(60, { 60, 64, 67, 71 }),  // Cmaj7
        makeMapping(62, { 62, 65, 69, 72 }),  // Dm7
        makeMapping(64, { 64, 67, 71, 74 }),  // Em7
        makeMapping(65, { 65, 69, 72, 76 }),  // Fmaj7
        makeMapping(67, { 67, 71, 74, 77 }),  // G7
        makeMapping(69, { 69, 72, 76, 79 }),  // Am7
        makeMapping(71, { 71, 74, 77, 81 })   // Bm7b5
    }));

    appendChordPresets(factory);

    return factory;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MnKordAudioProcessor();
}
