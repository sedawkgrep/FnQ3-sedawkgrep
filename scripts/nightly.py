from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fnq3_meta import (
    ROOT,
    VERSION_HEADER,
    base_metadata,
    compose_version_string,
    compose_windows_version,
    normalize_commit,
    normalize_date,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="FnQuake3 nightly build helper")
    subparsers = parser.add_subparsers(dest="command", required=True)

    for command in ("summary", "github-output"):
        subparser = subparsers.add_parser(command)
        subparser.add_argument("--build-date")
        subparser.add_argument("--head-commit")
        subparser.add_argument("--force", action="store_true")

    stamp = subparsers.add_parser("stamp-version")
    stamp.add_argument("--build-number", required=True, type=int)
    stamp.add_argument("--header", type=Path, default=VERSION_HEADER)

    notes = subparsers.add_parser("release-notes")
    notes.add_argument("--build-number", required=True, type=int)
    notes.add_argument("--build-date")
    notes.add_argument("--from-commit")
    notes.add_argument("--to-commit")
    notes.add_argument("--output", type=Path)

    return parser.parse_args()


def git(*args: str, check: bool = True) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if check and result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or "git command failed"
        raise RuntimeError(message)
    return result.stdout.strip()


def resolve_tag_commit(tag_name: str) -> str:
    result = subprocess.run(
        ["git", "rev-parse", f"refs/tags/{tag_name}^{{commit}}"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def latest_stable_tag(tag_prefix: str) -> str:
    pattern = f"{tag_prefix}[0-9]*"
    output = git("tag", "--list", pattern, "--sort=-version:refname", check=False)
    tags = [line.strip() for line in output.splitlines() if line.strip()]
    return tags[0] if tags else ""


def commit_count_since(stable_tag: str, head_commit: str) -> int:
    range_spec = head_commit if not stable_tag else f"{stable_tag}..{head_commit}"
    count = int(git("rev-list", "--count", range_spec) or "0")
    return max(1, count)


def nightly_context(
    *,
    build_date: str | None = None,
    head_commit: str | None = None,
    force: bool = False,
) -> dict[str, object]:
    meta = base_metadata()
    head_sha = (head_commit or git("rev-parse", "HEAD")).strip()
    iso_date, _ = normalize_date(build_date)
    previous_nightly_commit = resolve_tag_commit(str(meta["nightly_tag"]))
    stable_tag = latest_stable_tag(str(meta["tag_prefix"]))
    build_number = commit_count_since(stable_tag, head_sha)
    version_string = compose_version_string(
        int(meta["version_major"]),
        int(meta["version_minor"]),
        int(meta["version_patch"]),
        build_number,
    )

    return {
        "project_name": str(meta["project_name"]),
        "base_version": str(meta["base_version"]),
        "version_string": version_string,
        "build_number": build_number,
        "build_date": iso_date,
        "head_commit": head_sha,
        "head_commit_short": normalize_commit(head_sha),
        "nightly_tag": str(meta["nightly_tag"]),
        "stable_tag": stable_tag,
        "previous_nightly_commit": previous_nightly_commit,
        "should_build": force or not previous_nightly_commit or previous_nightly_commit != head_sha,
    }


def print_mapping(data: dict[str, object]) -> None:
    for key, value in data.items():
        if isinstance(value, bool):
            rendered = str(value).lower()
        else:
            rendered = str(value)
        print(f"{key}={rendered}")


def stamp_version(header: Path, build_number: int) -> dict[str, object]:
    meta = base_metadata(header)
    version_string = compose_version_string(
        int(meta["version_major"]),
        int(meta["version_minor"]),
        int(meta["version_patch"]),
        build_number,
    )
    windows_version = compose_windows_version(
        int(meta["version_major"]),
        int(meta["version_minor"]),
        int(meta["version_patch"]),
        build_number,
    )

    replacements = {
        "FNQ3_VERSION_TWEAK": str(build_number),
        "FNQ3_VERSION_STRING": f'"{version_string}"',
        "FNQ3_WINDOWS_FILE_VERSION": windows_version,
        "FNQ3_WINDOWS_PRODUCT_VERSION": windows_version,
    }

    lines = header.read_text(encoding="utf-8").splitlines()
    seen: set[str] = set()
    rewritten: list[str] = []

    for line in lines:
        parts = line.strip().split(maxsplit=2)
        if len(parts) >= 3 and parts[0] == "#define" and parts[1] in replacements:
            key = parts[1]
            rewritten.append(f"#define {key} {replacements[key]}")
            seen.add(key)
        else:
            rewritten.append(line)

    missing = sorted(set(replacements) - seen)
    if missing:
        raise KeyError(f"Missing version defines in {header}: {', '.join(missing)}")

    header.write_text("\n".join(rewritten) + "\n", encoding="utf-8", newline="\n")
    return {
        "version_string": version_string,
        "build_number": build_number,
    }


def commit_lines(from_commit: str | None, to_commit: str) -> list[str]:
    if from_commit and from_commit != to_commit:
        range_spec = f"{from_commit}..{to_commit}"
    elif from_commit == to_commit:
        range_spec = f"{to_commit}^!"
    else:
        stable_tag = latest_stable_tag(str(base_metadata()["tag_prefix"]))
        range_spec = f"{stable_tag}..{to_commit}" if stable_tag else to_commit

    log = git("log", "--reverse", "--pretty=format:- %h %s", range_spec)
    return [line.rstrip() for line in log.splitlines() if line.strip()]


def render_release_notes(
    *,
    build_number: int,
    build_date: str | None = None,
    from_commit: str | None = None,
    to_commit: str | None = None,
) -> str:
    meta = base_metadata()
    target_commit = (to_commit or git("rev-parse", "HEAD")).strip()
    iso_date, _ = normalize_date(build_date)
    version_string = compose_version_string(
        int(meta["version_major"]),
        int(meta["version_minor"]),
        int(meta["version_patch"]),
        build_number,
    )

    lines = [
        f"# {meta['project_name']} Nightly",
        "",
        "- Channel: nightly",
        f"- Base version line: {meta['base_version']}",
        f"- Build version: {version_string}",
        f"- Build date: {iso_date}",
        f"- Commit: {normalize_commit(target_commit)} ({target_commit})",
    ]

    if from_commit:
        lines.append(f"- Previous nightly commit: {normalize_commit(from_commit)} ({from_commit})")
    else:
        stable_tag = latest_stable_tag(str(meta["tag_prefix"]))
        if stable_tag:
            lines.append(f"- Previous stable tag: {stable_tag}")
        else:
            lines.append("- Previous nightly commit: none")

    lines.extend(["", "## Included commits", ""])
    commits = commit_lines(from_commit, target_commit)
    if commits:
        lines.extend(commits)
    else:
        lines.append(f"- {normalize_commit(target_commit)} no new commits were found for the requested range")

    return "\n".join(lines) + "\n"


def main() -> int:
    args = parse_args()

    if args.command in {"summary", "github-output"}:
        info = nightly_context(
            build_date=args.build_date,
            head_commit=args.head_commit,
            force=args.force,
        )
        print_mapping(info)
        return 0

    if args.command == "stamp-version":
        info = stamp_version(args.header, args.build_number)
        print_mapping(info)
        return 0

    notes = render_release_notes(
        build_number=args.build_number,
        build_date=args.build_date,
        from_commit=args.from_commit,
        to_commit=args.to_commit,
    )
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(notes, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(notes)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
