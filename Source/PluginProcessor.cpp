#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace kk96;

Kick96Processor::Kick96Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Kick96", createLayout())
{
    for (auto& s : paramSpecs())
    {
        paramPointers.emplace_back (apvts.getRawParameterValue (s.id), &s);
        apvts.addParameterListener (s.id, this);
    }
    lastSent.assign (paramPointers.size(), std::numeric_limits<float>::quiet_NaN());
    loadPreset (2); // start on "909 Gabber", like the prototype
    setLatencySamples (Engine::latencyFor (2));
}

Kick96Processor::~Kick96Processor()
{
    for (auto& s : paramSpecs()) apvts.removeParameterListener (s.id, this);
    cancelPendingUpdate();
}

bool Kick96Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void Kick96Processor::prepareToPlay (double sampleRate, int)
{
    for (size_t i = 0; i < paramPointers.size(); ++i)
    {
        const auto v = paramPointers[i].first->load();
        engine.setParam (paramPointers[i].second->engineIndex, engineValue (*paramPointers[i].second, v));
        lastSent[i] = v;
    }
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatency());
}

void Kick96Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    for (size_t i = 0; i < paramPointers.size(); ++i)
    {
        const auto v = paramPointers[i].first->load();
        if (! juce::exactlyEqual (v, lastSent[i]))
        {
            engine.setParam (paramPointers[i].second->engineIndex, engineValue (*paramPointers[i].second, v));
            lastSent[i] = v;
        }
    }

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;
    const float* inR = getTotalNumInputChannels() > 1 ? r : l; // mono in -> stereo out
    engine.process (l, inR, l, r, n);

    inPeak = juce::jmax (inPeak.load(), (float) engine.inPeak);
    outPeak = juce::jmax (outPeak.load(), (float) engine.outPeak);
    engine.inPeak = engine.outPeak = 0;
}

void Kick96Processor::parameterChanged (const juce::String& id, float)
{
    if (id == "quality") triggerAsyncUpdate();
    if (! loadingPreset && id != "bypass" && id != "quality") presetModified = true;
}

void Kick96Processor::handleAsyncUpdate()
{
    const auto q = apvts.getRawParameterValue ("quality")->load();
    setLatencySamples (Engine::latencyFor (q < 0.5f ? 1 : (q < 1.5f ? 2 : 4)));
}

const juce::String Kick96Processor::getProgramName (int index)
{
    const auto& p = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? p[(size_t) index].name : juce::String();
}

void Kick96Processor::loadPreset (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size())) return;
    loadingPreset = true;
    const auto& pr = presets[(size_t) index];
    for (auto& s : paramSpecs())
    {
        if (s.id == "bypass" || s.id == "quality") continue;
        float v = s.def;
        for (auto& kv : pr.values) if (kv.first == s.id) v = kv.second;
        if (auto* p = apvts.getParameter (s.id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    }
    currentPreset = index;
    loadingPreset = false;
    presetModified = false;
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void Kick96Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    state.setProperty ("presetModified", presetModified.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void Kick96Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            loadingPreset = true;
            auto tree = juce::ValueTree::fromXml (*xml);
            currentPreset = (int) tree.getProperty ("preset", 2);
            apvts.replaceState (tree);
            presetModified = (bool) tree.getProperty ("presetModified", false);
            loadingPreset = false;
        }
}

juce::AudioProcessorEditor* Kick96Processor::createEditor() { return new Kick96Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Kick96Processor(); }
