// Integration tests: run the whole plugin as a host would, then render the GUI to a PNG.
// Usage: vocal_host_tests <output.png> [path to the built .vst3]
#include "PluginEditor.h"

#include <cstdio>

static int failures = 0;

static void check (const char* name, double actual, double expected, double tolerance)
{
    const bool ok = std::abs (actual - expected) <= tolerance;
    std::printf ("%s %-58s got %12.5f  want %12.5f (+/-%g)\n", ok ? "PASS" : "FAIL", name, actual, expected, tolerance);
    if (! ok)
        ++failures;
}

static void set (VegaVocalProcessor& p, const juce::String& id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

static float get (VegaVocalProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

static float tone (double hz, double sr, int64_t i, float amp)
{
    double s = 0;
    for (int h = 1; h <= 8; ++h)
        s += std::sin (juce::MathConstants<double>::twoPi * hz * h * (double) i / sr) / (1.0 + 0.5 * h);
    return amp * (float) s;
}

// Streams a harmonic tone in irregular blocks, some larger than announced. Returns the output peak.
static float stream (VegaVocalProcessor& p, double hz, double sr, double seconds, float amp, int64_t& clock, int channels = 2)
{
    juce::AudioBuffer<float> buffer (channels, 4096);
    juce::MidiBuffer midi;
    constexpr std::array<int, 6> sizes { 512, 64, 1, 333, 512, 4096 };
    auto remaining = (int64_t) (seconds * sr);
    size_t turn = 0;
    float peak = 0;
    while (remaining > 0)
    {
        const int n = (int) juce::jmin ((int64_t) sizes[turn++ % sizes.size()], remaining);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), channels, n);
        for (int i = 0; i < n; ++i)
        {
            const float s = tone (hz, sr, clock + i, amp);
            for (int c = 0; c < channels; ++c)
                view.setSample (c, i, s);
        }
        p.processBlock (view, midi);
        peak = juce::jmax (peak, view.getMagnitude (0, n));
        clock += n;
        remaining -= n;
    }
    return peak;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sr = 44100.0;

    {
        VegaVocalProcessor p;
        check ("mono in, stereo out layout supported", p.isBusesLayoutSupported ({ { juce::AudioChannelSet::mono() }, { juce::AudioChannelSet::stereo() } }) ? 1 : 0, 1, 0);
        check ("stereo in, mono out layout rejected", p.isBusesLayoutSupported ({ { juce::AudioChannelSet::stereo() }, { juce::AudioChannelSet::mono() } }) ? 1 : 0, 0, 0);
        p.prepareToPlay (sr, 512);
        check ("latency reported to host (ms, under 40)", 1000.0 * p.getLatencySamples() / sr, 35, 5);

        int64_t clock = 0;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            const float peak = stream (p, 196.0 + i * 40.0, sr, 0.5, 0.2f, clock);
            const juce::String name = "preset finite, under 0 dBFS: " + p.getProgramName (i);
            check (name.toRawUTF8(), std::isfinite (peak) && peak <= 1.0f ? 1 : 0, 1, 0);
        }
        p.setCurrentProgram (2);
        check ("preset changes the parameters (tune speed)", get (p, "tune_speed"), 8, 0.5);

        // State round trip into a new instance, as when reopening a project.
        set (p, "key", 9);
        set (p, "scale", 2);
        set (p, "comp_thresh", -31.5f);
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        VegaVocalProcessor restored;
        restored.setStateInformation (saved.getData(), (int) saved.getSize());
        check ("saved state restores key", get (restored, "key"), 9, 0);
        check ("saved state restores scale", get (restored, "scale"), 2, 0);
        check ("saved state restores threshold", get (restored, "comp_thresh"), -31.5, 0.05);
        check ("saved state restores preset", restored.getCurrentProgram(), 2, 0);

        // Host bypass passes the signal delayed by the same latency, at unity.
        {
            VegaVocalProcessor b;
            set (b, "bypass", 1.0f);
            b.prepareToPlay (sr, 512);
            int64_t c = 0;
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            const int latency = b.getLatencySamples();
            double worst = 0;
            for (int block = 0; block < 60; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float s = tone (220.0, sr, c + i, 0.2f);
                    buffer.setSample (0, i, s);
                    buffer.setSample (1, i, s);
                }
                b.processBlock (buffer, midi);
                if (block > 10)
                    for (int i = 0; i < 512; ++i)
                        worst = juce::jmax (worst, (double) std::abs (buffer.getSample (0, i) - tone (220.0, sr, c + i - latency, 0.2f)));
                c += 512;
            }
            check ("bypass nulls against input delayed by latency", worst, 0, 1.0e-4);
        }

        // Mono track, as with a single-microphone recording.
        {
            VegaVocalProcessor m;
            m.setBusesLayout ({ { juce::AudioChannelSet::mono() }, { juce::AudioChannelSet::mono() } });
            m.prepareToPlay (sr, 512);
            int64_t c = 0;
            const float peak = stream (m, 220.0, sr, 0.5, 0.2f, c, 1);
            check ("mono layout runs and produces sound", peak > 0.01f && peak <= 1.0f ? 1 : 0, 1, 0);
        }

        // Editor plus Auto-Set from a real session: quiet voice, UI thread picks up the result.
        VegaVocalProcessor u;
        u.prepareToPlay (sr, 512);
        u.setCurrentProgram (2);
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (u.createEditor());
            check ("editor opens at its default size (width)", editor->getWidth(), 1120, 0);

            int64_t c = 0;
            u.startLearning();
            stream (u, 220.0, sr, 0.1, 0.03f, c);
            check ("auto-set reports that it is listening", u.isLearning() ? 1 : 0, 1, 0);
            stream (u, 220.0, sr, 7.0, 0.03f, c);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
            check ("auto-set finished", u.isLearning() ? 0 : 1, 1, 0);
            check ("auto-set raised the input gain", get (u, "input") > 6.0f ? 1 : 0, 1, 0);
            check ("auto-set found root A", get (u, "key"), 9, 0);

            // Let the display show a real tuned note before the snapshot.
            for (int round = 0; round < 6; ++round)
            {
                stream (u, 218.0, sr, 0.15, 0.03f, c);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            }

            if (argc > 1)
            {
                const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                juce::File file (juce::String::fromUTF8 (argv[1]));
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat png;
                check ("GUI snapshot written (1 = yes)", out.openedOk() && png.writeImageToStream (image, out) ? 1 : 0, 1, 0);
            }
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check ("editor closed cleanly (no editor left)", u.getActiveEditor() == nullptr ? 1 : 0, 1, 0);
    }

    // The real binary, loaded through the VST3 interface as FL Studio would.
    if (argc > 2)
    {
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, juce::String::fromUTF8 (argv[2]));
        check ("vst3: scanner finds one plugin", found.size(), 1, 0);
        if (found.size() == 1)
        {
            check ("vst3: name is Vega Vocal", found[0]->name == "Vega Vocal" ? 1 : 0, 1, 0);
            juce::String error;
            auto instance = format.createInstanceFromDescription (*found[0], sr, 512, error);
            check ("vst3: instance created", instance != nullptr ? 1 : 0, 1, 0);
            if (instance != nullptr)
            {
                instance->setPlayConfigDetails (2, 2, sr, 512);
                instance->prepareToPlay (sr, 512);
                check ("vst3: latency reaches the host (samples)", instance->getLatencySamples(), 1520, 40);
                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer midi;
                float peak = 0;
                for (int block = 0; block < 200; ++block)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = tone (218.0, sr, (int64_t) block * 512 + i, 0.2f);
                        buffer.setSample (0, i, v);
                        buffer.setSample (1, i, v);
                    }
                    instance->processBlock (buffer, midi);
                    peak = juce::jmax (peak, buffer.getMagnitude (0, 512));
                }
                check ("vst3: processes audio", peak > 0.01f && peak <= 1.0f ? 1 : 0, 1, 0);
                {
                    std::unique_ptr<juce::AudioProcessorEditor> ui (instance->createEditorIfNeeded());
                    check ("vst3: editor opens", ui != nullptr ? 1 : 0, 1, 0);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
                }
                juce::MemoryBlock state;
                instance->getStateInformation (state);
                check ("vst3: state saves", state.getSize() > 100 ? 1 : 0, 1, 0);
                instance->setStateInformation (state.getData(), (int) state.getSize());
                instance->releaseResources();
            }
        }
    }

    std::printf (failures == 0 ? "\nALL HOST TESTS PASSED\n" : "\n%d HOST TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
