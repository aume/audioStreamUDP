#include "AudioEngine.h"
#include "PacketFormat.h"
#include <algorithm>
#include <cstring>

AudioEngine::AudioEngine()
{
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
}

std::unique_ptr<UdpReceiver> AudioEngine::makeReceiver (InputRouteRuntime& runtime)
{
    return std::make_unique<UdpReceiver> (runtime.route.listenPort,
                                          [this, &runtime] (const void* data, int numBytes, const juce::String& senderIp)
    {
        onPacketReceived (runtime, data, numBytes, senderIp);
    });
}

juce::Uuid AudioEngine::addOutputRoute (OutputRoute route)
{
    auto runtime = std::make_unique<OutputRouteRuntime>();
    runtime->route = route;
    runtime->destination = jamlink::net::parse (route.destHost, route.destPort);
    auto id = route.id;

    const juce::ScopedLock sl (routesLock);
    outputRoutes.push_back (std::move (runtime));
    return id;
}

void AudioEngine::removeOutputRoute (const juce::Uuid& id)
{
    std::unique_ptr<OutputRouteRuntime> extracted;
    {
        const juce::ScopedLock sl (routesLock);
        auto it = std::find_if (outputRoutes.begin(), outputRoutes.end(),
                                 [&] (const std::unique_ptr<OutputRouteRuntime>& p) { return p->route.id == id; });
        if (it != outputRoutes.end())
        {
            extracted = std::move (*it);
            outputRoutes.erase (it);
        }
    }
}

void AudioEngine::updateOutputRoute (const OutputRoute& route)
{
    // Parsed here, never on the audio thread.
    auto destination = jamlink::net::parse (route.destHost, route.destPort);

    const juce::ScopedLock sl (routesLock);
    auto it = std::find_if (outputRoutes.begin(), outputRoutes.end(),
                             [&] (const std::unique_ptr<OutputRouteRuntime>& p) { return p->route.id == route.id; });
    if (it != outputRoutes.end())
    {
        if ((*it)->route.numChannels != route.numChannels)
            (*it)->preparedInRate = 0.0; // re-prepare the resamplers on the next callback
        (*it)->route = route;
        (*it)->destination = destination;
    }
}

std::vector<OutputRoute> AudioEngine::getOutputRoutes() const
{
    const juce::ScopedLock sl (routesLock);
    std::vector<OutputRoute> result;
    result.reserve (outputRoutes.size());
    for (auto& r : outputRoutes)
        result.push_back (r->route);
    return result;
}

juce::Uuid AudioEngine::addInputRoute (InputRoute route)
{
    auto runtime = std::make_unique<InputRouteRuntime>();
    runtime->route = route;
    runtime->ring = std::make_unique<AudioRingBuffer>();
    runtime->ring->setJitterTargetMs (jitterBufferMs.load());
    runtime->receiver = makeReceiver (*runtime);
    auto id = route.id;

    const juce::ScopedLock sl (routesLock);
    inputRoutes.push_back (std::move (runtime));
    return id;
}

void AudioEngine::removeInputRoute (const juce::Uuid& id)
{
    // Extract the runtime under the lock (fast) and let it destruct - which
    // stops its receive thread and can briefly block - outside the lock, so
    // the real-time audio callback is never held up waiting on a network
    // thread to join.
    std::unique_ptr<InputRouteRuntime> extracted;
    {
        const juce::ScopedLock sl (routesLock);
        auto it = std::find_if (inputRoutes.begin(), inputRoutes.end(),
                                 [&] (const std::unique_ptr<InputRouteRuntime>& p) { return p->route.id == id; });
        if (it != inputRoutes.end())
        {
            extracted = std::move (*it);
            inputRoutes.erase (it);
        }
    }
}

void AudioEngine::updateInputRoute (const InputRoute& route)
{
    // Message thread only, so the runtime can't be removed while we work on
    // it outside the lock.
    InputRouteRuntime* runtime = nullptr;
    std::unique_ptr<UdpReceiver> old;
    {
        const juce::ScopedLock sl (routesLock);
        auto it = std::find_if (inputRoutes.begin(), inputRoutes.end(),
                                 [&] (const std::unique_ptr<InputRouteRuntime>& p) { return p->route.id == route.id; });
        if (it == inputRoutes.end())
            return;

        runtime = it->get();
        bool portChanged = runtime->route.listenPort != route.listenPort;
        runtime->route = route;

        if (! portChanged)
            return;

        old = std::move (runtime->receiver);
    }

    // Stop the old receive thread before starting the new one, so only one
    // thread ever writes into the route's buffer.
    old.reset();
    runtime->haveSequence = false;
    auto fresh = makeReceiver (*runtime);

    const juce::ScopedLock sl (routesLock);
    runtime->receiver = std::move (fresh);
}

void AudioEngine::retryUnboundReceivers()
{
    std::vector<InputRouteRuntime*> unbound;
    {
        const juce::ScopedLock sl (routesLock);
        for (auto& r : inputRoutes)
            if ((r->receiver == nullptr || ! r->receiver->isBound()) && jamlink::isValidPort (r->route.listenPort))
                unbound.push_back (r.get());
    }

    for (auto* runtime : unbound)
    {
        auto fresh = makeReceiver (*runtime);
        if (! fresh->isBound())
            continue;

        runtime->haveSequence = false;
        std::unique_ptr<UdpReceiver> old;
        const juce::ScopedLock sl (routesLock);
        old = std::move (runtime->receiver);
        runtime->receiver = std::move (fresh);
    }
}

int AudioEngine::findFreeListenPort() const
{
    auto existing = getInputRoutes();
    auto isTaken = [&] (int port)
    {
        for (auto& r : existing)
            if (r.listenPort == port)
                return true;
        return ! UdpReceiver::isPortFree (port);
    };

    int port = jamlink::kDefaultPort;
    while (port < jamlink::kMaxPort && isTaken (port))
        ++port;
    return port;
}

std::vector<InputRoute> AudioEngine::getInputRoutes() const
{
    const juce::ScopedLock sl (routesLock);
    std::vector<InputRoute> result;
    result.reserve (inputRoutes.size());
    for (auto& r : inputRoutes)
        result.push_back (r->route);
    return result;
}

float AudioEngine::getOutputRouteLevel (const juce::Uuid& id) const
{
    const juce::ScopedLock sl (routesLock);
    for (auto& r : outputRoutes)
        if (r->route.id == id)
            return r->txLevel.load (std::memory_order_relaxed);
    return 0.0f;
}

float AudioEngine::getInputRouteLevel (const juce::Uuid& id) const
{
    const juce::ScopedLock sl (routesLock);
    for (auto& r : inputRoutes)
        if (r->route.id == id)
            return r->rxLevel.load (std::memory_order_relaxed);
    return 0.0f;
}

AudioEngine::PortStatus AudioEngine::getInputRoutePortStatus (const juce::Uuid& id) const
{
    const juce::ScopedLock sl (routesLock);
    for (auto& r : inputRoutes)
    {
        if (r->route.id == id)
        {
            if (! jamlink::isValidPort (r->route.listenPort))
                return PortStatus::invalidPort;
            return r->receiver != nullptr && r->receiver->isBound() ? PortStatus::listening : PortStatus::portInUse;
        }
    }
    return PortStatus::invalidPort;
}

void AudioEngine::setJitterBufferMs (double ms)
{
    jitterBufferMs.store (ms);

    const juce::ScopedLock sl (routesLock);
    for (auto& r : inputRoutes)
        r->ring->setJitterTargetMs (ms);
}

AudioEngine::InputStreamInfo AudioEngine::getInputStreamInfo (const juce::Uuid& id) const
{
    InputStreamInfo info;
    const juce::ScopedLock sl (routesLock);
    for (auto& r : inputRoutes)
    {
        if (r->route.id == id)
        {
            {
                const juce::SpinLock::ScopedLockType sl2 (r->senderLock);
                info.senderIp = r->senderIp;
            }
            info.active = r->route.enabled && r->ring->isPlaying();
            info.bufferMs = r->ring->getAverageFillMs();
            info.alignDelayMs = r->ring->getExtraDelayMs();
            break;
        }
    }
    return info;
}

juce::StringArray AudioEngine::getPeerAddresses() const
{
    juce::StringArray ips;
    const juce::ScopedLock sl (routesLock);
    for (auto& r : outputRoutes)
        if (r->route.enabled && r->route.destHost.isNotEmpty())
            ips.addIfNotAlreadyThere (r->route.destHost);
    for (auto& r : inputRoutes)
    {
        const juce::SpinLock::ScopedLockType sl2 (r->senderLock);
        if (r->senderIp.isNotEmpty())
            ips.addIfNotAlreadyThere (r->senderIp);
    }
    return ips;
}

juce::StringArray AudioEngine::getMidiPeerAddresses() const
{
    juce::StringArray ips;
    const juce::ScopedLock sl (routesLock);
    for (auto& r : outputRoutes)
        if (r->route.enabled && r->route.midi && r->route.destHost.isNotEmpty())
            ips.addIfNotAlreadyThere (r->route.destHost);
    return ips;
}

void AudioEngine::updateAlignment (const std::function<double (const juce::String&)>& roundTripMs)
{
    // Rings are only destroyed on the message thread (this one), so the
    // pointers stay valid after the lock is released. The round-trip lookups
    // happen outside the lock so the audio thread never waits on them.
    struct Stream { AudioRingBuffer* ring; juce::String ip; double natural = 0.0; };
    std::vector<Stream> streams;
    std::vector<AudioRingBuffer*> idle;
    const bool align = alignStreams.load();
    {
        const juce::ScopedLock sl (routesLock);
        for (auto& r : inputRoutes)
        {
            if (! align || ! r->route.enabled || ! r->ring->isPlaying())
            {
                idle.push_back (r->ring.get());
                continue;
            }
            const juce::SpinLock::ScopedLockType sl2 (r->senderLock);
            streams.push_back ({ r->ring.get(), r->senderIp });
        }
    }

    for (auto* ring : idle)
        ring->setExtraDelayMs (0.0);

    // The delay each stream has with no alignment: one-way network time
    // (half the round trip), plus its jitter buffer target, plus the extra
    // queueing its sender's packet bursts cause.
    double slowest = 0.0;
    for (auto& s : streams)
    {
        auto rtt = roundTripMs (s.ip);
        s.natural = (rtt > 0.0 ? rtt * 0.5 : 0.0) + jitterBufferMs.load() + s.ring->getBurstMs();
        slowest = juce::jmax (slowest, s.natural);
    }

    for (auto& s : streams)
    {
        auto wanted = juce::jlimit (0.0, 250.0, slowest - s.natural);
        // Small changes are measurement noise; ignoring them keeps the drift
        // control from constantly nudging the pitch.
        if (std::abs (wanted - s.ring->getExtraDelayMs()) > 3.0)
            s.ring->setExtraDelayMs (wanted);
    }
}

AudioEngine::Stats AudioEngine::getStats() const
{
    return { packetsSent.load (std::memory_order_relaxed),
             packetsReceived.load (std::memory_order_relaxed),
             packetsLost.load (std::memory_order_relaxed) };
}

juce::var AudioEngine::routesToVar() const
{
    juce::Array<juce::var> outs, ins;
    for (auto& r : getOutputRoutes())
        outs.add (r.toVar());
    for (auto& r : getInputRoutes())
        ins.add (r.toVar());

    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("outputs", outs);
    o->setProperty ("inputs", ins);
    return juce::var (o.release());
}

void AudioEngine::setRoutesFromVar (const juce::var& v)
{
    for (auto& r : getOutputRoutes())
        removeOutputRoute (r.id);
    for (auto& r : getInputRoutes())
        removeInputRoute (r.id);

    if (auto* o = v.getDynamicObject())
    {
        if (auto* outs = o->getProperty ("outputs").getArray())
            for (auto& item : *outs)
                addOutputRoute (OutputRoute::fromVar (item));

        if (auto* ins = o->getProperty ("inputs").getArray())
            for (auto& item : *ins)
                addInputRoute (InputRoute::fromVar (item));
    }
}

void AudioEngine::onPacketReceived (InputRouteRuntime& runtime, const void* data, int numBytes, const juce::String& senderIp)
{
    if (numBytes < jamlink::kHeaderSize)
        return;

    jamlink::PacketHeader header;
    std::memcpy (&header, data, (size_t) jamlink::kHeaderSize);

    if (header.magic != jamlink::kPacketMagic)
        return;

    const int numFrames = header.numFrames;
    const int numChannels = header.numChannels;
    const int expectedBytes = jamlink::kHeaderSize + numFrames * numChannels * (int) sizeof (int16_t);
    if (numFrames <= 0 || numChannels < 1 || numChannels > kMaxChannels || numBytes < expectedBytes
        || header.sampleRate < 8000 || header.sampleRate > 384000)
        return;

    packetsReceived.fetch_add (1, std::memory_order_relaxed);

    {
        const juce::SpinLock::ScopedLockType sl (runtime.senderLock);
        if (runtime.senderIp != senderIp)
            runtime.senderIp = senderIp;
    }

    if (runtime.haveSequence)
    {
        auto gap = (int32_t) (header.sequence - (runtime.lastSequence + 1));

        if (gap < 0 && gap > -64)
            return; // late / duplicate packet - its slot has already been played or filled

        if (gap > 0 && gap < 1000)
        {
            packetsLost.fetch_add ((uint64_t) gap, std::memory_order_relaxed);

            // Fill short gaps with silence so later audio stays on time.
            if (gap <= 8)
                runtime.ring->writeSilence (gap * runtime.lastNumFrames);
        }
        // Anything else is a sender restart: just resync.
    }

    const auto* samples = reinterpret_cast<const int16_t*> (static_cast<const char*> (data) + jamlink::kHeaderSize);
    runtime.ring->write (samples, numFrames, numChannels, (double) header.sampleRate);

    runtime.lastSequence = header.sequence;
    runtime.lastNumFrames = numFrames;
    runtime.haveSequence = true;
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate());
}

void AudioEngine::prepare (double sampleRate)
{
    currentSampleRate.store (sampleRate);

    const juce::ScopedLock sl (routesLock);
    for (auto& r : outputRoutes)
        r->preparedInRate = 0.0;
}

void AudioEngine::audioDeviceStopped()
{
}

void AudioEngine::sendRoute (OutputRouteRuntime& runtime, const float* const* inputChannelData,
                             int numInputChannels, int numSamples)
{
    const auto& route = runtime.route;
    const int numCh = route.numChannels;
    if (route.sourceChannel < 0 || route.sourceChannel >= numInputChannels || ! runtime.destination.valid)
        return;

    const double deviceRate = currentSampleRate.load (std::memory_order_relaxed);
    const double requested = streamSampleRate.load (std::memory_order_relaxed);
    const double streamRate = requested > 0.0 ? requested : deviceRate;

    // Gather the source channels; channels past the device's last input are silent.
    const float* src[kMaxChannels] = {};
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto index = route.sourceChannel + ch;
        src[ch] = index < numInputChannels ? inputChannelData[index] : nullptr;
    }

    const float* frames[kMaxChannels] = {};
    int numFrames = numSamples;

    if (std::abs (streamRate - deviceRate) < 0.5)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            if (src[ch] == nullptr)
            {
                txScratch.clear (ch, 0, numSamples);
                src[ch] = txScratch.getReadPointer (ch);
            }
            frames[ch] = src[ch];
        }
    }
    else
    {
        if (std::abs (runtime.preparedInRate - deviceRate) > 0.5 || std::abs (runtime.preparedOutRate - streamRate) > 0.5)
        {
            for (auto& r : runtime.resamplers)
                r.prepare (deviceRate, streamRate);
            runtime.preparedInRate = deviceRate;
            runtime.preparedOutRate = streamRate;
        }

        static const float silence[StreamResampler::kMaxBlock] = {};
        auto in = juce::jmin (numSamples, StreamResampler::kMaxBlock);
        for (int ch = 0; ch < numCh; ++ch)
            numFrames = runtime.resamplers[(size_t) ch].process (src[ch] != nullptr ? src[ch] : silence, in,
                                                                 txScratch.getWritePointer (ch), kScratchFrames);
        for (int ch = 0; ch < numCh; ++ch)
            frames[ch] = txScratch.getReadPointer (ch);
    }

    float peak = 0.0f;
    const int framesPerPacket = juce::jmin (jamlink::maxFramesPerPacket (numCh), 65535);

    for (int start = 0; start < numFrames; start += framesPerPacket)
    {
        const int count = juce::jmin (framesPerPacket, numFrames - start);

        for (int i = 0; i < count; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto s = juce::jlimit (-1.0f, 1.0f, frames[ch][start + i]);
                peak = juce::jmax (peak, std::abs (s));
                interleaved[(size_t) (i * numCh + ch)] = (int16_t) (s * 32767.0f);
            }
        }

        jamlink::PacketHeader header { jamlink::kPacketMagic, runtime.sequence++, (uint32_t) streamRate,
                                        (uint16_t) count, (uint8_t) numCh, 0 };
        const auto payloadBytes = count * numCh * (int) sizeof (int16_t);
        std::memcpy (packetBuffer.data(), &header, (size_t) jamlink::kHeaderSize);
        std::memcpy (packetBuffer.data() + jamlink::kHeaderSize, interleaved.data(), (size_t) payloadBytes);

        if (runtime.socket.sendTo (packetBuffer.data(), jamlink::kHeaderSize + payloadBytes, runtime.destination) > 0)
            packetsSent.fetch_add (1, std::memory_order_relaxed);
    }

    runtime.txLevel.store (peak, std::memory_order_relaxed);
}

void AudioEngine::receiveRoute (InputRouteRuntime& runtime, float* const* outputChannelData,
                                int numOutputChannels, int numSamples)
{
    const auto& route = runtime.route;
    if (route.destChannel < 0 || route.destChannel >= numOutputChannels)
        return;

    const double deviceRate = currentSampleRate.load (std::memory_order_relaxed);
    auto* const* scratch = rxScratch.getArrayOfWritePointers();
    float peak = 0.0f;

    for (int offset = 0; offset < numSamples; offset += kScratchFrames)
    {
        const int n = juce::jmin (kScratchFrames, numSamples - offset);
        const int streamCh = runtime.ring->read (scratch, n, deviceRate);
        if (streamCh == 0)
            continue;

        // Mono route + multichannel stream: mix down into scratch[0].
        if (route.numChannels == 1 && streamCh > 1)
        {
            for (int ch = 1; ch < streamCh; ++ch)
                juce::FloatVectorOperations::add (scratch[0], scratch[ch], n);
            juce::FloatVectorOperations::multiply (scratch[0], 1.0f / (float) streamCh, n);
        }

        for (int j = 0; j < route.numChannels; ++j)
        {
            const int outCh = route.destChannel + j;
            if (outCh >= numOutputChannels || outputChannelData[outCh] == nullptr)
                continue;

            // A mono stream feeds every channel of the route.
            const int srcCh = streamCh == 1 ? 0 : j;
            if (srcCh >= streamCh)
                continue;

            juce::FloatVectorOperations::add (outputChannelData[outCh] + offset, scratch[srcCh], n);
            peak = juce::jmax (peak, juce::FloatVectorOperations::findMaximum (scratch[srcCh], n),
                               -juce::FloatVectorOperations::findMinimum (scratch[srcCh], n));
        }
    }

    runtime.rxLevel.store (peak, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                     float* const* outputChannelData, int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    process (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void AudioEngine::process (const float* const* inputChannelData, int numInputChannels,
                           float* const* outputChannelData, int numOutputChannels, int numSamples)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    const juce::ScopedLock sl (routesLock);

    for (auto& runtime : outputRoutes)
        if (runtime->route.enabled)
            sendRoute (*runtime, inputChannelData, numInputChannels, juce::jmin (numSamples, StreamResampler::kMaxBlock));

    for (auto& runtime : inputRoutes)
    {
        if (runtime->route.enabled)
            receiveRoute (*runtime, outputChannelData, numOutputChannels, numSamples);
        else
            runtime->rxLevel.store (0.0f, std::memory_order_relaxed);
    }
}
