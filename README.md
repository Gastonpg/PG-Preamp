# PG Preamp

PG Preamp is an original PG Audio VST3 project. Its main branch remains the stable baseline while this experimental branch develops a reusable physical-component DSP engine.

## Experimental branch: circuit-engine-foundation

This branch develops and tests physical component models independently of the plugin signal path.

- Resistor: Ohm's law, power dissipation, temperature coefficients and Johnson-Nyquist noise.
- Capacitor: stored voltage/charge behavior, ESR and leakage.
- Inductor: inductance and winding resistance.
- Coupled windings: mutual inductance, polarity, separate winding resistance and a loaded turns-ratio test.
- NPN transistor: static Ebers-Moll junction currents, temperature scaling and analytic conductance.
- MNA solver: combines R, C, L, sources, coupled windings and nonlinear NPN devices using Newton iterations.
- Verified RC/RL transients, transformer turns ratio, transistor DC bias and an AC-coupled common-emitter test stage.
- Linux circuit-test workflow plus Windows x64 VST3 build workflow.

The network solver is currently an offline validation tool, not part of the plugin's real-time audio path. The common-emitter and coupled-winding circuits are engineering test fixtures, not the final PG Preamp topology or a claim of exact hardware emulation.

## Build and test

The circuit tests compile directly with a C++17 compiler on Linux. The Windows workflow builds the VST3 and runs the same component/network tests.

No local compiler or SDK installation is required for the cloud Windows build.

See docs/CIRCUIT_ENGINE.md and docs/REFERENCE_ARCHITECTURE.md for the physical assumptions, limitations and next milestones.

This is an original PG Audio design, not a claim of exact emulation of commercial hardware.
