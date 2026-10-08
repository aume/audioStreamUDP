#include "NearbyPeers.h"
#include "NetAddress.h"
#include "PeerLink.h"
#include <dns_sd.h>
#include <dispatch/dispatch.h>
#include <map>
#include <string>

namespace
{
    const char* const kServiceType = "_jamlink._tcp";

    bool isPeerToPeerInterface (uint32_t interfaceIndex)
    {
        char name[IF_NAMESIZE] = {};
        if (if_indextoname (interfaceIndex, name) == nullptr)
            return false;
        return std::strncmp (name, "awdl", 4) == 0 || std::strncmp (name, "llw", 3) == 0;
    }
}

// All DNS-SD work happens on one serial dispatch queue: every callback runs
// there, and refs are created and deallocated there.
struct NearbyPeers::Impl
{
    struct Instance
    {
        Impl* owner = nullptr;
        std::string serviceName;
        uint32_t interfaceIndex = 0;
        DNSServiceRef resolveRef = nullptr;
        DNSServiceRef addressRef = nullptr;
        bool isSelf = false;
        juce::String address;

        ~Instance()
        {
            if (addressRef != nullptr) DNSServiceRefDeallocate (addressRef);
            if (resolveRef != nullptr) DNSServiceRefDeallocate (resolveRef);
        }
    };

    Impl (NearbyPeers& ownerToUse, bool shouldAdvertise) : owner (ownerToUse), advertise (shouldAdvertise)
    {
        queue = dispatch_queue_create ("com.spiral.jamlink.bonjour", DISPATCH_QUEUE_SERIAL);
    }

    ~Impl()
    {
        runOnQueue ([this] { stop(); });
        dispatch_release (queue);
    }

    // Runs fn on the queue and waits for it.
    void runOnQueue (std::function<void()> fn)
    {
        dispatch_sync_f (queue, &fn, [] (void* context) { (*static_cast<std::function<void()>*> (context)) (); });
    }

    DNSServiceFlags flags() const
    {
        return p2p ? (DNSServiceFlags) (kDNSServiceFlagsIncludeP2P | kDNSServiceFlagsIncludeAWDL) : 0;
    }

    void start (bool enableP2P)
    {
        p2p = enableP2P;
        juce::String newError;

        TXTRecordRef txt;
        TXTRecordCreate (&txt, 0, nullptr);
        auto id = instanceId.toStdString();
        TXTRecordSetValue (&txt, "id", (uint8_t) id.size(), id.data());

        DNSServiceErrorType err = kDNSServiceErr_NoError;
        if (advertise)
            err = DNSServiceRegister (&registerRef, flags(), kDNSServiceInterfaceIndexAny, nullptr, kServiceType,
                                       nullptr, nullptr, htons ((uint16_t) PeerLink::kControlPort),
                                       TXTRecordGetLength (&txt), TXTRecordGetBytesPtr (&txt), nullptr, nullptr);
        TXTRecordDeallocate (&txt);

        if (registerRef != nullptr && err == kDNSServiceErr_NoError)
            DNSServiceSetDispatchQueue (registerRef, queue);
        else if (advertise)
        {
            registerRef = nullptr;
            newError = "Couldn't advertise this JamLink (Bonjour error " + juce::String ((int) err) + ")";
        }

        err = DNSServiceBrowse (&browseRef, flags(), kDNSServiceInterfaceIndexAny, kServiceType, nullptr, &Impl::browsed, this);
        if (err == kDNSServiceErr_NoError)
            DNSServiceSetDispatchQueue (browseRef, queue);
        else
        {
            browseRef = nullptr;
            newError = "Couldn't look for nearby JamLinks (Bonjour error " + juce::String ((int) err) + ")";
        }

        const juce::ScopedLock sl (lock);
        error = newError;
    }

    void stop()
    {
        instances.clear();
        if (browseRef != nullptr)   { DNSServiceRefDeallocate (browseRef); browseRef = nullptr; }
        if (registerRef != nullptr) { DNSServiceRefDeallocate (registerRef); registerRef = nullptr; }
        publish();
    }

    static void DNSSD_API browsed (DNSServiceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType err,
                                   const char* serviceName, const char* regType, const char* domain, void* context)
    {
        auto* self = static_cast<Impl*> (context);
        if (err != kDNSServiceErr_NoError)
            return;

        auto key = std::string (serviceName) + "#" + std::to_string (interfaceIndex);

        if ((flags & kDNSServiceFlagsAdd) == 0)
        {
            self->instances.erase (key);
            self->publish();
            return;
        }

        if (self->instances.count (key) != 0)
            return;

        auto instance = std::make_unique<Instance>();
        instance->owner = self;
        instance->serviceName = serviceName;
        instance->interfaceIndex = interfaceIndex;

        if (DNSServiceResolve (&instance->resolveRef, self->flags(), interfaceIndex, serviceName, regType, domain,
                               &Impl::resolved, instance.get()) == kDNSServiceErr_NoError)
            DNSServiceSetDispatchQueue (instance->resolveRef, self->queue);
        else
            instance->resolveRef = nullptr;

        self->instances[key] = std::move (instance);
    }

    static void DNSSD_API resolved (DNSServiceRef, DNSServiceFlags, uint32_t interfaceIndex, DNSServiceErrorType err,
                                    const char*, const char* hostTarget, uint16_t, uint16_t txtLength,
                                    const unsigned char* txtRecord, void* context)
    {
        auto* instance = static_cast<Instance*> (context);
        auto* self = instance->owner;
        if (err != kDNSServiceErr_NoError || instance->addressRef != nullptr)
            return;

        uint8_t idLength = 0;
        if (auto* id = TXTRecordGetValuePtr (txtLength, txtRecord, "id", &idLength))
        {
            if (juce::String::fromUTF8 (static_cast<const char*> (id), idLength) == self->instanceId)
            {
                instance->isSelf = true;
                return;
            }
        }

        if (DNSServiceGetAddrInfo (&instance->addressRef, self->flags(), interfaceIndex,
                                   kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6, hostTarget,
                                   &Impl::gotAddress, instance) == kDNSServiceErr_NoError)
            DNSServiceSetDispatchQueue (instance->addressRef, self->queue);
        else
            instance->addressRef = nullptr;
    }

    static void DNSSD_API gotAddress (DNSServiceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType err,
                                      const char*, const sockaddr* address, uint32_t, void* context)
    {
        auto* instance = static_cast<Instance*> (context);
        if (err != kDNSServiceErr_NoError || address == nullptr || (flags & kDNSServiceFlagsAdd) == 0)
            return;

        juce::String text;
        bool isIPv4 = address->sa_family == AF_INET;

        if (address->sa_family == AF_INET6)
        {
            // Link-local IPv6 is only usable with its interface attached.
            auto v6 = *reinterpret_cast<const sockaddr_in6*> (address);
            if (IN6_IS_ADDR_LINKLOCAL (&v6.sin6_addr) && v6.sin6_scope_id == 0)
                v6.sin6_scope_id = interfaceIndex;
            text = jamlink::net::toString (reinterpret_cast<const sockaddr*> (&v6));
        }
        else if (isIPv4)
        {
            text = jamlink::net::toString (address);
        }

        if (text.isEmpty())
            return;

        // On peer-to-peer Wi-Fi only IPv6 works; on a LAN prefer IPv4.
        bool p2p = isPeerToPeerInterface (interfaceIndex);
        if (p2p && isIPv4)
            return;
        if (! p2p && instance->address.isNotEmpty() && ! isIPv4)
            return;

        instance->address = text;
        instance->owner->publish();
    }

    // Queue thread. One entry per Mac: a LAN route is preferred over
    // peer-to-peer when both are available, as it has lower latency.
    void publish()
    {
        std::map<std::string, Peer> byName;
        for (auto& [key, instance] : instances)
        {
            if (instance->isSelf || instance->address.isEmpty())
                continue;

            Peer peer { juce::String::fromUTF8 (instance->serviceName.c_str()), instance->address,
                        isPeerToPeerInterface (instance->interfaceIndex) };

            auto existing = byName.find (instance->serviceName);
            if (existing == byName.end() || (existing->second.peerToPeer && ! peer.peerToPeer))
                byName[instance->serviceName] = peer;
        }

        std::vector<Peer> list;
        for (auto& [name, peer] : byName)
            list.push_back (peer);

        {
            const juce::ScopedLock sl (lock);
            peers = std::move (list);
        }
        owner.sendChangeMessage();
    }

    NearbyPeers& owner;
    const bool advertise;
    dispatch_queue_t queue;
    const juce::String instanceId = juce::Uuid().toString();
    bool p2p = false;
    DNSServiceRef registerRef = nullptr, browseRef = nullptr;
    std::map<std::string, std::unique_ptr<Instance>> instances;  // queue only

    mutable juce::CriticalSection lock;
    std::vector<Peer> peers;
    juce::String error;
};

NearbyPeers::NearbyPeers (bool advertise) : impl (std::make_unique<Impl> (*this, advertise))
{
    impl->runOnQueue ([this] { impl->start (false); });
}

NearbyPeers::~NearbyPeers()
{
    impl.reset();
    removeAllChangeListeners();
}

void NearbyPeers::setPeerToPeer (bool enabled)
{
    if (enabled == peerToPeer)
        return;

    peerToPeer = enabled;
    impl->runOnQueue ([this, enabled]
    {
        impl->stop();
        impl->start (enabled);
    });
}

std::vector<NearbyPeers::Peer> NearbyPeers::getPeers() const
{
    const juce::ScopedLock sl (impl->lock);
    return impl->peers;
}

juce::String NearbyPeers::getError() const
{
    const juce::ScopedLock sl (impl->lock);
    return impl->error;
}
