#include "PeerLink.h"
#include "NetAddress.h"
#include <algorithm>
#include <array>
#include <cerrno>

namespace
{
    constexpr int kConnectTimeoutMs = 1500;
    constexpr juce::uint32 kRetryIntervalMs = 3000;
    constexpr juce::uint32 kRequestTimeoutMs = 75000;  // the other player may take a while to accept
    constexpr juce::uint32 kMaxMessageBytes = 1 << 20;

    enum MessageType : char
    {
        hello  = 'H',  // payload: computer name (UTF-8)
        ping   = 'P',  // payload: sender's double timestamp, echoed back as a pong
        pong   = 'Q',
        midi   = 'M',  // payload: one raw MIDI message (sysex included)
        stream = 'S',  // payload: JSON stream request
        answer = 'A'   // payload: JSON answer to a stream request
    };

    juce::MemoryBlock makeMessage (char type, const void* payload, size_t size)
    {
        juce::MemoryBlock block (size + 1);
        block[0] = type;
        if (size > 0)
            block.copyFrom (payload, 1, size);
        return block;
    }

    juce::MemoryBlock makeJsonMessage (char type, const juce::var& v)
    {
        auto json = juce::JSON::toString (v, true).toStdString();
        return makeMessage (type, json.data(), json.size());
    }

    // The same peer must always map to the same text, whether we typed it,
    // discovered it or saw it on an incoming socket.
    juce::String normalise (const juce::String& address)
    {
        auto parsed = jamlink::net::parse (address, 0);
        return parsed.valid ? jamlink::net::toString (parsed.get()) : address.trim();
    }

    juce::var withProperty (const juce::var& source, const juce::Identifier& name, const juce::var& value)
    {
        auto copy = std::make_unique<juce::DynamicObject>();
        if (auto* o = source.getDynamicObject())
            for (auto& p : o->getProperties())
                copy->setProperty (p.name, p.value);
        copy->setProperty (name, value);
        return juce::var (copy.release());
    }

    juce::var errorAnswer (const juce::String& message)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("ok", false);
        o->setProperty ("error", message);
        return juce::var (o.release());
    }
}

//==============================================================================
// One framed TCP connection: each message is a 4-byte big-endian length
// followed by the payload. Reads happen on this object's own thread.
class PeerLink::Connection : public juce::Thread
{
public:
    Connection (PeerLink& ownerToUse, int socketToUse, const juce::String& addressToUse, int idToUse)
        : juce::Thread ("JamLink link " + addressToUse),
          owner (ownerToUse), fd (socketToUse), address (addressToUse), id (idToUse)
    {
        // A peer that stops reading mustn't block MIDI/pings forever.
        timeval timeout { 2, 0 };
        setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));
    }

    ~Connection() override
    {
        signalThreadShouldExit();
        ::shutdown (fd, SHUT_RDWR);
        stopThread (3000);
        ::close (fd);
    }

    void begin()
    {
        auto name = juce::SystemStats::getComputerName().toStdString();
        send (makeMessage (hello, name.data(), name.size()));
        sendPing();
        startThread();
    }

    bool isAlive() const noexcept                  { return alive.load(); }
    int getId() const noexcept                     { return id; }
    const juce::String& getAddress() const noexcept { return address; }
    double getRoundTripMs() const noexcept         { return roundTripMs.load(); }

    juce::String getName() const
    {
        const juce::SpinLock::ScopedLockType sl (nameLock);
        return peerName;
    }

    // Any thread. Pings go out on the message thread, pongs on this
    // connection's thread and MIDI on MIDI threads, so sends are serialised
    // here to keep messages from interleaving on the stream.
    bool send (const juce::MemoryBlock& block)
    {
        const juce::ScopedLock sl (sendLock);
        if (! alive.load())
            return false;

        auto length = htonl ((uint32_t) block.getSize());
        if (writeAll (&length, sizeof (length)) && writeAll (block.getData(), block.getSize()))
            return true;

        alive = false;
        ::shutdown (fd, SHUT_RDWR);
        return false;
    }

    void sendPing()
    {
        auto now = juce::Time::getMillisecondCounterHiRes();
        send (makeMessage (ping, &now, sizeof (now)));
    }

private:
    bool writeAll (const void* data, size_t size)
    {
        auto* p = static_cast<const char*> (data);
        while (size > 0)
        {
            auto n = ::send (fd, p, size, 0);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return false;
            p += n;
            size -= (size_t) n;
        }
        return true;
    }

    bool readAll (void* data, size_t size)
    {
        auto* p = static_cast<char*> (data);
        while (size > 0)
        {
            if (threadShouldExit())
                return false;

            auto ready = jamlink::net::waitReadable (fd, 100);
            if (ready == 0)
                continue;
            if (ready < 0)
                return false;

            auto n = ::recv (fd, p, size, 0);
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            if (n <= 0)
                return false; // closed or failed
            p += n;
            size -= (size_t) n;
        }
        return true;
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            uint32_t length = 0;
            if (! readAll (&length, sizeof (length)))
                break;

            length = ntohl (length);
            if (length == 0 || length > kMaxMessageBytes)
                break;

            juce::MemoryBlock message (length);
            if (! readAll (message.getData(), length))
                break;

            handleMessage (message);
        }

        alive = false;
    }

    void handleMessage (const juce::MemoryBlock& message)
    {
        auto type = static_cast<const char*> (message.getData())[0];
        auto* payload = static_cast<const char*> (message.getData()) + 1;
        auto size = message.getSize() - 1;

        switch (type)
        {
            case hello:
            {
                const juce::SpinLock::ScopedLockType sl (nameLock);
                peerName = juce::String::fromUTF8 (payload, (int) size);
                break;
            }

            case ping:
                send (makeMessage (pong, payload, size));
                break;

            case pong:
                if (size == sizeof (double))
                {
                    double sent;
                    std::memcpy (&sent, payload, sizeof (sent));
                    addRoundTripSample (juce::Time::getMillisecondCounterHiRes() - sent);
                }
                break;

            case midi:
                if (size > 0)
                    owner.handleMidiFromPeer (juce::MidiMessage (payload, (int) size));
                break;

            case stream:
                owner.handleStreamRequest (id, address, getName(), juce::JSON::parse (juce::String::fromUTF8 (payload, (int) size)));
                break;

            case answer:
            {
                auto reply = juce::JSON::parse (juce::String::fromUTF8 (payload, (int) size));
                auto requestId = (int) reply.getProperty ("req", 0);
                juce::MessageManager::callAsync ([weak = std::weak_ptr<PeerLink*> (owner.aliveToken), requestId, reply]
                {
                    if (auto link = weak.lock())
                        (*link)->finishRequest (requestId, reply);
                });
                break;
            }

            default:
                break;
        }
    }

    // Median of the last few pings, so one slow packet doesn't make the
    // displayed delay (or the stream alignment) jump around.
    void addRoundTripSample (double ms)
    {
        samples[(size_t) (numSamples++ % samples.size())] = ms;
        auto count = std::min (numSamples, samples.size());
        std::array<double, 7> sorted {};
        std::copy_n (samples.begin(), count, sorted.begin());
        std::sort (sorted.begin(), sorted.begin() + (long) count);
        roundTripMs = sorted[count / 2];
    }

    PeerLink& owner;
    const int fd;
    const juce::String address;
    const int id;
    std::atomic<bool> alive { true };
    std::atomic<double> roundTripMs { -1.0 };
    std::array<double, 7> samples {};
    size_t numSamples = 0;

    juce::SpinLock nameLock;
    juce::String peerName;
    juce::CriticalSection sendLock;
};

//==============================================================================
// Accepts incoming control connections on every interface, including
// peer-to-peer Wi-Fi.
class PeerLink::Server : public juce::Thread
{
public:
    explicit Server (PeerLink& ownerToUse) : juce::Thread ("JamLink link server"), owner (ownerToUse) {}

    ~Server() override
    {
        signalThreadShouldExit();
        stopThread (2000);
        if (fd >= 0)
            ::close (fd);
    }

    bool start (int port)
    {
        fd = jamlink::net::makeSocket (SOCK_STREAM);
        if (fd < 0)
            return false;

        int on = 1;
        setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof (on));
        if (! jamlink::net::bindAny (fd, port) || ::listen (fd, 16) != 0)
        {
            ::close (fd);
            fd = -1;
            return false;
        }

        startThread();
        return true;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            if (jamlink::net::waitReadable (fd, 200) != 1)
                continue;

            sockaddr_storage from {};
            socklen_t fromLength = sizeof (from);
            int client = ::accept (fd, reinterpret_cast<sockaddr*> (&from), &fromLength);
            if (client < 0)
                continue;

            int on = 1;
            setsockopt (client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof (on));
            setsockopt (client, IPPROTO_TCP, TCP_NODELAY, &on, sizeof (on));
            setsockopt (client, SOL_SOCKET, jamlink::net::SO_RECV_ANYIF, &on, sizeof (on));

            auto address = jamlink::net::toString (reinterpret_cast<const sockaddr*> (&from));
            auto connection = std::make_unique<Connection> (owner, client, address, owner.nextConnectionId++);
            auto* raw = connection.get();
            owner.addConnection (std::move (connection));
            raw->begin();
        }
    }

    PeerLink& owner;
    int fd = -1;
};

//==============================================================================
PeerLink::PeerLink (bool listenForPeers)
{
    if (listenForPeers)
    {
        server = std::make_unique<Server> (*this);
        listening = server->start (kControlPort);
    }
    startTimer (1000);
}

PeerLink::~PeerLink()
{
    stopTimer();
    server.reset();
    connectPool.removeAllJobs (true, 4000);

    std::vector<std::unique_ptr<Connection>> toDelete;
    {
        const juce::ScopedLock sl (lock);
        toDelete.swap (connections);
    }
}

void PeerLink::addConnection (std::unique_ptr<Connection> connection)
{
    const juce::ScopedLock sl (lock);
    connections.push_back (std::move (connection));
}

PeerLink::Connection* PeerLink::findLiveConnection (const juce::String& address) const
{
    // Caller holds `lock`.
    for (auto& c : connections)
        if (c->isAlive() && c->getAddress() == address)
            return c.get();
    return nullptr;
}

// Blocking: only call from the connect pool.
PeerLink::Connection* PeerLink::connectNow (const juce::String& address)
{
    auto target = jamlink::net::parse (address, kControlPort);
    if (! target.valid)
        return nullptr;

    int fd = jamlink::net::makeSocket (SOCK_STREAM);
    if (fd < 0)
        return nullptr;

    auto flags = fcntl (fd, F_GETFL, 0);
    fcntl (fd, F_SETFL, flags | O_NONBLOCK);

    bool ok = ::connect (fd, target.get(), target.size()) == 0;
    if (! ok && errno == EINPROGRESS)
    {
        pollfd p { fd, POLLOUT, 0 };
        if (::poll (&p, 1, kConnectTimeoutMs) == 1)
        {
            int error = 0;
            socklen_t length = sizeof (error);
            ok = getsockopt (fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0;
        }
    }

    if (! ok)
    {
        ::close (fd);
        return nullptr;
    }

    fcntl (fd, F_SETFL, flags);

    auto connection = std::make_unique<Connection> (*this, fd, jamlink::net::toString (target.get()), nextConnectionId++);
    auto* raw = connection.get();
    addConnection (std::move (connection));
    raw->begin();
    return raw;
}

void PeerLink::setWantedPeers (const juce::StringArray& addresses)
{
    juce::StringArray normalised;
    for (auto& a : addresses)
        normalised.addIfNotAlreadyThere (normalise (a));

    const juce::ScopedLock sl (lock);
    wantedPeers = normalised;
}

void PeerLink::setMidiPeers (const juce::StringArray& addresses)
{
    juce::StringArray normalised;
    for (auto& a : addresses)
        normalised.addIfNotAlreadyThere (normalise (a));

    const juce::ScopedLock sl (lock);
    midiPeers = normalised;
}

void PeerLink::setMidiCallback (std::function<void (const juce::MidiMessage&)> callback)
{
    const juce::ScopedLock sl (callbackLock);
    onMidiReceived = std::move (callback);
}

double PeerLink::getRoundTripMs (const juce::String& address) const
{
    auto key = normalise (address);
    double best = -1.0;
    const juce::ScopedLock sl (lock);
    for (auto& c : connections)
    {
        if (c->isAlive() && c->getAddress() == key)
        {
            auto rtt = c->getRoundTripMs();
            if (rtt >= 0.0 && (best < 0.0 || rtt < best))
                best = rtt;
        }
    }
    return best;
}

juce::String PeerLink::getPeerName (const juce::String& address) const
{
    auto key = normalise (address);
    const juce::ScopedLock sl (lock);
    for (auto& c : connections)
        if (c->isAlive() && c->getAddress() == key)
            if (auto name = c->getName(); name.isNotEmpty())
                return name;
    return {};
}

int PeerLink::getNumConnectedPeers() const
{
    juce::StringArray addresses;
    const juce::ScopedLock sl (lock);
    for (auto& c : connections)
        if (c->isAlive())
            addresses.addIfNotAlreadyThere (c->getAddress());
    return addresses.size();
}

void PeerLink::sendMidi (const juce::MidiMessage& message)
{
    auto block = makeMessage (midi, message.getRawData(), (size_t) message.getRawDataSize());

    // Two JamLinks that both send audio to each other end up with a
    // connection in each direction; send over just one per peer so MIDI
    // isn't doubled.
    juce::StringArray sentTo;
    const juce::ScopedLock sl (lock);
    for (auto& c : connections)
    {
        auto& address = c->getAddress();
        if (c->isAlive() && midiPeers.contains (address) && ! sentTo.contains (address))
        {
            if (c->send (block))
            {
                sentTo.add (address);
                midiSent.fetch_add (1);
            }
        }
    }
}

void PeerLink::handleMidiFromPeer (const juce::MidiMessage& message)
{
    midiReceived.fetch_add (1);
    const juce::ScopedLock sl (callbackLock);
    if (onMidiReceived)
        onMidiReceived (message);
}

void PeerLink::requestStream (const juce::String& address, const juce::var& request,
                              std::function<void (const juce::var&)> onAnswer)
{
    auto requestId = nextRequestId++;
    pendingRequests[requestId] = { std::move (onAnswer), juce::Time::getMillisecondCounter() + kRequestTimeoutMs };

    auto key = normalise (address);
    auto message = makeJsonMessage (stream, withProperty (request, "req", requestId));

    connectPool.addJob ([this, key, message, requestId, weak = std::weak_ptr<PeerLink*> (aliveToken)]
    {
        bool sent = false;
        {
            const juce::ScopedLock sl (lock);
            if (auto* c = findLiveConnection (key))
                sent = c->send (message);
        }

        if (! sent && connectNow (key) != nullptr)
        {
            const juce::ScopedLock sl (lock);
            if (auto* c = findLiveConnection (key))
                sent = c->send (message);
        }

        if (! sent)
        {
            juce::MessageManager::callAsync ([weak, requestId, key]
            {
                if (auto link = weak.lock())
                    (*link)->finishRequest (requestId, errorAnswer ("Couldn't reach JamLink at " + key + "."));
            });
        }
    });
}

void PeerLink::handleStreamRequest (int connectionId, const juce::String& fromAddress, const juce::String& fromName,
                                    const juce::var& request)
{
    // Network thread: hop to the message thread to deal with it.
    juce::MessageManager::callAsync ([weak = std::weak_ptr<PeerLink*> (aliveToken), connectionId, fromAddress, fromName, request]
    {
        auto link = weak.lock();
        if (link == nullptr)
            return;

        auto* self = *link;
        auto requestId = (int) request.getProperty ("req", 0);

        auto respond = [weak, connectionId, requestId] (const juce::var& reply)
        {
            auto alive = weak.lock();
            if (alive == nullptr)
                return;

            auto* owner = *alive;
            auto message = makeJsonMessage (answer, withProperty (reply, "req", requestId));
            const juce::ScopedLock sl (owner->lock);
            for (auto& c : owner->connections)
                if (c->getId() == connectionId && c->isAlive())
                    c->send (message);
        };

        if (self->streamRequestHandler)
            self->streamRequestHandler (fromAddress, fromName, request, respond);
        else
            respond (errorAnswer ("That JamLink isn't accepting streams."));
    });
}

void PeerLink::finishRequest (int requestId, const juce::var& reply)
{
    auto it = pendingRequests.find (requestId);
    if (it == pendingRequests.end())
        return;

    auto callback = std::move (it->second.onAnswer);
    pendingRequests.erase (it);
    if (callback)
        callback (reply);
}

void PeerLink::timerCallback()
{
    // Drop dead connections (destroyed here, on the message thread, never on
    // their own thread).
    std::vector<std::unique_ptr<Connection>> dead;
    juce::StringArray toConnect;
    std::vector<Connection*> toPing;
    {
        const juce::ScopedLock sl (lock);
        for (auto it = connections.begin(); it != connections.end();)
        {
            if (! (*it)->isAlive())
            {
                dead.push_back (std::move (*it));
                it = connections.erase (it);
            }
            else
            {
                ++it;
            }
        }

        auto now = juce::Time::getMillisecondCounter();
        for (auto& address : wantedPeers)
        {
            if (findLiveConnection (address) != nullptr || connecting.contains (address))
                continue;

            auto last = lastAttempt.find (address);
            if (last != lastAttempt.end() && now - last->second < kRetryIntervalMs)
                continue;

            lastAttempt[address] = now;
            connecting.add (address);
            toConnect.add (address);
        }

        for (auto& c : connections)
            toPing.push_back (c.get());
    }

    // Connections are only ever deleted on this thread, so the pointers stay
    // valid outside the lock; pinging there keeps a slow socket from holding
    // up MIDI sends.
    for (auto* c : toPing)
        c->sendPing();

    for (auto& address : toConnect)
    {
        connectPool.addJob ([this, address]
        {
            connectNow (address);
            const juce::ScopedLock sl (lock);
            connecting.removeString (address);
        });
    }

    // Give up on stream requests nobody answered.
    auto now = juce::Time::getMillisecondCounter();
    std::vector<int> expired;
    for (auto& [requestId, pending] : pendingRequests)
        if ((juce::int32) (now - pending.deadline) > 0)
            expired.push_back (requestId);
    for (auto requestId : expired)
        finishRequest (requestId, errorAnswer ("No answer from the other JamLink."));
}
