#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "PeerLink.h"

// "Receive From" card: lists every InputRoute (listen port -> output channel)
// and lets the user add, edit or remove them. Several routes can target the
// same output channel; their audio is mixed.
class InputsPanel : public juce::Component, private juce::Timer
{
public:
    InputsPanel (AudioEngine& engineToUse, PeerLink& linkToUse);
    ~InputsPanel() override;

    // One label per device channel, e.g. "Out 1" or "JamLink Return 1".
    void setOutputChannelLabels (const juce::StringArray& labels);
    void refreshRows();

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    class Row;

    void timerCallback() override;
    void addNewRoute();

    AudioEngine& engine;
    PeerLink& link;
    juce::StringArray channelLabels = numberedLabels ("Out", 2);

    juce::Label titleLabel { {}, "Receive From" };
    juce::Label subtitleLabel { {}, "Listen on a port and route incoming audio to an output channel" };
    juce::TextButton addButton { "+ Add Input" };
    juce::ToggleButton alignToggle { "Align streams" };
    juce::Viewport viewport;
    juce::Component rowContainer;
    std::vector<std::unique_ptr<Row>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputsPanel)
};
