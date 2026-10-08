#pragma once

#include <JuceHeader.h>

// A small, consistent colour palette used throughout the UI.
namespace JamColours
{
    static const juce::Colour background { 0xff14161c };
    static const juce::Colour panel      { 0xff1c1f28 };
    static const juce::Colour panelAlt   { 0xff242938 };
    static const juce::Colour border     { 0xff2c3140 };
    static const juce::Colour accent     { 0xff7c6cf0 };
    static const juce::Colour teal       { 0xff39d9c0 };
    static const juce::Colour amber      { 0xfff2c94c };
    static const juce::Colour red        { 0xffeb5757 };
    static const juce::Colour text       { 0xffe7e9ee };
    static const juce::Colour textMuted  { 0xff9aa0ac };
}

// A compact dark theme: rounded controls, a pill-style toggle switch, and a
// small accent colour used sparingly for focus/selection states.
class LookAndFeelModern : public juce::LookAndFeel_V4
{
public:
    LookAndFeelModern();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                        int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};
