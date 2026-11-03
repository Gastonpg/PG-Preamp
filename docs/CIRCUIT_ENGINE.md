# PG Audio Circuit Engine

## Development principle

Build and validate circuit elements individually before connecting them into a complete signal path. Every element needs a documented physical interpretation, SI-unit parameters where practical, deterministic state/reset behavior, response and stability tests, and measured CPU cost before real-time integration.

A component is not accepted merely because it sounds pleasing. Its behavior must be explainable and testable.

## Validated foundations in this branch

### Resistor (R)

The resistor is an algebraic component: it has no stored state. Its current and voltage follow Ohm's law, with power dissipation calculated as V*I. Optional first- and second-order temperature coefficients model resistance drift around a nominal temperature:

R(T) = R0 * [1 + TC1*(T-T0) + TC2*(T-T0)^2]

The Johnson-Nyquist noise density is:

en = sqrt(4 * kB * T * R(T)) V/sqrt(Hz)

For one-sided bandwidth B, integrated RMS noise is:

vn,rms = en * sqrt(B)

An optional deterministic Gaussian-approximation source exists for tests, but is not automatically inserted into plugin audio. Temperature-coefficient equations follow the ngspice device-model documentation.

### Capacitor (C)

The current-driven capacitor stores dielectric voltage and includes series equivalent resistance (ESR) plus parallel leakage resistance. With current held constant during one sample, leakage recovery uses the exact exponential solution. Stored charge, ESR drop and leakage time constant remain explicit. Dielectric absorption is deferred until it can be represented as independently tested relaxation branches.

### Inductor (L)

The current state follows the exact first-order response of an ideal inductance in series with winding resistance for a voltage held constant over each sample. Winding resistance is explicit. Core saturation, hysteresis and frequency-dependent core loss are not hidden inside this linear component.

### Passive RC stage

A series resistor feeding a shunt capacitor, with capacitor voltage as output. For the ideal network:

- tau = R * C
- fc = 1 / (2*pi*R*C)
- alpha = 1 - exp(-1/(fs*R*C))
- y[n] = y[n-1] + alpha*(x[n]-y[n-1])

Tests check time constant, cutoff, step response, stability at common sample rates and measured -3 dB response.

### Static NPN transistor (Ebers-Moll)

The first transistor primitive calculates collector, base and emitter currents from base-emitter and base-collector junction voltages, using saturation current, forward/reverse beta and absolute temperature. It includes SPICE-style temperature scaling for saturation current (band-gap energy and XTI) and beta (XTB). Terminal-current sign conventions are explicit and KCL is checked by tests.

This is a static junction model only. It does not yet include Early effect, ohmic parasitics, junction capacitances, diffusion charge or transit-time behavior. It is not connected to the audio path and is not claimed to reproduce a particular transistor device. Temperature equations follow the [ngspice BJT model documentation](https://ngspice.sourceforge.io/docs/ngspice-44-manual.pdf).

## Integration path

These are verified component primitives, not yet a complete amplifier. The next circuit-level milestone is a small modified-nodal-analysis (MNA) test solver that connects actual resistors, capacitors, inductors and a nonlinear transistor at once. It will be validated against hand-calculated bias points and transient responses before any solver is considered for real-time plugin use.

The MNA approach and Newton-Raphson solution of nonlinear circuit equations are standard circuit-simulation methods; see the [ngspice manual](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).

## Remaining model extensions

1. Capacitor dielectric absorption and voltage-dependent capacitance where justified.
2. Inductor core loss, saturation and hysteresis.
3. Transistor Early effect, ohmic parasitics, junction capacitances and stored charge.
4. Coupled transformer windings with leakage/magnetizing inductances, winding resistance and core behavior.
5. A validated active gain-stage topology built from those elements.

The engine remains independent of the VST3 wrapper so it can later be reused by PG Optical Compressor.
