#pragma once

#include <JuceHeader.h>
#include "LookAndFeelModern.h"

// A small horizontal level meter, colour-coded green -> amber -> red.
class LevelMeter : public juce::Component
{
public:
    void setLevel (float newLevel)
    {
        newLevel = juce::jlimit (0.0f, 1.0f, newLevel);
        if (std::abs (newLevel - level) > 0.01f)
        {
            level = newLevel;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (JamColours::panelAlt);
        g.fillRoundedRectangle (b, 3.0f);

        if (level > 0.001f)
        {
            auto filled = b.withWidth (b.getWidth() * level);
            auto colour = level < 0.7f ? JamColours::teal : (level < 0.9f ? JamColours::amber : JamColours::red);
            g.setColour (colour);
            g.fillRoundedRectangle (filled, 3.0f);
        }
    }

private:
    float level = 0.0f;
};
