# PG Preamp

PG Preamp is an original PG Audio VST3 project.

## Current milestone: v0.1 skeleton

- Native VST3 effect structure.
- Stereo input and output.
- Transparent pass-through audio path (no coloration yet).
- Initial host bypass parameter.
- Windows x64 build through GitHub Actions.
- No local compiler or SDK installation required for the cloud build.

## Build

Open the **Actions** tab and run **Build PG Preamp VST3** using **Run workflow**, or push a commit to trigger a build. When it succeeds, download the `PG-Preamp-Windows-x64` artifact from that workflow run.

The ZIP contains the VST3 bundle. Extract it and install the `.vst3` folder in the VST3 location supported by the DAW.

## Roadmap

1. Confirm the minimal plugin loads in FL Studio.
2. Add proper parameter state and controls.
3. Implement and measure the first nonlinear audio stage.
4. Add transformer/stage models only after validating each one.
5. Design the custom PG interface after the DSP works.

This project is not an exact emulation of any commercial hardware.
