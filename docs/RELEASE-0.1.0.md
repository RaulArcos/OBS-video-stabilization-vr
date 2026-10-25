# VR Head Stabilizer 0.1.0

First public release: an OBS Studio filter that cancels small, fast head vibrations in a VR game capture while letting deliberate head turns through.

## What it does

- Measures whole-image shift and roll each frame, keeps smooth motion and cancels the rest. The image is only shifted and rotated, never warped.
- Hides the moving edges with a uniform zoom (no stretching): a 5% baseline plus at most 5 more percentage points when a correction needs room.
- Follows outward head turns smoothly near the crop limit and eases the extra zoom back over a nominal 0.8 seconds.
- Settings: crop zoom, strength, smoothing and max roll correction, plus advanced follow, recenter, analysis resolution, bypass and logging options.

## Requirements

Windows x64 and OBS Studio 31 or newer. Build files for other platforms are kept in the repository, but no other package is part of this release.

## Assets

| File | Purpose |
|---|---|
| `obs-vr-stabilizer-0.1.0-windows-x64.zip` | Plugin folder, README, LICENSE and install/release docs. |
| `obs-vr-stabilizer-0.1.0-windows-x64.zip.sha256` | SHA-256 checksum of the ZIP. |

Installation steps: [INSTALL.md](INSTALL.md).

## Known limitations

- The comparison GIF is an offline simulation of the filter on recorded gameplay, not a live OBS recording. Live stream performance has not been tested.
- The crop and zoom are limited. Large sudden turns can still show the zoom-in or edges.
- Mirror overlays attached to the same game capture move with the image; separate sources do not.
- The DLL is unsigned and there is no installer. Portable OBS installs are not verified.

## Maintainer checklist (manual, nothing publishes automatically)

Needs Visual Studio with the C++ workload, CMake 3.28+, and Python 3.12+ for the packager. The `windows-x64` preset uses the Visual Studio 18 2026 generator, so it needs a matching CMake; on this project's machine that is the CMake bundled with Visual Studio. Do not install into `%ProgramData%`.

- [ ] Configure and build:
  ```bash
  cmake --preset windows-x64
  cmake --build build_x64 --config RelWithDebInfo
  ```
- [ ] Run the tests: `ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure`
- [ ] Stage into the repository: `cmake --install build_x64 --config RelWithDebInfo --prefix release/staging`. This writes `release/staging/obs-vr-stabilizer`.
- [ ] Package: `python tools/package_release.py`. It writes `release/obs-vr-stabilizer-0.1.0-windows-x64.zip` and `.zip.sha256` (name and version come from `buildspec.json`), leaves out `.pdb` files and adds `README.md`, `LICENSE`, `docs/INSTALL.md` and this file.
- [ ] Check packaging and workflow scripts: `python tests/package_release_test.py` and `python tests/workflows_test.py` (the latter needs PyYAML).
- [ ] Verify the ZIP against its `.sha256` and inspect its layout: `obs-vr-stabilizer/bin/64bit/*.dll` and `obs-vr-stabilizer/data/{locale,shaders}`.
- [ ] Upload the assets to a GitHub release by hand.

CI is the single manual workflow "Manual Build and Checks" (`.github/workflows/manual-build.yaml`). Pushes, pull requests and tags do not trigger it, and it never creates a release.
