#pragma once
#include <JuceHeader.h>

//==============================================================================
struct NoteMapping
{
    int inputNote = -1;
    std::vector<int> outputNotes;
};

struct Preset
{
    juce::String name;
    juce::String group;
    juce::String author;
    std::vector<NoteMapping> mappings;
};

//==============================================================================
class MnKordAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    MnKordAudioProcessor();
    ~MnKordAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;

    double getTailLengthSeconds() const override;

    //==============================================================================
    // Host program list mirrors the user preset list.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Preset management
    std::vector<Preset>& getPresets() { return presets; }
    const std::vector<Preset>& getPresets() const { return presets; }
    int getActivePresetIndex() const { return activePresetIndex; }
    void setActivePresetIndex(int index);
    void addPreset(const Preset& preset);
    void deletePreset(int index);

    // Guards presets/mappings across the audio and GUI threads. The editor must hold this
    // while structurally mutating presets/mappings obtained via getPresets().
    juce::CriticalSection& getStateLock() { return stateLock; }

    // File I/O. Import appends every recognised preset and selects the last one.
    // Returns how many presets were added (0 = invalid/empty file).
    int importPresetsFromFile(const juce::File& file);
    bool exportPresetToFile(int index, const juce::File& file) const;

    // A set of MIDI note numbers (0-127) as two 64-bit words.
    struct NoteBits
    {
        std::array<std::uint64_t, 2> words {};

        void add(int note)
        {
            if (note >= 0 && note < 128)
                words[(size_t) (note >> 6)] |= std::uint64_t(1) << (note & 63);
        }

        bool contains(int note) const
        {
            return note >= 0 && note < 128 && ((words[(size_t) (note >> 6)] >> (note & 63)) & 1u) != 0;
        }

        bool operator==(const NoteBits& other) const { return words == other.words; }
        bool operator!=(const NoteBits& other) const { return words != other.words; }
    };

    // INPUT notes currently held and OUTPUT notes currently sounding (any channel; notes held
    // by the sustain pedal count as sounding), for the editor's live display. Published by the
    // audio thread at the end of every block, so reading them never touches stateLock.
    NoteBits getActiveInputNotes() const  { return loadNoteBits(activeInputWords); }
    NoteBits getActiveOutputNotes() const { return loadNoteBits(activeOutputWords); }

private:
    //==============================================================================
    std::vector<Preset> presets;
    int activePresetIndex = -1;

    // Runtime state
    using NoteKey = std::pair<int, int>;              // MIDI channel, note number

    std::map<NoteKey, std::vector<int>> activeNotes;  // input note -> output notes currently sounding
    std::map<NoteKey, int> outputNoteRefCount;        // output note -> number of input notes using it
    bool sustainPedalDown = false;
    std::vector<NoteKey> pendingNoteOffs;             // output notes waiting for sustain release
    std::vector<NoteKey> notesToFlush;                // output notes to emit note-off for on next processBlock

    // Guards all runtime state + preset structural edits across the audio and GUI threads.
    juce::CriticalSection stateLock;
    // Guards notesToFlush (written on GUI thread, consumed on audio thread).
    juce::CriticalSection flushLock;

    using AtomicNoteWords = std::array<std::atomic<std::uint64_t>, 2>;
    AtomicNoteWords activeInputWords {};
    AtomicNoteWords activeOutputWords {};

    static NoteBits loadNoteBits(const AtomicNoteWords& source)
    {
        NoteBits bits;
        for (size_t i = 0; i < bits.words.size(); ++i)
            bits.words[i] = source[i].load(std::memory_order_relaxed);
        return bits;
    }

    // Recomputes activeInputWords/activeOutputWords from the runtime note state. Caller must
    // hold stateLock.
    void publishActiveNotes();

    void handleNoteOn(int channel, int note, int velocity, int samplePosition, juce::MidiBuffer& output);
    void handleNoteOff(int channel, int note, int samplePosition, juce::MidiBuffer& output);
    void handleSustain(bool down, int samplePosition, juce::MidiBuffer& output);
    // Delivers note-offs for notesToFlush into the output buffer. Caller must hold stateLock.
    void flushPendingNoteOffs(juce::MidiBuffer& output);
    // Clears runtime note state and queues note-offs for any still-sounding output notes.
    // Caller must hold stateLock.
    void clearActiveNotes();

    static std::unique_ptr<juce::XmlElement> presetToXml(const Preset& preset);
    static Preset presetFromXml(const juce::XmlElement& xml);
    static std::vector<Preset> presetsFromXmlRoot(const juce::XmlElement& root);
    static std::vector<Preset> createFactoryPresets();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MnKordAudioProcessor)
};
