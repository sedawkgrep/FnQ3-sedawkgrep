# AGENTS.md

## Mission

FnQuake3 is a modernized Quake III Arena engine branch. Every change should protect these project constraints:

1. Full compatibility with retail Quake III Arena and its demos.
2. Speed and efficiency in hot paths and tooling.
3. Modern platform support without regressing existing targets.
4. Cross-platform viability, even when a feature starts on one platform first.

## Repository Rules

- Treat demo, protocol, asset-loading, and VM behavior as compatibility-sensitive by default.
- Prefer incremental engine changes over broad rewrites unless a rewrite is the only coherent fix.
- Keep release packaging deterministic. `.install/` is the staged distribution area, not a scratchpad.
- Use `.tmp/` for temporary outputs, investigation notes, and disposable staging work.
- Do not ship documentation that mixes end-user guidance with maintainer detail. Keep the user surface in `README.md` and deeper material in linked technical docs.
- When versioning changes are required, update the canonical metadata in [`version/fnq3_version.h`](/e:/Repositories/FnQuake3/version/fnq3_version.h) first.

## Local References

- [`README.md`](/e:/Repositories/FnQuake3/README.md): end-user overview.
- [`BUILD.md`](/e:/Repositories/FnQuake3/BUILD.md): platform-specific build instructions.
- [`docs/TECHNICAL.md`](/e:/Repositories/FnQuake3/docs/TECHNICAL.md): maintainer-facing project, release, and repo notes.
- [`version/fnq3_version.h`](/e:/Repositories/FnQuake3/version/fnq3_version.h): single source of truth for project version metadata.
- [`scripts/version.py`](/e:/Repositories/FnQuake3/scripts/version.py): version/channel helper for humans and CI.
- [`scripts/generate_docs.py`](/e:/Repositories/FnQuake3/scripts/generate_docs.py): refreshes `README.md` and `.install/README.html` from templates.
- [`scripts/release.py`](/e:/Repositories/FnQuake3/scripts/release.py): stages artifacts through `.install/` and produces release archives plus manifests.
- [`.github/workflows/build.yml`](/e:/Repositories/FnQuake3/.github/workflows/build.yml): CI build, nightly packaging, and tagged release flow.

## Directory Map

- `.install/`: tracked distribution docs plus generated package outputs during release staging.
- `.tmp/`: ignored scratch workspace for temporary files and intermediate staging.
- `code/`: engine, renderer, platform, and VM sources.
- `docs/`: maintainer docs, legacy upstream docs, and template sources.
- `version/`: canonical version metadata consumed by code, docs, and CI scripts.
- `scripts/`: small repo-local automation for docs, versioning, and packaging.

## Release Workflow

1. Update [`version/fnq3_version.h`](/e:/Repositories/FnQuake3/version/fnq3_version.h) for the next tagged release.
2. Run `python scripts/generate_docs.py` to refresh user-facing docs.
3. Build platform artifacts.
4. Run `python scripts/release.py --channel nightly` or `python scripts/release.py --channel release --ref-name <tag>` against the downloaded artifact directory.
5. Publish the archives produced under `.install/packages/` with the generated manifest and checksums.

## Guardrails

- If a change touches runtime identity strings, keep compatibility-sensitive behavior unchanged unless the user explicitly wants a compatibility break.
- If you have to choose between a cleaner abstraction and a safer compatibility-preserving patch, default to the compatibility-preserving patch and document the tradeoff.
- When release packaging changes, ensure `.install/README.html` remains valid and the package still includes `README.md`, `BUILD.md`, `LICENSE`, and `docs/TECHNICAL.md`.
