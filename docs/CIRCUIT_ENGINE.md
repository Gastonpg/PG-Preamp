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

A current-driven capacitor stores dielectric voltage and includes series equivalent resistance (ESR) plus parallel leakage resistance. With current held constant during one sample, leakage recovery uses the exact exponential solution. In the MNA network solver, ESR is represented as an explicit series resistor, leakage as a parallel resistor, and the capacitor's transient current uses a backward-Euler companion model. Dielectric absorption and voltage-dependent capacitance are future model extensions.

### Inductor (L)

The component-level model tracks winding current with series winding resistance. In the network solver, an inductor is an MNA branch-current unknown with a backward-Euler constitutive equation. This permits the solver to connect inductors into larger RLC networks. Magnetic saturation, hysteresis, core loss and mutual coupling are not yet included.

### NPN transistor

The static Ebers-Moll primitive calculates collector, base and emitter currents from base-emitter and base-collector junction voltages. It includes saturation current, forward/reverse beta and SPICE-style temperature scaling. Its analytic terminal-conductance matrix is used by Newton iterations in the circuit solver.

This is a static junction model only. It does not yet include Early effect, ohmic parasitics, junction capacitances, diffusion charge or transit-time behavior. It is not claimed to reproduce a particular transistor device.

## Circuit-level solver

`MnaCircuit` is a double-precision modified-nodal-analysis (MNA) engine for offline component/network validation. It assembles:

- resistor conductance and temperature-dependent resistance;
- capacitor companion conductance and history current, including explicit ESR and leakage branches;
- inductor branch-current equations and winding resistance;
- ideal voltage/current sources;
- nonlinear NPN terminal currents and their analytic Jacobian.

A pivoted dense linear solve handles each Newton update. This is deliberately a test/engineering solver, not real-time plugin code: it allocates matrices and performs nonlinear iterations. It must not be called from the audio callback in its current form.

## Verified test circuits

The automated component suite currently checks:

1. R, L and C component equations, resistor temperature coefficients, and resistor thermal-noise statistics.
2. An RC network against its backward-Euler transient recurrence.
3. An RL network against its backward-Euler branch-current recurrence.
4. NPN terminal-current conservation and the analytic Jacobian against finite differences.
5. A 12 V common-emitter test stage with a base divider, collector/emitter resistors and a nonlinear NPN transistor, solving its DC bias point.
6. The same class of common-emitter stage with input/output coupling capacitors, source resistance and output load, verifying nonzero AC gain and phase inversion.

The common-emitter test is a solver validation fixture, not a claim that this is the final PG Preamp topology or an exact 1073 circuit.

## Next physical-model milestones

1. Add transistor series resistances, Early effect and junction/diffusion charge using sourced device parameters.
2. Add capacitor dielectric absorption and inductor core-loss behavior as separately validated models.
3. Implement coupled transformer windings with magnetizing/leakage inductance, winding resistance, interwinding capacitance and a documented core model.
4. Select and validate the PG Preamp's actual input, gain and output topology from a circuit-level reference design.
5. Profile accuracy, stability and CPU cost, then build a fixed-topology real-time solver or derive an efficient state-space implementation. Only after those steps should the model enter the VST3 audio path.

The engine is independent of the VST3 wrapper so validated components and models can later be reused by PG Optical Compressor.
