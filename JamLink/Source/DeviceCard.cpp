#include "DeviceCard.h"
#include "LookAndFeelModern.h"
#include "VirtualDevices.h"

namespace
{

    // Network stream rates offered (0 = same as the audio device).
    const juce::Array<double> streamRateChoices { 0.0, 32000.0, 44100.0, 48000.0, 96000.0 };
    const juce::Array<double> jitterMsChoices { 5.0, 10.0, 15.0, 20.0, 30.0, 40.0, 60.0, 80.0, 120.0 };

    juce::String rateName (double rate)
    {
        return juce::String (rate / 1000.0, juce::roundToInt (rate) % 1000 == 0 ? 0 : 1) + " kHz";
    }
}

// A compact list of the device's input or output channels with a switch on
// each, for choosing which channels are active.
class DeviceCard::ChannelList : public juce::Component, private juce::ListBoxModel
{
public:
    ChannelList (juce::AudioDeviceManager& dm, bool isInputToUse) : deviceManager (dm), isInput (isInputToUse)
    {
        list.setModel (this);
        list.setRowHeight (22);
        list.setColour (juce::ListBox::backgroundColourId, JamColours::panelAlt);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);
    }

    void refresh()
    {
        names.clear();
        if (auto* device = deviceManager.getCurrentAudioDevice())
            names = isInput ? device->getInputChannelNames() : device->getOutputChannelNames();
        list.updateContent();
        list.repaint();
    }

    void resized() override { list.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return names.isEmpty() ? 1 : names.size(); }

    juce::BigInteger activeChannels() const
    {
        if (auto* device = deviceManager.getCurrentAudioDevice())
            return isInput ? device->getActiveInputChannels() : device->getActiveOutputChannels();
        return {};
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (names.isEmpty())
        {
            g.setColour (JamColours::textMuted);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText ("(no channels)", 8, 0, width - 8, height, juce::Justification::centredLeft);
            return;
        }

        auto on = activeChannels()[row];
        juce::Rectangle<float> box (8.0f, (float) height * 0.5f - 7.0f, 14.0f, 14.0f);
        g.setColour (on ? JamColours::accent : JamColours::panel);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (JamColours::border);
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        if (on)
        {
            g.setColour (juce::Colours::white);
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
            tick.lineTo (box.getX() + 6.0f, box.getBottom() - 4.0f);
            tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.5f);
            g.strokePath (tick, juce::PathStrokeType (1.6f));
        }

        g.setColour (JamColours::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (juce::String (row + 1) + ".  " + names[row], 30, 0, width - 34, height, juce::Justification::centredLeft);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (names.isEmpty() || row >= names.size())
            return;

        auto setup = deviceManager.getAudioDeviceSetup();
        auto& bits = isInput ? setup.inputChannels : setup.outputChannels;
        bits = activeChannels();
        bits.setBit (row, ! bits[row]);
        (isInput ? setup.useDefaultInputChannels : setup.useDefaultOutputChannels) = false;

        auto error = deviceManager.setAudioDeviceSetup (setup, true);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Audio device", error);
        refresh();
    }

    juce::AudioDeviceManager& deviceManager;
    bool isInput;
    juce::StringArray names;
    juce::ListBox list;
};

DeviceCard::DeviceCard (AudioEngine& engineToUse, MidiBridge& midiToUse, NearbyPeers& nearbyToUse)
    : engine (engineToUse), midi (midiToUse), nearby (nearbyToUse)
{
    titleLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    addAndMakeVisible (titleLabel);

    ipLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    ipLabel.setColour (juce::Label::textColourId, JamColours::textMuted);
    ipLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (ipLabel);

    peerToPeerButton.setClickingTogglesState (true);
    peerToPeerButton.setToggleState (nearby.isPeerToPeer(), juce::dontSendNotification);
    peerToPeerButton.setTooltip ("Lets nearby Macs find and connect to this JamLink directly over Wi-Fi, with no router "
                                 "and without joining the same network (Wi-Fi just needs to be on). Switch it on on "
                                 "every Mac, then use \"+ Nearby...\" under Send To. Leave it off when everyone is on "
                                 "the same network: the peer-to-peer radio adds some latency to ordinary Wi-Fi.");
    peerToPeerButton.onClick = [this]
    {
        nearby.setPeerToPeer (peerToPeerButton.getToggleState());
        updatePeerToPeerButton();
    };
    updatePeerToPeerButton();
    addAndMakeVisible (peerToPeerButton);

    addAndMakeVisible (expandButton);
    expandButton.onClick = [this] { toggleExpanded(); };

    for (auto* l : { &typeLabel, &inputLabel, &outputLabel, &rateLabel, &bufferLabel,
                     &streamRateLabel, &jitterLabel, &activeInputsLabel, &activeOutputsLabel,
                     &midiInLabel, &midiOutLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (13.0f)));
        l->setColour (juce::Label::textColourId, JamColours::textMuted);
        addAndMakeVisible (*l);
    }

    auto& dm = engine.getDeviceManager();

    activeInputs = std::make_unique<ChannelList> (dm, true);
    activeOutputs = std::make_unique<ChannelList> (dm, false);
    addAndMakeVisible (*activeInputs);
    addAndMakeVisible (*activeOutputs);

    for (auto* box : { &typeBox, &inputBox, &outputBox, &rateBox, &bufferBox, &streamRateBox, &jitterBox,
                       &midiInBox, &midiOutBox })
        addAndMakeVisible (*box);

    auto virtualNote = juce::String (midi.hasVirtualPorts() ? "Apps can always play into and record from the \"JamLink\" MIDI port. "
                                                            : "");
    midiInBox.setTooltip (virtualNote + "Optionally also send MIDI from this device (e.g. a keyboard) to peers with MIDI switched on.");
    midiOutBox.setTooltip (virtualNote + "Optionally also play MIDI received from peers out of this device.");
    auto refuseLoop = [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI",
                                                "That would route JamLink's MIDI output back into its input, so MIDI "
                                                "would loop between machines. Pick a different device.");
        refreshMidiDevices();
    };
    midiInBox.onChange = [this, refuseLoop]
    {
        auto index = midiInBox.getSelectedId() - 2;
        if (! midi.setHardwareInput (juce::isPositiveAndBelow (index, midiInputs.size()) ? midiInputs[index].identifier : juce::String()))
            refuseLoop();
        refreshMidiDevices();
    };
    midiOutBox.onChange = [this, refuseLoop]
    {
        auto index = midiOutBox.getSelectedId() - 2;
        if (! midi.setHardwareOutput (juce::isPositiveAndBelow (index, midiOutputs.size()) ? midiOutputs[index].identifier : juce::String()))
            refuseLoop();
        refreshMidiDevices();
    };
    midiListConnection = juce::MidiDeviceListConnection::make ([this] { refreshMidiDevices(); });

    typeBox.onChange = [this]
    {
        auto name = typeBox.getText();
        if (name.isNotEmpty() && name != engine.getDeviceManager().getCurrentAudioDeviceType())
            engine.getDeviceManager().setCurrentAudioDeviceType (name, true);
        refresh();
    };

    inputBox.onChange = [this]
    {
        auto name = inputBox.getSelectedId() > 1 ? inputBox.getText() : juce::String();
        if (hardwareOverride)
        {
            overrideInput = name;
            if (onHardwareChanged) onHardwareChanged (overrideInput, overrideOutput);
            return;
        }

        // "None - JamLink Send only": switch sharing on, with no hardware input.
        if (name.isEmpty() && sharingDevicesReady() && onUseSharingWithHardware)
        {
            auto setup = engine.getDeviceManager().getAudioDeviceSetup();
            onUseSharingWithHardware (juce::String(), setup.outputDeviceName);
            return;
        }

        applySetup ([name] (auto& s) { s.inputDeviceName = name; s.useDefaultInputChannels = true; });
    };

    outputBox.onChange = [this]
    {
        auto name = outputBox.getSelectedId() > 1 ? outputBox.getText() : juce::String();
        if (hardwareOverride)
        {
            overrideOutput = name;
            if (onHardwareChanged) onHardwareChanged (overrideInput, overrideOutput);
            return;
        }

        // "None - JamLink Return only": switch sharing on, with no hardware output.
        if (name.isEmpty() && sharingDevicesReady() && onUseSharingWithHardware)
        {
            auto setup = engine.getDeviceManager().getAudioDeviceSetup();
            onUseSharingWithHardware (setup.inputDeviceName, juce::String());
            return;
        }

        applySetup ([name] (auto& s) { s.outputDeviceName = name; s.useDefaultOutputChannels = true; });
    };

    sharingLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    sharingLabel.setColour (juce::Label::textColourId, JamColours::textMuted);
    sharingStatusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    sharingButton.onClick = [this] { if (onInstallClicked) onInstallClicked(); };
    removeSharingButton.onClick = [this] { if (onRemoveClicked) onRemoveClicked(); };
    removeSharingButton.setColour (juce::TextButton::textColourOffId, JamColours::red);
    useSharingToggle.setTooltip ("Include JamLink Send and JamLink Return in JamLink's routing. They then appear as "
                                 "channels in Send To and Receive From.");
    useSharingToggle.onClick = [this] { if (onUseSharingChanged) onUseSharingChanged (useSharingToggle.getToggleState()); };
    for (auto* c : std::initializer_list<juce::Component*> { &sharingLabel, &sharingStatusLabel, &sharingButton,
                                                             &removeSharingButton, &useSharingToggle })
        addAndMakeVisible (*c);
    setSharingStatus (SharingStatus::unavailable, false);

    rateBox.onChange = [this]
    {
        auto rate = (double) rateBox.getSelectedId();
        if (rate > 0)
            applySetup ([rate] (auto& s) { s.sampleRate = rate; });
    };

    bufferBox.onChange = [this]
    {
        auto size = bufferBox.getSelectedId();
        if (size > 0)
            applySetup ([size] (auto& s) { s.bufferSize = size; });
    };

    for (int i = 0; i < streamRateChoices.size(); ++i)
        streamRateBox.addItem (streamRateChoices[i] == 0.0 ? "Same as device" : rateName (streamRateChoices[i]), i + 1);
    streamRateBox.setTooltip ("Sample rate audio is sent at. Receivers convert to their own device rate, so peers don't need to match.");
    streamRateBox.onChange = [this]
    {
        engine.setStreamSampleRate (streamRateChoices[streamRateBox.getSelectedId() - 1]);
    };

    for (int i = 0; i < jitterMsChoices.size(); ++i)
        jitterBox.addItem (juce::String ((int) jitterMsChoices[i]) + " ms", i + 1);
    jitterBox.setTooltip ("Audio buffered per incoming stream before playback. Raise it if you hear dropouts on Wi-Fi.");
    jitterBox.onChange = [this]
    {
        engine.setJitterBufferMs (jitterMsChoices[jitterBox.getSelectedId() - 1]);
    };

    dm.addChangeListener (this);
    refresh();
    refreshIpAddresses();
    startTimer (2000);
}

DeviceCard::~DeviceCard()
{
    stopTimer();
    engine.getDeviceManager().removeChangeListener (this);
}

void DeviceCard::applySetup (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>& change)
{
    auto& dm = engine.getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    change (setup);

    auto error = dm.setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Audio device", error);
    refresh();
}

void DeviceCard::refresh()
{
    auto& dm = engine.getDeviceManager();
    auto* device = dm.getCurrentAudioDevice();
    auto setup = dm.getAudioDeviceSetup();

    typeBox.clear (juce::dontSendNotification);
    auto& types = dm.getAvailableDeviceTypes();
    for (int i = 0; i < types.size(); ++i)
        typeBox.addItem (types[i]->getTypeName(), i + 1);
    typeBox.setText (dm.getCurrentAudioDeviceType(), juce::dontSendNotification);
    typeBox.setVisible (expanded && types.size() > 1);
    typeLabel.setVisible (typeBox.isVisible());

    auto fillDevices = [&] (juce::ComboBox& box, bool wantInputs, const juce::String& current)
    {
        box.clear (juce::dontSendNotification);
        // With the JamLink devices installed, "none" means JamLink uses only
        // its own device on that side (choosing it switches sharing on). If
        // sharing is off and nothing is selected, it really is just none.
        bool noneMeansJamLink = sharingDevicesReady() && (hardwareOverride || current.isNotEmpty());
        box.addItem (! noneMeansJamLink ? juce::String ("<< none >>")
                                             : wantInputs ? juce::String ("None - JamLink Send only")
                                                          : juce::String ("None - JamLink Return only"), 1);
        if (auto* type = dm.getCurrentDeviceTypeObject())
        {
            auto names = type->getDeviceNames (wantInputs);
            for (int i = 0; i < names.size(); ++i)
                box.addItem (names[i], i + 2);
        }
        // JamLink's own devices aren't offered here: using them as JamLink's
        // hardware would loop audio straight back into it. They're used
        // through "Other apps" below instead.
        bool hasOwnDevice = false;
        for (int i = box.getNumItems(); --i >= 1;)
        {
            if (VirtualDevices::isJamLinkDevice (box.getItemText (i)))
            {
                box.setItemEnabled (box.getItemId (i), false);
                hasOwnDevice = true;
            }
        }
        if (hasOwnDevice)
            box.addSectionHeading (juce::String ("JamLink ") + (wantInputs ? "Return" : "Send")
                                   + " is used from \"Other apps\" below");

        if (current.isEmpty())
            box.setSelectedId (1, juce::dontSendNotification);
        else
            box.setText (current, juce::dontSendNotification);
    };
    fillDevices (inputBox, true, hardwareOverride ? overrideInput : setup.inputDeviceName);
    fillDevices (outputBox, false, hardwareOverride ? overrideOutput : setup.outputDeviceName);

    rateBox.clear (juce::dontSendNotification);
    bufferBox.clear (juce::dontSendNotification);
    if (device != nullptr)
    {
        for (auto rate : device->getAvailableSampleRates())
            rateBox.addItem (rateName (rate), (int) rate);
        rateBox.setSelectedId ((int) device->getCurrentSampleRate(), juce::dontSendNotification);

        auto rate = device->getCurrentSampleRate();
        for (auto size : device->getAvailableBufferSizes())
            bufferBox.addItem (juce::String (size) + " samples (" + juce::String (size * 1000.0 / rate, 1) + " ms)", size);
        bufferBox.setSelectedId (device->getCurrentBufferSizeSamples(), juce::dontSendNotification);
    }

    auto streamIndex = streamRateChoices.indexOf (engine.getStreamSampleRate());
    streamRateBox.setSelectedId (juce::jmax (0, streamIndex) + 1, juce::dontSendNotification);

    auto jitterIndex = jitterMsChoices.indexOf (engine.getJitterBufferMs());
    jitterBox.setSelectedId ((jitterIndex >= 0 ? jitterIndex : jitterMsChoices.indexOf (20.0)) + 1, juce::dontSendNotification);

    activeInputs->refresh();
    activeOutputs->refresh();
    refreshMidiDevices();
    updatePeerToPeerButton();
    resized();
}

void DeviceCard::refreshMidiDevices()
{
    // JamLink's own ports are never offered, and a device already used in
    // the other direction is greyed out, so MIDI can't loop back in.
    midiInputs = midi.getSelectableInputs();
    midiOutputs = midi.getSelectableOutputs();

    auto fill = [] (juce::ComboBox& box, const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& current,
                    const std::function<bool (const juce::MidiDeviceInfo&)>& wouldLoop)
    {
        box.clear (juce::dontSendNotification);
        box.addItem ("JamLink port only", 1);
        int selected = 1;
        for (int i = 0; i < devices.size(); ++i)
        {
            auto isCurrent = devices[i].identifier == current;
            box.addItem (devices[i].name, i + 2);
            box.setItemEnabled (i + 2, isCurrent || ! wouldLoop (devices[i]));
            if (isCurrent)
                selected = i + 2;
        }
        box.setSelectedId (selected, juce::dontSendNotification);
    };
    fill (midiInBox, midiInputs, midi.getHardwareInput(), [this] (auto& d) { return midi.wouldLoopAsInput (d); });
    fill (midiOutBox, midiOutputs, midi.getHardwareOutput(), [this] (auto& d) { return midi.wouldLoopAsOutput (d); });
}

void DeviceCard::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void DeviceCard::updatePeerToPeerButton()
{
    peerToPeerButton.setToggleState (nearby.isPeerToPeer(), juce::dontSendNotification);
    peerToPeerButton.setButtonText (nearby.isPeerToPeer() ? "Peer-to-peer Wi-Fi: On" : "Peer-to-peer Wi-Fi: Off");
}

void DeviceCard::timerCallback()
{
    refreshIpAddresses();
}

void DeviceCard::setHardwareOverride (bool active, const juce::String& input, const juce::String& output)
{
    hardwareOverride = active;
    overrideInput = input;
    overrideOutput = output;
    refresh();
}

bool DeviceCard::sharingDevicesReady() const
{
    return sharingStatus == SharingStatus::installed || sharingStatus == SharingStatus::updateAvailable;
}

void DeviceCard::setSharingStatus (SharingStatus status, bool useWithJamLink)
{
    bool readyChanged = (status == SharingStatus::installed || status == SharingStatus::updateAvailable) != sharingDevicesReady();
    sharingStatus = status;
    if (readyChanged)
        refresh();   // relabel "none" in the Input/Output menus

    juce::String text, buttonText;
    bool showButton = true, showRemove = false, showToggle = false;

    switch (status)
    {
        case SharingStatus::unavailable:     text = "Not included in this build of JamLink"; showButton = false; break;
        case SharingStatus::notInstalled:    text = "Let other apps send and receive audio through JamLink";
                                             buttonText = "Install JamLink audio devices..."; break;
        case SharingStatus::updateAvailable: text = "JamLink audio devices: update available";
                                             buttonText = "Update..."; showRemove = true; showToggle = true; break;
        case SharingStatus::installed:       text = "JamLink Send / JamLink Return installed";
                                             showButton = false; showRemove = true; showToggle = true; break;
        case SharingStatus::notLoaded:       text = "Installed - waiting for macOS to load them...";
                                             showButton = false; showRemove = true; break;
        case SharingStatus::busy:            text = "Working..."; showButton = false; break;
    }

    sharingStatusLabel.setText (text, juce::dontSendNotification);
    sharingButton.setButtonText (buttonText);
    sharingButton.setVisible (expanded && showButton);
    removeSharingButton.setVisible (expanded && showRemove);
    useSharingToggle.setVisible (expanded && showToggle);
    useSharingToggle.setToggleState (useWithJamLink, juce::dontSendNotification);
    sharingStatusLabel.setVisible (expanded);
    sharingLabel.setVisible (expanded);
    resized();
}

void DeviceCard::refreshIpAddresses()
{
    juce::StringArray ips;
    juce::Array<juce::IPAddress> addresses;
    juce::IPAddress::findAllAddresses (addresses);
    for (auto& a : addresses)
        if (! a.isNull() && a.toString() != "127.0.0.1")
            ips.add (a.toString());

    auto text = ips.isEmpty() ? juce::String ("No network connection") : "This device's IP: " + ips.joinIntoString (", ");
    if (text == lastIpText)
        return;

    auto changed = lastIpText.isNotEmpty();
    lastIpText = text;
    ipLabel.setText (text, juce::dontSendNotification);

    if (changed && onNetworkChanged)
        onNetworkChanged();
}

void DeviceCard::toggleExpanded()
{
    expanded = ! expanded;
    expandButton.setButtonText (expanded ? juce::String::fromUTF8 ("\xE2\x96\xB4") : juce::String::fromUTF8 ("\xE2\x96\xB8"));

    for (auto* c : std::initializer_list<juce::Component*> { &inputLabel, &outputLabel, &rateLabel, &bufferLabel,
                                                             &streamRateLabel, &jitterLabel, &activeInputsLabel, &activeOutputsLabel,
                                                             &midiInLabel, &midiOutLabel, &midiInBox, &midiOutBox,
                                                             &inputBox, &outputBox, &rateBox, &bufferBox, &streamRateBox, &jitterBox,
                                                             activeInputs.get(), activeOutputs.get() })
        c->setVisible (expanded);

    setSharingStatus (sharingStatus, useSharingToggle.getToggleState());

    refresh();
    if (onToggleExpanded)
        onToggleExpanded();
}

int DeviceCard::getPreferredHeight() const
{
    if (! expanded)
        return 64;
    return 64 + 8 + 6 * 34 + 2 * 74 + (typeBox.isVisible() ? 34 : 0);
}

void DeviceCard::paint (juce::Graphics& g)
{
    g.setColour (JamColours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 10.0f);
    g.setColour (JamColours::border);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 10.0f, 1.0f);
}

void DeviceCard::resized()
{
    auto b = getLocalBounds().reduced (16);
    auto header = b.removeFromTop (32);
    expandButton.setBounds (header.removeFromRight (28));
    header.removeFromRight (8);
    peerToPeerButton.setBounds (header.removeFromRight (190).withSizeKeepingCentre (190, 28));
    header.removeFromRight (8);
    titleLabel.setBounds (header.removeFromLeft (160));
    ipLabel.setBounds (header);

    if (! expanded)
        return;

    b.removeFromTop (8);
    const int labelWidth = 110;

    auto row = [&] (juce::Label& label, juce::Component& control, int height)
    {
        auto r = b.removeFromTop (height);
        b.removeFromTop (height > 30 ? 8 : 4);
        label.setBounds (r.removeFromLeft (labelWidth).withHeight (30));
        control.setBounds (r);
    };

    auto pairRow = [&] (juce::Label& l1, juce::Component& c1, juce::Label& l2, juce::Component& c2)
    {
        auto r = b.removeFromTop (30);
        b.removeFromTop (4);
        auto left = r.removeFromLeft (r.getWidth() / 2 - 8);
        r.removeFromLeft (16);
        l1.setBounds (left.removeFromLeft (labelWidth));
        c1.setBounds (left);
        l2.setBounds (r.removeFromLeft (labelWidth));
        c2.setBounds (r);
    };

    if (typeBox.isVisible())
        row (typeLabel, typeBox, 30);
    row (inputLabel, inputBox, 30);
    row (outputLabel, outputBox, 30);
    pairRow (rateLabel, rateBox, bufferLabel, bufferBox);
    pairRow (streamRateLabel, streamRateBox, jitterLabel, jitterBox);
    pairRow (midiInLabel, midiInBox, midiOutLabel, midiOutBox);
    {
        auto r = b.removeFromTop (30);
        b.removeFromTop (4);
        sharingLabel.setBounds (r.removeFromLeft (labelWidth));
        if (removeSharingButton.isVisible())
            removeSharingButton.setBounds (r.removeFromRight (80).withSizeKeepingCentre (80, 26));
        r.removeFromRight (6);
        if (useSharingToggle.isVisible())
            useSharingToggle.setBounds (r.removeFromRight (170));
        if (sharingButton.isVisible())
            sharingButton.setBounds (r.removeFromRight (230).withSizeKeepingCentre (230, 26));
        r.removeFromRight (6);
        sharingStatusLabel.setBounds (r);
    }
    row (activeInputsLabel, *activeInputs, 66);
    row (activeOutputsLabel, *activeOutputs, 66);
}
