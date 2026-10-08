#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <memory>
#include <atomic>
#include "AudioRingBuffer.h"
#include "StreamResampler.h"
#include "UdpReceiver.h"
#include "RoutingModels.h"

// The real-time heart of JamLink. Holds the audio device callback, the list
// of outgoing (send) and incoming (receive) routes, and does the actual
// packet send/receive + channel routing/mixing on the audio thread.
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    juce::Uuid addOutputRoute (OutputRoute route);
    void removeOutputRoute (const juce::Uuid& id);
    void updateOutputRoute (const OutputRoute& route);
    std::vector<OutputRoute> getOutputRoutes() const;

    juce::Uuid addInputRoute (InputRoute route);
    void removeInputRoute (const juce::Uuid& id);
    void updateInputRoute (const InputRoute& route);
    std::vector<InputRoute> getInputRoutes() const;

    // First UDP port from the default upwards that no receive route uses and
    // nothing else on this machine is listening on.
    int findFreeListenPort() const;

    float getOutputRouteLevel (const juce::Uuid& id) const;
    float getInputRouteLevel (const juce::Uuid& id) const;

    enum class PortStatus { listening, portInUse, invalidPort };
    PortStatus getInputRoutePortStatus (const juce::Uuid& id) const;

    // Tries again to bind any receive route whose port was busy, e.g. after
    // another app released it or the network came back. Message thread only.
    void retryUnboundReceivers();

    // Sample rate audio is sent at over the network. 0 = same as the device.
    // Receivers resample whatever rate arrives to their own device rate, so
    // the two ends don't have to match.
    void setStreamSampleRate (double rate)   { streamSampleRate.store (rate); }
    double getStreamSampleRate() const       { return streamSampleRate.load(); }

    // How much audio each receive route buffers before playing, to absorb
    // network jitter. Larger = fewer dropouts, more latency.
    void setJitterBufferMs (double ms);
    double getJitterBufferMs() const         { return jitterBufferMs.load(); }

    // Live state of one receive route's stream, for showing delays.
    struct InputStreamInfo
    {
        juce::String senderIp;      // where the audio is coming from (empty until a packet arrives)
        bool active = false;        // audio currently playing
        double bufferMs = 0.0;      // latency added by the jitter buffer (includes alignment delay)
        double alignDelayMs = 0.0;  // part of bufferMs added to line streams up
    };
    InputStreamInfo getInputStreamInfo (const juce::Uuid& id) const;

    // Addresses JamLink should hold a control link to: every enabled send
    // destination plus every address audio is arriving from.
    juce::StringArray getPeerAddresses() const;
    juce::StringArray getMidiPeerAddresses() const;

    // "Align streams": delays each incoming stream so all of them reach the
    // speakers with the same total delay (network + buffer) as the slowest.
    void setAlignStreams (bool shouldAlign)  { alignStreams.store (shouldAlign); }
    bool getAlignStreams() const             { return alignStreams.load(); }

    // Recomputes the alignment delays. Message thread, about once a second.
    // roundTripMs returns the measured round trip to an address, or < 0.
    void updateAlignment (const std::function<double (const juce::String&)>& roundTripMs);

    struct Stats
    {
        uint64_t packetsSent = 0;
        uint64_t packetsReceived = 0;
        uint64_t packetsLost = 0;
    };
    Stats getStats() const;

    juce::var routesToVar() const;
    void setRoutesFromVar (const juce::var& v);

    // For hosts that drive the engine directly instead of through an audio
    // device (the JamLink plugin): call prepare() before processing, then
    // process() once per block. Outputs are overwritten, so inputs must not
    // share memory with outputs.
    void prepare (double sampleRate);
    void process (const float* const* inputChannelData, int numInputChannels,
                  float* const* outputChannelData, int numOutputChannels, int numSamples);

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                            float* const* outputChannelData, int numOutputChannels,
                                            int numSamples,
                                            const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    static constexpr int kMaxChannels = jamlink::kMaxChannels;
    static constexpr int kScratchFrames = 16384;

    struct OutputRouteRuntime
    {
        OutputRoute route;
        UdpSocket socket;
        jamlink::net::Address destination;   // resolved from route.destHost/destPort on the message thread
        std::array<StreamResampler, kMaxChannels> resamplers;
        double preparedInRate = 0.0, preparedOutRate = 0.0;
        std::atomic<float> txLevel { 0.0f };
        uint32_t sequence = 0;
    };

    struct InputRouteRuntime
    {
        InputRoute route;
        std::unique_ptr<AudioRingBuffer> ring;
        std::unique_ptr<UdpReceiver> receiver;
        std::atomic<float> rxLevel { 0.0f };

        // Sender's address: written by the receive thread, read by the UI.
        mutable juce::SpinLock senderLock;
        juce::String senderIp;

        // Receive-thread-only sequence tracking.
        uint32_t lastSequence = 0;
        int lastNumFrames = 0;
        bool haveSequence = false;
    };

    void onPacketReceived (InputRouteRuntime& runtime, const void* data, int numBytes, const juce::String& senderIp);
    std::unique_ptr<UdpReceiver> makeReceiver (InputRouteRuntime& runtime);
    void sendRoute (OutputRouteRuntime& runtime, const float* const* inputChannelData, int numInputChannels, int numSamples);
    void receiveRoute (InputRouteRuntime& runtime, float* const* outputChannelData, int numOutputChannels, int numSamples);

    juce::AudioDeviceManager deviceManager;
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<double> streamSampleRate { 0.0 };
    std::atomic<double> jitterBufferMs { 20.0 };
    std::atomic<bool> alignStreams { false };

    mutable juce::CriticalSection routesLock;
    std::vector<std::unique_ptr<OutputRouteRuntime>> outputRoutes;
    std::vector<std::unique_ptr<InputRouteRuntime>> inputRoutes;

    // Audio-thread scratch space, allocated up front.
    juce::AudioBuffer<float> txScratch { kMaxChannels, kScratchFrames };
    juce::AudioBuffer<float> rxScratch { kMaxChannels, kScratchFrames };
    std::vector<int16_t> interleaved = std::vector<int16_t> ((size_t) jamlink::kMaxPayloadBytes / 2);
    std::vector<char> packetBuffer = std::vector<char> ((size_t) jamlink::kMaxPacketBytes);

    std::atomic<uint64_t> packetsSent { 0 };
    std::atomic<uint64_t> packetsReceived { 0 };
    std::atomic<uint64_t> packetsLost { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
