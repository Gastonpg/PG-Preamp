# PG Preamp

PG Preamp is an original PG Audio VST3 project. The stable `main` branch remains untouched while this experimental branch develops the component-based DSP engine and its first audible signal path.

## Experimental first version

The Windows x64 VST3 contains two independently switchable blocks per channel:

- **A Stage:** a fixed common-emitter transistor circuit with a solved DC operating point, dynamic junction charge and coupling capacitors.
- **Iron:** a loaded two-winding microphone-input transformer macro-model, anchored to the Neve 10468 role and the published Carnhill VTB9045 reference configuration.

The temporary rack-style editor exposes:

- **Bypass**
- **Input:** -12 to +18 dB, default 0 dB
- **Output:** -12 to +12 dB, default 0 dB
- **A Stage:** enabled by default
- **Iron:** disabled by default

### Iron reference and limits

AMS Neve identifies the 10468 as the 1073 microphone-input transformer, the 31267 as its line-input transformer, and the LO1166 as its output transformer. The VTB9045 design guide documents a low-level microphone-input transformer with a 1:2 turns ratio, a parallel-primary 300-ohm / parallel-secondary 1.2-kohm configuration at +6 dB, and individual winding DCRs of 24+24 ohms primary and 130+130 ohms secondary. In that parallel configuration, the effective winding DCRs are 12 ohms and 65 ohms.

The Iron model uses those public ratio, impedance and winding-resistance values as its electrical reference. Its absolute effective turns, core geometry, permeability, coercivity and leakage values are macro-model parameters, not disclosed Marinair 10468 measurements. Therefore it is a measured-data-anchored equivalent, **not an exact replica of the original Marinair transformer**. The LO1166 output transformer is not yet implemented.

The real-time core uses a smooth, stateful hysteresis approximation rather than routing discrete Preisach relay switches directly into audio. This avoids discontinuous flux steps at sample boundaries. At a 5 mV, 1 kHz test level and 48 kHz sample rate, the reference configuration measured 1.2545x small-signal voltage gain, approximately 0.000059% THD, and a largest selected non-harmonic spur around -123.7 dBc. Those are synthetic single-tone test results, not a claim of measured noise or distortion on a physical 10468.

Iron contains no injected random white-noise source. It can still transform noise already present in the input recording; the model does not yet add a calibrated winding thermal-noise contribution.

### A Stage level matching

The fixed common-emitter test circuit measures about 4.14469x small-signal voltage gain at 1 kHz. When A Stage is switched off, its dry path uses that reference gain as makeup, preventing the roughly 12.3 dB level drop heard in the initial build. This compensation is a level-matched colour switch, not a claim that a passive bypassed transistor stage has physical gain.

### Real-time behavior

- The A Stage and Iron models are prepared outside the audio callback.
- Both blocks remain internally active to preserve state; their audible paths are crossfaded over 5 ms.
- Input and Output gain changes are smoothed over the same interval.
- Parameter changes are consumed at their VST3 sample offsets.
- The fixed common-emitter stage uses a fixed-size Newton solve and stack arrays; it does not allocate from the heap or call the general MNA solver in the callback.
- The transformer uses a fixed-size scalar solve and fixed state; it does not allocate from the heap.
- The plugin processes 32-bit audio; 64-bit VST3 processing is not implemented.
- CPU cost still needs profiling on the target PC.
- Core parameters still need calibration against measured 10468/VTB9045 B-H, impedance and frequency-response data.

## Build and download

No local compiler or SDK installation is required. The latest successful Windows build will be linked here after the current integration build completes. Open the repository's [Actions page](https://github.com/Gastonpg/PG-Preamp/actions) and download **PG-Preamp-Windows-x64** from the successful build run.

See [docs/CIRCUIT_ENGINE.md](docs/CIRCUIT_ENGINE.md) for model assumptions, validation tests, calibration boundaries and remaining milestones.

PG Preamp is an original PG Audio design, not an exact commercial 1073 emulation.
