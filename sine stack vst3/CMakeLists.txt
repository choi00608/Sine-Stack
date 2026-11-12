cmake_minimum_required(VERSION 3.22)
# Set the default before enabling compilers; explicit single-architecture overrides remain possible.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS" FORCE)
    endif()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SharedVersion.cmake")
project(SineStack VERSION ${SINESTACK_VERSION} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(SineStack COMPANY_NAME "SineStack" BUNDLE_ID "audio.sinestack.synth"
    PLUGIN_MANUFACTURER_CODE Snst PLUGIN_CODE Snst PRODUCT_NAME "Sine Stack"
    ICON_BIG "${SINESTACK_ROOT}/assets/SineStackIcon.png"
    PLIST_TO_MERGE "${SINESTACK_VERSION_PLIST}"
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "Sine Stack uses Bluetooth to connect MIDI keyboards and controllers so you can play the synthesizer wirelessly."
    COPY_PLUGIN_AFTER_BUILD FALSE FORMATS VST3 Standalone
    VST3_CATEGORIES Instrument Synth)
include("${SINESTACK_ROOT}/cmake/AppIcon.cmake")
target_sources(SineStack PRIVATE Source/Plugin.cpp)
include(cmake/BuildInfo.cmake)
target_sources(SineStack_Standalone PRIVATE Source/Standalone.cpp)
target_compile_definitions(SineStack_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_compile_definitions(SineStack PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SineStack PUBLIC juce::juce_audio_utils juce::juce_dsp PRIVATE juce::juce_recommended_config_flags)
if(APPLE)
    # The linker's executable-only signature does not seal the finished VST3 bundle.
    add_custom_command(TARGET SineStack_VST3 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:SineStack_VST3>/Contents/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdPartyNotices.txt" "$<TARGET_BUNDLE_DIR:SineStack_VST3>/Contents/Resources/ThirdPartyNotices.txt"
        COMMAND /usr/bin/codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:SineStack_VST3>"
        COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:SineStack_VST3>"
        VERBATIM)
    add_custom_command(TARGET SineStack_Standalone POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:SineStack_Standalone>/Contents/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdPartyNotices.txt" "$<TARGET_BUNDLE_DIR:SineStack_Standalone>/Contents/Resources/ThirdPartyNotices.txt"
        COMMAND /usr/bin/codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:SineStack_Standalone>"
        VERBATIM)
    add_custom_target(install-vst3
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_BUNDLE=$<TARGET_BUNDLE_DIR:SineStack_VST3>"
            "-DDESTINATION_BUNDLE=$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Sine Stack.vst3"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallVST3.cmake"
        DEPENDS SineStack_VST3
        COMMENT "Installing Sine Stack VST3 for the current user"
        VERBATIM)
endif()
option(SINESTACK_TESTS "Build offline audio checks" ON)
if(SINESTACK_TESTS)
    enable_testing()
    juce_add_console_app(SineStackChecks PRODUCT_NAME "Sine Stack Checks")
    target_sources(SineStackChecks PRIVATE Tests/AudioChecks.cpp)
    target_link_libraries(SineStackChecks PRIVATE SineStack)
    add_test(NAME audio-checks COMMAND SineStackChecks)
endif()

# Load the actual VST3 binary in an independent host to exercise the wrapper ABI.
if(SINESTACK_TESTS)
    juce_add_console_app(SineStackVST3HostChecks PRODUCT_NAME "Sine Stack VST3 Host Checks")
    target_sources(SineStackVST3HostChecks PRIVATE Tests/Vst3HostChecks.cpp)
    target_compile_definitions(SineStackVST3HostChecks PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SineStackVST3HostChecks PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    add_dependencies(SineStackVST3HostChecks SineStack_VST3)
    get_target_property(vst3_bundle SineStack_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
    add_test(NAME vst3-host-checks COMMAND SineStackVST3HostChecks "${vst3_bundle}")
endif()

# Fail a Universal build if either executable slice is missing, before installation.
if(APPLE AND "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES AND "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
    foreach(target SineStack_VST3 SineStack_Standalone)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND /usr/bin/lipo "$<TARGET_FILE:${target}>" -verify_arch arm64 x86_64
            VERBATIM)
    endforeach()
endif()
