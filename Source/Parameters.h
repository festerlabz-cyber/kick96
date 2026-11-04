// Parameter definitions, value formatting and factory presets.
// Parameter IDs match the browser prototype keys, so presets carry over 1:1.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Kick96DSP.h"

namespace kk96
{
enum class Unit { percent, pctOff, dB, dBPos, dBPlain, hz, toggle, choice };

struct ParamSpec
{
    juce::String id, name;
    int engineIndex;
    Unit unit;
    float min, max, def;
    bool logScale = false;
    juce::StringArray choices {};
};

inline juce::String formatHz (double v)
{
    if (juce::roundToInt (v) >= 1000) return juce::String (v / 1000.0, v >= 9995.0 ? 1 : 2) + " kHz";
    return juce::String (juce::roundToInt (v)) + " Hz";
}

inline juce::String formatValue (Unit unit, double v)
{
    switch (unit)
    {
        case Unit::percent: return juce::String (juce::roundToInt (v)) + " %";
        case Unit::pctOff:  return v < 0.5 ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " %";
        case Unit::dB:      return (v > 0.04 ? "+" : "") + juce::String (v, 1) + " dB";
        case Unit::dBPos:
        case Unit::dBPlain: return juce::String (v, 1) + " dB";
        case Unit::hz:      return formatHz (v);
        case Unit::toggle:  return v > 0.5 ? "On" : "Off";
        case Unit::choice:  break;
    }
    return juce::String (v);
}

inline const std::vector<ParamSpec>& paramSpecs()
{
    static const std::vector<ParamSpec> specs = []
    {
        std::vector<ParamSpec> s;
        s.push_back ({ "mode",    "Mode",        P::mode,    Unit::choice,  0, 3, 1, false, { "Warm", "Gabber", "Industrial", "Fuzz" } });
        s.push_back ({ "quality", "Quality",     P::quality, Unit::choice,  0, 2, 1, false, { "Eco", "Normal 2x", "High 4x" } });
        s.push_back ({ "input",   "Input",       P::input,   Unit::dB,      -12, 24, 0 });
        s.push_back ({ "drive",   "Drive",       P::drive,   Unit::dBPos,   0, 48, 18 });
        s.push_back ({ "punchF",  "Punch Freq",  P::punchF,  Unit::hz,      30, 250, 60, true });
        s.push_back ({ "punch",   "Punch Boost", P::punch,   Unit::dBPos,   0, 18, 4 });
        s.push_back ({ "focusF",  "Focus Freq",  P::focusF,  Unit::hz,      150, 3000, 900, true });
        s.push_back ({ "focus",   "Focus Boost", P::focus,   Unit::dBPos,   0, 18, 6 });
        s.push_back ({ "click",   "Click",       P::click,   Unit::dB,      -12, 12, 0 });
        s.push_back ({ "tail",    "Tail",        P::tail,    Unit::dB,      -12, 12, 0 });
        s.push_back ({ "subF",    "Sub Freq",    P::subF,    Unit::hz,      30, 150, 60, true });
        s.push_back ({ "sub",     "Sub Level",   P::sub,     Unit::pctOff,  0, 100, 0 });
        s.push_back ({ "high",    "High Cut",    P::high,    Unit::hz,      2000, 20000, 18000, true });
        s.push_back ({ "low",     "Low Cut",     P::low,     Unit::hz,      20, 300, 20, true });
        s.push_back ({ "ceiling", "Ceiling",     P::ceiling, Unit::dBPlain, -12, 0, 0 });
        s.push_back ({ "out",     "Output",      P::out,     Unit::dB,      -24, 12, 0 });
        s.push_back ({ "mix",     "Mix",         P::mix,     Unit::percent, 0, 100, 100 });
        s.push_back ({ "bypass",  "Bypass",      P::bypass,  Unit::toggle,  0, 1, 0 });
        return s;
    }();
    return specs;
}

inline const ParamSpec* findSpec (const juce::String& id)
{
    for (auto& s : paramSpecs()) if (s.id == id) return &s;
    return nullptr;
}

inline juce::NormalisableRange<float> makeRange (const ParamSpec& s)
{
    if (s.logScale)
        return { s.min, s.max,
                 [] (float a, float b, float x) { return a * std::pow (b / a, x); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 nullptr };
    return { s.min, s.max };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& s : paramSpecs())
    {
        const juce::ParameterID pid { s.id, 1 };
        if (s.unit == Unit::toggle)
            layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f));
        else if (s.unit == Unit::choice)
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, s.choices, (int) s.def));
        else
        {
            const auto unit = s.unit;
            auto attrs = juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([unit] (float v, int) { return formatValue (unit, v); })
                .withValueFromStringFunction ([unit] (const juce::String& t)
                {
                    auto v = t.retainCharacters ("-0123456789.").getFloatValue();
                    if (unit == Unit::hz && t.containsIgnoreCase ("k")) v *= 1000.0f;
                    return v;
                });
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, makeRange (s), s.def, attrs));
        }
    }
    return layout;
}

// choice parameters arrive as indices; the quality index maps to the oversampling factor
inline double engineValue (const ParamSpec& s, float raw)
{
    if (s.engineIndex == P::quality) return raw < 0.5f ? 1.0 : (raw < 1.5f ? 2.0 : 4.0);
    return raw;
}

//==============================================================================
struct Preset { juce::String name; std::vector<std::pair<juce::String, float>> values; };

// Choice values are indices. "bypass" and "quality" are never changed by presets; anything not listed goes to its default.
inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets {
        { "Init", {} },
        { "Clean Punch",      { {"mode",0},{"drive",6},{"punchF",55},{"punch",5},{"focusF",1200},{"focus",3},{"click",3} } },
        { "909 Gabber",       { {"mode",1},{"drive",30},{"punchF",60},{"punch",6},{"focusF",700},{"focus",9},{"click",2},{"tail",2},{"subF",55},{"sub",30},{"high",9000},{"low",30} } },
        { "Early Rave Kick",  { {"mode",0},{"drive",18},{"punchF",50},{"punch",6},{"focusF",1500},{"focus",5},{"tail",-3},{"high",10000} } },
        { "Industrial Stomp", { {"mode",2},{"drive",26},{"punchF",70},{"punch",4},{"focusF",450},{"focus",10},{"click",4},{"sub",25},{"high",7000},{"low",35} } },
        { "Distorted Tail",   { {"mode",1},{"drive",36},{"punchF",55},{"punch",3},{"focusF",900},{"focus",8},{"click",-4},{"tail",9},{"subF",50},{"sub",40},{"high",8000},{"low",30},{"ceiling",-1} } },
        { "Fuzz Thump",       { {"mode",3},{"drive",22},{"punchF",65},{"punch",5},{"focusF",600},{"focus",6},{"sub",20},{"high",6000} } },
        { "Parallel Crunch",  { {"mode",1},{"drive",28},{"focusF",1800},{"focus",8},{"mix",45} } },
        { "Hakkuh Hammer",    { {"mode",1},{"drive",48},{"input",3},{"punchF",58},{"punch",9},{"focusF",850},{"focus",14},{"click",6},{"tail",10},{"subF",50},{"sub",50},{"high",7500},{"low",28},{"ceiling",-0.5f},{"out",-2} } },
    };
    return presets;
}
} // namespace kk96
