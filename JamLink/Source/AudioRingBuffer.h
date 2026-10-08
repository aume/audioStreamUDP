#pragma once

#include <atomic>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "PacketFormat.h"

// Single-producer / single-consumer jitter buffer for one incoming stream.
//
// The producer (a UDP receive thread) appends interleaved frames of up to
// kMaxChannels channels. The consumer (the real-time audio callback) reads
// them out at the local device's rate. It:
//
//  - Primes: stays silent until `jitterTarget` worth of audio has queued up,
//    so normal network jitter and mismatched block sizes between the two
//    machines never cause an underrun.
//  - Tracks drift: two sound cards never run at exactly the same rate. The
//    consumer reads with a resampling ratio that is nudged (by at most
//    +/-0.3%) to hold the queue's low-water mark at the target, so it
//    neither slowly drains (periodic dropout) nor slowly fills (periodic
//    skip).
//  - Resamples: the stream's rate (from the packet header) may differ from
//    the device rate, e.g. a 32 kHz network stream played on a 48 kHz device.
//  - Recovers: on an underrun it fades out and re-primes; if a burst leaves
//    far too much queued it skips ahead to the target.
//
// Only the producer writes `writePos` and only the consumer writes
// `readPos`, so no position is ever written by both threads.
class AudioRingBuffer
{
public:
    static constexpr int kMaxChannels = jamlink::kMaxChannels;

    explicit AudioRingBuffer (int requestedCapacityFrames = 32768)
        : capacity (nextPowerOfTwo (requestedCapacityFrames)),
          mask ((uint32_t) capacity - 1),
          data ((size_t) capacity * kMaxChannels, 0.0f)
    {
    }

    void setJitterTargetMs (double ms) noexcept   { jitterTargetMs.store (ms, std::memory_order_relaxed); }

    // Extra delay on top of the jitter target, used to line this stream up
    // with slower ones. Changes are absorbed gradually by the drift control.
    void setExtraDelayMs (double ms) noexcept     { extraDelayMs.store (ms, std::memory_order_relaxed); }
    double getExtraDelayMs() const noexcept       { return extraDelayMs.load (std::memory_order_relaxed); }

    // Average amount of audio queued, in ms - i.e. the latency this buffer
    // adds. Any thread.
    double getAverageFillMs() const noexcept      { return averageFillMs.load (std::memory_order_relaxed); }
    bool isPlaying() const noexcept               { return playingFlag.load (std::memory_order_relaxed); }

    // How far the average queue sits above its low-water mark, in ms: the
    // latency caused by the sender's packet bursts and block-size mismatch.
    // Unlike getAverageFillMs() it doesn't move while an alignment delay is
    // still being absorbed, so it's safe to compute alignment from.
    double getBurstMs() const noexcept            { return burstMs.load (std::memory_order_relaxed); }

    // ---- Producer only (network receive thread) -------------------------

    void write (const int16_t* interleaved, int numFrames, int numChannels, double sampleRate) noexcept
    {
        numChannels = std::clamp (numChannels, 1, kMaxChannels);

        auto w = writePos.load (std::memory_order_relaxed);
        auto r = readPos.load (std::memory_order_acquire);

        // Leave a few frames of headroom so the interpolator's look-behind
        // sample is never overwritten. If the consumer has stalled (e.g. the
        // route is disabled) just drop - the consumer skips ahead when it
        // resumes.
        if ((int) (w - r) + numFrames > capacity - 8)
            return;

        for (int i = 0; i < numFrames; ++i)
        {
            auto* frame = &data[(size_t) ((w + (uint32_t) i) & mask) * kMaxChannels];
            for (int ch = 0; ch < numChannels; ++ch)
                frame[ch] = (float) interleaved[i * numChannels + ch] * (1.0f / 32768.0f);
        }

        streamChannels.store (numChannels, std::memory_order_relaxed);
        streamRate.store (sampleRate, std::memory_order_relaxed);
        writePos.store (w + (uint32_t) numFrames, std::memory_order_release);
    }

    // Used to keep timing stable across a lost packet: the gap is filled
    // with silence instead of pulling later audio forward.
    void writeSilence (int numFrames) noexcept
    {
        auto w = writePos.load (std::memory_order_relaxed);
        auto r = readPos.load (std::memory_order_acquire);
        if ((int) (w - r) + numFrames > capacity - 8)
            return;

        for (int i = 0; i < numFrames; ++i)
            std::fill_n (&data[(size_t) ((w + (uint32_t) i) & mask) * kMaxChannels], kMaxChannels, 0.0f);

        writePos.store (w + (uint32_t) numFrames, std::memory_order_release);
    }

    // ---- Consumer only (real-time audio thread) -------------------------

    // Renders numFrames at deviceRate into dest[0..kMaxChannels). Returns the
    // number of channels the stream carries (0 while priming / silent).
    int read (float* const* dest, int numFrames, double deviceRate) noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
            std::fill_n (dest[ch], numFrames, 0.0f);

        const auto rate = streamRate.load (std::memory_order_relaxed);
        if (rate <= 0.0 || deviceRate <= 0.0)
            return 0;

        const int numCh = streamChannels.load (std::memory_order_relaxed);
        const double target = std::max (64.0, (jitterTargetMs.load (std::memory_order_relaxed)
                                                 + extraDelayMs.load (std::memory_order_relaxed)) * rate / 1000.0);

        auto r = readPos.load (std::memory_order_relaxed);
        auto w = writePos.load (std::memory_order_acquire);
        auto avail = (int) (w - r);

        const double needed = numFrames * rate / deviceRate;

        // Hard cap: a huge backlog (e.g. the route was just re-enabled) is
        // dropped straight away, keeping only the newest audio.
        if ((double) avail - needed > target * 4.0 + 4096.0)
        {
            r = w - (uint32_t) (target + needed);
            avail = (int) (target + needed);
            fraction = 0.0;
            lowWater = target;
            windowMin = 1.0e9;
            windowFrames = 0;
            readPos.store (r, std::memory_order_release);
        }

        if (! playing)
        {
            playingFlag.store (false, std::memory_order_relaxed);
            if (avail < (int) (target + needed) + 4)
                return 0;

            playing = true;
            fadeIn = true;
            fraction = 1.0;  // keep one frame of look-behind for the interpolator
            lowWater = target; // until a window has measured the real low-water mark
            fillAverage = avail;
            playingFlag.store (true, std::memory_order_relaxed);
            windowMin = 1.0e9;
            windowFrames = 0;
        }

        // Track the queue's low-water mark (the least audio left over after a
        // read, over ~0.5 s windows) and nudge the playback ratio so that
        // low-water mark settles on the target. Regulating the minimum rather
        // than the average means bursty senders (large packets, big device
        // buffers) automatically get a deeper buffer instead of underrunning.
        windowMin = std::min (windowMin, (double) avail - needed);
        windowFrames += numFrames;
        if (windowFrames >= (int) (deviceRate * 0.5))
        {
            // If even the lowest point of the window had far more than the
            // target queued (e.g. after a network stall caught up), drop the
            // excess now rather than slowly pitch-shifting it away.
            if (windowMin > target * 2.0 + 256.0)
            {
                auto drop = (int) (windowMin - target);
                r += (uint32_t) drop;
                avail -= drop;
                readPos.store (r, std::memory_order_release);
                windowMin = target;
            }

            lowWater += 0.3 * (windowMin - lowWater);
            windowMin = 1.0e9;
            windowFrames = 0;
        }

        fillAverage += (1.0 - std::exp (-(double) numFrames / deviceRate)) * ((double) avail - fillAverage);
        averageFillMs.store (fillAverage * 1000.0 / rate, std::memory_order_relaxed);
        burstMs.store (std::max (0.0, fillAverage - lowWater) * 1000.0 / rate, std::memory_order_relaxed);

        const double errorSeconds = (lowWater - target) / rate;
        const double correction = std::clamp (errorSeconds * 0.1, -0.003, 0.003);
        const double step = rate / deviceRate * (1.0 + correction);

        double pos = fraction;
        int produced = 0;

        for (; produced < numFrames; ++produced)
        {
            auto idx = (int) pos;
            if (idx + 2 >= avail)
                break;

            const float t = (float) (pos - idx);
            const auto* xm1 = frameAt (r + (uint32_t) idx - 1);
            const auto* x0  = frameAt (r + (uint32_t) idx);
            const auto* x1  = frameAt (r + (uint32_t) idx + 1);
            const auto* x2  = frameAt (r + (uint32_t) idx + 2);

            for (int ch = 0; ch < numCh; ++ch)
                dest[ch][produced] = hermite (xm1[ch], x0[ch], x1[ch], x2[ch], t);

            pos += step;
        }

        if (fadeIn)
        {
            applyRamp (dest, numCh, 0, std::min (produced, kFadeFrames), true);
            fadeIn = false;
        }

        if (produced < numFrames)
        {
            // Underrun: fade out what we did render to avoid a click, then
            // go back to priming.
            auto fadeLen = std::min (produced, kFadeFrames);
            applyRamp (dest, numCh, produced - fadeLen, fadeLen, false);
            playing = false;
        }

        auto consumed = std::max (0, std::min ((int) pos - 1, avail - 1));
        fraction = pos - consumed;
        readPos.store (r + (uint32_t) consumed, std::memory_order_release);

        return numCh;
    }

private:
    static constexpr int kFadeFrames = 64;

    static int nextPowerOfTwo (int n) noexcept
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    const float* frameAt (uint32_t index) const noexcept
    {
        return &data[(size_t) (index & mask) * kMaxChannels];
    }

    // 4-point, 3rd-order Hermite interpolation.
    static float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    static void applyRamp (float* const* dest, int numCh, int start, int length, bool rising) noexcept
    {
        for (int i = 0; i < length; ++i)
        {
            auto g = (float) (i + 1) / (float) (length + 1);
            if (! rising) g = 1.0f - g;
            for (int ch = 0; ch < numCh; ++ch)
                dest[ch][start + i] *= g;
        }
    }

    int capacity;
    uint32_t mask;
    std::vector<float> data;
    std::atomic<uint32_t> readPos { 0 };
    std::atomic<uint32_t> writePos { 0 };
    std::atomic<int> streamChannels { 1 };
    std::atomic<double> streamRate { 0.0 };
    std::atomic<double> jitterTargetMs { 20.0 };
    std::atomic<double> extraDelayMs { 0.0 };
    std::atomic<double> averageFillMs { 0.0 };
    std::atomic<double> burstMs { 0.0 };
    std::atomic<bool> playingFlag { false };

    // Consumer-only state.
    bool playing = false;
    bool fadeIn = false;
    double fraction = 1.0;
    double lowWater = 0.0;
    double fillAverage = 0.0;
    double windowMin = 1.0e9;
    int windowFrames = 0;
};
