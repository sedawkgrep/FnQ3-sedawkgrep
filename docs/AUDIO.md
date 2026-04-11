# Audio Guide

FnQuake3 keeps Quake III's familiar sound commands and music flow, then layers a more modern backend model on top. The default path now uses OpenAL, which means you can keep the classic feel, use a more capable spatial pipeline, or drop back to the original mixer when you need strict fallback behavior.

This guide focuses on the player-facing audio controls: backend selection, device choice, master and music volume, focus muting, spatial audio, legacy mixer tuning, and the commands that help you verify what the engine actually started.

## Overview

If you want the short version first:

- `s_backend openal`: Use the default OpenAL backend with spatial audio support.
- `s_backend legacy`: Use the original software mixer and platform device path instead.
- `s_backendActive`: Read-only cvar that reports which backend actually started.
- `s_alDevice`: Pick a specific OpenAL playback device. Leave it blank for the system default device.
- `s_volume`: Set the master volume for all game audio.
- `s_musicVolume`: Set the music volume independently of gameplay sounds.
- `s_muteWhenUnfocused 1`: Mute audio when the game window loses focus.
- `s_muteWhenMinimized 1`: Mute audio when the game is minimized.
- `s_alReverb 1`: Enable OpenAL environmental reverb when the device supports EFX. Requires `snd_restart`.
- `s_alOcclusion 1`: Enable geometry-based occlusion on the OpenAL backend.
- `s_alReverbGain`: Scale reverb send level from `0` to `2`.
- `s_alOcclusionStrength`: Scale occlusion strength from `0` to `2`.
- `s_doppler 1`: Enable doppler shift on moving projectiles.
- `s_info`: Print the active backend, device, and runtime audio state.
- `snd_restart`: Reinitialize the sound system after backend or latched-device changes.

## Backend Selection

FnQuake3 exposes two audio paths on the client side.

- `openal`: The default backend. This is the modern path and the only one that provides the current spatial audio controls such as reverb, occlusion, device selection, and the spatial debug tools.
- `legacy`: The original Quake III mixer and output-device path. Use this when you want the classic backend behavior or when OpenAL is unavailable on a given system.

Behavior notes:

- `s_backend` is latched, so change it and then run `snd_restart`.
- `s_backendActive` tells you what actually initialized, not just what you asked for.
- If OpenAL fails to initialize, FnQuake3 falls back to the legacy backend automatically instead of leaving you without sound.
- `s_info` is the quickest way to confirm the active backend and inspect the runtime state after a restart.

Example:

```cfg
seta s_backend "openal"
snd_restart
```

If you want the classic mixer instead:

```cfg
seta s_backend "legacy"
snd_restart
```

## OpenAL Device Selection

When `s_backend` is set to `openal`, `s_alDevice` lets you choose which playback device OpenAL should open.

- Leave `s_alDevice` blank to use the system default device.
- Set `s_alDevice` to a device name if you want FnQuake3 to target a specific headset, speakers, or virtual audio device.
- `s_alDevice` is latched, so you must run `snd_restart` after changing it.
- `s_info` prints both the requested device and the active device, which is useful when the system falls back to a different output.

Example:

```cfg
seta s_backend "openal"
seta s_alDevice ""
snd_restart
```

## Volume, Music, And Focus Muting

The main day-to-day controls are intentionally simple.

- `s_volume`: Master volume for game audio. Range `0` to `1`. Default `0.8`.
- `s_musicVolume`: Music-only volume. Range `0` to `1`. Default `0.25`.
- `s_muteWhenUnfocused`: Mute audio when the window is no longer focused. Default `1`.
- `s_muteWhenMinimized`: Mute audio when the game is minimized. Default `1`.

Practical guidance:

- Lower `s_musicVolume` first if the soundtrack is stepping on match clarity.
- Use `s_volume` when the whole mix is too loud or too quiet.
- If the game seems to "lose" audio after task switching, check the two focus-muting cvars before assuming the backend is broken.
- Focus muting applies to both backends.

Examples:

```cfg
seta s_volume "0.65"
seta s_musicVolume "0.15"
```

Keep audio playing while alt-tabbed:

```cfg
seta s_muteWhenUnfocused "0"
seta s_muteWhenMinimized "0"
```

## Spatial Audio

Spatial audio lives on the OpenAL backend. It is enabled by default, but the individual features remain adjustable so you can tune the mix instead of treating it as all-or-nothing.

### Reverb

- `s_alReverb 1`: Enable environmental reverb sends on OpenAL devices that expose EFX support.
- `s_alReverb 0`: Disable the OpenAL reverb path.
- `s_alReverbGain`: Scale the wet reverb level from `0` to `2`. Default `1.0`.

Important details:

- Reverb requires an OpenAL device with EFX support.
- `s_alReverb` is latched and requires `snd_restart` to apply cleanly.
- `s_info` reports whether EFX support is available and whether the reverb send is active on the current device.

### Occlusion

- `s_alOcclusion 1`: Enable world-geometry occlusion checks.
- `s_alOcclusion 0`: Disable occlusion.
- `s_alOcclusionStrength`: Scale how strongly occluded sounds are muffled. Range `0` to `2`. Default `1.0`.

Occlusion is useful when you want walls, doors, and arena structure to affect how remote sounds read. If the result feels too dull, reducing `s_alOcclusionStrength` is usually a better first move than disabling the feature entirely.

### Doppler

- `s_doppler 1`: Enable doppler shift on moving projectiles.
- `s_doppler 0`: Disable doppler shift.

This setting affects both backends, but it matters most when the OpenAL path is doing the rest of the spatial work.

### Recommended OpenAL Starting Point

This is a solid default if you want FnQuake3's modern audio path without pushing the mix into something exaggerated:

```cfg
seta s_backend "openal"
seta s_alDevice ""
seta s_volume "0.8"
seta s_musicVolume "0.25"
seta s_doppler "1"
seta s_alReverb "1"
seta s_alReverbGain "1.0"
seta s_alOcclusion "1"
seta s_alOcclusionStrength "1.0"
snd_restart
```

## Legacy Backend Tuning

The legacy mixer remains available when you want the original software path or when OpenAL is not appropriate for a given machine.

The main legacy-specific controls are:

- `s_khz`: Output sampling rate for the legacy backend. Valid values are `8`, `11`, `22`, `44`, and `48`. Default `22`. Requires `snd_restart`.
- `s_mixAhead`: Amount of audio to pre-mix ahead of playback. Default `0.2`.
- `s_mixOffset`: Developer-facing timing offset for the legacy mixer. Range `0` to `0.5`.
- `s_device`: ALSA output device selector on Linux builds that use the non-SDL ALSA path.

Practical guidance:

- Higher `s_khz` values can improve fidelity, but they are not automatically the best choice on every machine.
- `s_mixAhead` is a stability-vs-latency control. Higher values can help with crackle or starvation on unstable systems, but they also push the mixer further ahead.
- `s_mixOffset` is an advanced knob and is usually best left at `0` unless you are deliberately testing mixer timing behavior.

Example legacy profile:

```cfg
seta s_backend "legacy"
seta s_khz "44"
seta s_mixAhead "0.2"
snd_restart
```

## Useful Commands

FnQuake3 keeps the classic sound commands and adds a few OpenAL-oriented inspection tools.

- `s_info`: Print the current backend, device, EFX support, reverb state, occlusion state, source counts, sample counts, and background-track state.
- `s_list`: List registered samples and whether each one is loaded, unloaded, or missing.
- `s_stop`: Stop active sounds.
- `play <soundfile>`: Play one or more sound files locally for a quick spot check.
- `music <intro> [loop]`: Start background music playback.
- `stopmusic`: Stop the current background track.
- `snd_restart`: Restart the whole sound system after backend or latched setting changes.
- `s_alDebugDump`: Print a spatial-audio debug snapshot on the OpenAL backend.

Examples:

```cfg
play sound/player/jump1.wav
```

```cfg
music music/fla22k_02.wav
```

## Spatial Debug Tools

If you are tuning the OpenAL path and want more than a yes-or-no answer, FnQuake3 exposes both console and overlay debug tools.

- `s_alDebugDump`: Prints a spatial snapshot for the currently inspected voice.
- `s_alDebugOverlay 0`: Disable the OpenAL debug overlay.
- `s_alDebugOverlay 1`: Show summary environment and selected voice state.
- `s_alDebugOverlay 2`: Add more detailed sample and gain information.
- `s_alDebugVoice -1`: Automatically inspect the nearest active voice.
- `s_alDebugVoice <entityNum>`: Lock the debug tools to a specific entity when you want to inspect a known source.

These tools are only meaningful on the OpenAL backend.

## Troubleshooting

### I changed a setting and nothing happened

Some audio cvars are latched. If you change any of the following, run `snd_restart` before judging the result:

- `s_backend`
- `s_alDevice`
- `s_alReverb`
- `s_khz`

### Spatial audio sounds flat

Check the basics in this order:

- Run `s_info` and confirm `s_backendActive` is `openal`.
- If you expect reverb, confirm that `s_info` reports `EFX support: enabled`.
- Confirm `s_alReverb 1` and `s_alOcclusion 1`.
- If the mix is too subtle, try increasing `s_alReverbGain` or `s_alOcclusionStrength` in small steps instead of jumping straight to extremes.

### The wrong output device started

- Set `s_alDevice` explicitly.
- Run `snd_restart`.
- Run `s_info` again and compare the requested device against the active device.

### The game goes silent after alt-tabbing

That may be intentional rather than a failure:

- `s_muteWhenUnfocused 1` mutes audio when the window is not focused.
- `s_muteWhenMinimized 1` mutes audio when the game is minimized.

If you want audio to continue while the game is in the background, disable one or both.

### I want the safest fallback path

Use the legacy backend:

```cfg
seta s_backend "legacy"
snd_restart
```

## Recommended Starting Points

### Modern Default

Use this if you want FnQuake3's intended current audio path:

- `s_backend openal`
- `s_alDevice ""`
- `s_alReverb 1`
- `s_alOcclusion 1`
- `s_volume 0.8`
- `s_musicVolume 0.25`

### Competitive And Dry

Use this if you want less ambience and more direct positional cues:

- `s_backend openal`
- `s_alReverb 0`
- `s_alOcclusion 1`
- `s_alOcclusionStrength 0.5`
- `s_musicVolume 0`

### Conservative Compatibility

Use this if you want the original mixer path:

- `s_backend legacy`
- `s_khz 22`
- `s_mixAhead 0.2`

## Related Guides

- [Console Guide](CONSOLE.md) for command entry, completion, and console-side workflow.
- [Technical Notes](fnquake3/TECHNICAL.md) for repository and release documentation rather than player settings.
