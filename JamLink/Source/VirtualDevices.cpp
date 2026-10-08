// CoreAudio first: JuceHeader.h brings in `using namespace juce`, whose
// Point / AudioBuffer would clash with CoreAudio's.
#if defined (__APPLE__)
 #include <CoreAudio/CoreAudio.h>
 #include <CoreFoundation/CoreFoundation.h>
 #include <unistd.h>
#endif

#include "VirtualDevices.h"

namespace
{
    const juce::StringArray driverNames { "JamLinkSend", "JamLinkReturn" };
    const char* const halFolder = "/Library/Audio/Plug-Ins/HAL";

    // UIDs of the hidden "for JamLink" sides of each driver (see the
    // JamLinkAudio repository's JamLink/*.h settings).
    const char* const sendHiddenUid = "JamLinkSend_2_UID";
    const char* const returnHiddenUid = "JamLinkReturn_2_UID";

    juce::File bundledDriversFolder()
    {
        return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                   .getChildFile ("Contents/Resources/Drivers");
    }

    juce::File installedDriver (const juce::String& name)
    {
        return juce::File (halFolder).getChildFile (name + ".driver");
    }

    juce::String shellQuote (const juce::String& s)
    {
        return "'" + s.replace ("'", "'\\''") + "'";
    }

   #if JUCE_MAC
    // kAudioObjectPropertyElementMain is only declared for macOS 12+; it's 0.
    constexpr AudioObjectPropertyElement elementMain = 0;

    // Read straight from the plist rather than through CFBundle, which caches
    // bundles and would keep reporting the version from before an update.
    int bundleBuildNumber (const juce::File& bundle)
    {
        juce::MemoryBlock data;
        if (! bundle.getChildFile ("Contents/Info.plist").loadFileAsData (data))
            return 0;

        auto cfData = CFDataCreate (nullptr, static_cast<const UInt8*> (data.getData()), (CFIndex) data.getSize());
        auto plist = CFPropertyListCreateWithData (nullptr, cfData, kCFPropertyListImmutable, nullptr, nullptr);
        CFRelease (cfData);

        int result = 0;
        if (plist != nullptr && CFGetTypeID (plist) == CFDictionaryGetTypeID())
            if (auto value = CFDictionaryGetValue ((CFDictionaryRef) plist, CFSTR ("CFBundleVersion")))
                if (CFGetTypeID (value) == CFStringGetTypeID())
                    result = juce::String::fromCFString ((CFStringRef) value).getIntValue();

        if (plist != nullptr)
            CFRelease (plist);
        return result;
    }

    AudioObjectID deviceForUid (const juce::String& uid)
    {
        auto cfUid = uid.toCFString();
        AudioObjectID device = kAudioObjectUnknown;
        UInt32 size = sizeof (device);
        AudioObjectPropertyAddress address { kAudioHardwarePropertyTranslateUIDToDevice,
                                             kAudioObjectPropertyScopeGlobal, elementMain };
        AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, sizeof (cfUid), &cfUid, &size, &device);
        CFRelease (cfUid);
        return device;
    }

    juce::String stringProperty (AudioObjectID object, AudioObjectPropertySelector selector)
    {
        CFStringRef value = nullptr;
        UInt32 size = sizeof (value);
        AudioObjectPropertyAddress address { selector, kAudioObjectPropertyScopeGlobal, elementMain };
        if (AudioObjectGetPropertyData (object, &address, 0, nullptr, &size, &value) != noErr || value == nullptr)
            return {};
        auto result = juce::String::fromCFString (value);
        CFRelease (value);
        return result;
    }

    std::vector<AudioObjectID> allDevices()
    {
        AudioObjectPropertyAddress address { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, elementMain };
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr)
            return {};
        std::vector<AudioObjectID> devices (size / sizeof (AudioObjectID));
        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr)
            return {};
        return devices;
    }

    AudioObjectID defaultDevice (bool input)
    {
        AudioObjectID device = kAudioObjectUnknown;
        UInt32 size = sizeof (device);
        AudioObjectPropertyAddress address { input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
                                             kAudioObjectPropertyScopeGlobal, elementMain };
        AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, &size, &device);
        return device;
    }

    void setDefaultDevice (bool input, AudioObjectID device)
    {
        AudioObjectPropertyAddress address { input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
                                             kAudioObjectPropertyScopeGlobal, elementMain };
        AudioObjectSetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, sizeof (device), &device);
    }

    // UID of the device JUCE lists under `name` (JUCE uses the same property).
    juce::String uidForDeviceName (const juce::String& name)
    {
        if (name.isEmpty())
            return {};

        for (auto device : allDevices())
            if (stringProperty (device, kAudioDevicePropertyDeviceNameCFString) == name)
                return stringProperty (device, kAudioDevicePropertyDeviceUID);
        return {};
    }

    int channelCount (AudioObjectID device, bool input)
    {
        AudioObjectPropertyAddress address { kAudioDevicePropertyStreamConfiguration,
                                             input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput,
                                             elementMain };
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (device, &address, 0, nullptr, &size) != noErr || size == 0)
            return 0;

        juce::HeapBlock<char> storage (size);
        auto* list = reinterpret_cast<AudioBufferList*> (storage.get());
        if (AudioObjectGetPropertyData (device, &address, 0, nullptr, &size, list) != noErr)
            return 0;

        int total = 0;
        for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
            total += (int) list->mBuffers[i].mNumberChannels;
        return total;
    }
   #endif
}

bool VirtualDevices::isJamLinkDevice (const juce::String& name)
{
    return name.startsWith ("JamLink Send") || name.startsWith ("JamLink Return") || name == aggregateName;
}

juce::String VirtualDevices::fallbackHardware (bool input)
{
   #if JUCE_MAC
    auto usable = [input] (AudioObjectID device)
    {
        auto name = stringProperty (device, kAudioDevicePropertyDeviceNameCFString);
        return device != kAudioObjectUnknown && name.isNotEmpty() && ! isJamLinkDevice (name) && channelCount (device, input) > 0;
    };

    if (auto device = defaultDevice (input); usable (device))
        return stringProperty (device, kAudioDevicePropertyDeviceNameCFString);

    for (auto device : allDevices())
        if (usable (device))
            return stringProperty (device, kAudioDevicePropertyDeviceNameCFString);
   #else
    juce::ignoreUnused (input);
   #endif
    return {};
}

void VirtualDevices::rememberSystemDefaults()
{
   #if JUCE_MAC
    savedDefaultInputUid = stringProperty (defaultDevice (true), kAudioDevicePropertyDeviceUID);
    savedDefaultOutputUid = stringProperty (defaultDevice (false), kAudioDevicePropertyDeviceUID);
   #endif
}

void VirtualDevices::restoreSystemDefaultsIfTakenOver()
{
   #if JUCE_MAC
    for (bool input : { true, false })
    {
        auto current = defaultDevice (input);
        if (! isJamLinkDevice (stringProperty (current, kAudioDevicePropertyDeviceNameCFString)))
            continue;

        auto saved = deviceForUid (input ? savedDefaultInputUid : savedDefaultOutputUid);
        if (saved != kAudioObjectUnknown && ! isJamLinkDevice (stringProperty (saved, kAudioDevicePropertyDeviceNameCFString)))
            setDefaultDevice (input, saved);
    }
   #endif
}

VirtualDevices::~VirtualDevices()
{
    destroyAggregate();
}

VirtualDevices::State VirtualDevices::getState() const
{
   #if JUCE_MAC
    auto bundled = bundledDriversFolder();
    int bundledBuild = 0;
    for (auto& name : driverNames)
    {
        auto driver = bundled.getChildFile (name + ".driver");
        if (! driver.isDirectory())
            return State::notBundled;
        bundledBuild = juce::jmax (bundledBuild, bundleBuildNumber (driver));
    }

    bool anyMissing = false, anyOlder = false;
    for (auto& name : driverNames)
    {
        auto installed = installedDriver (name);
        if (! installed.isDirectory())
            anyMissing = true;
        else if (bundleBuildNumber (installed) < bundledBuild)
            anyOlder = true;
    }

    if (anyMissing)
        return State::notInstalled;
    return anyOlder ? State::updateAvailable : State::installed;
   #else
    return State::notBundled;
   #endif
}

bool VirtualDevices::areDevicesLoaded() const
{
   #if JUCE_MAC
    return deviceForUid (sendHiddenUid) != kAudioObjectUnknown && deviceForUid (returnHiddenUid) != kAudioObjectUnknown;
   #else
    return false;
   #endif
}

void VirtualDevices::install (std::function<void (bool, const juce::String&)> done)
{
    auto source = bundledDriversFolder();
    juce::String command = "/bin/mkdir -p " + juce::String (halFolder);
    for (auto& name : driverNames)
    {
        auto target = installedDriver (name).getFullPathName();
        command << " && /bin/rm -rf " << shellQuote (target)
                << " && /usr/bin/ditto " << shellQuote (source.getChildFile (name + ".driver").getFullPathName())
                << " " << shellQuote (target)
                << " && /usr/sbin/chown -R root:wheel " << shellQuote (target);
    }
    // Restart the audio service so it loads the drivers.
    command << " && { /usr/bin/killall coreaudiod; true; }";

    runAdminScript (command, "JamLink wants to install its audio devices (JamLink Send and JamLink Return).", std::move (done));
}

void VirtualDevices::remove (std::function<void (bool, const juce::String&)> done)
{
    juce::String command = "true";
    for (auto& name : driverNames)
        command << " && /bin/rm -rf " << shellQuote (installedDriver (name).getFullPathName());
    command << " && { /usr/bin/killall coreaudiod; true; }";

    destroyAggregate();
    runAdminScript (command, "JamLink wants to remove its audio devices.", std::move (done));
}

void VirtualDevices::runAdminScript (const juce::String& shellCommand, const juce::String& prompt,
                                     std::function<void (bool, const juce::String&)> done)
{
    // The command and prompt are passed as arguments, so nothing in them is
    // ever parsed as AppleScript.
    juce::StringArray args { "/usr/bin/osascript",
                             "-e", "on run argv",
                             "-e", "do shell script (item 1 of argv) with administrator privileges with prompt (item 2 of argv)",
                             "-e", "end run",
                             shellCommand, prompt };

    juce::Thread::launch ([args, done]
    {
        juce::ChildProcess process;
        bool ok = false;
        juce::String output;
        if (process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            output = process.readAllProcessOutput().trim();
            ok = process.getExitCode() == 0;
        }
        else
        {
            output = "Couldn't run osascript.";
        }

        // -128 = the player cancelled the password prompt.
        auto error = ok || output.contains ("-128") ? juce::String() : (output.isNotEmpty() ? output : juce::String ("The command failed."));
        juce::MessageManager::callAsync ([done, ok, error] { done (ok, error); });
    });
}

juce::String VirtualDevices::createAggregate (const juce::String& hardwareInput, const juce::String& hardwareOutput,
                                              juce::String& error)
{
   #if JUCE_MAC
    destroyAggregate();

    if (! areDevicesLoaded())
    {
        error = "The JamLink audio devices aren't loaded yet.";
        return {};
    }

    auto inUid = uidForDeviceName (hardwareInput);
    auto outUid = uidForDeviceName (hardwareOutput);

    // The hardware output keeps time for the whole aggregate (falling back to
    // the input); every other device is drift-corrected against it.
    juce::String clockUid = outUid.isNotEmpty() ? outUid : (inUid.isNotEmpty() ? inUid : juce::String (sendHiddenUid));

    juce::StringArray subdevices;
    for (auto& uid : { inUid, outUid, juce::String (sendHiddenUid), juce::String (returnHiddenUid) })
        if (uid.isNotEmpty())
            subdevices.addIfNotAlreadyThere (uid);

    auto list = CFArrayCreateMutable (nullptr, 0, &kCFTypeArrayCallBacks);
    for (auto& uid : subdevices)
    {
        auto entry = CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        auto cfUid = uid.toCFString();
        CFDictionarySetValue (entry, CFSTR (kAudioSubDeviceUIDKey), cfUid);
        CFDictionarySetValue (entry, CFSTR (kAudioSubDeviceDriftCompensationKey), uid == clockUid ? kCFBooleanFalse : kCFBooleanTrue);
        CFArrayAppendValue (list, entry);
        CFRelease (cfUid);
        CFRelease (entry);
    }

    auto name = juce::String (aggregateName).toCFString();
    auto aggregateUid = ("com.spiral.jamlink.aggregate." + juce::String ((int) getpid())).toCFString();
    auto cfClock = clockUid.toCFString();
    int one = 1, zero = 0;
    auto cfOne = CFNumberCreate (nullptr, kCFNumberIntType, &one);
    auto cfZero = CFNumberCreate (nullptr, kCFNumberIntType, &zero);

    auto description = CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceNameKey), name);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceUIDKey), aggregateUid);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceIsPrivateKey), cfOne);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceIsStackedKey), cfZero);
    CFDictionarySetValue (description, CFSTR (kAudioAggregateDeviceSubDeviceListKey), list);
    // "master" is kAudioAggregateDeviceMainSubDeviceKey (macOS 12+ name).
    CFDictionarySetValue (description, CFSTR ("master"), cfClock);

    AudioObjectID created = kAudioObjectUnknown;
    auto status = AudioHardwareCreateAggregateDevice (description, &created);

    for (CFTypeRef ref : { (CFTypeRef) description, (CFTypeRef) list, (CFTypeRef) name, (CFTypeRef) aggregateUid,
                           (CFTypeRef) cfClock, (CFTypeRef) cfOne, (CFTypeRef) cfZero })
        CFRelease (ref);

    if (status != noErr || created == kAudioObjectUnknown)
    {
        error = "Couldn't combine the audio devices (CoreAudio error " + juce::String ((int) status) + ").";
        return {};
    }

    aggregateId = created;

    // Record the channel layout in sub-device order, for channel labels.
    inputSegments.clear();
    outputSegments.clear();
    for (auto& uid : subdevices)
    {
        auto device = deviceForUid (uid);
        bool jamLink = uid == sendHiddenUid || uid == returnHiddenUid;
        if (auto n = channelCount (device, true); n > 0)  inputSegments.push_back ({ n, jamLink });
        if (auto n = channelCount (device, false); n > 0) outputSegments.push_back ({ n, jamLink });
    }

    return aggregateName;
   #else
    juce::ignoreUnused (hardwareInput, hardwareOutput);
    error = "Not supported on this platform.";
    return {};
   #endif
}

void VirtualDevices::destroyAggregate()
{
   #if JUCE_MAC
    if (aggregateId != 0)
        AudioHardwareDestroyAggregateDevice (aggregateId);
   #endif
    aggregateId = 0;
    inputSegments.clear();
    outputSegments.clear();
}

namespace
{
    juce::StringArray labelsFor (const std::vector<std::pair<int, bool>>& segments, const juce::String& hardwarePrefix,
                                 const juce::String& jamLinkPrefix, const juce::BigInteger& active)
    {
        juce::StringArray all;
        int hardware = 0, jamLink = 0;
        for (auto& [count, isJamLink] : segments)
            for (int i = 0; i < count; ++i)
                all.add (isJamLink ? jamLinkPrefix + " " + juce::String (++jamLink)
                                   : hardwarePrefix + " " + juce::String (++hardware));

        juce::StringArray result;
        for (int i = 0; i < all.size(); ++i)
            if (active[i])
                result.add (all[i]);
        return result;
    }
}

juce::StringArray VirtualDevices::getInputLabels (const juce::BigInteger& activeChannels) const
{
    std::vector<std::pair<int, bool>> segments;
    for (auto& s : inputSegments)
        segments.emplace_back (s.numChannels, s.isJamLink);
    return labelsFor (segments, "In", "JamLink Send", activeChannels);
}

juce::StringArray VirtualDevices::getOutputLabels (const juce::BigInteger& activeChannels) const
{
    std::vector<std::pair<int, bool>> segments;
    for (auto& s : outputSegments)
        segments.emplace_back (s.numChannels, s.isJamLink);
    return labelsFor (segments, "Out", "JamLink Return", activeChannels);
}
