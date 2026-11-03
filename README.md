# PG Preamp

PG Preamp is an original PG Audio VST3 project. The stable `main` branch remains untouched while this experimental branch develops the component-based DSP engine and its first audible signal path.

## Experimental first version

The Windows x64 VST3 contains two independently switchable blocks per channel:

- **A Stage:** a fixed common-emitter transistor circuit with a solved DC operating point, dynamic junction charge and coupling capacitors.
- **Iron:** a loaded two-winding input transformer model with winding resistance, leakage inductance, magnetizing current, a smooth stateful magnetic core and Faraday-induced winding voltage.

The temporary rack-style editor exposes:

- **Bypass**
- **Input:** -12 to +18 dB, default 0 dB
- **Output:** -12 to +12 dB, default 0 dB
- **A Stage:** enabled by default
- **Iron:** disabled by default

A Stage and Iron can be auditioned separately or together. The state stream saves all five controls. Older three-value project states remain readable; they load with A Stage enabled and Iron disabled.

The Iron block is a reduced-order real-time core model. It is deliberately not the same as feeding the discrete Preisach relay ensemble directly into the audio callback: its smooth dynamic magnetic law avoids discontinuous flux steps at sample boundaries. The detailed Preisach core and current-driven transformer remain available as offline reference models. The real-time core still needs calibration against a selected transformer and measured B-H / impedance data.

The transistor parameters and transformer values are provisional engineering defaults, not calibrated BC184C or commercial transformer models. This is an original experimental circuit design, not an exact 1073 emulation.

## Real-time and validation notes

- The fixed common-emitter stage uses a fixed-size Newton solve and stack arrays; it has no heap allocation or general MNA solver in its sample method.
- The fixed transformer uses a scalar fixed-iteration solve, with separate primary and secondary winding-current state.
- Both models are prepared outside the audio callback.
- The plugin processes 32-bit audio; 64-bit VST3 processing is not implemented.
- Parameter changes are currently applied at processing-block boundaries. Sample-offset-accurate automation is future work.
- CPU cost still needs profiling on the target PC, and the magnetic parameters need physical calibration.

The Linux component/network test executable passes. The latest Windows x64 build, package upload, and Steinberg validator all succeeded; the validator reported **47 passed, 0 failed**.

## Build and download

No local compiler or SDK installation is required. Open the successful [Windows build run](https://github.com/Gastonpg/PG-Preamp/actions/runs/41575611834), then download **PG-Preamp-Windows-x64** from its Artifacts section.

See [docs/CIRCUIT_ENGINE.md](docs/CIRCUIT_ENGINE.md) for model assumptions, limitations, tests and remaining milestones.

PG Preamp is an original PG Audio design.
