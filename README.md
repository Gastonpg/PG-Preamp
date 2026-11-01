# PG Preamp

PG Preamp is an original PG Audio VST3 project.

## Current milestone: v0.4 1073-inspired preamp channel

- Native VST3 effect structure.
- Stereo input and output.
- Warm, asymmetric preamp saturation with a 3-band fixed-frequency EQ inspired by the Neve 1073 channel strip.
- A dark editor with black rotary knobs and white position markers.
- Automatable Drive, Low, Mid, High, and Output controls with host project-state saving.
- Parameter smoothing to reduce clicks when controls move.
- Windows x64 build through GitHub Actions.
- No local compiler or SDK installation required for the cloud build.

## Build

Open the **Actions** tab and run **Build PG Preamp VST3** using **Run workflow**, or push a commit to trigger a build. When it succeeds, download the `PG-Preamp-Windows-x64` artifact from that workflow run.

The downloaded artifact contains the `.vst3` bundle directly. Extract it once and install that folder in the VST3 location supported by the DAW.

## Roadmap

1. Confirm the editor and controls work in FL Studio.
2. Measure distortion and aliasing across common sample rates.
3. Refine the preamp stage and tone curves after listening tests.

This project is not an exact emulation of any commercial hardware.

