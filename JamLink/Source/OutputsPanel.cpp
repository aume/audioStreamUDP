#include "OutputsPanel.h"
#include "LevelMeter.h"

class OutputsPanel::Row : public juce::Component
{
public:
    Row (AudioEngine& engineToUse, PeerLink& linkToUse, OutputRoute routeToUse, std::function<void()> onRemoveCallback)
        : engine (engineToUse), link (linkToUse), route (std::move (routeToUse)), onRemove (std::move (onRemoveCallback))
    {
        addAndMakeVisible (midiButton);
        addAndMakeVisible (delayLabel);
        addAndMakeVisible (channelBox);
        addAndMakeVisible (countBox);
        addAndMakeVisible (hostEditor);
        addAndMakeVisible (portEditor);
        addAndMakeVisible (labelEditor);
        addAndMakeVisible (enabledToggle);
        addAndMakeVisible (meter);
        addAndMakeVisible (removeButton);

        for (auto n : routeChannelCountChoices())
            countBox.addItem (channelCountName (n), n);
        countBox.setSelectedId (route.numChannels, juce::dontSendNotification);
        countBox.setTooltip ("Send a single channel, or a block of adjacent channels as one stream");

        // IPv4, or IPv6 with an interface (peer-to-peer addresses look like
        // "fe80::1c2b:3aff:fe4d:5e6f%awdl0").
        hostEditor.setInputRestrictions (64, "0123456789abcdefghijklmnopqrstuvwxyzABCDEF.:%");
        hostEditor.setTooltip ("Peer's IP address");
        hostEditor.setJustification (juce::Justification::centredLeft);
        hostEditor.setText (route.destHost, juce::dontSendNotification);

        portEditor.setInputRestrictions (5, "0123456789");
        portEditor.setJustification (juce::Justification::centred);
        portEditor.setText (juce::String (route.destPort), juce::dontSendNotification);
        portEditor.setTooltip ("Peer's port (" + juce::String (jamlink::kMinPort) + "-" + juce::String (jamlink::kMaxPort) + ")");

        labelEditor.setJustification (juce::Justification::centredLeft);
        labelEditor.setText (route.label, juce::dontSendNotification);
        labelEditor.setTextToShowWhenEmpty ("Label (optional)", JamColours::textMuted);

        enabledToggle.setButtonText ({});
        enabledToggle.setToggleState (route.enabled, juce::dontSendNotification);

        midiButton.setClickingTogglesState (true);
        midiButton.setToggleState (route.midi, juce::dontSendNotification);
        midiButton.setTooltip ("Also send MIDI to this peer: anything played into the \"JamLink\" MIDI port "
                               "or the MIDI input chosen under Audio Device");
        midiButton.onClick = [this] { pushChange(); };

        delayLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        delayLabel.setJustificationType (juce::Justification::centredRight);
        delayLabel.setColour (juce::Label::textColourId, JamColours::textMuted);

        hostEditor.onFocusLost = [this] { pushChange(); };
        hostEditor.onReturnKey = [this] { pushChange(); };
        portEditor.onFocusLost = [this] { pushChange(); };
        portEditor.onReturnKey = [this] { pushChange(); };
        labelEditor.onFocusLost = [this] { pushChange(); };
        labelEditor.onReturnKey = [this] { pushChange(); };
        channelBox.onChange = [this] { channelBox.setTooltip (channelBox.getText()); pushChange(); };
        countBox.onChange = [this]
        {
            route.numChannels = sanitiseChannelCount (countBox.getSelectedId());
            setChannelLabels (channelLabels);
            pushChange();
        };
        enabledToggle.onClick = [this] { pushChange(); };
        removeButton.onClick = [this] { if (onRemove) onRemove(); };

        removeButton.setColour (juce::TextButton::textColourOffId, JamColours::red);
    }

    void setChannelLabels (const juce::StringArray& labels)
    {
        channelLabels = labels;
        auto numDeviceChannels = juce::jmax (1, labels.size());
        auto width = route.numChannels;
        auto numStarts = juce::jmax (1, numDeviceChannels - width + 1);

        channelBox.clear (juce::dontSendNotification);
        for (int i = 0; i < numStarts; ++i)
            channelBox.addItem (channelRangeName (labels, i, width), i + 1);
        channelBox.setSelectedId (juce::jlimit (1, numStarts, route.sourceChannel + 1), juce::dontSendNotification);

        // Wide enough for the longest entry (e.g. "JamLink Return 1-2"), so
        // names aren't cut off; the full name is also the tooltip.
        auto font = juce::Font (juce::FontOptions (14.0f));
        int widest = 0;
        for (int i = 0; i < channelBox.getNumItems(); ++i)
            widest = juce::jmax (widest, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, channelBox.getItemText (i))));
        channelBoxWidth = juce::jlimit (92, 190, widest + 36);
        channelBox.setTooltip (channelBox.getText());
        resized();
    }

    void updateLevel()
    {
        meter.setLevel (engine.getOutputRouteLevel (route.id));

        auto rtt = link.getRoundTripMs (route.destHost);
        auto name = link.getPeerName (route.destHost);
        delayLabel.setText (rtt >= 0.0 ? "RTT " + juce::String (juce::roundToInt (rtt)) + " ms" : juce::String ("no link"),
                            juce::dontSendNotification);
        delayLabel.setTooltip (rtt >= 0.0
                                 ? "Round trip to " + (name.isNotEmpty() ? name + " (" + route.destHost + ")" : route.destHost)
                                     + ": " + juce::String (rtt, 1) + " ms. Audio reaches them in about half that, plus their buffer."
                                 : "No JamLink control link to " + route.destHost + " yet, so the delay can't be measured "
                                   "and MIDI can't be sent. Is JamLink running there, with TCP port "
                                   + juce::String (PeerLink::kControlPort) + " allowed through its firewall?");
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (JamColours::panelAlt.withAlpha (0.35f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (4);
        auto top = b.removeFromTop (b.getHeight() / 2).withTrimmedBottom (2);
        auto bottom = b.withTrimmedTop (2);

        channelBox.setBounds (top.removeFromLeft (channelBoxWidth));
        top.removeFromLeft (4);
        countBox.setBounds (top.removeFromLeft (74));
        top.removeFromLeft (6);
        removeButton.setBounds (top.removeFromRight (28));
        top.removeFromRight (6);
        portEditor.setBounds (top.removeFromRight (64));
        top.removeFromRight (4);
        hostEditor.setBounds (top);

        enabledToggle.setBounds (bottom.removeFromRight (40));
        bottom.removeFromRight (6);
        meter.setBounds (bottom.removeFromRight (56).withSizeKeepingCentre (56, 8));
        bottom.removeFromRight (6);
        delayLabel.setBounds (bottom.removeFromRight (70));
        bottom.removeFromRight (4);
        midiButton.setBounds (bottom.removeFromRight (50));
        bottom.removeFromRight (6);
        labelEditor.setBounds (bottom);
    }

private:
    void pushChange()
    {
        auto port = portEditor.getText().getIntValue();
        if (jamlink::isValidPort (port))
            route.destPort = port;
        else
            portEditor.setText (juce::String (route.destPort), juce::dontSendNotification);

        route.numChannels = sanitiseChannelCount (countBox.getSelectedId());
        route.sourceChannel = juce::jmax (0, channelBox.getSelectedId() - 1);
        route.destHost = hostEditor.getText().trim();
        route.label = labelEditor.getText();
        route.enabled = enabledToggle.getToggleState();
        route.midi = midiButton.getToggleState();
        engine.updateOutputRoute (route);
    }

    AudioEngine& engine;
    PeerLink& link;
    OutputRoute route;
    std::function<void()> onRemove;
    juce::StringArray channelLabels = numberedLabels ("In", 2);

    juce::ComboBox channelBox, countBox;
    int channelBoxWidth = 92;
    juce::TextEditor hostEditor, portEditor, labelEditor;
    juce::ToggleButton enabledToggle;
    juce::TextButton midiButton { "MIDI" };
    juce::Label delayLabel;
    LevelMeter meter;
    juce::TextButton removeButton { "X" };
};

OutputsPanel::OutputsPanel (AudioEngine& engineToUse, PeerLink& linkToUse) : engine (engineToUse), link (linkToUse)
{
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    subtitleLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    subtitleLabel.setColour (juce::Label::textColourId, JamColours::textMuted);

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (subtitleLabel);
    addAndMakeVisible (addButton);
    addAndMakeVisible (viewport);

    viewport.setViewedComponent (&rowContainer, false);
    viewport.setScrollBarsShown (true, false);

    addButton.onClick = [this] { addNewRoute(); };

    nearbyButton.setTooltip ("Connect to a JamLink found on this network or over peer-to-peer Wi-Fi; "
                             "ports are set up automatically");
    nearbyButton.onClick = [this] { if (onNearbyClicked) onNearbyClicked (nearbyButton); };
    addAndMakeVisible (nearbyButton);

    refreshRows();
    startTimerHz (20);
}

OutputsPanel::~OutputsPanel()
{
    stopTimer();
}

void OutputsPanel::setInputChannelLabels (const juce::StringArray& labels)
{
    channelLabels = labels.isEmpty() ? numberedLabels ("In", 1) : labels;
    for (auto& r : rows)
        r->setChannelLabels (channelLabels);
}

void OutputsPanel::refreshRows()
{
    rows.clear();
    rowContainer.removeAllChildren();

    for (auto& route : engine.getOutputRoutes())
    {
        auto id = route.id;
        auto row = std::make_unique<Row> (engine, link, route, [this, id]
        {
            // Deferred: the row's own button is mid-click, so it mustn't be
            // deleted until that call has returned.
            juce::Component::SafePointer<OutputsPanel> safeThis (this);
            juce::MessageManager::callAsync ([safeThis, id]
            {
                if (auto* panel = safeThis.getComponent())
                {
                    panel->engine.removeOutputRoute (id);
                    panel->refreshRows();
                }
            });
        });
        row->setChannelLabels (channelLabels);
        rowContainer.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    resized();
}

void OutputsPanel::addNewRoute()
{
    auto existing = engine.getOutputRoutes();

    // Next port after the highest one already in use, starting at the default.
    int port = jamlink::kDefaultPort;
    for (auto& r : existing)
        if (r.destPort >= port && r.destPort < jamlink::kMaxPort)
            port = r.destPort + 1;

    OutputRoute r;
    r.id = juce::Uuid();
    r.sourceChannel = 0;
    r.destHost = "127.0.0.1";
    r.destPort = port;
    r.label = "Peer " + juce::String ((int) existing.size() + 1);
    engine.addOutputRoute (r);
    refreshRows();
}

void OutputsPanel::resized()
{
    auto b = getLocalBounds().reduced (16);
    titleLabel.setBounds (b.removeFromTop (24));
    subtitleLabel.setBounds (b.removeFromTop (18));
    b.removeFromTop (8);

    auto buttonArea = b.removeFromBottom (40);
    addButton.setBounds (buttonArea.removeFromLeft (150).withSizeKeepingCentre (150, 30));
    buttonArea.removeFromLeft (8);
    nearbyButton.setBounds (buttonArea.removeFromLeft (150).withSizeKeepingCentre (150, 30));
    b.removeFromBottom (8);

    viewport.setBounds (b);

    const int rowHeight = 76;
    const int containerWidth = juce::jmax (200, viewport.getWidth() - 10);
    rowContainer.setSize (containerWidth, juce::jmax (viewport.getHeight(), (int) rows.size() * rowHeight));

    int y = 0;
    for (auto& r : rows)
    {
        r->setBounds (0, y, containerWidth, rowHeight - 6);
        y += rowHeight;
    }
}

void OutputsPanel::paint (juce::Graphics& g)
{
    g.setColour (JamColours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 10.0f);
    g.setColour (JamColours::border);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 10.0f, 1.0f);
}

void OutputsPanel::timerCallback()
{
    for (auto& r : rows)
        r->updateLevel();
}
