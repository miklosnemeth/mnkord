#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class MnKordAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::ComboBox::Listener,
                                   public juce::Button::Listener,
                                   public juce::Timer
{
public:
    MnKordAudioProcessorEditor(MnKordAudioProcessor&);
    ~MnKordAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Listener callbacks
    void comboBoxChanged(juce::ComboBox*) override;
    void buttonClicked(juce::Button*) override;
    void timerCallback() override;

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

    // Options for populateDiatonicChords(), gathered from the New-preset dialog.
    struct DiatonicFillOptions
    {
        int scaleRoot = 0;
        Scale scaleType = Scale::Chromatic;
        int chordSize = 3;      // 3 (triad) or 4 (seventh chord)
        int inversion = 0;      // number of times to invert upward (0-3)
        int octaveShift = 0;    // octaves to transpose the generated chord (-2..+2)
        int lowestInputNote = 0;     // only INPUT notes in [lowest, highest] get a mapping
        int highestInputNote = 127;
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
        Graphite,
        Frost,
        Neon
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
    };

    // Content shown in the CallOutBox opened by clicking scaleSummaryButton: the root and
    // scale-type pickers (live-applied on change, no Save/Cancel needed), plus the Fill
    // triads/7ths actions and how many octaves below/above the middle octave they cover.
    //
    // The CallOutBox that hosts this is launched with a null parent (see showScalePopup()),
    // so it becomes an independent top-level window that can outlive the editor if the host
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
        juce::TextButton fillTriadsButton { "Fill triads" };
        juce::TextButton fillSeventhsButton { "Fill 7ths" };
    };

    // Content shown in the CallOutBox opened by clicking presetSummaryButton: group- and
    // author-filter dropdowns and a search box above a scrollable, group-headed list of
    // presets (see refreshPresetBox()'s grouping). Clicking a preset row applies it and
    // dismisses the popup; clicking a group heading does nothing. See ScalePopupContent above
    // for why owner is a SafePointer.
    struct PresetBrowserContent : public juce::Component,
                                  private juce::ListBoxModel,
                                  private juce::ComboBox::Listener
    {
        // One row of the flattened, filtered, group-headed preset list.
        struct Row
        {
            bool isGroupHeading = false;
            juce::String text;      // group name, or preset name
            int presetIndex = -1;   // valid (>= 0) only when !isGroupHeading
        };

        explicit PresetBrowserContent(MnKordAudioProcessorEditor& ownerRef);
        void resized() override;
        // Re-filters/re-groups from the current preset list, both filter dropdowns and the
        // search text, called on every keystroke in searchBox and every filter dropdown change.
        void rebuildRows();
        void comboBoxChanged(juce::ComboBox*) override;

        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int rowNumber, const juce::MouseEvent&) override;

        juce::Component::SafePointer<MnKordAudioProcessorEditor> owner;
        // Item 1 is always "All groups"/"All authors"; items 2.. are one per distinct
        // preset.group/preset.author (built in the constructor from the preset list at the
        // time the popup was opened).
        juce::ComboBox groupFilterBox;
        juce::ComboBox authorFilterBox;
        juce::TextEditor searchBox;
        juce::ListBox listBox { "PresetBrowser", this };
        std::vector<Row> rows;
    };

    //==============================================================================
    MnKordAudioProcessor& audioProcessor;

    // Services setTooltip() on the TOOLS-row icon buttons (Undo/Redo/Octave); without a
    // TooltipWindow instance somewhere in the hierarchy, JUCE never shows tooltips at all.
    juce::TooltipWindow tooltipWindow { this };

    // Preset controls
    FlatLookAndFeel flatLookAndFeel;
    juce::ComboBox designBox;
    // Read-only summary ("C Major"); clicking it opens a small popup to change root/scale.
    // The scale is set-once-per-preset in practice, so it doesn't need permanent dropdowns.
    // The CallOutBox launched by showScalePopup() (null-parented, so not torn down with the
    // editor's component tree). Tracked so the destructor can defuse its LookAndFeel pointer
    // (which would otherwise dangle into this soon-to-be-destroyed editor) before returning.
    juce::Component::SafePointer<juce::CallOutBox> activeScalePopup;
    juce::TextButton scaleSummaryButton;
    // Read-only summary of the active preset's name; clicking it opens showPresetBrowser().
    juce::TextButton presetSummaryButton;
    // The CallOutBox launched by showPresetBrowser(), tracked for the same reason as
    // activeScalePopup above.
    juce::Component::SafePointer<juce::CallOutBox> activePresetPopup;
    juce::TextButton newPresetButton;
    juce::TextButton editPresetButton;
    // Re-voices every mapping in the active preset for smooth voice leading between chords.
    juce::TextButton compactVoicingButton;
    juce::TextButton savePresetButton;
    juce::TextButton deletePresetButton;
    juce::TextButton importPresetButton;
    juce::TextButton exportPresetButton;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Label selectedInputLabel;
    juce::Label chordNotesLabel;
    juce::Label presetInfoLabel;   // shows "group · author" of the active preset in the header

    // Keyboard editor
    juce::MidiKeyboardState inputKeyboardState;
    juce::MidiKeyboardState outputKeyboardState;
    MappingKeyboard inputKeyboard;
    MappingKeyboard outputKeyboard;
    juce::Label inputKeyboardLabel;
    juce::Label outputKeyboardLabel;

    int selectedMappingIndex = -1;
    VisualTheme visualTheme = VisualTheme::Graphite;
    int scaleRoot = 0;
    Scale scaleType = Scale::Chromatic;
    // Octaves below / above the middle octave (the scale root in octave 4 up to the note
    // before the next root, e.g. D4..C#5) that Fill triads/7ths covers besides that octave
    // (both 0 = middle octave only). Kept here rather than in ScalePopupContent so they
    // survive closing and reopening the popup.
    int fillOctavesBelow = 1;
    int fillOctavesAbove = 1;
    static constexpr int maxFillOctaves = 3;   // C1..B7, inside the keyboards' 21-108 range

    // What refreshPresetBox() last synced presetBox to, so timerCallback() can detect a
    // host/external change without reading back through presetBox's own (filtered/grouped,
    // so no longer 1:1 with the preset list) item count or selection.
    int lastSyncedActivePresetIndex = -2;
    int lastSyncedPresetCount = -1;

    //==============================================================================
    // Updates presetSummaryButton's text and deletePresetButton's enabled state from the
    // current preset list/active index. Call after any preset add/remove/rename.
    void refreshPresetBox();
    void refreshDesignBox();
    // Refreshes scaleSummaryButton's text ("Full range" / "C Major" / ...) from the current
    // scaleRoot/scaleType.
    void updateScaleSummaryText();
    void showScalePopup();
    void showPresetBrowser();
    // Sets the active preset and clears the current mapping selection, since it belonged to
    // the previously active preset. Used by PresetBrowserContent's row click.
    void selectActivePreset(int index);
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
    void compactVoiceLeading();
    void refreshKeyboardHighlights();
    void refreshSummaryLabels();
    bool isHighlightedKey(KeyboardRole role, int midiNoteNumber) const;
    bool isMappedInputKey(int midiNoteNumber) const;
    juce::String getOutputDegreeLabel(int midiNoteNumber) const;
    juce::Colour getBackgroundColour() const;
    juce::Colour getPanelColour() const;
    juce::Colour getPanelLineColour() const;
    juce::Colour getTextColour() const;
    juce::Colour getMutedTextColour() const;
    juce::Colour getAccentColour() const;
    juce::Colour getControlColour() const;
    juce::Colour getWhiteKeyTopColour() const;
    juce::Colour getWhiteKeyBottomColour() const;
    juce::Colour getBlackKeyTopColour() const;
    juce::Colour getBlackKeyBottomColour() const;
    NoteMapping* getSelectedMapping();
    const NoteMapping* getSelectedMapping() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MnKordAudioProcessorEditor)
};
