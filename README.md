# VR Head Stabilizer for OBS

An OBS Studio video filter that removes the small, fast head vibrations from a VR game capture (iRacing, or any other VR mirror window) while keeping the image intact and letting intentional head movement through naturally.

- **No distortion**: the image is only shifted and very slightly rotated. It is never warped or bent.
- **Intentional movement passes through**: deliberate head turns are reproduced 1:1. Only the non-smooth part of the motion (tremor, pulse, capture judder) is cancelled.
- **Small fixed zoom**: a configurable crop (5% by default) provides the room needed to move the image without exposing edges.
- **Cheap**: one small GPU downsample per frame, a couple of milliseconds of CPU for motion analysis, and a single GPU warp pass. Nothing that competes with the VR rendering.
- Works on the game capture only, so overlays, chat and webcam sources stay perfectly still.

## Installation

1. Download the release zip (or build it, see below).
2. Copy the `obs-vr-stabilizer` folder into `%ProgramData%\obs-studio\plugins\` so that you end up with `%ProgramData%\obs-studio\plugins\obs-vr-stabilizer\bin\64bit\obs-vr-stabilizer.dll` and `...\obs-vr-stabilizer\data\...`.
3. Start OBS. Right click your game capture source, choose **Filters**, add **VR Head Stabilizer**.

Requires OBS Studio 31 or newer on Windows x64.

## Settings

| Setting | Default | What it does |
|---|---|---|
| Crop zoom | 5 % | Zoom applied to hide the edges. Larger values allow bigger corrections. 4 to 6 % works well for 1080p. |
| Strength | 100 % | Fraction of the computed correction that is applied. |
| Smoothing | 0.3 s | How strongly motion is smoothed while the head is still. Higher cancels slower vibration, but delays the very start of a head turn slightly. |
| Max roll correction | 0.4° | Limit for the tilt correction. 0 disables rotation correction entirely. |
| Fast-motion follow (advanced) | 4 px/frame | Above this speed the smoothing is reduced so fast turns are followed 1:1. |
| Recenter time (advanced) | 0.6 s | How quickly the stabilized view drifts back to the real view. |
| Analysis resolution (advanced) | 320 | Width of the internal image used to measure motion. |
| Bypass (advanced) | off | Shows the original with the same zoom, for before/after comparison. |
| Log statistics (advanced) | off | Writes timing and confidence statistics to the OBS log every 5 seconds. |

If you see the image sliding or lagging behind fast head turns, lower **Smoothing** or raise **Fast-motion follow**. If vibration is still visible when the head is still, raise **Smoothing** or **Crop zoom**.

## How it works

Every frame the filter renders the source into a texture, produces a small grayscale copy on the GPU, and reads that copy back to the CPU. A pyramidal, robustly weighted Lucas-Kanade alignment measures the rigid motion (shift and rotation) between the previous and the current small frame. Moving cars, the mouse cursor and other outliers are down-weighted automatically.

The measured motion feeds a per-axis filter:

```
displayed velocity = smoothed actual velocity + spring toward the real position
```

Smooth motion is passed through, so a deliberate head turn is reproduced exactly. Tremor and the alternating big/small steps caused by capturing an 89 fps game at 60 fps are cancelled. The spring keeps the correction from drifting, and a soft limiter keeps it inside the crop margin. The final image is drawn with a single GPU pass that applies zoom, rotation and shift with bilinear sampling.

The estimator also keeps a temporal consistency map: pixels whose motion keeps agreeing with the rigid model (the cockpit, the HUD) gain weight, pixels that move on their own (the track streaming past, mirrors, other cars) lose weight. That way the head motion is measured on the cockpit rather than on the scenery.

The reference implementation of the filter lives in `tools/stabfilter.py` and is kept in sync with `src/stabilizer.cpp`.

## Measured results

Measured on the real plugin output recorded by OBS (RTX 5070 Ti, 1080p60):

| Test | Tremor above 4 Hz, before | after | removed |
|---|---|---|---|
| Synthetic clip with known tremor, x | 2.8 px rms, p99 6.2 px | 0.6 px rms, p99 1.6 px | 79 % |
| Synthetic clip with known tremor, y | 2.4 px rms, p99 5.2 px | 0.3 px rms, p99 0.6 px | 88 % |
| Real iRacing VR recording, cockpit regions | 1.4 to 2.5 px rms | 0.7 to 1.0 px rms | 45 to 60 % (measurement noise floor is about 1 px) |

Cost per frame inside OBS: about 1.5 to 2 ms of CPU for the motion estimate and 0.5 ms for the GPU readback, at 60 fps. Deliberate head turns pass through; during a fast turn the view can trail the real view by up to the crop margin (about 35 px at 5 % zoom) and catches up within a second.

## Building

Requirements: Visual Studio 2022 or 2026 with the C++ workload, CMake 3.28+ (the one bundled with Visual Studio works), Git.

```bash
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
```

The first configure downloads the OBS sources and prebuilt dependencies into `.deps/` and builds libobs, which takes a few minutes. The plugin ends up in `build_x64/rundir/RelWithDebInfo/`. To install into your local OBS:

```bash
cmake --install build_x64 --config RelWithDebInfo
```

### Offline tools

The `tools/` folder contains the scripts used to tune the filter against a real recording (Python 3 with numpy and opencv-python):

```bash
python tools/analyze_motion.py test.mp4 --out tools/out/motion.csv      # measure motion per frame
python tools/simulate.py tools/out/motion.csv --sweep                    # compare filter settings
python tools/simulate.py tools/out/motion.csv --render test.mp4 --start 5 --dur 20 --out tools/out/preview.mp4
```

`tools/make_synth.py` renders a synthetic clip with a known trajectory (and `ideal.mp4`, the same clip without tremor); record it through the plugin in OBS and run `tools/eval_synth.py <recording>` to measure the residual tremor exactly.

`vrstab-test` (built alongside the plugin) validates the C++ motion estimator on synthetic shifts and on raw gray frames extracted with ffmpeg:

```bash
ffmpeg -i test.mp4 -vf scale=320:180:flags=area -pix_fmt gray -f rawvideo tools/out/test_320x180.gray
build_x64/RelWithDebInfo/vrstab-test synth tools/out/test_320x180.gray 320 180
build_x64/RelWithDebInfo/vrstab-test video tools/out/test_320x180.gray 320 180 1920 tools/out/motion_cpp.csv
```

## License

GPL-2.0-or-later, like OBS Studio itself.
