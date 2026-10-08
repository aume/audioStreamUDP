#pragma once

#include <cstdint>

// Wire format for the raw PCM audio packets sent between JamLink peers.
// Kept intentionally tiny (no codec, no handshake) to minimise latency on
// a LAN - every packet is self-contained interleaved 16-bit PCM (1-8
// channels) plus a small header. The header carries the stream's sample
// rate so the receiver can resample to whatever rate its own device runs at.
namespace jamlink
{
    constexpr uint32_t kPacketMagic = 0x4a414d32; // "JAM2"
    constexpr int kMaxChannels = 8;

    // Payload is capped so a packet fits a standard 1500-byte Ethernet/Wi-Fi
    // MTU without IP fragmentation (one lost fragment loses the whole packet).
    constexpr int kMaxPayloadBytes = 1400;

    // Port numbers JamLink accepts. Below 1024 needs root; the default sits
    // at 58001 to stay clear of SuperCollider (57110 scsynth, 57120 sclang).
    constexpr int kMinPort = 1024;
    constexpr int kMaxPort = 65535;
    constexpr int kDefaultPort = 58001;

    inline bool isValidPort (int port) noexcept { return port >= kMinPort && port <= kMaxPort; }

#if defined (_MSC_VER)
    #pragma pack(push, 1)
    struct PacketHeader
    {
        uint32_t magic;
        uint32_t sequence;
        uint32_t sampleRate;
        uint16_t numFrames;
        uint8_t numChannels;
        uint8_t reserved;
    };
    #pragma pack(pop)
#else
    struct __attribute__ ((packed)) PacketHeader
    {
        uint32_t magic;
        uint32_t sequence;
        uint32_t sampleRate;
        uint16_t numFrames;
        uint8_t numChannels;
        uint8_t reserved;
    };
#endif

    constexpr int kHeaderSize = (int) sizeof (PacketHeader);
    constexpr int kMaxPacketBytes = kHeaderSize + kMaxPayloadBytes;

    inline int maxFramesPerPacket (int numChannels) noexcept
    {
        return kMaxPayloadBytes / ((int) sizeof (int16_t) * (numChannels > 0 ? numChannels : 1));
    }
}
