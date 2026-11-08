#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

const juce::StringArray keyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const juce::StringArray scaleNames { juce::String::fromUTF8 ("Cromática"), "Mayor", "Menor", juce::String::fromUTF8 ("Menor armónica"), "Penta mayor", "Penta menor" };
const juce::StringArray syncNames { "Tiempo libre", "1/2", "1/4 con punto", "1/4", "1/8 con punto", "1/8", "1/16" };
constexpr std::array<double, 7> syncBeats { 0.0, 2.0, 1.5, 1.0, 0.75, 0.5, 0.25 };

Range skewed (float lo, float hi, float centre, float step = 0.01f)
{
    Range r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}

struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

// Presets set the character of the chain. Gain, key and scale stay yours (or Auto-Set's).
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Natural", { { "tune_on", 1 }, { "tune_amount", 70 }, { "tune_speed", 90 }, { "deess_on", 1 }, { "deess_amount", 35 }, { "comp_on", 1 }, { "comp_thresh", -18 }, { "comp_ratio", 2.5f }, { "comp_attack", 15 }, { "comp_release", 150 }, { "comp_makeup", 3 }, { "sat_on", 1 }, { "sat_drive", 12 }, { "sat_tilt", 0 }, { "sat_mix", 100 }, { "delay_on", 0 }, { "rev_on", 1 }, { "rev_size", 35 }, { "rev_decay", 1.1f }, { "rev_damp", 50 }, { "rev_pre", 20 }, { "rev_mix", 10 }, { "duck", 30 } } },
        { "Pop moderno", { { "tune_on", 1 }, { "tune_amount", 90 }, { "tune_speed", 45 }, { "deess_on", 1 }, { "deess_amount", 50 }, { "comp_on", 1 }, { "comp_thresh", -22 }, { "comp_ratio", 4 }, { "comp_attack", 6 }, { "comp_release", 100 }, { "comp_makeup", 6 }, { "sat_on", 1 }, { "sat_drive", 25 }, { "sat_tilt", 25 }, { "sat_mix", 100 }, { "delay_on", 1 }, { "delay_sync", 4 }, { "delay_feedback", 22 }, { "delay_mix", 10 }, { "delay_spread", 100 }, { "rev_on", 1 }, { "rev_size", 50 }, { "rev_decay", 1.7f }, { "rev_damp", 35 }, { "rev_pre", 30 }, { "rev_mix", 14 }, { "duck", 50 } } },
        { "Urbano afinado", { { "tune_on", 1 }, { "tune_amount", 100 }, { "tune_speed", 8 }, { "deess_on", 1 }, { "deess_amount", 55 }, { "comp_on", 1 }, { "comp_thresh", -24 }, { "comp_ratio", 5 }, { "comp_attack", 4 }, { "comp_release", 80 }, { "comp_makeup", 7 }, { "sat_on", 1 }, { "sat_drive", 35 }, { "sat_tilt", 15 }, { "sat_mix", 100 }, { "delay_on", 1 }, { "delay_sync", 3 }, { "delay_feedback", 30 }, { "delay_mix", 14 }, { "delay_spread", 100 }, { "rev_on", 1 }, { "rev_size", 40 }, { "rev_decay", 1.3f }, { "rev_damp", 40 }, { "rev_pre", 25 }, { "rev_mix", 12 }, { "duck", 65 } } },
        { "Tune duro", { { "tune_on", 1 }, { "tune_amount", 100 }, { "tune_speed", 0 }, { "deess_on", 1 }, { "deess_amount", 60 }, { "comp_on", 1 }, { "comp_thresh", -26 }, { "comp_ratio", 6 }, { "comp_attack", 3 }, { "comp_release", 70 }, { "comp_makeup", 8 }, { "sat_on", 1 }, { "sat_drive", 45 }, { "sat_tilt", 20 }, { "sat_mix", 100 }, { "delay_on", 1 }, { "delay_sync", 5 }, { "delay_feedback", 35 }, { "delay_mix", 16 }, { "delay_spread", 100 }, { "rev_on", 1 }, { "rev_size", 55 }, { "rev_decay", 1.8f }, { "rev_damp", 35 }, { "rev_pre", 30 }, { "rev_mix", 16 }, { "duck", 70 } } },
        { "R&B suave", { { "tune_on", 1 }, { "tune_amount", 85 }, { "tune_speed", 60 }, { "deess_on", 1 }, { "deess_amount", 45 }, { "comp_on", 1 }, { "comp_thresh", -20 }, { "comp_ratio", 3 }, { "comp_attack", 12 }, { "comp_release", 160 }, { "comp_makeup", 5 }, { "sat_on", 1 }, { "sat_drive", 22 }, { "sat_tilt", -30 }, { "sat_mix", 100 }, { "delay_on", 1 }, { "delay_sync", 2 }, { "delay_feedback", 28 }, { "delay_mix", 12 }, { "delay_spread", 80 }, { "rev_on", 1 }, { "rev_size", 70 }, { "rev_decay", 2.6f }, { "rev_damp", 55 }, { "rev_pre", 40 }, { "rev_mix", 20 }, { "duck", 55 } } },
        { "Rap al frente", { { "tune_on", 0 }, { "deess_on", 1 }, { "deess_amount", 50 }, { "comp_on", 1 }, { "comp_thresh", -26 }, { "comp_ratio", 5 }, { "comp_attack", 5 }, { "comp_release", 70 }, { "comp_makeup", 8 }, { "sat_on", 1 }, { "sat_drive", 38 }, { "sat_tilt", 10 }, { "sat_mix", 100 }, { "delay_on", 1 }, { "delay_sync", 5 }, { "delay_feedback", 12 }, { "delay_mix", 7 }, { "delay_spread", 100 }, { "rev_on", 1 }, { "rev_size", 20 }, { "rev_decay", 0.6f }, { "rev_damp", 55 }, { "rev_pre", 10 }, { "rev_mix", 6 }, { "duck", 70 } } },
        { "Balada amplia", { { "tune_on", 1 }, { "tune_amount", 75 }, { "tune_speed", 110 }, { "deess_on", 1 }, { "deess_amount", 40 }, { "comp_on", 1 }, { "comp_thresh", -20 }, { "comp_ratio", 3 }, { "comp_attack", 18 }, { "comp_release", 220 }, { "comp_makeup", 4 }, { "sat_on", 1 }, { "sat_drive", 14 }, { "sat_tilt", 10 }, { "sat_mix", 100 }, { "delay_on", 1 }, { "delay_sync", 2 }, { "delay_feedback", 35 }, { "delay_mix", 13 }, { "delay_spread", 100 }, { "rev_on", 1 }, { "rev_size", 85 }, { "rev_decay", 3.6f }, { "rev_damp", 40 }, { "rev_pre", 55 }, { "rev_mix", 25 }, { "duck", 45 } } },
    };
    return list;
}
} // namespace

VegaVocalProcessor::VegaVocalProcessor()
    : AudioProcessor (BusesProperties().withInput ("Entrada", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Salida", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VegaVocal", createLayout())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
}

juce::AudioProcessorValueTreeState::ParameterLayout VegaVocalProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const auto addF = [&l] (const char* id, const char* name, Range range, float def, const char* unit = "")
    {
        l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, juce::String::fromUTF8 (name), range, def, juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    const auto addB = [&l] (const char* id, const char* name, bool def) { l.add (std::make_unique<APB> (juce::ParameterID { id, 1 }, juce::String::fromUTF8 (name), def)); };
    const auto addC = [&l] (const char* id, const char* name, const juce::StringArray& items, int def) { l.add (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, items, def)); };

    addB ("bypass", "Bypass", false);
    addF ("input", "Entrada", Range (-24, 24, 0.1f), 0, "dB");
    addF ("lowcut", "Corte graves", skewed (20, 300, 80, 1), 70, "Hz");
    addB ("autolevel", "Nivelador", false);
    addF ("output", "Salida", Range (-24, 12, 0.1f), 0, "dB");

    addB ("tune_on", "Afinación", true);
    addF ("tune_amount", "Cantidad", Range (0, 100, 1), 90, "%");
    addF ("tune_speed", "Velocidad", skewed (0, 400, 50, 1), 45, "ms");
    addC ("key", "Tono", keyNames, 0);
    addC ("scale", "Escala", scaleNames, 0);

    addB ("deess_on", "De-Esser", true);
    addF ("deess_amount", "Cantidad de-esser", Range (0, 100, 1), 45, "%");
    addF ("deess_freq", "Frecuencia de-esser", skewed (3000, 11000, 6000, 10), 6500, "Hz");

    addB ("comp_on", "Compresor", true);
    addF ("comp_thresh", "Umbral", Range (-50, 0, 0.1f), -20, "dB");
    addF ("comp_ratio", "Ratio", skewed (1, 20, 4, 0.1f), 3.5f, ":1");
    addF ("comp_attack", "Ataque", skewed (0.5f, 100, 10, 0.1f), 8, "ms");
    addF ("comp_release", "Release", skewed (20, 800, 120, 1), 110, "ms");
    addF ("comp_makeup", "Ganancia", Range (0, 24, 0.1f), 5, "dB");

    addB ("sat_on", "Saturación", true);
    addF ("sat_drive", "Drive", Range (0, 100, 1), 22, "%");
    addF ("sat_tilt", "Color", Range (-100, 100, 1), 0, "");
    addF ("sat_mix", "Mezcla sat", Range (0, 100, 1), 100, "%");

    addB ("delay_on", "Delay", false);
    addC ("delay_sync", "Sincronia", syncNames, 3);
    addF ("delay_time", "Tiempo", skewed (20, 2000, 300, 1), 375, "ms");
    addF ("delay_feedback", "Repetición", Range (0, 90, 1), 28, "%");
    addF ("delay_mix", "Mezcla delay", Range (0, 100, 1), 14, "%");
    addF ("delay_spread", "Ping-pong", Range (0, 100, 1), 100, "%");

    addB ("rev_on", "Reverb", true);
    addF ("rev_size", "Tamaño", Range (0, 100, 1), 50, "%");
    addF ("rev_decay", "Decaimiento", skewed (0.2f, 10, 1.5f, 0.01f), 1.5f, "s");
    addF ("rev_damp", "Oscuridad", Range (0, 90, 1), 40, "%");
    addF ("rev_pre", "Pre-delay", Range (0, 200, 1), 25, "ms");
    addF ("rev_mix", "Mezcla reverb", Range (0, 100, 1), 14, "%");
    addF ("duck", "Esquivar voz", Range (0, 100, 1), 45, "%");
    return l;
}

bool VegaVocalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    return (out == stereo && (in == mono || in == stereo)) || (out == mono && in == mono);
}

void VegaVocalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate);
    chain.setParams (readParams());
    setLatencySamples (chain.latency());
    work.setSize (2, juce::jmax (samplesPerBlock, 512));
    work.clear();
    bypassDelay.setSize (2, chain.latency() + 1);
    bypassDelay.clear();
    bypassWrite = 0;
    bypassMix = bypassParam->get() ? 1.0f : 0.0f;
    bypassCoeff = vocal::timeCoeff (0.01f, sampleRate);
}

float VegaVocalProcessor::delayTimeMs()
{
    const int sync = (int) apvts.getRawParameterValue ("delay_sync")->load();
    if (sync <= 0)
        return apvts.getRawParameterValue ("delay_time")->load();
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto bpm = position->getBpm(); bpm && *bpm > 20.0)
                lastBpm = *bpm;
    return (float) juce::jmin (2000.0, 60000.0 / lastBpm * syncBeats[(size_t) sync]);
}

vocal::Params VegaVocalProcessor::readParams()
{
    const auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const auto b = [&f] (const char* id) { return f (id) > 0.5f; };
    vocal::Params p;
    p.inputDb = f ("input");
    p.lowCutHz = f ("lowcut");
    p.autoLevel = b ("autolevel");
    p.outputDb = f ("output");
    p.tuneOn = b ("tune_on");
    p.tuneAmount = f ("tune_amount") * 0.01f;
    p.tuneSpeedMs = f ("tune_speed");
    p.key = (int) f ("key");
    p.scale = (int) f ("scale");
    p.deEssOn = b ("deess_on");
    p.deEss = f ("deess_amount") * 0.01f;
    p.deEssHz = f ("deess_freq");
    p.compOn = b ("comp_on");
    p.compThreshold = f ("comp_thresh");
    p.compRatio = f ("comp_ratio");
    p.compAttackMs = f ("comp_attack");
    p.compReleaseMs = f ("comp_release");
    p.compMakeupDb = f ("comp_makeup");
    p.satOn = b ("sat_on");
    p.satDrive = f ("sat_drive") * 0.01f;
    p.satTilt = f ("sat_tilt") * 0.01f;
    p.satMix = f ("sat_mix") * 0.01f;
    p.delayOn = b ("delay_on");
    p.delayMs = delayTimeMs();
    p.delayFeedback = f ("delay_feedback") * 0.01f;
    p.delayMix = f ("delay_mix") * 0.01f;
    p.delaySpread = f ("delay_spread") * 0.01f;
    p.reverbOn = b ("rev_on");
    p.reverbSize = f ("rev_size") * 0.01f;
    p.reverbDecay = f ("rev_decay");
    p.reverbDamp = f ("rev_damp") * 0.01f;
    p.reverbPreDelayMs = f ("rev_pre");
    p.reverbMix = f ("rev_mix") * 0.01f;
    p.duck = f ("duck") * 0.01f;
    return p;
}

void VegaVocalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int inputs = getTotalNumInputChannels(), outputs = getTotalNumOutputChannels();
    if (inputs == 0 || outputs == 0 || work.getNumSamples() == 0)
        return;

    chain.setParams (readParams());
    const float bypassTarget = bypassParam->get() ? 1.0f : 0.0f;
    const int delaySize = bypassDelay.getNumSamples();
    auto* dryL = bypassDelay.getWritePointer (0);
    auto* dryR = bypassDelay.getWritePointer (1);

    // Some hosts send more than they announced: work through it in pieces, never reallocate.
    for (int done = 0; done < buffer.getNumSamples();)
    {
        const int n = juce::jmin (buffer.getNumSamples() - done, work.getNumSamples());
        auto* left = buffer.getWritePointer (0, done);
        auto* right = inputs > 1 ? buffer.getWritePointer (1, done) : left;
        auto* outRight = outputs > 1 ? buffer.getWritePointer (1, done) : nullptr;
        auto* workL = work.getWritePointer (0);
        auto* workR = work.getWritePointer (1);
        juce::FloatVectorOperations::copy (workL, left, n);
        juce::FloatVectorOperations::copy (workR, right, n);

        chain.process (workL, workR, n);

        for (int i = 0; i < n; ++i)
        {
            // The dry path is delayed by the same latency so bypass stays in time and never clicks.
            dryL[bypassWrite] = left[i];
            dryR[bypassWrite] = right[i];
            bypassWrite = bypassWrite + 1 >= delaySize ? 0 : bypassWrite + 1;

            bypassMix = bypassCoeff * (bypassMix - bypassTarget) + bypassTarget;
            const float l = workL[i] + bypassMix * (dryL[bypassWrite] - workL[i]);
            const float r = workR[i] + bypassMix * (dryR[bypassWrite] - workR[i]);
            if (outRight != nullptr)
            {
                left[i] = l;
                outRight[i] = r;
            }
            else
            {
                left[i] = 0.5f * (l + r);
            }
        }
        done += n;
    }

    for (int c = juce::jmax (inputs, outputs); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
}

void VegaVocalProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

bool VegaVocalProcessor::applyLearnResult (bool& succeeded)
{
    vocal::LearnResult result;
    if (! chain.takeLearnResult (result))
        return false;
    succeeded = result.valid;
    if (result.valid)
    {
        setParam ("input", result.inputDb);
        setParam ("lowcut", result.lowCutHz);
        setParam ("deess_amount", result.deEss * 100.0f);
        setParam ("comp_thresh", result.compThreshold);
        setParam ("key", (float) result.key);
        setParam ("scale", (float) result.scale);
    }
    return true;
}

int VegaVocalProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String VegaVocalProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (presets()[(size_t) index].name) : juce::String();
}

void VegaVocalProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    currentProgram = index;
    for (const auto& [id, value] : presets()[(size_t) index].values)
        setParam (id, value);
}

void VegaVocalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VegaVocalProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = state.getProperty ("program", 0);
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* VegaVocalProcessor::createEditor() { return new VegaVocalEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaVocalProcessor(); }
