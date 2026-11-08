#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

class VocalLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    VocalLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle,
                           float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

// A knob with its name above and value below, attached to one parameter.
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title);
    void resized() override;
    juce::Slider slider;

private:
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// One titled module in the chain with its on/off switch.
class ModulePanel final : public juce::Component
{
public:
    ModulePanel (juce::AudioProcessorValueTreeState&, const juce::String& title, juce::Colour accent, const juce::String& enableId);
    Knob* addKnob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& title);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setReduction (float db);
    void setExtra (juce::Component* c, int height) { extra = c; extraHeight = height; addAndMakeVisible (c); }
    void setShowsReduction (bool s) { showsReduction = s; }

private:
    static constexpr int headerHeight = 30;
    juce::String title;
    juce::Colour accent;
    juce::ToggleButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    juce::OwnedArray<Knob> knobs;
    juce::Component* extra = nullptr;
    int extraHeight = 0;
    float reduction = 0;
    bool showsReduction = false;
};

// Shows the note being sung, where it should be, and the correction being applied.
class TunerDisplay final : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void update (bool voiced, float midi, float target, float correctionCents, uint16_t mask);

private:
    bool voiced = false;
    float midi = 0, target = 0, correction = 0, heldCents = 0;
    uint16_t mask = 0xfff;
};

class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (juce::String titleIn) : title (std::move (titleIn)) {}
    void paint (juce::Graphics&) override;
    void setLevel (float db) { level = db; peak = juce::jmax (db, peak - 0.6f); repaint(); }

private:
    juce::String title;
    float level = -120, peak = -120;
};

class VegaVocalEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VegaVocalEditor (VegaVocalProcessor&);
    ~VegaVocalEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showStatus (const juce::String& text) { statusText = text; statusTicks = 240; repaint (statusArea); }

    VegaVocalProcessor& processor;
    VocalLookAndFeel look;

    juce::ComboBox presetBox, keyBox, scaleBox, syncBox;
    juce::TextButton autoButton { "AUTO-AJUSTE" };
    juce::ToggleButton levelerButton { "Nivelador" };
    juce::Component tuneExtras;

    ModulePanel inputPanel, tunePanel, deEssPanel, compPanel, satPanel, delayPanel, reverbPanel;
    Knob* delayTimeKnob = nullptr;
    TunerDisplay tuner;
    LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    Knob outputKnob;

    juce::AudioProcessorValueTreeState::ComboBoxAttachment keyAttachment, scaleAttachment, syncAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment levelerAttachment;

    juce::String statusText;
    juce::Rectangle<int> statusArea;
    int statusTicks = 0;
    bool wasLearning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaVocalEditor)
};
