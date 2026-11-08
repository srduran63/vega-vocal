#pragma once

#include "VocalCore.h"

#include <juce_audio_processors/juce_audio_processors.h>

class VegaVocalProcessor final : public juce::AudioProcessor
{
public:
    VegaVocalProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // Auto-Set. Call applyLearnResult() regularly from the message thread; it returns true once a
    // session has ended, and `succeeded` says whether enough voice was heard.
    void startLearning() { chain.requestLearn(); }
    void cancelLearning() { chain.cancelLearn(); }
    bool isLearning() const { return chain.isLearning(); }
    float learnProgress() const { return chain.learnProgress(); }
    bool applyLearnResult (bool& succeeded);

    const vocal::Meters& getMeters() const { return chain.getMeters(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParam (const juce::String& id, float value);
    vocal::Params readParams();
    float delayTimeMs();

    vocal::VocalChain chain;
    juce::AudioParameterBool* bypassParam = nullptr;
    juce::AudioBuffer<float> work, bypassDelay;
    int bypassWrite = 0, currentProgram = 0;
    float bypassMix = 0.0f, bypassCoeff = 0.0f;
    double lastBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaVocalProcessor)
};
