cmake_minimum_required(VERSION 3.22)

# macOS 11+ on both Apple Silicon and Intel. Checked for empty as well as
# undefined: an existing build cache can hold an empty value, which would
# otherwise silently target the build machine's own macOS version.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary" FORCE)
endif()

project(JamLink VERSION 1.3.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED JUCE_PATH)
    set(JUCE_PATH "/Applications/JUCE" CACHE PATH "Path to the JUCE framework")
endif()

add_subdirectory(${JUCE_PATH} JUCE)

juce_add_gui_app(JamLink
    PRODUCT_NAME "JamLink"
    COMPANY_NAME "Spiral Lab"
    BUNDLE_ID "com.spiral.jamlink"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "JamLink needs access to audio input devices to stream audio to other musicians."
    HARDENED_RUNTIME_ENABLED TRUE
    # Without this, the hardened runtime (required for notarization) feeds
    # silence to every audio input.
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input
    PLIST_TO_MERGE "<plist><dict><key>LSMinimumSystemVersion</key><string>11.0</string><key>NSBonjourServices</key><array><string>_jamlink._tcp</string></array><key>NSLocalNetworkUsageDescription</key><string>JamLink finds other JamLinks nearby and streams audio and MIDI to them over your local network or peer-to-peer Wi-Fi.</string></dict></plist>"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Branding/jamlink-icon-1024.png"
)

juce_generate_juce_header(JamLink)

target_sources(JamLink
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/AudioEngine.cpp
        Source/LookAndFeelModern.cpp
        Source/OutputsPanel.cpp
        Source/InputsPanel.cpp
        Source/DeviceCard.cpp
        Source/DarkAppearance.cpp
        Source/PeerLink.cpp
        Source/MidiBridge.cpp
        Source/NearbyPeers.cpp
        Source/NearbyJam.cpp
        Source/VirtualDevices.cpp
)

if(APPLE)
    target_sources(JamLink PRIVATE Source/DarkAppearance.mm)
endif()

# Optional "JamLink Send" / "JamLink Return" audio devices (a GPL-3.0 fork of
# BlackHole, built separately by JamLinkAudio/JamLink/build.sh). When present
# they're bundled in the app, which installs them only if the player asks.
set(JAMLINK_DRIVERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-drivers" CACHE PATH "Folder holding JamLinkSend.driver and JamLinkReturn.driver")
if(APPLE AND EXISTS "${JAMLINK_DRIVERS_DIR}/JamLinkSend.driver" AND EXISTS "${JAMLINK_DRIVERS_DIR}/JamLinkReturn.driver")
    foreach(driver JamLinkSend JamLinkReturn)
        add_custom_command(TARGET JamLink POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:JamLink>/Resources/Drivers/${driver}.driver"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${JAMLINK_DRIVERS_DIR}/${driver}.driver"
                    "$<TARGET_BUNDLE_CONTENT_DIR:JamLink>/Resources/Drivers/${driver}.driver"
            VERBATIM)
    endforeach()
else()
    message(STATUS "JamLink audio drivers not found in ${JAMLINK_DRIVERS_DIR}: building without them")
endif()

target_compile_definitions(JamLink
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(JamLink
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
