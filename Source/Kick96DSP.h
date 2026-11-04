// BoomTown Beats Kick 96 — DSP engine.
// Straight C++ port of the browser prototype (prototype/dsp.js). No JUCE dependency, so it can be tested on its own.
// Per-sample double precision; all buffers are allocated in prepare(), nothing in process().
//
// Signal flow:  in -> input gain -> transient shaper (click / tail) -> low cut -> punch EQ
//               -> [oversampled: stage 1 drive -> focus EQ -> stage 2 drive (mode) -> DC block]
//               -> high cut -> level compensation -> dry/wet mix (latency-aligned) + clean sub band -> ceiling -> output
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace kk96
{
constexpr double kTau = 6.283185307179586476925286766559;
constexpr double kPi  = 3.1415926535897932384626433832795;

inline double dbl (double d) noexcept { return std::pow (10.0, d / 20.0); }
inline double clampd (double v, double a, double b) noexcept { return v < a ? a : (v > b ? b : v); }
inline double ft (double x) noexcept
{
    if (x > 3.0) return 1.0;
    if (x < -3.0) return -1.0;
    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}
// smooth hard clip, k = sharpness
inline double hardk (double x, double k) noexcept { return x / std::pow (1.0 + std::pow (std::abs (x), k), 1.0 / k); }

//==============================================================================
namespace P
{
    enum Id { mode, quality, input, drive, punchF, punch, focusF, focus, click, tail, subF, sub, high, low, ceiling, out,
              mix, bypass, total };
}

struct ParamInfo { double def; bool logSmooth; bool noSmooth; };
inline const std::array<ParamInfo, P::total>& paramInfo()
{
    static const std::array<ParamInfo, P::total> t {{
        { 1, false, true },       // mode (Warm, Gabber, Industrial, Fuzz)
        { 2, false, true },       // quality = oversampling factor 1 / 2 / 4
        { 0, false, false },      // input dB
        { 18, false, false },     // drive dB
        { 60, true, false },      // punch freq Hz
        { 4, false, false },      // punch boost dB
        { 900, true, false },     // focus freq Hz
        { 6, false, false },      // focus boost dB
        { 0, false, false },      // click dB
        { 0, false, false },      // tail dB
        { 60, true, false },      // sub freq Hz
        { 0, false, false },      // sub level %
        { 18000, true, false },   // high cut Hz
        { 20, true, false },      // low cut Hz
        { 0, false, false },      // ceiling dB
        { 0, false, false },      // output dB
        { 100, false, false },    // mix %
        { 0, false, false },      // bypass
    }};
    return t;
}

constexpr const char* kModeNames[4] = { "Warm", "Gabber", "Industrial", "Fuzz" };
// level compensation per mode: comp dB = -(B + A * (1 - exp(-drive / 8))), measured on a 909 kick
constexpr double kCompB[4] = { 0.2, 1.5, 3.0, -1.6 };
constexpr double kCompA[4] = { 7.2, 6.0, -1.0, 5.9 };

//==============================================================================
using Coef  = std::array<double, 5>;
using State = std::array<double, 2>;
inline double bq (const Coef& c, State& s, double x) noexcept
{
    const double y = c[0] * x + s[0];
    s[0] = c[1] * x - c[3] * y + s[1];
    s[1] = c[2] * x - c[4] * y;
    return y;
}
inline void setHP2 (Coef& o, double fs, double f, double q) noexcept
{
    f = clampd (f, 5.0, fs * 0.45);
    const double w = kTau * f / fs, sn = std::sin (w), cs = std::cos (w), al = sn / (2.0 * q), a0 = 1.0 + al;
    o = { (1.0 + cs) / 2.0 / a0, -(1.0 + cs) / a0, (1.0 + cs) / 2.0 / a0, -2.0 * cs / a0, (1.0 - al) / a0 };
}
inline void setLP2 (Coef& o, double fs, double f, double q) noexcept
{
    f = clampd (f, 20.0, fs * 0.45);
    const double w = kTau * f / fs, sn = std::sin (w), cs = std::cos (w), al = sn / (2.0 * q), a0 = 1.0 + al;
    o = { (1.0 - cs) / 2.0 / a0, (1.0 - cs) / a0, (1.0 - cs) / 2.0 / a0, -2.0 * cs / a0, (1.0 - al) / a0 };
}
inline void setPeakQ (Coef& o, double fs, double f, double g, double q) noexcept
{
    f = clampd (f, 10.0, fs * 0.45);
    const double A = std::pow (10.0, g / 40.0), w = kTau * f / fs, sn = std::sin (w), cs = std::cos (w), al = sn / (2.0 * q), a0 = 1.0 + al / A;
    o = { (1.0 + al * A) / a0, -2.0 * cs / a0, (1.0 - al * A) / a0, -2.0 * cs / a0, (1.0 - al / A) / a0 };
}

//==============================================================================
// Kaiser-windowed FIR oversampling (polyphase up, decimating down). 129 / 257 taps: up + down is exactly 64 base-rate
// samples at 2x and 4x. down() is aligned to the first sub-sample of each group, which is what makes it exact.
inline double besselI0 (double x) noexcept
{
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 32; ++k) { const double r = x / (2.0 * k); t *= r * r; s += t; }
    return s;
}

class Oversampler
{
public:
    void prepare (int factor)
    {
        f = factor;
        if (f == 1) return;
        nReal = (f == 2) ? 129 : 257;
        N = ((nReal + f - 1) / f) * f;
        h.assign ((size_t) N, 0.0);
        const double fc = 0.5 / f * 0.9, beta = 7.0, m = (nReal - 1) / 2.0, i0b = besselI0 (beta);
        double sum = 0.0;
        for (int n = 0; n < nReal; ++n)
        {
            const double t = n - m, x = 2.0 * fc * t;
            const double sinc = (t == 0.0) ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double r = 2.0 * n / (nReal - 1) - 1.0;
            h[(size_t) n] = 2.0 * fc * sinc * besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            sum += h[(size_t) n];
        }
        for (auto& v : h) v /= sum;
        L = N / f;
        ph.assign ((size_t) f, std::vector<double> ((size_t) L, 0.0));
        for (int p = 0; p < f; ++p)
            for (int j = 0; j < L; ++j)
                ph[(size_t) p][(size_t) j] = h[(size_t) (p + j * f)] * f;
        ui.assign ((size_t) L * 2, 0.0);
        di.assign ((size_t) N * 2, 0.0);
        reset();
    }

    void reset() noexcept
    {
        std::fill (ui.begin(), ui.end(), 0.0);
        std::fill (di.begin(), di.end(), 0.0);
        up = 0; dp = 0;
    }

    void upsample (double x, double* out) noexcept
    {
        up = (up + 1) % L;
        ui[(size_t) up] = x; ui[(size_t) (up + L)] = x;
        const double* u = ui.data() + up + L;
        for (int p = 0; p < f; ++p)
        {
            const double* a = ph[(size_t) p].data();
            double s = 0.0;
            for (int j = 0; j < L; ++j) s += a[j] * u[-j];
            out[p] = s;
        }
    }

    void push (double x) noexcept
    {
        dp = (dp + 1) % N;
        di[(size_t) dp] = x; di[(size_t) (dp + N)] = x;
    }

    double down() const noexcept
    {
        const double* d = di.data() + dp + N - (f - 1);
        double s = 0.0;
        for (int k = 0; k < nReal; ++k) s += h[(size_t) k] * d[-k];
        return s;
    }

private:
    int f = 1, nReal = 0, N = 0, L = 0, up = 0, dp = 0;
    std::vector<double> h, ui, di;
    std::vector<std::vector<double>> ph;
};

//==============================================================================
class Engine
{
public:
    static int latencyFor (int os) noexcept { return os == 1 ? 0 : 64; }

    Engine()
    {
        const auto& info = paramInfo();
        for (int i = 0; i < P::total; ++i) tgt[(size_t) i] = cur[(size_t) i] = info[(size_t) i].def;
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        sa = 1.0 - std::exp (-kBlock / (fs * 0.025));
        for (int k = 0; k < 3; ++k)
            for (int s = 0; s < 2; ++s) { U[k][s].prepare (1 << k); D[k][s].prepare (1 << k); }
        setQuality ((int) tgt[P::quality]);
        reset();
    }

    void reset()
    {
        for (int i = 0; i < P::total; ++i) cur[(size_t) i] = tgt[(size_t) i];
        for (int k = 0; k < 3; ++k) for (int s = 0; s < 2; ++s) { U[k][s].reset(); D[k][s].reset(); }
        for (auto* arr : { sLow, sPunch, sHigh, sSub1, sSub2, sFocus }) { arr[0] = {}; arr[1] = {}; }
        dcx[0] = dcx[1] = dcy[0] = dcy[1] = 0.0;
        for (auto& b : dry) b.fill (0.0);
        for (auto& b : subD) b.fill (0.0);
        for (auto& b : raw) b.fill (0.0);
        dw = 0;
        env.fill (0.0);
        design(); derive();
    }

    void setParam (int index, double value) noexcept
    {
        tgt[(size_t) index] = value;
        if (index == P::quality && (int) value != os) setQuality ((int) value);
    }

    int getOversampling() const noexcept { return os; }
    int getLatency() const noexcept { return latencyFor (os); }

    // meters, read and cleared by the caller after process()
    double inPeak = 0, outPeak = 0;

    void process (const float* iL, const float* iR, float* oL, float* oR, int n) noexcept
    {
        double ub[4];
        for (int b0 = 0; b0 < n; b0 += kBlock)
        {
            const int e = std::min (n, b0 + kBlock);
            if (smooth()) design();
            derive();
            const int mode = (int) cur[P::mode];
            const int lat = latencyFor (os);
            const int k = os == 1 ? 0 : (os == 2 ? 1 : 2);
            for (int s = b0; s < e; ++s)
            {
                const double inL = iL[s], inR = iR[s];
                inPeak = std::max (inPeak, std::max (std::abs (inL), std::abs (inR)));
                const double xs[2] = { inL * inG, inR * inG };

                // transient shaper, linked: fast vs slow envelopes
                const double pk = std::max (std::abs (xs[0]), std::abs (xs[1]));
                env[0] += (pk > env[0] ? kA1 : kR1) * (pk - env[0]);
                env[1] += (pk > env[1] ? kA2 : kR2) * (pk - env[1]);
                env[2] += (pk > env[2] ? kA3 : kR3) * (pk - env[2]);
                env[3] += (pk > env[3] ? kA3 : kR4) * (pk - env[3]);
                double gdb = 0.0;
                if (clickDb != 0.0 || tailDb != 0.0)
                {
                    auto l = [] (double v) { return 20.0 * std::log10 (v + 1e-9); };
                    gdb = clickDb * clampd ((l (env[0]) - l (env[1])) / 9.0, 0.0, 1.0)
                        + tailDb * clampd ((l (env[3]) - l (env[2])) / 9.0, 0.0, 1.0);
                }
                const double gts = dbl (gdb);

                const int wi = dw, ri = (dw - lat + 128) & 127;
                double outs[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double x = xs[ch];
                    const double sub = bq (cSub, sSub2[ch], bq (cSub, sSub1[ch], x));
                    double m = bq (cLow, sLow[ch], x * gts);
                    m = bq (cPunch, sPunch[ch], m);
                    double y;
                    if (os == 1) y = core (m, ch, mode);
                    else
                    {
                        U[k][ch].upsample (m, ub);
                        for (int p = 0; p < os; ++p) D[k][ch].push (core (ub[p], ch, mode));
                        y = D[k][ch].down();
                    }
                    y = bq (cHigh, sHigh[ch], y) * comp;
                    dry[(size_t) ch][(size_t) wi] = x; subD[(size_t) ch][(size_t) wi] = sub;
                    const double dryD = dry[(size_t) ch][(size_t) ri], subDl = subD[(size_t) ch][(size_t) ri];
                    const double o = dryD + (y - dryD) * mixW + subDl * subG;
                    outs[ch] = ceiling (o) * outG;
                }
                // bypass: crossfade to the raw input delayed by the same latency
                raw[0][(size_t) wi] = inL; raw[1][(size_t) wi] = inR;
                const double rl = raw[0][(size_t) ri], rr = raw[1][(size_t) ri];
                dw = (dw + 1) & 127;
                const double yl = outs[0] + (rl - outs[0]) * byp, yr = outs[1] + (rr - outs[1]) * byp;
                oL[s] = (float) yl; oR[s] = (float) yr;
                outPeak = std::max (outPeak, std::max (std::abs (yl), std::abs (yr)));
            }
        }
    }

private:
    static constexpr int kBlock = 32;

    double fs = 48000.0, fsOS = 96000.0, sa = 0.0;
    int os = 2;
    std::array<double, P::total> tgt {}, cur {};
    Oversampler U[3][2], D[3][2];
    Coef cLow {}, cPunch {}, cHigh {}, cSub {}, cFocus {};
    State sLow[2] {}, sPunch[2] {}, sHigh[2] {}, sSub1[2] {}, sSub2[2] {}, sFocus[2] {};
    double dcx[2] {}, dcy[2] {}, dcR = 0.0;
    std::array<std::array<double, 128>, 2> dry {}, subD {}, raw {};
    int dw = 0;
    std::array<double, 4> env {};
    double inG = 1, d1 = 1, d2 = 1, comp = 1, mixW = 1, subG = 0, ceil = 1, outG = 1, byp = 0, clickDb = 0, tailDb = 0;
    double kA1 = 0, kR1 = 0, kA2 = 0, kR2 = 0, kA3 = 0, kR3 = 0, kR4 = 0;

    void setQuality (int q)
    {
        os = (q == 1 || q == 2 || q == 4) ? q : 2;
        cur[P::quality] = os;
        fsOS = fs * os;
        const int k = os == 1 ? 0 : (os == 2 ? 1 : 2);
        for (int s = 0; s < 2; ++s) { U[k][s].reset(); D[k][s].reset(); }
        sFocus[0] = {}; sFocus[1] = {};
        dcx[0] = dcx[1] = dcy[0] = dcy[1] = 0.0;
        dcR = 1.0 - kTau * 5.0 / fsOS;
        design();
    }

    bool smooth() noexcept
    {
        const auto& info = paramInfo();
        bool moved = false;
        for (int k = 0; k < P::total; ++k)
        {
            if (k == P::quality) continue;
            const auto& pi = info[(size_t) k];
            const double target = tgt[(size_t) k], v = cur[(size_t) k];
            if (v == target) continue;
            double nv;
            if (pi.noSmooth) nv = target;
            else if (pi.logSmooth) { nv = v * std::pow (target / v, sa); if (std::abs (nv / target - 1.0) < 1e-4) nv = target; }
            else { nv = v + (target - v) * sa; if (std::abs (nv - target) < 1e-5) nv = target; }
            cur[(size_t) k] = nv;
            moved = true;
        }
        return moved;
    }

    void design() noexcept
    {
        setHP2 (cLow, fs, cur[P::low], 0.7071);
        setPeakQ (cPunch, fs, cur[P::punchF], cur[P::punch], 0.9);
        setLP2 (cHigh, fs, cur[P::high], 0.7071);
        setLP2 (cSub, fs, cur[P::subF], 0.7071);
        setPeakQ (cFocus, fsOS, cur[P::focusF], cur[P::focus], 1.1);
    }

    void derive() noexcept
    {
        inG = dbl (cur[P::input]);
        d1 = dbl (cur[P::drive] * 0.45); d2 = dbl (cur[P::drive] * 0.55);
        const int m = std::clamp ((int) cur[P::mode], 0, 3);
        comp = dbl (-(kCompB[m] + kCompA[m] * (1.0 - std::exp (-cur[P::drive] / 8.0))));
        mixW = cur[P::mix] / 100.0; subG = cur[P::sub] / 100.0 * 1.4;
        ceil = dbl (cur[P::ceiling]); outG = dbl (cur[P::out]); byp = cur[P::bypass];
        clickDb = cur[P::click]; tailDb = cur[P::tail];
        auto k = [this] (double ms) { return 1.0 - std::exp (-1.0 / (fs * ms / 1000.0)); };
        kA1 = k (0.5); kR1 = k (40); kA2 = k (25); kR2 = k (40); kA3 = k (0.5); kR3 = k (25); kR4 = k (250);
    }

    // the oversampled part: two drive stages around the focus EQ
    double core (double u, int ch, int mode) noexcept
    {
        double s1 = mode == 2 ? hardk (u * d1, 8.0) : ft (u * d1);
        s1 = bq (cFocus, sFocus[ch], s1);
        const double v = s1 * d2;
        double y;
        switch (mode)
        {
            case 0:  y = ft (v + 0.3) - ft (0.3); break;                                                       // Warm
            case 1:  y = hardk (v, 6.0); break;                                                                // Gabber
            case 2:  { const double w = clampd (v, -6.0, 6.0); y = 0.5 * std::sin (kPi / 2.0 * w) + 0.5 * hardk (v, 8.0); break; } // Industrial
            default: { const double r = hardk (v, 4.0); y = 0.7 * r + 0.4 * std::abs (r); }                   // Fuzz
        }
        const double d = y - dcx[ch] + dcR * dcy[ch]; dcx[ch] = y; dcy[ch] = d;
        return d;
    }

    // soft ceiling: linear up to 80 % of the ceiling, then rounds off into it
    double ceiling (double x) const noexcept
    {
        const double c = ceil, kk = 0.8 * c, a = std::abs (x);
        if (a <= kk) return x;
        return (x < 0 ? -1.0 : 1.0) * (kk + 0.2 * c * ft ((a - kk) / (0.2 * c)));
    }
};
} // namespace kk96
