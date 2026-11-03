# PG Audio Circuit Engine

## Development principle

Build and validate circuit elements individually before connecting them into a complete signal path. Every element needs a documented physical interpretation, SI-unit parameters where practical, deterministic state/reset behavior, response and stability tests, and measured CPU cost before real-time integration.

A component is not accepted merely because it sounds pleasing. Its behavior must be explainable and testable.

## Validated foundations in this branch

### Passive RC stage

A series resistor feeding a shunt capacitor, with capacitor voltage as output. For the ideal network:

- \`tau = R * C\`
- \`fc = 1 / (2*pi*R*C)\`
- \`alpha = 1 - exp(-1/(fs*R*C))\`
- \`y[n] = y[n-1] + alpha*(x[n]-y[n-1])\`

The exact zero-order-hold coefficient is used. Tests check time constant, cutoff, step response, stability at common sample rates and measured -3 dB response.

### Capacitor with ESR and leakage

A current-driven capacitor stores dielectric voltage and includes series equivalent resistance (ESR) plus parallel leakage resistance. With current held constant during one sample, leakage recovery uses the exact exponential solution. Stored charge, ESR drop and leakage time constant remain explicit. Dielectric absorption is deferred until it can be represented as independently tested relaxation branches.

### Resistor and Johnson-Nyquist noise

The model exposes thermal-noise density:

\`en = sqrt(4 * kB * T * R)\` V/sqrt(Hz)

For one-sided bandwidth \`B\`, integrated RMS noise is:

\`vn,rms = en * sqrt(B)\`

An optional deterministic noise source produces a low-cost Gaussian approximation for component experiments. It is not automatically inserted into the plugin signal path. Noise must be assigned to a physical component and bandwidth to avoid arbitrary or double-counted hiss.

### Inductor with winding resistance

The current state follows the exact first-order response of an ideal inductance in series with winding resistance for a voltage held constant over each sample. The winding resistance is explicit. Core saturation, hysteresis and frequency-dependent core loss are intentionally not hidden inside this linear component.

## Next candidates

1. Add dielectric-absorption behavior as independently testable relaxation branches.
2. Add magnetic core loss, saturation and hysteresis only with documented assumptions and tests.
3. Build passive RLC networks from validated components.
4. Develop a transformer model from winding/core data, rather than using a generic saturation curve.
5. Define and test the active transistor gain-stage topology before real-time integration.

The engine remains independent of the VST3 wrapper so it can later be reused by PG Optical Compressor.
