from __future__ import annotations

import argparse
import datetime as _dt
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CHANGELOG = ROOT / "docs" / "fnquake3" / "CHANGELOG.md"


SECTION_RE = re.compile(r"^##\s+\[(?P<label>[^\]]+)\](?:\s+-\s+(?P<date>\d{4}-\d{2}-\d{2}))?\s*$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="FnQuake3 changelog helper")
    subparsers = parser.add_subparsers(dest="command", required=True)

    section = subparsers.add_parser("section")
    section.add_argument("--version", default="Unreleased", help="Section label such as Unreleased or 0.1.0")
    section.add_argument("--changelog", type=Path, default=DEFAULT_CHANGELOG)

    release = subparsers.add_parser("prepare-release")
    release.add_argument("--version", required=True, help="Release version to stamp")
    release.add_argument("--date", default=_dt.date.today().isoformat())
    release.add_argument("--changelog", type=Path, default=DEFAULT_CHANGELOG)

    return parser.parse_args()


def read_sections(path: Path) -> list[tuple[str, str | None, list[str]]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    sections: list[tuple[str, str | None, list[str]]] = []
    current_label: str | None = None
    current_date: str | None = None
    current_lines: list[str] = []

    for line in lines:
        match = SECTION_RE.match(line)
        if match:
            if current_label is not None:
                sections.append((current_label, current_date, current_lines))
            current_label = match.group("label")
            current_date = match.group("date")
            current_lines = []
            continue

        if current_label is not None:
            current_lines.append(line)

    if current_label is not None:
        sections.append((current_label, current_date, current_lines))
    return sections


def section_text(path: Path, target: str) -> str:
    for label, _date, lines in read_sections(path):
        if label.lower() == target.lower():
            body = "\n".join(lines).strip()
            if not body:
                return "- No changes documented yet.\n"
            return f"{body}\n"
    raise KeyError(f"Section [{target}] was not found in {path}")


def prepare_release(path: Path, version: str, date_value: str) -> str:
    lines = path.read_text(encoding="utf-8").splitlines()
    unreleased_header = "## [Unreleased]"
    release_header = f"## [{version}] - {date_value}"
    if any(line.strip() == release_header for line in lines):
        raise ValueError(f"Release section already exists: {release_header}")

    try:
        unreleased_index = lines.index(unreleased_header)
    except ValueError as exc:
        raise ValueError(f"Missing section header: {unreleased_header}")

    next_section_index = len(lines)
    for index in range(unreleased_index + 1, len(lines)):
        if SECTION_RE.match(lines[index]):
            next_section_index = index
            break

    unreleased_body = lines[unreleased_index + 1 : next_section_index]
    trimmed_body = [line for line in unreleased_body]
    while trimmed_body and not trimmed_body[0].strip():
        trimmed_body = trimmed_body[1:]
    while trimmed_body and not trimmed_body[-1].strip():
        trimmed_body = trimmed_body[:-1]

    refreshed_unreleased = [
        "",
        "### Added",
        "- _None yet._",
        "",
        "### Changed",
        "- _None yet._",
        "",
        "### Fixed",
        "- _None yet._",
    ]
    release_block = ["", release_header, ""]
    release_block.extend(trimmed_body if trimmed_body else ["- No documented changes."])
    updated_lines = (
        lines[: unreleased_index + 1]
        + refreshed_unreleased
        + release_block
        + lines[next_section_index:]
    )
    path.write_text("\n".join(updated_lines).rstrip() + "\n", encoding="utf-8", newline="\n")
    return release_header


def main() -> int:
    args = parse_args()
    if args.command == "section":
        sys.stdout.write(section_text(args.changelog, args.version))
        return 0

    header = prepare_release(args.changelog, args.version, args.date)
    print(header)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
