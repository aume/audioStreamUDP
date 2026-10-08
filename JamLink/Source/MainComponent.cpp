#include "MainComponent.h"

MainComponent::MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    appTitle.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    appSubtitle.setFont (juce::Font (juce::FontOptions (14.0f)));
    appSubtitle.setColour (juce::Label::textColourId, JamColours::textMuted);
    statsLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    statsLabel.setColour (juce::Label::textColourId, JamColours::textMuted);
    statsLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (appTitle);
    addAndMakeVisible (appSubtitle);
    addAndMakeVisible (statsLabel);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);

    saveButton.onClick = [this] { saveConfig(); };
    loadButton.onClick = [this] { loadConfig(); };

    deviceCard = std::make_unique<DeviceCard> (engine, midiBridge, nearby);
    deviceCard->onToggleExpanded = [this] { resized(); };
    deviceCard->onNetworkChanged = [this] { engine.retryUnboundReceivers(); };
    deviceCard->onInstallClicked = [this] { installVirtualDevices(); };
    deviceCard->onRemoveClicked = [this] { removeVirtualDevices(); };
    deviceCard->onUseSharingChanged = [this] (bool use)
    {
        shareWithApps = use;
        applySharing (true);
    };
    deviceCard->onUseSharingWithHardware = [this] (const juce::String& in, const juce::String& out)
    {
        shareWithApps = true;
        hardwareInput = in;
        hardwareOutput = out;
        applySharing (false);
    };
    deviceCard->onHardwareChanged = [this] (const juce::String& in, const juce::String& out)
    {
        hardwareInput = in;
        hardwareOutput = out;
        applySharing (false);
    };
    addAndMakeVisible (*deviceCard);

    outputsPanel = std::make_unique<OutputsPanel> (engine, peerLink);
    inputsPanel = std::make_unique<InputsPanel> (engine, peerLink);
    addAndMakeVisible (*outputsPanel);
    addAndMakeVisible (*inputsPanel);

    nearbyJam = std::make_unique<NearbyJam> (engine, peerLink, nearby, [this] { refreshRouteViews(); });
    outputsPanel->onNearbyClicked = [this] (juce::Component& button) { nearbyJam->showNearbyMenu (button); };

    engine.getDeviceManager().addChangeListener (this);

    // Restore the previous session (audio device + routing) if we have one.
    std::unique_ptr<juce::XmlElement> deviceState;
    juce::var routingState;

    auto sessionFile = getSessionFile();
    if (sessionFile.existsAsFile())
    {
        auto parsed = juce::JSON::parse (sessionFile.loadFileAsString());
        if (auto* obj = parsed.getDynamicObject())
        {
            auto deviceXmlString = obj->getProperty ("device").toString();
            if (deviceXmlString.isNotEmpty())
                deviceState = juce::parseXML (deviceXmlString);
            routingState = obj->getProperty ("routing");

            if (auto* settings = obj->getProperty ("settings").getDynamicObject())
            {
                engine.setStreamSampleRate ((double) settings->getProperty ("streamSampleRate"));
                if (settings->hasProperty ("jitterBufferMs"))
                    engine.setJitterBufferMs ((double) settings->getProperty ("jitterBufferMs"));
                engine.setAlignStreams ((bool) settings->getProperty ("alignStreams"));
                midiBridge.setHardwareInput (settings->getProperty ("midiInput").toString());
                midiBridge.setHardwareOutput (settings->getProperty ("midiOutput").toString());
                nearby.setPeerToPeer ((bool) settings->getProperty ("peerToPeer"));
                shareWithApps = (bool) settings->getProperty ("shareWithApps");
                hardwareInput = settings->getProperty ("hardwareInput").toString();
                hardwareOutput = settings->getProperty ("hardwareOutput").toString();
            }
        }
    }

    engine.getDeviceManager().initialise (2, 2, deviceState.get(), true);

    if (routingState.isObject())
        engine.setRoutesFromVar (routingState);

    refreshSharingStatus();
    if (shareWithApps)
        applySharing (hardwareInput.isEmpty() && hardwareOutput.isEmpty());
    else if (auto current = engine.getDeviceManager().getAudioDeviceSetup();
             VirtualDevices::isJamLinkDevice (current.inputDeviceName) || VirtualDevices::isJamLinkDevice (current.outputDeviceName))
        openHardware();   // e.g. the Mac's default output is JamLink Send

    updateChannelCounts();
    deviceCard->refresh();
    outputsPanel->refreshRows();
    inputsPanel->refreshRows();

    setSize (1180, 860);
    startTimerHz (10);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.getDeviceManager().removeChangeListener (this);

    auto sessionFile = getSessionFile();
    sessionFile.getParentDirectory().createDirectory();

    auto obj = std::make_unique<juce::DynamicObject>();
    if (auto xml = engine.getDeviceManager().createStateXml())
    {
        // The combined device only exists while JamLink runs; remember the
        // player's hardware instead.
        if (virtualDevices.hasAggregate())
        {
            xml->setAttribute ("audioInputDeviceName", hardwareInput);
            xml->setAttribute ("audioOutputDeviceName", hardwareOutput);
        }
        obj->setProperty ("device", xml->toString());
    }
    obj->setProperty ("routing", engine.routesToVar());

    auto settings = std::make_unique<juce::DynamicObject>();
    settings->setProperty ("streamSampleRate", engine.getStreamSampleRate());
    settings->setProperty ("jitterBufferMs", engine.getJitterBufferMs());
    settings->setProperty ("alignStreams", engine.getAlignStreams());
    settings->setProperty ("midiInput", midiBridge.getHardwareInput());
    settings->setProperty ("midiOutput", midiBridge.getHardwareOutput());
    settings->setProperty ("peerToPeer", nearby.isPeerToPeer());
    settings->setProperty ("shareWithApps", shareWithApps);
    if (virtualDevices.hasAggregate())
    {
        settings->setProperty ("hardwareInput", hardwareInput);
        settings->setProperty ("hardwareOutput", hardwareOutput);
    }
    obj->setProperty ("settings", juce::var (settings.release()));
    sessionFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

juce::File MainComponent::getSessionFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("JamLink")
             .getChildFile ("session.json");
}

void MainComponent::saveConfig()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Save routing preset", juce::File(), "*.json");
    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;
        if (! file.hasFileExtension ("json"))
            file = file.withFileExtension ("json");
        file.replaceWithText (juce::JSON::toString (engine.routesToVar()));
    });
}

void MainComponent::loadConfig()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load routing preset", juce::File(), "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        auto parsed = juce::JSON::parse (file.loadFileAsString());
        engine.setRoutesFromVar (parsed);
        outputsPanel->refreshRows();
        inputsPanel->refreshRows();
    });
}

void MainComponent::refreshRouteViews()
{
    outputsPanel->refreshRows();
    inputsPanel->refreshRows();
}

void MainComponent::updateChannelCounts()
{
    juce::StringArray inLabels = numberedLabels ("In", 2), outLabels = numberedLabels ("Out", 2);

    if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
    {
        auto activeIn = device->getActiveInputChannels();
        auto activeOut = device->getActiveOutputChannels();

        if (virtualDevices.hasAggregate() && device->getName() == VirtualDevices::aggregateName)
        {
            inLabels = virtualDevices.getInputLabels (activeIn);
            outLabels = virtualDevices.getOutputLabels (activeOut);
        }
        else
        {
            inLabels = numberedLabels ("In", juce::jmax (1, activeIn.countNumberOfSetBits()));
            outLabels = numberedLabels ("Out", juce::jmax (1, activeOut.countNumberOfSetBits()));
        }
    }

    outputsPanel->setInputChannelLabels (inLabels);
    inputsPanel->setOutputChannelLabels (outLabels);
}

//==============================================================================
void MainComponent::refreshSharingStatus()
{
    using Status = DeviceCard::SharingStatus;
    auto status = Status::unavailable;

    if (sharingBusy)
        status = Status::busy;
    else switch (virtualDevices.getState())
    {
        case VirtualDevices::State::notBundled:      status = Status::unavailable; break;
        case VirtualDevices::State::notInstalled:    status = Status::notInstalled; break;
        case VirtualDevices::State::updateAvailable: status = Status::updateAvailable; break;
        case VirtualDevices::State::installed:
            status = virtualDevices.areDevicesLoaded() ? Status::installed : Status::notLoaded;
            break;
    }

    deviceCard->setSharingStatus (status, shareWithApps);
    resized();
}

void MainComponent::installVirtualDevices()
{
    bool updating = virtualDevices.getState() == VirtualDevices::State::updateAvailable;

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle (updating ? "Update JamLink audio devices" : "Install JamLink audio devices")
                       .withMessage ("This adds two audio devices other apps can use:\n\n"
                                     "  JamLink Send - play into it from any app to send that audio to your peers\n"
                                     "  JamLink Return - record from it in any app to get your peers' audio\n\n"
                                     "macOS will ask for an admin password, and all audio on this Mac will stop "
                                     "for a few seconds while it reloads. You can remove them again here at any time.\n\n"
                                     "The devices are based on BlackHole by Existential Audio (GPL-3.0); "
                                     "source: github.com/aume/JamLinkAudio")
                       .withButton (updating ? "Update" : "Install")
                       .withButton ("Cancel");

    juce::AlertWindow::showAsync (options, [this, safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
    {
        if (safe == nullptr || result != 1)
            return;

        sharingBusy = true;
        refreshSharingStatus();

        // The audio service restarts underneath us; let go of the device first.
        if (virtualDevices.hasAggregate())
            openHardware();
        virtualDevices.rememberSystemDefaults();

        virtualDevices.install ([this, safe] (bool ok, const juce::String& error)
        {
            if (safe == nullptr)
                return;

            if (! ok)
            {
                sharingBusy = false;
                refreshSharingStatus();
                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Couldn't install the JamLink audio devices", error);
                return;
            }

            shareWithApps = true;
            reopenAudioAfterRestart (true);
        });
    });
}

void MainComponent::removeVirtualDevices()
{
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Remove JamLink audio devices")
                       .withMessage ("JamLink Send and JamLink Return will disappear from all apps. macOS will ask for "
                                     "an admin password, and all audio on this Mac will stop for a few seconds.")
                       .withButton ("Remove")
                       .withButton ("Cancel");

    juce::AlertWindow::showAsync (options, [this, safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
    {
        if (safe == nullptr || result != 1)
            return;

        sharingBusy = true;
        shareWithApps = false;
        applySharing (false);   // back to the player's own hardware first
        refreshSharingStatus();

        virtualDevices.remove ([this, safe] (bool ok, const juce::String& error)
        {
            if (safe == nullptr)
                return;

            if (! ok)
            {
                sharingBusy = false;
                refreshSharingStatus();
                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Couldn't remove the JamLink audio devices", error);
                return;
            }

            reopenAudioAfterRestart (false);
        });
    });
}

void MainComponent::reopenAudioAfterRestart (bool waitForDrivers, int attemptsLeft)
{
    juce::Timer::callAfterDelay (500, [this, safe = juce::Component::SafePointer<MainComponent> (this), waitForDrivers, attemptsLeft]
    {
        if (safe == nullptr)
            return;

        bool ready = waitForDrivers ? virtualDevices.areDevicesLoaded() : attemptsLeft < 36; // ~2 s for a plain restart
        if (! ready && attemptsLeft > 0)
        {
            reopenAudioAfterRestart (waitForDrivers, attemptsLeft - 1);
            return;
        }

        sharingBusy = false;
        if (waitForDrivers && ready)
            virtualDevices.restoreSystemDefaultsIfTakenOver();
        if (auto* type = engine.getDeviceManager().getCurrentDeviceTypeObject())
            type->scanForDevices();

        refreshSharingStatus();
        if (shareWithApps && virtualDevices.areDevicesLoaded())
            applySharing (false);
        else
            openHardware();
        updateChannelCounts();

        if (waitForDrivers && ! ready)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "JamLink audio devices",
                                                    "The devices were installed but macOS hasn't loaded them yet. "
                                                    "Restarting the Mac will load them.");
    });
}

void MainComponent::applySharing (bool captureHardware)
{
    auto& dm = engine.getDeviceManager();
    bool wanted = shareWithApps && virtualDevices.areDevicesLoaded()
               && virtualDevices.getState() != VirtualDevices::State::notInstalled;

    if (wanted)
    {
        auto setup = dm.getAudioDeviceSetup();
        if (captureHardware && setup.outputDeviceName != VirtualDevices::aggregateName)
        {
            hardwareInput = setup.inputDeviceName;
            hardwareOutput = setup.outputDeviceName;
        }

        // JamLink's own devices can't be its hardware (that would loop).
        if (VirtualDevices::isJamLinkDevice (hardwareInput))
            hardwareInput = VirtualDevices::fallbackHardware (true);
        if (VirtualDevices::isJamLinkDevice (hardwareOutput))
            hardwareOutput = VirtualDevices::fallbackHardware (false);

        if (! openAggregate (true))
            openHardware();
    }
    else if (virtualDevices.hasAggregate() || dm.getAudioDeviceSetup().outputDeviceName == VirtualDevices::aggregateName)
    {
        openHardware();
    }

    deviceCard->setHardwareOverride (virtualDevices.hasAggregate(), hardwareInput, hardwareOutput);
    refreshSharingStatus();
    updateChannelCounts();
}

bool MainComponent::openAggregate (bool retryIfMissing)
{
    auto& dm = engine.getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    dm.closeAudioDevice();

    juce::String error;
    auto name = virtualDevices.createAggregate (hardwareInput, hardwareOutput, error);
    if (name.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't share audio with other apps", error);
        return false;
    }

    if (auto* type = dm.getCurrentDeviceTypeObject())
        type->scanForDevices();

    setup.inputDeviceName = name;
    setup.outputDeviceName = name;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.setRange (0, 64, true);
    setup.outputChannels.setRange (0, 64, true);

    error = dm.setAudioDeviceSetup (setup, true);
    if (error.isEmpty())
        return true;

    // CoreAudio can take a moment to publish a new aggregate; try once more.
    if (retryIfMissing)
    {
        juce::Timer::callAfterDelay (600, [this, safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr && shareWithApps && ! openAggregate (false))
            {
                openHardware();
                deviceCard->setHardwareOverride (false, {}, {});
                updateChannelCounts();
            }
        });
        return true;
    }

    virtualDevices.destroyAggregate();
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't share audio with other apps", error);
    return false;
}

void MainComponent::openHardware()
{
    auto& dm = engine.getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    bool wasAggregate = setup.outputDeviceName == VirtualDevices::aggregateName || virtualDevices.hasAggregate();

    dm.closeAudioDevice();
    virtualDevices.destroyAggregate();
    if (auto* type = dm.getCurrentDeviceTypeObject())
        type->scanForDevices();

    if (wasAggregate)
    {
        setup.inputDeviceName = hardwareInput;
        setup.outputDeviceName = hardwareOutput;
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;
    }

    if (VirtualDevices::isJamLinkDevice (setup.inputDeviceName))
        setup.inputDeviceName = VirtualDevices::fallbackHardware (true);
    if (VirtualDevices::isJamLinkDevice (setup.outputDeviceName))
        setup.outputDeviceName = VirtualDevices::fallbackHardware (false);

    auto error = dm.setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty())
        dm.initialiseWithDefaultDevices (2, 2);

    deviceCard->setHardwareOverride (false, {}, {});
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateChannelCounts();
}

void MainComponent::timerCallback()
{
    ++timerTicks;

    // Every 2 s, pick up receive ports that were busy and have since freed up.
    if (timerTicks % 20 == 0)
        engine.retryUnboundReceivers();

    // Every second: keep control links open to everyone we're jamming with,
    // and re-line-up the incoming streams from the latest delay measurements.
    if (timerTicks % 10 == 0)
    {
        peerLink.setWantedPeers (engine.getPeerAddresses());
        peerLink.setMidiPeers (engine.getMidiPeerAddresses());
        engine.updateAlignment ([this] (const juce::String& ip) { return peerLink.getRoundTripMs (ip); });
    }

    auto stats = engine.getStats();
    statsLabel.setText ("Sent " + juce::String (stats.packetsSent)
                             + "   Received " + juce::String (stats.packetsReceived)
                             + "   Lost " + juce::String (stats.packetsLost)
                             + "\nPeers linked " + juce::String (peerLink.getNumConnectedPeers())
                             + "   MIDI out " + juce::String (peerLink.getMidiSent())
                             + " / in " + juce::String (peerLink.getMidiReceived())
                             + (peerLink.isListening() ? juce::String()
                                                       : "   (TCP " + juce::String (PeerLink::kControlPort) + " busy)")
                             + (nearbyJam->getStatus().isNotEmpty() ? "\n" + nearbyJam->getStatus() : juce::String()),
                         juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (JamColours::background);
}

void MainComponent::resized()
{
    auto b = getLocalBounds().reduced (20);

    auto header = b.removeFromTop (60);
    auto titleArea = header.removeFromLeft (400);
    appTitle.setBounds (titleArea.removeFromTop (32));
    appSubtitle.setBounds (titleArea);

    auto buttonArea = header.removeFromRight (250);
    loadButton.setBounds (buttonArea.removeFromRight (115).withSizeKeepingCentre (115, 30));
    buttonArea.removeFromRight (8);
    saveButton.setBounds (buttonArea.removeFromRight (115).withSizeKeepingCentre (115, 30));

    statsLabel.setBounds (header);

    b.removeFromTop (12);

    deviceCard->setBounds (b.removeFromTop (deviceCard->getPreferredHeight()));
    b.removeFromTop (12);

    auto columns = b;
    auto leftColumn = columns.removeFromLeft (columns.getWidth() / 2 - 6);
    columns.removeFromLeft (12);
    auto rightColumn = columns;

    outputsPanel->setBounds (leftColumn);
    inputsPanel->setBounds (rightColumn);
}
