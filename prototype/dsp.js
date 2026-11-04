// BoomTown Beats Kick 96 — DSP core (browser prototype).
// Same structure as the other 96 plug-ins: one self-contained function, serialised into the AudioWorklet and
// written to port 1:1 to C++.
//
// Signal flow:  in -> input gain -> transient shaper (click / tail) -> low cut -> punch EQ
//               -> [oversampled: stage 1 drive -> focus EQ -> stage 2 drive (mode) -> DC block]
//               -> high cut -> level compensation -> dry/wet mix (latency-aligned) + clean sub band -> ceiling -> output
function kickModule(){
  'use strict';
  const TAU = Math.PI * 2;
  const dbl = d => Math.pow(10, d / 20);
  const clampf = (v, a, b) => v < a ? a : (v > b ? b : v);
  function ft(x){ if (x > 3) return 1; if (x < -3) return -1; const x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }
  const hardk = (x, k) => x / Math.pow(1 + Math.pow(Math.abs(x), k), 1 / k);   // smooth hard clip, k = sharpness

  function setHP2(o, fs, f, q){
    f = clampf(f, 5, fs * 0.45);
    const w = TAU * f / fs, sn = Math.sin(w), cs = Math.cos(w), al = sn / (2 * q), a0 = 1 + al;
    o[0] = (1 + cs) / 2 / a0; o[1] = -(1 + cs) / a0; o[2] = o[0]; o[3] = -2 * cs / a0; o[4] = (1 - al) / a0;
  }
  function setLP2(o, fs, f, q){
    f = clampf(f, 20, fs * 0.45);
    const w = TAU * f / fs, sn = Math.sin(w), cs = Math.cos(w), al = sn / (2 * q), a0 = 1 + al;
    o[0] = (1 - cs) / 2 / a0; o[1] = (1 - cs) / a0; o[2] = o[0]; o[3] = -2 * cs / a0; o[4] = (1 - al) / a0;
  }
  function setPeakQ(o, fs, f, g, q){
    f = clampf(f, 10, fs * 0.45);
    const A = Math.pow(10, g / 40), w = TAU * f / fs, sn = Math.sin(w), cs = Math.cos(w), al = sn / (2 * q), a0 = 1 + al / A;
    o[0] = (1 + al * A) / a0; o[1] = -2 * cs / a0; o[2] = (1 - al * A) / a0; o[3] = -2 * cs / a0; o[4] = (1 - al / A) / a0;
  }
  function bq(c, s, x){ const y = c[0] * x + s[0]; s[0] = c[1] * x - c[3] * y + s[1]; s[1] = c[2] * x - c[4] * y; return y; }

  // ---------- Kaiser-windowed FIR oversampling: 129 / 257 taps -> exactly 64 samples latency at 2x and 4x ----------
  function besselI0(x){ let s = 1, t = 1; for (let k = 1; k < 32; k++){ const r = x / (2 * k); t *= r * r; s += t; } return s; }
  class Oversampler {
    constructor(f){
      this.f = f; if (f === 1) return;
      const nReal = f === 2 ? 129 : 257, N = Math.ceil(nReal / f) * f;
      const h = new Float64Array(N), fc = 0.5 / f * 0.9, beta = 7, m = (nReal - 1) / 2, i0b = besselI0(beta);
      let sum = 0;
      for (let n = 0; n < nReal; n++){
        const t = n - m, x = 2 * fc * t, sinc = t === 0 ? 1 : Math.sin(Math.PI * x) / (Math.PI * x), r = 2 * n / (nReal - 1) - 1;
        h[n] = 2 * fc * sinc * besselI0(beta * Math.sqrt(Math.max(0, 1 - r * r))) / i0b; sum += h[n];
      }
      for (let n = 0; n < N; n++) h[n] /= sum;
      this.N = N; this.nReal = nReal; this.h = h; this.L = N / f; this.ph = [];
      for (let p = 0; p < f; p++){ const a = new Float64Array(this.L); for (let j = 0; j < this.L; j++) a[j] = h[p + j * f] * f; this.ph.push(a); }
      this.ui = new Float64Array(this.L * 2); this.up = 0; this.di = new Float64Array(N * 2); this.dp = 0;
    }
    upsample(x, out){
      const L = this.L; this.up = (this.up + 1) % L; this.ui[this.up] = x; this.ui[this.up + L] = x;
      const b = this.up + L, ui = this.ui;
      for (let p = 0; p < this.f; p++){ const a = this.ph[p]; let s = 0; for (let j = 0; j < L; j++) s += a[j] * ui[b - j]; out[p] = s; }
    }
    push(x){ const N = this.N; this.dp = (this.dp + 1) % N; this.di[this.dp] = x; this.di[this.dp + N] = x; }
    // aligned to the first sub-sample of the group, so up + down is exactly 64 base-rate samples (h is zero past nReal)
    down(){ const h = this.h, di = this.di, b = this.dp + this.N - (this.f - 1); let s = 0; for (let k = 0; k < this.nReal; k++) s += h[k] * di[b - k]; return s; }
  }

  const MODES = ['Warm', 'Gabber', 'Industrial', 'Fuzz'];
  // level compensation per mode: comp dB = -(B + A * (1 - exp(-drive / 8))), measured on a 909 kick so the Drive knob
  // changes the character more than the loudness
  const COMP_B = [0.2, 1.5, 3.0, -1.6], COMP_A = [7.2, 6.0, -1.0, 5.9];
  const FT03 = ft(0.3);

  const DEFAULTS = { mode:1, quality:2, input:0, drive:18, punchF:60, punch:4, focusF:900, focus:6, click:0, tail:0,
                     subF:60, sub:0, high:18000, low:20, ceiling:0, out:0, mix:100, bypass:0 };
  const LOG = { punchF:1, focusF:1, subF:1, high:1, low:1 };
  const NOSMOOTH = { mode:1, quality:1 };
  const BLOCK = 32, DEC = 8;
  const latencyFor = os => os === 1 ? 0 : 64;

  class Engine {
    constructor(fs){
      this.fs = fs;
      this.tgt = Object.assign({}, DEFAULTS);
      this.cur = Object.assign({}, DEFAULTS);
      const c5 = () => new Float64Array(5), s2 = () => new Float64Array(2);
      this.cLow = c5(); this.cPunch = c5(); this.cHigh = c5(); this.cSub = c5(); this.cFocus = c5();
      this.sLow = [s2(), s2()]; this.sPunch = [s2(), s2()]; this.sHigh = [s2(), s2()];
      this.sSub1 = [s2(), s2()]; this.sSub2 = [s2(), s2()]; this.sFocus = [s2(), s2()];
      this.dcx = [0, 0]; this.dcy = [0, 0];
      this.dry = [new Float64Array(128), new Float64Array(128)]; this.subD = [new Float64Array(128), new Float64Array(128)];
      this.raw = [new Float64Array(128), new Float64Array(128)]; this.dw = 0;
      this.xs = new Float64Array(2); this.outs = new Float64Array(2);
      this.e = [0, 0, 0, 0];
      this.ub = new Float64Array(4);
      this.inPk = 0; this.outPk = 0; this.mCount = 0; this.decN = 0; this.wIn = []; this.wOut = [];
      this.onMeter = null;
      this.setQuality(this.cur.quality);
      this.design(); this.derive();
    }
    setParams(p){ Object.assign(this.tgt, p); }
    setQuality(q){
      this.os = q === 1 || q === 4 ? q : 2;
      this.fsOS = this.fs * this.os;
      this.U = [new Oversampler(this.os), new Oversampler(this.os)];
      this.D = [new Oversampler(this.os), new Oversampler(this.os)];
      this.sFocus = [new Float64Array(2), new Float64Array(2)]; this.dcx = [0, 0]; this.dcy = [0, 0];
      this.dcR = 1 - TAU * 5 / this.fsOS;
    }
    smooth(){
      const t = this.tgt, c = this.cur, a = 1 - Math.exp(-BLOCK / (this.fs * 0.025)); let moved = false;
      if ((t.quality | 0) !== this.os && (t.quality === 1 || t.quality === 2 || t.quality === 4)){ c.quality = t.quality; this.setQuality(t.quality); moved = true; }
      for (const k in t){
        const target = t[k], v = c[k]; if (v === target) continue;
        let nv;
        if (NOSMOOTH[k]) nv = target;
        else if (LOG[k]){ nv = v * Math.pow(target / v, a); if (Math.abs(nv / target - 1) < 1e-4) nv = target; }
        else { nv = v + (target - v) * a; if (Math.abs(nv - target) < 1e-5) nv = target; }
        c[k] = nv; moved = true;
      }
      return moved;
    }
    design(){
      const c = this.cur, fs = this.fs;
      setHP2(this.cLow, fs, c.low, 0.7071);
      setPeakQ(this.cPunch, fs, c.punchF, c.punch, 0.9);
      setLP2(this.cHigh, fs, c.high, 0.7071);
      setLP2(this.cSub, fs, c.subF, 0.7071);
      setPeakQ(this.cFocus, this.fsOS, c.focusF, c.focus, 1.1);
    }
    derive(){
      const c = this.cur, fs = this.fs;
      this.inG = dbl(c.input);
      this.d1 = dbl(c.drive * 0.45); this.d2 = dbl(c.drive * 0.55);
      const m = c.mode | 0; this.comp = dbl(-(COMP_B[m] + COMP_A[m] * (1 - Math.exp(-c.drive / 8))));
      this.mixW = c.mix / 100; this.subG = c.sub / 100 * 1.4;
      this.ceil = dbl(c.ceiling); this.outG = dbl(c.out); this.byp = c.bypass;
      this.click = c.click; this.tail = c.tail;
      this.lat = latencyFor(this.os);
      // envelope followers for the transient shaper
      const k = ms => 1 - Math.exp(-1 / (fs * ms / 1000));
      this.kA1 = k(0.5); this.kR1 = k(40); this.kA2 = k(25); this.kR2 = k(40); this.kA3 = k(0.5); this.kR3 = k(25); this.kR4 = k(250);
    }
    // the oversampled part: two drive stages around the focus EQ
    core(u, ch, mode){
      const d1 = this.d1, d2 = this.d2;
      let s1;
      if (mode === 2) s1 = hardk(u * d1, 8); else s1 = ft(u * d1);
      s1 = bq(this.cFocus, this.sFocus[ch], s1);
      const v = s1 * d2;
      let y;
      switch (mode){
        case 0: y = (ft(v + 0.3) - FT03); break;                                    // Warm: asymmetric soft
        case 1: y = hardk(v, 6); break;                                              // Gabber: hard
        case 2: { const w = clampf(v, -6, 6); y = 0.5 * Math.sin(Math.PI / 2 * w) + 0.5 * hardk(v, 8); break; } // Industrial: fold
        default: { const r = hardk(v, 4); y = 0.7 * r + 0.4 * Math.abs(r); }         // Fuzz: rectified
      }
      const d = y - this.dcx[ch] + this.dcR * this.dcy[ch]; this.dcx[ch] = y; this.dcy[ch] = d;
      return d;
    }
    // soft ceiling: linear up to 80 % of the ceiling, then rounds off into it
    ceiling(x){
      const c = this.ceil, k = 0.8 * c, a = Math.abs(x);
      if (a <= k) return x;
      return Math.sign(x) * (k + 0.2 * c * ft((a - k) / (0.2 * c)));
    }
    process(iL, iR, oL, oR, n){
      const ub = this.ub;
      for (let b0 = 0; b0 < n; b0 += BLOCK){
        const e = Math.min(n, b0 + BLOCK);
        if (this.smooth()) this.design();
        this.derive();
        const mode = this.cur.mode | 0, os = this.os, lat = this.lat;
        for (let s = b0; s < e; s++){
          const inL = iL[s], inR = iR[s];
          const pkIn = Math.max(Math.abs(inL), Math.abs(inR)); if (pkIn > this.inPk) this.inPk = pkIn;
          const xl = inL * this.inG, xr = inR * this.inG;
          // transient shaper, linked: fast vs slow envelopes
          const pk = Math.max(Math.abs(xl), Math.abs(xr)), E = this.e;
          E[0] += (pk > E[0] ? this.kA1 : this.kR1) * (pk - E[0]);
          E[1] += (pk > E[1] ? this.kA2 : this.kR2) * (pk - E[1]);
          E[2] += (pk > E[2] ? this.kA3 : this.kR3) * (pk - E[2]);
          E[3] += (pk > E[3] ? this.kA3 : this.kR4) * (pk - E[3]);
          let gdb = 0;
          if (this.click !== 0 || this.tail !== 0){
            const l = v => 20 * Math.log10(v + 1e-9);
            gdb = this.click * clampf((l(E[0]) - l(E[1])) / 9, 0, 1) + this.tail * clampf((l(E[3]) - l(E[2])) / 9, 0, 1);
          }
          const gts = dbl(gdb);
          const xs = this.xs, outs = this.outs; xs[0] = xl; xs[1] = xr;
          const wi = this.dw, ri = (this.dw - lat + 128) & 127;
          for (let ch = 0; ch < 2; ch++){
            const x = xs[ch];
            const sub = bq(this.cSub, this.sSub2[ch], bq(this.cSub, this.sSub1[ch], x));
            let m = bq(this.cLow, this.sLow[ch], x * gts);
            m = bq(this.cPunch, this.sPunch[ch], m);
            let y;
            if (os === 1) y = this.core(m, ch, mode);
            else { const U = this.U[ch], D = this.D[ch]; U.upsample(m, ub); for (let p = 0; p < os; p++) D.push(this.core(ub[p], ch, mode)); y = D.down(); }
            y = bq(this.cHigh, this.sHigh[ch], y) * this.comp;
            this.dry[ch][wi] = x; this.subD[ch][wi] = sub;
            const dryD = this.dry[ch][ri], subD = this.subD[ch][ri];
            let o = dryD + (y - dryD) * this.mixW + subD * this.subG;
            outs[ch] = this.ceiling(o) * this.outG;
          }
          // bypass: crossfade to the raw input delayed by the same latency
          this.raw[0][wi] = inL; this.raw[1][wi] = inR;
          const rl = this.raw[0][ri], rr = this.raw[1][ri];
          this.dw = (this.dw + 1) & 127;
          const yl = outs[0] + (rl - outs[0]) * this.byp, yr = outs[1] + (rr - outs[1]) * this.byp;
          oL[s] = yl; oR[s] = yr;
          const po = Math.max(Math.abs(yl), Math.abs(yr)); if (po > this.outPk) this.outPk = po;
          if (++this.decN >= DEC){ this.decN = 0; this.wIn.push(rl); this.wOut.push(yl); }
        }
        this.mCount += e - b0;
      }
      if (this.mCount >= this.fs / 30){
        this.mCount = 0;
        if (this.onMeter) this.onMeter({ in:this.inPk, out:this.outPk, wIn:this.wIn, wOut:this.wOut, dec:DEC });
        this.inPk = 0; this.outPk = 0; this.wIn = []; this.wOut = [];
      }
    }
  }
  return { Engine, DEFAULTS, MODES, latencyFor };
}
