# PG Preamp

PG Preamp is an original PG Audio VST3 project. Its main branch remains the stable baseline while this experimental branch develops a reusable physical-component DSP engine.

## Experimental branch: circuit-engine-foundation

This branch establishes a component-by-component workflow without changing the stable main branch.

- Native VST3 effect structure and Windows x64 cloud build.
- Circuit-engine code is separate from the VST3 wrapper.
- Passive RC network with analytical and response tests.
- Resistor Johnson-Nyquist noise model and deterministic test source.
- Current-driven capacitor model with ESR and leakage resistance.
- Linear inductor model with winding resistance.
- Static Ebers-Moll NPN transistor primitive with device-level tests.
- GitHub Actions compiles the plugin and runs standalone component tests.

These models are not connected to the Preamp audio path yet, so they do not change its sound.

## Build and test

Pushes to this branch run Build PG Preamp VST3. The workflow builds the VST3, compiles pg_component_tests, runs CTest, and packages the plugin as PG-Preamp-Windows-x64.

No local compiler or SDK installation is required for the cloud build.

## PG Circuit Engine principles

1. Model one component at a time using documented physical assumptions.
2. Express parameters in physical units where practical.
3. Test transient response, frequency response, stability and state behavior.
4. Measure CPU cost before real-time integration.
5. Connect validated components into networks and only then into the audio path.

See docs/CIRCUIT_ENGINE.md and docs/REFERENCE_ARCHITECTURE.md.

## Planned progression

1. Validate passive R/C/L elements and networks.
2. Calibrate active device models from data sheets/SPICE references.
3. Develop transformer models with explicit nonlinear and magnetic assumptions.
4. Design and validate the Preamp's active gain-stage topology.
5. Reuse the engine in PG Optical Compressor.

This project is an original PG Audio design, not a claim of exact emulation of commercial hardware.
