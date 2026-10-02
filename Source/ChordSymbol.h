#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Chord symbols ("Dm9", "G/B", "Bbmaj7", "C6/9", ...): parsing them into notes and the other
// way round. Used by the factory presets and by the chord symbol field in the editor.
namespace ChordSymbol
{
    struct ParsedChord
    {
        bool valid = false;
        int rootPc = 0;
        int bassPc = -1;               // slash bass, or -1
        std::vector<int> intervals;    // from the root; extensions above 12 are kept
    };

    ParsedChord parseChord(const juce::String& symbol);

    // A mapping for inputNote that plays the chord: a closed voicing centred around E4/F4 with
    // the root (or slash bass) as a separate bass note in C2-B2.
    NoteMapping voicedMapping(int inputNote, const ParsedChord& chord);

    // The symbol that describes notes ("Dm7/F", "Cmaj9"), or an empty string if none fits.
    juce::String nameFor(const std::vector<int>& notes);
}
