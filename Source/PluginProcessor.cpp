#include "PluginProcessor.h"
#include "PluginEditor.h"

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
        preset.mappings = std::move(mappings);
        return preset;
    }
}

//==============================================================================
MnKordAudioProcessor::MnKordAudioProcessor()
{
    presets = createFactoryPresets();
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
    if (index >= 0 && index < (int) presets.size())
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
            handleNoteOn(channel, message.getNoteNumber(), message.getVelocity(), samplePosition, outputMessages);
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

    midiMessages.swapWith(outputMessages);
}

//==============================================================================
void MnKordAudioProcessor::handleNoteOn(int channel, int note, int velocity, int samplePosition, juce::MidiBuffer& output)
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
}

//==============================================================================
void MnKordAudioProcessor::setActivePresetIndex(int index)
{
    const juce::ScopedLock lock(stateLock);

    if (index >= 0 && index < (int) presets.size())
        activePresetIndex = index;
    else
        activePresetIndex = -1;

    clearActiveNotes();
}

void MnKordAudioProcessor::addPreset(const Preset& preset)
{
    const juce::ScopedLock lock(stateLock);

    presets.push_back(preset);
    activePresetIndex = (int) presets.size() - 1;
    clearActiveNotes();
}

void MnKordAudioProcessor::deletePreset(int index)
{
    const juce::ScopedLock lock(stateLock);

    if (index < 0 || index >= (int) presets.size())
        return;

    presets.erase(presets.begin() + index);

    if (index < activePresetIndex)
        --activePresetIndex;

    if (activePresetIndex >= (int) presets.size())
        activePresetIndex = (int) presets.size() - 1;

    clearActiveNotes();
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
void MnKordAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock(stateLock);

    juce::XmlElement root("mnKordState");
    root.setAttribute("version", 1);
    root.setAttribute("activePreset", activePresetIndex);

    auto* presetsXml = new juce::XmlElement("Presets");
    for (const auto& preset : presets)
        presetsXml->addChildElement(presetToXml(preset).release());

    root.addChildElement(presetsXml);

    juce::String xmlString = root.toString();
    destData.append(xmlString.toRawUTF8(), xmlString.getNumBytesAsUTF8());
}

void MnKordAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::String xmlString(static_cast<const char*> (data), sizeInBytes);
    auto root = juce::XmlDocument::parse(xmlString);
    if (root == nullptr)
        return;

    auto loaded = presetsFromXmlRoot(*root);

    const juce::ScopedLock lock(stateLock);
    presets = std::move(loaded);
    activePresetIndex = root->getIntAttribute("activePreset", presets.empty() ? -1 : 0);
    if (activePresetIndex >= (int) presets.size())
        activePresetIndex = presets.empty() ? -1 : 0;

    clearActiveNotes();
}

//==============================================================================
std::unique_ptr<juce::XmlElement> MnKordAudioProcessor::presetToXml(const Preset& preset)
{
    auto xml = std::make_unique<juce::XmlElement>("Preset");
    xml->setAttribute("name", preset.name);
    xml->setAttribute("group", preset.group);
    xml->setAttribute("author", preset.author);

    for (const auto& mapping : preset.mappings)
    {
        auto* mappingXml = new juce::XmlElement("Mapping");
        mappingXml->setAttribute("input", mapping.inputNote);

        juce::String outputStr;
        for (int i = 0; i < (int) mapping.outputNotes.size(); ++i)
        {
            if (i > 0)
                outputStr += " ";
            outputStr += juce::String(mapping.outputNotes[i]);
        }
        mappingXml->setAttribute("output", outputStr);
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

    for (auto* mappingXml : xml.getChildIterator())
    {
        if (! mappingXml->hasTagName("Mapping"))
            continue;

        NoteMapping mapping;
        mapping.inputNote = mappingXml->getIntAttribute("input", -1);

        juce::StringArray tokens;
        tokens.addTokens(mappingXml->getStringAttribute("output"), " ", {});
        for (const auto& token : tokens)
            if (token.isNotEmpty())
                mapping.outputNotes.push_back(token.getIntValue());

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

    const int added = (int) loaded.size();

    const juce::ScopedLock lock(stateLock);
    for (auto& preset : loaded)
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

bool MnKordAudioProcessor::exportBankToFile(const juce::File& file) const
{
    std::vector<Preset> copy;
    {
        const juce::ScopedLock lock(stateLock);
        copy = presets;
    }

    juce::XmlElement root("mnKordBank");
    root.setAttribute("version", 1);
    for (const auto& preset : copy)
        root.addChildElement(presetToXml(preset).release());

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

    return factory;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MnKordAudioProcessor();
}
