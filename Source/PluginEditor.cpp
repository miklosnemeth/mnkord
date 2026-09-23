#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NoteName.h"
#include "BinaryData.h"

#include <functional>
#include <limits>

namespace
{
    // File-backed debug log, always active (independent of Debug/Release), so UI issues can
    // be diagnosed from the actual log file instead of a debugger. Logged to
    // <user app data>/mnKord/Logs/mnKord.log (path printed to the log itself on startup).
    void installFileLogger()
    {
        static std::unique_ptr<juce::FileLogger> fileLogger(
            juce::FileLogger::createDefaultAppLogger("mnKord", "mnKord.log", "mnKord debug log"));
        juce::Logger::setCurrentLogger(fileLogger.get());
    }

    void logUi(const juce::String& message)
    {
        juce::Logger::writeToLog(message);
    }

    juce::String displayNoteName(int midiNote)
    {
        // Delegate to the shared NoteName utility so octave numbering is consistent
        // across the whole plugin (MIDI 60 == "C4").
        return NoteName::toString(midiNote);
    }

    juce::String displayPitchClassName(int midiNote)
    {
        return NoteName::pitchClassName(midiNote);
    }

    juce::String noteListToString(const std::vector<int>& notes)
    {
        if (notes.empty())
            return "-";

        juce::String result;
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            if (i > 0)
                result += "  ";

            result += displayNoteName(notes[(size_t) i]);
        }

        return result;
    }

    bool pitchClassesMatch(const std::vector<int>& notes, int root, std::initializer_list<int> intervals)
    {
        if (notes.size() != intervals.size())
            return false;

        std::vector<int> actual;
        actual.reserve(notes.size());

        for (const auto note : notes)
            actual.push_back((note - root + 1200) % 12);

        std::sort(actual.begin(), actual.end());
        actual.erase(std::unique(actual.begin(), actual.end()), actual.end());

        std::vector<int> expected(intervals);
        std::sort(expected.begin(), expected.end());

        return actual == expected;
    }

    juce::String describeChord(const NoteMapping& mapping)
    {
        const auto notes = noteListToString(mapping.outputNotes);

        if (mapping.outputNotes.empty())
            return displayNoteName(mapping.inputNote) + " (-)";

        const auto root = mapping.inputNote;
        const auto rootName = displayPitchClassName(root);
        juce::String quality = " chord";

        if (pitchClassesMatch(mapping.outputNotes, root, { 0, 4, 7 }))
            quality = "maj";
        else if (pitchClassesMatch(mapping.outputNotes, root, { 0, 3, 7 }))
            quality = "min";
        else if (pitchClassesMatch(mapping.outputNotes, root, { 0, 2, 7 }))
            quality = "sus2";
        else if (pitchClassesMatch(mapping.outputNotes, root, { 0, 5, 7 }))
            quality = "sus4";
        else if (pitchClassesMatch(mapping.outputNotes, root, { 0, 4, 7, 10 }))
            quality = "7";
        else if (pitchClassesMatch(mapping.outputNotes, root, { 0, 4, 7, 11 }))
            quality = "maj7";
        else if (pitchClassesMatch(mapping.outputNotes, root, { 0, 3, 7, 10 }))
            quality = "min7";
        else if (pitchClassesMatch(mapping.outputNotes, root, { 0, 3, 6 }))
            quality = "dim";
        else if (pitchClassesMatch(mapping.outputNotes, root, { 0, 2, 4, 7 }))
            quality = "add9";

        return rootName + quality + " (" + notes + ")";
    }

    //==============================================================================
    // A custom content component holding the three labelled metadata fields with an
    // explicit, predictable layout. It is shown inside a juce::AlertWindow via
    // addCustomComponent() — AlertWindow reliably appears in plugin hosts, whereas
    // DialogWindow::LaunchOptions::launchAsync() did not show up.
    class PresetMetadataContent : public juce::Component
    {
    public:
        PresetMetadataContent(const Preset& seedPreset,
                              juce::Colour text,
                              juce::Colour control,
                              juce::Colour accent,
                              bool showGenerationOptionsIn,
                              int seedScaleRoot,
                              MnKordAudioProcessorEditor::Scale seedScaleType)
            : showGenerationOptions(showGenerationOptionsIn)
        {
            nameLabel.setText("Name", juce::dontSendNotification);
            groupLabel.setText("Group", juce::dontSendNotification);
            authorLabel.setText("Author", juce::dontSendNotification);

            nameEditor.setText(seedPreset.name);
            groupEditor.setText(seedPreset.group);
            authorEditor.setText(seedPreset.author);

            for (auto* label : { &nameLabel, &groupLabel, &authorLabel,
                                 &scaleLabel, &chordSizeLabel, &inversionLabel, &octaveLabel })
                label->setColour(juce::Label::textColourId, text);

            for (auto* e : { &nameEditor, &groupEditor, &authorEditor })
            {
                e->setColour(juce::TextEditor::backgroundColourId, control);
                e->setColour(juce::TextEditor::textColourId, text);
                e->setColour(juce::TextEditor::outlineColourId, accent);
                e->setColour(juce::TextEditor::focusedOutlineColourId, accent);
                e->setColour(juce::TextEditor::highlightColourId, accent.withAlpha(0.25f));
            }

            addAndMakeVisible(nameLabel);
            addAndMakeVisible(groupLabel);
            addAndMakeVisible(authorLabel);
            addAndMakeVisible(nameEditor);
            addAndMakeVisible(groupEditor);
            addAndMakeVisible(authorEditor);

            if (showGenerationOptions)
            {
                scaleLabel.setText("Auto-fill", juce::dontSendNotification);
                chordSizeLabel.setText("Chord size", juce::dontSendNotification);
                inversionLabel.setText("Inversion", juce::dontSendNotification);
                octaveLabel.setText("Octave", juce::dontSendNotification);

                for (int i = 0; i < 12; ++i)
                    scaleRootBox.addItem(NoteName::pitchClassName(i), i + 1);
                scaleRootBox.setSelectedItemIndex(seedScaleRoot, juce::dontSendNotification);

                scaleTypeBox.addItem("Full range", 1);
                scaleTypeBox.addItem("Major", 2);
                scaleTypeBox.addItem("Natural minor", 3);
                scaleTypeBox.addItem("Harmonic minor", 4);
                scaleTypeBox.addItem("Melodic minor", 5);
                scaleTypeBox.addItem("Maj pentatonic", 6);
                scaleTypeBox.addItem("Min pentatonic", 7);
                scaleTypeBox.setSelectedItemIndex(scaleIndexFor(seedScaleType), juce::dontSendNotification);

                chordSizeBox.addItem("Triad (3 notes)", 1);
                chordSizeBox.addItem("Seventh (4 notes)", 2);
                chordSizeBox.setSelectedItemIndex(0, juce::dontSendNotification);

                inversionBox.addItem("Root position", 1);
                inversionBox.addItem("1st inversion", 2);
                inversionBox.addItem("2nd inversion", 3);
                inversionBox.addItem("3rd inversion", 4);
                inversionBox.setSelectedItemIndex(0, juce::dontSendNotification);

                octaveBox.addItem("-2", 1);
                octaveBox.addItem("-1", 2);
                octaveBox.addItem("0", 3);
                octaveBox.addItem("+1", 4);
                octaveBox.addItem("+2", 5);
                octaveBox.setSelectedItemIndex(2, juce::dontSendNotification);

                for (auto* box : { &scaleRootBox, &scaleTypeBox, &chordSizeBox, &inversionBox, &octaveBox })
                {
                    box->setColour(juce::ComboBox::backgroundColourId, control);
                    box->setColour(juce::ComboBox::textColourId, text);
                    box->setColour(juce::ComboBox::outlineColourId, accent.withAlpha(0.4f));
                    addAndMakeVisible(box);
                }

                addAndMakeVisible(scaleLabel);
                addAndMakeVisible(chordSizeLabel);
                addAndMakeVisible(inversionLabel);
                addAndMakeVisible(octaveLabel);
            }

            setSize(420, showGenerationOptions ? 254 : 128);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            const int rowH = 28;
            const int gap = 14;
            const int labelW = 68;

            auto nameRow = area.removeFromTop(rowH);
            nameLabel.setBounds(nameRow.removeFromLeft(labelW));
            nameEditor.setBounds(nameRow);

            area.removeFromTop(gap);

            auto groupRow = area.removeFromTop(rowH);
            groupLabel.setBounds(groupRow.removeFromLeft(labelW));
            groupEditor.setBounds(groupRow);

            area.removeFromTop(gap);

            auto authorRow = area.removeFromTop(rowH);
            authorLabel.setBounds(authorRow.removeFromLeft(labelW));
            authorEditor.setBounds(authorRow);

            if (! showGenerationOptions)
                return;

            area.removeFromTop(gap);

            auto scaleRow = area.removeFromTop(rowH);
            scaleLabel.setBounds(scaleRow.removeFromLeft(labelW));
            scaleRootBox.setBounds(scaleRow.removeFromLeft(64));
            scaleRow.removeFromLeft(6);
            scaleTypeBox.setBounds(scaleRow);

            area.removeFromTop(gap);

            auto chordRow = area.removeFromTop(rowH);
            const int halfW = (chordRow.getWidth() - 6) / 2;
            chordSizeLabel.setBounds(chordRow.removeFromLeft(labelW));
            chordSizeBox.setBounds(chordRow.removeFromLeft(halfW - labelW));
            chordRow.removeFromLeft(6);
            inversionLabel.setBounds(chordRow.removeFromLeft(labelW));
            inversionBox.setBounds(chordRow);

            area.removeFromTop(gap);

            auto octaveRow = area.removeFromTop(rowH);
            octaveLabel.setBounds(octaveRow.removeFromLeft(labelW));
            octaveBox.setBounds(octaveRow.removeFromLeft(80));
        }

        juce::String getPresetName() const { return nameEditor.getText().trim(); }
        juce::String getPresetGroup() const { return groupEditor.getText().trim(); }
        juce::String getPresetAuthor() const { return authorEditor.getText().trim(); }

        MnKordAudioProcessorEditor::DiatonicFillOptions getFillOptions() const
        {
            MnKordAudioProcessorEditor::DiatonicFillOptions options;

            if (! showGenerationOptions)
                return options;

            options.scaleRoot = juce::jmax(0, scaleRootBox.getSelectedItemIndex());

            switch (scaleTypeBox.getSelectedItemIndex())
            {
                case 1:  options.scaleType = MnKordAudioProcessorEditor::Scale::Major; break;
                case 2:  options.scaleType = MnKordAudioProcessorEditor::Scale::NaturalMinor; break;
                case 3:  options.scaleType = MnKordAudioProcessorEditor::Scale::HarmonicMinor; break;
                case 4:  options.scaleType = MnKordAudioProcessorEditor::Scale::MelodicMinor; break;
                case 5:  options.scaleType = MnKordAudioProcessorEditor::Scale::MajorPentatonic; break;
                case 6:  options.scaleType = MnKordAudioProcessorEditor::Scale::MinorPentatonic; break;
                default: options.scaleType = MnKordAudioProcessorEditor::Scale::Chromatic; break;
            }

            options.chordSize = chordSizeBox.getSelectedItemIndex() == 1 ? 4 : 3;
            options.inversion = juce::jmax(0, inversionBox.getSelectedItemIndex());
            options.octaveShift = juce::jmax(0, octaveBox.getSelectedItemIndex()) - 2;

            return options;
        }

    private:
        static int scaleIndexFor(MnKordAudioProcessorEditor::Scale scale)
        {
            switch (scale)
            {
                case MnKordAudioProcessorEditor::Scale::Major:           return 1;
                case MnKordAudioProcessorEditor::Scale::NaturalMinor:    return 2;
                case MnKordAudioProcessorEditor::Scale::HarmonicMinor:   return 3;
                case MnKordAudioProcessorEditor::Scale::MelodicMinor:    return 4;
                case MnKordAudioProcessorEditor::Scale::MajorPentatonic: return 5;
                case MnKordAudioProcessorEditor::Scale::MinorPentatonic: return 6;
                case MnKordAudioProcessorEditor::Scale::Chromatic:       return 0;
            }
            return 0;
        }

        bool showGenerationOptions = false;

        juce::Label nameLabel, groupLabel, authorLabel;
        juce::TextEditor nameEditor, groupEditor, authorEditor;

        juce::Label scaleLabel, chordSizeLabel, inversionLabel, octaveLabel;
        juce::ComboBox scaleRootBox, scaleTypeBox, chordSizeBox, inversionBox, octaveBox;
    };
}

//==============================================================================
juce::Typeface::Ptr MnKordAudioProcessorEditor::FlatLookAndFeel::getTypefaceForFont(const juce::Font&)
{
    static const juce::Typeface::Ptr robotoCondensed = juce::Typeface::createSystemTypefaceFor(
        BinaryData::RobotoCondensed_ttf, (size_t) BinaryData::RobotoCondensed_ttfSize);
    return robotoCondensed;
}

juce::Typeface::Ptr MnKordAudioProcessorEditor::getTitleTypeface()
{
    static const juce::Typeface::Ptr montserrat = juce::Typeface::createSystemTypefaceFor(
        BinaryData::Montserrat_ttf, (size_t) BinaryData::Montserrat_ttfSize);
    return montserrat;
}

void MnKordAudioProcessorEditor::IconButton::paintButton(juce::Graphics& g,
                                                          bool shouldDrawButtonAsHighlighted,
                                                          bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat();

    if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
    {
        g.setColour(findColour(juce::TextButton::buttonOnColourId).withAlpha(shouldDrawButtonAsDown ? 0.35f : 0.18f));
        g.fillRect(bounds);
    }

    auto iconArea = bounds.reduced(9.0f);
    auto pt = [&iconArea](float x, float y)
    {
        return juce::Point<float>(iconArea.getX() + (x / 24.0f) * iconArea.getWidth(),
                                  iconArea.getY() + (y / 24.0f) * iconArea.getHeight());
    };

    juce::Path p;
    switch (icon)
    {
        case ToolIcon::Undo:
            p.startNewSubPath(pt(15, 5)); p.lineTo(pt(9, 12)); p.lineTo(pt(15, 19));
            p.startNewSubPath(pt(9, 12)); p.lineTo(pt(19, 12));
            break;
        case ToolIcon::Redo:
            p.startNewSubPath(pt(9, 5)); p.lineTo(pt(15, 12)); p.lineTo(pt(9, 19));
            p.startNewSubPath(pt(15, 12)); p.lineTo(pt(5, 12));
            break;
        case ToolIcon::OctaveDown:
            p.startNewSubPath(pt(6, 9));  p.lineTo(pt(12, 15)); p.lineTo(pt(18, 9));
            p.startNewSubPath(pt(6, 15)); p.lineTo(pt(12, 21)); p.lineTo(pt(18, 15));
            break;
        case ToolIcon::OctaveUp:
            p.startNewSubPath(pt(6, 15)); p.lineTo(pt(12, 9)); p.lineTo(pt(18, 15));
            p.startNewSubPath(pt(6, 9));  p.lineTo(pt(12, 3)); p.lineTo(pt(18, 9));
            break;
    }

    auto strokeColour = findColour(juce::TextButton::textColourOffId);
    if (! isEnabled())
        strokeColour = strokeColour.withAlpha(0.35f);

    g.setColour(strokeColour);
    g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MnKordAudioProcessorEditor::FlatLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                                                       juce::Button& button,
                                                                       const juce::Colour& backgroundColour,
                                                                       bool shouldDrawButtonAsHighlighted,
                                                                       bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    auto fill = backgroundColour;

    if (shouldDrawButtonAsDown)
        fill = fill.brighter(0.16f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.08f);

    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha(0.5f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 7.0f);
}

void MnKordAudioProcessorEditor::FlatLookAndFeel::drawComboBox(juce::Graphics& g,
                                                               int width,
                                                               int height,
                                                               bool isButtonDown,
                                                               int buttonX,
                                                               int buttonY,
                                                               int buttonW,
                                                               int buttonH,
                                                               juce::ComboBox& box)
{
    juce::ignoreUnused(buttonX, buttonY, buttonW, buttonH);

    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);
    auto fill = box.findColour(juce::ComboBox::backgroundColourId);
    if (isButtonDown)
        fill = fill.brighter(0.12f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 7.0f);

    auto arrow = juce::Path();
    const auto cx = (float) width - 17.0f;
    const auto cy = (float) height * 0.5f + 1.0f;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 2.0f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);

    g.setColour(box.findColour(juce::ComboBox::textColourId).withAlpha(0.8f));
    g.strokePath(arrow, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MnKordAudioProcessorEditor::FlatLookAndFeel::fillTextEditorBackground(juce::Graphics& g,
                                                                           int width,
                                                                           int height,
                                                                           juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height), 7.0f);
}

void MnKordAudioProcessorEditor::FlatLookAndFeel::drawTextEditorOutline(juce::Graphics& g,
                                                                        int width,
                                                                        int height,
                                                                        juce::TextEditor& editor)
{
    juce::ignoreUnused(g, width, height, editor);
}

void MnKordAudioProcessorEditor::FlatLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.setColour(findColour(juce::PopupMenu::backgroundColourId));
    g.fillRect(0, 0, width, height);
}

void MnKordAudioProcessorEditor::FlatLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& g,
                                                                                     int width,
                                                                                     int height,
                                                                                     const juce::PopupMenu::Options& options)
{
    juce::ignoreUnused(options);
    drawPopupMenuBackground(g, width, height);
}

void MnKordAudioProcessorEditor::FlatLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                                                    const juce::Rectangle<int>& area,
                                                                    bool isSeparator,
                                                                    bool isActive,
                                                                    bool isHighlighted,
                                                                    bool isTicked,
                                                                    bool hasSubMenu,
                                                                    const juce::String& text,
                                                                    const juce::String& shortcutKeyText,
                                                                    const juce::Drawable* icon,
                                                                    const juce::Colour* textColour)
{
    juce::ignoreUnused(shortcutKeyText, icon, hasSubMenu);

    if (isSeparator)
        return;

    auto row = area.reduced(0, 1);
    if (isHighlighted)
    {
        g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRect(row);
    }

    auto colour = textColour != nullptr ? *textColour
                                        : findColour(isHighlighted ? juce::PopupMenu::highlightedTextColourId
                                                                   : juce::PopupMenu::textColourId);
    if (! isActive)
        colour = colour.withAlpha(0.45f);

    g.setColour(colour);
    g.setFont(juce::FontOptions(14.0f));

    auto textArea = row.reduced(28, 0);
    g.drawText(text, textArea, juce::Justification::centredLeft, true);

    if (isTicked)
    {
        juce::Path tick;
        const auto tickArea = row.removeFromLeft(24).toFloat().reduced(7.0f, 8.0f);
        tick.startNewSubPath(tickArea.getX(), tickArea.getCentreY());
        tick.lineTo(tickArea.getCentreX() - 1.0f, tickArea.getBottom());
        tick.lineTo(tickArea.getRight(), tickArea.getY());
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

int MnKordAudioProcessorEditor::FlatLookAndFeel::getPopupMenuBorderSize()
{
    return 0;
}

int MnKordAudioProcessorEditor::FlatLookAndFeel::getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options& options)
{
    juce::ignoreUnused(options);
    return 0;
}

//==============================================================================
MnKordAudioProcessorEditor::MappingKeyboard::MappingKeyboard(MnKordAudioProcessorEditor& ownerRef,
                                                             KeyboardRole roleRef,
                                                             juce::MidiKeyboardState& stateRef)
    : juce::MidiKeyboardComponent(stateRef, juce::MidiKeyboardComponent::horizontalKeyboard),
      owner(ownerRef),
      role(roleRef)
{
    setAvailableRange(21, 108);
    setLowestVisibleKey(36);
    setKeyWidth(role == KeyboardRole::input ? 24.0f : 25.0f);
    setScrollButtonsVisible(true);
    setMidiChannel(1);

    setColour(whiteNoteColourId, owner.getWhiteKeyTopColour());
    setColour(blackNoteColourId, owner.getBlackKeyBottomColour());
    setColour(keySeparatorLineColourId, juce::Colour(0xff485057));
    setColour(mouseOverKeyOverlayColourId, juce::Colour(0x1a2ee66b));
    setColour(keyDownOverlayColourId, juce::Colour(0x3d2ee66b));
    setColour(textLabelColourId, juce::Colour(0xff66716b));
    setColour(shadowColourId, juce::Colour(0x88000000));
}

bool MnKordAudioProcessorEditor::MappingKeyboard::mouseDownOnKey(int midiNoteNumber, const juce::MouseEvent& event)
{
    logUi("mouseDownOnKey: role=" + juce::String(role == KeyboardRole::input ? "input" : "output")
        + " note=" + juce::String(midiNoteNumber)
        + " rightClick=" + juce::String((int) event.mods.isPopupMenu())
        + " leftClick=" + juce::String((int) event.mods.isLeftButtonDown()));

    if (role == KeyboardRole::input && ! owner.isInSelectedScale(midiNoteNumber))
    {
        logUi("  -> ignored: note not in selected scale");
        return false;
    }

    if (role == KeyboardRole::input && event.mods.isPopupMenu())
    {
        logUi("  -> showChordTemplateMenu");
        owner.showChordTemplateMenu(midiNoteNumber);
        return false;
    }

    if (role == KeyboardRole::output && event.mods.isPopupMenu())
    {
        logUi("  -> showInvertMenu");
        owner.showInvertMenu();
        return false;
    }

    logUi("  -> keyboardNoteClicked");
    owner.keyboardNoteClicked(role, midiNoteNumber);
    return false;
}

bool MnKordAudioProcessorEditor::MappingKeyboard::mouseDraggedToKey(int midiNoteNumber, const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (role == KeyboardRole::input && ! owner.isInSelectedScale(midiNoteNumber))
        return false;

    owner.keyboardNoteClicked(role, midiNoteNumber);
    return false;
}

void MnKordAudioProcessorEditor::MappingKeyboard::drawWhiteNote(int midiNoteNumber,
                                                                juce::Graphics& g,
                                                                juce::Rectangle<float> area,
                                                                bool isDown,
                                                                bool isOver,
                                                                juce::Colour lineColour,
                                                                juce::Colour textColour)
{
    const auto highlighted = owner.isHighlightedKey(role, midiNoteNumber);
    const auto mapped = role == KeyboardRole::input && owner.isMappedInputKey(midiNoteNumber);
    const auto outOfScale = role == KeyboardRole::input && ! owner.isInSelectedScale(midiNoteNumber);
    auto keyArea = area.reduced(0.5f, 0.0f);

    if (highlighted)
    {
        g.setGradientFill(juce::ColourGradient(owner.getAccentColour().brighter(0.42f), keyArea.getX(), keyArea.getY(),
                                               owner.getAccentColour(), keyArea.getX(), keyArea.getBottom(), false));
    }
    else if (mapped)
    {
        g.setGradientFill(juce::ColourGradient(owner.getWhiteKeyTopColour().interpolatedWith(owner.getAccentColour(), 0.22f),
                                               keyArea.getX(), keyArea.getY(),
                                               owner.getWhiteKeyBottomColour().interpolatedWith(owner.getAccentColour(), 0.28f),
                                               keyArea.getX(), keyArea.getBottom(), false));
    }
    else
    {
        g.setGradientFill(juce::ColourGradient(isDown ? owner.getWhiteKeyTopColour().interpolatedWith(owner.getAccentColour(), 0.12f) : owner.getWhiteKeyTopColour(),
                                               keyArea.getX(), keyArea.getY(),
                                               isDown ? owner.getWhiteKeyBottomColour().interpolatedWith(owner.getAccentColour(), 0.16f) : owner.getWhiteKeyBottomColour(),
                                               keyArea.getX(), keyArea.getBottom(), false));
    }

    g.fillRect(keyArea);

    if (outOfScale)
    {
        // Flat muted-grey scrim so out-of-scale keys read as disabled regardless of
        // how light/dark the underlying key colour is.
        g.setColour(owner.getMutedTextColour().withAlpha(0.55f));
        g.fillRect(keyArea);
    }

    if (isOver && ! highlighted && ! outOfScale)
    {
        g.setColour(owner.getAccentColour().withAlpha(0.13f));
        g.fillRect(area.reduced(1.0f, 1.0f));
    }

    if (highlighted)
    {
        g.setColour(juce::Colour(0x22000000));
        g.fillRect(area.withHeight(5.0f).reduced(2.0f, 0.0f));
    }

    if (midiNoteNumber % 12 == 0)
    {
        g.setColour(highlighted ? juce::Colour(0xff043716) : textColour);
        g.setFont(11.0f);
        g.drawText(getWhiteNoteText(midiNoteNumber), area.reduced(2.0f, 6.0f), juce::Justification::centredBottom, false);
    }

    if (highlighted && role == KeyboardRole::output)
    {
        g.setColour(juce::Colour(0xff043716));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(owner.getOutputDegreeLabel(midiNoteNumber), area.reduced(2.0f, 8.0f),
                   juce::Justification::centredTop, false);
    }
}

void MnKordAudioProcessorEditor::MappingKeyboard::drawBlackNote(int midiNoteNumber,
                                                                juce::Graphics& g,
                                                                juce::Rectangle<float> area,
                                                                bool isDown,
                                                                bool isOver,
                                                                juce::Colour noteFillColour)
{
    const auto highlighted = owner.isHighlightedKey(role, midiNoteNumber);
    const auto mapped = role == KeyboardRole::input && owner.isMappedInputKey(midiNoteNumber);
    const auto outOfScale = role == KeyboardRole::input && ! owner.isInSelectedScale(midiNoteNumber);
    auto keyArea = area.reduced(1.0f, 0.0f);

    if (highlighted)
    {
        g.setGradientFill(juce::ColourGradient(owner.getAccentColour().brighter(0.34f), keyArea.getX(), keyArea.getY(),
                                               owner.getAccentColour().darker(0.12f), keyArea.getX(), keyArea.getBottom(), false));
    }
    else if (mapped)
    {
        g.setGradientFill(juce::ColourGradient(owner.getBlackKeyTopColour().interpolatedWith(owner.getAccentColour(), 0.34f),
                                               keyArea.getX(), keyArea.getY(),
                                               owner.getBlackKeyBottomColour().interpolatedWith(owner.getAccentColour(), 0.3f),
                                               keyArea.getX(), keyArea.getBottom(), false));
    }
    else
    {
        g.setGradientFill(juce::ColourGradient(isDown ? owner.getBlackKeyTopColour().interpolatedWith(owner.getAccentColour(), 0.18f) : owner.getBlackKeyTopColour(),
                                               keyArea.getX(), keyArea.getY(),
                                               isDown ? owner.getBlackKeyBottomColour().interpolatedWith(owner.getAccentColour(), 0.14f) : owner.getBlackKeyBottomColour(),
                                               keyArea.getX(), keyArea.getBottom(), false));
    }

    g.fillRoundedRectangle(keyArea, 2.0f);

    if (outOfScale)
    {
        // Flat muted-grey scrim so out-of-scale keys read as disabled regardless of
        // how light/dark the underlying key colour is.
        g.setColour(owner.getMutedTextColour().withAlpha(0.55f));
        g.fillRoundedRectangle(keyArea, 2.0f);
    }

    if (isOver && ! highlighted && ! outOfScale)
    {
        g.setColour(owner.getAccentColour().withAlpha(0.14f));
        g.fillRoundedRectangle(keyArea, 2.0f);
    }

    if (highlighted && role == KeyboardRole::output)
    {
        g.setColour(juce::Colour(0xff043716));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(owner.getOutputDegreeLabel(midiNoteNumber), keyArea.reduced(1.0f, 4.0f).toNearestInt(),
                   juce::Justification::centredTop, false);
    }
}

//==============================================================================
MnKordAudioProcessorEditor::MnKordAudioProcessorEditor(MnKordAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      inputKeyboard(*this, KeyboardRole::input, inputKeyboardState),
      outputKeyboard(*this, KeyboardRole::output, outputKeyboardState)
{
    installFileLogger();
    logUi("mnKord editor starting up. Log file: "
        + juce::FileLogger::getSystemLogFileFolder().getChildFile("mnKord").getChildFile("mnKord.log").getFullPathName());

    setLookAndFeel(&flatLookAndFeel);

    presetBox.addListener(this);
    addAndMakeVisible(presetBox);

    designBox.addListener(this);
    addAndMakeVisible(designBox);

    scaleSummaryButton.addListener(this);
    addAndMakeVisible(scaleSummaryButton);

    newPresetButton.setButtonText("New");
    editPresetButton.setButtonText("Edit");
    savePresetButton.setButtonText("Save as");
    deletePresetButton.setButtonText("Delete");
    importPresetButton.setButtonText("Import");
    exportPresetButton.setButtonText("Export");
    compactVoicingButton.setButtonText("Compact");
    clearChordButton.setButtonText("Clear chord");
    deleteMappingButton.setButtonText("Delete key");

    undoButton.setTooltip("Undo");
    redoButton.setTooltip("Redo");
    octaveDownButton.setTooltip("Octave down");
    octaveUpButton.setTooltip("Octave up");

    for (auto* button : { &newPresetButton, &editPresetButton, &savePresetButton, &deletePresetButton,
                          &importPresetButton, &exportPresetButton,
                          &compactVoicingButton,
                          &clearChordButton, &deleteMappingButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }

    for (auto* button : { &undoButton, &redoButton, &octaveDownButton, &octaveUpButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }

    setWantsKeyboardFocus(true);

    // Host/program changes and external preset edits may arrive while the editor is open.
    startTimerHz(8);

    inputKeyboardLabel.setText("INPUT", juce::dontSendNotification);
    outputKeyboardLabel.setText("CHORD", juce::dontSendNotification);
    toolsLabel.setText("TOOLS   -   right-click INPUT for chord templates, CHORD to invert", juce::dontSendNotification);
    selectedInputLabel.setText("-", juce::dontSendNotification);
    chordNotesLabel.setText("-", juce::dontSendNotification);
    presetInfoLabel.setText("-", juce::dontSendNotification);

    for (auto* label : { &inputKeyboardLabel, &toolsLabel, &outputKeyboardLabel })
    {
        label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    }

    for (auto* label : { &selectedInputLabel, &chordNotesLabel })
    {
        label->setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
    }

    presetInfoLabel.setJustificationType(juce::Justification::centredLeft);
    presetInfoLabel.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(presetInfoLabel);

    addAndMakeVisible(inputKeyboard);
    addAndMakeVisible(outputKeyboard);

    setSize(980, 546);

    refreshDesignBox();
    updateScaleSummaryText();
    applyVisualTheme();
    refreshPresetBox();
    setSelectedMappingIndex(selectedMappingIndex);
    updateUndoRedoButtons();
    grabKeyboardFocus();
}

MnKordAudioProcessorEditor::~MnKordAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void MnKordAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getBackgroundColour());

    auto bounds = getLocalBounds().toFloat();
    auto header = bounds.removeFromTop(74.0f);

    g.setGradientFill(juce::ColourGradient(getPanelColour().brighter(0.08f), 0.0f, 0.0f,
                                           getBackgroundColour(), bounds.getRight(), header.getBottom(), false));
    g.fillRect(header);

    g.setColour(getTextColour());
    g.setFont(juce::Font(getTitleTypeface()).withHeight(34.0f).boldened());

    const auto titleArea = header.withTrimmedLeft(18.0f);

    // The variable font's default instance plus JUCE's synthetic bold isn't quite heavy
    // enough on its own, so restroke the text with a small offset to thicken it further.
    for (const auto offset : { juce::Point<float> (0.0f, 0.0f), juce::Point<float> (0.6f, 0.0f) })
        g.drawText("mnKord", titleArea.translated(offset.x, offset.y), juce::Justification::centredLeft);

    auto panelArea = getLocalBounds().reduced(14).withTrimmedTop(74).toFloat();
    auto topPanel = panelArea.removeFromTop(142.0f);
    panelArea.removeFromTop(14.0f);
    auto toolsPanel = panelArea.removeFromTop(76.0f);
    panelArea.removeFromTop(14.0f);
    auto bottomPanel = panelArea.removeFromTop(184.0f);

    for (const auto rect : { topPanel, toolsPanel, bottomPanel })
    {
        g.setColour(getPanelColour());
        g.fillRoundedRectangle(rect, 8.0f);
    }

    // Shared rounded backgrounds behind the Undo/Redo and Octave icon-button pairs, each split
    // by a thin divider so the pair still reads as two distinct actions.
    for (const auto& seg : { segUndoRedoBounds, segOctaveBounds })
    {
        g.setColour(getControlColour());
        g.fillRoundedRectangle(seg.toFloat(), 8.0f);

        g.setColour(getPanelLineColour());
        g.fillRect(juce::Rectangle<float>((float) seg.getCentreX() - 0.5f, (float) seg.getY() + 5.0f,
                                          1.0f, (float) seg.getHeight() - 10.0f));
    }

    g.setColour(getPanelLineColour());
    g.fillRect(toolsGroupDivider.toFloat());
}

void MnKordAudioProcessorEditor::resized()
{
    // The header background (paint()) spans the full, unmargined width/height 0-74, so its
    // children are laid out from raw local bounds too, with margins applied per-child instead
    // of via an outer reduced() that would otherwise desync the two.
    auto headerArea = getLocalBounds().removeFromTop(74).reduced(18, 8);

    auto titleColumn = headerArea.removeFromLeft(172);
    presetInfoLabel.setBounds(titleColumn.removeFromBottom(18));

    const int groupWidth = 180 + 8 + 110 + 8 + 62 + 8 + 60 + 8 + 80 + 8 + 72 + 8 + 72 + 8 + 72;
    auto controls = headerArea.withSizeKeepingCentre(groupWidth, 34);
    presetBox.setBounds(controls.removeFromLeft(180));
    controls.removeFromLeft(8);
    designBox.setBounds(controls.removeFromLeft(110));
    controls.removeFromLeft(8);
    newPresetButton.setBounds(controls.removeFromLeft(62));
    controls.removeFromLeft(8);
    editPresetButton.setBounds(controls.removeFromLeft(60));
    controls.removeFromLeft(8);
    savePresetButton.setBounds(controls.removeFromLeft(80));
    controls.removeFromLeft(8);
    deletePresetButton.setBounds(controls.removeFromLeft(72));
    controls.removeFromLeft(8);
    importPresetButton.setBounds(controls.removeFromLeft(72));
    controls.removeFromLeft(8);
    exportPresetButton.setBounds(controls.removeFromLeft(72));

    // Left/right/bottom margins of 14, top starting exactly where the header ends.
    auto area = getLocalBounds().reduced(14).withTrimmedTop(60);
    area.removeFromTop(14);

    auto inputPanel = area.removeFromTop(142).reduced(14, 10);
    auto inputHeader = inputPanel.removeFromTop(24);
    inputKeyboardLabel.setBounds(inputHeader.removeFromLeft(120));
    scaleSummaryButton.setBounds(inputHeader.removeFromRight(110));
    selectedInputLabel.setBounds(inputHeader);
    inputPanel.removeFromTop(8);
    inputKeyboard.setBounds(inputPanel);

    area.removeFromTop(14);

    auto toolsPanel = area.removeFromTop(76).reduced(14, 10);
    toolsLabel.setBounds(toolsPanel.removeFromTop(18));
    toolsPanel.removeFromTop(8);

    auto toolsRow = toolsPanel.removeFromTop(30);

    // Undo/Redo: a segmented icon-button pair sharing one rounded background.
    auto undoRedoPair = toolsRow.removeFromLeft(64);
    segUndoRedoBounds = undoRedoPair;
    {
        auto pairArea = undoRedoPair;
        undoButton.setBounds(pairArea.removeFromLeft(32));
        redoButton.setBounds(pairArea);
    }

    toolsRow.removeFromLeft(16);

    // Octave: a segmented icon-button pair, same treatment. Invert now lives on the CHORD
    // keyboard's right-click menu, so there's no separate Inv +/- pair here.
    auto octavePair = toolsRow.removeFromLeft(64);
    segOctaveBounds = octavePair;
    {
        auto pairArea = octavePair;
        octaveDownButton.setBounds(pairArea.removeFromLeft(32));
        octaveUpButton.setBounds(pairArea);
    }

    toolsRow.removeFromLeft(16);

    compactVoicingButton.setBounds(toolsRow.removeFromLeft(90));

    // Clear/Delete sit flush to the right edge, set off by a divider — a "danger zone"
    // distinct from the transform actions on the left.
    auto dangerZone = toolsRow.removeFromRight(104 + 6 + 96);
    toolsGroupDivider = toolsRow.removeFromRight(18).withSizeKeepingCentre(1, toolsRow.getHeight());
    clearChordButton.setBounds(dangerZone.removeFromLeft(104));
    dangerZone.removeFromLeft(6);
    deleteMappingButton.setBounds(dangerZone);

    area.removeFromTop(14);

    auto outputPanel = area.removeFromTop(184).reduced(14, 10);
    auto outputHeader = outputPanel.removeFromTop(24);
    outputKeyboardLabel.setBounds(outputHeader.removeFromLeft(120));
    chordNotesLabel.setBounds(outputHeader);
    outputPanel.removeFromTop(8);
    outputKeyboard.setBounds(outputPanel);
}

//==============================================================================
void MnKordAudioProcessorEditor::comboBoxChanged(juce::ComboBox* box)
{
    if (box == &presetBox)
    {
        audioProcessor.setActivePresetIndex(presetBox.getSelectedItemIndex());
        selectedMappingIndex = -1;
        setSelectedMappingIndex(selectedMappingIndex);
        clearUndoHistory();
    }
    else if (box == &designBox)
    {
        switch (designBox.getSelectedItemIndex())
        {
            case 1:  visualTheme = VisualTheme::Frost; break;
            case 2:  visualTheme = VisualTheme::Neon; break;
            default: visualTheme = VisualTheme::Graphite; break;
        }

        applyVisualTheme();
    }
}

void MnKordAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &newPresetButton)
    {
        logUi("buttonClicked: New");
        Preset seed;
        seed.name = "Preset " + juce::String((int) audioProcessor.getPresets().size() + 1);
        promptPresetMetadata(seed, true, [this](const Preset& p, const DiatonicFillOptions& fillOptions)
        {
            Preset preset = p;
            if (fillOptions.scaleType != Scale::Chromatic)
                populateDiatonicChords(preset, fillOptions);

            // The dialog's scale choice also becomes the active INPUT-keyboard scale filter.
            scaleRoot = fillOptions.scaleRoot;
            scaleType = fillOptions.scaleType;
            updateScaleSummaryText();
            inputKeyboard.repaint();

            audioProcessor.addPreset(preset);
            selectedMappingIndex = -1;
            refreshPresetBox();
            setSelectedMappingIndex(selectedMappingIndex);
            clearUndoHistory();
        });
    }
    else if (button == &editPresetButton)
    {
        logUi("buttonClicked: Edit");
        editActivePresetMetadata();
    }
    else if (button == &savePresetButton)
    {
        logUi("buttonClicked: Save as");
        saveCurrentPresetAs();
    }
    else if (button == &deletePresetButton)
    {
        logUi("buttonClicked: Delete preset");
        const int index = audioProcessor.getActivePresetIndex();
        if (index >= 0)
        {
            audioProcessor.deletePreset(index);
            selectedMappingIndex = -1;
            refreshPresetBox();
            setSelectedMappingIndex(selectedMappingIndex);
            clearUndoHistory();
        }
    }
    else if (button == &importPresetButton)
    {
        logUi("buttonClicked: Import");
        importPresetsFromChooser();
    }
    else if (button == &exportPresetButton)
    {
        logUi("buttonClicked: Export");
        exportPresetsFromChooser();
    }
    else if (button == &scaleSummaryButton)
    {
        logUi("buttonClicked: scale chip");
        showScalePopup();
    }
    else if (button == &undoButton)
    {
        logUi("buttonClicked: Undo (stack size=" + juce::String((int) undoStack.size()) + ")");
        undo();
    }
    else if (button == &redoButton)
    {
        logUi("buttonClicked: Redo (stack size=" + juce::String((int) redoStack.size()) + ")");
        redo();
    }
    else if (button == &octaveDownButton)
    {
        logUi("buttonClicked: Octave down");
        shiftSelectedChord(-12);
    }
    else if (button == &octaveUpButton)
    {
        logUi("buttonClicked: Octave up");
        shiftSelectedChord(12);
    }
    else if (button == &compactVoicingButton)
    {
        logUi("buttonClicked: Compact");
        compactVoiceLeading();
    }
    else if (button == &clearChordButton)
    {
        logUi("buttonClicked: Clear chord");
        clearSelectedChord();
    }
    else if (button == &deleteMappingButton)
    {
        logUi("buttonClicked: Delete key");
        deleteSelectedMapping();
    }
    else
    {
        logUi("buttonClicked: UNRECOGNISED button pointer " + juce::String::toHexString((juce::pointer_sized_int) button));
    }
}

//==============================================================================
void MnKordAudioProcessorEditor::refreshPresetBox()
{
    presetBox.clear();
    const auto& presets = audioProcessor.getPresets();

    for (const auto& preset : presets)
        presetBox.addItem(preset.name, presetBox.getNumItems() + 1);

    const int activeIndex = audioProcessor.getActivePresetIndex();
    if (activeIndex >= 0 && activeIndex < (int) presets.size())
        presetBox.setSelectedItemIndex(activeIndex, juce::dontSendNotification);

    // The processor refuses to delete the last remaining preset (there'd be no active preset
    // left to edit), so grey the button out instead of leaving it a silent no-op.
    deletePresetButton.setEnabled(presets.size() > 1);

    updatePresetMetaLabel();
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::refreshDesignBox()
{
    designBox.clear();
    designBox.addItem("Graphite", 1);
    designBox.addItem("Frost", 2);
    designBox.addItem("Neon", 3);
    designBox.setSelectedItemIndex(0, juce::dontSendNotification);
}

int MnKordAudioProcessorEditor::scaleTypeIndexFor(Scale scale)
{
    switch (scale)
    {
        case Scale::Major:           return 1;
        case Scale::NaturalMinor:    return 2;
        case Scale::HarmonicMinor:   return 3;
        case Scale::MelodicMinor:    return 4;
        case Scale::MajorPentatonic: return 5;
        case Scale::MinorPentatonic: return 6;
        case Scale::Chromatic:       return 0;
    }
    return 0;
}

MnKordAudioProcessorEditor::Scale MnKordAudioProcessorEditor::scaleTypeForIndex(int index)
{
    switch (index)
    {
        case 1:  return Scale::Major;
        case 2:  return Scale::NaturalMinor;
        case 3:  return Scale::HarmonicMinor;
        case 4:  return Scale::MelodicMinor;
        case 5:  return Scale::MajorPentatonic;
        case 6:  return Scale::MinorPentatonic;
        default: return Scale::Chromatic;
    }
}

juce::String MnKordAudioProcessorEditor::scaleTypeName(Scale scale)
{
    switch (scale)
    {
        case Scale::Major:           return "Major";
        case Scale::NaturalMinor:    return "Natural minor";
        case Scale::HarmonicMinor:   return "Harmonic minor";
        case Scale::MelodicMinor:    return "Melodic minor";
        case Scale::MajorPentatonic: return "Maj pentatonic";
        case Scale::MinorPentatonic: return "Min pentatonic";
        case Scale::Chromatic:       return "Full range";
    }
    return "Full range";
}

void MnKordAudioProcessorEditor::updateScaleSummaryText()
{
    if (scaleType == Scale::Chromatic)
        scaleSummaryButton.setButtonText("Full range");
    else
        scaleSummaryButton.setButtonText(NoteName::pitchClassName(scaleRoot) + " " + scaleTypeName(scaleType));
}

MnKordAudioProcessorEditor::ScalePopupContent::ScalePopupContent(MnKordAudioProcessorEditor& ownerRef)
    : owner(ownerRef)
{
    for (int i = 0; i < 12; ++i)
        rootBox.addItem(NoteName::pitchClassName(i), i + 1);
    rootBox.setSelectedItemIndex(owner.scaleRoot, juce::dontSendNotification);

    typeBox.addItem("Full range", 1);
    typeBox.addItem("Major", 2);
    typeBox.addItem("Natural minor", 3);
    typeBox.addItem("Harmonic minor", 4);
    typeBox.addItem("Melodic minor", 5);
    typeBox.addItem("Maj pentatonic", 6);
    typeBox.addItem("Min pentatonic", 7);
    typeBox.setSelectedItemIndex(scaleTypeIndexFor(owner.scaleType), juce::dontSendNotification);

    const auto control = owner.getControlColour();
    const auto text = owner.getTextColour();
    const auto accent = owner.getAccentColour();

    for (auto* box : { &rootBox, &typeBox })
    {
        box->setColour(juce::ComboBox::backgroundColourId, control);
        box->setColour(juce::ComboBox::textColourId, text);
        box->setColour(juce::ComboBox::outlineColourId, accent.withAlpha(0.4f));
        box->addListener(this);
        addAndMakeVisible(box);
    }

    setSize(226, 34);
}

void MnKordAudioProcessorEditor::ScalePopupContent::resized()
{
    auto area = getLocalBounds();
    rootBox.setBounds(area.removeFromLeft(64));
    area.removeFromLeft(6);
    typeBox.setBounds(area);
}

void MnKordAudioProcessorEditor::ScalePopupContent::comboBoxChanged(juce::ComboBox*)
{
    owner.scaleRoot = juce::jmax(0, rootBox.getSelectedItemIndex());
    owner.scaleType = scaleTypeForIndex(typeBox.getSelectedItemIndex());
    logUi("ScalePopupContent::comboBoxChanged -> root=" + juce::String(owner.scaleRoot)
        + " type=" + scaleTypeName(owner.scaleType));
    owner.updateScaleSummaryText();
    owner.inputKeyboard.repaint();
}

void MnKordAudioProcessorEditor::showScalePopup()
{
    logUi("showScalePopup: current=" + scaleSummaryButton.getButtonText()
        + " buttonBounds=" + scaleSummaryButton.getBounds().toString()
        + " screenBounds=" + localAreaToGlobal(scaleSummaryButton.getBounds()).toString());

    auto content = std::make_unique<ScalePopupContent>(*this);
    content->setLookAndFeel(&flatLookAndFeel);
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(content),
        localAreaToGlobal(scaleSummaryButton.getBounds()), nullptr);
    box.setLookAndFeel(&flatLookAndFeel);

    logUi("showScalePopup: CallOutBox launched");
}

const std::vector<int>& MnKordAudioProcessorEditor::scaleIntervals(Scale scale)
{
    static const std::vector<int> chromaticIntervals       { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    static const std::vector<int> majorIntervals           { 0, 2, 4, 5, 7, 9, 11 };
    static const std::vector<int> naturalMinorIntervals    { 0, 2, 3, 5, 7, 8, 10 };
    static const std::vector<int> harmonicMinorIntervals   { 0, 2, 3, 5, 7, 8, 11 };
    static const std::vector<int> melodicMinorIntervals    { 0, 2, 3, 5, 7, 9, 11 };
    static const std::vector<int> majorPentatonicIntervals { 0, 2, 4, 7, 9 };
    static const std::vector<int> minorPentatonicIntervals { 0, 3, 5, 7, 10 };

    switch (scale)
    {
        case Scale::Major:           return majorIntervals;
        case Scale::NaturalMinor:    return naturalMinorIntervals;
        case Scale::HarmonicMinor:   return harmonicMinorIntervals;
        case Scale::MelodicMinor:    return melodicMinorIntervals;
        case Scale::MajorPentatonic: return majorPentatonicIntervals;
        case Scale::MinorPentatonic: return minorPentatonicIntervals;
        case Scale::Chromatic:       return chromaticIntervals;
    }

    return chromaticIntervals;
}

bool MnKordAudioProcessorEditor::isInSelectedScale(int midiNoteNumber) const
{
    if (scaleType == Scale::Chromatic)
        return true;

    const auto& intervals = scaleIntervals(scaleType);
    const int relative = ((midiNoteNumber % 12) - scaleRoot + 12) % 12;
    return std::find(intervals.begin(), intervals.end(), relative) != intervals.end();
}

void MnKordAudioProcessorEditor::populateDiatonicChords(Preset& preset, const DiatonicFillOptions& options)
{
    const auto& intervals = scaleIntervals(options.scaleType);
    const int scaleSize = (int) intervals.size();
    const std::vector<int> steps = options.chordSize >= 4
                                       ? std::vector<int> { 0, 2, 4, 6 }
                                       : std::vector<int> { 0, 2, 4 };

    for (int note = 0; note < 128; ++note)
    {
        const int relative = ((note % 12) - options.scaleRoot + 12) % 12;
        const auto degreeIt = std::find(intervals.begin(), intervals.end(), relative);
        if (degreeIt == intervals.end())
            continue;

        const int degreeIndex = (int) std::distance(intervals.begin(), degreeIt);
        const int base = note - relative;

        std::vector<int> outputNotes;
        for (const int step : steps)
        {
            const int degree = degreeIndex + step;
            const int octaveWrap = degree / scaleSize;
            const int tone = base + octaveWrap * 12 + intervals[(size_t) (degree % scaleSize)]
                            + options.octaveShift * 12;
            if (tone >= 0 && tone < 128)
                outputNotes.push_back(tone);
        }

        std::sort(outputNotes.begin(), outputNotes.end());
        outputNotes.erase(std::unique(outputNotes.begin(), outputNotes.end()), outputNotes.end());

        for (int inv = 0; inv < options.inversion; ++inv)
        {
            if (outputNotes.size() < 2)
                break;

            const auto moved = outputNotes.front() + 12;
            if (moved > 127)
                break;

            outputNotes.erase(outputNotes.begin());
            outputNotes.push_back(moved);
            std::sort(outputNotes.begin(), outputNotes.end());
        }

        NoteMapping mapping;
        mapping.inputNote = note;
        mapping.outputNotes = outputNotes;
        preset.mappings.push_back(mapping);
    }
}

void MnKordAudioProcessorEditor::applyVisualTheme()
{
    const auto control = getControlColour();
    const auto textColour = getTextColour();
    const auto transparent = juce::Colours::transparentBlack;

    for (auto* box : { &presetBox, &designBox })
    {
        box->setColour(juce::ComboBox::backgroundColourId, control);
        box->setColour(juce::ComboBox::textColourId, textColour);
        box->setColour(juce::ComboBox::outlineColourId, transparent);
        box->setColour(juce::ComboBox::focusedOutlineColourId, transparent);
    }

    // A small chip, quieter than the preset dropdowns but still visibly clickable.
    scaleSummaryButton.setColour(juce::TextButton::buttonColourId, control);
    scaleSummaryButton.setColour(juce::TextButton::buttonOnColourId, getAccentColour().darker(0.2f));
    scaleSummaryButton.setColour(juce::TextButton::textColourOffId, getMutedTextColour());
    scaleSummaryButton.setColour(juce::TextButton::textColourOnId, textColour);

    flatLookAndFeel.setColour(juce::PopupMenu::backgroundColourId, control);
    flatLookAndFeel.setColour(juce::PopupMenu::textColourId, textColour);
    flatLookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId, getAccentColour().withAlpha(0.18f));
    flatLookAndFeel.setColour(juce::PopupMenu::highlightedTextColourId, textColour);

    // LookAndFeel_V4's own colour scheme (not the individual setColour() calls above) drives
    // chrome we don't paint ourselves, such as CallOutBox's bubble/outline. Without this it
    // falls back to stock JUCE grey, clashing with the rest of the UI.
    flatLookAndFeel.setColourScheme({
        getBackgroundColour(),                    // windowBackground
        getPanelColour(),                         // widgetBackground
        control,                                  // menuBackground
        getPanelLineColour(),                     // outline
        textColour,                               // defaultText
        control,                                  // defaultFill
        textColour,                               // highlightedText
        getAccentColour(),                        // highlightedFill
        textColour                                // menuText
    });

    for (auto* button : { &newPresetButton, &editPresetButton, &savePresetButton, &deletePresetButton,
                          &importPresetButton, &exportPresetButton,
                          &compactVoicingButton,
                          &clearChordButton, &deleteMappingButton })
    {
        button->setColour(juce::TextButton::buttonColourId, control);
        button->setColour(juce::TextButton::buttonOnColourId, getAccentColour().darker(0.2f));
        button->setColour(juce::TextButton::textColourOffId, textColour);
        button->setColour(juce::TextButton::textColourOnId, textColour);
    }

    for (auto* button : { &undoButton, &redoButton, &octaveDownButton, &octaveUpButton })
    {
        button->setColour(juce::TextButton::buttonColourId, control);
        button->setColour(juce::TextButton::buttonOnColourId, getAccentColour().darker(0.2f));
        button->setColour(juce::TextButton::textColourOffId, textColour);
        button->setColour(juce::TextButton::textColourOnId, textColour);
    }

    // Destructive actions get a subtle red tint so they read as distinct from
    // the rest of the (otherwise uniform) TOOLS buttons.
    const auto dangerColour = control.interpolatedWith(juce::Colour(0xffcf4b41), 0.35f);
    for (auto* button : { &deletePresetButton, &deleteMappingButton })
        button->setColour(juce::TextButton::buttonColourId, dangerColour);

    // Compact voicing gets a subtle accent tint so it reads as the "smart" action.
    const auto accentPillColour = control.interpolatedWith(getAccentColour(), 0.2f);
    compactVoicingButton.setColour(juce::TextButton::buttonColourId, accentPillColour);
    compactVoicingButton.setColour(juce::TextButton::textColourOffId, getAccentColour().brighter(0.3f));

    for (auto* label : { &inputKeyboardLabel, &toolsLabel, &outputKeyboardLabel })
        label->setColour(juce::Label::textColourId, getMutedTextColour());

    for (auto* label : { &selectedInputLabel, &chordNotesLabel })
        label->setColour(juce::Label::textColourId, textColour);

    presetInfoLabel.setColour(juce::Label::textColourId, getMutedTextColour());

    inputKeyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, getWhiteKeyTopColour());
    inputKeyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, getBlackKeyBottomColour());
    outputKeyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, getWhiteKeyTopColour());
    outputKeyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, getBlackKeyBottomColour());

    repaint();
    inputKeyboard.repaint();
    outputKeyboard.repaint();
}

void MnKordAudioProcessorEditor::saveCurrentPresetAs()
{
    const auto& presets = audioProcessor.getPresets();
    const int index = audioProcessor.getActivePresetIndex();

    if (index < 0 || index >= (int) presets.size())
        return;

    promptPresetMetadata(presets[(size_t) index], false, [this](const Preset& p, const DiatonicFillOptions&)
    {
        audioProcessor.addPreset(p);
        selectedMappingIndex = -1;
        refreshPresetBox();
        setSelectedMappingIndex(selectedMappingIndex);
        clearUndoHistory();
    });
}

void MnKordAudioProcessorEditor::editActivePresetMetadata()
{
    const int index = audioProcessor.getActivePresetIndex();
    const auto& presets = audioProcessor.getPresets();

    if (index < 0 || index >= (int) presets.size())
        return;

    promptPresetMetadata(presets[(size_t) index], false, [this, index](const Preset& p, const DiatonicFillOptions&)
    {
        {
            const juce::ScopedLock lock(audioProcessor.getStateLock());
            auto& mutablePresets = audioProcessor.getPresets();

            if (index < 0 || index >= (int) mutablePresets.size())
                return;

            mutablePresets[(size_t) index].name = p.name;
            mutablePresets[(size_t) index].group = p.group;
            mutablePresets[(size_t) index].author = p.author;
        }

        refreshPresetBox();
        updatePresetMetaLabel();
    });
}

void MnKordAudioProcessorEditor::timerCallback()
{
    // Keep the preset combo in sync if the host changes program, or if another
    // editor instance mutates the bank.
    const int active = audioProcessor.getActivePresetIndex();
    const int count = (int) audioProcessor.getPresets().size();

    if (presetBox.getNumItems() != count
        || presetBox.getSelectedItemIndex() != active)
    {
        refreshPresetBox();
        setSelectedMappingIndex(selectedMappingIndex);
    }
}

void MnKordAudioProcessorEditor::importPresetsFromChooser()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Import mnKord preset(s)",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.xml");

    constexpr auto flags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (! file.existsAsFile())
            return;

        const int added = audioProcessor.importPresetsFromFile(file);
        if (added <= 0)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Import failed",
                "Could not read any presets from:\n" + file.getFullPathName(),
                "OK",
                this);
            return;
        }

        selectedMappingIndex = -1;
        refreshPresetBox();
        setSelectedMappingIndex(selectedMappingIndex);
        clearUndoHistory();
    });
}

void MnKordAudioProcessorEditor::exportPresetsFromChooser()
{
    const auto& presets = audioProcessor.getPresets();
    if (presets.empty())
        return;

    const int active = audioProcessor.getActivePresetIndex();
    const bool exportBank = presets.size() > 1;

    juce::PopupMenu menu;
    menu.setLookAndFeel(&flatLookAndFeel);
    if (active >= 0 && active < (int) presets.size())
        menu.addItem(1, "Export current preset…");
    menu.addItem(2, "Export entire bank…");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportPresetButton),
                       [this, active, exportBank](int result)
                       {
                           juce::ignoreUnused(exportBank);
                           if (result != 1 && result != 2)
                               return;

                           const bool bank = (result == 2);
                           const auto defaultName = bank ? "mnKord-bank.xml"
                                                         : (active >= 0 && active < (int) audioProcessor.getPresets().size()
                                                            ? audioProcessor.getPresets()[(size_t) active].name + ".xml"
                                                            : "mnKord-preset.xml");

                           fileChooser = std::make_unique<juce::FileChooser>(
                               bank ? "Export mnKord bank" : "Export mnKord preset",
                               juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(defaultName),
                               "*.xml");

                           constexpr auto flags = juce::FileBrowserComponent::saveMode
                                                | juce::FileBrowserComponent::canSelectFiles
                                                | juce::FileBrowserComponent::warnAboutOverwriting;

                           fileChooser->launchAsync(flags, [this, bank, active](const juce::FileChooser& chooser)
                           {
                               auto file = chooser.getResult();
                               if (file == juce::File())
                                   return;

                               if (! file.hasFileExtension(".xml"))
                                   file = file.withFileExtension(".xml");

                               const bool ok = bank ? audioProcessor.exportBankToFile(file)
                                                    : audioProcessor.exportPresetToFile(active, file);

                               if (! ok)
                               {
                                   juce::AlertWindow::showMessageBoxAsync(
                                       juce::MessageBoxIconType::WarningIcon,
                                       "Export failed",
                                       "Could not write:\n" + file.getFullPathName(),
                                       "OK",
                                       this);
                               }
                           });
                       });
}

void MnKordAudioProcessorEditor::promptPresetMetadata(const Preset& seed,
                                                       bool showGenerationOptions,
                                                       const std::function<void(const Preset&, const DiatonicFillOptions&)>& onSave)
{
    auto* dialog = new juce::AlertWindow("Save preset", "", juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&flatLookAndFeel);

    auto* content = new PresetMetadataContent(seed, getTextColour(), getControlColour(), getAccentColour(),
                                              showGenerationOptions, scaleRoot, scaleType);
    dialog->addCustomComponent(content);

    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create([this, dialog, content, seed, onSave](int result)
                            {
                                const auto name = content->getPresetName();
                                const auto group = content->getPresetGroup();
                                const auto author = content->getPresetAuthor();
                                const auto fillOptions = content->getFillOptions();
                                dialog->setLookAndFeel(nullptr);
                                delete content;

                                if (result != 1 || name.isEmpty())
                                    return;

                                Preset preset = seed;
                                preset.name = name;
                                preset.group = group;
                                preset.author = author;
                                onSave(preset, fillOptions);
                            }),
                            true);
}

void MnKordAudioProcessorEditor::updatePresetMetaLabel()
{
    const auto& presets = audioProcessor.getPresets();
    const int index = audioProcessor.getActivePresetIndex();

    if (index < 0 || index >= (int) presets.size())
    {
        presetInfoLabel.setText("-", juce::dontSendNotification);
        return;
    }

    const auto& preset = presets[(size_t) index];

    juce::String meta;
    if (preset.group.isNotEmpty())
        meta += preset.group;

    if (preset.author.isNotEmpty())
    {
        if (meta.isNotEmpty())
            meta += "  ·  ";
        meta += preset.author;
    }

    if (meta.isEmpty())
        meta = "-";

    presetInfoLabel.setText(meta, juce::dontSendNotification);
}

void MnKordAudioProcessorEditor::setSelectedMappingIndex(int index)
{
    auto& presets = audioProcessor.getPresets();
    const int presetIndex = audioProcessor.getActivePresetIndex();

    if (presetIndex < 0 || presetIndex >= (int) presets.size() || presets[(size_t) presetIndex].mappings.empty())
        selectedMappingIndex = -1;
    else if (index < 0)
        selectedMappingIndex = 0;
    else
        selectedMappingIndex = juce::jlimit(0, (int) presets[(size_t) presetIndex].mappings.size() - 1, index);

    refreshKeyboardHighlights();
    refreshSummaryLabels();
    updatePresetMetaLabel();
}

void MnKordAudioProcessorEditor::pushUndoSnapshot()
{
    const juce::ScopedLock lock(audioProcessor.getStateLock());

    auto& presets = audioProcessor.getPresets();
    const int presetIndex = audioProcessor.getActivePresetIndex();
    if (presetIndex < 0 || presetIndex >= (int) presets.size())
        return;

    undoStack.push_back(presets[(size_t) presetIndex].mappings);
    if (undoStack.size() > 50)
        undoStack.erase(undoStack.begin());

    redoStack.clear();
    updateUndoRedoButtons();
}

void MnKordAudioProcessorEditor::clearUndoHistory()
{
    undoStack.clear();
    redoStack.clear();
    updateUndoRedoButtons();
}

void MnKordAudioProcessorEditor::undo()
{
    if (undoStack.empty())
        return;

    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto& presets = audioProcessor.getPresets();
        const int presetIndex = audioProcessor.getActivePresetIndex();
        if (presetIndex < 0 || presetIndex >= (int) presets.size())
            return;

        redoStack.push_back(presets[(size_t) presetIndex].mappings);
        presets[(size_t) presetIndex].mappings = undoStack.back();
        undoStack.pop_back();
    }

    setSelectedMappingIndex(selectedMappingIndex);
    updateUndoRedoButtons();
}

void MnKordAudioProcessorEditor::redo()
{
    if (redoStack.empty())
        return;

    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto& presets = audioProcessor.getPresets();
        const int presetIndex = audioProcessor.getActivePresetIndex();
        if (presetIndex < 0 || presetIndex >= (int) presets.size())
            return;

        undoStack.push_back(presets[(size_t) presetIndex].mappings);
        presets[(size_t) presetIndex].mappings = redoStack.back();
        redoStack.pop_back();
    }

    setSelectedMappingIndex(selectedMappingIndex);
    updateUndoRedoButtons();
}

void MnKordAudioProcessorEditor::updateUndoRedoButtons()
{
    undoButton.setEnabled(! undoStack.empty());
    redoButton.setEnabled(! redoStack.empty());
}

bool MnKordAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    const auto isZ = key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z';
    const auto isY = key.getTextCharacter() == 'y' || key.getTextCharacter() == 'Y';

    if (key.getModifiers().isCommandDown() && isZ)
    {
        if (key.getModifiers().isShiftDown())
            redo();
        else
            undo();
        return true;
    }

    if (key.getModifiers().isCommandDown() && isY)
    {
        redo();
        return true;
    }

    return false;
}

void MnKordAudioProcessorEditor::keyboardNoteClicked(KeyboardRole role, int midiNoteNumber)
{
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto& presets = audioProcessor.getPresets();
        const int presetIndex = audioProcessor.getActivePresetIndex();

        logUi("keyboardNoteClicked: role=" + juce::String(role == KeyboardRole::input ? "input" : "output")
            + " note=" + juce::String(midiNoteNumber)
            + " presetIndex=" + juce::String(presetIndex)
            + " presetCount=" + juce::String((int) presets.size()));

        if (presetIndex < 0 || presetIndex >= (int) presets.size())
        {
            logUi("  -> EARLY RETURN: no valid active preset");
            return;
        }

        auto& preset = presets[(size_t) presetIndex];

        if (role == KeyboardRole::input)
        {
            for (int i = 0; i < (int) preset.mappings.size(); ++i)
            {
                if (preset.mappings[(size_t) i].inputNote == midiNoteNumber)
                {
                    logUi("  -> selecting existing mapping index=" + juce::String(i));
                    setSelectedMappingIndex(i);
                    logUi("  -> selectedMappingIndex now=" + juce::String(selectedMappingIndex));
                    return;
                }
            }

            pushUndoSnapshot();

            NoteMapping mapping;
            mapping.inputNote = midiNoteNumber;
            preset.mappings.push_back(mapping);
            logUi("  -> created new mapping, selecting index=" + juce::String((int) preset.mappings.size() - 1));
            setSelectedMappingIndex((int) preset.mappings.size() - 1);
            logUi("  -> selectedMappingIndex now=" + juce::String(selectedMappingIndex));
            return;
        }

        if (auto* mapping = getSelectedMapping())
        {
            pushUndoSnapshot();

            auto& notes = mapping->outputNotes;
            const auto it = std::find(notes.begin(), notes.end(), midiNoteNumber);

            if (it == notes.end())
                notes.push_back(midiNoteNumber);
            else
                notes.erase(it);

            std::sort(notes.begin(), notes.end());
            notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
        }
    }

    refreshKeyboardHighlights();
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::showChordTemplateMenu(int midiNoteNumber)
{
    // Right-clicking a key selects/creates its mapping, same as a left click would.
    keyboardNoteClicked(KeyboardRole::input, midiNoteNumber);

    juce::PopupMenu menu;
    menu.setLookAndFeel(&flatLookAndFeel);
    menu.addItem(1, "Maj");
    menu.addItem(2, "Min");
    menu.addItem(3, "Sus2");
    menu.addItem(4, "Sus4");
    menu.addItem(5, "7");
    menu.addItem(6, "Maj7");
    menu.addItem(7, "Min7");
    menu.addItem(8, "Dim");
    menu.addItem(9, "Add9");

    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        static const std::vector<int> majorIntervals         { 0, 4, 7 };
        static const std::vector<int> minorIntervals         { 0, 3, 7 };
        static const std::vector<int> sus2Intervals          { 0, 2, 7 };
        static const std::vector<int> sus4Intervals          { 0, 5, 7 };
        static const std::vector<int> seventhIntervals       { 0, 4, 7, 10 };
        static const std::vector<int> majorSeventhIntervals  { 0, 4, 7, 11 };
        static const std::vector<int> minorSeventhIntervals  { 0, 3, 7, 10 };
        static const std::vector<int> diminishedIntervals    { 0, 3, 6 };
        static const std::vector<int> addNineIntervals       { 0, 4, 7, 14 };

        switch (result)
        {
            case 1: safeThis->applyChordTemplate(majorIntervals); break;
            case 2: safeThis->applyChordTemplate(minorIntervals); break;
            case 3: safeThis->applyChordTemplate(sus2Intervals); break;
            case 4: safeThis->applyChordTemplate(sus4Intervals); break;
            case 5: safeThis->applyChordTemplate(seventhIntervals); break;
            case 6: safeThis->applyChordTemplate(majorSeventhIntervals); break;
            case 7: safeThis->applyChordTemplate(minorSeventhIntervals); break;
            case 8: safeThis->applyChordTemplate(diminishedIntervals); break;
            case 9: safeThis->applyChordTemplate(addNineIntervals); break;
            default: break;
        }
    });
}

void MnKordAudioProcessorEditor::showInvertMenu()
{
    logUi("showInvertMenu: selectedMappingIndex=" + juce::String(selectedMappingIndex)
        + " hasSelection=" + juce::String((int) (getSelectedMapping() != nullptr)));

    if (getSelectedMapping() == nullptr)
        return;

    juce::PopupMenu menu;
    menu.setLookAndFeel(&flatLookAndFeel);
    menu.addItem(1, "Invert up");
    menu.addItem(2, "Invert down");

    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result)
    {
        logUi("showInvertMenu: result=" + juce::String(result));

        if (safeThis == nullptr || result == 0)
            return;

        safeThis->invertSelectedChord(result == 1);
    });
}

void MnKordAudioProcessorEditor::applyChordTemplate(const std::vector<int>& intervals)
{
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto* mapping = getSelectedMapping();
        if (mapping == nullptr)
            return;

        pushUndoSnapshot();

        mapping->outputNotes.clear();

        for (const auto interval : intervals)
        {
            const auto note = mapping->inputNote + interval;
            if (note >= 0 && note < 128)
                mapping->outputNotes.push_back(note);
        }
    }

    refreshKeyboardHighlights();
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::invertSelectedChord(bool upwards)
{
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto* mapping = getSelectedMapping();
        if (mapping == nullptr)
        {
            logUi("invertSelectedChord(" + juce::String((int) upwards) + "): no selected mapping");
            return;
        }

        logUi("invertSelectedChord(" + juce::String(upwards ? "up" : "down") + "): before="
            + noteListToString(mapping->outputNotes));

        if (mapping->outputNotes.size() < 2)
            return;

        std::sort(mapping->outputNotes.begin(), mapping->outputNotes.end());

        if (upwards)
        {
            const auto moved = mapping->outputNotes.front() + 12;
            if (moved > 127)
                return;

            pushUndoSnapshot();
            mapping->outputNotes.erase(mapping->outputNotes.begin());
            mapping->outputNotes.push_back(moved);
        }
        else
        {
            const auto moved = mapping->outputNotes.back() - 12;
            if (moved < 0)
                return;

            pushUndoSnapshot();
            mapping->outputNotes.pop_back();
            mapping->outputNotes.insert(mapping->outputNotes.begin(), moved);
        }

        std::sort(mapping->outputNotes.begin(), mapping->outputNotes.end());
        logUi("invertSelectedChord: after=" + noteListToString(mapping->outputNotes));
    }

    refreshKeyboardHighlights();
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::shiftSelectedChord(int semitones)
{
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto* mapping = getSelectedMapping();
        if (mapping == nullptr)
            return;

        logUi("shiftSelectedChord(" + juce::String(semitones) + "): before="
            + noteListToString(mapping->outputNotes));

        if (mapping->outputNotes.empty())
            return;

        for (const auto note : mapping->outputNotes)
            if (note + semitones < 0 || note + semitones > 127)
                return;

        pushUndoSnapshot();

        for (auto& note : mapping->outputNotes)
            note += semitones;

        std::sort(mapping->outputNotes.begin(), mapping->outputNotes.end());
        logUi("shiftSelectedChord: after=" + noteListToString(mapping->outputNotes));
    }

    refreshKeyboardHighlights();
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::clearSelectedChord()
{
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto* mapping = getSelectedMapping();
        if (mapping == nullptr)
            return;

        pushUndoSnapshot();
        mapping->outputNotes.clear();
    }

    refreshKeyboardHighlights();
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::deleteSelectedMapping()
{
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto& presets = audioProcessor.getPresets();
        const int presetIndex = audioProcessor.getActivePresetIndex();

        if (presetIndex < 0 || presetIndex >= (int) presets.size())
            return;

        auto& mappings = presets[(size_t) presetIndex].mappings;
        if (selectedMappingIndex < 0 || selectedMappingIndex >= (int) mappings.size())
            return;

        pushUndoSnapshot();
        mappings.erase(mappings.begin() + selectedMappingIndex);

        if (selectedMappingIndex >= (int) mappings.size())
            selectedMappingIndex = (int) mappings.size() - 1;
    }

    setSelectedMappingIndex(selectedMappingIndex);
}

void MnKordAudioProcessorEditor::compactVoiceLeading()
{
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto& presets = audioProcessor.getPresets();
        const int presetIndex = audioProcessor.getActivePresetIndex();
        if (presetIndex < 0 || presetIndex >= (int) presets.size())
            return;

        auto& mappings = presets[(size_t) presetIndex].mappings;
        if (mappings.size() < 2)
            return;

        pushUndoSnapshot();

        std::vector<NoteMapping*> ordered;
        ordered.reserve(mappings.size());
        for (auto& mapping : mappings)
            ordered.push_back(&mapping);

        std::sort(ordered.begin(), ordered.end(), [](const NoteMapping* a, const NoteMapping* b)
        {
            return a->inputNote < b->inputNote;
        });

        for (size_t i = 1; i < ordered.size(); ++i)
        {
            const auto& previousNotes = ordered[i - 1]->outputNotes;
            auto& currentNotes = ordered[i]->outputNotes;

            if (previousNotes.empty() || currentNotes.empty())
                continue;

            double centroid = 0.0;
            for (const int note : previousNotes)
                centroid += note;
            centroid /= (double) previousNotes.size();

            std::vector<int> pitchClasses;
            for (const int note : currentNotes)
            {
                const int pitchClass = ((note % 12) + 12) % 12;
                if (std::find(pitchClasses.begin(), pitchClasses.end(), pitchClass) == pitchClasses.end())
                    pitchClasses.push_back(pitchClass);
            }

            std::vector<int> revoiced;
            for (const int pitchClass : pitchClasses)
            {
                int closest = pitchClass;
                double closestDistance = std::numeric_limits<double>::max();

                for (int candidate = pitchClass; candidate < 128; candidate += 12)
                {
                    const double distance = std::abs((double) candidate - centroid);
                    if (distance < closestDistance)
                    {
                        closestDistance = distance;
                        closest = candidate;
                    }
                }

                revoiced.push_back(closest);
            }

            std::sort(revoiced.begin(), revoiced.end());
            revoiced.erase(std::unique(revoiced.begin(), revoiced.end()), revoiced.end());
            currentNotes = revoiced;
        }
    }

    refreshKeyboardHighlights();
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::refreshKeyboardHighlights()
{
    inputKeyboardState.allNotesOff(1);
    outputKeyboardState.allNotesOff(1);

    if (const auto* mapping = getSelectedMapping())
    {
        if (mapping->inputNote >= 0 && mapping->inputNote < 128)
            inputKeyboardState.noteOn(1, mapping->inputNote, 0.9f);

        for (const auto note : mapping->outputNotes)
            if (note >= 0 && note < 128)
                outputKeyboardState.noteOn(1, note, 0.85f);
    }

    inputKeyboard.repaint();
    outputKeyboard.repaint();
}

void MnKordAudioProcessorEditor::refreshSummaryLabels()
{
    if (const auto* mapping = getSelectedMapping())
    {
        selectedInputLabel.setText(displayNoteName(mapping->inputNote), juce::dontSendNotification);
        chordNotesLabel.setText(describeChord(*mapping), juce::dontSendNotification);
    }
    else
    {
        selectedInputLabel.setText("-", juce::dontSendNotification);
        chordNotesLabel.setText("-", juce::dontSendNotification);
    }
}

bool MnKordAudioProcessorEditor::isHighlightedKey(KeyboardRole role, int midiNoteNumber) const
{
    if (const auto* mapping = getSelectedMapping())
    {
        if (role == KeyboardRole::input)
            return mapping->inputNote == midiNoteNumber;

        return std::find(mapping->outputNotes.begin(), mapping->outputNotes.end(), midiNoteNumber) != mapping->outputNotes.end();
    }

    return false;
}

bool MnKordAudioProcessorEditor::isMappedInputKey(int midiNoteNumber) const
{
    const auto& presets = audioProcessor.getPresets();
    const int presetIndex = audioProcessor.getActivePresetIndex();

    if (presetIndex < 0 || presetIndex >= (int) presets.size())
        return false;

    const auto& mappings = presets[(size_t) presetIndex].mappings;
    return std::any_of(mappings.begin(), mappings.end(), [midiNoteNumber](const NoteMapping& mapping)
    {
        return mapping.inputNote == midiNoteNumber && ! mapping.outputNotes.empty();
    });
}

juce::String MnKordAudioProcessorEditor::getOutputDegreeLabel(int midiNoteNumber) const
{
    const auto* mapping = getSelectedMapping();
    if (mapping == nullptr)
        return {};

    const auto interval = (midiNoteNumber - mapping->inputNote + 1200) % 12;

    switch (interval)
    {
        case 0:  return "R";
        case 1:  return "b2";
        case 2:  return "2";
        case 3:  return "b3";
        case 4:  return "3";
        case 5:  return "4";
        case 6:  return "b5";
        case 7:  return "5";
        case 8:  return "#5";
        case 9:  return "6";
        case 10: return "b7";
        case 11: return "7";
        default: return {};
    }
}

juce::Colour MnKordAudioProcessorEditor::getBackgroundColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xffeef2f0);
        case VisualTheme::Neon: return juce::Colour(0xff080c10);
        default: return juce::Colour(0xff101315);
    }
}

juce::Colour MnKordAudioProcessorEditor::getPanelColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xffffffff);
        case VisualTheme::Neon: return juce::Colour(0xff101820);
        default: return juce::Colour(0xff191e21);
    }
}

juce::Colour MnKordAudioProcessorEditor::getPanelLineColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xffd8dfdc);
        case VisualTheme::Neon: return juce::Colour(0xff1c2a33);
        default: return juce::Colour(0xff2a3237);
    }
}

juce::Colour MnKordAudioProcessorEditor::getTextColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xff17201b);
        case VisualTheme::Neon: return juce::Colour(0xffeffff5);
        default: return juce::Colour(0xffeef3ef);
    }
}

juce::Colour MnKordAudioProcessorEditor::getMutedTextColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xff66736d);
        case VisualTheme::Neon: return juce::Colour(0xff7a9187);
        default: return juce::Colour(0xff95a09a);
    }
}

juce::Colour MnKordAudioProcessorEditor::getAccentColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xff11b85f);
        case VisualTheme::Neon: return juce::Colour(0xff39ff88);
        default: return juce::Colour(0xff2ee66b);
    }
}

juce::Colour MnKordAudioProcessorEditor::getControlColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xffdde5e1);
        case VisualTheme::Neon: return juce::Colour(0xff16222b);
        default: return juce::Colour(0xff242b2f);
    }
}

juce::Colour MnKordAudioProcessorEditor::getWhiteKeyTopColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xffffffff);
        case VisualTheme::Neon: return juce::Colour(0xffedf2ee);
        default: return juce::Colour(0xfffbfcfb);
    }
}

juce::Colour MnKordAudioProcessorEditor::getWhiteKeyBottomColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xffdce3df);
        case VisualTheme::Neon: return juce::Colour(0xffbfc9c2);
        default: return juce::Colour(0xffd7ddd8);
    }
}

juce::Colour MnKordAudioProcessorEditor::getBlackKeyTopColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xff26302d);
        case VisualTheme::Neon: return juce::Colour(0xff1f2d35);
        default: return juce::Colour(0xff252b2e);
    }
}

juce::Colour MnKordAudioProcessorEditor::getBlackKeyBottomColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Frost: return juce::Colour(0xff0e1211);
        case VisualTheme::Neon: return juce::Colour(0xff07090b);
        default: return juce::Colour(0xff111416);
    }
}

NoteMapping* MnKordAudioProcessorEditor::getSelectedMapping()
{
    auto& presets = audioProcessor.getPresets();
    const int presetIndex = audioProcessor.getActivePresetIndex();

    if (presetIndex < 0 || presetIndex >= (int) presets.size())
        return nullptr;

    auto& mappings = presets[(size_t) presetIndex].mappings;
    if (selectedMappingIndex < 0 || selectedMappingIndex >= (int) mappings.size())
        return nullptr;

    return &mappings[(size_t) selectedMappingIndex];
}

const NoteMapping* MnKordAudioProcessorEditor::getSelectedMapping() const
{
    const auto& presets = audioProcessor.getPresets();
    const int presetIndex = audioProcessor.getActivePresetIndex();

    if (presetIndex < 0 || presetIndex >= (int) presets.size())
        return nullptr;

    const auto& mappings = presets[(size_t) presetIndex].mappings;
    if (selectedMappingIndex < 0 || selectedMappingIndex >= (int) mappings.size())
        return nullptr;

    return &mappings[(size_t) selectedMappingIndex];
}
