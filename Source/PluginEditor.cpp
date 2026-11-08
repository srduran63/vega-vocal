#include "PluginEditor.h"

namespace
{
const juce::Colour background (0xff131416), panel (0xff1d1e21), grid (0xff2e3034), textDim (0xff9a9ca1), textMain (0xffe8e8ea);
const juce::Colour accentInput (0xff8fa3b8), accentTune (0xff3ddcb4), accentDeEss (0xfff2c14e), accentComp (0xffff8a4c),
                   accentSat (0xfff0566e), accentDelay (0xff9b7bff), accentReverb (0xff4fa8ff);

const char* const noteNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

juce::String text (const char* utf8) { return juce::String::fromUTF8 (utf8); }

juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace

VocalLookAndFeel::VocalLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, background);
    setColour (juce::ComboBox::outlineColourId, grid);
    setColour (juce::ComboBox::textColourId, textMain);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, grid);
    setColour (juce::TextButton::buttonColourId, accentTune.withAlpha (0.18f));
    setColour (juce::TextButton::buttonOnColourId, accentTune);
    setColour (juce::TextButton::textColourOffId, accentTune);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::Slider::textBoxTextColourId, textMain);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, textDim);
}

void VocalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                         float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const bool enabled = slider.isEnabled();

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (grid);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
    g.setColour (enabled ? accent : textDim.withAlpha (0.4f));
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float cap = radius * 0.68f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff36383d), c.x, c.y - cap, juce::Colour (0xff17181a), c.x, c.y + cap, false));
    g.fillEllipse (juce::Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (c));
    g.setColour (juce::Colours::black);
    g.drawEllipse (juce::Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (c), 1.0f);

    g.setColour (enabled ? juce::Colours::white : textDim);
    g.drawLine (juce::Line<float> (c.getPointOnCircumference (cap * 0.42f, angle), c.getPointOnCircumference (cap * 0.92f, angle)), 2.2f);
}

void VocalLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto accent = button.findColour (juce::ToggleButton::tickColourId);
    const auto area = button.getLocalBounds().toFloat();
    const auto sw = area.withWidth (28.0f).withSizeKeepingCentre (28.0f, 14.0f);
    const bool on = button.getToggleState();
    g.setColour (on ? accent : (highlighted ? textDim : grid));
    g.fillRoundedRectangle (sw, 7.0f);
    g.setColour (on ? background : textMain.withAlpha (0.8f));
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ on ? sw.getRight() - 7.0f : sw.getX() + 7.0f, sw.getCentreY() }));
    if (button.getButtonText().isNotEmpty())
    {
        g.setColour (on ? textMain : textDim);
        g.setFont (uiFont (12.5f));
        g.drawText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (36), juce::Justification::centredLeft);
    }
}

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title)
    : attachment (state, paramId, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    if (auto* param = state.getParameter (paramId))
        slider.setTextValueSuffix (param->getLabel().isEmpty() ? juce::String() : " " + param->getLabel());
    label.setText (title, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (uiFont (12.0f));
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    // Keep name, knob and value together however tall the panel is.
    auto area = getLocalBounds().withSizeKeepingCentre (getWidth(), juce::jmin (getHeight(), getWidth() + 34));
    label.setBounds (area.removeFromTop (17));
    slider.setBounds (area);
}

ModulePanel::ModulePanel (juce::AudioProcessorValueTreeState& state, const juce::String& titleIn, juce::Colour accentIn, const juce::String& enableId)
    : title (titleIn), accent (accentIn)
{
    if (enableId.isNotEmpty())
    {
        power.setColour (juce::ToggleButton::tickColourId, accent);
        power.setTitle (title);
        powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, enableId, power);
        addAndMakeVisible (power);
    }
}

Knob* ModulePanel::addKnob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& knobTitle)
{
    auto* knob = knobs.add (new Knob (state, id, knobTitle));
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob);
    return knob;
}

void ModulePanel::setReduction (float db)
{
    if (std::abs (db - reduction) > 0.05f)
    {
        reduction = db;
        repaint (getLocalBounds().removeFromTop (headerHeight + 8));
    }
}

void ModulePanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (area, 7.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (area.withHeight (3.0f).reduced (7.0f, 0.0f), 1.5f);
    g.setColour (textMain);
    g.setFont (uiFont (13.5f, true));
    g.drawText (title, 12, 6, getWidth() - 50, headerHeight - 6, juce::Justification::centredLeft);

    if (showsReduction)
    {
        const auto meter = juce::Rectangle<float> (12.0f, (float) headerHeight + 1.0f, (float) getWidth() - 24.0f, 4.0f);
        g.setColour (grid);
        g.fillRoundedRectangle (meter, 2.0f);
        const float fill = juce::jlimit (0.0f, 1.0f, reduction / 18.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * fill), 2.0f);
    }
}

void ModulePanel::resized()
{
    auto area = getLocalBounds().reduced (8, 6);
    auto header = area.removeFromTop (headerHeight - 6);
    power.setBounds (header.removeFromRight (30));
    if (showsReduction)
        area.removeFromTop (8);
    if (extra != nullptr)
    {
        extra->setBounds (area.removeFromTop (extraHeight));
        area.removeFromTop (4);
    }
    if (knobs.isEmpty())
        return;

    // Lay the knobs out in as many columns as stays comfortable for the panel's width.
    const int columns = juce::jlimit (1, knobs.size(), area.getWidth() / 74);
    const int rows = (knobs.size() + columns - 1) / columns;
    const int rowHeight = area.getHeight() / rows;
    for (int row = 0, index = 0; row < rows; ++row)
    {
        auto rowArea = area.removeFromTop (rowHeight);
        const int inRow = juce::jmin (columns, knobs.size() - index);
        const int width = rowArea.getWidth() / columns;
        rowArea = rowArea.withSizeKeepingCentre (width * inRow, rowArea.getHeight());
        for (int i = 0; i < inRow; ++i)
            knobs[index++]->setBounds (rowArea.removeFromLeft (width));
    }
}

void TunerDisplay::update (bool voicedIn, float midiIn, float targetIn, float correctionCents, uint16_t maskIn)
{
    voiced = voicedIn;
    midi = midiIn;
    target = targetIn;
    correction = correctionCents;
    mask = maskIn;
    if (voiced)
        heldCents = (midi - target) * 100.0f;
    repaint();
}

void TunerDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (background);
    g.fillRoundedRectangle (area, 6.0f);
    auto inner = area.reduced (10.0f, 8.0f);

    // Note name
    auto left = inner.removeFromLeft (92.0f);
    const int note = juce::roundToInt (target);
    const bool hasNote = voiced && note >= 0;
    g.setColour (hasNote ? accentTune : grid);
    g.setFont (uiFont (40.0f, true));
    g.drawText (hasNote ? juce::String (noteNames[note % 12]) + juce::String (note / 12 - 1) : juce::String ("--"),
                left.removeFromTop (54.0f), juce::Justification::centred);
    g.setColour (textDim);
    g.setFont (uiFont (11.5f));
    g.drawText (hasNote ? juce::String (correction >= 0 ? "+" : "") + juce::String (juce::roundToInt (correction)) + " cents" : juce::String ("sin voz"),
                left, juce::Justification::centredTop);

    // Keyboard showing the notes allowed by the current key and scale
    auto keys = inner.removeFromBottom (26.0f);
    const float keyWidth = keys.getWidth() / 12.0f;
    for (int i = 0; i < 12; ++i)
    {
        const auto key = juce::Rectangle<float> (keys.getX() + keyWidth * (float) i, keys.getY(), keyWidth, keys.getHeight()).reduced (1.5f, 0.0f);
        const bool allowed = (mask >> i) & 1, active = hasNote && note % 12 == i;
        g.setColour (active ? accentTune : allowed ? juce::Colour (0xff3a3d42) : juce::Colour (0xff1b1c1f));
        g.fillRoundedRectangle (key, 3.0f);
        g.setColour (active ? background : allowed ? textMain : textDim.withAlpha (0.4f));
        g.setFont (uiFont (10.5f, active));
        g.drawText (noteNames[i], key, juce::Justification::centred);
    }

    // Pitch error: centre is in tune
    inner.removeFromBottom (8.0f);
    const auto scale = inner.reduced (0.0f, 4.0f);
    const float centreX = scale.getCentreX(), halfWidth = scale.getWidth() * 0.5f;
    g.setColour (grid);
    for (int cents = -50; cents <= 50; cents += 10)
    {
        const float x = centreX + halfWidth * (float) cents / 50.0f;
        const float inset = cents % 50 == 0 ? 0.0f : scale.getHeight() * 0.3f;
        g.drawLine (x, scale.getY() + inset, x, scale.getBottom() - inset, cents == 0 ? 2.0f : 1.0f);
    }
    g.setColour (textDim);
    g.setFont (uiFont (10.0f));
    g.drawText ("bajo", scale.withWidth (40.0f).translated (4.0f, 0.0f), juce::Justification::topLeft);
    g.drawText ("alto", scale.withTrimmedLeft (scale.getWidth() - 40.0f).translated (-4.0f, 0.0f), juce::Justification::topRight);
    if (hasNote)
    {
        const float x = centreX + halfWidth * juce::jlimit (-1.0f, 1.0f, heldCents / 50.0f);
        g.setColour (accentTune);
        g.fillRoundedRectangle (juce::Rectangle<float> (5.0f, scale.getHeight()).withCentre ({ x, scale.getCentreY() }), 2.5f);
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (textDim);
    g.setFont (uiFont (10.5f, true));
    g.drawText (title, area.removeFromTop (15.0f), juce::Justification::centred);
    auto bar = area.withSizeKeepingCentre (juce::jmin (14.0f, area.getWidth()), area.getHeight());
    g.setColour (background);
    g.fillRoundedRectangle (bar, 3.0f);

    const auto toY = [&bar] (float db) { return bar.getBottom() - bar.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    g.setGradientFill (juce::ColourGradient (accentSat, 0.0f, toY (0.0f), accentTune, 0.0f, toY (-18.0f), false));
    g.fillRect (bar.withTop (toY (level)).reduced (1.5f, 0.0f));
    g.setColour (textMain);
    if (peak > -60.0f)
        g.fillRect (bar.getX() + 1.5f, toY (peak), bar.getWidth() - 3.0f, 1.5f);
}

VegaVocalEditor::VegaVocalEditor (VegaVocalProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      inputPanel (p.apvts, "1  ENTRADA", accentInput, {}),
      tunePanel (p.apvts, text ("2  AFINACIÓN"), accentTune, "tune_on"),
      deEssPanel (p.apvts, "3  DE-ESSER", accentDeEss, "deess_on"),
      compPanel (p.apvts, "4  COMPRESOR", accentComp, "comp_on"),
      satPanel (p.apvts, text ("5  SATURACIÓN"), accentSat, "sat_on"),
      delayPanel (p.apvts, "6  DELAY", accentDelay, "delay_on"),
      reverbPanel (p.apvts, "7  REVERB", accentReverb, "rev_on"),
      outputKnob (p.apvts, "output", "Salida"),
      keyAttachment (p.apvts, "key", keyBox),
      scaleAttachment (p.apvts, "scale", scaleBox),
      syncAttachment (p.apvts, "delay_sync", syncBox),
      levelerAttachment (p.apvts, "autolevel", levelerButton)
{
    setLookAndFeel (&look);

    // The attachments were built before the lists existed, so select the current values again.
    const auto fill = [&p] (juce::ComboBox& box, const char* id)
    {
        box.addItemList (p.apvts.getParameter (id)->getAllValueStrings(), 1);
        box.setSelectedItemIndex ((int) p.apvts.getRawParameterValue (id)->load(), juce::dontSendNotification);
    };
    fill (keyBox, "key");
    fill (scaleBox, "scale");
    fill (syncBox, "delay_sync");

    for (int i = 0; i < p.getNumPrograms(); ++i)
        presetBox.addItem (p.getProgramName (i), i + 1);
    presetBox.setSelectedItemIndex (p.getCurrentProgram(), juce::dontSendNotification);
    presetBox.onChange = [this] { processor.setCurrentProgram (presetBox.getSelectedItemIndex()); };
    addAndMakeVisible (presetBox);

    autoButton.onClick = [this]
    {
        if (processor.isLearning())
        {
            processor.cancelLearning();
            showStatus ("Auto-ajuste cancelado");
        }
        else
        {
            processor.startLearning();
        }
    };
    addAndMakeVisible (autoButton);

    levelerButton.setColour (juce::ToggleButton::tickColourId, accentInput);
    tuneExtras.addAndMakeVisible (keyBox);
    tuneExtras.addAndMakeVisible (scaleBox);
    tuneExtras.addAndMakeVisible (tuner);

    inputPanel.setExtra (&levelerButton, 22);
    inputPanel.addKnob (p.apvts, "input", "Ganancia");
    inputPanel.addKnob (p.apvts, "lowcut", "Corte graves");

    tunePanel.setExtra (&tuneExtras, 132);
    tunePanel.addKnob (p.apvts, "tune_amount", "Cantidad");
    tunePanel.addKnob (p.apvts, "tune_speed", "Velocidad");

    deEssPanel.setShowsReduction (true);
    deEssPanel.addKnob (p.apvts, "deess_amount", "Cantidad");
    deEssPanel.addKnob (p.apvts, "deess_freq", "Frecuencia");

    compPanel.setShowsReduction (true);
    compPanel.addKnob (p.apvts, "comp_thresh", "Umbral");
    compPanel.addKnob (p.apvts, "comp_ratio", "Ratio");
    compPanel.addKnob (p.apvts, "comp_attack", "Ataque");
    compPanel.addKnob (p.apvts, "comp_release", "Release");
    compPanel.addKnob (p.apvts, "comp_makeup", "Ganancia");

    satPanel.addKnob (p.apvts, "sat_drive", "Drive");
    satPanel.addKnob (p.apvts, "sat_tilt", "Color");
    satPanel.addKnob (p.apvts, "sat_mix", "Mezcla");

    delayPanel.setExtra (&syncBox, 26);
    delayTimeKnob = delayPanel.addKnob (p.apvts, "delay_time", "Tiempo");
    delayPanel.addKnob (p.apvts, "delay_feedback", text ("Repetición"));
    delayPanel.addKnob (p.apvts, "delay_mix", "Mezcla");
    delayPanel.addKnob (p.apvts, "delay_spread", "Ping-pong");

    reverbPanel.addKnob (p.apvts, "rev_size", text ("Tamaño"));
    reverbPanel.addKnob (p.apvts, "rev_decay", "Decaimiento");
    reverbPanel.addKnob (p.apvts, "rev_damp", "Oscuridad");
    reverbPanel.addKnob (p.apvts, "rev_pre", "Pre-delay");
    reverbPanel.addKnob (p.apvts, "rev_mix", "Mezcla");
    reverbPanel.addKnob (p.apvts, "duck", "Esquivar voz");

    for (auto* c : std::initializer_list<juce::Component*> { &inputPanel, &tunePanel, &deEssPanel, &compPanel, &satPanel, &delayPanel, &reverbPanel, &inMeter, &outMeter, &outputKnob })
        addAndMakeVisible (c);
    outputKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, accentInput);

    setSize (1120, 680);
    startTimerHz (30);
}

VegaVocalEditor::~VegaVocalEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VegaVocalEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (textMain);
    g.setFont (uiFont (21.0f, true));
    g.drawText ("VEGA VOCAL", 16, 10, 150, 32, juce::Justification::centredLeft);

    if (statusText.isNotEmpty())
    {
        g.setColour (textDim);
        g.setFont (uiFont (13.0f));
        g.drawText (statusText, statusArea, juce::Justification::centredLeft);
    }
}

void VegaVocalEditor::resized()
{
    auto area = getLocalBounds().reduced (12, 10);
    auto header = area.removeFromTop (34);
    header.removeFromLeft (158);
    presetBox.setBounds (header.removeFromLeft (190).reduced (0, 3));
    header.removeFromLeft (10);
    autoButton.setBounds (header.removeFromLeft (130).reduced (0, 3));
    header.removeFromLeft (12);
    statusArea = header;
    area.removeFromTop (8);

    // Right column: output
    auto right = area.removeFromRight (86);
    area.removeFromRight (8);
    outputKnob.setBounds (right.removeFromBottom (112));
    right.removeFromBottom (6);
    inMeter.setBounds (right.removeFromLeft (right.getWidth() / 2));
    outMeter.setBounds (right);

    // Two rows of the chain, in signal order
    constexpr int gap = 8;
    auto top = area.removeFromTop ((area.getHeight() - gap) * 54 / 100);
    area.removeFromTop (gap);
    auto bottom = area;

    inputPanel.setBounds (top.removeFromLeft (172));
    top.removeFromLeft (gap);
    deEssPanel.setBounds (top.removeFromRight (178));
    top.removeFromRight (gap);
    tunePanel.setBounds (top);

    // tunePanel's layout has just sized tuneExtras.
    auto tuneArea = tuneExtras.getLocalBounds();
    auto selectors = tuneArea.removeFromRight (150);
    keyBox.setBounds (selectors.removeFromTop (28));
    selectors.removeFromTop (6);
    scaleBox.setBounds (selectors.removeFromTop (28));
    tuneArea.removeFromRight (8);
    tuner.setBounds (tuneArea);

    const int unit = (bottom.getWidth() - gap * 3) / 16;
    compPanel.setBounds (bottom.removeFromLeft (unit * 5));
    bottom.removeFromLeft (gap);
    satPanel.setBounds (bottom.removeFromLeft (unit * 3));
    bottom.removeFromLeft (gap);
    delayPanel.setBounds (bottom.removeFromLeft (unit * 4));
    bottom.removeFromLeft (gap);
    reverbPanel.setBounds (bottom);
}

void VegaVocalEditor::timerCallback()
{
    const auto& m = processor.getMeters();
    inMeter.setLevel (m.input.load());
    outMeter.setLevel (m.output.load());
    deEssPanel.setReduction (m.deEss.load());
    compPanel.setReduction (m.comp.load());

    const int key = (int) processor.apvts.getRawParameterValue ("key")->load();
    const int scale = (int) processor.apvts.getRawParameterValue ("scale")->load();
    tuner.update (m.voiced.load(), m.midi.load(), m.target.load(), m.correctionCents.load(), vocal::scaleMask (scale, key));
    delayTimeKnob->setEnabled (syncBox.getSelectedItemIndex() == 0);

    const bool learning = processor.isLearning();
    if (learning)
    {
        statusText = "Escuchando la voz... canta o reproduce la pista  " + juce::String (juce::roundToInt (processor.learnProgress() * 100.0f)) + "%";
        statusTicks = 2;
        repaint (statusArea);
    }
    if (learning != wasLearning)
    {
        autoButton.setButtonText (learning ? "CANCELAR" : "AUTO-AJUSTE");
        wasLearning = learning;
    }

    bool succeeded = false;
    if (processor.applyLearnResult (succeeded))
    {
        if (succeeded)
            showStatus ("Listo: tono " + keyBox.getText() + " " + scaleBox.getText() + ", entrada "
                        + juce::String (processor.apvts.getRawParameterValue ("input")->load(), 1) + " dB");
        else
            showStatus (text ("No se oyó suficiente voz. Prueba otra vez mientras suena la voz."));
    }

    if (statusTicks > 0 && --statusTicks == 0)
    {
        statusText.clear();
        repaint (statusArea);
    }
}
