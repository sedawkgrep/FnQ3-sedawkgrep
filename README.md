<div align="center">

# FnQ3
Fappin' and Fraggin'

<p>
  <a href="BUILD.md"><img alt="Build Guide" src="https://img.shields.io/badge/Build-Guide-1f2937?style=for-the-badge"></a>
  <a href="docs/fnquake3/TECHNICAL.md"><img alt="Technical Notes" src="https://img.shields.io/badge/Technical-Notes-0f766e?style=for-the-badge"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/License-GPL--2.0-c2410c?style=for-the-badge"></a>
</p>

</div>

**FnQ3** is a modern source-port for *Quake III Arena* built around a simple goal: enhance the modern Quake 3 experience whilst leaving the game code untouched.

## Features

- A flexible display pipeline with renderer selection, fullscreen and windowed mode controls, internal render scaling, **HDR**, anti-aliasing, and detailed **bloom** tuning. See the [Display Guide](docs/DISPLAY.md).
- Configurable **cel shading** for model entities, including map world models, player models, and the first-person weapon, with banded lighting and optional silhouette outlines. See the [Display Guide](docs/DISPLAY.md).
- Optional player highlighting with **rimlight** and **stencil border effects**, per-mode team or free colors, and teammate/enemy override colors. See the [Visuals guide](docs/VISUALS.md).
- Flexible 4:3-aware **aspect correction** for HUDs, menus, UI model widgets, and cinematics, with sensible widescreen options when you want them. See the [Aspect Correction guide](docs/ASPECT_CORRECTION.md).
- **Enhanced console** functionality, including configurable scaling, smoother scrolling, mouse interaction, text selection, drag and drop, and an optional live tab-completion popup. See the [Console guide](docs/CONSOLE.md).
- An expanded **screenshot system** with pattern-based naming, optional view metadata sidecars, watermark compositing, and OpenGL cube-map capture. See the [Screenshot guide](docs/SCREENSHOTS.md).
- CPMA/CNQ3-style **rainbow text color escapes**, with live preview right in the console input line.
- **SDL3** support for video, audio, and input on modern platforms.
- OpenAL is the default audio backend, with device selection, **spatial reverb, occlusion**, and an easy fallback to the original mixer. See the [Audio guide](docs/AUDIO.md).
- Compatibility support for Quake III Arena 1.17 **`.dm3` demo playback** alongside standard retail `.dm_XX` demo formats.
- Compatibility support for older **Quake 3 IHV / q3test `IBSP v43` maps** alongside standard retail Quake III Arena BSP content.
- Quick-and-simple compatibility support for **Quake Live `IBSP v47` BSPs**, including ignored advert data and `advertisement` entity fallback to `func_static`.
- Support for the **`novlcollapse`** shader keyword used by Quake Live materials.
- Support for **Quake Live BETA encrypted `.pk3` archives** alongside normal ZIP-based Quake 3 content packages.
- Compatibility support for **Quake II `.pak` archives**, including archive discovery, reading, seeking, and pure/download path handling alongside standard Quake 3 packages.
- Compatibility support for **Quake II `.wal` textures**, with automatic palette loading from `pics/colormap.pcx` during decode.

## Credits

- CPMA/CNQ3-style rainbow text color escape support is adapted from the CPMADevs CNQ3 project.
- IBSP v43 / Quake 3 IHV map loading support is adapted from Spearmint's `bsp_q3ihv.c` implementation.
- Legacy `.dm3` demo compatibility work was cross-checked against [WolfcamQL](https://github.com/brugal/wolfcamql).
- Quake Live BETA encrypted PK3 support uses the XOR table published by Luigi Auriemma's **qldec** (`quakelivedec.c`).
- Quake II `.pak` / `.wal` compatibility work was cross-checked against the DarkMatter-Q2 project.
- Upstream [Quake3e](https://github.com/ec-/Quake3e) upon which this is based.

## Quick Start

1. Build the engine for your platform, or grab a packaged build if you already have one.
2. Put the executable next to your Quake III Arena installation and data files.
3. Leave your original retail game assets alone.
4. Use [BUILD.md](BUILD.md) if you need the platform-specific build steps.

## Release Channels

- Tagged releases use semantic version tags such as `v0.1.0`.
- Nightly packages follow current mainline work and include the incremented build version, build date, and commit in the archive name.

## Documentation

- [Build Guide](BUILD.md) if you want to compile FnQuake3 yourself.
- [Display Guide](docs/DISPLAY.md) for renderer choice, video modes, render scaling, HDR, anti-aliasing, and bloom tuning.
- [Visuals Guide](docs/VISUALS.md) for player highlighting and the current visual presentation controls.
- [Aspect Correction Guide](docs/ASPECT_CORRECTION.md) for HUD, menu, and cinematic presentation options.
- [Audio Guide](docs/AUDIO.md) for backend selection, volume, music, spatial audio, and sound debugging tools.
- [Console Guide](docs/CONSOLE.md) for console scaling, interaction, completion, and appearance.
- [Screenshot Guide](docs/SCREENSHOTS.md) for capture commands, naming patterns, view metadata sidecars, watermarks, and cube-map export.
- [Technical Notes](docs/fnquake3/TECHNICAL.md) for maintainers, release flow, and repo conventions.
- [Agent Guide](AGENTS.md) for automation rules and local repository references.

## Project Priorities

1. Full compatibility with retail Quake III Arena and its demos.
2. Speed and efficiency.
3. Enhanced modern platform support.
4. Cross-platform support, with other platforms considered.

Current tracked base version: `0.1.0`
Current build version: `0.1.0`
