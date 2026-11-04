// Offline check: runs a 909-style kick through the real plug-in processor (all presets), checks the reported latency
// against the measured one for each quality, a state round trip, and renders the editor to PNG.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double sr = 48000.0; const int block = 256;

    auto proc = std::make_unique<Kick96Processor>();
    proc->setPlayConfigDetails (2, 2, sr, block);
    proc->prepareToPlay (sr, block);

    // clean kick every 0.343 s (175 BPM): sine with a pitch sweep, peak about -1 dBFS
    const int beat = (int) (60.0 / 175.0 * sr);
    double ph = 0;
    auto input = [&] (int n) { const int j = n % beat; const double t = j / sr; if (j == 0) ph = 0;
                               ph += 2 * juce::MathConstants<double>::pi * (50 + 180 * std::exp (-t / 0.03)) / sr;
                               return (float) (0.9 * std::exp (-t / 0.35) * std::min (1.0, t / 0.002) * std::sin (ph)); };

    for (int preset = 0; preset < proc->getNumPrograms(); ++preset)
    {
        proc->setCurrentProgram (preset);
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        double peak = 0, sum = 0, inSum = 0; int cnt = 0; bool bad = false; int n = 0;
        for (int b = 0; b < (int) (sr * 4 / block); ++b)
        {
            for (int i = 0; i < block; ++i, ++n) { const float x = input (n); buf.setSample (0, i, x); buf.setSample (1, i, x); if (n > sr) inSum += x * x; }
            proc->processBlock (buf, midi);
            if (n > sr)
                for (int i = 0; i < block; ++i)
                    for (int ch = 0; ch < 2; ++ch) { const float y = buf.getSample (ch, i); if (! std::isfinite (y)) bad = true; peak = std::max (peak, (double) std::abs (y)); if (ch == 0) { sum += y * y; ++cnt; } }
        }
        std::printf ("%-18s rms %6.1f dB (in %6.1f)  peak %6.1f dBFS%s\n", proc->getProgramName (preset).toRawUTF8(),
                     10 * std::log10 (sum / cnt), 10 * std::log10 (inSum / cnt), 20 * std::log10 (peak), bad ? "  NaN!" : "");
    }

    // latency: with Drive 0, flat EQ and Mix 100 % an impulse comes out after exactly the reported latency
    for (int q = 0; q < 3; ++q)
    {
        proc->setCurrentProgram (0);
        auto set = [&] (const char* id, float v) { if (auto* p = proc->apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        set ("quality", (float) q); set ("drive", 0); set ("punch", 0); set ("focus", 0); set ("high", 20000); set ("mode", 1);
        proc->prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        std::vector<float> out;
        for (int b = 0; b < 4; ++b)
        {
            buf.clear(); if (b == 0) { buf.setSample (0, 10, 0.3f); buf.setSample (1, 10, 0.3f); }
            proc->processBlock (buf, midi);
            for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
        }
        int at = 0; for (int i = 0; i < (int) out.size(); ++i) if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) at])) at = i;
        std::printf ("quality %d: impulse peak after %d samples, reported latency %d\n", q, at - 10, proc->getLatencySamples());
    }
    if (auto* p = proc->apvts.getParameter ("quality")) p->setValueNotifyingHost (p->convertTo0to1 (1.0f));

    // state round trip
    proc->setCurrentProgram (8);
    juce::MemoryBlock mb; proc->getStateInformation (mb);
    proc->setCurrentProgram (0);
    proc->setStateInformation (mb.getData(), (int) mb.getSize());
    std::printf ("state restore: preset %d (%s), drive %.0f\n", proc->getCurrentProgram(), proc->getProgramName (proc->getCurrentProgram()).toRawUTF8(),
                 proc->apvts.getRawParameterValue ("drive")->load());

    proc->setCurrentProgram (2);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    for (float sc : { 1.0f, 2.0f })
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, sc);
        juce::PNGImageFormat png; auto f = outDir.getChildFile ("kick96_gui_" + juce::String ((int) sc) + "x.png"); f.deleteFile();
        juce::FileOutputStream os (f); png.writeImageToStream (img, os);
        std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
    return 0;
}
