# PG Audio Circuit Engine

## Development principle

Build and validate circuit elements individually before connecting them into a complete signal path. Every element needs a documented physical interpretation, SI-unit parameters where practical, deterministic state/reset behavior, response and stability tests, and measured CPU cost before real-time integration.

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

### Magnetic core and transformer

`PreisachCore` uses a discrete weighted ensemble of magnetic relay domains with separate upper/lower switching fields. The domain states preserve magnetic history, allowing remanence, coercive switching and minor-loop behavior. Its flux-density calculation includes the vacuum-field term plus the domain magnetization contribution.

The model accumulates the B-H work integral:

W_h / volume = integral(H dB)

and provides a separate rate-dependent eddy-loss proxy proportional to (dB/dt)^2. The relay ensemble is a phenomenological Preisach-type model, not a first-principles micromagnetic simulation. Its coercivity distribution, saturation flux density and loss coefficient need calibration from measured core material data before it can represent a particular transformer core.

`MagneticTransformer` combines this history-dependent flux with winding turns, winding resistance and leakage inductance. Given primary and secondary winding currents, it computes induced winding voltage from Faraday's law:

v = Rw*i + L_leak*(di/dt) + N*A*(dB/dt)

This current-driven transformer block is a nonlinear two-port component model for offline validation. It is not yet connected to the generic MNA solver or the plugin signal path.

### NPN transistor

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

### Fixed-topology real-time candidate

`FixedCommonEmitterStage` implements one explicitly fixed common-emitter topology using a 7-unknown Newton system and stack-allocated arrays. Its audio-sample method uses no dynamic containers, heap allocation, locks or general-purpose MNA assembly. It includes the transistor's internal series resistances and dynamic junction charge, plus input/output coupling-capacitor history.

This stage is an optimization candidate and a circuit validation fixture. It is not yet the final PG Preamp schematic, and it does not include the nonlinear transformer in its audio path.

## Verified test circuits

The automated component suite currently checks:

1. R, L and C equations, resistor temperature coefficients and thermal-noise statistics.
2. RC and RL network transients against their backward-Euler recurrences.
3. Two linearly coupled windings under load, checking turns ratio.
4. NPN terminal-current conservation, Early effect, and analytic current/charge Jacobians against finite differences.
5. A Preisach-style core's saturation, remanence, hysteresis work and non-negative eddy-loss proxy.
6. A current-driven nonlinear transformer: small-signal turns ratio, core saturation and remanent flux.
7. A 12 V common-emitter circuit with bias divider, collector/emitter resistors and nonlinear transistor, solving its DC operating point.
8. The same class of stage with AC coupling and load, verifying gain and phase inversion in generic MNA.
9. The fixed-topology stage, verifying gain/phase and comparing its measured gain with the generic MNA reference.

The amplifier fixtures are engineering test topologies. They are not claims of an exact 1073 circuit.

## Next physical-model milestones

1. Calibrate transistor model parameters against chosen device datasheets or SPICE reference sweeps, including temperature and bias-dependent effects.
2. Calibrate magnetic core hysteresis and losses against a selected transformer/core dataset, and add winding capacitance/interwinding parasitics.
3. Connect the transformer and fixed gain-stage models in a fixed-topology signal path; compare the result against the generic MNA reference and measured frequency/transient responses.
4. Add explicit input/headroom and output controls, then integrate the validated topology into the VST3 processor.
5. Measure CPU cost, oversampling needs, denormal behavior and stability at 44.1/48/96/192 kHz before release.

The component and model libraries remain independent of the VST3 wrapper so validated models can later be reused by PG Optical Compressor.


## Experimental VST3 integration

The experimental Windows VST3 currently uses `FixedCommonEmitterStage`, one independent instance per stereo channel. The host exposes Bypass, Input and Output controls. Input ranges from -12 to +18 dB and Output from -12 to +12 dB; both default to 0 dB. These parameters are serialized in the processor state and restored into the edit controller, including the standard VST3 bypass-persistence behavior.

The DSP signal is scaled into the stage's internal voltage range, processed through the transistor circuit and scaled back to the host's floating-point audio range. The current stage uses provisional transistor parameters selected for stable engineering tests. They are not calibrated to a particular transistor datasheet or measured hardware.

Important scope boundary: only the fixed common-emitter stage currently processes plugin audio. The magnetic transformer / Preisach core block is separately implemented and tested but is not yet in the VST3 signal path. The plugin is therefore an audible experimental circuit stage, not a completed hardware emulation or final PG Preamp schematic.

The generic MNA solver remains an offline reference tool. The fixed stage uses a fixed-size, stack-only solve and no dynamic allocation in its per-sample method. Automation is currently read at the processing-block boundary; sample-offset-accurate parameter ramps remain future work.

Latest successful validation: Windows x64 build, 51 component/network tests and Steinberg VST3 validator all passed in [GitHub Actions run 41570556094](https://github.com/Gastonpg/PG-Preamp/actions/runs/41570556094).
