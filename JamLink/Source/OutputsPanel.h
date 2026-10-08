#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "PeerLink.h"

// "Send To" card: lists every OutputRoute (input channel -> peer IP:port) and
// lets the user add, edit or remove them.
class OutputsPanel : public juce::Component, private juce::Timer
{
public:
    OutputsPanel (AudioEngine& engineToUse, PeerLink& linkToUse);
    ~OutputsPanel() override;

    // One label per device channel, e.g. "In 1" or "JamLink Send 1".
    void setInputChannelLabels (const juce::StringArray& labels);
    void refreshRows();

    // Called when "+ Nearby..." is clicked, with the button to show a menu from.
    std::function<void (juce::Component& button)> onNearbyClicked;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    class Row;

    void timerCallback() override;
    void addNewRoute();

    AudioEngine& engine;
    PeerLink& link;
    juce::StringArray channelLabels = numberedLabels ("In", 2);

    juce::Label titleLabel { {}, "Send To" };
    juce::Label subtitleLabel { {}, "Route an input channel to a peer's IP address and port" };
    juce::TextButton addButton { "+ Add Output" };
    juce::TextButton nearbyButton { "+ Nearby..." };
    juce::Viewport viewport;
    juce::Component rowContainer;
    std::vector<std::unique_ptr<Row>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputsPanel)
};
