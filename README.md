# PG Preamp

PG Preamp is an original PG Audio VST3 project.

## Current milestone: v0.3 warm preamp with a basic editor

- Native VST3 effect structure.
- Stereo input and output.
- Warm, asymmetric valve-style soft clipping stage.
- Basic editor with Drive and Output sliders and editable dB fields.
- Automatable Drive and Output controls with host project-state saving.
- Parameter smoothing to reduce clicks when controls move.
- Windows x64 build through GitHub Actions.
- No local compiler or SDK installation required for the cloud build.

## Build

Open the **Actions** tab and run **Build PG Preamp VST3** using **Run workflow**, or push a commit to trigger a build. When it succeeds, download the `PG-Preamp-Windows-x64` artifact from that workflow run.

The downloaded artifact contains the `.vst3` bundle directly. Extract it once and install that folder in the VST3 location supported by the DAW.

## Roadmap

1. Confirm the editor opens and the controls work in FL Studio.
2. Measure distortion and aliasing across common sample rates.
3. Refine the valve stage and add tone shaping only after listening tests.

This project is not an exact emulation of any commercial hardware.

