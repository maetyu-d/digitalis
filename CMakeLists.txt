cmake_minimum_required(VERSION 3.22)

project(DigitalisPlugins VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)

add_subdirectory(JUCE)

function(add_digitalis_plugin target_name plugin_code plugin_index)
    string(TOLOWER "${target_name}" bundle_suffix)

    juce_add_plugin(${target_name}
        COMPANY_NAME "Digitalis"
        COMPANY_WEBSITE "https://example.com"
        COMPANY_EMAIL "dev@example.com"
        BUNDLE_ID "com.digitalis.${bundle_suffix}"
        PLUGIN_MANUFACTURER_CODE Dgts
        PLUGIN_CODE ${plugin_code}
        FORMATS AU VST3
        PRODUCT_NAME "${target_name}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

    target_sources(${target_name}
        PRIVATE
            Source/PluginProcessor.cpp
            Source/PluginProcessor.h
            Source/PluginEditor.cpp
            Source/PluginEditor.h)

    target_compile_definitions(${target_name}
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            DIGITALIS_PLUGIN_INDEX=${plugin_index})

    target_link_libraries(${target_name}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

function(add_measure_tool tool_name plugin_target)
    juce_add_console_app(${tool_name}
        PRODUCT_NAME "${tool_name}")

    target_sources(${tool_name}
        PRIVATE
            Tools/MeasurePluginMain.cpp)

    target_compile_definitions(${tool_name}
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
    target_compile_options(${tool_name}
        PRIVATE
            -Wno-macro-redefined)

    target_link_libraries(${tool_name}
        PRIVATE
            ${plugin_target}
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

add_digitalis_plugin(FloatingPointCollapse Fpcl 1)
add_digitalis_plugin(NyquistDestroyer Nyqd 2)
add_digitalis_plugin(BufferGlitchEngine Bfge 3)
add_digitalis_plugin(AutomationQuantiser Atqn 4)
add_digitalis_plugin(StreamingArtifactGenerator Stag 5)
add_digitalis_plugin(FFTBrutalist Ffbr 6)
add_digitalis_plugin(OverclockFailure Ovcl 7)
add_digitalis_plugin(DeterministicMachine Dtmc 8)
add_digitalis_plugin(ClassicBufferStutter Cbst 9)
add_digitalis_plugin(MelodicSkippingEngine Mskp 10)

add_measure_tool(MeasureFloatingPointCollapse FloatingPointCollapse)
add_measure_tool(MeasureNyquistDestroyer NyquistDestroyer)
add_measure_tool(MeasureBufferGlitchEngine BufferGlitchEngine)
add_measure_tool(MeasureAutomationQuantiser AutomationQuantiser)
add_measure_tool(MeasureStreamingArtifactGenerator StreamingArtifactGenerator)
add_measure_tool(MeasureFFTBrutalist FFTBrutalist)
add_measure_tool(MeasureOverclockFailure OverclockFailure)
add_measure_tool(MeasureDeterministicMachine DeterministicMachine)
add_measure_tool(MeasureClassicBufferStutter ClassicBufferStutter)
add_measure_tool(MeasureMelodicSkippingEngine MelodicSkippingEngine)
