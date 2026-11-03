# PG Audio Circuit Engine

## Development principle

Build and validate circuit elements individually before connecting them into a complete signal path. Every element needs a documented physical interpretation, SI-unit parameters where practical, deterministic state/reset behavior, response and stability tests, and measured CPU cost before a production release.

A component is not accepted merely because it sounds pleasing. Its behavior must be explainable and testable.

## Component models

### Resistor (R)

The resistor is algebraic: it has no stored state. Its current and voltage follow Ohm's law, with power dissipation calculated as V*I. Optional first- and second-order temperature coefficients model resistance drift around a nominal temperature:

R(T) = R0 * [1 + TC1*(T-T0) + TC2*(T-T0)^2]

The Johnson-Nyquist noise density is:

en = sqrt(4 * kB * T * R(T)) V/sqrt(Hz)

For one-sided bandwidth B, integrated RMS noise is:

vn,rms = en * sqrt(B)

An optional deterministic Gaussian-approximation source exists for tests, but is not automatically inserted into plugin audio.

### Capacitor (C)

A current-driven capacitor stores dielectric voltage and includes series equivalent resistance (ESR) plus parallel leakage resistance. With current held constant during one sample, leakage recovery uses the exact exponential solution. In the MNA network solver, ESR is represented as an explicit series resistor, leakage as a parallel resistor, and the capacitor's transient current uses a backward-Euler companion model.

### Inductor (L)

The component-level model tracks winding current with series winding resistance. In the network solver, an inductor is an MNA branch-current unknown with a backward-Euler constitutive equation. Linear magnetic coupling between two winding branches is represented through signed mutual inductance:

M = k * sqrt(Lp * Ls), |k| < 1

### Detailed magnetic core and transformer reference

`PreisachCore` uses a discrete weighted ensemble of magnetic relay domains with separate upper/lower switching fields. The domain states preserve magnetic history, allowing remanence, coercive switching and minor-loop behavior. Its flux-density calculation includes the vacuum-field term plus the domain magnetization contribution.

The model accumulates the B-H work integral:

W_h / volume = integral(H dB)

and provides a separate rate-dependent eddy-loss proxy proportional to (dB/dt)^2. The relay ensemble is a phenomenological Preisach-type model, not a first-principles micromagnetic simulation. Its coercivity distribution, saturation flux density and loss coefficient need calibration from measured core material data before it can represent a particular transformer core.

`MagneticTransformer` combines this history-dependent flux with winding turns, winding resistance and leakage inductance. Given primary and secondary winding currents, it computes induced winding voltage from Faraday's law:

v = Rw*i + L_leak*(di/dt) + N*A*(dB/dt)

This detailed current-driven two-port is retained as an offline reference model. Its discrete relay transitions and eddy-loss proxy are not directly used in the real-time plugin path.

## NPN transistor

The extended NPN model retains Ebers-Moll transport currents and adds a compact Gummel-Poon-style set of effects:

- forward/reverse Early-voltage output-conductance correction;
- external base, collector and emitter series resistances;
- voltage-dependent base-emitter and base-collector depletion charge;
- forward/reverse transit-time diffusion charge;
- analytic current-conductance and charge-capacitance Jacobians;
- temperature scaling of saturation current and beta.

The MNA solver stamps the internal series resistors and integrates terminal charge with a backward-Euler companion equation. This is a compact model, not the full ngspice Gummel-Poon model: high-current beta roll-off, leakage-current branches, full bias-dependent transit-time behavior, substrate junctions and every SPICE temperature coefficient are not implemented. Device-specific parameters must come from a device datasheet or measured characterization. The model structure follows the documented families of parameters in the [ngspice manual](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).

## Circuit-level solvers

### Generic offline MNA

`MnaCircuit` is a double-precision modified-nodal-analysis engine for offline component/network validation. It assembles resistors, capacitors, inductors, coupled windings, sources and nonlinear NPN devices. A pivoted dense linear solve handles each Newton update.

It uses dynamically sized matrices and performs nonlinear iterations. It is a reference/test solver and must not be called from the audio callback.

### Fixed common-emitter A Stage

`FixedCommonEmitterStage` implements one explicitly fixed common-emitter topology using a 7-unknown Newton system and stack-allocated arrays. Its audio-sample method uses no dynamic containers, heap allocation, locks or general-purpose MNA assembly. It includes the transistor's internal series resistances and dynamic junction charge, plus input/output coupling-capacitor history.

At prepare/reset time it solves the DC operating point and initializes the coupling-capacitor voltages from that solution. This prevents a false output transient from an inconsistent initial bias. The topology is a test fixture and a first audible A Stage, not the final PG Preamp schematic.

At 48 kHz and 1 kHz, the current provisional transistor/topology setup measures approximately 4.14469x small-signal voltage gain. The processor uses this exact measured value as makeup gain on the A Stage dry path, so disabling the colour stage does not cause the roughly 12.3 dB drop heard in the first build. The fixed common-emitter stage is inverting; its level-matched dry reference uses the same polarity so the 5 ms crossfade does not pass through a polarity cancellation. A regression test checks that the actual plugin transistor stage has negative input/output correlation. This is a UI/processor level-matching choice, not a physical gain claim for a disconnected transistor stage.

### PG A Stage M1 profile and Class-A operating checks

`PgAStageProfileV1` centralizes the transistor and fixed common-emitter network used by the VST3 processor. The profile preserves the currently auditioned sound; it does not silently replace the generic transistor with a different device model. Its present BJT constants (IS, beta, Early voltages, series resistances, junction capacitances and transit times) are explicitly provisional.

The fixed stage now reports its solved quiescent point and the instantaneous terminal currents. At 48 kHz and 300.15 K, the current profile solves to approximately 1.04182 mA collector current and 6.04779 V VCE. Automated regressions check:
- positive collector current and negative emitter current at rest;
- collector/base/emitter current conservation;
- a forward-active quiescent point with more than 1 V collector-emitter headroom;
- uninterrupted collector conduction and positive active-region voltage over a complete 1 kHz cycle at 40 mV peak input, representing the nominal +18 dB Input/full-scale host envelope at the circuit boundary. In that test the minimum collector current is 1.00486 mA and minimum VCE is 5.8446 V.

The same regression harness measures A Stage THD of approximately 0.00249% at 5 mV peak and 0.01953% at 40 mV peak at 1 kHz. Its small-signal sweep from 20 Hz to 20 kHz varies by no more than 0.0177 dB from the 1 kHz reference. These are deterministic synthetic tests of the present model; they are not measurements of a physical preamp. These are bounded engineering checks for the current topology and temperature, not a universal guarantee under arbitrary overload, frequency, temperature or component spread. The word *Class A* describes the conduction mode under the specified test envelope, not a claim of a fully calibrated commercial hardware clone.

A possible future device reference is the onsemi BC550C low-noise NPN. Its datasheet specifies a C gain grade of 420–800 at 2 mA / 5 V, a typical 1.2 dB noise figure at 200 µA / 5 V / 1 kHz with 2 kΩ source resistance, a typical 300 MHz transition frequency at 10 mA, and typical input/output capacitance data. Those are sparse datasheet points, not a complete Gummel-Poon parameter set; therefore the current profile does not claim to be a BC550C model. Device fitting still needs a manufacturer SPICE card or measured transistor curves and should be validated at the actual stage bias current.

### Fixed loaded Iron transformer

`FixedMagneticTransformerStage` is the real-time reduced-order transformer used by the Iron switch. It models a source resistance, primary winding resistance and leakage inductance, a magnetizing field from Ampere's law, a secondary winding with resistance/leakage, and a finite load.

The reference is the 1073's **10468 microphone-input role**, not the 31267 line-input transformer or the LO1166 output transformer. AMS Neve's own documentation identifies those three devices by role. The original Marinair 10468's detailed winding/core measurements are not publicly specified in the documentation available for this project.

To establish a reproducible electrical target, the macro-model uses the published Carnhill VTB9045 low-level microphone-transformer configuration as a reference proxy. Its design guide specifies:
- a 1+1 : 2+2 turns arrangement (overall voltage turns ratio 1:2);
- parallel primary rated at 300 ohms and parallel secondary rated at 1.2 kilohms for +6 dB voltage gain;
- primary coil resistances 24 + 24 ohms and secondary coil resistances 130 + 130 ohms. In the parallel/parallel configuration these reduce to effective winding DCRs of 12 ohms and 65 ohms.

The real-time model therefore uses 1:2 turns ratio, 150-ohm source resistance, 12-ohm primary DCR, 65-ohm secondary DCR and a 1.2-kilohm secondary load. The 150-ohm source is a nominal microphone-source assumption; it is not an internal Carnhill value.

These values, together with the PG magnetic curve and effective turns/leakage assumptions, are now centralized in `source/circuit/pg_iron_calibration.h` as **PG Iron M1**. The profile deliberately separates electrical anchors taken from public replacement data from PG-owned macro-parameters. Its saturation flux density, effective core geometry, permeability, coercivity and relaxation constants are design/voicing values rather than claims about the proprietary Marinair core. The VST3 processor and component tests both consume the same profile, preventing test/runtime parameter drift. This is the project's own starting calibration; future revisions can change its voicing without pretending to reproduce an undocumented production unit.

A second independent cross-check is the **Sowter Type 9145**, which Sowter explicitly describes as a replacement for the Neve 10468. Its published 1:2 / 300-ohm-to-1.2-kilohm configuration gives a response specification of ±0.5 dB from 10 Hz to 60 kHz with a 1-kilohm source and 600-ohm load. Its specification also lists a maximum input level of +10 dBu at 50 Hz for 0.5% THD with a 40-ohm source. The automated frequency sweep therefore uses the published 1-kilohm source / 600-ohm load and checks the ±0.5 dB envelope from 10 Hz to 60 kHz, referenced to 1 kHz. The current macro-model's measured maximum deviation across the eight-point sweep is 0.036 dB. This is an independent replacement-transformer response target, not a measurement of the original Marinair or a claim that the two replacement transformers share winding DCR. See the [Sowter Type 9145 data](https://www.sowter.co.uk/specs/9145.php) and [AMS Neve's 1073 transformer description](https://www.ams-neve.com/outboard/1073-range/1073-mic-preamp-equaliser/).

Absolute effective turns, core geometry, saturation, relative permeability, coercive field, branch-memory time constant and leakage inductance are macro-model values, not measured original Marinair data. They are kept explicit in the code and marked provisional. The implementation is therefore a data-anchored 10468-role model, not an exact replica of an original 10468 or a claim that all of its core properties have been calibrated.

The core model now uses a smooth stateful branch-memory variable and continuous magnetization relaxation. The earlier hard `branchDirection` sign flip was removed because it produced a discontinuous change in the target flux when the audio waveform reversed slope. That discontinuity could create noise-like broadband artifacts. The detailed discrete Preisach core remains an offline reference and is not fed directly into the audio callback.

At 48 kHz, with a 5 mV, 1 kHz sine, the current VTB9045-reference configuration measures:
- small-signal gain: 1.2545x before processor level matching;
- low-level THD: 5.94e-7 ratio (approximately 0.000059%);
- largest of the selected non-harmonic DFT bins: 6.52e-7 of the fundamental (approximately -123.7 dBc);
- integrated residual across all non-harmonic DFT bins: 8.43e-5 of the fundamental (approximately -81.5 dBc).

Those figures are deterministic synthetic single-tone tests of this macro-model. The integrated residual is a spectral artifact measurement, not a direct listening or physical noise-floor measurement. They are not measurements of a physical VTB9045 or Marinair 10468 and do not quantify noise from a real microphone/recording. The model does not inject a random white-noise source. Transformer thermal-noise contribution is not yet modelled; audible noise already present in an input recording can still pass through the signal path.

The transformer is prepared outside the audio callback. Its per-sample method uses fixed scalar state and no dynamic allocation or general-purpose MNA solver.

## Verified test circuits

The automated component/network suite checks:

1. R, L and C equations, resistor temperature coefficients and thermal-noise statistics.
2. RC and RL network transients against their backward-Euler recurrences.
3. Two linearly coupled windings under load, checking turns ratio.
4. NPN terminal-current conservation, Early effect, and analytic current/charge Jacobians against finite differences.
5. A Preisach-style core's saturation, remanence, hysteresis work and non-negative eddy-loss proxy.
6. A current-driven nonlinear transformer: small-signal turns ratio, core saturation and remanent flux.
7. A 12 V common-emitter circuit with bias divider, collector/emitter resistors and nonlinear transistor, solving its DC operating point.
8. The common-emitter stage with AC coupling and load, verifying gain and phase inversion in generic MNA.
9. The fixed common-emitter stage, verifying gain/phase and comparing its measured gain with the generic MNA reference.
10. The fixed loaded transformer: zero-signal startup, 1:2 turns ratio, loaded audio-band transfer, low-level harmonic and non-harmonic spectrum, bounded high-level core flux and 44.1/48/96/192 kHz operation.

The amplifier fixtures are engineering test topologies. They are not claims of an exact 1073 circuit.

## Experimental VST3 integration

The experimental Windows x64 VST3 uses one independent A Stage and Iron transformer instance per stereo channel.

The signal path is:

1. Host audio is scaled to the circuit's internal microphone-level voltage range.
2. **Iron**, when enabled, passes the signal through the loaded 10468-role input-transformer macro-model. The dry path is normalized against the measured small-signal macro-model gain so engaging Iron is not simply a volume increase.
3. **Input Gain** is applied after the input transformer, following the basic 1073 gain-staging order more closely.
4. **A Stage**, when enabled, passes the signal through the fixed common-emitter transistor circuit. When disabled, its dry path uses the measured 4.14469x small-signal gain for level matching.
5. **Output Gain** maps the circuit output back to the host audio range.

The editor exposes Bypass, Input, Output, A Stage and Iron. A Stage is enabled by default; Iron is disabled by default. Output now spans -18 to +18 dB, centered at 0 dB, so it can compensate the full +18 dB Input range. State version 2 records the new mapping; older three- and five-value sessions are migrated by preserving their previous Output dB value, and older states still default to A Stage enabled / Iron disabled.

Block switches crossfade over 5 ms. The circuit blocks continue running internally while their audible path is disabled, preserving the state so toggling them does not connect a cold model mid-waveform. Input and Output gain changes are also smoothed over 5 ms. VST3 parameter events are consumed at their sample offsets rather than taking only the final value in the processing block.

The processor supports 32-bit audio; 64-bit processing is not implemented. The latest Windows x64 build passed 47 Steinberg VST3 validator tests with 0 failures, including the sample-accuracy automation check, and the component/network test executable passed on Linux and Windows. Download the [PG-Preamp-Windows-x64 artifact](https://github.com/Gastonpg/PG-Preamp/actions/runs/41580552568/artifacts/13206340355) or inspect the [successful build log](https://github.com/Gastonpg/PG-Preamp/actions/runs/41580552568).

## Remaining milestones

1. Iterate PG Iron M1's voicing against level-matched listening tests on real vocal and instrument material; use physical B-H, impedance and frequency-response measurements later if a specific hardware unit becomes available.
2. Add winding thermal noise and more detailed core-loss behavior only when the chosen PG noise target and a trustworthy measurement dataset justify it.
3. Decide whether to add the separate 31267 line-input transformer and LO1166 output-transformer models as additional modes.
4. Calibrate the transistor model against a selected device datasheet or measured/SPICE reference, including temperature and bias-dependent effects.
5. Profile real-time CPU cost, oversampling requirements, denormal behavior and stability on the target PC at 44.1/48/96/192 kHz.
6. Refine the temporary rack editor and add a properly metered output display.

A 48 kHz stereo benchmark of two active Iron instances plus two A Stage instances currently takes about 10.0% of one second of audio time on the GitHub Ubuntu test runner, with a maximum of one Newton iteration for A Stage and two for Iron at the benchmark signal. This is only a CI-runner baseline and must not be read as a CPU estimate for the target PC.

The component and model libraries remain independent of the VST3 wrapper so validated parts can later be reused by PG Optical Compressor.
