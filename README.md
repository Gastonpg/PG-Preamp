# PG Preamp

PG Preamp is an original PG Audio VST3 project. The audio path remains transparent while the circuit models are developed and validated independently.

## Experimental branch: circuit-engine-foundation

This branch starts the PG Audio component-by-component modeling workflow without changing the stable `main` branch.

- Native VST3 effect structure and Windows x64 cloud build.
- Independent circuit-engine code, separate from the VST3 wrapper.
- First physical building block: passive series-R / shunt-C stage.
- Standalone C++ tests for physical time constant, cutoff frequency, step response, reset, and sample-rate stability.
- GitHub Actions compiles the plugin and runs the component tests.

The first RC stage is an engineering foundation, not an added audible coloration in the Preamp yet.

## Build and test

Pushes to this branch run **Build PG Preamp VST3**. The workflow builds the VST3, compiles `pg_component_tests`, runs CTest, and packages the plugin as `PG-Preamp-Windows-x64`.

No local compiler or SDK installation is required for the cloud build.

## PG Circuit Engine principles

1. Model one component at a time using documented physical assumptions.
2. Express parameters in physical units where practical.
3. Test transient response, frequency response, stability, and state behavior.
4. Measure CPU cost before real-time integration.
5. Connect validated components into networks and only then into the audio path.

See [docs/CIRCUIT_ENGINE.md](docs/CIRCUIT_ENGINE.md) for the first component and the development roadmap.

## Planned progression

1. Validate passive R/C elements and networks.
2. Develop transformer models with explicit nonlinear and magnetic assumptions.
3. Define and model the Preamp's active gain-stage topology.
4. Integrate each validated stage into the audio path and compare against reference behavior.
5. Reuse the engine in PG Optical Compressor.

This project is an original PG Audio design, not a claim of exact emulation of commercial hardware.
