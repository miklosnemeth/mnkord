#pragma once
#include <JuceHeader.h>

namespace NoteName
{
    /** Converts a MIDI note number (0-127) to a note name like "C4", "D#4", "A3", "C-1". */
    juce::String toString(int midiNote);

    /** Converts a note name like "C4", "D#4", "A3", "C-1" to a MIDI note number, or -1 if invalid. */
    int fromString(const juce::String& name);

    /** Returns the pitch class name (e.g. "C#") for a MIDI note number, ignoring octave. */
    juce::String pitchClassName(int midiNote);

    /** Returns a list of all 128 note names for use in a ComboBox. */
    juce::StringArray getAllNames();
}
