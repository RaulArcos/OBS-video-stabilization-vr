# VR Head Stabilizer for OBS

An OBS Studio video filter that cancels the small, fast head vibrations in a VR mirror window, so streams and recordings of VR games (made with iRacing in mind) stay steady. Deliberate head turns pass through; only the tremor is removed.

![Unstabilized raw full field of view next to the stabilized, cropped view](docs/demo.gif)

*Offline simulation of the filter on recorded gameplay, not a live OBS recording. Left: the raw, unstabilized full field of view. Right: the 0.1.0 stabilized result, which is cropped (5% baseline zoom plus dynamic zoom), so it shows slightly less of the scene.*

## Download and install

Release 0.1.0 targets **Windows x64 with OBS Studio 31 or newer**. Get `obs-vr-stabilizer-0.1.0-windows-x64.zip` from the [GitHub releases page](https://github.com/RaulArcos/OBS-video-stabilization-vr/releases), then:

1. Unzip it and close OBS.
2. Copy the `obs-vr-stabilizer` folder into `%ProgramData%\obs-studio\plugins\`.
3. Start OBS, right click your game capture source, open Filters and add "VR Head Stabilizer".

Details, updating, removal and troubleshooting: [docs/INSTALL.md](docs/INSTALL.md). What is in this release and its limits: [docs/RELEASE-0.1.0.md](docs/RELEASE-0.1.0.md).

Other platforms: the build files for them are kept in the repository, but no other package is built, verified or promised for 0.1.0.

The filter works on the source it is attached to. Mirror overlays attached to the same game capture move with it; separate sources, such as a webcam or overlay graphics, are not affected.

## How it works

Each frame, the filter measures how the whole image moved since the previous frame (shift and roll), learning over time which pixels belong to the cockpit and which to the scenery so that it tracks the head and not the car. Smooth motion is kept, the non-smooth part is cancelled, and the image is only shifted and rotated, never warped.

To hide the moving edges the image is zoomed in uniformly, with no stretching. The crop is a 5% baseline, and it can grow by at most 5 more percentage points when a correction needs room. Outward head turns follow smoothly near that limit, and the extra zoom eases back over a nominal 0.8 seconds. Large sudden turns can still make the zoom-in visible. This release has not been tested for stream performance.

## Settings

| Setting | Default | Effect |
|---|---|---|
| Crop zoom | 5 % | Baseline crop. Dynamic crop adds up to 5 percentage points when corrections need more room, then eases back. |
| Strength | 100 % | How much of the correction is applied. |
| Smoothing | 0.3 s | Higher removes slower vibration, but delays the start of head turns slightly. |
| Max roll correction | 0.4 deg | Limit for tilt correction, 0 disables it. |

Combined shift and roll are bounded by the crop budget. Bypass keeps only the baseline crop.

Advanced settings (fast-motion follow, recenter time, analysis resolution, bypass, statistics logging) are available in the filter properties. If turns feel floaty, lower Smoothing. If vibration remains, raise Smoothing or Crop zoom.

## Building

Visual Studio 2022 or 2026 with the C++ workload and CMake 3.28 or newer. The `windows-x64` preset uses the Visual Studio 18 2026 generator, so it needs a CMake that supports it.

```bash
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
cmake --install build_x64 --config RelWithDebInfo --prefix release/staging
python tools/package_release.py
```

Staging and packaging stay inside `release/`; nothing is installed into OBS. The full maintainer checklist is in [docs/RELEASE-0.1.0.md](docs/RELEASE-0.1.0.md).

All GitHub Actions entry points are manual (`workflow_dispatch`); the single launcher is "Manual Build and Checks". Pushes, tags and pull requests do not trigger builds, and no workflow creates a GitHub release.

The first configure downloads the OBS sources and dependencies into `.deps/` and builds libobs. The `tools/` folder contains the Python scripts used to tune and verify the filter against real and synthetic footage, and `vrstab-test` validates the motion estimator offline.

## License

GPL-2.0-or-later, like OBS Studio.
