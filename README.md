# PG Preamp

PG Preamp is an original PG Audio VST3 project.

## Current milestone: v0.5 analog-inspired preamp channel

- Native VST3 effect structure.
- Stereo input and output.
- Asymmetric soft saturation with an antialiasing stage, voiced for adjustable warmth and smoother peaks.
- A three-band EQ with 110 Hz low shelf, 1.6 kHz mid peak, and 12 kHz high shelf, using classic 1073 frequency landmarks.
- A rack-style dark metal front panel, black rotary knobs, pale position markers, and editable readouts.
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

The tonal direction takes inspiration from classic Class-A, transformer-coupled preamps and the musical EQ landmarks of the 1073. This project does not simulate that hardware's discrete circuit, transformers, or inductors and is not an exact emulation.
