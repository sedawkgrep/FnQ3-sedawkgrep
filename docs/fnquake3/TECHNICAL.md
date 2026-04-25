# Technical Notes

## Purpose

This file is the maintainer-facing companion to [`README.md`](../../README.md). Keep user installation guidance in the README and use this document for repo structure, release flow, and implementation conventions.

## Project Constraints

FnQuake3 exists to modernize Quake III Arena without losing the properties that make it a long-lived engine target:

1. Retail Quake III Arena compatibility stays intact.
2. Demo playback compatibility stays intact.
3. Performance regressions need a clear reason and measurement.
4. Platform additions should not silently narrow the supported matrix.

Compatibility-sensitive areas include:

- demo parsing and recording
- network protocol behavior
- filesystem search order and pak loading
- VM ABI and bytecode execution
- renderer defaults that affect demo output or deterministic behavior

## Repository Layout

- [`code/`](../../code): engine and platform code.
- [`docs/`](../../docs): technical docs, upstream reference material, and README templates.
- [`version/`](../../version): shared project version metadata.
- [`scripts/`](../../scripts): repo-local automation for docs and release packaging.
- [`.install/`](../../.install): tracked distribution docs plus generated manifests and package archives.
- [`.tmp/`](../../.tmp): ignored scratch workspace for temporary outputs.

## Versioning

The canonical metadata lives in [`version/fnq3_version.h`](../../version/fnq3_version.h).

That header feeds:

- runtime version strings via [`code/qcommon/q_shared.h`](../../code/qcommon/q_shared.h)
- Windows resource metadata via [`code/win32/win_resource.rc`](../../code/win32/win_resource.rc)
- Make and CMake version reporting
- documentation rendering
- nightly and tagged release archive naming

Current policy:

- Tagged releases use semantic version tags in the form `vX.Y.Z`.
- Nightly builds produce a unique tag per build (e.g. `nightly-0.1.0.42-20240403-abc12345`), combining the build version, date, and commit for a persistent per-build release.
- The base version in `fnq3_version.h` should always represent the next intended stable release line.
- Release-facing change history lives in [`docs/fnquake3/CHANGELOG.md`](./CHANGELOG.md). Keep the `Unreleased` section current as work lands.
- Use [`scripts/changelog.py`](../../scripts/changelog.py) to extract a section or promote `Unreleased` into a dated release section during tagging.

Typical changelog helper usage:

```powershell
python scripts/changelog.py section --version Unreleased
python scripts/changelog.py prepare-release --version 0.1.0 --date 2026-04-25
```

## Docs Flow

The user-facing docs are generated from templates:

- [`docs/templates/README.md.in`](../templates/README.md.in)
- [`docs/templates/install-readme.html.in`](../templates/install-readme.html.in)

Refresh them with:

```powershell
python scripts/generate_docs.py
```

That command rewrites:

- [`README.md`](../../README.md)
- [`.install/README.html`](../../.install/README.html)

## Release Packaging

The packaging entry point is [`scripts/release.py`](../../scripts/release.py).
Nightly CI orchestration lives in [`scripts/nightly.py`](../../scripts/nightly.py).

Typical local usage:

```powershell
python scripts/nightly.py summary
python scripts/release.py --channel nightly --artifact-root <downloaded-artifacts-dir>
python scripts/release.py --channel release --artifact-root <downloaded-artifacts-dir> --ref-name v0.1.0
```

The script:

1. refreshes generated docs
2. stages each platform artifact under `.tmp/release/`
3. injects shared docs into the staged package
4. writes versioned `.zip` archives into `.install/packages/`
5. emits `.install/release-manifest.json` and `.install/SHA256SUMS.txt`

## CI Notes

[`.github/workflows/nightly.yml`](../../.github/workflows/nightly.yml) owns scheduled and manual nightly publishing.

Expected behavior:

- pull requests build only
- `main` pushes validate the main branch without publishing a nightly release
- scheduled nightly runs produce a new unique-tagged release per build when `main` has advanced since the last nightly
- published GitHub releases upload stable archives built from the tagged version

## Audio Backend Notes

- The default client audio path is the OpenAL backend selected by `s_backend openal`.
- `s_backend legacy` keeps the original Quake III mixer/device backend available as a fallback path.
- OpenAL headers are vendored under [`code/openal/include`](../../code/openal/include), and Windows x86/x64 package builds stage a matching `OpenAL32.dll` from [`code/openal/windows`](../../code/openal/windows).
- The runtime reporting cvar is `s_backendActive`. Device selection for the OpenAL backend uses `s_alDevice`.
- The OpenAL backend also exposes `s_alReverb`, `s_alOcclusion`, `s_alReverbGain`, and `s_alOcclusionStrength` for the environmental spatial layer. Reverb enablement is latched because the EFX reverb slot is created at backend init.
- Keep dedicated-server builds free of the OpenAL runtime dependency.

## Naming

Active build, packaging, and distribution surfaces should use `FnQuake3` naming consistently.
Historical upstream references should only remain where they are part of provenance, copyright notices, or archived material.
