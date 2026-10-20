#pragma once

#include <JuceHeader.h>

//==============================================================================
// The look shared by the MNAudio plugins (mnKord and mnArpee): the palette, the sizes of the header
// and of the cards, and the drawing of the window frame, the cards, the brand and the "unsaved" dot.
//
// This file is the same in both repositories (Source/MnAudioUi.h): change it in one, copy it to
// the other. It only needs JUCE; the fonts come from the plugin (each has its own Fonts), so the
// drawing functions take them as arguments.
//==============================================================================
namespace mnaudio::ui
{
// Dark palette with a slightly green tint, used by the whole UI.
struct Palette
{
    static juce::Colour bg()       { return juce::Colour(0xff0b1510); }
    static juce::Colour card()     { return juce::Colour(0xff13211a); }
    static juce::Colour cardHi()   { return juce::Colour(0xff1c3026); }
    static juce::Colour border()   { return juce::Colour(0xff2d4a3a); }
    static juce::Colour text()     { return juce::Colour(0xfff0faf3); }
    static juce::Colour dimText()  { return juce::Colour(0xffb4cdbc); }
    static juce::Colour accent()   { return juce::Colour(0xff3ddc84); }
    static juce::Colour accent2()  { return juce::Colour(0xffff5d5d); }
    static juce::Colour danger()   { return juce::Colour(0xffff7a7a); }
    static juce::Colour cellOff()  { return juce::Colour(0xff22352b); }
    static juce::Colour playHead() { return juce::Colour(0x40e8ffa0); }
};

// Sizes in pixels.
namespace metrics
{
constexpr int margin = 12;          // between the window edge and the cards
constexpr int headerHeight = 56;
constexpr int brandWidth = 170;     // the title with the version under it
constexpr int brandTitleHeight = 38;
constexpr int brandVersionHeight = 16;
constexpr int cardGap = 10;         // between two cards
constexpr float cardRadius = 8.0f;
constexpr float titleSize = 28.0f;
constexpr float versionSize = 13.0f;

// The preset picker: [<] [name] [>]  [New] [Save] [Menu]   [undo] [redo]
constexpr int pickerHeight = 34;
constexpr int arrowWidth = 26;
constexpr int chipWidth = 190;
constexpr int newWidth = 52;
constexpr int saveWidth = 52;
constexpr int menuWidth = 62;
constexpr int historyWidth = 30;
}

// Where the parts of the header go.
struct HeaderRects
{
    juce::Rectangle<int> brand;    // the title
    juce::Rectangle<int> version;  // under the title
    juce::Rectangle<int> prev, preset, next, newButton, save, menu;
    juce::Rectangle<int> undo, redo;
    juce::Rectangle<int> right;    // what is left at the right end (a plugin's own control), may be empty
    juce::Rectangle<int> below;    // the window below the header, inside the margins
};

// Lays out the header in a window of the given size. rightWidth: width kept free at the right end
// for a control of the plugin itself (0 = none).
inline HeaderRects layoutHeader(juce::Rectangle<int> windowBounds, int rightWidth = 0)
{
    using namespace metrics;
    HeaderRects r;

    auto area = windowBounds.reduced(margin);
    auto header = area.removeFromTop(headerHeight);
    r.right = header.removeFromRight(rightWidth);
    auto brand = header.removeFromLeft(brandWidth);
    r.brand = brand.removeFromTop(brandTitleHeight);
    r.version = brand.removeFromTop(brandVersionHeight);

    // The picker and the undo / redo buttons form one group, centred in what is left.
    const int pickerWidth = arrowWidth + 4 + chipWidth + 4 + arrowWidth + 8 + newWidth + 6 + saveWidth + 6 + menuWidth;
    const int groupWidth = pickerWidth + 14 + historyWidth + 6 + historyWidth;
    auto group = header.withSizeKeepingCentre(groupWidth, pickerHeight);
    auto picker = group.removeFromLeft(pickerWidth);
    group.removeFromLeft(14);
    r.undo = group.removeFromLeft(historyWidth);
    group.removeFromLeft(6);
    r.redo = group.removeFromLeft(historyWidth);

    r.prev = picker.removeFromLeft(arrowWidth).reduced(6, 8);
    picker.removeFromLeft(4);
    r.preset = picker.removeFromLeft(chipWidth);
    picker.removeFromLeft(4);
    r.next = picker.removeFromLeft(arrowWidth).reduced(6, 8);
    picker.removeFromLeft(8);
    r.newButton = picker.removeFromLeft(newWidth);
    picker.removeFromLeft(6);
    r.save = picker.removeFromLeft(saveWidth);
    picker.removeFromLeft(6);
    r.menu = picker.removeFromLeft(menuWidth);

    r.below = area;
    return r;
}

// The window background with a one pixel frame.
inline void paintWindow(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.fillAll(Palette::bg());
    g.setColour(Palette::border());
    g.drawRect(bounds, 1);
}

// A card: the panel behind a group of controls.
inline void paintCard(juce::Graphics& g, juce::Rectangle<int> card)
{
    g.setColour(Palette::card());
    g.fillRoundedRectangle(card.toFloat(), metrics::cardRadius);
    g.setColour(Palette::border());
    g.drawRoundedRectangle(card.toFloat(), metrics::cardRadius, 1.0f);
}

// The title in the accent colour with the version under it. The variable font's default instance
// plus synthetic bold is a bit light, so the title is stroked twice with a small offset.
inline void paintBrand(juce::Graphics& g, juce::Rectangle<int> titleArea, juce::Rectangle<int> versionArea,
                       const juce::String& name, const juce::String& version,
                       const juce::Font& titleFont, const juce::Font& versionFont)
{
    g.setColour(Palette::accent());
    g.setFont(titleFont);
    for (const float dx : { 0.0f, 0.6f })
        g.drawText(name, titleArea.toFloat().translated(dx, 0.0f), juce::Justification::centredLeft);

    g.setColour(Palette::dimText());
    g.setFont(versionFont);
    g.drawText(version, versionArea, juce::Justification::centredLeft);
}

// Unsaved changes: a dot at the left of the preset name (draw it over the preset button).
inline void paintDirtyDot(juce::Graphics& g, juce::Rectangle<int> presetButtonBounds)
{
    g.setColour(Palette::accent());
    g.fillEllipse((float) presetButtonBounds.getX() + 10.0f, (float) presetButtonBounds.getCentreY() - 4.0f, 8.0f, 8.0f);
}

// The undo (curved arrow to the left) and redo (to the right) button of the header.
class HistoryButton : public juce::Button
{
public:
    explicit HistoryButton(bool redoIn) : juce::Button(redoIn ? "redo" : "undo"), redo(redoIn) {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(down ? Palette::cardHi().brighter(0.2f) : highlighted ? Palette::cardHi().brighter(0.1f) : Palette::cardHi());
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(Palette::border());
        g.drawRoundedRectangle(r, 5.0f, 1.0f);

        // A curved arrow: the arc, and the head at its end (mirrored for redo).
        const auto c = r.getCentre();
        const float rad = std::min(r.getWidth(), r.getHeight()) * 0.28f;
        juce::Path arc;
        arc.addCentredArc(c.x, c.y + 1.0f, rad, rad, 0.0f, -2.3f, 1.6f, true);
        juce::Path head;
        head.addTriangle(-4.5f, 0.0f, 4.5f, 0.0f, 0.0f, -6.0f);
        g.setColour(isEnabled() ? Palette::text() : Palette::dimText().withAlpha(0.35f));
        juce::AffineTransform flip = redo ? juce::AffineTransform::scale(-1.0f, 1.0f, c.x, c.y) : juce::AffineTransform();
        g.strokePath(arc, juce::PathStrokeType(2.0f), flip);
        const auto tip = arc.getPointAlongPath(0.0f).transformedBy(flip);
        head = head.createPathWithRoundedCorners(1.0f);
        g.fillPath(head, juce::AffineTransform::rotation(redo ? 0.6f : -0.6f).translated(tip.x, tip.y));
    }

private:
    bool redo;
};
} // namespace mnaudio::ui
