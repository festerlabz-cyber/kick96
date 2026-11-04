// Runs the C++ engine on a deterministic kick-like test signal with idx=value parameter pairs and writes L/R as float32;
// used to compare against prototype/dsp.js (same signal formula in the comparison script).
#include "../Source/Kick96DSP.h"
#include <cstdint>
#include <cstdio>
#include <string>
int main (int argc, char** argv)
{
    const int SR = 48000, n = SR * 2, beat = (int) (0.35 * SR);
    std::vector<float> x ((size_t) n), xr ((size_t) n), l ((size_t) n), r ((size_t) n);
    uint32_t seed = 12345; double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const int j = i % beat; const double t = j / (double) SR; if (j == 0) ph = 0;
        ph += 2.0 * M_PI * (50.0 + 180.0 * std::exp (-t / 0.03)) / SR;
        seed = seed * 1664525u + 1013904223u;
        const double noise = seed / 4294967296.0 * 2.0 - 1.0;
        x[(size_t) i] = (float) (0.8 * std::exp (-t / 0.25) * std::sin (ph) + 0.05 * noise * std::exp (-t / 0.01) + 0.08 * std::sin (2.0 * M_PI * 330.0 * i / SR));
        xr[(size_t) i] = x[(size_t) i] * 0.7f;
    }
    kk96::Engine e; e.prepare (SR);
    for (int a = 2; a < argc; ++a) { std::string s = argv[a]; auto p = s.find ('='); e.setParam (std::stoi (s.substr (0, p)), std::stod (s.substr (p + 1))); }
    for (int i = 0; i < n; i += 128) e.process (x.data() + i, xr.data() + i, l.data() + i, r.data() + i, std::min (128, n - i));
    FILE* f = std::fopen (argv[1], "wb"); std::fwrite (l.data(), 4, (size_t) n, f); std::fwrite (r.data(), 4, (size_t) n, f); std::fclose (f);
    return 0;
}
