#include "NearbyJam.h"

NearbyJam::NearbyJam (AudioEngine& engineToUse, PeerLink& linkToUse, NearbyPeers& nearbyToUse,
                      std::function<void()> onRoutesChanged)
    : engine (engineToUse), link (linkToUse), nearby (nearbyToUse), routesChanged (std::move (onRoutesChanged))
{
    if (link.isListening())
        link.setStreamRequestHandler ([this] (const juce::String& from, const juce::String& name, const juce::var& request,
                                              std::function<void (const juce::var&)> respond)
        {
            handleStreamRequest (from, name, request, std::move (respond));
        });
}

NearbyJam::~NearbyJam()
{
    link.setStreamRequestHandler (nullptr);
}

void NearbyJam::showNearbyMenu (juce::Component& button)
{
    auto peers = nearby.getPeers();
    juce::PopupMenu menu;

    if (auto error = nearby.getError(); error.isNotEmpty())
        menu.addItem (-1, error, false);

    if (peers.empty())
    {
        menu.addItem (-1, "No JamLinks found nearby", false);
        menu.addItem (-1, nearby.isPeerToPeer()
                            ? "Is JamLink open on the other Mac, with Peer-to-peer Wi-Fi on there too?"
                            : "Not on the same network? Switch on Peer-to-peer Wi-Fi on both Macs.",
                      false);
    }

    for (size_t i = 0; i < peers.size(); ++i)
    {
        menu.addSectionHeader (peers[i].name + (peers[i].peerToPeer ? "   (peer-to-peer Wi-Fi)" : "   (local network)"));
        menu.addItem ((int) i * 2 + 1, "Jam with " + peers[i].name + " (audio both ways)");
        menu.addItem ((int) i * 2 + 2, "Send to " + peers[i].name + " only");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button),
                        [this, peers, alive = std::weak_ptr<bool> (aliveToken)] (int result)
    {
        if (alive.expired() || result <= 0 || (size_t) (result - 1) / 2 >= peers.size())
            return;
        startJam (peers[(size_t) (result - 1) / 2], (result - 1) % 2 == 0);
    });
}

// Request fields: name (our Mac's name), channels, returnPort (a port we've
// opened for them to send back to, or 0). Answer: ok, port (the port they
// opened for us) or error.
void NearbyJam::startJam (const NearbyPeers::Peer& peer, bool bothWays)
{
    constexpr int numChannels = 1;
    juce::Uuid returnRouteId;
    int returnPort = 0;

    if (bothWays)
    {
        InputRoute in;
        in.id = returnRouteId;
        in.label = peer.name;
        in.listenPort = engine.findFreeListenPort();
        in.numChannels = numChannels;
        engine.addInputRoute (in);
        returnPort = in.listenPort;
        routesChanged();
    }

    auto request = std::make_unique<juce::DynamicObject>();
    request->setProperty ("name", juce::SystemStats::getComputerName());
    request->setProperty ("channels", numChannels);
    request->setProperty ("returnPort", returnPort);

    jamStatus = "Waiting for " + peer.name + " to accept...";

    link.requestStream (peer.address, juce::var (request.release()),
                            [this, peer, bothWays, returnRouteId] (const juce::var& answer)
    {
        jamStatus.clear();
        int port = answer.getProperty ("port", 0);

        if (! (bool) answer.getProperty ("ok", false) || ! jamlink::isValidPort (port))
        {
            if (bothWays)
                engine.removeInputRoute (returnRouteId);
            routesChanged();

            auto reason = answer.getProperty ("error", "The other JamLink declined.").toString();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't connect to " + peer.name, reason);
            return;
        }

        OutputRoute out;
        out.id = juce::Uuid();
        out.label = peer.name;
        out.destHost = peer.address;
        out.destPort = port;
        out.numChannels = numChannels;
        engine.addOutputRoute (out);
        routesChanged();
    });
}

void NearbyJam::handleStreamRequest (const juce::String& fromAddress, const juce::String& fromName,
                                         const juce::var& request, std::function<void (const juce::var&)> respond)
{
    auto name = request.getProperty ("name", {}).toString();
    if (name.isEmpty())
        name = fromName.isNotEmpty() ? fromName : fromAddress;

    auto numChannels = sanitiseChannelCount ((int) request.getProperty ("channels", 1));
    int returnPort = request.getProperty ("returnPort", 0);
    bool bothWays = jamlink::isValidPort (returnPort);

    // Never start sending someone our audio without asking.
    auto message = bothWays ? name + " wants to jam: they'll send you their audio, and your input "
                                     "(In 1) will be sent back to them."
                            : name + " wants to send you their audio.";

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Jam request from " + name)
                       .withMessage (message)
                       .withButton ("Accept")
                       .withButton ("Decline");

    juce::AlertWindow::showAsync (options, [this, alive = std::weak_ptr<bool> (aliveToken),
                                            name, fromAddress, numChannels, returnPort, bothWays, respond] (int result)
    {
        if (alive.expired())
            return; // JamLink is shutting down

        auto reply = std::make_unique<juce::DynamicObject>();

        if (result != 1)
        {
            reply->setProperty ("ok", false);
            reply->setProperty ("error", "The other player declined.");
            respond (juce::var (reply.release()));
            return;
        }

        InputRoute in;
        in.id = juce::Uuid();
        in.label = name;
        in.listenPort = engine.findFreeListenPort();
        in.numChannels = numChannels;
        engine.addInputRoute (in);

        if (bothWays)
        {
            OutputRoute out;
            out.id = juce::Uuid();
            out.label = name;
            out.destHost = fromAddress;
            out.destPort = returnPort;
            out.numChannels = numChannels;
            engine.addOutputRoute (out);
        }

        routesChanged();

        reply->setProperty ("ok", true);
        reply->setProperty ("port", in.listenPort);
        respond (juce::var (reply.release()));
    });
}
