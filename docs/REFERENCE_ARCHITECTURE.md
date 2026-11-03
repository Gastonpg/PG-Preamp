# PG Preamp: reference architecture and modeling boundaries

The PG Preamp is an original design. The Neve 1073 is a reference for the broad engineering approach, not a circuit to copy component-for-component.

## What the reference tells us

The classic 1073 signal path combines transformer-balanced inputs, discrete Class-A transistor gain stages, an equalizer using passive/inductive networks, and a transformer-balanced output. The microphone and line inputs use different transformer specifications; the output transformer is the LO1166. AMS Neve documents the 10468 microphone transformer, 31267 line transformer, LO1166 output transformer and Class-A discrete architecture:

- https://www.ams-neve.com/outboard/1073-range/1073-mic-preamp-equaliser/
- https://www.ams-neve.com/consoles/history-of-1073/

Archived service documentation identifies the 1073 channel-amplifier boards and distinct blocks (BA283, BA284, B182, B205 and B211), while noting that BA283 board variants are populated differently:

- https://technicalaudio.com/neve/neve_pdf/1073-fullpak.pdf
- https://medias.audiofanzine.com/files/neve-1073-1084-user-manual-issue5-478436.pdf

These references establish the kinds of physical systems involved. They do not provide every transformer parameter, operating point, parasitic or unit-to-unit measurement needed for a defensible exact digital clone.

## PG modeling plan

We will define our own signal path before implementing gain or coloration:

1. **Input network:** source impedance, termination, coupling and passive filtering.
2. **Input transformer model:** winding ratio, magnetizing inductance, leakage inductance, winding resistance, interwinding capacitance, core loss and nonlinear core behavior. Parameters must come from documented measurements or explicitly declared estimates.
3. **Active gain stage:** select a realizable transistor topology and bias model. A static waveshaper alone is not an adequate substitute for a biased active circuit.
4. **Passive network:** model resistors, capacitors and inductors, including relevant loss/parasitic terms. Test both frequency and transient responses.
5. **Output stage and transformer:** model headroom, output impedance, load dependence and magnetic behavior; validate against reference measurements.

## Modeling rules

- Separate physical component state from VST3 parameters and GUI code.
- Use SI units in the DSP model; convert controls only at the boundary.
- Preserve linear behavior at small signal levels where the reference is linear. Introduce nonlinearities only when supported by the physical model.
- Do not add arbitrary saturation, harmonic generators or noise merely to make a preset sound vintage.
- Keep noise sources attributable to components, with bandwidth and temperature assumptions recorded.
- Validate each block against analytical responses, SPICE/reference data or measured hardware before real-time integration.
- Record model limitations and parameter provenance. A simulated circuit is only as accurate as its topology and component data.

## Current component inventory

- Ideal passive RC network with exact discrete-time coefficient.
- Resistor thermal-noise density and bandwidth-integrated RMS noise, plus an optional seeded test source.
- Current-driven capacitor with ESR and leakage resistance.
- Linear inductor with series winding resistance.
- Static Ebers-Moll NPN junction model with forward/reverse beta and temperature dependence.

These are component-level foundations only; they are not yet wired into the Preamp audio path. Capacitor dielectric absorption, transistor charge storage and nonlinear magnetic effects remain separately testable extensions.
