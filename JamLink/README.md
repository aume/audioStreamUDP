# JamLink

> **JamLink has moved to [github.com/aume/JamLinkAudio](https://github.com/aume/JamLinkAudio)**, which
> holds the app and its audio drivers together. This copy is no longer updated.

A low-latency UDP network audio jamming app, built with JUCE (C++). It replaces
the Python prototype in the parent folder (`AudioStreamServer.py` /
`AudioStreamClient.py`) with a single app that can act as sender, receiver, or
both at once - so it works for one-to-one, one-to-many, and many-to-many jam
sessions from one binary.

## What it does

- Pick an audio input and output device (any multi-channel interface, e.g.
  BlackHole, an audio interface, etc.) in the **Audio Device** card.
- **Send To**: add one row per destination. Each row picks a local input
  channel (Mono) or a block of adjacent channels (Stereo / 4 ch / 8 ch) and a
  peer's IP address + port. Add several rows to fan the same channel out to
  many peers, or send different channels to different peers.
- **Receive From**: add one row per UDP port you want to listen on. Each row
  routes the incoming audio to a local output channel (or block of
  channels). Several rows can target the same output channel - their audio
  is mixed - so many peers can feed into the same mix. A mono stream on a
  multichannel row is copied to every channel; a multichannel stream on a
  Mono row is mixed down.
- Audio streams as raw 16-bit PCM over UDP (no codec, so no compression
  latency). Each incoming stream has a jitter buffer (**Jitter buffer**,
  default 20 ms) that fills before playback and continuously corrects for
  clock drift between the two machines' sound cards.
- **Network rate** sets the sample rate audio is sent at (Same as device,
  32 / 44.1 / 48 / 96 kHz). Receivers resample to their own device rate, so
  peers don't need matching sample rates.
- Routing setups can be saved/loaded as JSON presets (**Save Preset** /
  **Load Preset**), and the last session (device + routing) is restored
  automatically on relaunch.

## Building

Requires the JUCE framework and CMake. This was set up against JUCE 8
installed at `/Applications/JUCE`; pass a different location with
`-DJUCE_PATH=/path/to/JUCE` if yours lives elsewhere.

```sh
cmake -B build
cmake --build build -j 8
open build/JamLink_artefacts/JamLink.app
```

On macOS, the first launch will prompt for microphone/audio-input permission
(`System Settings > Privacy & Security > Microphone`) - allow it, or audio
input devices won't be usable.

## Nearby JamLinks and peer-to-peer Wi-Fi

JamLink advertises itself with Bonjour and finds other JamLinks
automatically. Under **Send To**, **+ Nearby...** lists them:

- **Jam with X (audio both ways)** - X's player is asked to accept; then both
  JamLinks pick free ports and set up a Send and Receive row each (mono,
  In 1 / Out 1 - adjust afterwards).
- **Send to X only** - one-way, X still has to accept.

Nobody types IP addresses or ports, and nothing is sent until the other
player accepts.

**Peer-to-peer Wi-Fi** (button in the Audio Device card) lets Macs that
aren't on the same network - or on no network at all - find and reach each
other directly over Apple's peer-to-peer Wi-Fi (AWDL, the radio AirDrop
uses). No router, hotspot or admin password: switch it on on every Mac (Wi-Fi
must be on), then use **+ Nearby...**. Peers reached this way are marked
"peer-to-peer Wi-Fi" and use IPv6 link-local addresses such as
`fe80::1c2b:3aff:fe4d:5e6f%awdl0`. That radio hops channels about once a
second, so expect to need a larger jitter buffer (around 40-60 ms); leave it
off when everyone is on the same network, as it also adds latency spikes to
ordinary Wi-Fi.

## Sharing audio with other apps

Other apps - DAWs, Zoom, OBS, browsers - can send audio into JamLink and
record what peers send, through two optional audio devices:

- **JamLink Send** (an output): anything an app plays into it reaches
  JamLink, as the "JamLink Send 1-8" channels in **Send To**.
- **JamLink Return** (an input): route a **Receive From** row to the
  "JamLink Return 1-8" channels and any app can record it.

They're off by default. **Other apps > Install JamLink audio devices...** in
the Audio Device card installs them (admin password; all audio stops for a
few seconds while macOS reloads). **Use with JamLink** switches them in and
out of JamLink's routing; **Remove** uninstalls them - use it before
deleting JamLink, as the devices live in `/Library/Audio/Plug-Ins/HAL`, not
in the app.

While they're in use JamLink runs on a private combined device (your
interface plus the two devices, drift-corrected by CoreAudio) that only
JamLink can see; the Input/Output menus still show your own hardware.
Choosing **None - JamLink Send only** (Input) or **None - JamLink Return
only** (Output) uses just the JamLink device on that side - e.g. to stream
only what a DAW plays, with no microphone - and switches sharing on.

The devices are built from [BlackHole](https://github.com/ExistentialAudio/BlackHole)
by Existential Audio (GPL-3.0); the source and build settings are at
[github.com/aume/JamLinkAudio](https://github.com/aume/JamLinkAudio/tree/jamlink) (branch `jamlink`). They're not BlackHole
and aren't made or endorsed by Existential Audio.

## MIDI and delay sync

JamLink keeps a TCP control link (port 58000) open to every peer it sends
audio to or receives audio from, alongside the UDP audio. Over it:

- **MIDI**: JamLink shows up in DAWs and other apps as a MIDI device called
  "JamLink". MIDI sent *to* the JamLink port goes to every Send row with
  **MIDI** switched on; MIDI from peers comes *out of* the JamLink port. A
  hardware MIDI input/output (e.g. a keyboard) can also be picked in the
  Audio Device card. MIDI goes over TCP so no note-off is ever lost. To stop
  MIDI looping between machines, JamLink never offers its own port in those
  menus and won't use the same device (e.g. an IAC bus) as both input and
  output; MIDI received from peers is never sent back out to peers.
- **Delay display**: Send rows show the round-trip time to that peer. Receive
  rows show the total delay of that stream (network one-way + jitter
  buffer), with a breakdown in the tooltip. "no link" means JamLink isn't
  running at that address or TCP 58000 is blocked.
- **Align streams** (Receive From panel): delays the faster incoming streams
  so every stream reaches your speakers with the same total delay as the
  slowest one. Changes are eased in over a few seconds rather than jumping.

## Networking notes

- Destination addresses must be numeric IPv4 or IPv6 addresses (not
  hostnames); they're parsed once when edited, never on the audio thread.
- Every peer sending to you needs a distinct UDP port; every port you send
  from your own machine should be unique per destination if you want
  independent enable/disable control (you can reuse a port for multiple
  destinations at the same source channel, e.g. broadcasting to a whole
  band).
- Ports default to 58001 upwards (valid range 1024-65535), clear of
  SuperCollider's 57110/57120. A red dot next to a receive port means the
  port is taken by another route or app; JamLink rebinds it automatically
  once it frees up.
- Packets are kept under 1500 bytes so they're never IP-fragmented.
- The wire format is "JAM2" - not compatible with builds older than the
  multichannel/resampling update, so update every machine in the session.
- The IP address shown in the Audio Device card follows network changes.
- On Wi-Fi, macOS's AirDrop/AWDL radio causes periodic latency spikes. If you
  still hear regular dropouts, raise **Jitter buffer**, or test with AWDL off
  (`sudo ifconfig awdl0 down`; it comes back on reboot).
- Make sure your firewall allows inbound UDP on the ports you configure in
  **Receive From**, and inbound TCP on 58000 for MIDI / delay sync.

## Licence

JamLink is free software under the GNU General Public License v3.0 (see
`LICENSE`), as required for the BlackHole-based audio devices. It's built
with JUCE, used under JUCE's AGPLv3 licence option.

## Source layout

- `AudioEngine` - the real-time audio callback: reads local input channels,
  sends UDP packets, receives UDP packets into per-route ring buffers, and
  mixes them into output channels.
- `AudioRingBuffer` - the lock-free single-producer/single-consumer jitter
  buffer used per incoming route.
- `UdpReceiver` - one thread + bound socket per listening port.
- `RoutingModels` - the `OutputRoute` / `InputRoute` data + JSON (de)serialisation.
- `NetAddress` - dual-stack (IPv4/IPv6, incl. peer-to-peer Wi-Fi) sockets.
- `PeerLink` - the TCP control link (delays, MIDI, stream negotiation).
- `NearbyPeers`, `NearbyJam` - Bonjour discovery and the "+ Nearby..." flow.
- `MidiBridge` - the virtual "JamLink" MIDI ports.
- `VirtualDevices` - installs the JamLink Send/Return drivers and builds the
  private combined device.
- `MainComponent`, `DeviceCard`, `OutputsPanel`, `InputsPanel`,
  `LookAndFeelModern` - the UI.
