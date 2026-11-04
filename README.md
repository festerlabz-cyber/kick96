# BoomTown Beats — Kick 96

Distortion speciaal voor de kick, als **Audio Unit** (Logic Pro) en **VST3**.
Gebouwd met [JUCE](https://juce.com) 9.0.4. De klank is één op één overgezet uit het browserprototype in `prototype/`.

## Downloaden en installeren (Mac)

1. Ga naar het tabblad **Actions** van deze repository en open de bovenste groene run *Build macOS (AU + VST3)*.
2. Download onderaan bij **Artifacts** het bestand `Kick96-…`. Je krijgt een zip; dubbelklik die om de `.dmg` te krijgen.
3. Open de `.dmg`.
4. Sluit Logic Pro, open **Terminal** en plak:

   ```
   bash "/Volumes/Kick 96/install.sh"
   ```

5. Start Logic Pro. De plugin staat bij **Audio FX → Audio Units → BoomTown Beats → Kick 96**.

## Wat de plugin doet

- **Drive** (0–48 dB) stuurt twee distortion-trappen in oversampling. **Mode**: Warm, Gabber, Industrial of Fuzz.
  Het niveau blijft ongeveer gelijk als je Drive opendraait.
- **Input** en **Mix** (parallel, met latency-gecompenseerd droog signaal).
- **Punch**: boost van het lage lichaam vóór de distortion. **Focus**: welk gebied tussen de trappen het hardst kraakt.
- **Shape**: Click (aanslag) en Tail (staart), ±12 dB. **Sub**: schone sub onder de distortion.
- **Tone**: High en Low cut. **Out**: Ceiling (zachte begrenzing) en Output.
- **Quality**: Eco (geen latency), Normal 2× of High 4× (64 samples latency, Logic compenseert dit automatisch).
- 9 presets, waaronder *909 Gabber*, *Industrial Stomp* en *Hakkuh Hammer*.

## Zelf bouwen

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
```

## Licenties

- JUCE: dual-licensed (AGPLv3 of de JUCE 9-licentie); gratis Starter-licentie zolang de omzet onder $20.000 per jaar blijft.
- Fonts: Anton (OFL), Inter (OFL), JetBrains Mono (OFL); licenties staan in `Resources/fonts/`.
