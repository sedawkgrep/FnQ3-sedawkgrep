<div align="center">

# FnQuake3

Fappin' Quake III, shortened to FnQuake3 or FnQ3.

<p>
  <a href="BUILD.md"><img alt="Build Guide" src="https://img.shields.io/badge/Build-Guide-1f2937?style=for-the-badge"></a>
  <a href="docs/TECHNICAL.md"><img alt="Technical Notes" src="https://img.shields.io/badge/Technical-Notes-0f766e?style=for-the-badge"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/License-GPL--2.0-c2410c?style=for-the-badge"></a>
</p>

</div>

FnQuake3 is a modernized Quake III Arena engine project focused on retail compatibility, demo compatibility, speed, and broad platform coverage.

## What Matters

- Retail Quake III Arena compatibility is a hard requirement.
- Demo playback compatibility is treated as a hard requirement.
- Performance work should stay practical and measurable.
- Windows, Linux, macOS, and ARM targets are first-class concerns.

## Features

- Broad 4:3-aware aspect correction for HUDs, menus, UI model widgets, cinematics, and optional console font virtualization. See the embedded [Aspect Correction guide](docs/ASPECT_CORRECTION.md).
- SDL3 backend support for video, audio, and input on modern platforms.
- Compatibility-first engine modernization that keeps retail Quake III Arena assets, protocol behavior, and demos as first-class concerns.
- Multiple renderer and platform targets aimed at practical performance across legacy and modern systems.

## Quick Start

1. Build the engine for your platform or use a packaged build.
2. Put the executable next to your Quake III Arena installation and data files.
3. Keep your original retail game assets intact.
4. Use [BUILD.md](BUILD.md) for platform-specific build instructions.

## Release Channels

- Tagged releases use semantic version tags such as `v0.1.0`.
- Nightly packages track current mainline work and include the build date plus commit in the archive name.

## Documentation

- [Build Guide](BUILD.md) for compiling on supported platforms.
- [Aspect Correction Guide](docs/ASPECT_CORRECTION.md) for HUD, menu, cinematic, and console scaling behavior.
- [Technical Notes](docs/TECHNICAL.md) for maintainers, release flow, and repo conventions.
- [Agent Guide](AGENTS.md) for automation rules and local repository references.

## Project Priorities

1. Full compatibility with retail Quake III Arena and its demos.
2. Speed and efficiency.
3. Enhanced modern platform support.
4. Cross-platform support, with other platforms considered.

Current tracked base version: `0.1.0`
