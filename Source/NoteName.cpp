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

    int fromString(const juce::String& name)
    {
        auto trimmed = name.trim();
        if (trimmed.isEmpty())
            return -1;

        int idx = 0;
        juce::String notePart;

        // Parse the note letter (C, D, E, F, G, A, B)
        if (idx < trimmed.length() && juce::String("CDEFGAB").containsChar(trimmed[idx]))
        {
            notePart += trimmed[idx];
            idx++;
        }
        else
        {
            return -1;
        }

        // Check for a sharp
        if (idx < trimmed.length() && trimmed[idx] == '#')
        {
            notePart += '#';
            idx++;
        }

        // Find the note index
        int note = -1;
        for (int i = 0; i < 12; ++i)
        {
            if (notePart == noteNames[i])
            {
                note = i;
                break;
            }
        }

        if (note == -1)
            return -1;

        // Parse the octave
        int sign = 1;
        if (idx < trimmed.length() && trimmed[idx] == '-')
        {
            sign = -1;
            idx++;
        }

        int octave = 0;
        bool hasOctave = false;
        while (idx < trimmed.length() && trimmed[idx] >= '0' && trimmed[idx] <= '9')
        {
            octave = octave * 10 + (trimmed[idx] - '0');
            hasOctave = true;
            idx++;
        }

        if (!hasOctave)
            return -1;

        octave *= sign;

        int midiNote = (octave + 1) * 12 + note;
        if (midiNote < 0 || midiNote > 127)
            return -1;

        return midiNote;
    }

    juce::String pitchClassName(int midiNote)
    {
        if (midiNote < 0 || midiNote > 127)
            return {};

        return noteNames[midiNote % 12];
    }

    juce::StringArray getAllNames()
    {
        juce::StringArray names;
        for (int i = 0; i < 128; ++i)
            names.add(toString(i));
        return names;
    }
}
