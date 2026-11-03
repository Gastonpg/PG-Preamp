# PG Preamp

PG Preamp is an original PG Audio VST3 project. The stable `main` branch remains untouched while this experimental branch develops the component-based DSP engine and its first audible signal path.

## Experimental first version

The Windows x64 VST3 contains two independently switchable blocks per channel:

- **A Stage:** a fixed common-emitter transistor circuit with a solved DC operating point, dynamic junction charge and coupling capacitors.
- **Iron:** a loaded two-winding microphone-input transformer macro-model, anchored to the Neve 10468 role and the published Carnhill VTB9045 reference configuration.

The temporary rack-style editor exposes:

- **Bypass**
- **Input:** -12 to +18 dB, default 0 dB
- **Output:** -18 to +18 dB, default 0 dB (enough attenuation to offset the full +18 dB Input range)
- **A Stage:** enabled by default
- **Iron:** disabled by default

### Iron reference and limits

AMS Neve identifies the 10468 as the 1073 microphone-input transformer, the 31267 as its line-input transformer, and the LO1166 as its output transformer. The VTB9045 design guide documents a low-level microphone-input transformer with a 1:2 turns ratio, a parallel-primary 300-ohm / parallel-secondary 1.2-kohm configuration at +6 dB, and individual winding DCRs of 24+24 ohms primary and 130+130 ohms secondary. In that parallel configuration, the effective winding DCRs are 12 ohms and 65 ohms.

The Iron model uses those public ratio, impedance and winding-resistance values as its electrical reference. Its absolute effective turns, core geometry, permeability, coercivity and leakage values are PG design parameters, not disclosed Marinair 10468 measurements. They are grouped in the reusable **PG Iron M1** profile in `source/circuit/pg_iron_calibration.h`, so future voicing changes and tests share one calibration source. PG Iron M1 is our own design informed by the Marinair/10468 role and public replacement data, **not an exact replica of the original transformer**.

As an independent 10468-replacement reference, Sowter's Type 9145 specifies a ±0.5 dB response from 10 Hz to 60 kHz with a 1-kilohm source and 600-ohm load. The current model's eight-point sweep under those source/load conditions deviates by no more than 0.036 dB from 1 kHz. This is a response-envelope cross-check against a published replacement specification, not a measurement of an original 10468. The LO1166 output transformer is not yet implemented.

The real-time core uses a smooth, stateful hysteresis approximation rather than routing discrete Preisach relay switches directly into audio. This avoids discontinuous flux steps at sample boundaries. At a 5 mV, 1 kHz test level and 48 kHz sample rate, the reference configuration measured 1.2545x small-signal voltage gain, approximately 0.000059% THD, and a largest selected non-harmonic spur around -123.7 dBc. The integrated non-harmonic DFT residual across the test spectrum is 8.43e-5 of the fundamental (about -81.5 dBc). Those are synthetic single-tone test results, not a claim of measured noise or distortion on a physical 10468.

Iron contains no injected random white-noise source. It can still transform noise already present in the input recording; the model does not yet add a calibrated winding thermal-noise contribution.

### A Stage level matching

The fixed common-emitter test circuit measures about 4.14469x small-signal voltage gain at 1 kHz. When A Stage is switched off, its dry path uses that reference gain as makeup, preserving its inverting polarity and preventing the roughly 12.3 dB level drop heard in the initial build. This compensation is a level-matched colour switch, not a claim that a passive bypassed transistor stage has physical gain.

### Real-time behavior

- The A Stage and Iron models are prepared outside the audio callback.
- Both blocks remain internally active to preserve state; their audible paths are crossfaded over 5 ms.
- Input and Output gain changes are smoothed over the same interval. Output spans -18 to +18 dB, centered at 0 dB; versioned state migration preserves the previous Output level in existing sessions.
- Parameter changes are consumed at their VST3 sample offsets.
- The fixed common-emitter stage uses a fixed-size Newton solve and stack arrays; it does not allocate from the heap or call the general MNA solver in the callback.
- The transformer uses a fixed-size scalar solve and fixed state; it does not allocate from the heap.
- The plugin processes 32-bit audio; 64-bit VST3 processing is not implemented.
- CPU cost still needs profiling on the target PC.
- Core parameters still need calibration against measured 10468/VTB9045 B-H, impedance and frequency-response data.

## Build and download

No local compiler or SDK installation is required. The latest Windows x64 build of this experimental DSP/editor branch passed the Steinberg VST3 validator (47 passed, 0 failed), the sample-accuracy automation check and the component/network test suite.

- [Download PG-Preamp-Windows-x64](https://github.com/Gastonpg/PG-Preamp/actions/runs/41580547955/artifacts/13205751219)
- [View successful Windows build and validation](https://github.com/Gastonpg/PG-Preamp/actions/runs/41580547955)

See [docs/CIRCUIT_ENGINE.md](docs/CIRCUIT_ENGINE.md) for model assumptions, validation tests, calibration boundaries and remaining milestones.

PG Preamp is an original PG Audio design, not an exact commercial 1073 emulation.
