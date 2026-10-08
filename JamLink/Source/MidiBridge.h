#pragma once

#include <JuceHeader.h>
#include "PeerLink.h"

// Makes JamLink a MIDI device. It publishes a virtual "JamLink" MIDI port
// pair that DAWs and other apps can see:
//
//   apps -> "JamLink" (destination)  : forwarded to every peer with MIDI on
//   peers -> "JamLink" (source)      : MIDI from peers, for apps to record/play
//
// A hardware MIDI input (e.g. a keyboard) and output can also be chosen, so
// JamLink can be used without a DAW in between.
class MidiBridge : private juce::MidiInputCallback
{
public:
    explicit MidiBridge (PeerLink& linkToUse);
    ~MidiBridge() override;

    bool hasVirtualPorts() const noexcept { return virtualIn != nullptr && virtualOut != nullptr; }

    // Empty identifier = none. Returns false (and changes nothing) if the
    // choice would feed JamLink's MIDI output back into its input: JamLink's
    // own virtual port, or the same device as the other direction (e.g. an
    // IAC bus picked as both input and output).
    bool setHardwareInput (const juce::String& identifier);
    bool setHardwareOutput (const juce::String& identifier);
    juce::String getHardwareInput() const;
    juce::String getHardwareOutput() const;

    // Devices offered in the menus: everything except JamLink's own ports.
    juce::Array<juce::MidiDeviceInfo> getSelectableInputs() const;
    juce::Array<juce::MidiDeviceInfo> getSelectableOutputs() const;

    // True if choosing this device would loop MIDI back into JamLink.
    bool wouldLoopAsInput (const juce::MidiDeviceInfo& device) const;
    bool wouldLoopAsOutput (const juce::MidiDeviceInfo& device) const;

private:
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void sendToLocalOutputs (const juce::MidiMessage& message);

    PeerLink& link;
    std::unique_ptr<juce::MidiInput> virtualIn, hardwareIn;
    std::unique_ptr<juce::MidiOutput> virtualOut, hardwareOut;
    bool isOwnPort (const juce::MidiDeviceInfo& device) const;
    static juce::MidiDeviceInfo findDevice (const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& identifier);

    juce::String hardwareInId, hardwareOutId;
    juce::String hardwareInName, hardwareOutName;
    juce::CriticalSection outputLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiBridge)
};
