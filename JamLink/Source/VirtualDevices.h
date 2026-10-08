#pragma once

#include <JuceHeader.h>

// The optional "JamLink Send" / "JamLink Return" audio devices, which let
// other apps (DAWs, Zoom, OBS, browsers...) share audio with JamLink:
//
//   other app --plays into--> JamLink Send   --> JamLink --> peers
//   other app <--records---  JamLink Return  <-- JamLink <-- peers
//
// The drivers (a GPL-3.0 fork of BlackHole) ship inside JamLink.app and are
// installed only when the player asks, behind the standard macOS admin
// prompt. To use them alongside the player's own interface, JamLink builds a
// private aggregate device: the player's hardware plus the two hidden
// "for JamLink" sides of the drivers, with CoreAudio's drift correction on
// the virtual devices. Only JamLink can see that aggregate.
class VirtualDevices
{
public:
    enum class State
    {
        notBundled,       // this build of JamLink doesn't include the drivers
        notInstalled,
        installed,        // the bundled version is installed
        updateAvailable   // an older version is installed
    };

    VirtualDevices() = default;
    ~VirtualDevices();

    State getState() const;

    // True once CoreAudio has loaded the drivers (after install, or once
    // coreaudiod has restarted).
    bool areDevicesLoaded() const;

    // Install / update / remove the drivers. Asks for an admin password and
    // restarts the system audio service, which briefly interrupts all audio.
    // `done` is called on the message thread; `error` is empty on success and
    // also empty if the player cancelled the password prompt (with ok = false).
    void install (std::function<void (bool ok, const juce::String& error)> done);
    void remove (std::function<void (bool ok, const juce::String& error)> done);

    // Builds (or rebuilds) the private aggregate from the given hardware
    // devices (JUCE device names; either may be empty). Returns the name to
    // open it by, or an empty string with `error` set.
    juce::String createAggregate (const juce::String& hardwareInput, const juce::String& hardwareOutput,
                                  juce::String& error);
    void destroyAggregate();
    bool hasAggregate() const noexcept { return aggregateId != 0; }

    // Per-channel labels for the aggregate's channels ("In 1", ...,
    // "JamLink Send 1", ...), for the channels set in the active masks.
    juce::StringArray getInputLabels (const juce::BigInteger& activeChannels) const;
    juce::StringArray getOutputLabels (const juce::BigInteger& activeChannels) const;

    static constexpr const char* aggregateName = "JamLink (with other apps)";

    // True for "JamLink Send", "JamLink Return" and the combined device -
    // never usable as JamLink's own hardware.
    static bool isJamLinkDevice (const juce::String& name);

    // The system's default input/output if it isn't a JamLink device, else
    // the first other device; empty if there's none.
    static juce::String fallbackHardware (bool input);

    // macOS can switch the default input/output to newly installed devices.
    // Call before installing, then after the drivers load to switch back.
    void rememberSystemDefaults();
    void restoreSystemDefaultsIfTakenOver();

private:
    void runAdminScript (const juce::String& shellCommand, const juce::String& prompt,
                         std::function<void (bool, const juce::String&)> done);

    juce::uint32 aggregateId = 0;
    juce::String savedDefaultInputUid, savedDefaultOutputUid;

    // Channel layout of the aggregate, in CoreAudio's order.
    struct Segment { int numChannels; bool isJamLink; };
    std::vector<Segment> inputSegments, outputSegments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualDevices)
};
