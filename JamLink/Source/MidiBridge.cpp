#include "MidiBridge.h"

MidiBridge::MidiBridge (PeerLink& linkToUse) : link (linkToUse)
{
   #if JUCE_MAC || JUCE_LINUX
    virtualIn = juce::MidiInput::createNewDevice ("JamLink", this);
    virtualOut = juce::MidiOutput::createNewDevice ("JamLink");
    if (virtualIn != nullptr)
        virtualIn->start();
   #endif

    link.setMidiCallback ([this] (const juce::MidiMessage& m) { sendToLocalOutputs (m); });
}

MidiBridge::~MidiBridge()
{
    link.setMidiCallback (nullptr);
    if (virtualIn != nullptr) virtualIn->stop();
    if (hardwareIn != nullptr) hardwareIn->stop();
}

juce::MidiDeviceInfo MidiBridge::findDevice (const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& identifier)
{
    for (auto& d : devices)
        if (d.identifier == identifier)
            return d;
    return {};
}

bool MidiBridge::isOwnPort (const juce::MidiDeviceInfo& device) const
{
    return device.name == "JamLink"
        || (virtualIn != nullptr && device.identifier == virtualIn->getIdentifier())
        || (virtualOut != nullptr && device.identifier == virtualOut->getIdentifier());
}

// A device that appears as both a MIDI source and destination under the same
// name (IAC buses, network sessions, many interfaces) passes everything sent
// to it straight back, so it can only be used in one direction.
bool MidiBridge::wouldLoopAsInput (const juce::MidiDeviceInfo& device) const
{
    return isOwnPort (device) || (hardwareOutName.isNotEmpty() && device.name == hardwareOutName);
}

bool MidiBridge::wouldLoopAsOutput (const juce::MidiDeviceInfo& device) const
{
    return isOwnPort (device) || (hardwareInName.isNotEmpty() && device.name == hardwareInName);
}

juce::Array<juce::MidiDeviceInfo> MidiBridge::getSelectableInputs() const
{
    auto devices = juce::MidiInput::getAvailableDevices();
    devices.removeIf ([this] (const juce::MidiDeviceInfo& d) { return isOwnPort (d); });
    return devices;
}

juce::Array<juce::MidiDeviceInfo> MidiBridge::getSelectableOutputs() const
{
    auto devices = juce::MidiOutput::getAvailableDevices();
    devices.removeIf ([this] (const juce::MidiDeviceInfo& d) { return isOwnPort (d); });
    return devices;
}

bool MidiBridge::setHardwareInput (const juce::String& identifier)
{
    if (identifier == hardwareInId && (identifier.isEmpty() || hardwareIn != nullptr))
        return true;

    juce::MidiDeviceInfo device;
    if (identifier.isNotEmpty())
    {
        device = findDevice (juce::MidiInput::getAvailableDevices(), identifier);
        if (device.identifier.isEmpty() || wouldLoopAsInput (device))
            return false;
    }

    if (hardwareIn != nullptr)
        hardwareIn->stop();
    hardwareIn.reset();
    hardwareInId = identifier;
    hardwareInName = device.name;

    if (identifier.isNotEmpty())
        if ((hardwareIn = juce::MidiInput::openDevice (identifier, this)) != nullptr)
            hardwareIn->start();
    return true;
}

bool MidiBridge::setHardwareOutput (const juce::String& identifier)
{
    const juce::ScopedLock sl (outputLock);
    if (identifier == hardwareOutId && (identifier.isEmpty() || hardwareOut != nullptr))
        return true;

    juce::MidiDeviceInfo device;
    if (identifier.isNotEmpty())
    {
        device = findDevice (juce::MidiOutput::getAvailableDevices(), identifier);
        if (device.identifier.isEmpty() || wouldLoopAsOutput (device))
            return false;
    }

    hardwareOut.reset();
    hardwareOutId = identifier;
    hardwareOutName = device.name;
    if (identifier.isNotEmpty())
        hardwareOut = juce::MidiOutput::openDevice (identifier);
    return true;
}

juce::String MidiBridge::getHardwareInput() const  { return hardwareInId; }
juce::String MidiBridge::getHardwareOutput() const { return hardwareOutId; }

void MidiBridge::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    // Only JamLink's own input port and the chosen hardware input feed peers.
    if (source != virtualIn.get() && source != hardwareIn.get())
        return;

    // Active sensing is a local keep-alive between a keyboard and the port
    // it's plugged into; everything else goes to the peers.
    if (message.isActiveSense())
        return;
    link.sendMidi (message);
}

void MidiBridge::sendToLocalOutputs (const juce::MidiMessage& message)
{
    const juce::ScopedLock sl (outputLock);
    if (virtualOut != nullptr)
        virtualOut->sendMessageNow (message);
    if (hardwareOut != nullptr)
        hardwareOut->sendMessageNow (message);
}
