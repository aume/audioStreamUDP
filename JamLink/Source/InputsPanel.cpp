#include "InputsPanel.h"
#include "LevelMeter.h"

class InputsPanel::Row : public juce::Component
{
public:
    Row (AudioEngine& engineToUse, PeerLink& linkToUse, InputRoute routeToUse, std::function<void()> onRemoveCallback)
        : engine (engineToUse), link (linkToUse), route (std::move (routeToUse)), onRemove (std::move (onRemoveCallback))
    {
        addAndMakeVisible (delayLabel);
        delayLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        delayLabel.setJustificationType (juce::Justification::centredRight);
        delayLabel.setColour (juce::Label::textColourId, JamColours::textMuted);
        addAndMakeVisible (portEditor);
        addAndMakeVisible (statusDot);
        addAndMakeVisible (channelBox);
        addAndMakeVisible (countBox);
        addAndMakeVisible (labelEditor);
        addAndMakeVisible (enabledToggle);
        addAndMakeVisible (meter);
        addAndMakeVisible (removeButton);

        portEditor.setInputRestrictions (5, "0123456789");
        portEditor.setJustification (juce::Justification::centred);
        portEditor.setText (juce::String (route.listenPort), juce::dontSendNotification);
        portEditor.setTooltip ("Port to listen on (" + juce::String (jamlink::kMinPort) + "-" + juce::String (jamlink::kMaxPort) + ")");

        for (auto n : routeChannelCountChoices())
            countBox.addItem (channelCountName (n), n);
        countBox.setSelectedId (route.numChannels, juce::dontSendNotification);
        countBox.setTooltip ("Output channels this stream plays on. Mono streams are copied to every channel; "
                             "multichannel streams are mixed down on a mono route.");

        labelEditor.setJustification (juce::Justification::centredLeft);
        labelEditor.setText (route.label, juce::dontSendNotification);
        labelEditor.setTextToShowWhenEmpty ("Label (optional)", JamColours::textMuted);

        enabledToggle.setButtonText ({});
        enabledToggle.setToggleState (route.enabled, juce::dontSendNotification);

        statusDot.setJustificationType (juce::Justification::centred);
        statusDot.setFont (juce::Font (juce::FontOptions (16.0f)));

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
        channelBox.setSelectedId (juce::jlimit (1, numStarts, route.destChannel + 1), juce::dontSendNotification);

        // Wide enough for the longest entry (e.g. "JamLink Return 1-2"), so
        // names aren't cut off; the full name is also the tooltip.
        auto font = juce::Font (juce::FontOptions (14.0f));
        int widest = 0;
        for (int i = 0; i < channelBox.getNumItems(); ++i)
            widest = juce::jmax (widest, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, channelBox.getItemText (i))));
        channelBoxWidth = juce::jlimit (100, 190, widest + 36);
        channelBox.setTooltip (channelBox.getText());
        resized();
    }

    void updateLevel()
    {
        meter.setLevel (engine.getInputRouteLevel (route.id));

        auto status = engine.getInputRoutePortStatus (route.id);
        statusDot.setColour (juce::Label::textColourId, status == AudioEngine::PortStatus::listening ? JamColours::teal
                                                                                                     : JamColours::red);
        statusDot.setTooltip (status == AudioEngine::PortStatus::listening
                                ? "Listening on port " + juce::String (route.listenPort)
                                : status == AudioEngine::PortStatus::portInUse
                                    ? "Port " + juce::String (route.listenPort) + " is already in use by another route or app "
                                      "(e.g. SuperCollider). JamLink will pick it up automatically once it's free."
                                    : "Invalid port");

        updateDelay();
    }

    void updateDelay()
    {
        auto info = engine.getInputStreamInfo (route.id);
        if (! info.active)
        {
            delayLabel.setText ("idle", juce::dontSendNotification);
            delayLabel.setTooltip ("No audio arriving on this port");
            return;
        }

        auto rtt = link.getRoundTripMs (info.senderIp);
        auto name = link.getPeerName (info.senderIp);
        auto from = name.isNotEmpty() ? name + " (" + info.senderIp + ")" : info.senderIp;
        auto oneWay = rtt >= 0.0 ? rtt * 0.5 : 0.0;
        auto total = juce::roundToInt (oneWay + info.bufferMs);

        delayLabel.setText ((rtt >= 0.0 ? "" : "~") + juce::String (total) + " ms", juce::dontSendNotification);

        juce::String tip = "From " + from + ": about " + juce::String (total) + " ms in total = ";
        tip << (rtt >= 0.0 ? "network " + juce::String (oneWay, 1) + " ms (half the round trip) + "
                           : juce::String ("network unknown (no JamLink control link) + "));
        tip << "buffer " + juce::String (info.bufferMs, 1) + " ms";
        if (info.alignDelayMs > 0.5)
            tip << " (incl. " + juce::String (info.alignDelayMs, 1) + " ms added to line up with slower streams)";
        tip << ". Sound card buffers on each end add a little more.";
        delayLabel.setTooltip (tip);
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

        portEditor.setBounds (top.removeFromLeft (64));
        top.removeFromLeft (2);
        statusDot.setBounds (top.removeFromLeft (18));
        top.removeFromLeft (4);
        channelBox.setBounds (top.removeFromLeft (channelBoxWidth));
        top.removeFromLeft (4);
        countBox.setBounds (top.removeFromLeft (74));
        removeButton.setBounds (top.removeFromRight (28));

        enabledToggle.setBounds (bottom.removeFromRight (40));
        bottom.removeFromRight (6);
        meter.setBounds (bottom.removeFromRight (56).withSizeKeepingCentre (56, 8));
        bottom.removeFromRight (6);
        delayLabel.setBounds (bottom.removeFromRight (70));
        bottom.removeFromRight (6);
        labelEditor.setBounds (bottom);
    }

private:
    void pushChange()
    {
        auto port = portEditor.getText().getIntValue();
        if (jamlink::isValidPort (port))
            route.listenPort = port;
        else
            portEditor.setText (juce::String (route.listenPort), juce::dontSendNotification);

        route.numChannels = sanitiseChannelCount (countBox.getSelectedId());
        route.destChannel = juce::jmax (0, channelBox.getSelectedId() - 1);
        route.label = labelEditor.getText();
        route.enabled = enabledToggle.getToggleState();
        engine.updateInputRoute (route);
    }

    AudioEngine& engine;
    PeerLink& link;
    InputRoute route;
    std::function<void()> onRemove;
    juce::StringArray channelLabels = numberedLabels ("Out", 2);

    juce::TextEditor portEditor, labelEditor;
    juce::ComboBox channelBox, countBox;
    int channelBoxWidth = 100;
    juce::ToggleButton enabledToggle;
    juce::Label delayLabel;
    juce::Label statusDot { {}, juce::String::fromUTF8 ("\xE2\x97\x8F") };
    LevelMeter meter;
    juce::TextButton removeButton { "X" };
};

InputsPanel::InputsPanel (AudioEngine& engineToUse, PeerLink& linkToUse) : engine (engineToUse), link (linkToUse)
{
    alignToggle.setToggleState (engine.getAlignStreams(), juce::dontSendNotification);
    alignToggle.setTooltip ("Delay faster incoming streams so every stream reaches your speakers with the same "
                            "total delay as the slowest one, keeping all players in time with each other");
    alignToggle.onClick = [this] { engine.setAlignStreams (alignToggle.getToggleState()); };
    addAndMakeVisible (alignToggle);

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

    refreshRows();
    startTimerHz (20);
}

InputsPanel::~InputsPanel()
{
    stopTimer();
}

void InputsPanel::setOutputChannelLabels (const juce::StringArray& labels)
{
    channelLabels = labels.isEmpty() ? numberedLabels ("Out", 1) : labels;
    for (auto& r : rows)
        r->setChannelLabels (channelLabels);
}

void InputsPanel::refreshRows()
{
    alignToggle.setToggleState (engine.getAlignStreams(), juce::dontSendNotification);

    rows.clear();
    rowContainer.removeAllChildren();

    for (auto& route : engine.getInputRoutes())
    {
        auto id = route.id;
        auto row = std::make_unique<Row> (engine, link, route, [this, id]
        {
            // Deferred: the row's own button is mid-click, so it mustn't be
            // deleted until that call has returned.
            juce::Component::SafePointer<InputsPanel> safeThis (this);
            juce::MessageManager::callAsync ([safeThis, id]
            {
                if (auto* panel = safeThis.getComponent())
                {
                    panel->engine.removeInputRoute (id);
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

void InputsPanel::addNewRoute()
{
    auto existing = engine.getInputRoutes();

    int port = engine.findFreeListenPort();

    InputRoute r;
    r.id = juce::Uuid();
    r.listenPort = port;
    r.destChannel = 0;
    r.label = "Peer " + juce::String ((int) existing.size() + 1);
    engine.addInputRoute (r);
    refreshRows();
}

void InputsPanel::resized()
{
    auto b = getLocalBounds().reduced (16);
    alignToggle.setBounds (b.withHeight (24).removeFromRight (150));
    titleLabel.setBounds (b.removeFromTop (24));
    subtitleLabel.setBounds (b.removeFromTop (18));
    b.removeFromTop (8);

    auto buttonArea = b.removeFromBottom (40);
    addButton.setBounds (buttonArea.removeFromLeft (150).withSizeKeepingCentre (150, 30));
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

void InputsPanel::paint (juce::Graphics& g)
{
    g.setColour (JamColours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 10.0f);
    g.setColour (JamColours::border);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 10.0f, 1.0f);
}

void InputsPanel::timerCallback()
{
    for (auto& r : rows)
        r->updateLevel();
}
