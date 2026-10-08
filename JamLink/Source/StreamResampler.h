#pragma once

#include <JuceHeader.h>
#include <vector>

// Converts one channel of audio from the device's sample rate to the network
// stream rate (e.g. 48 kHz device -> 32 kHz stream) on the sending side.
// Streaming/stateful: any block size in, a varying number of samples out.
// When downsampling, a 4th-order Butterworth low-pass removes content above
// the new Nyquist first so it doesn't alias.
class StreamResampler
{
public:
    static constexpr int kMaxBlock = 4096;

    StreamResampler()
    {
        history.reserve ((size_t) kMaxBlock + 8);
    }

    void prepare (double inputRate, double outputRate)
    {
        step = inputRate / outputRate;
        filtering = outputRate < inputRate;
        if (filtering)
        {
            auto cutoff = outputRate * 0.45;
            // Q values for a 4th-order Butterworth split into two biquads.
            lowPassA.setCoefficients (juce::IIRCoefficients::makeLowPass (inputRate, cutoff, 0.5412));
            lowPassB.setCoefficients (juce::IIRCoefficients::makeLowPass (inputRate, cutoff, 1.3066));
        }
        lowPassA.reset();
        lowPassB.reset();
        history.assign (3, 0.0f);
        position = 1.0;
    }

    // Consumes numIn (<= kMaxBlock) samples; returns how many were written to out.
    int process (const float* in, int numIn, float* out, int maxOut) noexcept
    {
        auto base = history.size();
        history.resize (base + (size_t) numIn);
        std::copy (in, in + numIn, history.begin() + (long) base);

        if (filtering)
        {
            lowPassA.processSamples (history.data() + base, numIn);
            lowPassB.processSamples (history.data() + base, numIn);
        }

        int produced = 0;
        while (produced < maxOut)
        {
            auto idx = (size_t) position;
            if (idx + 2 >= history.size())
                break;

            auto t = (float) (position - (double) idx);
            auto xm1 = history[idx - 1], x0 = history[idx], x1 = history[idx + 1], x2 = history[idx + 2];
            auto c1 = 0.5f * (x1 - xm1);
            auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            out[produced++] = ((c3 * t + c2) * t + c1) * t + x0;
            position += step;
        }

        // Drop everything except the one sample of look-behind we still need.
        auto drop = (size_t) position - 1;
        history.erase (history.begin(), history.begin() + (long) drop);
        position -= (double) drop;
        return produced;
    }

private:
    std::vector<float> history;
    double step = 1.0;
    double position = 1.0;
    bool filtering = false;
    juce::SingleThreadedIIRFilter lowPassA, lowPassB;
};
