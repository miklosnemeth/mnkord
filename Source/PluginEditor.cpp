#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NoteName.h"
#include "ChordSymbol.h"
#include "BinaryData.h"
#include "MnKordVersion.h"

#include <functional>

namespace
{
    juce::String displayNoteName(int midiNote)
    {
        // Delegate to the shared NoteName utility so octave numbering is consistent
        // across the whole plugin (MIDI 60 == "C4").
        return NoteName::toString(midiNote);
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

    // showScalePopup() and showPresetBrowser() both launch a CallOutBox owned by JUCE's modal
    // machinery rather than the editor (only parented to it for display), whose content and
    // box point at the editor's flatLookAndFeel. If the popup is still open when
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
            tagsLabel.setText("Tags", juce::dontSendNotification);

            nameEditor.setText(seedPreset.name);
            groupEditor.setText(seedPreset.group);
            authorEditor.setText(seedPreset.author);
            tagsEditor.setText(seedPreset.tags.joinIntoString(", "));
            tagsEditor.setTextToShowWhenEmpty("Pop, Jazz, Ballad (comma separated)", text.withAlpha(0.4f));

            for (auto* label : { &nameLabel, &groupLabel, &authorLabel, &tagsLabel,
                                 &scaleLabel, &chordSizeLabel, &inversionLabel, &octaveLabel })
                label->setColour(juce::Label::textColourId, text);

            for (auto* e : { &nameEditor, &groupEditor, &authorEditor, &tagsEditor })
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
            addAndMakeVisible(tagsLabel);
            addAndMakeVisible(nameEditor);
            addAndMakeVisible(groupEditor);
            addAndMakeVisible(authorEditor);
            addAndMakeVisible(tagsEditor);

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

            // Rows are 28px with 10px gaps; tight enough for the dialog to fit inside the plugin
            // window, which it's now embedded in.
            setSize(420, showGenerationOptions ? 7 * 28 + 7 * 10 + MnKordAudioProcessorEditor::InitVoicingPanel::preferredHeight
                                               : 4 * 28 + 3 * 10);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            const int rowH = 28;
            const int gap = 10;
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

            area.removeFromTop(gap);

            auto tagsRow = area.removeFromTop(rowH);
            tagsLabel.setBounds(tagsRow.removeFromLeft(labelW));
            tagsEditor.setBounds(tagsRow);

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

        juce::StringArray getPresetTags() const
        {
            juce::StringArray tags;
            tags.addTokens(tagsEditor.getText(), ",;", "");
            tags.trim();
            tags.removeEmptyStrings();
            tags.removeDuplicates(true);
            return tags;
        }

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

        juce::Label nameLabel, groupLabel, authorLabel, tagsLabel;
        juce::TextEditor nameEditor, groupEditor, authorEditor, tagsEditor;

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

juce::Component* MnKordAudioProcessorEditor::FlatLookAndFeel::getParentComponentForMenuOptions(const juce::PopupMenu::Options& options)
{
    if (auto* parent = options.getParentComponent())
        return parent;

    if (auto* target = options.getTargetComponent())
        return target->findParentComponentOfClass<juce::AudioProcessorEditor>();

    return nullptr;
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
    // JUCE labels MIDI 60 as "C3" by default; the rest of mnKord (NoteName) calls it C4.
    setOctaveForMiddleC(4);
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
    if (role == KeyboardRole::input && ! owner.isInSelectedScale(midiNoteNumber))
        return false;

    if (role == KeyboardRole::input && event.mods.isPopupMenu())
    {
        owner.showChordTemplateMenu(midiNoteNumber);
        return false;
    }

    if (role == KeyboardRole::output && event.mods.isPopupMenu())
    {
        owner.showInvertMenu();
        return false;
    }

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
    setLookAndFeel(&flatLookAndFeel);


    presetSummaryButton.addListener(this);
    addAndMakeVisible(presetSummaryButton);
    undoButton.setTooltip("Undo (Ctrl+Z)");
    redoButton.setTooltip("Redo (Ctrl+Y)");
    undoButton.onClick = [this] { undoEdit(); };
    redoButton.onClick = [this] { redoEdit(); };
    addAndMakeVisible(undoButton);
    addAndMakeVisible(redoButton);
    setWantsKeyboardFocus(true);
    updateHistoryButtons();
    prevPresetButton.setTooltip("Previous preset");
    nextPresetButton.setTooltip("Next preset");
    prevPresetButton.onClick = [this] { stepPreset(-1); };
    nextPresetButton.onClick = [this] { stepPreset(1); };
    addAndMakeVisible(prevPresetButton);
    addAndMakeVisible(nextPresetButton);

    themeToggleButton.addListener(this);
    addAndMakeVisible(themeToggleButton);

    scaleSummaryButton.addListener(this);
    addAndMakeVisible(scaleSummaryButton);

    newPresetButton.setButtonText("New");
    savePresetButton.setButtonText("Save");
    presetMenuButton.setButtonText("Preset");
    savePresetButton.setTooltip("Save changes to this preset");
    presetMenuButton.setTooltip("Save as, revert, rename, import, export, delete");

    for (auto* button : { &newPresetButton, &savePresetButton, &presetMenuButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }

    // Host/program changes and external preset edits may arrive while the editor is open.
    // (Also polls which notes are playing for the live key overlay, hence the rate.)
    startTimerHz(30);

    inputKeyboardLabel.setText("INPUT", juce::dontSendNotification);
    chordSymbolEditor.setTextToShowWhenEmpty("Type a chord, e.g. Dm9/G", getMutedTextColour());
    chordSymbolEditor.setTooltip("Replace the selected chord: C, Am7, F#m7b5, Bbmaj9, C6/9, G/B, Dsus4, ...");
    chordSymbolEditor.onReturnKey = [this] { applyChordSymbol(); };
    chordSymbolEditor.onEscapeKey = [this] { chordSymbolEditor.clear(); };
    chordSymbolEditor.onTextChange = [this]
    {
        chordSymbolEditor.setColour(juce::TextEditor::textColourId, getTextColour());
        chordSymbolEditor.applyFontToAllText(chordSymbolEditor.getFont());
    };
    addAndMakeVisible(chordSymbolEditor);
    layerButton.addListener(this);
    layerButton.setTooltip("Velocity layers: edit the soft layer, or a hard layer played at higher velocities");
    addAndMakeVisible(layerButton);
    updateLayerButton();
    learnButton.setButtonText("Learn");
    learnButton.setTooltip("MIDI learn: play a key on your controller to select its mapping");
    learnButton.setClickingTogglesState(true);
    learnButton.onClick = [this] { learnStamp = audioProcessor.getInputNoteOnStamp(); };
    addAndMakeVisible(learnButton);
    copyOctaveUpButton.setButtonText("Copy up");
    copyOctaveDownButton.setButtonText("Copy down");
    copyOctaveUpButton.setTooltip("Copy the selected octave's chords one octave up");
    copyOctaveDownButton.setTooltip("Copy the selected octave's chords one octave down");
    for (auto* button : { &copyOctaveUpButton, &copyOctaveDownButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }
    addAndMakeVisible(octaveStrip);
    updateOctaveCopyButtons();
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

    // The header's button row needs 640px plus the 172px title column and 36px of margins.
    setSize(980, 456);

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

    // Dialogs are modal children of this editor: dismiss any still open (with the "cancel"
    // result, so their callbacks don't act) before the editor they point into goes away.
    for (auto& dialog : openDialogs)
        if (dialog != nullptr)
            dialog->exitModalState(0);

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
    g.setFont(juce::Font(juce::FontOptions(getTitleTypeface()).withHeight(34.0f)).boldened());

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

    // The build's version ("0.1.0", or "0.1.0-dev+<commit>" between releases) right after the
    // title, so a bug report can say which build it came from.
    const auto titleFont = juce::Font(juce::FontOptions(getTitleTypeface()).withHeight(34.0f)).boldened();
    const float titleWidth = (float) juce::GlyphArrangement::getStringWidthInt(titleFont, "mnKord");
    g.setColour(getMutedTextColour());
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("v" MNKORD_VERSION_STRING, juce::Rectangle<float>(titleArea.getX() + titleWidth + 8.0f, titleArea.getY(), 200.0f, 44.0f),
               juce::Justification::bottomLeft, false);
}

void MnKordAudioProcessorEditor::resized()
{
    // The header background (paint()) spans the full, unmargined width/height 0-74, so its
    // children are laid out from raw local bounds too, with margins applied per-child instead
    // of via an outer reduced() that would otherwise desync the two.
    auto headerArea = getLocalBounds().removeFromTop(74).reduced(18, 8);

    auto titleColumn = headerArea.removeFromLeft(172);
    presetInfoLabel.setBounds(titleColumn.removeFromBottom(18));

    const int groupWidth = 26 + 4 + 200 + 4 + 26 + 8 + 62 + 8 + 62 + 8 + 76;
    auto controls = headerArea.withSizeKeepingCentre(groupWidth, 34);
    prevPresetButton.setBounds(controls.removeFromLeft(26));
    controls.removeFromLeft(4);
    presetSummaryButton.setBounds(controls.removeFromLeft(200));
    controls.removeFromLeft(4);
    nextPresetButton.setBounds(controls.removeFromLeft(26));
    controls.removeFromLeft(8);
    newPresetButton.setBounds(controls.removeFromLeft(62));
    controls.removeFromLeft(8);
    savePresetButton.setBounds(controls.removeFromLeft(62));
    controls.removeFromLeft(8);
    presetMenuButton.setBounds(controls.removeFromLeft(76));

    const auto themeBounds = juce::Rectangle<int>(26, 26).withCentre({ getWidth() - 18 - 13, headerArea.getCentreY() });
    themeToggleButton.setBounds(themeBounds);
    redoButton.setBounds(themeBounds.translated(-34, 0));
    undoButton.setBounds(themeBounds.translated(-62, 0));

    // Left/right/bottom margins of 14, top starting exactly where the header ends.
    auto area = getLocalBounds().reduced(14).withTrimmedTop(60);
    area.removeFromTop(14);

    auto inputPanel = area.removeFromTop(142).reduced(14, 10);
    auto inputHeader = inputPanel.removeFromTop(24);
    inputKeyboardLabel.setBounds(inputHeader.removeFromLeft(120));
    scaleSummaryButton.setBounds(inputHeader.removeFromRight(110));
    inputHeader.removeFromRight(8);
    copyOctaveDownButton.setBounds(inputHeader.removeFromRight(76));
    inputHeader.removeFromRight(6);
    copyOctaveUpButton.setBounds(inputHeader.removeFromRight(66));
    inputHeader.removeFromRight(6);
    learnButton.setBounds(inputHeader.removeFromRight(56));
    inputHeader.removeFromRight(6);
    layerButton.setBounds(inputHeader.removeFromRight(112));
    selectedInputLabel.setBounds(inputHeader);
    inputPanel.removeFromTop(2);
    octaveStrip.setBounds(inputPanel.removeFromTop(14));
    inputPanel.removeFromTop(2);
    inputKeyboard.setBounds(inputPanel);
    // 5 octaves (7 white keys each) fill the visible width exactly; the rest of
    // setAvailableRange()'s range is reachable via the scroll buttons/drag. Starting at C2
    // (see setLowestVisibleKey()) puts A4/440Hz (MIDI 69) roughly in the middle of the window.
    inputKeyboard.setKeyWidth((float) inputPanel.getWidth() / (5 * 7));

    area.removeFromTop(14);

    auto outputPanel = area.removeFromTop(184).reduced(14, 10);
    auto outputHeader = outputPanel.removeFromTop(24);
    outputKeyboardLabel.setBounds(outputHeader.removeFromLeft(120));
    chordSymbolEditor.setBounds(outputHeader.removeFromRight(190).reduced(0, 1));
    outputHeader.removeFromRight(8);
    chordNotesLabel.setBounds(outputHeader);
    outputPanel.removeFromTop(8);
    outputKeyboard.setBounds(outputPanel);
    outputKeyboard.setKeyWidth((float) outputPanel.getWidth() / (5 * 7));
}

//==============================================================================
juce::Range<float> MnKordAudioProcessorEditor::OctaveStrip::segmentRange(int start) const
{
    constexpr int lowest = 21, highest = 108;   // the keyboards' available range
    const auto& keyboard = owner.inputKeyboard;

    const int first = juce::jlimit(lowest, highest, start);
    const int last = juce::jlimit(lowest, highest, start + 11);
    if (start + 11 < lowest || start > highest)
        return {};

    const float x0 = keyboard.getRectangleForKey(first).getX();
    const float x1 = keyboard.getRectangleForKey(last).getRight();
    return juce::Range<float>(juce::jmax(0.0f, x0), juce::jmin((float) getWidth(), x1));
}

int MnKordAudioProcessorEditor::OctaveStrip::octaveStartAt(float x) const
{
    const int rootPitchClass = ((owner.scaleRoot % 12) + 12) % 12;

    for (int start = rootPitchClass - 12; start <= 120; start += 12)
    {
        const auto range = segmentRange(start);
        if (! range.isEmpty() && x >= range.getStart() && x < range.getEnd())
            return start;
    }

    return -1;
}

void MnKordAudioProcessorEditor::OctaveStrip::mouseDown(const juce::MouseEvent& e)
{
    const int start = octaveStartAt((float) e.x);
    selectedStart = (start < 0 || start == selectedStart) ? -1 : start;
    owner.updateOctaveCopyButtons();
    repaint();
}

void MnKordAudioProcessorEditor::OctaveStrip::paint(juce::Graphics& g)
{
    const int rootPitchClass = ((owner.scaleRoot % 12) + 12) % 12;
    g.setFont(juce::FontOptions(11.0f));

    for (int start = rootPitchClass - 12; start <= 120; start += 12)
    {
        const auto range = segmentRange(start);
        if (range.isEmpty())
            continue;

        const auto segment = juce::Rectangle<float>(range.getStart() + 1.0f, 1.0f, range.getLength() - 2.0f,
                                                    (float) getHeight() - 2.0f);
        const bool isSelected = start == selectedStart;

        g.setColour(isSelected ? owner.getAccentColour().withAlpha(0.35f)
                               : owner.getControlColour().withAlpha(0.7f));
        g.fillRoundedRectangle(segment, 3.0f);

        if (isSelected)
        {
            g.setColour(owner.getAccentColour());
            g.drawRoundedRectangle(segment, 3.0f, 1.0f);
        }

        // A segment cut by the edge of the range has no fixed label.
        if (start >= 0 && start + 11 <= 127)
        {
            g.setColour(isSelected ? owner.getAccentColour() : owner.getMutedTextColour());
            g.drawText(NoteName::toString(start) + " - " + NoteName::toString(start + 11), segment.toNearestInt(),
                       juce::Justification::centred, true);
        }
    }
}

void MnKordAudioProcessorEditor::updateOctaveCopyButtons()
{
    const bool enabled = octaveStrip.selectedStart >= 0;
    copyOctaveUpButton.setEnabled(enabled && octaveStrip.selectedStart + 12 <= 127);
    copyOctaveDownButton.setEnabled(enabled && octaveStrip.selectedStart - 12 >= 0);
}

void MnKordAudioProcessorEditor::copySelectedOctave(int semitones)
{
    const int start = octaveStrip.selectedStart;
    if (start < 0)
        return;

    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto& presets = audioProcessor.getPresets();
        const int presetIndex = audioProcessor.getActivePresetIndex();
        if (presetIndex < 0 || presetIndex >= (int) presets.size())
            return;

        auto& mappings = presets[(size_t) presetIndex].mappings;

        // Collect first: the copies may land on mappings that are sources themselves.
        std::vector<NoteMapping> copies;
        for (const auto& mapping : mappings)
        {
            if (mapping.inputNote < start || mapping.inputNote >= start + 12)
                continue;

            NoteMapping copy;
            copy.inputNote = mapping.inputNote + semitones;
            copy.minVelocity = mapping.minVelocity;
            if (copy.inputNote < 0 || copy.inputNote > 127)
                continue;

            for (const int note : mapping.outputNotes)
            {
                const int shifted = note + semitones;
                if (shifted < 0 || shifted > 127)
                    continue;

                copy.outputNotes.push_back(shifted);
                copy.outputVelocities[shifted] = mapping.velocityFor(note);
            }

            if (! copy.outputNotes.empty())
                copies.push_back(std::move(copy));
        }

        for (auto& copy : copies)
        {
            const auto existing = std::find_if(mappings.begin(), mappings.end(),
                                               [&copy](const NoteMapping& m)
                                               { return m.inputNote == copy.inputNote && m.minVelocity == copy.minVelocity; });
            if (existing != mappings.end())
                *existing = std::move(copy);
            else
                mappings.push_back(std::move(copy));
        }
    }

    // Selecting the copy lets the next click chain on to the following octave.
    octaveStrip.selectedStart = start + semitones;
    updateOctaveCopyButtons();
    octaveStrip.repaint();
    setSelectedMappingIndex(selectedMappingIndex);
    refreshSummaryLabels();
}

//==============================================================================
void MnKordAudioProcessorEditor::ThemeToggleButton::paintButton(juce::Graphics& g,
                                                                  bool shouldDrawButtonAsHighlighted,
                                                                  bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat();

    auto fill = findColour(juce::TextButton::buttonColourId);
    if (shouldDrawButtonAsDown)
        fill = fill.brighter(0.16f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.08f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 7.0f);

    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.2f;
    g.setColour(findColour(juce::TextButton::textColourOffId));

    if (showSun)
    {
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));

        juce::Path rays;
        for (int i = 0; i < 8; ++i)
        {
            const float angle = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
            const auto direction = juce::Point<float>(std::cos(angle), std::sin(angle));
            rays.startNewSubPath(centre + direction * (radius * 1.55f));
            rays.lineTo(centre + direction * (radius * 2.05f));
        }
        g.strokePath(rays, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        // A crescent: a disc with an offset disc cut out of it.
        const float moonRadius = radius * 1.5f;
        juce::Path moon;
        moon.addEllipse(juce::Rectangle<float>(moonRadius * 2.0f, moonRadius * 2.0f).withCentre(centre));

        juce::Path bite;
        bite.addEllipse(juce::Rectangle<float>(moonRadius * 1.8f, moonRadius * 1.8f)
                            .withCentre(centre + juce::Point<float>(moonRadius * 0.7f, -moonRadius * 0.55f)));

        moon.setUsingNonZeroWinding(false);
        moon.addPath(bite);
        g.fillPath(moon);
    }
}

void MnKordAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &layerButton)
        showLayerMenu();
    else if (button == &copyOctaveUpButton)
        copySelectedOctave(12);
    else if (button == &copyOctaveDownButton)
        copySelectedOctave(-12);
    else if (button == &themeToggleButton)
    {
        visualTheme = visualTheme == VisualTheme::Dark ? VisualTheme::Light : VisualTheme::Dark;
        applyVisualTheme();
        repaint();
    }
    else if (button == &presetSummaryButton)
    {
        showPresetBrowser();
    }
    else if (button == &newPresetButton)
    {
        juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);
        confirmLeavePreset([safeThis]
        {
            if (safeThis == nullptr)
                return;

            auto& self = *safeThis;
            Preset seed;
            seed.name = "Preset " + juce::String((int) self.audioProcessor.getPresets().size() + 1);
            self.promptPresetMetadata(seed, true, [safeThis](const Preset& p, const DiatonicFillOptions& fillOptions)
            {
                if (safeThis == nullptr)
                    return;

                auto& editor = *safeThis;
                Preset preset = p;
                if (fillOptions.scaleType != Scale::Chromatic)
                    editor.populateDiatonicChords(preset, fillOptions);

                // The dialog's scale choice also becomes the active INPUT-keyboard scale filter,
                // and its voicing the one Fill uses next.
                editor.scaleRoot = fillOptions.scaleRoot;
                editor.scaleType = fillOptions.scaleType;
                editor.initVoicing = fillOptions.voicing;
                editor.updateScaleSummaryText();
                editor.inputKeyboard.repaint();

                const bool written = editor.audioProcessor.addPreset(preset);
                editor.selectedMappingIndex = -1;
                editor.refreshPresetBox();
                editor.setSelectedMappingIndex(editor.selectedMappingIndex);

                if (! written)
                    editor.showSaveFailedWarning();
            });
        });
    }
    else if (button == &savePresetButton)
    {
        saveActivePreset();
    }
    else if (button == &presetMenuButton)
    {
        showPresetMenu();
    }
    else if (button == &scaleSummaryButton)
    {
        showScalePopup();
    }
}

//==============================================================================
void MnKordAudioProcessorEditor::refreshPresetBox()
{
    const auto& presets = audioProcessor.getPresets();
    const int activeIndex = audioProcessor.getActivePresetIndex();

    lastSyncedActivePresetIndex = activeIndex;
    lastSyncedPresetCount = (int) presets.size();

    // Sets the chip text (name plus the unsaved-changes marker) and the Save button.
    lastSyncedDirty = ! audioProcessor.isPresetDirty(activeIndex);
    updateDirtyState();

    updatePresetMetaLabel();
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::updateDirtyState()
{
    const auto& presets = audioProcessor.getPresets();
    const int activeIndex = audioProcessor.getActivePresetIndex();
    const bool valid = activeIndex >= 0 && activeIndex < (int) presets.size();
    const bool dirty = valid && audioProcessor.isPresetDirty(activeIndex);

    if (dirty == lastSyncedDirty && presetSummaryButton.getButtonText().isNotEmpty() && valid)
        return;

    lastSyncedDirty = dirty;

    // "Save" is for user presets; a changed factory preset can only go through Save as.
    const bool isFactory = valid && presets[(size_t) activeIndex].isFactory;
    juce::String text = valid ? presets[(size_t) activeIndex].name : juce::String("No preset");
    if (dirty)
        text += "  \xe2\x80\xa2";   // bullet (U+2022); the geometric dot U+25CF isn't in every font

    presetSummaryButton.setButtonText(text);
    savePresetButton.setEnabled(dirty);
    savePresetButton.setTooltip(isFactory ? "Factory presets can't be overwritten: Save opens Save as"
                                          : "Save changes to this preset");
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
    // The CallOutBox hosting this is owned by the modal machinery (see showScalePopup()), so it can
    // outlive the editor if the host closes the plugin UI while it's still open.
    if (owner == nullptr)
        return;

    owner->scaleRoot = juce::jmax(0, rootBox.getSelectedItemIndex());
    owner->scaleType = scaleTypeForIndex(typeBox.getSelectedItemIndex());
    owner->updateScaleSummaryText();
    owner->inputKeyboard.repaint();
    updateStepperCaptions();
    updateFillButtons();
}

void MnKordAudioProcessorEditor::showScalePopup()
{
    // Parented to the editor so the box stays inside the plugin window.
    auto content = std::make_unique<ScalePopupContent>(*this);
    content->setLookAndFeel(&flatLookAndFeel);
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(content), scaleSummaryButton.getBounds(), this);
    box.setLookAndFeel(&flatLookAndFeel);
    activeScalePopup = &box;
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

    // "All" followed by the distinct values labelFor yields, in first-appearance order.
    juce::StringArray distinctLabels(const std::vector<Preset>& presets,
                                     const std::function<juce::String(const Preset&)>& labelFor)
    {
        juce::StringArray values;
        values.add("All");
        for (const auto& preset : presets)
            values.addIfNotAlreadyThere(labelFor(preset));
        return values;
    }

    // "All" followed by every distinct style tag, alphabetically.
    juce::StringArray distinctTags(const std::vector<Preset>& presets)
    {
        juce::StringArray values;
        for (const auto& preset : presets)
            for (const auto& tag : preset.tags)
                values.addIfNotAlreadyThere(tag, true);
        values.sortNatural();
        values.insert(0, "All");
        return values;
    }
}

void MnKordAudioProcessorEditor::PresetBrowserContent::IconButton::paintButton(juce::Graphics& g, bool highlighted,
                                                                               bool down)
{
    auto bounds = getLocalBounds().toFloat();
    const bool lit = highlighted || down || (kind == Kind::Star && getToggleState());
    g.setColour(! isEnabled() ? dimColour.withAlpha(0.3f) : (lit ? litColour : dimColour));

    const auto centre = bounds.getCentre();
    const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.27f;

    if (kind == Kind::Check)
    {
        juce::Path p;
        p.startNewSubPath(centre.x - r, centre.y + r * 0.1f);
        p.lineTo(centre.x - r * 0.3f, centre.y + r * 0.8f);
        p.lineTo(centre.x + r, centre.y - r * 0.7f);
        g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (kind == Kind::Cross)
    {
        juce::Path p;
        p.startNewSubPath(centre.x - r, centre.y - r);
        p.lineTo(centre.x + r, centre.y + r);
        p.startNewSubPath(centre.x + r, centre.y - r);
        p.lineTo(centre.x - r, centre.y + r);
        g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (kind == Kind::Undo || kind == Kind::Redo)
    {
        // An open arc with an arrowhead on its lower-left end (mirrored for redo).
        const float radius = r * 1.2f;
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, -0.75f * juce::MathConstants<float>::pi,
                          0.45f * juce::MathConstants<float>::pi, true);
        juce::Path head;
        const juce::Point<float> tip(centre.x - radius * 0.7071f, centre.y + radius * 0.7071f);
        head.startNewSubPath(tip.x - radius * 0.1f, tip.y - radius * 0.85f);
        head.lineTo(tip);
        head.lineTo(tip.x + radius * 0.85f, tip.y + radius * 0.1f);
        if (kind == Kind::Redo)
        {
            const auto flip = juce::AffineTransform::scale(-1.0f, 1.0f, centre.x, centre.y);
            arc.applyTransform(flip);
            head.applyTransform(flip);
        }
        g.strokePath(arc, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.strokePath(head, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (kind == Kind::Prev || kind == Kind::Next)
    {
        const float direction = kind == Kind::Prev ? -1.0f : 1.0f;
        juce::Path p;
        p.startNewSubPath(centre.x - direction * r * 0.5f, centre.y - r * 1.1f);
        p.lineTo(centre.x + direction * r * 0.5f, centre.y);
        p.lineTo(centre.x - direction * r * 0.5f, centre.y + r * 1.1f);
        g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        juce::Path star;
        star.addStar(centre, 5, r * 0.5f, r * 1.15f, -juce::MathConstants<float>::halfPi);
        if (getToggleState())
            g.fillPath(star);
        else
            g.strokePath(star, juce::PathStrokeType(1.4f));
    }
}

MnKordAudioProcessorEditor::PresetBrowserContent::FilterColumn::FilterColumn(PresetBrowserContent& ownerRef)
    : owner(ownerRef), box("BrowserColumn", this)
{
}

void MnKordAudioProcessorEditor::PresetBrowserContent::FilterColumn::paintListBoxItem(int row, juce::Graphics& g,
                                                                                      int width, int height, bool)
{
    auto* editor = owner.owner.getComponent();
    if (editor == nullptr || row < 0 || row >= items.size())
        return;

    const bool isSelected = row == selected;
    if (isSelected)
    {
        g.setColour(editor->getAccentColour().withAlpha(0.18f));
        g.fillRect(0, 0, width, height);
    }

    g.setColour(isSelected ? editor->getAccentColour()
                           : (row == 0 ? editor->getMutedTextColour() : editor->getTextColour()));
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(items[row], 10, 0, width - 14, height, juce::Justification::centredLeft, true);
}

void MnKordAudioProcessorEditor::PresetBrowserContent::FilterColumn::listBoxItemClicked(int row,
                                                                                         const juce::MouseEvent&)
{
    if (row < 0 || row >= items.size())
        return;

    selected = row;
    box.repaint();
    owner.rebuildRows();
}

MnKordAudioProcessorEditor::PresetBrowserContent::PresetBrowserContent(MnKordAudioProcessorEditor& ownerRef)
    : owner(&ownerRef),
      groupColumn(*this),
      authorColumn(*this),
      styleColumn(*this),
      presetColumn(*this),
      favoritesOnlyButton(IconButton::Kind::Star),
      confirmButton(IconButton::Kind::Check),
      cancelButton(IconButton::Kind::Cross),
      startIndex(ownerRef.audioProcessor.getActivePresetIndex())
{
    const auto& presets = owner->audioProcessor.getPresets();
    groupColumn.items = distinctLabels(presets, groupLabelFor);
    authorColumn.items = distinctLabels(presets, authorLabelFor);
    styleColumn.items = distinctTags(presets);

    for (auto* column : { &groupColumn.box, &authorColumn.box, &styleColumn.box, &presetColumn.box })
    {
        column->setRowHeight(26);
        column->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        column->setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(column);
    }

    searchBox.setTextToShowWhenEmpty("Search...", owner->getMutedTextColour());
    searchBox.setColour(juce::TextEditor::backgroundColourId, owner->getControlColour());
    searchBox.setColour(juce::TextEditor::textColourId, owner->getTextColour());
    searchBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    searchBox.setColour(juce::TextEditor::focusedOutlineColourId, owner->getAccentColour());
    searchBox.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible(searchBox);

    for (auto* button : { &favoritesOnlyButton, &confirmButton, &cancelButton })
    {
        button->dimColour = owner->getMutedTextColour();
        button->litColour = owner->getAccentColour();
        addAndMakeVisible(button);
    }

    favoritesOnlyButton.setClickingTogglesState(true);
    favoritesOnlyButton.setTooltip("Show favorites only");
    favoritesOnlyButton.onClick = [this] { rebuildRows(); };
    confirmButton.setTooltip("Keep this preset");
    confirmButton.onClick = [this] { closeBrowser(); };
    cancelButton.setTooltip("Close and go back to the preset you started from");
    cancelButton.onClick = [this]
    {
        if (owner != nullptr && startIndex >= 0 && startIndex < (int) owner->audioProcessor.getPresets().size())
            owner->selectActivePreset(startIndex);

        closeBrowser();
    };

    rebuildRows();
    startTimerHz(8);

    // Wide enough for the three columns, short enough to sit below the header inside the window.
    setSize(690, 330);
}

void MnKordAudioProcessorEditor::PresetBrowserContent::closeBrowser()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void MnKordAudioProcessorEditor::PresetBrowserContent::timerCallback()
{
    // A preset switch may be answered later (unsaved-changes dialog), so follow the active preset.
    presetColumn.box.repaint();
}

void MnKordAudioProcessorEditor::PresetBrowserContent::paint(juce::Graphics& g)
{
    if (owner == nullptr)
        return;

    g.setColour(owner->getMutedTextColour());
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));

    const auto drawTitle = [&](const juce::String& text, juce::Rectangle<int> column)
    {
        g.drawText(text, column.getX() + 10, headerArea.getY(), column.getWidth() - 10, headerArea.getHeight(),
                   juce::Justification::centredLeft);
    };

    drawTitle("GROUP", groupColumn.box.getBounds());
    drawTitle("AUTHOR", authorColumn.box.getBounds());
    drawTitle("STYLE", styleColumn.box.getBounds());
    drawTitle("PRESETS", presetColumn.box.getBounds().withTrimmedLeft(24));

    g.setColour(owner->getMutedTextColour().withAlpha(0.25f));
    for (auto* column : { &authorColumn.box, &styleColumn.box, &presetColumn.box })
        g.fillRect(column->getX() - 4, column->getY(), 1, column->getHeight());
}

void MnKordAudioProcessorEditor::PresetBrowserContent::resized()
{
    auto area = getLocalBounds().reduced(10);
    headerArea = area.removeFromTop(30);
    area.removeFromTop(6);

    auto header = headerArea;
    cancelButton.setBounds(header.removeFromRight(28));
    confirmButton.setBounds(header.removeFromRight(28));
    header.removeFromRight(8);
    searchBox.setBounds(header.removeFromRight(150).reduced(0, 2));

    groupColumn.box.setBounds(area.removeFromLeft(120));
    area.removeFromLeft(8);
    authorColumn.box.setBounds(area.removeFromLeft(120));
    area.removeFromLeft(8);
    styleColumn.box.setBounds(area.removeFromLeft(120));
    area.removeFromLeft(8);
    presetColumn.box.setBounds(area);
    favoritesOnlyButton.setBounds(presetColumn.box.getX() + 1, headerArea.getY() + 1, 28, 28);
}

void MnKordAudioProcessorEditor::PresetBrowserContent::rebuildRows()
{
    presetColumn.rows.clear();

    // The CallOutBox hosting this is owned by the modal machinery (see showPresetBrowser()), so it can
    // outlive the editor if the host closes the plugin UI while it's still open.
    if (owner != nullptr)
    {
        const auto& presets = owner->audioProcessor.getPresets();
        const auto filter = searchBox.getText().trim();
        const auto group = groupColumn.selected > 0 ? groupColumn.items[groupColumn.selected] : juce::String();
        const auto author = authorColumn.selected > 0 ? authorColumn.items[authorColumn.selected] : juce::String();
        const auto style = styleColumn.selected > 0 ? styleColumn.items[styleColumn.selected] : juce::String();
        const bool favoritesOnly = favoritesOnlyButton.getToggleState();

        for (int i = 0; i < (int) presets.size(); ++i)
        {
            const auto& preset = presets[(size_t) i];

            if (group.isNotEmpty() && groupLabelFor(preset) != group)
                continue;
            if (author.isNotEmpty() && authorLabelFor(preset) != author)
                continue;
            if (style.isNotEmpty() && ! preset.tags.contains(style, true))
                continue;
            if (favoritesOnly && ! owner->audioProcessor.isFavoritePreset(preset))
                continue;
            if (filter.isNotEmpty() && ! (preset.name.containsIgnoreCase(filter)
                                          || preset.group.containsIgnoreCase(filter)
                                          || preset.author.containsIgnoreCase(filter)
                                          || preset.tags.joinIntoString(" ").containsIgnoreCase(filter)))
                continue;

            presetColumn.rows.push_back(i);
        }
    }

    presetColumn.box.updateContent();
    presetColumn.box.repaint();
}

int MnKordAudioProcessorEditor::PresetBrowserContent::PresetColumn::getNumRows()
{
    return (int) rows.size();
}

void MnKordAudioProcessorEditor::PresetBrowserContent::PresetColumn::paintListBoxItem(int row, juce::Graphics& g,
                                                                                      int width, int height, bool)
{
    auto* editor = owner.owner.getComponent();
    if (editor == nullptr || row < 0 || row >= (int) rows.size())
        return;

    const auto& presets = editor->audioProcessor.getPresets();
    const int presetIndex = rows[(size_t) row];
    if (presetIndex < 0 || presetIndex >= (int) presets.size())
        return;

    const auto& preset = presets[(size_t) presetIndex];
    const bool isActive = presetIndex == editor->audioProcessor.getActivePresetIndex();

    if (isActive)
    {
        g.setColour(editor->getAccentColour().withAlpha(0.18f));
        g.fillRect(0, 0, width, height);
    }

    const auto starCentre = juce::Point<float>(15.0f, (float) height * 0.5f);
    juce::Path star;
    star.addStar(starCentre, 5, 3.4f, 7.0f, -juce::MathConstants<float>::halfPi);
    if (editor->audioProcessor.isFavoritePreset(preset))
    {
        g.setColour(editor->getAccentColour());
        g.fillPath(star);
    }
    else
    {
        g.setColour(editor->getMutedTextColour().withAlpha(0.7f));
        g.strokePath(star, juce::PathStrokeType(1.2f));
    }

    g.setColour(isActive ? editor->getAccentColour() : editor->getTextColour());
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(preset.name, 34, 0, width - 40, height, juce::Justification::centredLeft, true);
}

void MnKordAudioProcessorEditor::PresetBrowserContent::PresetColumn::listBoxItemClicked(int row,
                                                                                         const juce::MouseEvent& e)
{
    auto* editor = owner.owner.getComponent();
    if (editor == nullptr || row < 0 || row >= (int) rows.size())
        return;

    const int presetIndex = rows[(size_t) row];
    const auto& presets = editor->audioProcessor.getPresets();
    if (presetIndex < 0 || presetIndex >= (int) presets.size())
        return;

    if (e.mods.isPopupMenu())
    {
        editor->showPresetRowMenu(presetIndex);
        return;
    }

    if (e.x < 30)
    {
        const auto& preset = presets[(size_t) presetIndex];
        editor->audioProcessor.setFavoritePreset(preset, ! editor->audioProcessor.isFavoritePreset(preset));
        owner.rebuildRows();
        return;
    }

    // Auditioning: the browser stays open; the tick keeps the preset, the cross goes back.
    editor->selectActivePreset(presetIndex);
}

void MnKordAudioProcessorEditor::PresetBrowserContent::PresetColumn::listBoxItemDoubleClicked(int row,
                                                                                               const juce::MouseEvent& e)
{
    if (row >= 0 && row < (int) rows.size() && e.x >= 30)
        owner.closeBrowser();
}

void MnKordAudioProcessorEditor::showPresetBrowser()
{
    // Presets saved by another instance (or dropped into the User folder) show up.
    audioProcessor.refreshUserLibrary();

    // Parented to the editor so the box stays inside the plugin window.
    auto content = std::make_unique<PresetBrowserContent>(*this);
    content->setLookAndFeel(&flatLookAndFeel);
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(content), presetSummaryButton.getBounds(), this);
    box.setLookAndFeel(&flatLookAndFeel);
    activePresetPopup = &box;
}

void MnKordAudioProcessorEditor::selectActivePreset(int index)
{
    if (index == audioProcessor.getActivePresetIndex())
        return;

    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);
    confirmLeavePreset([safeThis, index]
    {
        if (safeThis == nullptr)
            return;

        safeThis->audioProcessor.setActivePresetIndex(index);
        safeThis->selectedMappingIndex = -1;
        safeThis->setSelectedMappingIndex(safeThis->selectedMappingIndex);
    });
}

void MnKordAudioProcessorEditor::trackDialog(juce::Component* dialog)
{
    openDialogs.removeIf([](const auto& d) { return d == nullptr; });
    openDialogs.add(dialog);
}

void MnKordAudioProcessorEditor::showMessage(const juce::String& title, const juce::String& message)
{
    auto* dialog = new juce::AlertWindow(title, message, juce::MessageBoxIconType::WarningIcon, this);
    dialog->setLookAndFeel(&flatLookAndFeel);
    addChildComponent(dialog);
    trackDialog(dialog);

    dialog->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create([dialog](int) { dialog->setLookAndFeel(nullptr); }),
                            true);
}

void MnKordAudioProcessorEditor::showSaveFailedWarning()
{
    showMessage("Couldn't save the preset",
                "mnKord couldn't write the preset file to:\n"
                    + MnKordAudioProcessor::getUserFolder().getFullPathName()
                    + "\n\nThe preset stays loaded, with its changes still unsaved.");
}

void MnKordAudioProcessorEditor::confirmLeavePreset(std::function<void()> proceed)
{
    const int index = audioProcessor.getActivePresetIndex();
    const auto& presets = audioProcessor.getPresets();

    if (index < 0 || index >= (int) presets.size() || ! audioProcessor.isPresetDirty(index))
    {
        proceed();
        return;
    }

    const bool isFactory = presets[(size_t) index].isFactory;
    juce::String message = "\"" + presets[(size_t) index].name + "\" has unsaved changes.";
    if (isFactory)
        message += "\n\nFactory presets can't be overwritten. Discard the changes, or cancel and use "
                   "Save as to keep them.";

    auto* dialog = new juce::AlertWindow("Unsaved changes", message, juce::MessageBoxIconType::QuestionIcon, this);
    dialog->setLookAndFeel(&flatLookAndFeel);
    addChildComponent(dialog);
    trackDialog(dialog);

    if (! isFactory)
        dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));

    dialog->addButton("Discard", 2);
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);
    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create([safeThis, dialog, index, proceed](int result)
                            {
                                dialog->setLookAndFeel(nullptr);

                                if (safeThis == nullptr)
                                    return;

                                if (result == 1)
                                {
                                    if (! safeThis->audioProcessor.saveUserPreset(index))
                                    {
                                        safeThis->showSaveFailedWarning();
                                        return;
                                    }
                                }
                                else if (result != 2)
                                {
                                    return;
                                }

                                proceed();
                            }),
                            true);
}

void MnKordAudioProcessorEditor::saveActivePreset()
{
    const auto& presets = audioProcessor.getPresets();
    const int index = audioProcessor.getActivePresetIndex();

    if (index < 0 || index >= (int) presets.size())
        return;

    // A factory preset can't be overwritten, so saving it means saving a copy.
    if (presets[(size_t) index].isFactory)
    {
        saveCurrentPresetAs();
        return;
    }

    if (! audioProcessor.saveUserPreset(index))
        showSaveFailedWarning();

    refreshPresetBox();
}

void MnKordAudioProcessorEditor::revertActivePreset()
{
    const int index = audioProcessor.getActivePresetIndex();
    if (index < 0)
        return;

    audioProcessor.revertPreset(index);
    refreshPresetBox();
    setSelectedMappingIndex(selectedMappingIndex);
}

void MnKordAudioProcessorEditor::deleteActivePreset()
{
    const int index = audioProcessor.getActivePresetIndex();
    if (index < 0)
        return;

    // The file goes to the recycle bin, so this can be undone from there.
    if (! audioProcessor.deletePreset(index))
    {
        showMessage("Couldn't delete the preset",
                    "The preset file couldn't be moved to the recycle bin, so it was left in place.");
        return;
    }

    selectedMappingIndex = -1;
    refreshPresetBox();
    setSelectedMappingIndex(selectedMappingIndex);
}

namespace
{
    bool sameMappings(const std::vector<NoteMapping>& a, const std::vector<NoteMapping>& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].inputNote != b[i].inputNote || a[i].minVelocity != b[i].minVelocity
                || a[i].outputNotes != b[i].outputNotes
                || a[i].outputVelocities != b[i].outputVelocities)
                return false;

        return true;
    }
}

void MnKordAudioProcessorEditor::updateHistoryButtons()
{
    undoButton.setEnabled(! undoStack.empty());
    redoButton.setEnabled(! redoStack.empty());
}

void MnKordAudioProcessorEditor::trackHistory()
{
    const auto& presets = audioProcessor.getPresets();
    const int index = audioProcessor.getActivePresetIndex();
    if (index < 0 || index >= (int) presets.size())
        return;

    const auto& preset = presets[(size_t) index];
    const auto key = juce::String(index) + (preset.isFactory ? "|F|" : "|U|")
                     + (preset.file != juce::File() ? preset.file.getFullPathName() : preset.name);

    if (key != historyKey)
    {
        historyKey = key;
        historyMappings = preset.mappings;
        undoStack.clear();
        redoStack.clear();
        lastEditTimeMs = 0;
        updateHistoryButtons();
        return;
    }

    if (sameMappings(historyMappings, preset.mappings))
        return;

    // Changes arriving in quick succession (a velocity drag, a burst of clicks) are one step.
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastEditTimeMs > 600)
    {
        undoStack.push_back(historyMappings);
        if (undoStack.size() > 100)
            undoStack.erase(undoStack.begin());
        redoStack.clear();
    }

    historyMappings = preset.mappings;
    lastEditTimeMs = now;
    updateHistoryButtons();
}

void MnKordAudioProcessorEditor::undoEdit()
{
    trackHistory();
    if (undoStack.empty())
        return;

    const int index = audioProcessor.getActivePresetIndex();
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());
        auto& presets = audioProcessor.getPresets();
        if (index < 0 || index >= (int) presets.size())
            return;

        redoStack.push_back(presets[(size_t) index].mappings);
        presets[(size_t) index].mappings = undoStack.back();
        historyMappings = presets[(size_t) index].mappings;
    }

    undoStack.pop_back();
    lastEditTimeMs = 0;
    updateHistoryButtons();
    setSelectedMappingIndex(selectedMappingIndex);
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::redoEdit()
{
    trackHistory();
    if (redoStack.empty())
        return;

    const int index = audioProcessor.getActivePresetIndex();
    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());
        auto& presets = audioProcessor.getPresets();
        if (index < 0 || index >= (int) presets.size())
            return;

        undoStack.push_back(presets[(size_t) index].mappings);
        presets[(size_t) index].mappings = redoStack.back();
        historyMappings = presets[(size_t) index].mappings;
    }

    redoStack.pop_back();
    lastEditTimeMs = 0;
    updateHistoryButtons();
    setSelectedMappingIndex(selectedMappingIndex);
    refreshSummaryLabels();
}

bool MnKordAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();

    if (mods.isCommandDown() && code == 'Z')
    {
        if (mods.isShiftDown())
            redoEdit();
        else
            undoEdit();
        return true;
    }

    if (mods.isCommandDown() && code == 'Y')
    {
        redoEdit();
        return true;
    }

    return false;
}

void MnKordAudioProcessorEditor::mouseDown(const juce::MouseEvent&)
{
    // Lets the undo shortcuts work after clicking anywhere in the editor.
    grabKeyboardFocus();
}

void MnKordAudioProcessorEditor::transposePreset(int semitones, bool inputs, bool outputs)
{
    bool outOfRange = false;

    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto& presets = audioProcessor.getPresets();
        const int presetIndex = audioProcessor.getActivePresetIndex();
        if (presetIndex < 0 || presetIndex >= (int) presets.size())
            return;

        auto& mappings = presets[(size_t) presetIndex].mappings;
        auto fits = [semitones](int note) { return note + semitones >= 0 && note + semitones <= 127; };

        for (const auto& mapping : mappings)
        {
            if (inputs && ! fits(mapping.inputNote))
                outOfRange = true;

            if (outputs)
                for (const int note : mapping.outputNotes)
                    if (! fits(note))
                        outOfRange = true;
        }

        // All or nothing: a transposition that pushes a note off the keyboard changes nothing.
        if (! outOfRange)
        {
            for (auto& mapping : mappings)
            {
                if (inputs)
                    mapping.inputNote += semitones;

                if (outputs)
                {
                    for (auto& note : mapping.outputNotes)
                        note += semitones;

                    std::map<int, int> shifted;
                    for (const auto& [note, velocity] : mapping.outputVelocities)
                        shifted[note + semitones] = velocity;
                    mapping.outputVelocities = std::move(shifted);
                }
            }
        }
    }

    if (outOfRange)
    {
        showMessage("Can't transpose", "Some notes would end up outside the MIDI range (0-127).");
        return;
    }

    setSelectedMappingIndex(selectedMappingIndex);
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::applyChordSymbol()
{
    const auto chord = ChordSymbol::parseChord(chordSymbolEditor.getText());
    if (! chord.valid)
    {
        chordSymbolEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xffcf4b41));
        chordSymbolEditor.applyFontToAllText(chordSymbolEditor.getFont());
        return;
    }

    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto* mapping = getSelectedMapping();
        if (mapping == nullptr)
            return;   // nothing selected: click an INPUT key first

        const auto voiced = ChordSymbol::voicedMapping(mapping->inputNote, chord);
        mapping->outputNotes = voiced.outputNotes;
        mapping->outputVelocities = voiced.outputVelocities;
    }

    chordSymbolEditor.clear();
    setSelectedMappingIndex(selectedMappingIndex);
    refreshSummaryLabels();
}

void MnKordAudioProcessorEditor::updateLayerButton()
{
    layerButton.setButtonText(hardLayerEditing ? "Hard >= " + juce::String(hardLayerThreshold) : juce::String("Soft layer"));
    layerButton.setToggleState(hardLayerEditing, juce::dontSendNotification);
}

void MnKordAudioProcessorEditor::showLayerMenu()
{
    auto menu = makeStyledMenu();
    menu.addItem(1, "Soft layer (plays at any velocity)", true, ! hardLayerEditing);

    const int thresholds[] = { 80, 90, 100, 110, 120 };
    for (int i = 0; i < 5; ++i)
        menu.addItem(10 + i, "Hard layer from velocity " + juce::String(thresholds[i]), true,
                     hardLayerEditing && hardLayerThreshold == thresholds[i]);

    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&layerButton).withParentComponent(this),
                       [safeThis, thresholds](int result)
                       {
                           if (safeThis == nullptr || result == 0)
                               return;

                           auto& editor = *safeThis;
                           editor.hardLayerEditing = result >= 10;
                           if (result >= 10)
                               editor.hardLayerThreshold = thresholds[result - 10];

                           // The threshold belongs to the preset's hard layers as a whole.
                           if (result >= 10)
                           {
                               const juce::ScopedLock lock(editor.audioProcessor.getStateLock());
                               auto& presets = editor.audioProcessor.getPresets();
                               const int presetIndex = editor.audioProcessor.getActivePresetIndex();
                               if (presetIndex >= 0 && presetIndex < (int) presets.size())
                                   for (auto& mapping : presets[(size_t) presetIndex].mappings)
                                       if (mapping.minVelocity > 1)
                                           mapping.minVelocity = editor.hardLayerThreshold;
                           }

                           editor.updateLayerButton();

                           // Follow the selected key to its mapping in the layer now being edited.
                           int newSelection = -1;
                           if (const auto* selected = editor.getSelectedMapping())
                           {
                               const auto& presets = editor.audioProcessor.getPresets();
                               const auto& mappings = presets[(size_t) editor.audioProcessor.getActivePresetIndex()].mappings;
                               for (int i = 0; i < (int) mappings.size(); ++i)
                                   if (mappings[(size_t) i].inputNote == selected->inputNote
                                       && (mappings[(size_t) i].minVelocity > 1) == editor.hardLayerEditing)
                                       newSelection = i;
                           }

                           editor.setSelectedMappingIndex(newSelection);
                           editor.refreshSummaryLabels();
                       });
}

void MnKordAudioProcessorEditor::stepPreset(int delta)
{
    const int count = (int) audioProcessor.getPresets().size();
    const int active = audioProcessor.getActivePresetIndex();
    if (count < 2 || active < 0)
        return;

    selectActivePreset(((active + delta) % count + count) % count);
}

void MnKordAudioProcessorEditor::duplicatePreset(int presetIndex)
{
    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);

    // Duplicating saves a new preset and selects it, so unsaved edits get the usual question first.
    confirmLeavePreset([safeThis, presetIndex]
    {
        if (safeThis == nullptr)
            return;

        auto& editor = *safeThis;
        const auto& presets = editor.audioProcessor.getPresets();
        if (presetIndex < 0 || presetIndex >= (int) presets.size())
            return;

        Preset copy = presets[(size_t) presetIndex];
        const auto baseName = copy.name;

        auto nameTaken = [&presets](const juce::String& name)
        {
            return std::any_of(presets.begin(), presets.end(), [&name](const Preset& p) { return p.name == name; });
        };

        copy.name = baseName + " copy";
        for (int n = 2; nameTaken(copy.name); ++n)
            copy.name = baseName + " copy " + juce::String(n);

        copy.group = copy.isFactory ? "User" : copy.group;
        copy.author = copy.isFactory ? juce::String() : copy.author;
        copy.isFactory = false;
        copy.file = juce::File();
        copy.savedXml = {};

        const bool written = editor.audioProcessor.addPreset(copy);
        editor.selectedMappingIndex = -1;
        editor.refreshPresetBox();
        editor.setSelectedMappingIndex(editor.selectedMappingIndex);

        if (! written)
            editor.showSaveFailedWarning();
    });
}

void MnKordAudioProcessorEditor::showPresetRowMenu(int presetIndex)
{
    const auto& presets = audioProcessor.getPresets();
    if (presetIndex < 0 || presetIndex >= (int) presets.size())
        return;

    const auto& preset = presets[(size_t) presetIndex];

    auto menu = makeStyledMenu();
    menu.addItem(1, "Load");
    menu.addItem(2, "Duplicate");
    menu.addItem(3, "Rename / edit info...", ! preset.isFactory);
    menu.addItem(4, "Show in folder", preset.file.existsAsFile());
    menu.addSeparator();
    menu.addColouredItem(5, "Delete", juce::Colour(0xffcf4b41), ! preset.isFactory);

    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);

    const auto mouse = juce::Desktop::getInstance().getMousePosition();
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea(juce::Rectangle<int>(mouse.x, mouse.y, 1, 1))
                           .withParentComponent(this),
                       [safeThis, presetIndex](int result)
                       {
                           if (safeThis == nullptr || result == 0)
                               return;

                           auto& editor = *safeThis;
                           const auto& list = editor.audioProcessor.getPresets();
                           if (presetIndex >= (int) list.size())
                               return;

                           switch (result)
                           {
                               case 1:
                                   editor.selectActivePreset(presetIndex);
                                   return;   // the browser stays open for auditioning
                               case 2:
                                   editor.duplicatePreset(presetIndex);
                                   break;
                               case 3:
                                   if (presetIndex == editor.audioProcessor.getActivePresetIndex())
                                   {
                                       editor.editActivePresetMetadata();
                                   }
                                   else
                                   {
                                       editor.confirmLeavePreset([safeThis, presetIndex]
                                       {
                                           if (safeThis == nullptr
                                               || presetIndex >= (int) safeThis->audioProcessor.getPresets().size())
                                               return;

                                           safeThis->audioProcessor.setActivePresetIndex(presetIndex);
                                           safeThis->selectedMappingIndex = -1;
                                           safeThis->refreshPresetBox();
                                           safeThis->setSelectedMappingIndex(safeThis->selectedMappingIndex);
                                           safeThis->editActivePresetMetadata();
                                       });
                                   }
                                   break;
                               case 4:
                                   list[(size_t) presetIndex].file.revealToUser();
                                   return;
                               case 5:
                                   if (! editor.audioProcessor.deletePreset(presetIndex))
                                   {
                                       editor.showMessage("Couldn't delete the preset",
                                                          "The preset file couldn't be moved to the recycle bin, so it was left in place.");
                                       return;
                                   }

                                   editor.selectedMappingIndex = -1;
                                   editor.refreshPresetBox();
                                   editor.setSelectedMappingIndex(editor.selectedMappingIndex);
                                   break;
                               default: break;
                           }

                           // The browser's rows are stale after these, so close it.
                           if (editor.activePresetPopup != nullptr)
                               editor.activePresetPopup->dismiss();
                       });
}

void MnKordAudioProcessorEditor::showPresetMenu()
{
    const auto& presets = audioProcessor.getPresets();
    const int index = audioProcessor.getActivePresetIndex();
    const bool valid = index >= 0 && index < (int) presets.size();
    const bool isFactory = valid && presets[(size_t) index].isFactory;
    const bool dirty = valid && audioProcessor.isPresetDirty(index);

    auto menu = makeStyledMenu();
    menu.addItem(1, "Save as...", valid);
    menu.addItem(2, "Revert to saved", dirty && audioProcessor.canRevertPreset(index));
    menu.addItem(3, "Rename / edit info...", valid && ! isFactory);

    auto transposeMenu = makeStyledMenu();
    transposeMenu.addItem(10, "Key up a semitone (keys and chords)");
    transposeMenu.addItem(11, "Key down a semitone (keys and chords)");
    transposeMenu.addItem(12, "Key up an octave (keys and chords)");
    transposeMenu.addItem(13, "Key down an octave (keys and chords)");
    transposeMenu.addSeparator();
    transposeMenu.addItem(14, "Chords up a semitone");
    transposeMenu.addItem(15, "Chords down a semitone");
    transposeMenu.addItem(16, "Chords up an octave");
    transposeMenu.addItem(17, "Chords down an octave");
    menu.addSubMenu("Transpose", transposeMenu, valid);

    menu.addSeparator();
    menu.addItem(4, "Import...");
    menu.addItem(5, "Export...", valid);
    menu.addItem(6, "Show in folder", valid && presets[(size_t) index].file.existsAsFile());
    menu.addSeparator();
    menu.addColouredItem(7, "Delete", juce::Colour(0xffcf4b41), valid && ! isFactory);

    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetMenuButton).withParentComponent(this),
                       [safeThis](int result)
                       {
                           if (safeThis == nullptr || result == 0)
                               return;

                           auto& editor = *safeThis;

                           switch (result)
                           {
                               case 1: editor.saveCurrentPresetAs(); break;
                               case 2: editor.revertActivePreset(); break;
                               case 3: editor.editActivePresetMetadata(); break;
                               case 4:
                                   editor.confirmLeavePreset([safeThis]
                                   {
                                       if (safeThis != nullptr)
                                           safeThis->importPresetsFromChooser();
                                   });
                                   break;
                               case 5: editor.exportPresetsFromChooser(); break;
                               case 6:
                               {
                                   const auto& list = editor.audioProcessor.getPresets();
                                   const int active = editor.audioProcessor.getActivePresetIndex();
                                   if (active >= 0 && active < (int) list.size())
                                       list[(size_t) active].file.revealToUser();
                                   break;
                               }
                               case 7: editor.deleteActivePreset(); break;
                               case 10: editor.transposePreset(1, true, true); break;
                               case 11: editor.transposePreset(-1, true, true); break;
                               case 12: editor.transposePreset(12, true, true); break;
                               case 13: editor.transposePreset(-12, true, true); break;
                               case 14: editor.transposePreset(1, false, true); break;
                               case 15: editor.transposePreset(-1, false, true); break;
                               case 16: editor.transposePreset(12, false, true); break;
                               case 17: editor.transposePreset(-12, false, true); break;
                               default: break;
                           }
                       });
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
    chordSymbolEditor.setColour(juce::TextEditor::backgroundColourId, getControlColour());
    chordSymbolEditor.setColour(juce::TextEditor::textColourId, getTextColour());
    chordSymbolEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    chordSymbolEditor.setColour(juce::TextEditor::focusedOutlineColourId, getAccentColour());
    chordSymbolEditor.setColour(juce::TextEditor::highlightColourId, getAccentColour().withAlpha(0.25f));
    chordSymbolEditor.setTextToShowWhenEmpty("Type a chord, e.g. Dm9/G", getMutedTextColour());

    const auto control = getControlColour();
    const auto textColour = getTextColour();

    themeToggleButton.showSun = visualTheme == VisualTheme::Dark;
    themeToggleButton.setTooltip(visualTheme == VisualTheme::Dark ? "Switch to the light theme"
                                                                  : "Switch to the dark theme");
    themeToggleButton.setColour(juce::TextButton::buttonColourId, control);
    themeToggleButton.setColour(juce::TextButton::textColourOffId, textColour);
    themeToggleButton.repaint();

    // A small chip, quieter than a full dropdown but still visibly clickable.
    scaleSummaryButton.setColour(juce::TextButton::buttonColourId, control);
    scaleSummaryButton.setColour(juce::TextButton::buttonOnColourId, getAccentColour().darker(0.2f));
    scaleSummaryButton.setColour(juce::TextButton::textColourOffId, getMutedTextColour());
    scaleSummaryButton.setColour(juce::TextButton::textColourOnId, textColour);
    // The primary preset selector, so its text reads at full strength (unlike the muted
    // scale chip above), matching how the preset name looked in the combo box it replaced.
    for (auto* arrow : { &prevPresetButton, &nextPresetButton, &undoButton, &redoButton })
    {
        arrow->dimColour = getMutedTextColour();
        arrow->litColour = getAccentColour();
        arrow->repaint();
    }

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

    for (auto* button : { &newPresetButton, &savePresetButton, &presetMenuButton, &learnButton, &layerButton,
                          &copyOctaveUpButton, &copyOctaveDownButton })
    {
        button->setColour(juce::TextButton::buttonColourId, control);
        button->setColour(juce::TextButton::buttonOnColourId, getAccentColour().darker(0.2f));
        button->setColour(juce::TextButton::textColourOffId, textColour);
        button->setColour(juce::TextButton::textColourOnId, textColour);
    }

    // Save lights up in the accent while there are unsaved changes (it's greyed out otherwise).
    savePresetButton.setColour(juce::TextButton::textColourOffId, getAccentColour().brighter(0.3f));

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
        // A new user preset (its own file), leaving the one it was copied from at its saved state.
        const bool written = audioProcessor.addPreset(p);
        selectedMappingIndex = -1;
        refreshPresetBox();
        setSelectedMappingIndex(selectedMappingIndex);

        if (! written)
            showSaveFailedWarning();
    });
}

void MnKordAudioProcessorEditor::editActivePresetMetadata()
{
    const int index = audioProcessor.getActivePresetIndex();
    const auto& presets = audioProcessor.getPresets();

    // Factory presets can't be changed (Save as makes an editable copy).
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].isFactory)
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
            mutablePresets[(size_t) index].tags = p.tags;
        }

        // The host's program list shows the new name.
        audioProcessor.notifyHostProgramsChanged();

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
        // Another preset (a host program change): the selection belonged to the old one.
        const bool switched = active != lastSyncedActivePresetIndex;
        refreshPresetBox();
        setSelectedMappingIndex(switched ? -1 : selectedMappingIndex);
    }
    else if ((++timerTick & 3) == 0)
    {
        // Edits mutate the preset in place, so unsaved changes are found by comparing.
        updateDirtyState();
    }

    // The strip follows the keyboard when it scrolls or the scale root changes; repaint it
    // only then, not on every tick.
    const auto stripKey = std::make_pair(inputKeyboard.getLowestVisibleKey(), scaleRoot);
    if (stripKey != lastOctaveStripKey)
    {
        lastOctaveStripKey = stripKey;
        octaveStrip.repaint();
    }
    trackHistory();

    const auto noteStamp = audioProcessor.getInputNoteOnStamp();
    if (noteStamp != learnStamp)
    {
        learnStamp = noteStamp;
        if (learnButton.getToggleState())
        {
            const int note = (int) (noteStamp & 0xffu);
            keyboardNoteClicked(KeyboardRole::input, note);

            // Bring the key into view if it is outside the visible part of the keyboard.
            const auto keyArea = inputKeyboard.getRectangleForKey(note);
            if (keyArea.getX() < 0.0f || keyArea.getRight() > (float) inputKeyboard.getWidth())
                inputKeyboard.setLowestVisibleKey(juce::jlimit(21, 108, note - 12));
        }
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
            showMessage("Import failed", "Could not read any presets from:\n" + file.getFullPathName());
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
            showMessage("Export failed", "Could not write:\n" + file.getFullPathName());
    });
}

void MnKordAudioProcessorEditor::promptPresetMetadata(const Preset& seed,
                                                       bool showGenerationOptions,
                                                       const std::function<void(const Preset&, const DiatonicFillOptions&)>& onSave)
{
    auto* dialog = new juce::AlertWindow("Save preset", "", juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&flatLookAndFeel);
    // A child of the editor rather than its own desktop window, so it stays inside the plugin
    // window; added before the content so its layout centres it within the editor.
    addChildComponent(dialog);
    trackDialog(dialog);

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
                                const auto tags = content->getPresetTags();
                                const auto fillOptions = content->getFillOptions();
                                dialog->setLookAndFeel(nullptr);
                                delete content;

                                if (result != 1 || name.isEmpty())
                                    return;

                                Preset preset = seed;
                                preset.name = name;
                                preset.group = group;
                                preset.author = author;
                                preset.tags = tags;
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
    {
        selectedMappingIndex = -1;
    }
    else if (index < 0)
    {
        // The first mapping of the layer being edited, or none: selecting another layer's mapping
        // would let the CHORD keys edit it unnoticed.
        const auto& mappings = presets[(size_t) presetIndex].mappings;
        const auto first = std::find_if(mappings.begin(), mappings.end(), [this](const NoteMapping& m)
                                        { return (m.minVelocity > 1) == hardLayerEditing; });
        selectedMappingIndex = first != mappings.end() ? (int) std::distance(mappings.begin(), first) : -1;
    }
    else
    {
        selectedMappingIndex = juce::jlimit(0, (int) presets[(size_t) presetIndex].mappings.size() - 1, index);
    }

    refreshKeyboardHighlights();
    refreshSummaryLabels();
    updatePresetMetaLabel();
}

void MnKordAudioProcessorEditor::keyboardNoteClicked(KeyboardRole role, int midiNoteNumber)
{
    // The mapping to select (INPUT), found or created under the lock; the UI is refreshed after
    // releasing it, so the audio thread isn't kept waiting on repaints and label updates.
    int selection = -1;

    {
        const juce::ScopedLock lock(audioProcessor.getStateLock());

        auto& presets = audioProcessor.getPresets();
        const int presetIndex = audioProcessor.getActivePresetIndex();

        if (presetIndex < 0 || presetIndex >= (int) presets.size())
            return;

        auto& preset = presets[(size_t) presetIndex];

        if (role == KeyboardRole::input)
        {
            for (int i = 0; i < (int) preset.mappings.size() && selection < 0; ++i)
                if (preset.mappings[(size_t) i].inputNote == midiNoteNumber
                    && (preset.mappings[(size_t) i].minVelocity > 1) == hardLayerEditing)
                    selection = i;

            if (selection < 0)
            {
                NoteMapping mapping;
                mapping.inputNote = midiNoteNumber;
                if (hardLayerEditing)
                {
                    // A new hard layer starts as a copy of the key's soft layer.
                    mapping.minVelocity = hardLayerThreshold;
                    for (const auto& other : preset.mappings)
                        if (other.inputNote == midiNoteNumber && other.minVelocity <= 1)
                        {
                            mapping.outputNotes = other.outputNotes;
                            mapping.outputVelocities = other.outputVelocities;
                            break;
                        }
                }
                preset.mappings.push_back(mapping);
                selection = (int) preset.mappings.size() - 1;
            }
        }
        else if (auto* mapping = getSelectedMapping())
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

    if (role == KeyboardRole::input)
    {
        setSelectedMappingIndex(selection);
        return;
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

    menu.showMenuAsync(juce::PopupMenu::Options().withParentComponent(this), [safeThis](int result)
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
    if (getSelectedMapping() == nullptr)
        return;

    auto menu = makeStyledMenu();
    menu.addItem(1, "Invert up");
    menu.addItem(2, "Invert down");

    juce::Component::SafePointer<MnKordAudioProcessorEditor> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options().withParentComponent(this), [safeThis](int result)
    {
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
            return;

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
    }

    // The neighbouring index may belong to the other velocity layer; go to the first of this one.
    setSelectedMappingIndex(-1);
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
        selectedInputLabel.setText(displayNoteName(mapping->inputNote)
                                       + (mapping->minVelocity > 1 ? "   hard layer, velocity " + juce::String(mapping->minVelocity) + "+"
                                                                   : juce::String()),
                                   juce::dontSendNotification);
        // The chord's name (with its bass: "Cm/G", "C/D") and its notes, lowest first.
        const auto symbol = ChordSymbol::nameFor(mapping->outputNotes);
        const auto notes = noteListToString(mapping->outputNotes);
        chordNotesLabel.setText(symbol.isNotEmpty() ? symbol + "   (" + notes + ")" : notes,
                                juce::dontSendNotification);
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
        case VisualTheme::Light: return juce::Colour(0xffeef2f0);
        default: return juce::Colour(0xff101315);
    }
}

juce::Colour MnKordAudioProcessorEditor::getPanelColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xffffffff);
        default: return juce::Colour(0xff191e21);
    }
}

juce::Colour MnKordAudioProcessorEditor::getPanelLineColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xffd8dfdc);
        default: return juce::Colour(0xff2a3237);
    }
}

juce::Colour MnKordAudioProcessorEditor::getTextColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xff17201b);
        default: return juce::Colour(0xffeef3ef);
    }
}

juce::Colour MnKordAudioProcessorEditor::getMutedTextColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xff66736d);
        default: return juce::Colour(0xff95a09a);
    }
}

juce::Colour MnKordAudioProcessorEditor::getAccentColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xff11b85f);
        default: return juce::Colour(0xff2ee66b);
    }
}

juce::Colour MnKordAudioProcessorEditor::getControlColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xffdde5e1);
        default: return juce::Colour(0xff242b2f);
    }
}

juce::Colour MnKordAudioProcessorEditor::getWhiteKeyTopColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xffffffff);
        default: return juce::Colour(0xfffbfcfb);
    }
}

juce::Colour MnKordAudioProcessorEditor::getWhiteKeyBottomColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xffdce3df);
        default: return juce::Colour(0xffd7ddd8);
    }
}

juce::Colour MnKordAudioProcessorEditor::getBlackKeyTopColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xff26302d);
        default: return juce::Colour(0xff252b2e);
    }
}

juce::Colour MnKordAudioProcessorEditor::getBlackKeyBottomColour() const
{
    switch (visualTheme)
    {
        case VisualTheme::Light: return juce::Colour(0xff0e1211);
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
