#include "PluginEditor.h"

namespace
{
constexpr int kPluginIndex = DIGITALIS_PLUGIN_INDEX;

juce::Colour fromHex(int rgb)
{
    return juce::Colour::fromRGB((uint8_t) ((rgb >> 16) & 0xff), (uint8_t) ((rgb >> 8) & 0xff), (uint8_t) (rgb & 0xff));
}

juce::Font titleFont()
{
    return juce::Font(juce::FontOptions(26.0f, juce::Font::bold));
}

juce::Font sectionFont()
{
    return juce::Font(juce::FontOptions(13.0f, juce::Font::bold));
}

juce::Font captionFont()
{
    return juce::Font(juce::FontOptions(12.0f, juce::Font::plain));
}
}

class DigitalisAudioProcessorEditor::Style final : public juce::LookAndFeel_V4
{
public:
    explicit Style(const Theme& t)
        : theme(t)
    {
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        juce::ignoreUnused(slider);

        auto area = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(6.0f);
        const auto radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;
        const auto centre = area.getCentre();
        const auto angle = juce::jmap(sliderPosProportional, rotaryStartAngle, rotaryEndAngle);

        g.setColour(theme.panel.brighter(0.2f));
        g.fillEllipse(area);

        g.setColour(theme.bgBottom.withAlpha(0.9f));
        g.drawEllipse(area, 1.5f);

        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius - 6.0f, radius - 6.0f, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(theme.accent);
        g.strokePath(arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path notch;
        notch.addRectangle(-1.6f, -radius + 10.0f, 3.2f, radius * 0.48f);
        notch.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
        g.setColour(theme.text);
        g.fillPath(notch);

        g.setColour(theme.accent.withAlpha(0.2f));
        g.fillEllipse(area.reduced(radius * 0.58f));
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        label->setJustificationType(juce::Justification::centred);
        return label;
    }

private:
    Theme theme;
};

DigitalisAudioProcessorEditor::DigitalisAudioProcessorEditor(DigitalisAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef(p),
      state(p.getValueTreeState()),
      theme(getTheme())
{
    style = std::make_unique<Style>(theme);
    setLookAndFeel(style.get());

    setOpaque(true);
    setSize(980, 640);

    title.setText(processorRef.getName(), juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centredLeft);
    title.setColour(juce::Label::textColourId, theme.text);
    title.setFont(titleFont());
    addAndMakeVisible(title);

    subtitle.setText(getSubtitle(), juce::dontSendNotification);
    subtitle.setJustificationType(juce::Justification::centredLeft);
    subtitle.setColour(juce::Label::textColourId, theme.muted);
    subtitle.setFont(captionFont());
    addAndMakeVisible(subtitle);

    macroTitle.setText("MACRO ARRAY", juce::dontSendNotification);
    macroTitle.setJustificationType(juce::Justification::centredLeft);
    macroTitle.setColour(juce::Label::textColourId, theme.accent);
    macroTitle.setFont(sectionFont());
    addAndMakeVisible(macroTitle);

    globalTitle.setText("GLOBAL", juce::dontSendNotification);
    globalTitle.setJustificationType(juce::Justification::centredLeft);
    globalTitle.setColour(juce::Label::textColourId, theme.accent);
    globalTitle.setFont(sectionFont());
    addAndMakeVisible(globalTitle);

    advancedTitle.setText("ADVANCED", juce::dontSendNotification);
    advancedTitle.setJustificationType(juce::Justification::centredLeft);
    advancedTitle.setColour(juce::Label::textColourId, theme.accent);
    advancedTitle.setFont(sectionFont());
    addAndMakeVisible(advancedTitle);

    aboutBody.setJustificationType(juce::Justification::topLeft);
    aboutBody.setColour(juce::Label::textColourId, theme.text);
    aboutBody.setColour(juce::Label::backgroundColourId, theme.panel.brighter(0.05f));
    aboutBody.setColour(juce::Label::outlineColourId, theme.accent.withAlpha(0.35f));
    aboutBody.setText(getAboutText(), juce::dontSendNotification);
    aboutBody.setFont(captionFont());
    addAndMakeVisible(aboutBody);

    auto setupPageButton = [this](juce::TextButton& b, const juce::String& label, Page page)
    {
        b.setButtonText(label);
        b.setColour(juce::TextButton::buttonColourId, theme.panel.brighter(0.1f));
        b.setColour(juce::TextButton::buttonOnColourId, theme.accent.withAlpha(0.35f));
        b.setColour(juce::TextButton::textColourOffId, theme.text);
        b.setColour(juce::TextButton::textColourOnId, theme.text);
        b.onClick = [this, page] { setPage(page); };
        addAndMakeVisible(b);
    };
    setupPageButton(mainPageButton, "Main", Page::main);
    setupPageButton(advancedPageButton, "Advanced", Page::advanced);
    setupPageButton(aboutPageButton, "About", Page::about);

    prevPresetButton.setButtonText("<");
    nextPresetButton.setButtonText(">");
    prevPresetButton.setColour(juce::TextButton::buttonColourId, theme.panel.brighter(0.1f));
    nextPresetButton.setColour(juce::TextButton::buttonColourId, theme.panel.brighter(0.1f));
    prevPresetButton.setColour(juce::TextButton::textColourOffId, theme.text);
    nextPresetButton.setColour(juce::TextButton::textColourOffId, theme.text);
    addAndMakeVisible(prevPresetButton);
    addAndMakeVisible(nextPresetButton);

    presetBox.setColour(juce::ComboBox::backgroundColourId, theme.panel.brighter(0.07f));
    presetBox.setColour(juce::ComboBox::textColourId, theme.text);
    presetBox.setColour(juce::ComboBox::outlineColourId, theme.accent.withAlpha(0.3f));
    presetBox.setColour(juce::ComboBox::buttonColourId, theme.panel.brighter(0.12f));
    presetBox.setColour(juce::ComboBox::arrowColourId, theme.text);
    addAndMakeVisible(presetBox);

    const auto presetCount = processorRef.getNumPrograms();
    for (int i = 0; i < presetCount; ++i)
        presetBox.addItem(processorRef.getProgramName(i), i + 1);
    presetBox.setSelectedId(processorRef.getCurrentProgram() + 1, juce::dontSendNotification);

    presetBox.onChange = [this]
    {
        if (presetUiUpdating)
            return;

        const auto idx = presetBox.getSelectedId() - 1;
        if (idx >= 0)
            processorRef.setCurrentProgram(idx);
    };

    prevPresetButton.onClick = [this]
    {
        const auto n = juce::jmax(1, processorRef.getNumPrograms());
        const auto idx = (processorRef.getCurrentProgram() - 1 + n) % n;
        processorRef.setCurrentProgram(idx);
        presetBox.setSelectedId(idx + 1, juce::dontSendNotification);
    };

    nextPresetButton.onClick = [this]
    {
        const auto n = juce::jmax(1, processorRef.getNumPrograms());
        const auto idx = (processorRef.getCurrentProgram() + 1) % n;
        processorRef.setCurrentProgram(idx);
        presetBox.setSelectedId(idx + 1, juce::dontSendNotification);
    };

    const auto macrosSpec = getMacroLayout();
    for (size_t i = 0; i < macros.size(); ++i)
        setupControl(macros[i], macrosSpec[i]);

    constexpr std::array<ParamSpec, 4> globalSpec {{
        { "mix", "Mix" },
        { "autolevel", "Auto Level" },
        { "safety", "Safety" },
        { "output", "Output" }
    }};
    for (size_t i = 0; i < globals.size(); ++i)
        setupControl(globals[i], globalSpec[i]);

    const auto advancedSpec = getAdvancedLayout();
    for (size_t i = 0; i < advanced.size(); ++i)
        setupControl(advanced[i], advancedSpec[i]);

    setPage(Page::main);
    startTimerHz(8);
}

void DigitalisAudioProcessorEditor::setupControl(Control& control, const ParamSpec& spec)
{
    control.caption.setText(spec.label, juce::dontSendNotification);
    control.caption.setJustificationType(juce::Justification::centred);
    control.caption.setColour(juce::Label::textColourId, theme.text);
    control.caption.setFont(captionFont());
    addAndMakeVisible(control.caption);

    control.knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    control.knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 20);
    control.knob.setColour(juce::Slider::rotarySliderFillColourId, theme.accent);
    control.knob.setColour(juce::Slider::thumbColourId, theme.text);
    control.knob.setColour(juce::Slider::textBoxTextColourId, theme.text);
    control.knob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    control.knob.setColour(juce::Slider::textBoxBackgroundColourId, theme.panel.brighter(0.08f));
    addAndMakeVisible(control.knob);

    if (state.getParameter(spec.id) != nullptr)
    {
        control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, spec.id, control.knob);
    }
    else
    {
        control.knob.setEnabled(false);
        control.knob.setTextValueSuffix(" n/a");
    }
}

DigitalisAudioProcessorEditor::~DigitalisAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void DigitalisAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bg(theme.bgTop, 0.0f, 0.0f, theme.bgBottom, 0.0f, (float) getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();

    auto bounds = getLocalBounds().toFloat().reduced(12.0f);
    g.setColour(theme.panel.withAlpha(0.92f));
    g.fillRoundedRectangle(bounds, 16.0f);

    g.setColour(theme.accent.withAlpha(0.45f));
    g.drawRoundedRectangle(bounds.reduced(1.5f), 16.0f, 2.0f);
}

void DigitalisAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(28);

    auto header = area.removeFromTop(86);
    auto topRow = header.removeFromTop(42);
    auto buttonsArea = topRow.removeFromRight(276);
    auto presetArea = topRow.removeFromRight(360);
    title.setBounds(topRow);
    prevPresetButton.setBounds(presetArea.removeFromLeft(28).reduced(2));
    presetBox.setBounds(presetArea.removeFromLeft(300).reduced(4));
    nextPresetButton.setBounds(presetArea.removeFromLeft(28).reduced(2));
    const int bw = 88;
    mainPageButton.setBounds(buttonsArea.removeFromLeft(bw).reduced(4));
    advancedPageButton.setBounds(buttonsArea.removeFromLeft(bw).reduced(4));
    aboutPageButton.setBounds(buttonsArea.removeFromLeft(bw).reduced(4));
    subtitle.setBounds(header.removeFromTop(24));

    area.removeFromTop(6);

    if (currentPage == Page::main)
    {
        auto macroLabelArea = area.removeFromTop(24);
        macroTitle.setBounds(macroLabelArea.removeFromLeft(200));

        auto macroGrid = area.removeFromTop(380);
        const int cellW = macroGrid.getWidth() / 4;
        const int cellH = macroGrid.getHeight() / 2;

        for (int i = 0; i < 8; ++i)
        {
            const int row = i / 4;
            const int col = i % 4;
            auto cell = juce::Rectangle<int>(macroGrid.getX() + col * cellW, macroGrid.getY() + row * cellH, cellW, cellH).reduced(8);

            auto captionArea = cell.removeFromTop(26);
            macros[(size_t) i].caption.setBounds(captionArea);
            macros[(size_t) i].knob.setBounds(cell.reduced(10, 0));
        }

        area.removeFromTop(10);
        globalTitle.setBounds(area.removeFromTop(24).removeFromLeft(150));

        auto globalRow = area.removeFromTop(140);
        const int globalW = globalRow.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = juce::Rectangle<int>(globalRow.getX() + i * globalW, globalRow.getY(), globalW, globalRow.getHeight()).reduced(8);
            auto captionArea = cell.removeFromTop(24);
            globals[(size_t) i].caption.setBounds(captionArea);
            globals[(size_t) i].knob.setBounds(cell.reduced(10, 0));
        }
    }
    else if (currentPage == Page::advanced)
    {
        advancedTitle.setBounds(area.removeFromTop(24).removeFromLeft(180));
        auto advancedGrid = area.removeFromTop(460);
        const int cellW = advancedGrid.getWidth() / 2;
        const int cellH = advancedGrid.getHeight() / 2;
        for (int i = 0; i < 4; ++i)
        {
            const int row = i / 2;
            const int col = i % 2;
            auto cell = juce::Rectangle<int>(advancedGrid.getX() + col * cellW, advancedGrid.getY() + row * cellH, cellW, cellH).reduced(10);
            auto captionArea = cell.removeFromTop(26);
            advanced[(size_t) i].caption.setBounds(captionArea);
            advanced[(size_t) i].knob.setBounds(cell.reduced(14, 0));
        }
    }
    else
    {
        auto aboutArea = area.reduced(4);
        aboutBody.setBounds(aboutArea.removeFromTop(500).reduced(8));
    }
}

void DigitalisAudioProcessorEditor::timerCallback()
{
    const auto idx = processorRef.getCurrentProgram();
    const auto expectedId = idx + 1;
    if (presetBox.getSelectedId() != expectedId)
    {
        presetUiUpdating = true;
        presetBox.setSelectedId(expectedId, juce::dontSendNotification);
        presetUiUpdating = false;
    }
}

void DigitalisAudioProcessorEditor::setPage(Page page)
{
    currentPage = page;
    const auto isMain = currentPage == Page::main;
    const auto isAdvanced = currentPage == Page::advanced;
    const auto isAbout = currentPage == Page::about;

    mainPageButton.setToggleState(isMain, juce::dontSendNotification);
    advancedPageButton.setToggleState(isAdvanced, juce::dontSendNotification);
    aboutPageButton.setToggleState(isAbout, juce::dontSendNotification);

    macroTitle.setVisible(isMain);
    globalTitle.setVisible(isMain);
    for (auto& c : macros)
    {
        c.caption.setVisible(isMain);
        c.knob.setVisible(isMain);
    }
    for (auto& c : globals)
    {
        c.caption.setVisible(isMain);
        c.knob.setVisible(isMain);
    }

    advancedTitle.setVisible(isAdvanced);
    for (auto& c : advanced)
    {
        c.caption.setVisible(isAdvanced);
        c.knob.setVisible(isAdvanced);
    }

    aboutBody.setVisible(isAbout);
    resized();
}

DigitalisAudioProcessorEditor::Theme DigitalisAudioProcessorEditor::getTheme()
{
    switch (kPluginIndex)
    {
        case 1: return { fromHex(0x1A1D29), fromHex(0x0B0F1E), fromHex(0x12172A), fromHex(0x41D3BD), fromHex(0xEAF5FF), fromHex(0xA9BBCA) };
        case 2: return { fromHex(0x241612), fromHex(0x120B0A), fromHex(0x221310), fromHex(0xFF8552), fromHex(0xFFEFE7), fromHex(0xCCAD9C) };
        case 3: return { fromHex(0x201928), fromHex(0x100C14), fromHex(0x1B1322), fromHex(0xFF6FA8), fromHex(0xFFF0F7), fromHex(0xCCB2BF) };
        case 4: return { fromHex(0x102522), fromHex(0x081411), fromHex(0x10201D), fromHex(0x5EE38F), fromHex(0xECFFF2), fromHex(0xAAC9B6) };
        case 5: return { fromHex(0x252214), fromHex(0x131108), fromHex(0x1F1A10), fromHex(0xFFC94B), fromHex(0xFFF9E6), fromHex(0xD2C7A6) };
        case 6: return { fromHex(0x141A2C), fromHex(0x090D18), fromHex(0x12172A), fromHex(0x7FA6FF), fromHex(0xEDF3FF), fromHex(0xAAB9D8) };
        case 7: return { fromHex(0x2A1616), fromHex(0x140A0A), fromHex(0x241212), fromHex(0xFF5B5B), fromHex(0xFFEDED), fromHex(0xD5AFAF) };
        case 8: return { fromHex(0x1B2316), fromHex(0x0F140B), fromHex(0x192214), fromHex(0x9FDF5A), fromHex(0xF4FFE7), fromHex(0xB9CEA7) };
        case 9: return { fromHex(0x22191A), fromHex(0x120D0E), fromHex(0x1D1415), fromHex(0xFF7A6A), fromHex(0xFFF1EE), fromHex(0xD4B1AB) };
        case 10: return { fromHex(0x181E29), fromHex(0x0D121A), fromHex(0x141B25), fromHex(0x79C7FF), fromHex(0xECF7FF), fromHex(0xA8C2D6) };
        default: return { fromHex(0x1E1E1E), fromHex(0x101010), fromHex(0x1A1A1A), fromHex(0x7FC8FF), fromHex(0xF0F0F0), fromHex(0xB9B9B9) };
    }
}

juce::String DigitalisAudioProcessorEditor::getSubtitle()
{
    switch (kPluginIndex)
    {
        case 1: return "Numerical precision collapse workstation";
        case 2: return "Alias-first spectral destruction engine";
        case 3: return "Buffer seams, reorder, and DAW-core failure";
        case 4: return "Grid-locked control-rate brutalism";
        case 5: return "Streaming codec artifact synthesizer";
        case 6: return "FFT-domain machine hearing vandalism";
        case 7: return "CPU stress and thermal drift simulation";
        case 8: return "Finite-state microloop deterministic machine";
        case 9: return "Classic repeat-buffer stutter workstation";
        case 10: return "Diskont-era melodic skip and jump composer";
        default: return "Digitalis";
    }
}

std::array<DigitalisAudioProcessorEditor::ParamSpec, 8> DigitalisAudioProcessorEditor::getMacroLayout()
{
    switch (kPluginIndex)
    {
        case 1: return {{
            { "collapse", "Collapse" },
            { "mantissaBits", "Mantissa Bits" },
            { "exponentStep", "Exponent Step" },
            { "temporalHold", "Temporal Hold" },
            { "blockSize", "Block Size" },
            { "quantCurve", "Quant Curve" },
            { "rounding", "Rounding Chaos" },
            { "denormal", "Denormal Burst" }
        }};
        case 2: return {{
            { "destroy", "Destroy" },
            { "minSR", "Min SR" },
            { "maxSR", "Max SR" },
            { "modDepth", "SR Mod Depth" },
            { "modRate", "SR Mod Rate" },
            { "interpErr", "Interp Error" },
            { "transient", "Transient Drop" },
            { "feedback", "Alias Feedback" }
        }};
        case 3: return {{
            { "stress", "Engine Stress" },
            { "baseBlock", "Base Block" },
            { "blockJitter", "Block Jitter" },
            { "seam", "Seam Error" },
            { "tailDrop", "Tail Drop" },
            { "reorder", "Reorder" },
            { "lookFail", "Lookahead Fail" },
            { "mix", "Mix Macro" }
        }};
        case 4: return {{
            { "brutal", "Brutalism" },
            { "gridMode", "Grid Mode" },
            { "stepDiv", "Step Division" },
            { "zipper", "Zipper Tone" },
            { "levels", "Env Levels" },
            { "phaseLock", "Phase Lock" },
            { "jitter", "Human Error" },
            { "mix", "Mix Macro" }
        }};
        case 5: return {{
            { "artifact", "Artifact" },
            { "bitrate", "Bitrate" },
            { "masking", "Masking" },
            { "smear", "Smear" },
            { "codecMode", "Codec Mode" },
            { "switchMs", "Switch Rate" },
            { "packetLoss", "Packet Loss" },
            { "burst", "Burstiness" }
        }};
        case 6: return {{
            { "brutalism", "Brutalism" },
            { "binDensity", "Bin Density" },
            { "cluster", "Cluster" },
            { "freezeRate", "Freeze Rate" },
            { "freezeLen", "Freeze Length" },
            { "phaseScramble", "Phase Scramble" },
            { "phaseSteps", "Phase Steps" },
            { "sortAmount", "Sort Amount" }
        }};
        case 7: return {{
            { "overclock", "Overclock" },
            { "sensitivity", "Sensitivity" },
            { "failureRate", "Failure Rate" },
            { "latencySpike", "Latency Spike" },
            { "desync", "L/R Desync" },
            { "thermal", "Thermal Drift" },
            { "recovery", "Recovery" },
            { "mix", "Mix Macro" }
        }};
        case 8: return {{
            { "determinism", "Determinism" },
            { "stateCount", "State Count" },
            { "stateDwell", "State Dwell" },
            { "loopMs", "Loop Length" },
            { "hashWindow", "Hash Window" },
            { "jumpRule", "Jump Rule" },
            { "memory", "Memory" },
            { "mix", "Mix Macro" }
        }};
        case 9: return {{
            { "amount", "Amount" },
            { "rateHz", "Rate" },
            { "sliceMs", "Slice Length" },
            { "repeats", "Repeats" },
            { "reverse", "Reverse Chance" },
            { "timingJitter", "Timing Jitter" },
            { "duck", "Dry Duck" },
            { "mix", "Mix Macro" }
        }};
        case 10: return {{
            { "skip", "Skip Amount" },
            { "jumpRate", "Jump Rate" },
            { "segMs", "Segment Length" },
            { "melody", "Melody" },
            { "spread", "Pitch Spread" },
            { "reverse", "Reverse Chance" },
            { "flutter", "Flutter" },
            { "blur", "Blur" }
        }};
        default: return {{
            { "digital", "Digital" },
            { "mix", "Mix" },
            { "autolevel", "Auto Level" },
            { "safety", "Safety" },
            { "output", "Output" },
            { "digital", "Digital 2" },
            { "mix", "Mix 2" },
            { "output", "Output 2" }
        }};
    }
}

std::array<DigitalisAudioProcessorEditor::ParamSpec, 4> DigitalisAudioProcessorEditor::getAdvancedLayout()
{
    switch (kPluginIndex)
    {
        case 1: return {{ { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "safety", "Safety" }, { "output", "Output" } }};
        case 2: return {{ { "fbTone", "Feedback Tone" }, { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "output", "Output" } }};
        case 3: return {{ { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "safety", "Safety" }, { "output", "Output" } }};
        case 4: return {{ { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "safety", "Safety" }, { "output", "Output" } }};
        case 5: return {{ { "preecho", "Pre Echo" }, { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "output", "Output" } }};
        case 6: return {{ { "jitter", "Spectral Jitter" }, { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "output", "Output" } }};
        case 7: return {{ { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "safety", "Safety" }, { "output", "Output" } }};
        case 8: return {{ { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "safety", "Safety" }, { "output", "Output" } }};
        case 9: return {{ { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "safety", "Safety" }, { "output", "Output" } }};
        case 10: return {{ { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "safety", "Safety" }, { "output", "Output" } }};
        default: return {{ { "mix", "Mix" }, { "autolevel", "Auto Level" }, { "safety", "Safety" }, { "output", "Output" } }};
    }
}

juce::String DigitalisAudioProcessorEditor::getAboutText()
{
    return "Digitalis: hyper-digital signal abuse suite\\n\\n"
           "This plugin is one module in a 10-part system with shared UX, "
           "macro topology, and gain safety.\\n\\n"
           "Pages:\\n"
           "Main: core macro grid for fast sound design\\n"
           "Advanced: extra module-specific controls + global finishing\\n"
           "About: identity + workflow context\\n\\n"
           "Tip: keep Mix below 40% and raise Auto Level when designing subtle artifacts.";
}
