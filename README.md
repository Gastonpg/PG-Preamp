# PG Preamp

PG Preamp is an original PG Audio VST3 project. Its main branch remains the stable baseline while this experimental branch develops a reusable physical-component DSP engine.

## Experimental branch: circuit-engine-foundation

This branch develops and tests physical component models independently of the plugin signal path.

- Resistor: Ohm's law, dissipation, temperature coefficients and Johnson-Nyquist noise.
- Capacitor: stored voltage/charge behavior, ESR and leakage.
- Inductor: inductance and winding resistance.
- Passive RC network: measured transient and frequency response.
- NPN transistor: static Ebers-Moll junction currents, KCL and SPICE-style temperature scaling.
- Windows x64 GitHub Actions build and standalone component tests.

These pieces are not yet connected into a complete preamp circuit and do not change the plugin's sound. The next circuit-level step is a tested MNA solver to connect the components.

## Build and test

Pushes to this branch run Build PG Preamp VST3. The workflow builds the VST3, compiles pg_component_tests, runs CTest, and packages PG-Preamp-Windows-x64.

No local compiler or SDK installation is required for the cloud build.

See docs/CIRCUIT_ENGINE.md and docs/REFERENCE_ARCHITECTURE.md for the modeling assumptions and planned architecture.

This is an original PG Audio design, not a claim of exact emulation of commercial hardware.
