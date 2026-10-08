#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

// TCP control link between JamLink machines, running alongside the UDP
// audio streams. Every JamLink listens on kControlPort and connects to each
// peer it sends audio to or receives audio from. Over the link it:
//
//  - exchanges computer names,
//  - pings once a second to measure round-trip time (shown to players and
//    used to line up incoming streams),
//  - carries MIDI. TCP rather than UDP so a note-off is never lost,
//  - negotiates streams: one JamLink asks another to open a receive port
//    (and optionally to send back), so nobody has to type IPs or ports.
//
// Sockets are dual-stack, so peers can be on a LAN (IPv4) or on
// peer-to-peer Wi-Fi (IPv6 link-local, e.g. "fe80::1%awdl0").
class PeerLink : private juce::Timer
{
public:
    static constexpr int kControlPort = 58000;

    // listenForPeers = false makes a client-only link (used by the plugin,
    // which leaves port kControlPort to the JamLink app): it can measure
    // delays and ask peers to start streams, but can't be asked itself.
    explicit PeerLink (bool listenForPeers = true);
    ~PeerLink() override;

    bool isListening() const noexcept { return listening; }

    // Peers we should keep a connection open to (numeric addresses).
    // Message thread.
    void setWantedPeers (const juce::StringArray& addresses);

    // Peers that MIDI is sent to. Any thread.
    void setMidiPeers (const juce::StringArray& addresses);

    // Smoothed round-trip time to a peer in ms, or -1 if there's no live
    // JamLink link to that address.
    double getRoundTripMs (const juce::String& address) const;
    juce::String getPeerName (const juce::String& address) const;
    int getNumConnectedPeers() const;

    // Sends to every MIDI peer. Any thread (typically a MIDI input thread).
    void sendMidi (const juce::MidiMessage& message);

    // Called on a network thread for every MIDI message a peer sends us.
    // Pass nullptr to stop; once this returns the old callback won't be called.
    void setMidiCallback (std::function<void (const juce::MidiMessage&)> callback);

    // Asks the JamLink at `address` to set up a stream (see MainComponent for
    // the request fields). `onAnswer` is called on the message thread with
    // the peer's answer, or with an object holding "error". Message thread.
    void requestStream (const juce::String& address, const juce::var& request,
                        std::function<void (const juce::var& answer)> onAnswer);

    // Handles a stream request from a peer, on the message thread. Call
    // `respond` exactly once - it may be later, e.g. after asking the user.
    using StreamRequestHandler = std::function<void (const juce::String& fromAddress, const juce::String& fromName,
                                                     const juce::var& request,
                                                     std::function<void (const juce::var& answer)> respond)>;
    void setStreamRequestHandler (StreamRequestHandler handler) { streamRequestHandler = std::move (handler); }

    uint64_t getMidiSent() const noexcept     { return midiSent.load(); }
    uint64_t getMidiReceived() const noexcept { return midiReceived.load(); }

private:
    class Connection;
    class Server;

    void timerCallback() override;
    void addConnection (std::unique_ptr<Connection> connection);
    Connection* findLiveConnection (const juce::String& address) const;
    Connection* connectNow (const juce::String& address);
    void handleMidiFromPeer (const juce::MidiMessage& message);
    void handleStreamRequest (int connectionId, const juce::String& fromAddress, const juce::String& fromName,
                              const juce::var& request);
    void finishRequest (int requestId, const juce::var& answer);

    std::unique_ptr<Server> server;
    bool listening = false;

    mutable juce::CriticalSection lock;
    std::vector<std::unique_ptr<Connection>> connections;
    juce::StringArray wantedPeers, midiPeers, connecting;
    std::map<juce::String, juce::uint32> lastAttempt;
    std::atomic<int> nextConnectionId { 1 };

    struct PendingRequest
    {
        std::function<void (const juce::var&)> onAnswer;
        juce::uint32 deadline = 0;
    };
    std::map<int, PendingRequest> pendingRequests;  // message thread only
    int nextRequestId = 1;
    StreamRequestHandler streamRequestHandler;

    // Lets work posted to the message thread check this PeerLink still exists.
    std::shared_ptr<PeerLink*> aliveToken = std::make_shared<PeerLink*> (this);

    juce::ThreadPool connectPool { juce::ThreadPoolOptions{}.withThreadName ("JamLink connect").withNumberOfThreads (2) };

    juce::CriticalSection callbackLock;
    std::function<void (const juce::MidiMessage&)> onMidiReceived;

    std::atomic<uint64_t> midiSent { 0 }, midiReceived { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeerLink)
};
