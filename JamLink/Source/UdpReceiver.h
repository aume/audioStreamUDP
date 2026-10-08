#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>
#include "PacketFormat.h"
#include "NetAddress.h"

// Owns a bound UDP socket and a dedicated thread that blocks waiting for
// packets, handing each one to a callback. One instance per listening port.
// If the port can't be bound (invalid, or already taken by another route or
// another app such as SuperCollider) no thread is started; check isBound().
class UdpReceiver : private juce::Thread
{
public:
    using PacketCallback = std::function<void (const void* data, int numBytes, const juce::String& senderIP)>;

    UdpReceiver (int portToBind, PacketCallback callbackToUse)
        : juce::Thread ("JamLink Rx " + juce::String (portToBind)),
          callback (std::move (callbackToUse))
    {
        if (! jamlink::isValidPort (portToBind))
            return;

        socket = std::make_unique<UdpSocket>();
        bound = socket->bind (portToBind);

        if (bound)
            startThread (juce::Thread::Priority::high);
        else
            socket.reset();
    }

    ~UdpReceiver() override
    {
        if (socket != nullptr)
        {
            // The thread wakes at least every 100 ms to check this.
            signalThreadShouldExit();
            stopThread (2000);
        }
    }

    bool isBound() const noexcept { return bound; }

    // True if nothing else on this machine is listening on the UDP port.
    static bool isPortFree (int port)
    {
        if (! jamlink::isValidPort (port))
            return false;
        UdpSocket probe;
        return probe.bind (port);
    }

private:
    void run() override
    {
        // Large enough for any datagram, so oversized/foreign packets are
        // read whole and then rejected rather than left half-read.
        std::vector<char> buffer (65536);
        juce::String senderIP;

        while (! threadShouldExit())
        {
            auto bytesRead = socket->receive (buffer.data(), (int) buffer.size(), 100, senderIP);
            if (bytesRead > 0 && callback)
                callback (buffer.data(), bytesRead, senderIP);
            else if (bytesRead < 0)
                juce::Thread::sleep (10);
        }
    }

    std::unique_ptr<UdpSocket> socket;
    PacketCallback callback;
    bool bound = false;
};
