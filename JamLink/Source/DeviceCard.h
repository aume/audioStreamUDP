#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "MidiBridge.h"
#include "NearbyPeers.h"

// Collapsible card with the audio device settings (input listed before
// output throughout), the network stream settings, and this machine's LAN
// IP addresses (so a peer knows what to type into their "Send To" row).
// The IP list is re-polled so it follows Wi-Fi / network changes.
class DeviceCard : public juce::Component,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    DeviceCard (AudioEngine& engineToUse, MidiBridge& midiToUse, NearbyPeers& nearbyToUse);
    ~DeviceCard() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    int getPreferredHeight() const;

    // Re-reads the device + stream settings into the controls.
    void refresh();

    std::function<void()> onToggleExpanded;
    std::function<void()> onNetworkChanged;

    // "Share audio with other apps" (the JamLink Send/Return devices).
    enum class SharingStatus { unavailable, notInstalled, installed, updateAvailable, notLoaded, busy };
    void setSharingStatus (SharingStatus status, bool useWithJamLink);
    std::function<void()> onInstallClicked, onRemoveClicked;
    std::function<void (bool use)> onUseSharingChanged;
    // Choosing "None - JamLink Send/Return only" while sharing is off: switch
    // sharing on with this hardware (one side empty).
    std::function<void (const juce::String& input, const juce::String& output)> onUseSharingWithHardware;

    // While sharing is on, JamLink runs on a private combined device, so the
    // Input/Output menus show (and change, via this callback) the player's own
    // hardware instead.
    void setHardwareOverride (bool active, const juce::String& input, const juce::String& output);
    std::function<void (const juce::String& input, const juce::String& output)> onHardwareChanged;

private:
    class ChannelList;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void toggleExpanded();
    void refreshIpAddresses();
    void applySetup (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>& change);

    void refreshMidiDevices();
    void updatePeerToPeerButton();
    bool sharingDevicesReady() const;

    AudioEngine& engine;
    MidiBridge& midi;
    NearbyPeers& nearby;
    bool expanded = true;
    juce::String lastIpText;

    juce::Label titleLabel { {}, "Audio Device" };
    juce::Label ipLabel;
    juce::TextButton peerToPeerButton;
    juce::TextButton expandButton { juce::String::fromUTF8 ("\xE2\x96\xB4") };

    juce::Label typeLabel { {}, "Driver" }, inputLabel { {}, "Input" }, outputLabel { {}, "Output" },
                rateLabel { {}, "Sample rate" }, bufferLabel { {}, "Buffer size" },
                streamRateLabel { {}, "Network rate" }, jitterLabel { {}, "Jitter buffer" },
                activeInputsLabel { {}, "Active inputs" }, activeOutputsLabel { {}, "Active outputs" },
                midiInLabel { {}, "MIDI in" }, midiOutLabel { {}, "MIDI out" },
                sharingLabel { {}, "Other apps" };

    juce::Label sharingStatusLabel;
    juce::TextButton sharingButton, removeSharingButton { "Remove" };
    juce::ToggleButton useSharingToggle { "Use with JamLink" };
    SharingStatus sharingStatus = SharingStatus::unavailable;
    bool hardwareOverride = false;
    juce::String overrideInput, overrideOutput;

    juce::ComboBox typeBox, inputBox, outputBox, rateBox, bufferBox, streamRateBox, jitterBox, midiInBox, midiOutBox;
    juce::Array<juce::MidiDeviceInfo> midiInputs, midiOutputs;
    juce::MidiDeviceListConnection midiListConnection;
    std::unique_ptr<ChannelList> activeInputs, activeOutputs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceCard)
};
