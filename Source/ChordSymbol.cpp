#include "ChordSymbol.h"

namespace ChordSymbol
{
    // Pitch class of the note name at the start of s (C, F#, Bb, ...), or -1; used = characters read.
    int readNoteName(const juce::String& s, int& used)
    {
        if (s.isEmpty())
            return -1;

        int pc = -1;
        switch (juce::CharacterFunctions::toUpperCase(s[0]))
        {
            case 'C': pc = 0; break;
            case 'D': pc = 2; break;
            case 'E': pc = 4; break;
            case 'F': pc = 5; break;
            case 'G': pc = 7; break;
            case 'A': pc = 9; break;
            case 'B': pc = 11; break;
            default: return -1;
        }

        used = 1;
        if (s.length() > 1 && s[1] == '#')      { pc += 1; used = 2; }
        else if (s.length() > 1 && s[1] == 'b') { pc -= 1; used = 2; }

        return (pc + 12) % 12;
    }

    ParsedChord parseChord(const juce::String& symbol)
    {
        static const struct { const char* suffix; std::vector<int> intervals; } shapes[] = {
            { "",      { 0, 4, 7 } },            { "m",     { 0, 3, 7 } },
            { "dim",   { 0, 3, 6 } },            { "aug",   { 0, 4, 8 } },
            { "sus2",  { 0, 2, 7 } },            { "sus4",  { 0, 5, 7 } },
            { "5",     { 0, 7, 12 } },           { "6",     { 0, 4, 7, 9 } },
            { "m6",    { 0, 3, 7, 9 } },         { "maj7",  { 0, 4, 7, 11 } },
            { "m7",    { 0, 3, 7, 10 } },        { "7",     { 0, 4, 7, 10 } },
            { "m7b5",  { 0, 3, 6, 10 } },        { "dim7",  { 0, 3, 6, 9 } },
            { "add9",  { 0, 4, 7, 14 } },        { "madd9", { 0, 3, 7, 14 } },
            { "maj9",  { 0, 4, 7, 11, 14 } },    { "m9",    { 0, 3, 7, 10, 14 } },
            { "9",     { 0, 4, 7, 10, 14 } },    { "7sus4", { 0, 5, 7, 10 } },
            { "mmaj7", { 0, 3, 7, 11 } },        { "7#9",   { 0, 4, 7, 10, 15 } },
            { "7b9",   { 0, 4, 7, 10, 13 } },    { "6/9",   { 0, 4, 7, 9, 14 } },
            { "4th",   { 0, 5, 10, 15 } },
        };

        ParsedChord result;
        auto text = symbol.trim();
        if (text.isEmpty())
            return result;

        // A slash bass only counts when what follows is a note name ("G/B", but not "6/9").
        const int slash = text.lastIndexOfChar('/');
        if (slash > 0)
        {
            const auto bassText = text.substring(slash + 1);
            int used = 0;
            const int bass = readNoteName(bassText, used);
            if (bass >= 0 && used == bassText.length())
            {
                result.bassPc = bass;
                text = text.substring(0, slash);
            }
        }

        int used = 0;
        const int root = readNoteName(text, used);
        if (root < 0)
            return result;

        const auto suffix = text.substring(used);
        for (const auto& shape : shapes)
        {
            if (suffix == shape.suffix)
            {
                result.valid = true;
                result.rootPc = root;
                result.intervals = shape.intervals;
                return result;
            }
        }

        return result;   // unknown suffix (also reached by whatever is typed in the chord field)
    }

    // Closed voicing centred around E4/F4: the inversion (and octave) of the chord whose centre of
    // gravity is closest to there, with the root (or slash bass) as a separate bass note in C2-B2.
    NoteMapping voicedMapping(int inputNote, const ParsedChord& chord)
    {
        std::vector<int> stack;
        for (const int interval : chord.intervals)
            stack.push_back(48 + chord.rootPc + interval);

        std::vector<int> best = stack;
        double bestDistance = 1.0e9;

        for (size_t rotation = 0; rotation < stack.size(); ++rotation)
        {
            std::vector<int> notes = stack;
            for (size_t i = 0; i < rotation; ++i)
                notes[i] += 12;
            std::sort(notes.begin(), notes.end());

            // A chord with an octave in it (C5: C G C) would land a note on top of another.
            if (std::adjacent_find(notes.begin(), notes.end()) != notes.end())
                continue;

            for (const int shift : { -12, 0, 12 })
            {
                const int lowest = notes.front() + shift;
                if (lowest < 50 || lowest > 64)
                    continue;

                double centroid = 0.0;
                for (const int note : notes)
                    centroid += note + shift;
                centroid /= (double) notes.size();

                const double distance = std::abs(centroid - 64.0);
                if (distance < bestDistance - 1.0e-9)
                {
                    bestDistance = distance;
                    best = notes;
                    for (auto& note : best)
                        note += shift;
                }
            }
        }

        NoteMapping mapping;
        mapping.inputNote = inputNote;

        const int bassNote = 36 + (chord.bassPc >= 0 ? chord.bassPc : chord.rootPc);
        mapping.outputNotes.push_back(bassNote);
        mapping.outputVelocities[bassNote] = 90;

        for (const int note : best)
            if (note >= 0 && note < 128)
                mapping.outputNotes.push_back(note);

        return mapping;
    }

    juce::String nameFor(const std::vector<int>& notes)
    {
        if (notes.empty())
            return {};

        // Simplest first: when a set of notes fits several shapes, the one listed earlier wins.
        static const struct { const char* suffix; std::vector<int> intervals; } shapes[] = {
            { "",      { 0, 4, 7 } },         { "m",     { 0, 3, 7 } },
            { "dim",   { 0, 3, 6 } },         { "aug",   { 0, 4, 8 } },
            { "sus4",  { 0, 5, 7 } },         { "sus2",  { 0, 2, 7 } },
            { "7",     { 0, 4, 7, 10 } },     { "m7",    { 0, 3, 7, 10 } },
            { "maj7",  { 0, 4, 7, 11 } },     { "m7b5",  { 0, 3, 6, 10 } },
            { "dim7",  { 0, 3, 6, 9 } },      { "mmaj7", { 0, 3, 7, 11 } },
            { "7sus4", { 0, 5, 7, 10 } },     { "6",     { 0, 4, 7, 9 } },
            { "m6",    { 0, 3, 7, 9 } },      { "add9",  { 0, 2, 4, 7 } },
            { "madd9", { 0, 2, 3, 7 } },      { "9",     { 0, 2, 4, 7, 10 } },
            { "m9",    { 0, 2, 3, 7, 10 } },  { "maj9",  { 0, 2, 4, 7, 11 } },
            { "6/9",   { 0, 2, 4, 7, 9 } },   { "7b9",   { 0, 1, 4, 7, 10 } },
            { "7#9",   { 0, 3, 4, 7, 10 } },  { "4th",   { 0, 5, 10, 3 } },
            { "5",     { 0, 7 } },
        };

        static const char* const pitchNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

        auto maskOf = [](const std::vector<int>& intervals)
        {
            unsigned mask = 0;
            for (const int interval : intervals)
                mask |= 1u << (((interval % 12) + 12) % 12);
            return mask;
        };

        // Index into shapes of the chord rooted on root whose pitch classes are exactly pcMask, or -1.
        auto shapeFor = [&maskOf](unsigned pcMask, int root)
        {
            unsigned relative = 0;
            for (int pc = 0; pc < 12; ++pc)
                if ((pcMask >> pc & 1u) != 0)
                    relative |= 1u << ((pc - root + 12) % 12);

            for (int i = 0; i < (int) (sizeof(shapes) / sizeof(shapes[0])); ++i)
                if (maskOf(shapes[i].intervals) == relative)
                    return i;

            return -1;
        };

        const int lowest = *std::min_element(notes.begin(), notes.end());
        const int bassPc = ((lowest % 12) + 12) % 12;

        unsigned pcMask = 0, upperMask = 0;
        for (const int note : notes)
        {
            pcMask |= 1u << (((note % 12) + 12) % 12);
            if (note != lowest)
                upperMask |= 1u << (((note % 12) + 12) % 12);
        }

        // 1. Root position: the bass note is the root.
        if (const int shape = shapeFor(pcMask, bassPc); shape >= 0)
            return juce::String(pitchNames[bassPc]) + shapes[shape].suffix;

        // 2. A slash chord: an inversion (the bass is a chord tone), or a chord over a bass note that
        //    isn't in it (C/D, Am/G) -- only when the bass pitch class sounds nowhere above it.
        //    The simplest shape wins; on a tie, the inversion.
        const bool bassOnlyInBass = (upperMask >> bassPc & 1u) == 0;
        int bestRoot = -1, bestShape = -1;

        for (int root = 0; root < 12; ++root)
        {
            if (root == bassPc)
                continue;

            if ((pcMask >> root & 1u) != 0)
                if (const int shape = shapeFor(pcMask, root); shape >= 0 && (bestShape < 0 || shape < bestShape))
                    bestRoot = root, bestShape = shape;

            if (bassOnlyInBass && (upperMask >> root & 1u) != 0)
                if (const int shape = shapeFor(upperMask, root); shape >= 0 && (bestShape < 0 || shape < bestShape))
                    bestRoot = root, bestShape = shape;
        }

        if (bestShape < 0)
            return {};

        return juce::String(pitchNames[bestRoot]) + shapes[bestShape].suffix + "/" + pitchNames[bassPc];
    }
}
