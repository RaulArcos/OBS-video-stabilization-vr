# Install VR Head Stabilizer 0.1.0

Copy one folder into `%ProgramData%\obs-studio\plugins\` with OBS closed, then add the filter to your game capture. This guide covers the standard Windows x64 install of OBS Studio.

## Prerequisites

- Windows x64 and OBS Studio 31 or newer (64-bit).
- Permission to write to `%ProgramData%\obs-studio\plugins\`. Creating that folder, or copying into it, may need administrator approval.
- The release ZIP `obs-vr-stabilizer-0.1.0-windows-x64.zip`, downloaded from the [GitHub releases page](https://github.com/RaulArcos/OBS-video-stabilization-vr/releases) and compared with the published `.sha256` file if you like.

The DLL is not code-signed and there is no installer, so Windows may show a warning when OBS first loads it.

## Quick path

1. Close OBS Studio.
2. Unzip the ZIP into a new folder. It contains `obs-vr-stabilizer/`, plus `README.md`, `LICENSE` and `docs/`.
3. Copy the whole `obs-vr-stabilizer` folder into `%ProgramData%\obs-studio\plugins\`.
4. Start OBS, right click your game capture source, choose Filters, press `+` and add "VR Head Stabilizer".

The installed folder must look like this, without a second `obs-vr-stabilizer` level inside it:

```text
%ProgramData%\obs-studio\plugins\obs-vr-stabilizer\
  bin\64bit\obs-vr-stabilizer.dll
  data\locale\en-US.ini
  data\locale\es-ES.ini
  data\shaders\luma-downsample.effect
  data\shaders\warp.effect
```

Copy the whole folder, not only the DLL: the filter needs its shaders and locale files.

## Portable OBS

Not verified for 0.1.0. This project only documents and tests the standard `%ProgramData%\obs-studio\plugins\` install.

## Update

1. Close OBS.
2. Rename or copy the existing `%ProgramData%\obs-studio\plugins\obs-vr-stabilizer` folder somewhere safe as a backup.
3. Replace it with the new `obs-vr-stabilizer` folder from the ZIP. Only plugin files are replaced; OBS itself is untouched.
4. Start OBS. Your scenes and filter settings are kept, because OBS stores them in its own profile and scene files, which this process does not touch.

## Uninstall

1. Remove the "VR Head Stabilizer" filter from each source that uses it (optional, but avoids a "missing filter" entry).
2. Close OBS.
3. Delete the `%ProgramData%\obs-studio\plugins\obs-vr-stabilizer` folder.

Scenes, sources and other plugins are not touched.

## Check that it works

1. Add the filter to a game capture and keep your head still: the image should stay steady.
2. Move slowly, then quickly, while watching a mirror or fixed cockpit edge. Slow, deliberate turns should pass through; vibration should be reduced.
3. Stop the game or the head movement and return to the start position with the same settings: the view should settle back without a lasting offset.
4. Make a short local recording and review it. Do not judge only from the preview.

Mirror overlays that belong to the same game capture move with the image. Sources outside that capture are not affected.

## Troubleshooting

| Symptom | Check |
|---|---|
| "VR Head Stabilizer" is not in the filter list | OBS was restarted after copying; OBS is 31+ and 64-bit; the DLL is at `plugins\obs-vr-stabilizer\bin\64bit\`, not nested one level deeper; the filter is added to a video source, not an audio one. Look in the OBS log (Help > Log Files) for lines mentioning `obs-vr-stabilizer`. |
| Filter appears but names look like `VRStabilizer.Name` | The `data\locale` folder is missing. Copy the whole folder, not only the DLL. |
| Filter appears but the image is black or shaders fail in the log | The `data\shaders` folder is missing or incomplete. Copy the whole folder. |
| Image lags or stutters | Disable the filter (or enable Bypass) to see whether the lag comes from CPU or GPU load elsewhere in the scene or from the filter. Lower Analysis resolution in the advanced settings. |
| Zoom seems to "breathe" | The crop grows when a correction needs room and eases back. Lower Strength or raise Crop zoom if it is distracting. |
| Vibration remains, or turns feel floaty | Raise or lower Smoothing as described in the [README](../README.md#settings). |

When asking for help, include the OBS version and the OBS log.
