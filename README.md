# PG Preamp

PG Preamp is an original PG Audio VST3 project. Its main branch remains the stable transparent baseline while this experimental branch develops a physical-component DSP engine and a first audible circuit stage.

## Experimental branch: circuit-engine-foundation

### Experimental VST3 build

The Windows x64 VST3 currently contains one fixed-topology common-emitter stage per audio channel. It exposes generic host controls:

- **Bypass**
- **Input:** -12 to +18 dB, default 0 dB
- **Output:** -12 to +12 dB, default 0 dB

The processor saves and restores these three parameter values with VST3 state. The fixed stage is prepared outside the audio callback and processes samples with fixed-size stack arrays; it does not allocate heap memory or invoke the generic MNA solver from the callback.

The transistor values are provisional engineering parameters, not a calibrated BC184C model. The experimental plugin currently does **not** include the nonlinear transformer in its audio path, and the current common-emitter circuit is not the final PG Preamp topology or a claim of exact 1073 emulation.

### Circuit engine

- **Resistor:** Ohm's law, power dissipation, temperature coefficients and Johnson-Nyquist noise.
- **Capacitor:** stored charge, ESR and leakage.
- **Inductor:** inductance and winding resistance.
- **Linear transformer:** coupled windings, mutual inductance, polarity and winding resistance.
- **Magnetic core:** Preisach-type hysteresis, remanence, saturation and an eddy-loss proxy.
- **Nonlinear transformer:** winding resistance/leakage plus history-dependent core flux and Faraday-induced voltage.
- **NPN transistor:** Early effect, internal series resistances, depletion capacitances, transit-time charge and temperature scaling.
- **Generic MNA reference solver:** combines R, C, L, sources, coupled windings and nonlinear NPN devices using Newton iterations.
- **Fixed common-emitter stage:** a 7-unknown, stack-only solver optimized for the current fixed topology.

Automated tests cover RLC responses, transistor current/charge Jacobians, transformer turns ratio, core hysteresis, DC bias, AC gain/phase, fixed-stage agreement with generic MNA, and operation at 44.1/48/96/192 kHz.

### Build and test

The circuit tests compile directly with a C++17 compiler on Linux. The Windows workflow builds the VST3, runs the 51 component/network tests, runs the Steinberg VST3 validator and packages the plugin as `PG-Preamp-Windows-x64.zip`.

The latest successful Windows build is available under Actions:
[Build PG Preamp VST3 — successful run](https://github.com/Gastonpg/PG-Preamp/actions/runs/41570556094)

No local compiler or SDK installation is required for the cloud Windows build.

See [docs/CIRCUIT_ENGINE.md](docs/CIRCUIT_ENGINE.md) and [docs/REFERENCE_ARCHITECTURE.md](docs/REFERENCE_ARCHITECTURE.md) for model assumptions, limitations and next steps.

This is an original PG Audio design, not a claim of exact emulation of commercial hardware.
