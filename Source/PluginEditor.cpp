#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NoteName.h"
#include "BinaryData.h"

#include <functional>

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
        // Several call sites invoke this while holding MnKordAudioProcessor::getStateLock(),
        // which processBlock() also holds (for the whole MIDI block) on the real-time audio
        // thread. FileLogger::logMessage() opens/writes/closes the log file synchronously on
        // every call, so writing here directly could stall the GUI thread mid-lock on slow
        // disk I/O and block the audio thread behind it. Defer the actual write to the next
        // message-loop iteration, by which point any such lock has already been released.
        juce::MessageManager::callAsync([message]
        {
            juce::Logger::writeToLog(message);
        });
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

    // Backs the right-click chord-template menu (showChordTemplateMenu()): both the menu-item
    // ids/intervals used to apply a template and the "does this fit the current scale" check
    // that greys out non-diatonic entries read from these same two lists.
    struct ChordTemplateOption
    {
        int id;
        const char* name;
        std::vector<int> intervals;
    };

    const std::vector<ChordTemplateOption>& triadTemplateOptions()
    {
        static const std::vector<ChordTemplateOption> options {
            { 1, "Maj",  { 0, 4, 7 } },
            { 2, "Min",  { 0, 3, 7 } },
            { 3, "Sus2", { 0, 2, 7 } },
            { 4, "Sus4", { 0, 5, 7 } },
            { 5, "Dim",  { 0, 3, 6 } },
        };
        return options;
    }

    const std::vector<ChordTemplateOption>& seventhTemplateOptions()
    {
        static const std::vector<ChordTemplateOption> options {
            { 6, "7",    { 0, 4, 7, 10 } },
            { 7, "Maj7", { 0, 4, 7, 11 } },
            { 8, "Min7", { 0, 3, 7, 10 } },
            { 9, "Add9", { 0, 4, 7, 14 } },
        };
        return options;
    }

    const std::vector<int>* chordTemplateIntervalsForId(int id)
    {
        for (const auto& option : triadTemplateOptions())
            if (option.id == id)
                return &option.intervals;

        for (const auto& option : seventhTemplateOptions())
            if (option.id == id)
                return &option.intervals;

        return nullptr;
    }

    juce::String degreeLabelForInterval(int semitonesAboveRoot)
    {
        static const char* const labels[] { "R", "b2", "2", "b3", "3", "4", "b5", "5", "#5", "6", "b7", "7" };
        return labels[((semitonesAboveRoot % 12) + 12) % 12];
    }

    // Adds the root (the mapping's input pitch class) just below a generated chord's lowest
    // note (bass, e.g. C3 under C4-E4-G4) and/or just above its highest (top, C5), at the
    // voicing's bass/top velocities. Expects outputNotes sorted.
    void addBassAndTop(NoteMapping& mapping, const MnKordAudioProcessorEditor::InitVoicing& voicing)
    {
        auto& notes = mapping.outputNotes;
        if (notes.empty())
            return;

        const int lowest = notes.front();
        const int highest = notes.back();
        const int rootPitchClass = ((mapping.inputNote % 12) + 12) % 12;

        if (voicing.addBass)
        {
            // Highest root strictly below the chord.
            const int bass = lowest - 1 - ((lowest - 1 - rootPitchClass) % 12 + 12) % 12;
            if (bass >= 0)
            {
                notes.insert(notes.begin(), bass);
                mapping.outputVelocities[bass] = voicing.bassVelocity;
            }
        }

        if (voicing.addTop)
        {
            // Lowest root strictly above the chord.
            const int top = highest + 1 + ((rootPitchClass - (highest + 1)) % 12 + 12) % 12;
            if (top <= 127)
            {
                notes.push_back(top);
                mapping.outputVelocities[top] = voicing.topVelocity;
            }
        }
    }

    // Carries an output note's custom velocity (if it has one) over to the note it moved to.
    void moveOutputVelocity(NoteMapping& mapping, int fromNote, int toNote)
    {
        const auto it = mapping.outputVelocities.find(fromNote);
        if (it == mapping.outputVelocities.end())
            return;

        const auto velocity = it->second;
        mapping.outputVelocities.erase(it);
        mapping.outputVelocities[toNote] = velocity;
    }

    // showScalePopup() and showPresetBrowser() both launch a null-parented CallOutBox (an
    // independent top-level window not torn down with the editor's component tree) whose
    // content and box point at the editor's flatLookAndFeel. If the popup is still open when
    // the editor is destroyed, clearing that pointer first makes any later repaint fall back to
    // JUCE's default LookAndFeel instead of dereferencing freed memory. Dismissing it is a
    // best-effort extra (it's an async request), not what makes this safe.
    void defusePopup(juce::Component::SafePointer<juce::CallOutBox>& popup)
    {
        if (popup == nullptr)
            return;

        if (auto* content = popup->getChildComponent(0))
            content->setLookAndFeel(nullptr);

        popup->setLookAndFeel(nullptr);
        popup->dismiss();
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
                              MnKordAudioProcessorEditor::Scale seedScaleType,
                              const MnKordAudioProcessorEditor::InitVoicing& seedVoicing)
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

                for (int i = 0; i <= 6; ++i)
                    scaleTypeBox.addItem(MnKordAudioProcessorEditor::scaleTypeName(
                        MnKordAudioProcessorEditor::scaleTypeForIndex(i)), i + 1);
                scaleTypeBox.setSelectedItemIndex(MnKordAudioProcessorEditor::scaleTypeIndexFor(seedScaleType),
                                                  juce::dontSendNotification);

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

                voicingLabel.setText("Velocity", juce::dontSendNotification);
                voicingLabel.setColour(juce::Label::textColourId, text);
                addAndMakeVisible(voicingLabel);

                voicingPanel.setColours(text, text.withAlpha(0.6f), control, accent);
                voicingPanel.setVoicing(seedVoicing);
                addAndMakeVisible(voicingPanel);
            }

            setSize(420, showGenerationOptions ? 254 + 14 + MnKordAudioProcessorEditor::InitVoicingPanel::preferredHeight
                                               : 128);
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

            area.removeFromTop(gap);

            auto voicingRow = area.removeFromTop(MnKordAudioProcessorEditor::InitVoicingPanel::preferredHeight);
            voicingLabel.setBounds(voicingRow.removeFromLeft(labelW).removeFromBottom(26));
            voicingPanel.setBounds(voicingRow);
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
            options.scaleType = MnKordAudioProcessorEditor::scaleTypeForIndex(scaleTypeBox.getSelectedItemIndex());

            options.chordSize = chordSizeBox.getSelectedItemIndex() == 1 ? 4 : 3;
            options.inversion = juce::jmax(0, inversionBox.getSelectedItemIndex());
            options.octaveShift = juce::jmax(0, octaveBox.getSelectedItemIndex()) - 2;
            options.voicing = voicingPanel.getVoicing();

            return options;
        }

    private:
        bool showGenerationOptions = false;

        juce::Label nameLabel, groupLabel, authorLabel;
        juce::TextEditor nameEditor, groupEditor, authorEditor;

        juce::Label scaleLabel, chordSizeLabel, inversionLabel, octaveLabel;
        juce::ComboBox scaleRootBox, scaleTypeBox, chordSizeBox, inversionBox, octaveBox;
        juce::Label voicingLabel;
        MnKordAudioProcessorEditor::InitVoicingPanel voicingPanel;
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
    {
        g.setColour(findColour(juce::PopupMenu::textColourId).withAlpha(0.2f));
        g.fillRect(area.withSizeKeepingCentre(area.getWidth() - 16, 1));
        return;
    }

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

void MnKordAudioProcessorEditor::FlatLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text,
                                                                             bool isSeparator,
                                                                             int standardMenuItemHeight,
                                                                             int& idealWidth,
                                                                             int& idealHeight)
{
    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 24;

    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = idealHeight / 2;
        return;
    }

    // Must match drawPopupMenuItem()'s row.reduced(28, 0) text area, plus a little breathing room.
    idealWidth = juce::GlyphArrangement::getStringWidthInt(juce::Font(juce::FontOptions(14.0f)), text) + 56 + 16;
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
    // Actual key width is set in the editor's resized() once the component's real width is
    // known, so exactly 3 octaves fill it; this is just a harmless placeholder until then.
    setKeyWidth(24.0f);
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

int MnKordAudioProcessorEditor::MappingKeyboard::selectedChordNoteAt(juce::Point<float> position)
{
    if (role != KeyboardRole::output)
        return -1;

    const auto note = getNoteAndVelocityAtPosition(position).note;
    return note >= 0 && owner.isHighlightedKey(role, note) ? note : -1;
}

void MnKordAudioProcessorEditor::MappingKeyboard::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && ! e.mods.isPopupMenu())
    {
        const auto note = selectedChordNoteAt(e.position);
        if (note >= 0)
        {
            velocityDragNote = note;
            velocityDragStart = owner.getSelectedOutputVelocity(note);
            velocityDragMoved = false;
            return;
        }
    }

    juce::MidiKeyboardComponent::mouseDown(e);
}

void MnKordAudioProcessorEditor::MappingKeyboard::mouseDrag(const juce::MouseEvent& e)
{
    if (velocityDragNote < 0)
    {
        juce::MidiKeyboardComponent::mouseDrag(e);
        return;
    }

    const auto dy = e.getDistanceFromDragStartY();
    if (std::abs(dy) >= 3)
        velocityDragMoved = true;

    if (velocityDragMoved)
        owner.setSelectedOutputVelocity(velocityDragNote, juce::jlimit(1, 127, velocityDragStart - dy / 2));

    owner.showVelocityBubble(*this, velocityDragNote);
}

void MnKordAudioProcessorEditor::MappingKeyboard::mouseUp(const juce::MouseEvent& e)
{
    if (velocityDragNote < 0)
    {
        juce::MidiKeyboardComponent::mouseUp(e);
        return;
    }

    if (! velocityDragMoved)
        owner.keyboardNoteClicked(role, velocityDragNote);

    velocityDragNote = -1;
    updateVelocityBubble(e.position);
}

void MnKordAudioProcessorEditor::MappingKeyboard::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(selectedChordNoteAt(e.position) >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                                        : juce::MouseCursor::NormalCursor);
    updateVelocityBubble(e.position);
    juce::MidiKeyboardComponent::mouseMove(e);
}

void MnKordAudioProcessorEditor::MappingKeyboard::mouseExit(const juce::MouseEvent& e)
{
    if (velocityDragNote < 0)
        owner.hideVelocityBubble();

    juce::MidiKeyboardComponent::mouseExit(e);
}

void MnKordAudioProcessorEditor::MappingKeyboard::updateVelocityBubble(juce::Point<float> position)
{
    const auto note = selectedChordNoteAt(position);
    if (note >= 0)
        owner.showVelocityBubble(*this, note);
    else
        owner.hideVelocityBubble();
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

    if (owner.isPlayingKey(role, midiNoteNumber))
    {
        g.setColour(owner.getPlayingColour());
        g.fillRect(keyArea.withTop(keyArea.getBottom() - 7.0f));
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

    if (owner.isPlayingKey(role, midiNoteNumber))
    {
        g.setColour(owner.getPlayingColour());
        g.fillRect(keyArea.withTop(keyArea.getBottom() - 7.0f));
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


    presetSummaryButton.addListener(this);
    addAndMakeVisible(presetSummaryButton);

    designBox.addListener(this);
    addAndMakeVisible(designBox);

    scaleSummaryButton.addListener(this);
    addAndMakeVisible(scaleSummaryButton);

    newPresetButton.setButtonText("New");
    editPresetButton.setButtonText("Edit");
    compactVoicingButton.setButtonText("Compact");
    savePresetButton.setButtonText("Save as");
    deletePresetButton.setButtonText("Delete");
    importPresetButton.setButtonText("Import");
    exportPresetButton.setButtonText("Export");

    for (auto* button : { &newPresetButton, &editPresetButton, &compactVoicingButton, &savePresetButton,
                          &deletePresetButton, &importPresetButton, &exportPresetButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }

    // Host/program changes and external preset edits may arrive while the editor is open.
    // (Also polls which notes are playing for the live key overlay, hence the rate.)
    startTimerHz(30);

    inputKeyboardLabel.setText("INPUT", juce::dontSendNotification);
    outputKeyboardLabel.setText("CHORD", juce::dontSendNotification);
    selectedInputLabel.setText("-", juce::dontSendNotification);
    chordNotesLabel.setText("-", juce::dontSendNotification);
    presetInfoLabel.setText("-", juce::dontSendNotification);

    for (auto* label : { &inputKeyboardLabel, &outputKeyboardLabel })
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

    velocityBubble.setInterceptsMouseClicks(false, false);
    addChildComponent(velocityBubble);

    setSize(1100, 456);

    refreshDesignBox();
    updateScaleSummaryText();
    applyVisualTheme();
    refreshPresetBox();
    setSelectedMappingIndex(selectedMappingIndex);
}

MnKordAudioProcessorEditor::~MnKordAudioProcessorEditor()
{
    stopTimer();

    defusePopup(activeScalePopup);
    defusePopup(activePresetPopup);

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
    auto bottomPanel = panelArea.removeFromTop(184.0f);

    for (const auto rect : { topPanel, bottomPanel })
    {
        g.setColour(getPanelColour());
        g.fillRoundedRectangle(rect, 8.0f);
    }
}

void MnKordAudioProcessorEditor::resized()
{
    // The header background (paint()) spans the full, unmargined width/height 0-74, so its
    // children are laid out from raw local bounds too, with margins applied per-child instead
    // of via an outer reduced() that would otherwise desync the two.
    auto headerArea = getLocalBounds().removeFromTop(74).reduced(18, 8);

    auto titleColumn = headerArea.removeFromLeft(172);
    presetInfoLabel.setBounds(titleColumn.removeFromBottom(18));

    const int groupWidth = 180 + 8 + 110 + 8 + 62 + 8 + 60 + 8 + 90 + 8 + 80 + 8 + 72 + 8 + 72 + 8 + 72;
    auto controls = headerArea.withSizeKeepingCentre(groupWidth, 34);
    presetSummaryButton.setBounds(controls.removeFromLeft(180));
    controls.removeFromLeft(8);
    designBox.setBounds(controls.removeFromLeft(110));
    controls.removeFromLeft(8);
    newPresetButton.setBounds(controls.removeFromLeft(62));
    controls.removeFromLeft(8);
    editPresetButton.setBounds(controls.removeFromLeft(60));
    controls.removeFromLeft(8);
    compactVoicingButton.setBounds(controls.removeFromLeft(90));
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
    // 5 octaves (7 white keys each) fill the visible width exactly; the rest of
    // setAvailableRange()'s range is reachable via the scroll buttons/drag. Starting at C2
    // (see setLowestVisibleKey()) puts A4/440Hz (MIDI 69) roughly in the middle of the window.
    inputKeyboard.setKeyWidth((float) inputPanel.getWidth() / (5 * 7));

    area.removeFromTop(14);

    auto outputPanel = area.removeFromTop(184).reduced(14, 10);
    auto outputHeader = outputPanel.removeFromTop(24);
    outputKeyboardLabel.setBounds(outputHeader.removeFromLeft(120));
    chordNotesLabel.setBounds(outputHeader);
    outputPanel.removeFromTop(8);
    outputKeyboard.setBounds(outputPanel);
    outputKeyboard.setKeyWidth((float) outputPanel.getWidth() / (5 * 7));
}

//==============================================================================
void MnKordAudioProcessorEditor::comboBoxChanged(juce::ComboBox* box)
{
    if (box == &designBox)
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
    if (button == &presetSummaryButton)
    {
        logUi("buttonClicked: preset chip");
        showPresetBrowser();
    }
    else if (button == &newPresetButton)
    {
        logUi("buttonClicked: New");
        Preset seed;
        seed.name = "Preset " + juce::String((int) audioProcessor.getPresets().size() + 1);
        promptPresetMetadata(seed, true, [this](const Preset& p, const DiatonicFillOptions& fillOptions)
        {
            Preset preset = p;
            if (fillOptions.scaleType != Scale::Chromatic)
                populateDiatonicChords(preset, fillOptions);

            // The dialog's scale choice also becomes the active INPUT-keyboard scale filter, and
            // its voicing the one Fill uses next.
            scaleRoot = fillOptions.scaleRoot;
            scaleType = fillOptions.scaleType;
            initVoicing = fillOptions.voicing;
            updateScaleSummaryText();
            inputKeyboard.repaint();

            audioProcessor.addPreset(preset);
            selectedMappingIndex = -1;
            refreshPresetBox();
            setSelectedMappingIndex(selectedMappingIndex);
        });
    }
    else if (button == &editPresetButton)
    {
        logUi("buttonClicked: Edit");
        editActivePresetMetadata();
    }
    else if (button == &compactVoicingButton)
    {
        logUi("buttonClicked: Compact");
        compactVoiceLeading();
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
    else
    {
        logUi("buttonClicked: UNRECOGNISED button pointer " + juce::String::toHexString((juce::pointer_sized_int) button));
    }
}

//==============================================================================
void MnKordAudioProcessorEditor::refreshPresetBox()
{
    const auto& presets = audioProcessor.getPresets();
    const int activeIndex = audioProcessor.getActivePresetIndex();

    presetSummaryButton.setButtonText(activeIndex >= 0 && activeIndex < (int) presets.size()
        ? presets[(size_t) activeIndex].name
        : "No preset");

    lastSyncedActivePresetIndex = activeIndex;
    lastSyncedPresetCount = (int) presets.size();

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

MnKordAudioProcessorEditor::InitVoicingPanel::InitVoicingPanel()
{
    static const char* const toneNames[] { "R", "3", "5", "7" };
    for (size_t i = 0; i < toneLabels.size(); ++i)
    {
        toneLabels[i].setText(toneNames[i], juce::dontSendNotification);
        toneLabels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(toneLabels[i]);
    }
    toneLabels[3].setTooltip("Seventh: only used by 7th chords");

    bassButton.setTooltip("Add the root just below each chord");
    topButton.setTooltip("Add the root just above each chord");

    for (auto* toggle : { &bassButton, &topButton })
    {
        toggle->setClickingTogglesState(true);
        toggle->onClick = [this]
        {
            velocitySliders[4].setEnabled(bassButton.getToggleState());
            velocitySliders[5].setEnabled(topButton.getToggleState());
            if (onChange)
                onChange();
        };
        addAndMakeVisible(toggle);
    }

    for (auto& slider : velocitySliders)
    {
        slider.setSliderStyle(juce::Slider::LinearBarVertical);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 0, 0);
        slider.setRange(1.0, 127.0, 1.0);
        slider.setValue(NoteMapping::defaultOutputVelocity, juce::dontSendNotification);
        slider.setTooltip("Velocity (drag up/down)");
        slider.onValueChange = [this] { if (onChange) onChange(); };
        addAndMakeVisible(slider);
    }

    velocitySliders[4].setEnabled(false);
    velocitySliders[5].setEnabled(false);
}

void MnKordAudioProcessorEditor::InitVoicingPanel::resized()
{
    auto area = getLocalBounds();
    const auto header = area.removeFromTop(22);
    area.removeFromTop(4);

    constexpr int columns = 6, gap = 4;
    const int columnWidth = (getWidth() - gap * (columns - 1)) / columns;

    for (int i = 0; i < columns; ++i)
    {
        const int x = i * (columnWidth + gap);
        const juce::Rectangle<int> headerCell(x, header.getY(), columnWidth, header.getHeight());

        if (i < 4)
            toneLabels[(size_t) i].setBounds(headerCell);
        else
            (i == 4 ? bassButton : topButton).setBounds(headerCell);

        velocitySliders[(size_t) i].setBounds(x, area.getY(), columnWidth, area.getHeight());
    }
}

void MnKordAudioProcessorEditor::InitVoicingPanel::setColours(juce::Colour text, juce::Colour muted,
                                                              juce::Colour control, juce::Colour accent)
{
    for (auto& label : toneLabels)
        label.setColour(juce::Label::textColourId, muted);

    for (auto* toggle : { &bassButton, &topButton })
    {
        toggle->setColour(juce::TextButton::buttonColourId, control);
        toggle->setColour(juce::TextButton::buttonOnColourId, accent.darker(0.2f));
        toggle->setColour(juce::TextButton::textColourOffId, muted);
        toggle->setColour(juce::TextButton::textColourOnId, text);
    }

    for (auto& slider : velocitySliders)
    {
        slider.setColour(juce::Slider::backgroundColourId, control);
        slider.setColour(juce::Slider::trackColourId, accent.withAlpha(0.35f));
        slider.setColour(juce::Slider::textBoxTextColourId, text);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
}

void MnKordAudioProcessorEditor::InitVoicingPanel::setVoicing(const InitVoicing& voicing)
{
    for (size_t i = 0; i < voicing.toneVelocities.size(); ++i)
        velocitySliders[i].setValue(voicing.toneVelocities[i], juce::dontSendNotification);

    velocitySliders[4].setValue(voicing.bassVelocity, juce::dontSendNotification);
    velocitySliders[5].setValue(voicing.topVelocity, juce::dontSendNotification);
    bassButton.setToggleState(voicing.addBass, juce::dontSendNotification);
    topButton.setToggleState(voicing.addTop, juce::dontSendNotification);
    velocitySliders[4].setEnabled(voicing.addBass);
    velocitySliders[5].setEnabled(voicing.addTop);
}

MnKordAudioProcessorEditor::InitVoicing MnKordAudioProcessorEditor::InitVoicingPanel::getVoicing() const
{
    InitVoicing voicing;
    for (size_t i = 0; i < voicing.toneVelocities.size(); ++i)
        voicing.toneVelocities[i] = (int) velocitySliders[i].getValue();

    voicing.addBass = bassButton.getToggleState();
    voicing.bassVelocity = (int) velocitySliders[4].getValue();
    voicing.addTop = topButton.getToggleState();
    voicing.topVelocity = (int) velocitySliders[5].getValue();
    return voicing;
}

MnKordAudioProcessorEditor::ScalePopupContent::ScalePopupContent(MnKordAudioProcessorEditor& ownerRef)
    : owner(&ownerRef)
{
    for (int i = 0; i < 12; ++i)
        rootBox.addItem(NoteName::pitchClassName(i), i + 1);
    rootBox.setSelectedItemIndex(owner->scaleRoot, juce::dontSendNotification);

    typeBox.addItem("Full range", 1);
    typeBox.addItem("Major", 2);
    typeBox.addItem("Natural minor", 3);
    typeBox.addItem("Harmonic minor", 4);
    typeBox.addItem("Melodic minor", 5);
    typeBox.addItem("Maj pentatonic", 6);
    typeBox.addItem("Min pentatonic", 7);
    typeBox.setSelectedItemIndex(scaleTypeIndexFor(owner->scaleType), juce::dontSendNotification);

    const auto control = owner->getControlColour();
    const auto text = owner->getTextColour();
    const auto accent = owner->getAccentColour();

    for (auto* box : { &rootBox, &typeBox })
    {
        box->setColour(juce::ComboBox::backgroundColourId, control);
        box->setColour(juce::ComboBox::textColourId, text);
        box->setColour(juce::ComboBox::outlineColourId, accent.withAlpha(0.4f));
        box->addListener(this);
        addAndMakeVisible(box);
    }

    for (auto* button : { &fillTriadsButton, &fillSeventhsButton })
    {
        button->setColour(juce::TextButton::buttonColourId, control);
        button->setColour(juce::TextButton::textColourOffId, text);
        button->setTooltip("Replace every mapping in the active preset with this scale's chords");
        addAndMakeVisible(button);
    }

    fillTriadsButton.onClick = [this] { if (owner != nullptr) owner->fillActivePresetFromScale(3); };
    fillSeventhsButton.onClick = [this] { if (owner != nullptr) owner->fillActivePresetFromScale(4); };

    addAndMakeVisible(belowStepper);
    addAndMakeVisible(aboveStepper);

    voicingPanel.setColours(text, owner->getMutedTextColour(), control, accent);
    voicingPanel.setVoicing(owner->initVoicing);
    voicingPanel.onChange = [this]
    {
        if (owner != nullptr)
            owner->initVoicing = voicingPanel.getVoicing();
    };
    addAndMakeVisible(voicingPanel);

    updateStepperCaptions();
    updateFillButtons();

    setSize(260, 136 + 6 + InitVoicingPanel::preferredHeight);
}

void MnKordAudioProcessorEditor::ScalePopupContent::resized()
{
    auto area = getLocalBounds();

    auto pickers = area.removeFromTop(34);
    rootBox.setBounds(pickers.removeFromLeft(64));
    pickers.removeFromLeft(6);
    typeBox.setBounds(pickers);

    area.removeFromTop(6);
    belowStepper.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);
    aboveStepper.setBounds(area.removeFromTop(28));

    area.removeFromTop(6);
    voicingPanel.setBounds(area.removeFromTop(InitVoicingPanel::preferredHeight));

    area.removeFromTop(6);
    const int half = (area.getWidth() - 6) / 2;
    fillTriadsButton.setBounds(area.removeFromLeft(half));
    area.removeFromLeft(6);
    fillSeventhsButton.setBounds(area);
}

void MnKordAudioProcessorEditor::ScalePopupContent::updateStepperCaptions()
{
    if (owner == nullptr)
        return;

    const auto rootName = NoteName::pitchClassName(owner->scaleRoot);
    belowStepper.captionLabel.setText("Octaves below " + rootName + "4", juce::dontSendNotification);
    aboveStepper.captionLabel.setText("Octaves from " + rootName + "5 up", juce::dontSendNotification);
}

MnKordAudioProcessorEditor::ScalePopupContent::OctaveStepper::OctaveStepper(ScalePopupContent& popupRef,
                                                                            int MnKordAudioProcessorEditor::* fieldRef)
    : popup(popupRef), field(fieldRef)
{
    const auto& owner = popup.owner;

    captionLabel.setColour(juce::Label::textColourId, owner->getMutedTextColour());
    valueLabel.setJustificationType(juce::Justification::centred);
    valueLabel.setColour(juce::Label::textColourId, owner->getTextColour());
    addAndMakeVisible(captionLabel);
    addAndMakeVisible(valueLabel);

    for (auto* button : { &downButton, &upButton })
    {
        button->setColour(juce::TextButton::buttonColourId, owner->getControlColour());
        button->setColour(juce::TextButton::textColourOffId, owner->getTextColour());
        addAndMakeVisible(button);
    }

    downButton.onClick = [this] { change(-1); };
    upButton.onClick = [this] { change(1); };
    change(0);
}

void MnKordAudioProcessorEditor::ScalePopupContent::OctaveStepper::resized()
{
    auto row = getLocalBounds();
    upButton.setBounds(row.removeFromRight(28));
    valueLabel.setBounds(row.removeFromRight(30));
    downButton.setBounds(row.removeFromRight(28));
    row.removeFromRight(4);
    captionLabel.setBounds(row);
}

void MnKordAudioProcessorEditor::ScalePopupContent::OctaveStepper::change(int delta)
{
    auto* editor = popup.owner.getComponent();
    if (editor == nullptr)
        return;

    auto& value = editor->*field;
    value = juce::jlimit(0, maxFillOctaves, value + delta);
    valueLabel.setText(juce::String(value), juce::dontSendNotification);
    downButton.setEnabled(value > 0);
    upButton.setEnabled(value < maxFillOctaves);
}

void MnKordAudioProcessorEditor::ScalePopupContent::updateFillButtons()
{
    const bool hasScale = owner != nullptr && owner->scaleType != Scale::Chromatic;
    fillTriadsButton.setEnabled(hasScale);
    fillSeventhsButton.setEnabled(hasScale);
}

void MnKordAudioProcessorEditor::ScalePopupContent::comboBoxChanged(juce::ComboBox*)
{
    // The CallOutBox hosting this is a top-level window (see showScalePopup()), so it can
    // outlive the editor if the host closes the plugin UI while it's still open.
    if (owner == nullptr)
        return;

    owner->scaleRoot = juce::jmax(0, rootBox.getSelectedItemIndex());
    owner->scaleType = scaleTypeForIndex(typeBox.getSelectedItemIndex());
    logUi("ScalePopupContent::comboBoxChanged -> root=" + juce::String(owner->scaleRoot)
        + " type=" + scaleTypeName(owner->scaleType));
    owner->updateScaleSummaryText();
    owner->inputKeyboard.repaint();
    updateStepperCaptions();
    updateFillButtons();
}

void MnKordAudioProcessorEditor::showScalePopup()
{
    const auto screenBounds = localAreaToGlobal(scaleSummaryButton.getBounds());

    logUi("showScalePopup: current=" + scaleSummaryButton.getButtonText()
        + " buttonBounds=" + scaleSummaryButton.getBounds().toString()
        + " screenBounds=" + screenBounds.toString());

    auto content = std::make_unique<ScalePopupContent>(*this);
    content->setLookAndFeel(&flatLookAndFeel);
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
    box.setLookAndFeel(&flatLookAndFeel);
    activeScalePopup = &box;

    logUi("showScalePopup: CallOutBox launched");
}

namespace
{
    juce::String groupLabelFor(const Preset& preset)
    {
        return preset.group.isNotEmpty() ? preset.group : juce::String("Ungrouped");
    }

    juce::String authorLabelFor(const Preset& preset)
    {
        return preset.author.isNotEmpty() ? preset.author : juce::String("No author");
    }

    // Fills a filter dropdown with "All <label>s" (id 1) plus one item per distinct value
    // toStringId returns for a preset (in first-appearance order).
    void populateFilterBox(juce::ComboBox& box, const std::vector<Preset>& presets,
                           const juce::String& allLabel,
                           const std::function<juce::String(const Preset&)>& labelFor)
    {
        box.addItem(allLabel, 1);

        juce::StringArray valuesInOrder;
        for (const auto& preset : presets)
            valuesInOrder.addIfNotAlreadyThere(labelFor(preset));

        for (int i = 0; i < valuesInOrder.size(); ++i)
            box.addItem(valuesInOrder[i], i + 2);

        box.setSelectedId(1, juce::dontSendNotification);
    }
}

MnKordAudioProcessorEditor::PresetBrowserContent::PresetBrowserContent(MnKordAudioProcessorEditor& ownerRef)
    : owner(&ownerRef)
{
    const auto& presets = owner->audioProcessor.getPresets();
    populateFilterBox(groupFilterBox, presets, "All groups", groupLabelFor);
    populateFilterBox(authorFilterBox, presets, "All authors", authorLabelFor);

    for (auto* box : { &groupFilterBox, &authorFilterBox })
    {
        box->setColour(juce::ComboBox::backgroundColourId, owner->getControlColour());
        box->setColour(juce::ComboBox::textColourId, owner->getTextColour());
        box->setColour(juce::ComboBox::outlineColourId, owner->getAccentColour().withAlpha(0.4f));
        box->addListener(this);
        addAndMakeVisible(box);
    }

    searchBox.setTextToShowWhenEmpty("Search presets...", owner->getMutedTextColour());
    searchBox.setColour(juce::TextEditor::backgroundColourId, owner->getControlColour());
    searchBox.setColour(juce::TextEditor::textColourId, owner->getTextColour());
    searchBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    searchBox.setColour(juce::TextEditor::focusedOutlineColourId, owner->getAccentColour());
    searchBox.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible(searchBox);

    listBox.setRowHeight(26);
    listBox.setColour(juce::ListBox::backgroundColourId, owner->getPanelColour());
    listBox.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(listBox);

    rebuildRows();
    setSize(260, 374);
}

void MnKordAudioProcessorEditor::PresetBrowserContent::resized()
{
    auto area = getLocalBounds().reduced(8);
    groupFilterBox.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
    authorFilterBox.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
    searchBox.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);
    listBox.setBounds(area);
}

void MnKordAudioProcessorEditor::PresetBrowserContent::comboBoxChanged(juce::ComboBox*)
{
    rebuildRows();
}

void MnKordAudioProcessorEditor::PresetBrowserContent::rebuildRows()
{
    rows.clear();

    // The CallOutBox hosting this is a top-level window (see showPresetBrowser()), so it can
    // outlive the editor if the host closes the plugin UI while it's still open.
    if (owner != nullptr)
    {
        const auto& presets = owner->audioProcessor.getPresets();
        const int activeIndex = owner->audioProcessor.getActivePresetIndex();
        const auto filter = searchBox.getText().trim();
        const auto selectedGroupId = groupFilterBox.getSelectedId();
        const auto selectedGroupText = selectedGroupId > 1 ? groupFilterBox.getText() : juce::String();
        const auto selectedAuthorId = authorFilterBox.getSelectedId();
        const auto selectedAuthorText = selectedAuthorId > 1 ? authorFilterBox.getText() : juce::String();

        // A preset stays listed while searching if it matches, OR if it's the active preset --
        // so a search that doesn't match it can't silently orphan the current selection. Both
        // filter dropdowns apply the same exception, for the same reason.
        auto isShown = [&](int index)
        {
            if (index == activeIndex)
                return true;

            const auto& preset = presets[(size_t) index];

            if (selectedGroupId > 1 && groupLabelFor(preset) != selectedGroupText)
                return false;

            if (selectedAuthorId > 1 && authorLabelFor(preset) != selectedAuthorText)
                return false;

            return filter.isEmpty() || preset.name.containsIgnoreCase(filter)
                                     || preset.group.containsIgnoreCase(filter)
                                     || preset.author.containsIgnoreCase(filter);
        };

        juce::StringArray groupsInOrder;
        for (int i = 0; i < (int) presets.size(); ++i)
            if (isShown(i))
                groupsInOrder.addIfNotAlreadyThere(groupLabelFor(presets[(size_t) i]));

        // A single selected group is already named by groupFilterBox -- repeating it as a row
        // heading would be redundant, so only show headings when browsing "All groups".
        const bool showHeadings = selectedGroupId <= 1;

        for (const auto& group : groupsInOrder)
        {
            if (showHeadings)
                rows.push_back({ true, group, -1 });

            for (int i = 0; i < (int) presets.size(); ++i)
                if (isShown(i) && groupLabelFor(presets[(size_t) i]) == group)
                    rows.push_back({ false, presets[(size_t) i].name, i });
        }
    }

    listBox.updateContent();
    listBox.repaint();
}

int MnKordAudioProcessorEditor::PresetBrowserContent::getNumRows()
{
    return (int) rows.size();
}

void MnKordAudioProcessorEditor::PresetBrowserContent::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                                        int width, int height, bool rowIsSelected)
{
    if (owner == nullptr || rowNumber < 0 || rowNumber >= (int) rows.size())
        return;

    const auto& row = rows[(size_t) rowNumber];

    if (row.isGroupHeading)
    {
        g.setColour(owner->getMutedTextColour());
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(row.text.toUpperCase(), 10, 0, width - 10, height, juce::Justification::centredLeft);
        return;
    }

    if (rowIsSelected)
    {
        g.setColour(owner->getAccentColour().withAlpha(0.18f));
        g.fillRect(0, 0, width, height);
    }

    const bool isActive = row.presetIndex == owner->audioProcessor.getActivePresetIndex();
    g.setColour(isActive ? owner->getAccentColour() : owner->getTextColour());
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(row.text, 18, 0, width - 26, height, juce::Justification::centredLeft, true);
}

void MnKordAudioProcessorEditor::PresetBrowserContent::listBoxItemClicked(int rowNumber, const juce::MouseEvent&)
{
    if (owner == nullptr || rowNumber < 0 || rowNumber >= (int) rows.size())
        return;

    const auto& row = rows[(size_t) rowNumber];

    if (row.isGroupHeading)
    {
        listBox.deselectAllRows();
        return;
    }

    owner->selectActivePreset(row.presetIndex);

    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void MnKordAudioProcessorEditor::showPresetBrowser()
{
    const auto screenBounds = localAreaToGlobal(presetSummaryButton.getBounds());

    auto content = std::make_unique<PresetBrowserContent>(*this);
    content->setLookAndFeel(&flatLookAndFeel);
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(content), screenBounds, nullptr);
    box.setLookAndFeel(&flatLookAndFeel);
    activePresetPopup = &box;
}

void MnKordAudioProcessorEditor::selectActivePreset(int index)
{
    audioProcessor.setActivePresetIndex(index);
    selectedMappingIndex = -1;
    setSelectedMappingIndex(selectedMappingIndex);
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

    for (int note = juce::jmax(0, options.lowestInputNote); note <= juce::jmin(127, options.highestInputNote); ++note)
    {
        const int relative = ((note % 12) - options.scaleRoot + 12) % 12;
        const auto degreeIt = std::find(intervals.begin(), intervals.end(), relative);
        if (degreeIt == intervals.end())
            continue;

        const int degreeIndex = (int) std::distance(intervals.begin(), degreeIt);
        const int base = note - relative;

        NoteMapping mapping;
        mapping.inputNote = note;
        auto& outputNotes = mapping.outputNotes;

        // Each stacked tone (R, 3, 5, 7 = position in the stack) starts at its role's velocity.
        for (size_t role = 0; role < steps.size(); ++role)
        {
            const int degree = degreeIndex + steps[role];
            const int octaveWrap = degree / scaleSize;
            const int tone = base + octaveWrap * 12 + intervals[(size_t) (degree % scaleSize)]
                            + options.octaveShift * 12;
            if (tone >= 0 && tone < 128 && mapping.outputVelocities.count(tone) == 0)
            {
                outputNotes.push_back(tone);
                mapping.outputVelocities[tone] = options.voicing.toneVelocities[role];
            }
        }

        std::sort(outputNotes.begin(), outputNotes.end());

        for (int inv = 0; inv < options.inversion; ++inv)
        {
            if (outputNotes.size() < 2)
                break;

            const auto moved = outputNotes.front() + 12;
            if (moved > 127)
                break;

            moveOutputVelocity(mapping, outputNotes.front(), moved);
            outputNotes.erase(outputNotes.begin());
            outputNotes.push_back(moved);
            std::sort(outputNotes.begin(), outputNotes.end());
        }

        addBassAndTop(mapping, options.voicing);
        preset.mappings.push_back(mapping);
    }
}

void MnKordAudioProcessorEditor::fillActivePresetFromScale(int chordSize)
{
    if (scaleType == Scale::Chromatic)
        return;

    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto& presets = audioProcessor.getPresets();
        const int index = audioProcessor.getActivePresetIndex();
        if (index < 0 || index >= (int) presets.size())
            return;

        DiatonicFillOptions options;
        options.scaleRoot = scaleRoot;
        options.scaleType = scaleType;
        options.chordSize = chordSize;
        const int middleRoot = 60 + scaleRoot;                               // root in octave 4
        options.lowestInputNote = middleRoot - 12 * fillOctavesBelow;
        options.highestInputNote = middleRoot + 11 + 12 * fillOctavesAbove; // note before next root
        options.voicing = initVoicing;

        auto& preset = presets[(size_t) index];
        preset.mappings.clear();
        populateDiatonicChords(preset, options);
    }

    selectedMappingIndex = -1;
    setSelectedMappingIndex(selectedMappingIndex);
}

void MnKordAudioProcessorEditor::applyVisualTheme()
{
    const auto control = getControlColour();
    const auto textColour = getTextColour();
    const auto transparent = juce::Colours::transparentBlack;

    designBox.setColour(juce::ComboBox::backgroundColourId, control);
    designBox.setColour(juce::ComboBox::textColourId, textColour);
    designBox.setColour(juce::ComboBox::outlineColourId, transparent);
    designBox.setColour(juce::ComboBox::focusedOutlineColourId, transparent);

    // A small chip, quieter than a full dropdown but still visibly clickable.
    scaleSummaryButton.setColour(juce::TextButton::buttonColourId, control);
    scaleSummaryButton.setColour(juce::TextButton::buttonOnColourId, getAccentColour().darker(0.2f));
    scaleSummaryButton.setColour(juce::TextButton::textColourOffId, getMutedTextColour());
    scaleSummaryButton.setColour(juce::TextButton::textColourOnId, textColour);
    // The primary preset selector, so its text reads at full strength (unlike the muted
    // scale chip above), matching how the preset name looked in the combo box it replaced.
    presetSummaryButton.setColour(juce::TextButton::buttonColourId, control);
    presetSummaryButton.setColour(juce::TextButton::buttonOnColourId, getAccentColour().darker(0.2f));
    presetSummaryButton.setColour(juce::TextButton::textColourOffId, textColour);
    presetSummaryButton.setColour(juce::TextButton::textColourOnId, textColour);

    // LookAndFeel_V4's own colour scheme (not individual setColour() calls) drives chrome we
    // don't paint ourselves, such as CallOutBox's bubble/outline. Without this it falls back to
    // stock JUCE grey, clashing with the rest of the UI. setColourScheme() internally calls
    // setColour() for a fixed set of ids (including the PopupMenu ones below), so it must run
    // *before* our own explicit overrides or it would silently clobber them.
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

    flatLookAndFeel.setColour(juce::PopupMenu::backgroundColourId, control);
    flatLookAndFeel.setColour(juce::PopupMenu::textColourId, textColour);
    flatLookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId, getAccentColour().withAlpha(0.18f));
    flatLookAndFeel.setColour(juce::PopupMenu::highlightedTextColourId, textColour);

    for (auto* button : { &newPresetButton, &editPresetButton, &compactVoicingButton, &savePresetButton,
                          &deletePresetButton, &importPresetButton, &exportPresetButton })
    {
        button->setColour(juce::TextButton::buttonColourId, control);
        button->setColour(juce::TextButton::buttonOnColourId, getAccentColour().darker(0.2f));
        button->setColour(juce::TextButton::textColourOffId, textColour);
        button->setColour(juce::TextButton::textColourOnId, textColour);
    }

    // Destructive action gets a subtle red tint so it reads as distinct from the rest.
    const auto dangerColour = control.interpolatedWith(juce::Colour(0xffcf4b41), 0.35f);
    deletePresetButton.setColour(juce::TextButton::buttonColourId, dangerColour);

    // Compact voicing gets a subtle accent tint so it reads as the "smart" action.
    const auto accentPillColour = control.interpolatedWith(getAccentColour(), 0.2f);
    compactVoicingButton.setColour(juce::TextButton::buttonColourId, accentPillColour);
    compactVoicingButton.setColour(juce::TextButton::textColourOffId, getAccentColour().brighter(0.3f));

    for (auto* label : { &inputKeyboardLabel, &outputKeyboardLabel })
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

            // Bounds-check alone isn't enough: while the dialog was open, a host program
            // change or another mutation could have made `index` point at a *different*
            // preset than the one the user opened "Edit" on. Only apply the edit if the
            // active preset is still the one we started editing.
            if (index < 0 || index >= (int) mutablePresets.size()
                || audioProcessor.getActivePresetIndex() != index)
                return;

            mutablePresets[(size_t) index].name = p.name;
            mutablePresets[(size_t) index].group = p.group;
            mutablePresets[(size_t) index].author = p.author;
        }

        // refreshPresetBox() already calls updatePresetMetaLabel() internally.
        refreshPresetBox();
    });
}

void MnKordAudioProcessorEditor::timerCallback()
{
    // Keep the preset combo in sync if the host changes program, or if another
    // editor instance mutates the bank.
    const int active = audioProcessor.getActivePresetIndex();
    const int count = (int) audioProcessor.getPresets().size();

    if (active != lastSyncedActivePresetIndex || count != lastSyncedPresetCount)
    {
        refreshPresetBox();
        setSelectedMappingIndex(selectedMappingIndex);
    }

    const auto input = audioProcessor.getActiveInputNotes();
    if (input != playingInputNotes)
    {
        playingInputNotes = input;
        inputKeyboard.repaint();
    }

    const auto output = audioProcessor.getActiveOutputNotes();
    if (output != playingOutputNotes)
    {
        playingOutputNotes = output;
        outputKeyboard.repaint();
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
    });
}

void MnKordAudioProcessorEditor::exportPresetsFromChooser()
{
    const auto& presets = audioProcessor.getPresets();
    const int active = audioProcessor.getActivePresetIndex();
    if (active < 0 || active >= (int) presets.size())
        return;

    const auto defaultName = presets[(size_t) active].name + ".xml";

    fileChooser = std::make_unique<juce::FileChooser>(
        "Export mnKord preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(defaultName),
        "*.xml");

    constexpr auto flags = juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync(flags, [this, active](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return;

        if (! file.hasFileExtension(".xml"))
            file = file.withFileExtension(".xml");

        if (! audioProcessor.exportPresetToFile(active, file))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Export failed",
                "Could not write:\n" + file.getFullPathName(),
                "OK",
                this);
        }
    });
}

void MnKordAudioProcessorEditor::promptPresetMetadata(const Preset& seed,
                                                       bool showGenerationOptions,
                                                       const std::function<void(const Preset&, const DiatonicFillOptions&)>& onSave)
{
    auto* dialog = new juce::AlertWindow("Save preset", "", juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&flatLookAndFeel);

    auto* content = new PresetMetadataContent(seed, getTextColour(), getControlColour(), getAccentColour(),
                                              showGenerationOptions, scaleRoot, scaleType, initVoicing);
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

juce::PopupMenu MnKordAudioProcessorEditor::makeStyledMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&flatLookAndFeel);
    return menu;
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
            auto& notes = mapping->outputNotes;
            const auto it = std::find(notes.begin(), notes.end(), midiNoteNumber);

            if (it == notes.end())
                notes.push_back(midiNoteNumber);
            else
                notes.erase(it);

            // Added or removed either way: a re-added note starts again at the default.
            mapping->outputVelocities.erase(midiNoteNumber);

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

    const auto* mapping = getSelectedMapping();
    const bool hasNotes = mapping != nullptr && ! mapping->outputNotes.empty();
    const int rootNote = mapping != nullptr ? mapping->inputNote : midiNoteNumber;

    // A template is greyed out (not hidden -- same treatment as Octave/Clear below) when
    // applying it would add a note outside the currently selected scale. With no scale
    // restriction (Full range) every template is always available.
    auto fitsScale = [this, rootNote](const std::vector<int>& intervals)
    {
        if (scaleType == Scale::Chromatic)
            return true;

        for (const auto interval : intervals)
            if (! isInSelectedScale(rootNote + interval))
                return false;

        return true;
    };

    auto menu = makeStyledMenu();
    menu.addSectionHeader("Triad");
    for (const auto& option : triadTemplateOptions())
        menu.addItem(option.id, option.name, fitsScale(option.intervals));

    menu.addSeparator();
    menu.addSectionHeader("Seventh");
    for (const auto& option : seventhTemplateOptions())
        menu.addItem(option.id, option.name, fitsScale(option.intervals));

    const bool anyNoteOutOfScale = scaleType != Scale::Chromatic && mapping != nullptr
        && std::any_of(mapping->outputNotes.begin(), mapping->outputNotes.end(), [this](int note)
                       { return ! isInSelectedScale(note); });

    menu.addSeparator();
    menu.addSectionHeader("Chord");
    menu.addItem(10, "Octave up", hasNotes);
    menu.addItem(11, "Octave down", hasNotes);
    menu.addItem(12, "Clear chord", hasNotes);
    menu.addItem(14, "Fit to scale", anyNoteOutOfScale);

    menu.addSeparator();
    menu.addColouredItem(13, "Delete mapping", juce::Colour(0xffcf4b41));

    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (const auto* intervals = chordTemplateIntervalsForId(result))
        {
            safeThis->applyChordTemplate(*intervals);
            return;
        }

        switch (result)
        {
            case 10: safeThis->shiftSelectedChord(12); break;
            case 11: safeThis->shiftSelectedChord(-12); break;
            case 12: safeThis->clearSelectedChord(); break;
            case 13: safeThis->deleteSelectedMapping(); break;
            case 14: safeThis->fitSelectedChordToScale(); break;
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

    auto menu = makeStyledMenu();
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

        mapping->outputNotes.clear();
        mapping->outputVelocities.clear();

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

            moveOutputVelocity(*mapping, mapping->outputNotes.front(), moved);
            mapping->outputNotes.erase(mapping->outputNotes.begin());
            mapping->outputNotes.push_back(moved);
        }
        else
        {
            const auto moved = mapping->outputNotes.back() - 12;
            if (moved < 0)
                return;

            moveOutputVelocity(*mapping, mapping->outputNotes.back(), moved);
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
        if (mapping == nullptr || mapping->outputNotes.empty())
            return;

        for (const auto note : mapping->outputNotes)
            if (note + semitones < 0 || note + semitones > 127)
                return;

        for (auto& note : mapping->outputNotes)
            note += semitones;

        std::map<int, int> shiftedVelocities;
        for (const auto& [note, velocity] : mapping->outputVelocities)
            shiftedVelocities[note + semitones] = velocity;
        mapping->outputVelocities = std::move(shiftedVelocities);

        std::sort(mapping->outputNotes.begin(), mapping->outputNotes.end());
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

        mapping->outputNotes.clear();
        mapping->outputVelocities.clear();
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

        mappings.erase(mappings.begin() + selectedMappingIndex);

        if (selectedMappingIndex >= (int) mappings.size())
            selectedMappingIndex = (int) mappings.size() - 1;
    }

    setSelectedMappingIndex(selectedMappingIndex);
}

void MnKordAudioProcessorEditor::fitSelectedChordToScale()
{
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        if (scaleType == Scale::Chromatic)
            return;

        auto* mapping = getSelectedMapping();
        if (mapping == nullptr || mapping->outputNotes.empty())
            return;

        for (auto& note : mapping->outputNotes)
        {
            if (isInSelectedScale(note))
                continue;

            int nearest = note;
            int nearestDistance = 128;

            for (int candidate = 0; candidate < 128; ++candidate)
            {
                if (! isInSelectedScale(candidate))
                    continue;

                const int distance = std::abs(candidate - note);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearest = candidate;
                }
            }

            moveOutputVelocity(*mapping, note, nearest);
            note = nearest;
        }

        std::sort(mapping->outputNotes.begin(), mapping->outputNotes.end());
        mapping->outputNotes.erase(std::unique(mapping->outputNotes.begin(), mapping->outputNotes.end()),
                                   mapping->outputNotes.end());
    }

    refreshKeyboardHighlights();
    refreshSummaryLabels();
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

            // Each chord tone (by pitch class) moves to whichever octave sits closest to the
            // previous chord's centroid. No note is forced to be "the bass" -- that's what lets
            // a chord like iii come out inverted (e.g. B-E-G rather than E-G-B) when that's the
            // closest voicing to what came before, matching the scale root's chord it's chained
            // from rather than each chord's own root position.
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
                double closestDistance = 1.0e9;

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

            // Revoicing only changes octaves, so each note keeps the velocity of its pitch class.
            auto& velocities = ordered[i]->outputVelocities;
            std::map<int, int> velocityByPitchClass;
            for (const auto& [note, velocity] : velocities)
                velocityByPitchClass[((note % 12) + 12) % 12] = velocity;

            velocities.clear();
            for (const int note : revoiced)
                if (const auto it = velocityByPitchClass.find(note % 12); it != velocityByPitchClass.end())
                    velocities[note] = it->second;

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

int MnKordAudioProcessorEditor::getSelectedOutputVelocity(int outputNote) const
{
    const auto* mapping = getSelectedMapping();
    return mapping != nullptr ? mapping->velocityFor(outputNote) : NoteMapping::defaultOutputVelocity;
}

void MnKordAudioProcessorEditor::setSelectedOutputVelocity(int outputNote, int velocity)
{
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto* mapping = getSelectedMapping();
        if (mapping == nullptr)
            return;

        mapping->outputVelocities[outputNote] = juce::jlimit(1, 127, velocity);
    }

    outputKeyboard.repaint();
}

void MnKordAudioProcessorEditor::showVelocityBubble(const MappingKeyboard& keyboard, int outputNote)
{
    const auto key = getLocalArea(&keyboard, keyboard.getRectangleForKey(outputNote)).toNearestInt();

    const int height = VelocityBubble::bodyHeight + VelocityBubble::pointerHeight;
    const int x = juce::jlimit(0, getWidth() - VelocityBubble::width, key.getCentreX() - VelocityBubble::width / 2);
    velocityBubble.setBounds(x, key.getY() + 4 - height, VelocityBubble::width, height);

    velocityBubble.velocity = getSelectedOutputVelocity(outputNote);
    velocityBubble.setVisible(true);
    velocityBubble.toFront(false);
    velocityBubble.repaint();
}

void MnKordAudioProcessorEditor::hideVelocityBubble()
{
    velocityBubble.setVisible(false);
}

void MnKordAudioProcessorEditor::VelocityBubble::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto body = bounds.withHeight((float) bodyHeight).reduced(0.5f);

    juce::Path shape;
    shape.addRoundedRectangle(body, 6.0f);
    const auto tipX = bounds.getCentreX();
    shape.addTriangle(tipX - (float) pointerHeight, body.getBottom() - 0.5f,
                      tipX + (float) pointerHeight, body.getBottom() - 0.5f,
                      tipX, bounds.getBottom());

    g.setColour(owner.getPanelColour().brighter(0.12f));
    g.fillPath(shape);
    g.setColour(owner.getAccentColour().withAlpha(0.7f));
    g.strokePath(shape, juce::PathStrokeType(1.0f));

    auto text = body.reduced(2.0f, 3.0f);
    g.setColour(owner.getMutedTextColour());
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText("VEL", text.removeFromTop(10.0f), juce::Justification::centred, false);

    g.setColour(owner.getTextColour());
    g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    g.drawText(juce::String(velocity), text, juce::Justification::centred, false);
}

bool MnKordAudioProcessorEditor::isPlayingKey(KeyboardRole role, int midiNoteNumber) const
{
    return role == KeyboardRole::input ? playingInputNotes.contains(midiNoteNumber)
                                       : playingOutputNotes.contains(midiNoteNumber);
}

juce::Colour MnKordAudioProcessorEditor::getPlayingColour() const
{
    return juce::Colour(0xffffa53a);
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

    return degreeLabelForInterval(midiNoteNumber - mapping->inputNote);
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
