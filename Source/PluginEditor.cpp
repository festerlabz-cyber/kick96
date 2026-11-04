#include "PluginEditor.h"

using namespace btui;

namespace
{
    constexpr int kTopH = 66, kBarH = 60, kPad = 22;
    constexpr int kSmall = 46, kBig = 140;
}

//==============================================================================
MainPanel::MainPanel (Kick96Processor& p) : proc (p)
{
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    input = addKnob ("input", "Input", col::amber, kSmall);
    mix   = addKnob ("mix", "Mix", col::amber, kSmall);
    drive = addKnob ("drive", "Drive", col::amber, kBig, true);

    pairs = {{ { addKnob ("punchF", "Freq", col::magenta, kSmall), addKnob ("punch", "Boost", col::magenta, kSmall) },
               { addKnob ("focusF", "Freq", col::gold, kSmall),    addKnob ("focus", "Boost", col::gold, kSmall) },
               { addKnob ("click", "Click", col::olive, kSmall),   addKnob ("tail", "Tail", col::olive, kSmall) },
               { addKnob ("subF", "Freq", col::magenta, kSmall),   addKnob ("sub", "Level", col::magenta, kSmall) },
               { addKnob ("high", "High", col::amber, kSmall),     addKnob ("low", "Low", col::amber, kSmall) },
               { addKnob ("ceiling", "Ceiling", col::olive, kSmall), addKnob ("out", "Output", col::olive, kSmall) } }};
    groups = {{ { "Punch", col::magenta, {} }, { "Focus", col::gold, {} }, { "Shape", col::olive, {} },
                { "Sub", col::magenta, {} }, { "Tone", col::amber, {} }, { "Out", col::olive, {} } }};

    addAndMakeVisible (bypassBtn);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "bypass", bypassBtn);

    modeBox.addItemList ({ "Warm", "Gabber", "Industrial", "Fuzz" }, 1);
    qualityBox.addItemList ({ "Eco", juce::String (juce::CharPointer_UTF8 ("Normal 2\xc3\x97")), juce::String (juce::CharPointer_UTF8 ("High 4\xc3\x97")) }, 1);
    modeBox.setTitle ("Mode"); qualityBox.setTitle ("Quality"); presetBox.setTitle ("Preset");
    addAndMakeVisible (modeBox); addAndMakeVisible (qualityBox);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "mode", modeBox);
    qualityAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "quality", qualityBox);

    const auto& presets = kk96::factoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i) presetBox.addItem (presets[(size_t) i].name, i + 1);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0) { proc.loadPreset (id - 1); shownPreset = -2; }
    };
    addAndMakeVisible (presetBox);
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    addAndMakeVisible (prevBtn); addAndMakeVisible (nextBtn);

    setSize (W, H);
    tick();
}

Knob* MainPanel::addKnob (const juce::String& id, const juce::String& label, juce::Colour c, int diameter, bool big)
{
    const auto* spec = kk96::findSpec (id);
    const auto unit = spec != nullptr ? spec->unit : kk96::Unit::percent;
    auto fmt = [unit] (double v) { return kk96::formatValue (unit, v); };
    std::optional<float> ref;
    if (id == "out" || id == "input" || id == "click" || id == "tail") ref = 0.0f;
    knobs.push_back (std::make_unique<Knob> (proc.apvts, id, label, c, fmt, diameter, big, ref));
    addAndMakeVisible (*knobs.back());
    return knobs.back().get();
}

void MainPanel::paint (juce::Graphics& g)
{
    auto& f = Fonts::get();
    juce::ColourGradient bg (col::panel2, 0, 0, col::panel, 0, (float) H, false);
    g.setGradientFill (bg);
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.012f));
    for (int x = 0; x < W; x += 3) g.drawVerticalLine (x, 0, (float) (H - kBarH));

    // title strip
    if (logo.isValid())
    {
        const float lh = 48.0f, lw = lh * (float) logo.getWidth() / (float) logo.getHeight();
        g.setOpacity (1.0f);   // drawImage uses the current opacity, which the texture lines above left at 1 %
        g.drawImage (logo, juce::Rectangle<float> ((float) kPad, 10.0f, lw, lh), juce::RectanglePlacement::centred);
        const auto title = f.display (36.0f, 0.04f);
        const float tx = (float) kPad + lw + 18.0f;
        g.setFont (title);
        g.setColour (col::text);
        g.drawText ("KICK", juce::Rectangle<float> (tx, 12.0f, 200.0f, 44.0f), juce::Justification::centredLeft, false);
        const float rw = juce::GlyphArrangement::getStringWidth (title, "KICK ");
        g.setColour (col::amber);
        g.drawText ("96", juce::Rectangle<float> (tx + rw, 12.0f, 80.0f, 44.0f), juce::Justification::centredLeft, false);
    }
    g.setColour (col::gold);
    g.setFont (f.label (9.5f, 0.14f));
    g.drawText ("VERSION 0.1", bypassBtn.getX() - 220, 18, 200, 13, juce::Justification::centredRight, false);
    g.drawText ("BOOMTOWNBEATS.COM", bypassBtn.getX() - 220, 33, 200, 13, juce::Justification::centredRight, false);

    for (auto& grp : groups)
    {
        const auto r = grp.bounds.toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.22f), 0, r.getY(), juce::Colours::black.withAlpha (0.05f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (grp.colour);
        g.drawRoundedRectangle (r.reduced (1.0f), 13.0f, 2.0f);
        g.setFont (f.display (16.0f, 0.16f));
        g.drawText (grp.title.toUpperCase(), grp.bounds.withHeight (32).withTrimmedTop (8), juce::Justification::centred, false);
    }

    // bottom bar
    const int by = H - kBarH;
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRect (0, by, W, kBarH);
    g.setColour (col::line);
    g.drawHorizontalLine (by, 0, (float) W);
    const auto barFont = f.display (17.0f, 0.12f);
    auto barLabel = [&] (const juce::Component& c, const juce::String& t)
    {
        g.setFont (barFont);
        g.setColour (col::text);
        const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (barFont, t));
        g.drawText (t, c.getX() - w - 10, c.getY(), w + 2, c.getHeight(), juce::Justification::centredRight, false);
    };
    barLabel (modeBox, "MODE:");
    barLabel (qualityBox, "QUALITY:");
    barLabel (presetBox, "PRESETS:");
}

void MainPanel::paintOverChildren (juce::Graphics& g)
{
    for (auto pt : { juce::Point<float> (12, 12), { (float) W - 12, 12 }, { 12, (float) H - 12 }, { (float) W - 12, (float) H - 12 } })
    {
        juce::ColourGradient sg (juce::Colour (0xff6b5f52), pt.x - 2, pt.y - 2, juce::Colour (0xff2b241e), pt.x + 4, pt.y + 4, true);
        g.setGradientFill (sg);
        g.fillEllipse (pt.x - 5.5f, pt.y - 5.5f, 11, 11);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine (juce::Line<float> (pt.x - 3.5f, pt.y - 2.5f, pt.x + 3.5f, pt.y + 2.5f), 1.0f);
    }
}

void MainPanel::resized()
{
    bypassBtn.setBounds (W - kPad - 104, 18, 104, 30);

    const int top = kTopH + 8, bottom = H - kBarH - 12, mainH = bottom - top;
    // Input / Mix column
    const int colW = input->naturalWidth(), colH = input->naturalHeight();
    input->setBounds (kPad - 6, top + 6, colW, colH);
    mix->setBounds (kPad - 6, bottom - colH - 6, colW, colH);

    // big Drive knob, centred in its column
    const int bw = drive->naturalWidth(), bh = drive->naturalHeight();
    const int bx = kPad - 6 + colW + 10;
    drive->setBounds (bx, top + (mainH - bh) / 2, bw, bh);

    // six groups
    const int gx0 = bx + bw + 14, gx1 = W - kPad, gap = 10;
    const int gw = (gx1 - gx0 - 5 * gap) / 6;
    const int kw = pairs[0].first->naturalWidth(), kh = pairs[0].first->naturalHeight();
    for (int i = 0; i < 6; ++i)
    {
        auto r = juce::Rectangle<int> (gx0 + i * (gw + gap), top, gw, mainH);
        groups[(size_t) i].bounds = r;
        const int kx = r.getCentreX() - kw / 2;
        pairs[(size_t) i].first->setBounds (kx, r.getY() + 34, kw, kh);
        pairs[(size_t) i].second->setBounds (kx, r.getY() + 34 + kh + 8, kw, kh);
    }

    // bottom bar
    const int by = H - kBarH, cy = by + (kBarH - 30) / 2;
    modeBox.setBounds (kPad + 66, cy, 130, 30);
    qualityBox.setBounds (modeBox.getRight() + 40 + 92, cy, 130, 30);
    nextBtn.setBounds (W - kPad - 28, cy + 1, 28, 28);
    prevBtn.setBounds (nextBtn.getX() - 34, cy + 1, 28, 28);
    presetBox.setBounds (prevBtn.getX() - 12 - 200, cy, 200, 30);
}

void MainPanel::refreshPresetBox()
{
    const int idx = proc.getPresetIndex();
    const bool mod = proc.isPresetModified();
    if (idx == shownPreset && mod == shownModified) return;
    shownPreset = idx; shownModified = mod;
    const auto& presets = kk96::factoryPresets();
    const auto name = presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, idx)].name;
    if (mod) presetBox.setText (name + " *", juce::dontSendNotification);
    else presetBox.setSelectedId (idx + 1, juce::dontSendNotification);
}

void MainPanel::stepPreset (int delta)
{
    const int n = (int) kk96::factoryPresets().size();
    proc.loadPreset ((proc.getPresetIndex() + delta + n) % n);
    shownPreset = -2;
    refreshPresetBox();
}

void MainPanel::tick()
{
    refreshPresetBox();
}

//==============================================================================
Kick96Editor::Kick96Editor (Kick96Processor& p)
    : AudioProcessorEditor (p), panel (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    setResizable (true, true);
    setResizeLimits ((int) (MainPanel::W * 0.7), (int) (MainPanel::H * 0.7), (int) (MainPanel::W * 1.6), (int) (MainPanel::H * 1.6));
    getConstrainer()->setFixedAspectRatio ((double) MainPanel::W / MainPanel::H);
    setSize (MainPanel::W, MainPanel::H);
    startTimerHz (15);
}

Kick96Editor::~Kick96Editor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Kick96Editor::resized()
{
    const float s = (float) getWidth() / (float) MainPanel::W;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, MainPanel::W, MainPanel::H);
}
