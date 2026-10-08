#pragma once

#include <JuceHeader.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>

// Dual-stack (IPv4 + IPv6) socket helpers.
//
// JUCE's sockets are IPv4-only, but peer-to-peer Wi-Fi (Apple's AWDL, the
// radio AirDrop uses) only carries IPv6 link-local addresses such as
// "fe80::1c2b:3aff:fe4d:5e6f%awdl0". Every JamLink socket is therefore an
// IPv6 socket that also accepts IPv4 (as ::ffff:a.b.c.d), so LAN peers and
// peer-to-peer peers go through exactly the same code.
namespace jamlink::net
{
    // Lets a socket send/receive on "restricted" interfaces such as awdl0.
    // Not in the public SDK headers; this value is Apple's documented one for
    // BSD sockets used with Bonjour peer-to-peer.
   #ifndef SO_RECV_ANYIF
    constexpr int SO_RECV_ANYIF = 0x1104;
   #endif

    struct Address
    {
        sockaddr_in6 sa {};
        bool valid = false;

        const sockaddr* get() const noexcept { return reinterpret_cast<const sockaddr*> (&sa); }
        socklen_t size() const noexcept       { return (socklen_t) sizeof (sa); }
    };

    // Parses a numeric IPv4 or IPv6 address (with optional %interface scope)
    // into a dual-stack socket address. No DNS lookups: safe to call
    // anywhere except the audio thread.
    inline Address parse (const juce::String& host, int port)
    {
        Address result;
        addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_NUMERICHOST;

        addrinfo* info = nullptr;
        if (host.isEmpty() || getaddrinfo (host.trim().toRawUTF8(), nullptr, &hints, &info) != 0 || info == nullptr)
            return result;

        if (info->ai_family == AF_INET6)
        {
            std::memcpy (&result.sa, info->ai_addr, sizeof (sockaddr_in6));
            result.valid = true;
        }
        else if (info->ai_family == AF_INET)
        {
            // IPv4 as an IPv4-mapped IPv6 address (::ffff:a.b.c.d).
            auto* v4 = reinterpret_cast<const sockaddr_in*> (info->ai_addr);
            result.sa.sin6_family = AF_INET6;
            result.sa.sin6_len = sizeof (sockaddr_in6);
            result.sa.sin6_addr.s6_addr[10] = 0xff;
            result.sa.sin6_addr.s6_addr[11] = 0xff;
            std::memcpy (&result.sa.sin6_addr.s6_addr[12], &v4->sin_addr, 4);
            result.valid = true;
        }

        freeaddrinfo (info);
        result.sa.sin6_port = htons ((uint16_t) port);
        return result;
    }

    // Numeric text for a socket address. IPv4-mapped addresses come out as
    // plain IPv4 and link-local IPv6 keeps its %interface, so the same peer
    // always produces the same string whichever socket saw it.
    inline juce::String toString (const sockaddr* sa)
    {
        char buffer[INET6_ADDRSTRLEN + IF_NAMESIZE + 2] = {};

        if (sa->sa_family == AF_INET)
        {
            inet_ntop (AF_INET, &reinterpret_cast<const sockaddr_in*> (sa)->sin_addr, buffer, sizeof (buffer));
            return buffer;
        }

        if (sa->sa_family == AF_INET6)
        {
            auto* v6 = reinterpret_cast<const sockaddr_in6*> (sa);
            if (IN6_IS_ADDR_V4MAPPED (&v6->sin6_addr))
            {
                inet_ntop (AF_INET, &v6->sin6_addr.s6_addr[12], buffer, sizeof (buffer));
                return buffer;
            }

            if (getnameinfo (sa, sizeof (sockaddr_in6), buffer, sizeof (buffer), nullptr, 0, NI_NUMERICHOST) == 0)
                return buffer;
        }

        return {};
    }

    inline bool isNumericAddress (const juce::String& host)  { return parse (host, 0).valid; }

    // True for addresses on Apple's peer-to-peer Wi-Fi interfaces.
    inline bool isPeerToPeer (const juce::String& host)
    {
        return host.contains ("%awdl") || host.contains ("%llw");
    }

    // A dual-stack socket that can also use peer-to-peer interfaces.
    inline int makeSocket (int type)
    {
        int fd = ::socket (AF_INET6, type, 0);
        if (fd < 0)
            return -1;

        int off = 0, on = 1;
        setsockopt (fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof (off));
        setsockopt (fd, SOL_SOCKET, SO_RECV_ANYIF, &on, sizeof (on));
        setsockopt (fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof (on));
        if (type == SOCK_STREAM)
            setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof (on));
        return fd;
    }

    inline bool bindAny (int fd, int port)
    {
        sockaddr_in6 sa {};
        sa.sin6_family = AF_INET6;
        sa.sin6_len = sizeof (sa);
        sa.sin6_addr = in6addr_any;
        sa.sin6_port = htons ((uint16_t) port);
        return ::bind (fd, reinterpret_cast<const sockaddr*> (&sa), sizeof (sa)) == 0;
    }

    // Waits up to timeoutMs for fd to become readable: 1 = ready, 0 = timeout, -1 = error.
    inline int waitReadable (int fd, int timeoutMs)
    {
        pollfd p { fd, POLLIN, 0 };
        auto r = ::poll (&p, 1, timeoutMs);
        if (r > 0)
            return (p.revents & (POLLERR | POLLNVAL)) != 0 ? -1 : 1;
        return r;
    }
}

// One dual-stack UDP socket, used both to receive (bound to a port) and to
// send (unbound, OS-assigned source port).
class UdpSocket
{
public:
    UdpSocket() : fd (jamlink::net::makeSocket (SOCK_DGRAM)) {}
    ~UdpSocket()                     { close(); }

    bool bind (int port)             { return fd >= 0 && jamlink::net::bindAny (fd, port); }
    void close()                     { if (fd >= 0) { ::close (fd); fd = -1; } }

    // Never blocks, so it's safe on the audio thread.
    int sendTo (const void* data, int numBytes, const jamlink::net::Address& to) noexcept
    {
        if (fd < 0 || ! to.valid)
            return -1;
        return (int) ::sendto (fd, data, (size_t) numBytes, MSG_DONTWAIT, to.get(), to.size());
    }

    // Waits up to timeoutMs for a datagram. Returns its size, 0 on timeout,
    // -1 on error; senderIp is set to the sender's address.
    int receive (void* buffer, int bufferSize, int timeoutMs, juce::String& senderIp)
    {
        if (fd < 0)
            return -1;

        auto ready = jamlink::net::waitReadable (fd, timeoutMs);
        if (ready <= 0)
            return ready;

        sockaddr_storage from {};
        socklen_t fromLength = sizeof (from);
        auto n = (int) ::recvfrom (fd, buffer, (size_t) bufferSize, 0, reinterpret_cast<sockaddr*> (&from), &fromLength);
        if (n > 0)
            senderIp = jamlink::net::toString (reinterpret_cast<const sockaddr*> (&from));
        return n < 0 ? -1 : n;
    }

private:
    int fd = -1;

    JUCE_DECLARE_NON_COPYABLE (UdpSocket)
};
