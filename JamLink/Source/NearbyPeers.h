#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

// Finds other JamLinks nearby with Bonjour, and advertises this one.
//
// Always works on the local network. With peer-to-peer switched on it also
// uses Apple's peer-to-peer Wi-Fi (AWDL, the radio AirDrop uses), so Macs
// can find and reach each other with no router and without joining the same
// network - just Wi-Fi switched on. Keeping that radio active costs some
// latency on ordinary Wi-Fi, so it's off by default.
//
// Sends a change message (on the message thread) whenever the list changes.
class NearbyPeers : public juce::ChangeBroadcaster
{
public:
    struct Peer
    {
        juce::String name;      // the other Mac's name
        juce::String address;   // numeric address to reach its JamLink
        bool peerToPeer = false;
    };

    // advertise = false only browses (used by the plugin, which can't accept
    // incoming jam requests).
    explicit NearbyPeers (bool advertise = true);
    ~NearbyPeers() override;

    void setPeerToPeer (bool enabled);
    bool isPeerToPeer() const noexcept { return peerToPeer; }

    std::vector<Peer> getPeers() const;

    // Non-empty if Bonjour couldn't start.
    juce::String getError() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
    bool peerToPeer = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NearbyPeers)
};
