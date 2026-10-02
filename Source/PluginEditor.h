#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class MnKordAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::Button::Listener,
                                   public juce::Timer
{
public:
    MnKordAudioProcessorEditor(MnKordAudioProcessor&);
    ~MnKordAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Listener callbacks
    void buttonClicked(juce::Button*) override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override;

    // Restricts which notes are clickable/highlighted on the INPUT keyboard, to make it
    // easier to build diatonic mappings without hitting out-of-scale keys by accident.
    // Public so the (free-standing) New-preset dialog content component can reference it.
    enum class Scale
    {
        Chromatic,      // no restriction (all 12 notes)
        Major,
        NaturalMinor,
        HarmonicMinor,
        MelodicMinor,
        MajorPentatonic,
        MinorPentatonic
    };

    // Velocities generated chords start with, by the role each note plays: the stacked chord
    // tones R, 3, 5, 7 (by position in the stack, so "3" is the third whether minor or major),
    // plus the optional root added just below the chord (bass) and just above it (top).
    // Only applied when chords are generated; afterwards each note is edited on the CHORD keys.
    struct InitVoicing
    {
        std::array<int, 4> toneVelocities { 100, 100, 100, 100 };   // R, 3, 5, 7
        bool addBass = false;
        int bassVelocity = 100;
        bool addTop = false;
        int topVelocity = 100;
    };

    // Edits an InitVoicing: one drag-up/down velocity per role (R, 3, 5, 7, Bass, Top), where
    // the Bass/Top column headers are toggles that add those notes. Shared by the New-preset
    // dialog and the scale popup's Fill section.
    struct InitVoicingPanel : public juce::Component
    {
        InitVoicingPanel();
        void resized() override;
        void setColours(juce::Colour text, juce::Colour muted, juce::Colour control, juce::Colour accent);
        void setVoicing(const InitVoicing& voicing);
        InitVoicing getVoicing() const;

        std::function<void()> onChange;
        static constexpr int preferredHeight = 52;

    private:
        std::array<juce::Label, 4> toneLabels;
        juce::TextButton bassButton { "Bass" };
        juce::TextButton topButton { "Top" };
        std::array<juce::Slider, 6> velocitySliders;   // R, 3, 5, 7, Bass, Top
    };

    // Options for populateDiatonicChords(), gathered from the New-preset dialog or the scale
    // popup's Fill.
    struct DiatonicFillOptions
    {
        int scaleRoot = 0;
        Scale scaleType = Scale::Chromatic;
        int chordSize = 3;      // 3 (triad) or 4 (seventh chord)
        int inversion = 0;      // number of times to invert upward (0-3)
        int octaveShift = 0;    // octaves to transpose the generated chord (-2..+2)
        int lowestInputNote = 0;     // only INPUT notes in [lowest, highest] get a mapping
        int highestInputNote = 127;
        InitVoicing voicing;
    };

    // Maps Scale <-> the scale-type combo box item index (0 = Full range/Chromatic). Public,
    // stateless, and side-effect-free, so it can be the single shared mapping for
    // updateScaleSummaryText(), ScalePopupContent, and the (free-standing) New-preset dialog
    // content component, instead of each keeping its own copy.
    static int scaleTypeIndexFor(Scale scale);
    static Scale scaleTypeForIndex(int index);
    static juce::String scaleTypeName(Scale scale);

private:
    enum class KeyboardRole
    {
        input,
        output
    };

    enum class VisualTheme
    {
        Dark,
        Light
    };

    // Dark/light switch for the header: draws the icon of the theme a click switches to (a
    // sun while dark, a moon while light).
    struct ThemeToggleButton : public juce::Button
    {
        ThemeToggleButton() : juce::Button({}) {}
        void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

        bool showSun = true;
    };

    struct FlatLookAndFeel : public juce::LookAndFeel_V4
    {
        juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
        void drawButtonBackground(juce::Graphics&,
                                  juce::Button&,
                                  const juce::Colour& backgroundColour,
                                  bool shouldDrawButtonAsHighlighted,
                                  bool shouldDrawButtonAsDown) override;
        void drawComboBox(juce::Graphics&,
                          int width,
                          int height,
                          bool isButtonDown,
                          int buttonX,
                          int buttonY,
                          int buttonW,
                          int buttonH,
                          juce::ComboBox&) override;
        void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
        void drawPopupMenuBackgroundWithOptions(juce::Graphics&,
                                                int width,
                                                int height,
                                                const juce::PopupMenu::Options&) override;
        void drawPopupMenuItem(juce::Graphics&,
                               const juce::Rectangle<int>& area,
                               bool isSeparator,
                               bool isActive,
                               bool isHighlighted,
                               bool isTicked,
                               bool hasSubMenu,
                               const juce::String& text,
                               const juce::String& shortcutKeyText,
                               const juce::Drawable* icon,
                               const juce::Colour* textColour) override;
        int getPopupMenuBorderSize() override;
        // Keeps popup menus (incl. combo box dropdowns) inside the plugin window instead of
        // opening as separate top-level windows that can hang off its edges.
        juce::Component* getParentComponentForMenuOptions(const juce::PopupMenu::Options&) override;
        int getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&) override;
        // Without this, JUCE sizes popup items from the raw string width, which is narrower
        // than the 28px-per-side margin drawPopupMenuItem() reserves for the text -- causing
        // longer labels (e.g. "Sus2", "Maj7", "Add9") to be ellipsised.
        void getIdealPopupMenuItemSize(const juce::String& text,
                                       bool isSeparator,
                                       int standardMenuItemHeight,
                                       int& idealWidth,
                                       int& idealHeight) override;
    };

    struct MappingKeyboard : public juce::MidiKeyboardComponent
    {
        MappingKeyboard(MnKordAudioProcessorEditor& ownerRef,
                        KeyboardRole roleRef,
                        juce::MidiKeyboardState& stateRef);

        bool mouseDownOnKey(int midiNoteNumber, const juce::MouseEvent& event) override;
        bool mouseDraggedToKey(int midiNoteNumber, const juce::MouseEvent& event) override;

        // On the CHORD keyboard, a left-button drag up/down that starts on a note of the
        // selected chord sets that note's velocity instead of toggling it; a click without
        // any vertical movement still toggles it off as before.
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        int selectedChordNoteAt(juce::Point<float> position);
        // Shows the velocity bubble over the selected chord note under position, or hides it.
        void updateVelocityBubble(juce::Point<float> position);

        void drawWhiteNote(int midiNoteNumber,
                           juce::Graphics& g,
                           juce::Rectangle<float> area,
                           bool isDown,
                           bool isOver,
                           juce::Colour lineColour,
                           juce::Colour textColour) override;
        void drawBlackNote(int midiNoteNumber,
                           juce::Graphics& g,
                           juce::Rectangle<float> area,
                           bool isDown,
                           bool isOver,
                           juce::Colour noteFillColour) override;

        MnKordAudioProcessorEditor& owner;
        KeyboardRole role;

        int velocityDragNote = -1;      // -1 = no velocity drag in progress
        int velocityDragStart = NoteMapping::defaultOutputVelocity;
        bool velocityDragMoved = false;
    };

    // A thin strip above the INPUT keyboard with one segment per octave (starting on the scale
    // root, like the auto-fill's octaves). Clicking a segment selects that octave so its mappings
    // can be copied an octave up or down; clicking it again deselects.
    struct OctaveStrip : public juce::Component
    {
        explicit OctaveStrip(MnKordAudioProcessorEditor& ownerRef) : owner(ownerRef) {}
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        // Segment [start, start + 12) containing x, or -1.
        int octaveStartAt(float x) const;
        // The x range of the segment starting at the given note, clipped to the strip.
        juce::Range<float> segmentRange(int start) const;

        MnKordAudioProcessorEditor& owner;
        int selectedStart = -1;
    };

    // Tooltip-style bubble showing a selected chord note's velocity, floated above its key
    // while hovering or dragging it. Lives on the editor (not the keyboard) so it can sit above
    // the keyboard's top edge.
    struct VelocityBubble : public juce::Component
    {
        explicit VelocityBubble(MnKordAudioProcessorEditor& ownerRef) : owner(ownerRef) {}
        void paint(juce::Graphics&) override;

        MnKordAudioProcessorEditor& owner;
        int velocity = NoteMapping::defaultOutputVelocity;
        static constexpr int width = 48, bodyHeight = 34, pointerHeight = 6;
    };

    // Content shown in the CallOutBox opened by clicking scaleSummaryButton: the root and
    // scale-type pickers (live-applied on change, no Save/Cancel needed), plus the Fill
    // triads/7ths actions and how many octaves below/above the middle octave they cover.
    //
    // The CallOutBox that hosts this is owned by JUCE's modal-component machinery, not by the
    // editor (it's only parented to it for display), so it can outlive the editor if the host
    // closes the plugin UI while it's still open. owner is therefore a SafePointer, checked
    // before every use, rather than a raw reference.
    struct ScalePopupContent : public juce::Component, private juce::ComboBox::Listener
    {
        // A "caption  [-] n [+]" row editing one of the editor's octave-count fields,
        // clamped to 0..maxFillOctaves. The caption is set by updateStepperCaptions(), since
        // it names the current root.
        struct OctaveStepper : public juce::Component
        {
            OctaveStepper(ScalePopupContent& popupRef, int MnKordAudioProcessorEditor::* fieldRef);
            void resized() override;
            void change(int delta);

            ScalePopupContent& popup;
            int MnKordAudioProcessorEditor::* field;
            juce::Label captionLabel, valueLabel;
            juce::TextButton downButton { "-" };
            juce::TextButton upButton { "+" };
        };

        explicit ScalePopupContent(MnKordAudioProcessorEditor& ownerRef);
        void resized() override;
        void comboBoxChanged(juce::ComboBox*) override;

        // Fill buttons only make sense with an actual scale selected ("Full range" has no
        // diatonic chords to stack), so they're disabled otherwise.
        void updateFillButtons();
        void updateStepperCaptions();

        juce::Component::SafePointer<MnKordAudioProcessorEditor> owner;
        juce::ComboBox rootBox, typeBox;
        OctaveStepper belowStepper { *this, &MnKordAudioProcessorEditor::fillOctavesBelow };
        OctaveStepper aboveStepper { *this, &MnKordAudioProcessorEditor::fillOctavesAbove };
        InitVoicingPanel voicingPanel;
        juce::TextButton fillTriadsButton { "Fill triads" };
        juce::TextButton fillSeventhsButton { "Fill 7ths" };
    };

    // Content shown in the CallOutBox opened by clicking presetSummaryButton: GROUP, AUTHOR and
    // STYLE (tag) columns that filter the PRESETS column (each row has a favourite star), plus a search box,
    // a favourites-only toggle and tick / cross buttons. Clicking a preset loads it and keeps the
    // browser open so presets can be auditioned; the tick keeps the current one, the cross goes
    // back to the preset the browser was opened on. See ScalePopupContent above for why owner is
    // a SafePointer.
    struct PresetBrowserContent : public juce::Component,
                                  private juce::Timer
    {
        // A small icon button: tick, cross, a star (toggle), or a previous / next chevron.
        struct IconButton : public juce::Button
        {
            enum class Kind { Check, Cross, Star, Prev, Next, Undo, Redo };

            explicit IconButton(Kind k) : juce::Button("icon"), kind(k) {}
            void paintButton(juce::Graphics&, bool highlighted, bool down) override;

            Kind kind;
            juce::Colour dimColour, litColour;
        };

        // Item 0 is always "All"; items 1.. are the distinct group / author / style-tag labels (built from
        // the preset list at the time the browser was opened).
        struct FilterColumn : public juce::ListBoxModel
        {
            explicit FilterColumn(PresetBrowserContent& ownerRef);
            int getNumRows() override { return items.size(); }
            void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
            void listBoxItemClicked(int row, const juce::MouseEvent&) override;

            PresetBrowserContent& owner;
            juce::StringArray items;
            int selected = 0;
            juce::ListBox box;
        };

        // The filtered presets (indices into the processor's list). A click on the star area
        // toggles the favourite, a click elsewhere loads the preset.
        struct PresetColumn : public juce::ListBoxModel
        {
            explicit PresetColumn(PresetBrowserContent& ownerRef)
                : owner(ownerRef), box("BrowserPresets", this) {}
            int getNumRows() override;
            void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
            void listBoxItemClicked(int row, const juce::MouseEvent&) override;
            void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

            PresetBrowserContent& owner;
            std::vector<int> rows;
            juce::ListBox box;
        };

        explicit PresetBrowserContent(MnKordAudioProcessorEditor& ownerRef);
        void paint(juce::Graphics&) override;
        void resized() override;
        void timerCallback() override;
        // Re-filters the PRESETS column from both columns, the favourites toggle and the search text.
        void rebuildRows();
        void closeBrowser();

        juce::Component::SafePointer<MnKordAudioProcessorEditor> owner;
        FilterColumn groupColumn;
        FilterColumn authorColumn;
        FilterColumn styleColumn;
        PresetColumn presetColumn;
        juce::TextEditor searchBox;
        IconButton favoritesOnlyButton;
        IconButton confirmButton;
        IconButton cancelButton;
        int startIndex = -1;
        juce::Rectangle<int> headerArea;
    };

    //==============================================================================
    MnKordAudioProcessor& audioProcessor;

    // Services setTooltip() on the TOOLS-row icon buttons (Undo/Redo/Octave); without a
    // TooltipWindow instance somewhere in the hierarchy, JUCE never shows tooltips at all.
    juce::TooltipWindow tooltipWindow { this };

    // Preset controls
    FlatLookAndFeel flatLookAndFeel;
    ThemeToggleButton themeToggleButton;
    // Read-only summary ("C Major"); clicking it opens a small popup to change root/scale.
    // The scale is set-once-per-preset in practice, so it doesn't need permanent dropdowns.
    // The CallOutBox launched by showScalePopup() (owned by the modal machinery, not the
    // editor). Tracked so the destructor can defuse its LookAndFeel pointer (which would
    // otherwise dangle into this soon-to-be-destroyed editor) before returning.
    juce::Component::SafePointer<juce::CallOutBox> activeScalePopup;
    juce::TextButton scaleSummaryButton;
    OctaveStrip octaveStrip { *this };
    juce::TextButton copyOctaveUpButton, copyOctaveDownButton;
    // While on, the next note played on the MIDI input selects (or creates) its mapping.
    juce::TextButton learnButton;
    std::uint32_t learnStamp = 0;
    // Read-only summary of the active preset's name; clicking it opens showPresetBrowser().
    juce::TextButton presetSummaryButton;
    PresetBrowserContent::IconButton undoButton { PresetBrowserContent::IconButton::Kind::Undo };
    PresetBrowserContent::IconButton redoButton { PresetBrowserContent::IconButton::Kind::Redo };
    // Step to the previous / next preset in the library.
    PresetBrowserContent::IconButton prevPresetButton { PresetBrowserContent::IconButton::Kind::Prev };
    PresetBrowserContent::IconButton nextPresetButton { PresetBrowserContent::IconButton::Kind::Next };
    // The CallOutBox launched by showPresetBrowser(), tracked for the same reason as
    // activeScalePopup above.
    juce::Component::SafePointer<juce::CallOutBox> activePresetPopup;
    juce::TextButton newPresetButton;
    // Re-voices every mapping in the active preset for smooth voice leading between chords.
    // Writes the active user preset back to its file; enabled while it has unsaved changes.
    juce::TextButton savePresetButton;
    // Opens the preset menu: Save as, Revert, Rename, Import, Export, Show in folder, Delete.
    juce::TextButton presetMenuButton;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Label selectedInputLabel;
    juce::Label chordNotesLabel;
    juce::Label presetInfoLabel;   // shows "group · author" of the active preset in the header

    // Keyboard editor
    juce::MidiKeyboardState inputKeyboardState;
    juce::MidiKeyboardState outputKeyboardState;
    MappingKeyboard inputKeyboard;
    MappingKeyboard outputKeyboard;
    VelocityBubble velocityBubble { *this };
    juce::Label inputKeyboardLabel;
    juce::Label outputKeyboardLabel;

    int selectedMappingIndex = -1;
    VisualTheme visualTheme = VisualTheme::Dark;
    int scaleRoot = 0;
    Scale scaleType = Scale::Chromatic;
    // Octaves below / above the middle octave (the scale root in octave 4 up to the note
    // before the next root, e.g. D4..C#5) that Fill triads/7ths covers besides that octave
    // (both 0 = middle octave only). Kept here rather than in ScalePopupContent so they
    // survive closing and reopening the popup.
    int fillOctavesBelow = 1;
    int fillOctavesAbove = 1;
    // The voicing generated chords start with, shared by Fill and the New-preset dialog (each
    // edits and remembers this same one) and kept for the whole editor session.
    InitVoicing initVoicing;
    static constexpr int maxFillOctaves = 3;   // C1..B7, inside the keyboards' 21-108 range

    // What refreshPresetBox() last synced presetBox to, so timerCallback() can detect a
    // host/external change without reading back through presetBox's own (filtered/grouped,
    // so no longer 1:1 with the preset list) item count or selection.
    int lastSyncedActivePresetIndex = -2;
    int lastSyncedPresetCount = -1;
    // Whether the active preset had unsaved changes when the chip/Save button last reflected
    // it; polled in timerCallback() because edits mutate presets directly.
    bool lastSyncedDirty = false;

    // Undo history of the active preset's mappings. Edits are noticed by comparing the mappings
    // on the timer rather than at every edit site; edits within a short window merge into one step.
    // The history is dropped whenever another preset becomes active.
    std::vector<std::vector<NoteMapping>> undoStack, redoStack;
    std::vector<NoteMapping> historyMappings;
    juce::String historyKey;
    juce::uint32 lastEditTimeMs = 0;
    int timerTick = 0;

    // Notes currently being played (INPUT) and the chord notes sounding for them (CHORD), as
    // last polled from the processor by timerCallback(); drawn as a live overlay on the keys.
    MnKordAudioProcessor::NoteBits playingInputNotes, playingOutputNotes;

    //==============================================================================
    // Updates presetSummaryButton's text (with a marker while the preset has unsaved changes)
    // and the Save button from the current preset list/active index. Call after any preset
    // add/remove/rename/save.
    void refreshPresetBox();
    void updateDirtyState();
    // Refreshes scaleSummaryButton's text ("Full range" / "C Major" / ...) from the current
    // scaleRoot/scaleType.
    void updateScaleSummaryText();
    void showScalePopup();
    void showPresetBrowser();
    // Sets the active preset and clears the current mapping selection, since it belonged to
    // the previously active preset. Used by PresetBrowserContent's row click; asks about
    // unsaved changes first.
    void selectActivePreset(int index);
    // Calls proceed() now if the active preset has no unsaved changes; otherwise asks first
    // (user preset: Save / Discard / Cancel, factory preset: Discard / Cancel) and calls it on
    // Save or Discard. Used before anything that switches away from the active preset.
    void confirmLeavePreset(std::function<void()> proceed);
    // "Save": writes the active user preset to its file; a factory or never-saved preset goes
    // through Save as instead.
    void saveActivePreset();
    void revertActivePreset();
    void deleteActivePreset();
    void showPresetMenu();
    void trackHistory();
    void undoEdit();
    void redoEdit();
    void updateHistoryButtons();
    // Right-click menu of a row in the preset browser (Load, Duplicate, Rename, Show in folder, Delete).
    void showPresetRowMenu(int presetIndex);
    void duplicatePreset(int presetIndex);
    // Selects the previous (-1) / next (+1) preset, wrapping around.
    void stepPreset(int delta);
    // Shifts the active preset's INPUT keys and/or chord notes; refuses if a note would leave 0-127.
    void transposePreset(int semitones, bool inputs, bool outputs);
    // Shows an OK-only message dialog inside the plugin window.
    void showMessage(const juce::String& title, const juce::String& message);
    void showSaveFailedWarning();
    // Dialogs are children of the editor but modal, so any still open when the editor goes
    // away must be dismissed first; they're tracked here for that.
    void trackDialog(juce::Component* dialog);
    juce::Array<juce::Component::SafePointer<juce::Component>> openDialogs;
    void applyVisualTheme();
    bool isInSelectedScale(int midiNoteNumber) const;
    static const std::vector<int>& scaleIntervals(Scale scale);
    // The "mnKord" header title uses Montserrat; every other piece of text in the UI goes
    // through FlatLookAndFeel::getTypefaceForFont(), which uses Roboto Condensed.
    static juce::Typeface::Ptr getTitleTypeface();
    // Fills, for every in-scale INPUT note across the keyboard, a tertian-style chord built by
    // stacking scale steps (root/+2/+4[/+6] scale degrees), then applies the requested initial
    // inversion and octave transposition. Used when creating a new preset while a scale
    // (other than Full range) is selected.
    static void populateDiatonicChords(Preset& preset, const DiatonicFillOptions& options);
    // Replaces every mapping in the active preset with diatonic chords (chordSize 3 = triads,
    // 4 = sevenths) built from the current scaleRoot/scaleType, on INPUT notes from the root
    // in octave 4 (minus fillOctavesBelow octaves) up to the note before the next root (plus
    // fillOctavesAbove octaves). No-op when scaleType is Chromatic.
    void fillActivePresetFromScale(int chordSize);
    void saveCurrentPresetAs();
    // Renames/re-tags the active preset in place (name/group/author only), unlike
    // saveCurrentPresetAs() which duplicates it into a new preset.
    void editActivePresetMetadata();
    void importPresetsFromChooser();
    void exportPresetsFromChooser();
    // Opens a modal dialog asking for name/group/author (and, if showGenerationOptions is set,
    // scale + chord-size + inversion/octave controls for the diatonic auto-fill), then calls
    // onSave with the filled-in preset and the chosen generation options.
    void promptPresetMetadata(const Preset& seed,
                              bool showGenerationOptions,
                              const std::function<void(const Preset&, const DiatonicFillOptions&)>& onSave);
    void updatePresetMetaLabel();
    // juce::PopupMenu doesn't inherit the editor's LookAndFeel automatically -- without this,
    // a menu falls back to stock JUCE grey (see commit that fixed that for the first time).
    juce::PopupMenu makeStyledMenu();
    void setSelectedMappingIndex(int index);
    void keyboardNoteClicked(KeyboardRole role, int midiNoteNumber);
    // Copies the mappings of the octave selected in octaveStrip by +/-12 semitones (INPUT notes
    // and chord notes), replacing mappings already there, and selects the copy.
    void copySelectedOctave(int semitones);
    void updateOctaveCopyButtons();
    // Selects/creates the mapping for midiNoteNumber, then shows a right-click popup menu of
    // chord templates plus Octave up/down, Clear chord and Delete mapping, at the current
    // mouse position; picking one applies it.
    void showChordTemplateMenu(int midiNoteNumber);
    // Right-clicking the CHORD keyboard shows a 2-item popup (Invert up / Invert down) for the
    // selected mapping, in place of dedicated Inv +/- buttons.
    void showInvertMenu();
    void applyChordTemplate(const std::vector<int>& intervals);
    void invertSelectedChord(bool upwards);
    void shiftSelectedChord(int semitones);
    void clearSelectedChord();
    void deleteSelectedMapping();
    // Moves every out-of-scale note in the selected mapping's chord to the nearest note that
    // is in the currently selected scale. No-op when "Full range" (Chromatic) is selected.
    void fitSelectedChordToScale();
    // Re-voices every mapping's chord in the active preset, processed in ascending input-note
    // order, so each chord picks the octave placement (per pitch class) closest to the
    // previous chord's centroid — minimising jumps between consecutive chords ("voice leading").
    void refreshKeyboardHighlights();
    void refreshSummaryLabels();
    bool isHighlightedKey(KeyboardRole role, int midiNoteNumber) const;
    bool isPlayingKey(KeyboardRole role, int midiNoteNumber) const;
    // Velocity of an output note in the selected mapping (default if it has none).
    int getSelectedOutputVelocity(int outputNote) const;
    void setSelectedOutputVelocity(int outputNote, int velocity);
    void showVelocityBubble(const MappingKeyboard& keyboard, int outputNote);
    void hideVelocityBubble();
    bool isMappedInputKey(int midiNoteNumber) const;
    juce::String getOutputDegreeLabel(int midiNoteNumber) const;
    juce::Colour getBackgroundColour() const;
    juce::Colour getPanelColour() const;
    juce::Colour getPanelLineColour() const;
    juce::Colour getTextColour() const;
    juce::Colour getMutedTextColour() const;
    juce::Colour getAccentColour() const;
    // Live "being played" marker; deliberately not the accent green, which marks the mapping
    // selected for editing.
    juce::Colour getPlayingColour() const;
    juce::Colour getControlColour() const;
    juce::Colour getWhiteKeyTopColour() const;
    juce::Colour getWhiteKeyBottomColour() const;
    juce::Colour getBlackKeyTopColour() const;
    juce::Colour getBlackKeyBottomColour() const;
    NoteMapping* getSelectedMapping();
    const NoteMapping* getSelectedMapping() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MnKordAudioProcessorEditor)
};
