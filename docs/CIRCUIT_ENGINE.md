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

### Fixed loaded Iron transformer

`FixedMagneticTransformerStage` is the real-time reduced-order transformer used by the Iron switch. It models a source resistance, primary winding resistance and leakage inductance, a magnetizing field from Ampere's law, a secondary winding with resistance/leakage, and a finite load.

For a candidate core field H, the solver advances a smooth stateful saturation/hysteresis macro-model, computes the core flux-density change, and derives primary/secondary induced voltages using Faraday's law. It solves the loaded primary/secondary current relation with a fixed-size scalar Newton iteration. The core law contains a reversible-permeability knee, saturation and a direction-dependent coercive offset with finite magnetic relaxation. Its state is continuous between samples, avoiding the flux jumps that would result from routing discrete relay-domain steps straight into audio.

This is a compact real-time macromodel, not a full Preisach implementation or a calibrated model of a named transformer. The winding, core and relaxation defaults are provisional. The current real-time path does not yet include a calibrated eddy-current loss branch, interwinding capacitance, distributed winding capacitance or measured parasitics.

The fixed transformer is prepared outside the audio callback. Its per-sample method uses fixed scalar state and no dynamic allocation or general-purpose MNA solver.

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
10. The fixed loaded transformer: zero-signal startup, loaded audio-band transfer, bounded high-level core flux and 44.1/48/96/192 kHz operation.

The amplifier fixtures are engineering test topologies. They are not claims of an exact 1073 circuit.

## Experimental VST3 integration

The experimental Windows x64 VST3 uses one independent A Stage and Iron transformer instance per stereo channel.

The signal path is:

1. Host audio is scaled to the circuit's internal voltage range and adjusted by Input Gain.
2. **Iron**, when enabled, passes the signal through the loaded magnetic input-transformer model.
3. **A Stage**, when enabled, passes the signal through the fixed common-emitter transistor circuit.
4. Output Gain maps the circuit output back to the host audio range.

The editor exposes Bypass, Input, Output, A Stage and Iron. A Stage is enabled by default; Iron is disabled by default. All five control values are saved in the processor state. Older three-value states remain readable and default to A Stage enabled / Iron disabled.

The processor supports 32-bit audio; 64-bit processing is not implemented. Parameters are currently read at the processing-block boundary, so sample-offset-accurate automation remains future work. The Steinberg VST3 validator reports 47 passed tests and 0 failed tests on the current build. The component/network test executable also passes on Linux and Windows.

## Remaining milestones

1. Calibrate the transistor model against a selected device datasheet or measured/SPICE reference, including temperature and bias-dependent effects.
2. Select and characterize an input transformer; calibrate turns, winding resistance, leakage, magnetizing inductance, B-H response and losses.
3. Add an explicit calibrated eddy-current/core-loss branch and winding/interwinding capacitances when supported by data.
4. Implement sample-offset-accurate automation and add transition smoothing where parameter changes alter circuit topology.
5. Profile real-time CPU cost, oversampling requirements, denormal behavior and stability on the target PC at 44.1/48/96/192 kHz.
6. Refine the temporary rack editor and add a properly metered output display.

The component and model libraries remain independent of the VST3 wrapper so validated parts can later be reused by PG Optical Compressor.
