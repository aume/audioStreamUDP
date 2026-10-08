#include "LookAndFeelModern.h"

LookAndFeelModern::LookAndFeelModern()
{
    // Start from LookAndFeel_V4's dark scheme so anything not styled below
    // (dialogs, file browser, ...) is dark too.
    setColourScheme (LookAndFeel_V4::getMidnightColourScheme());

    setColour (juce::ResizableWindow::backgroundColourId, JamColours::background);

    setColour (juce::TextButton::buttonColourId, JamColours::panelAlt);
    setColour (juce::TextButton::buttonOnColourId, JamColours::accent);
    setColour (juce::TextButton::textColourOffId, JamColours::text);
    setColour (juce::TextButton::textColourOnId, JamColours::text);

    setColour (juce::ComboBox::backgroundColourId, JamColours::panelAlt);
    setColour (juce::ComboBox::textColourId, JamColours::text);
    setColour (juce::ComboBox::outlineColourId, JamColours::border);
    setColour (juce::ComboBox::arrowColourId, JamColours::textMuted);

    setColour (juce::TextEditor::backgroundColourId, JamColours::panelAlt);
    setColour (juce::TextEditor::textColourId, JamColours::text);
    setColour (juce::TextEditor::outlineColourId, JamColours::border);
    setColour (juce::TextEditor::focusedOutlineColourId, JamColours::accent);

    setColour (juce::Label::textColourId, JamColours::text);
    setColour (juce::ToggleButton::textColourId, JamColours::text);

    setColour (juce::ScrollBar::thumbColourId, JamColours::border);

    setColour (juce::PopupMenu::backgroundColourId, JamColours::panel);
    setColour (juce::PopupMenu::textColourId, JamColours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, JamColours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, JamColours::text);

    setColour (juce::ListBox::backgroundColourId, JamColours::panelAlt);
    setColour (juce::ListBox::outlineColourId, JamColours::border);

    setColour (juce::TooltipWindow::backgroundColourId, JamColours::panelAlt);
    setColour (juce::TooltipWindow::textColourId, JamColours::text);
    setColour (juce::TooltipWindow::outlineColourId, JamColours::border);

    setColour (juce::AlertWindow::backgroundColourId, JamColours::panel);
    setColour (juce::AlertWindow::textColourId, JamColours::text);
    setColour (juce::AlertWindow::outlineColourId, JamColours::border);
}

void LookAndFeelModern::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                               bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto corner = 7.0f;

    auto colour = backgroundColour;
    if (isDown) colour = colour.brighter (0.2f);
    else if (isHighlighted) colour = colour.brighter (0.1f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, corner);

    if (button.getToggleState())
    {
        g.setColour (JamColours::accent.withAlpha (0.7f));
        g.drawRoundedRectangle (bounds, corner, 1.2f);
    }
}

void LookAndFeelModern::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                           bool isHighlighted, bool /*isDown*/)
{
    auto bounds = button.getLocalBounds();
    const float switchWidth = 36.0f, switchHeight = 20.0f;
    juce::Rectangle<float> switchBounds (0.0f, ((float) bounds.getHeight() - switchHeight) * 0.5f, switchWidth, switchHeight);

    auto on = button.getToggleState();
    g.setColour (on ? JamColours::accent : JamColours::panelAlt.brighter (isHighlighted ? 0.15f : 0.0f));
    g.fillRoundedRectangle (switchBounds, switchHeight * 0.5f);
    g.setColour (JamColours::border);
    g.drawRoundedRectangle (switchBounds, switchHeight * 0.5f, 1.0f);

    auto knobDiameter = switchHeight - 6.0f;
    auto knobX = on ? switchBounds.getRight() - knobDiameter - 3.0f : switchBounds.getX() + 3.0f;
    g.setColour (juce::Colours::white);
    g.fillEllipse (knobX, switchBounds.getY() + 3.0f, knobDiameter, knobDiameter);

    if (button.getButtonText().isNotEmpty())
    {
        g.setColour (JamColours::text.withAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawFittedText (button.getButtonText(),
                           (int) switchWidth + 8, 0, bounds.getWidth() - (int) switchWidth - 8, bounds.getHeight(),
                           juce::Justification::centredLeft, 1);
    }
}

void LookAndFeelModern::drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                       int, int, int, int, juce::ComboBox& box)
{
    juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds.reduced (0.5f), 6.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    juce::Rectangle<float> arrowZone ((float) width - 22.0f, 0.0f, 20.0f, (float) height);
    auto cx = arrowZone.getCentreX();
    auto cy = arrowZone.getCentreY();

    juce::Path path;
    path.addTriangle (cx - 4.0f, cy - 2.5f, cx + 4.0f, cy - 2.5f, cx, cy + 3.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (path);
}

void LookAndFeelModern::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f), 6.0f);
}

void LookAndFeelModern::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    auto colour = editor.hasKeyboardFocus (true)
                    ? editor.findColour (juce::TextEditor::focusedOutlineColourId)
                    : editor.findColour (juce::TextEditor::outlineColourId);
    g.setColour (colour);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f), 6.0f, 1.2f);
}

juce::Font LookAndFeelModern::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (14.0f));
}

juce::Font LookAndFeelModern::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions (14.0f));
}

juce::Font LookAndFeelModern::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::FontOptions (14.0f));
}
