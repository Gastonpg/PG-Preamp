# PG Preamp

PG Preamp is an original PG Audio VST3 project.

## Current milestone: v0.6 optional circuit-color stages

- Native VST3 effect structure.
- Stereo input and output.
- The original asymmetric soft saturation remains the default sound, with an antialiasing stage.
- Optional A Stage: a normalized BJT exponential-current model with emitter feedback around a quiescent point.
- Optional Iron: leaky-flux transformer-core saturation blocks before and after the EQ, with independent input/output states.
- A three-band EQ with 110 Hz low shelf, 1.6 kHz mid peak, and 12 kHz high shelf, using classic 1073 frequency landmarks.
- A rack-style dark metal front panel, black rotary knobs, pale position markers, and editable readouts.
- A functional output VU meter with a read-only animated needle and level smoothing.
- The PG AUDIO mark reproduced from Gemini's original logo artwork in the plugin editor.
- Automatable Drive, Low, Mid, High, and Output controls with host project-state saving.
- Parameter smoothing to reduce clicks when controls move.
- Both optional stages default to off; older saved projects load with them disabled.
- Windows x64 build through GitHub Actions.
- No local compiler or SDK installation required for the cloud build.

## Build

Open the **Actions** tab and run **Build PG Preamp VST3** using **Run workflow**, or push a commit to trigger a build. When it succeeds, download the `PG-Preamp-Windows-x64` artifact from that workflow run.

The downloaded artifact contains the `.vst3` bundle directly. Extract it once and install that folder in the VST3 location supported by the DAW.

## Roadmap

1. Confirm the editor and controls work in FL Studio.
2. Measure distortion and aliasing across common sample rates.
3. Calibrate the optional BJT and transformer models against a reference circuit or hardware capture, then refine by listening.

The optional A Stage and Iron blocks are first-pass, physically motivated models, not an exact simulation of the 1073's component values, Class-A topology, magnetic hysteresis, or transformers. Their normalized parameters still need calibration against measured hardware or a trusted SPICE reference.
