#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Kick96DSP.h"
#include "Parameters.h"

class Kick96Processor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    Kick96Processor();
    ~Kick96Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return (int) kk96::factoryPresets().size(); }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override { loadPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    void loadPreset (int index);
    int getPresetIndex() const noexcept { return currentPreset; }
    bool isPresetModified() const noexcept { return presetModified.load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };   // read and cleared by the GUI

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;

    kk96::Engine engine;
    std::vector<std::pair<std::atomic<float>*, const kk96::ParamSpec*>> paramPointers;
    std::vector<float> lastSent;
    int currentPreset = 2;
    std::atomic<bool> presetModified { false };
    bool loadingPreset = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Kick96Processor)
};
