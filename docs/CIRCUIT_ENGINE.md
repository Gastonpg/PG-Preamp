# PG Audio Circuit Engine

## Development principle

Build and validate circuit elements individually before connecting them into a complete signal path. Each element must have:

- a documented physical interpretation and assumptions;
- parameters expressed in physical units where practical;
- deterministic state initialization and reset behavior;
- tests for expected response, stability, and edge cases;
- a measured CPU cost before real-time integration.

A component is not accepted merely because it sounds pleasing. Its behavior must first be explainable and testable.

## First component: passive RC stage

```text
series resistor R -> node -> capacitor C to ground
                         |
                       output
```

The capacitor voltage is the output. The analog time constant is `tau = R * C` and the -3 dB corner is `fc = 1 / (2*pi*R*C)`. The discrete-time update uses the exact zero-order-hold coefficient `alpha = 1 - exp(-1/(fs*R*C))`, then `y[n] = y[n-1] + alpha*(x[n]-y[n-1])`.

This is a deliberately small first step: it validates physical units, sample-rate preparation, state handling, and a repeatable test harness. It is not yet intended to color the audible PG Preamp signal path.

## Next candidates

1. Validate the RC stage against frequency-response and transient reference calculations.
2. Add passive networks built from validated R/C elements.
3. Add transformer behavior only with a documented model and tests (nonlinear magnetization, hysteresis/core loss as appropriate).
4. Add the active gain stage after deciding its actual topology and operating assumptions.

The engine must remain independent of the VST3 wrapper so it can later be reused by PG Optical Compressor.
