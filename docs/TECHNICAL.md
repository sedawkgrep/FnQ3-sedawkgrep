# Technical Notes

## Purpose

This file is the maintainer-facing companion to [`README.md`](/e:/Repositories/FnQuake3/README.md). Keep user installation guidance in the README and use this document for repo structure, release flow, and implementation conventions.

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

- [`code/`](/e:/Repositories/FnQuake3/code): engine and platform code.
- [`docs/`](/e:/Repositories/FnQuake3/docs): technical docs, upstream reference material, and README templates.
- [`version/`](/e:/Repositories/FnQuake3/version): shared project version metadata.
- [`scripts/`](/e:/Repositories/FnQuake3/scripts): repo-local automation for docs and release packaging.
- [`.install/`](/e:/Repositories/FnQuake3/.install): tracked distribution docs plus generated manifests and package archives.
- [`.tmp/`](/e:/Repositories/FnQuake3/.tmp): ignored scratch workspace for temporary outputs.

## Versioning

The canonical metadata lives in [`version/fnq3_version.h`](/e:/Repositories/FnQuake3/version/fnq3_version.h).

That header feeds:

- runtime version strings via [`code/qcommon/q_shared.h`](/e:/Repositories/FnQuake3/code/qcommon/q_shared.h)
- Windows resource metadata via [`code/win32/win_resource.rc`](/e:/Repositories/FnQuake3/code/win32/win_resource.rc)
- Make and CMake version reporting
- documentation rendering
- nightly and tagged release archive naming

Current policy:

- Tagged releases use semantic version tags in the form `vX.Y.Z`.
- Nightly builds keep the moving `nightly` tag, derive an incrementing build revision from git history, and stamp archive filenames with build version, date, and commit.
- The base version in `fnq3_version.h` should always represent the next intended stable release line.

## Docs Flow

The user-facing docs are generated from templates:

- [`docs/templates/README.md.in`](/e:/Repositories/FnQuake3/docs/templates/README.md.in)
- [`docs/templates/install-readme.html.in`](/e:/Repositories/FnQuake3/docs/templates/install-readme.html.in)

Refresh them with:

```powershell
python scripts/generate_docs.py
```

That command rewrites:

- [`README.md`](/e:/Repositories/FnQuake3/README.md)
- [`.install/README.html`](/e:/Repositories/FnQuake3/.install/README.html)

## Release Packaging

The packaging entry point is [`scripts/release.py`](/e:/Repositories/FnQuake3/scripts/release.py).
Nightly CI orchestration lives in [`scripts/nightly.py`](/e:/Repositories/FnQuake3/scripts/nightly.py).

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

[`.github/workflows/build.yml`](/e:/Repositories/FnQuake3/.github/workflows/build.yml) covers CI and tagged releases.
[`.github/workflows/nightly.yml`](/e:/Repositories/FnQuake3/.github/workflows/nightly.yml) owns scheduled and manual nightly publishing.

Expected behavior:

- pull requests build only
- `main` pushes validate the main branch without publishing a nightly release
- scheduled nightly runs refresh the moving nightly package only when `main` has advanced since the previous nightly tag
- published GitHub releases upload stable archives built from the tagged version

## Naming

Active build, packaging, and distribution surfaces should use `FnQuake3` naming consistently.
Historical upstream references should only remain where they are part of provenance, copyright notices, or archived material.
