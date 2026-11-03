# PG Preamp

PG Preamp is an original PG Audio VST3 project. The stable `main` branch remains untouched while this experimental branch develops the component-based DSP engine and its first audible signal path.

## Experimental first version

The Windows x64 VST3 contains two independently switchable blocks per channel:

- **A Stage:** a fixed common-emitter transistor circuit with a solved DC operating point, dynamic junction charge and coupling capacitors.
- **Iron:** a loaded two-winding microphone-input transformer macro-model, anchored to the Neve 10468 role and the published Carnhill VTB9045 reference configuration.

The current active-block calibration profiles are centralized and shared by runtime and tests: **PG A Stage M1** and **PG Iron M1**. A Stage M1 preserves the currently auditioned voicing. A separately tested **BC550C Cordell reference** now provides a sample-derived transistor model for engineering comparison; it is not silently substituted into the active M1 sound.

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

### A Stage device reference and noise analysis

The active PG A Stage M1 remains unchanged. An alternate **BC550C Cordell reference** is available in `source/circuit/pg_a_stage_profile.h`, based on the [Cordell Audio SPICE model library](https://www.cordellaudio.com/book/spice_models.shtml). Cordell states that its models were adjusted against datasheet data and DC measurements of physical samples, including improved base-resistance estimates for small-signal noise simulation. This is a measured-sample-derived reference, not a guarantee that every BC550C production lot has identical characteristics.

The current compact model checks its predicted current gain at the published C-grade 2 mA / 5 V point: hFE 521.9, inside the 420–800 datasheet range. In the present PG circuit, its simulated quiescent point is 1.173 mA / 5.309 V VCE; at the nominal +18 dB input envelope it remains forward-active, with minimum collector current 1.136 mA. At 0 °C, 27 °C and 70 °C it converges and maintains conduction. Across 16 combinations of ±5% divider/collector/emitter resistor values, worst-cycle minimum collector current is 0.950 mA and minimum VCE is 3.586 V.

With the same fixed topology and test conditions, the reference produces 0.00217% THD at 5 mV peak and 0.01703% at 40 mV peak, versus 0.00249% / 0.01953% for M1. Its 20 Hz–20 kHz small-signal response varies by at most 0.01578 dB from 1 kHz. At 17 kHz / 40 mV, folded H2–H5 is 0.01658% at 48 kHz versus 0.01626% in the 192 kHz reference run. These are simulation comparisons, not measurements of a finished hardware preamp. The alternate profile is not yet active in the plugin; listening comparison is still needed before promoting it.

The NPN engine now has an optional compact Gummel-Poon base-charge path, emission coefficients, high-current roll-off and BE/BC leakage branches, with analytic conductance derivatives checked against finite differences. M1 retains its earlier path; the BC550C reference profile enables the added base-charge terms. The implementation is not the full ngspice BJT model: bias-dependent excess-phase terms (XTF/VTF/ITF), dynamic base-resistance modulation (IRB/RBM), full collector-base charge partition and self-heating are still missing.

First-order transistor noise diagnostics are also available without audio injection. At the BC550C reference bias and 300.15 K they report:
- base-resistance thermal noise: 1.664 nV/√Hz;
- collector-resistance thermal noise: 0.129 nV/√Hz;
- emitter-resistance thermal noise: 0.0257 nV/√Hz;
- base-current shot noise: 0.876 pA/√Hz;
- collector-current shot noise: 19.39 pA/√Hz.

These are one-sided source spectral densities, not the preamp's total equivalent input noise or output noise floor. A full noise budget still needs each source's transfer to the output, source/load impedances, correlations and any excess-noise terms. None of these sources is currently injected into the audio path.

### A Stage level matching

The fixed common-emitter test circuit measures about 4.14469x small-signal voltage gain at 1 kHz. When A Stage is switched off, its dry path uses that reference gain as makeup, preserving its inverting polarity and preventing the roughly 12.3 dB level drop heard in the initial build. This compensation is a level-matched colour switch, not a claim that a passive bypassed transistor stage has physical gain.

### Real-time behavior

- The A Stage and Iron models are prepared outside the audio callback.
- Both blocks remain internally active to preserve state; their audible paths are crossfaded over 5 ms.
- Input and Output gain changes are smoothed over the same interval. Output spans -18 to +18 dB, centered at 0 dB; versioned state migration preserves the previous Output level in existing sessions.
- Parameter changes are consumed at their VST3 sample offsets.
- The fixed common-emitter stage uses a fixed-size Newton solve and stack arrays; it does not allocate from the heap or call the general MNA solver in the callback.
- The transformer uses a fixed-size scalar solve and fixed state; it does not allocate from the heap.
- The plugin supports native VST3 32-bit and 64-bit audio buses. Both paths use the same sample-offset automation and double-precision circuit state; the 32-bit host path narrows only when writing its float output buffer.
- CPU cost still needs profiling on the target PC.
- A Stage M1 and the alternate BC550C reference have independent bias, distortion, response, temperature, tolerance and high-frequency harmonic tests. The BC550C model is a sample-derived reference candidate, not the currently active M1 transistor profile.
- The NPN engine exposes first-order resistor thermal-noise and base/collector shot-noise source densities for analysis. No random or deterministic noise is added to the audio output.
- Iron core parameters remain PG macro-model choices and should be iterated against listening tests and any future physical reference measurements.

## Build and download

No local compiler or SDK installation is required. The latest Windows x64 build of this experimental DSP/editor branch passed the Steinberg VST3 validator (47 passed, 0 failed), the sample-accuracy automation check and the component/network test suite.

- [Download PG-Preamp-Windows-x64](https://github.com/Gastonpg/PG-Preamp/actions/runs/41580552568/artifacts/13206340355)
- [View successful Windows build and validation](https://github.com/Gastonpg/PG-Preamp/actions/runs/41580552568)

See [docs/CIRCUIT_ENGINE.md](docs/CIRCUIT_ENGINE.md) for model assumptions, validation tests, calibration boundaries and remaining milestones.

PG Preamp is an original PG Audio design, not an exact commercial 1073 emulation.
