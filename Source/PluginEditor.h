#pragma once

#include "PluginProcessor.h"

#include <memory>
#include <vector>

class DigitalisAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit DigitalisAudioProcessorEditor(DigitalisAudioProcessor&);
    ~DigitalisAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    class Style;
    enum class Page
    {
        main,
        advanced,
        about
    };

    struct Theme
    {
        juce::Colour bgTop;
        juce::Colour bgBottom;
        juce::Colour panel;
        juce::Colour accent;
        juce::Colour text;
        juce::Colour muted;
    };

    struct ParamSpec
    {
        const char* id;
        const char* label;
    };

    struct Control
    {
        juce::Label caption;
        juce::Slider knob;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    static Theme getTheme();
    static juce::String getSubtitle();
    static std::array<ParamSpec, 8> getMacroLayout();
    static std::array<ParamSpec, 4> getAdvancedLayout();
    static juce::String getAboutText();

    void setupControl(Control& control, const ParamSpec& spec);
    void setPage(Page page);

    DigitalisAudioProcessor& processorRef;
    juce::AudioProcessorValueTreeState& state;

    Theme theme;
    std::unique_ptr<Style> style;

    juce::Label title;
    juce::Label subtitle;
    juce::Label macroTitle;
    juce::Label globalTitle;
    juce::TextButton mainPageButton;
    juce::TextButton advancedPageButton;
    juce::TextButton aboutPageButton;
    juce::TextButton prevPresetButton;
    juce::TextButton nextPresetButton;
    juce::ComboBox presetBox;
    juce::Label aboutBody;
    juce::Label advancedTitle;

    std::array<Control, 8> macros;
    std::array<Control, 4> globals;
    std::array<Control, 4> advanced;
    Page currentPage = Page::main;
    bool presetUiUpdating = false;
};
