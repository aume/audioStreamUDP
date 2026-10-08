#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "PeerLink.h"
#include "MidiBridge.h"
#include "NearbyPeers.h"
#include "NearbyJam.h"
#include "VirtualDevices.h"
#include "DeviceCard.h"
#include "OutputsPanel.h"
#include "InputsPanel.h"
#include "LookAndFeelModern.h"

class MainComponent : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void updateChannelCounts();
    void saveConfig();
    void loadConfig();
    juce::File getSessionFile() const;
    void refreshRouteViews();

    // "Share audio with other apps" (JamLink Send / JamLink Return).
    void refreshSharingStatus();
    void installVirtualDevices();
    void removeVirtualDevices();
    // Switches JamLink between the player's hardware and the private combined
    // device, following shareWithApps. captureHardware = take the hardware
    // from the device JamLink is using right now.
    void applySharing (bool captureHardware);
    bool openAggregate (bool retryIfMissing);
    void openHardware();
    // After the drivers are installed/removed macOS restarts its audio
    // service; waits for it (and, if wanted, the drivers) and reopens audio.
    void reopenAudioAfterRestart (bool waitForDrivers, int attemptsLeft = 40);


    LookAndFeelModern lookAndFeel;
    VirtualDevices virtualDevices;   // declared before engine: outlives the open device
    bool shareWithApps = false;
    bool sharingBusy = false;
    juce::String hardwareInput, hardwareOutput;   // the player's devices while sharing is on
    AudioEngine engine;
    PeerLink peerLink;
    MidiBridge midiBridge { peerLink };
    NearbyPeers nearby;
    juce::TooltipWindow tooltipWindow { this, 600 };
    int timerTicks = 0;

    juce::Label appTitle { {}, "JamLink" };
    juce::Label appSubtitle { {}, "Low-latency network audio jamming" };
    juce::Label statsLabel;
    juce::TextButton saveButton { "Save Preset" };
    juce::TextButton loadButton { "Load Preset" };

    std::unique_ptr<DeviceCard> deviceCard;
    std::unique_ptr<OutputsPanel> outputsPanel;
    std::unique_ptr<InputsPanel> inputsPanel;
    std::unique_ptr<NearbyJam> nearbyJam;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
