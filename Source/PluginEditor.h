#pragma once

#include "PluginProcessor.h"
#include "UIComponents.h"

//==============================================================================
// Everything is laid out at the design size; the editor scales this component as a whole.
// Layout (left to right): Input / Mix column, big Drive knob, six outlined groups (Punch, Focus, Shape, Sub, Tone, Out);
// bottom bar with Mode, Quality and Presets. Same layout as the browser prototype.
class MainPanel : public juce::Component
{
public:
    static constexpr int W = 1160, H = 372;

    explicit MainPanel (Kick96Processor&);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    struct Group { juce::String title; juce::Colour colour; juce::Rectangle<int> bounds; };

    Kick96Processor& proc;
    juce::Image logo;
    std::vector<std::unique_ptr<btui::Knob>> knobs;
    btui::Knob *input = nullptr, *mix = nullptr, *drive = nullptr;
    std::array<std::pair<btui::Knob*, btui::Knob*>, 6> pairs {};
    std::array<Group, 6> groups;

    btui::LedButton bypassBtn { "Bypass", btui::col::magenta, true };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    juce::ComboBox modeBox, qualityBox, presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, qualityAtt;
    btui::ArrowButton prevBtn { "Previous preset", true }, nextBtn { "Next preset", false };
    int shownPreset = -2; bool shownModified = false;

    btui::Knob* addKnob (const juce::String& id, const juce::String& label, juce::Colour c, int diameter, bool big = false);
    void refreshPresetBox();
    void stepPreset (int delta);
};

//==============================================================================
class Kick96Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Kick96Editor (Kick96Processor&);
    ~Kick96Editor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (btui::col::ink); }

private:
    void timerCallback() override { panel.tick(); }
    btui::LookAndFeel lnf;
    MainPanel panel;
};
