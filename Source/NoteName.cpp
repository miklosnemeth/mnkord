#include "NoteName.h"

namespace NoteName
{
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    juce::String toString(int midiNote)
    {
        if (midiNote < 0 || midiNote > 127)
            return {};

        int octave = (midiNote / 12) - 1;
        int note = midiNote % 12;
        return juce::String(noteNames[note]) + juce::String(octave);
    }

    juce::String pitchClassName(int midiNote)
    {
        if (midiNote < 0 || midiNote > 127)
            return {};

        return noteNames[midiNote % 12];
    }
}
