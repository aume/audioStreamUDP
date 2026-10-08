#pragma once

#include <JuceHeader.h>
#include "PacketFormat.h"

// Channel-count choices offered for a route: mono, or a block of N adjacent
// channels sent together in one stream.
inline const juce::Array<int>& routeChannelCountChoices()
{
    static const juce::Array<int> choices { 1, 2, 4, 8 };
    return choices;
}

inline juce::String channelCountName (int numChannels)
{
    return numChannels == 1 ? "Mono" : numChannels == 2 ? "Stereo" : juce::String (numChannels) + " ch";
}

// Menu text for `width` channels starting at `start`, from per-channel labels
// such as "In 3" or "JamLink Send 1": "In 3", "JamLink Send 1-2", or
// "In 2 - JamLink Send 1" when the block spans two kinds of channel.
inline juce::String channelRangeName (const juce::StringArray& labels, int start, int width)
{
    auto label = [&] (int i) { return juce::isPositiveAndBelow (i, labels.size()) ? labels[i] : "Ch " + juce::String (i + 1); };
    auto first = label (start);
    if (width <= 1)
        return first;

    auto last = label (start + width - 1);
    auto prefix = first.upToLastOccurrenceOf (" ", false, false);
    if (prefix == last.upToLastOccurrenceOf (" ", false, false))
        return first + "-" + last.fromLastOccurrenceOf (" ", false, false);
    return first + " - " + last;
}

inline juce::StringArray numberedLabels (const juce::String& prefix, int count)
{
    juce::StringArray labels;
    for (int i = 0; i < count; ++i)
        labels.add (prefix + " " + juce::String (i + 1));
    return labels;
}

inline int sanitiseChannelCount (int n)
{
    return routeChannelCountChoices().contains (n) ? n : 1;
}

// One outgoing stream: a block of local input channels (starting at
// sourceChannel) sent to a peer's IP:port. Several OutputRoutes can share
// the same source channel (fan-out to many peers) or send different
// channels to different peers.
struct OutputRoute
{
    juce::Uuid id;
    juce::String label { "Peer" };
    int sourceChannel = 0;
    int numChannels = 1;
    juce::String destHost { "127.0.0.1" };
    int destPort = jamlink::kDefaultPort;
    bool enabled = true;
    bool midi = false;   // also send MIDI to this peer (over the TCP control link)

    juce::var toVar() const
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("id", id.toString());
        o->setProperty ("label", label);
        o->setProperty ("sourceChannel", sourceChannel);
        o->setProperty ("numChannels", numChannels);
        o->setProperty ("destHost", destHost);
        o->setProperty ("destPort", destPort);
        o->setProperty ("enabled", enabled);
        o->setProperty ("midi", midi);
        return juce::var (o.release());
    }

    static OutputRoute fromVar (const juce::var& v)
    {
        OutputRoute r;
        if (auto* o = v.getDynamicObject())
        {
            r.id = juce::Uuid (o->getProperty ("id").toString());
            r.label = o->getProperty ("label").toString();
            r.sourceChannel = (int) o->getProperty ("sourceChannel");
            r.numChannels = sanitiseChannelCount ((int) o->getProperty ("numChannels"));
            r.destHost = o->getProperty ("destHost").toString();
            r.destPort = (int) o->getProperty ("destPort");
            r.enabled = (bool) o->getProperty ("enabled");
            r.midi = (bool) o->getProperty ("midi");
        }
        return r;
    }
};

// One incoming stream: a UDP listen port routed to a block of local output
// channels (starting at destChannel). Several InputRoutes can target the same
// output channel - their audio is mixed together - which is what makes
// many-to-many jams possible. A mono stream received on a multichannel route
// is copied to every channel; a multichannel stream received on a mono route
// is mixed down.
struct InputRoute
{
    juce::Uuid id;
    juce::String label { "Peer" };
    int listenPort = jamlink::kDefaultPort;
    int destChannel = 0;
    int numChannels = 1;
    bool enabled = true;

    juce::var toVar() const
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("id", id.toString());
        o->setProperty ("label", label);
        o->setProperty ("listenPort", listenPort);
        o->setProperty ("destChannel", destChannel);
        o->setProperty ("numChannels", numChannels);
        o->setProperty ("enabled", enabled);
        return juce::var (o.release());
    }

    static InputRoute fromVar (const juce::var& v)
    {
        InputRoute r;
        if (auto* o = v.getDynamicObject())
        {
            r.id = juce::Uuid (o->getProperty ("id").toString());
            r.label = o->getProperty ("label").toString();
            r.listenPort = (int) o->getProperty ("listenPort");
            r.destChannel = (int) o->getProperty ("destChannel");
            r.numChannels = sanitiseChannelCount ((int) o->getProperty ("numChannels"));
            r.enabled = (bool) o->getProperty ("enabled");
        }
        return r;
    }
};
