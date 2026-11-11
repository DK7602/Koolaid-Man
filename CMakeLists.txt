cmake_minimum_required(VERSION 3.24)

# CI passes the build number (1.0.<run>) so Cubase, the About box and the installer agree
set(KOOLAID_VERSION "1.0.0" CACHE STRING "Plug-in version")
project(KoolaidMan VERSION ${KOOLAID_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

if(MSVC)
    # Static runtime so the plugin does not depend on the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if JUCE_DIR is given, otherwise download it.
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.12
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Signalsmith Stretch (MIT): high-quality pitch shifting for the Pitch move
include(FetchContent)
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(signalsmith-stretch)

option(KOOLAID_BUILD_TESTS "Build the offline render test + UI snapshot tools" OFF)

# ---------------------------------------------------------------------------
# Embedded resources (skin artwork + fonts)
# ---------------------------------------------------------------------------
juce_add_binary_data(KoolaidResources
    NAMESPACE KoolaidBinary
    SOURCES
        resources/skin.png
        resources/skin_light.png
        docs/MANUAL.md
        resources/fonts/BarlowCondensed-Medium.ttf
        resources/fonts/BarlowCondensed-SemiBold.ttf
        resources/fonts/BarlowCondensed-Bold.ttf
        resources/fonts/BarlowCondensed-ExtraBoldItalic.ttf
        resources/fonts/Barlow-SemiBold.ttf
        resources/fonts/Barlow-Bold.ttf)

set_target_properties(KoolaidResources PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
juce_add_plugin(KoolaidMan
    COMPANY_NAME                "Koolaid Man"
    PRODUCT_NAME                "Koolaid Man"
    BUNDLE_ID                   "com.koolaidman.beatdrop"
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    PLUGIN_MANUFACTURER_CODE    Kool
    PLUGIN_CODE                 KmBd
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "Koolaid Man's standalone app can capture audio from your input.")

set(KOOLAID_CORE_SOURCES
    src/Model.cpp
    src/DrumDetector.cpp
    src/MoveProcessors.cpp
    src/DropRenderer.cpp
    src/Mastering.cpp
    src/KeyDetector.cpp
    src/Presets.cpp)

target_sources(KoolaidMan PRIVATE
    ${KOOLAID_CORE_SOURCES}
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Skin.cpp
    src/WaveformView.cpp
    src/MoveStackView.cpp
    src/EffectCards.cpp
    src/ManualView.cpp
    src/GridEditor.cpp)

target_include_directories(KoolaidMan PRIVATE src)

target_compile_definitions(KoolaidMan PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(KoolaidMan
    PRIVATE
        KoolaidResources
        signalsmith-stretch
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# ---------------------------------------------------------------------------
# Optional: headless tests (DSP render test and UI snapshot)
# ---------------------------------------------------------------------------
if(KOOLAID_BUILD_TESTS)
    juce_add_console_app(KoolaidTests PRODUCT_NAME "KoolaidTests")
    target_sources(KoolaidTests PRIVATE
        tests/RenderTest.cpp
        ${KOOLAID_CORE_SOURCES})
    target_include_directories(KoolaidTests PRIVATE src)
    target_compile_definitions(KoolaidTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_FLAC=1)
    target_link_libraries(KoolaidTests PRIVATE
        juce::juce_audio_formats juce::juce_dsp juce::juce_audio_basics juce::juce_data_structures signalsmith-stretch
        juce::juce_recommended_config_flags)

    set(KOOLAID_UI_SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/Skin.cpp
        src/WaveformView.cpp
        src/MoveStackView.cpp
        src/EffectCards.cpp
        src/ManualView.cpp
        src/GridEditor.cpp)

    foreach(tool KoolaidSnapshot KoolaidHostTest)
        juce_add_gui_app(${tool} PRODUCT_NAME "${tool}")
        target_include_directories(${tool} PRIVATE src)
        target_compile_definitions(${tool} PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_FLAC=1 JUCE_MODAL_LOOPS_PERMITTED=1
            JucePlugin_Name="Koolaid Man" JucePlugin_VersionString="${PROJECT_VERSION}")
        target_link_libraries(${tool} PRIVATE
            KoolaidResources signalsmith-stretch
            juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
            juce::juce_recommended_config_flags)
    endforeach()
    target_sources(KoolaidSnapshot PRIVATE tests/UISnapshot.cpp ${KOOLAID_CORE_SOURCES} ${KOOLAID_UI_SOURCES})
    target_sources(KoolaidHostTest PRIVATE tests/HostTest.cpp ${KOOLAID_CORE_SOURCES} ${KOOLAID_UI_SOURCES})
    target_compile_definitions(KoolaidHostTest PRIVATE JUCE_PLUGINHOST_VST3=1)
endif()
