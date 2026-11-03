# PG Preamp

PG Preamp is an original PG Audio VST3 project. Its main branch remains the stable baseline while this experimental branch develops a reusable physical-component DSP engine.

## Experimental branch: circuit-engine-foundation

This branch develops and tests physical component models independently of the plugin signal path.

- Resistor: Ohm's law, power dissipation, temperature coefficients and Johnson-Nyquist noise.
- Capacitor: stored charge, ESR and leakage.
- Inductor: inductance and winding resistance.
- Linear transformer: coupled windings, mutual inductance, polarity and winding resistance.
- Magnetic core: Preisach-type hysteresis, remanence, saturation and an eddy-loss proxy.
- Nonlinear transformer: winding resistance/leakage plus history-dependent core flux and Faraday-induced voltage.
- NPN transistor: Early effect, internal series resistances, depletion capacitances, transit-time charge and temperature scaling.
- Generic MNA reference solver and a fixed-topology, stack-only common-emitter audio-stage candidate.

Automated tests cover RLC responses, transistor Jacobians, transformer turns ratio, core hysteresis, DC bias, AC gain/phase and agreement between the generic and fixed-topology amplifier stages.

The generic MNA solver and nonlinear transformer are currently engineering/validation components, not wired into the VST3 audio path. The fixed common-emitter stage is a real-time candidate, not the final PG Preamp circuit.

## Build and test

The circuit tests compile directly with a C++17 compiler on Linux. The Windows workflow builds the VST3 and runs the same component/network tests.

No local compiler or SDK installation is required for the cloud Windows build.

See docs/CIRCUIT_ENGINE.md and docs/REFERENCE_ARCHITECTURE.md for model assumptions, limitations and next steps.

This is an original PG Audio design, not a claim of exact emulation of commercial hardware.
