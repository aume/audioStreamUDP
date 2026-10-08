#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "PeerLink.h"
#include "NearbyPeers.h"

// Self-organising streams, shared by the app and the plugin: "+ Nearby..."
// asks another JamLink to open a receive port (and, for a jam, to send back
// to a port we opened), so nobody types IPs or ports. When the PeerLink is
// listening (the app), it also answers other JamLinks' requests, asking the
// player first.
class NearbyJam
{
public:
    NearbyJam (AudioEngine& engineToUse, PeerLink& linkToUse, NearbyPeers& nearbyToUse,
               std::function<void()> onRoutesChanged);
    ~NearbyJam();

    void showNearbyMenu (juce::Component& button);
    void startJam (const NearbyPeers::Peer& peer, bool bothWays);

    // e.g. "Waiting for X to accept..."; empty when idle.
    const juce::String& getStatus() const noexcept { return jamStatus; }

private:
    void handleStreamRequest (const juce::String& fromAddress, const juce::String& fromName, const juce::var& request,
                              std::function<void (const juce::var&)> respond);

    AudioEngine& engine;
    PeerLink& link;
    NearbyPeers& nearby;
    std::function<void()> routesChanged;
    juce::String jamStatus;

    // Lets menu and dialog callbacks tell whether this object still exists.
    std::shared_ptr<bool> aliveToken = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE (NearbyJam)
};
