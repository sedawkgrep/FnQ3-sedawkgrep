from __future__ import annotations

import datetime as _dt
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_HEADER = ROOT / "version" / "fnq3_version.h"

_DEFINE_RE = re.compile(r"^\s*#define\s+([A-Z0-9_]+)\s+(.+?)\s*$")


def _strip_comments(raw_value: str) -> str:
    return raw_value.split("//", 1)[0].strip()


def _parse_define_value(raw_value: str):
    value = _strip_comments(raw_value)
    if value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    return value


def read_version_defines(path: Path = VERSION_HEADER) -> dict[str, object]:
    defines: dict[str, object] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = _DEFINE_RE.match(line)
        if not match:
            continue
        key, raw_value = match.groups()
        defines[key] = _parse_define_value(raw_value)
    return defines


def base_metadata() -> dict[str, object]:
    defines = read_version_defines()
    version = str(defines["FNQ3_VERSION_STRING"])
    return {
        "project_name": str(defines["FNQ3_PROJECT_NAME"]),
        "project_name_short": str(defines["FNQ3_PROJECT_NAME_SHORT"]),
        "display_name": str(defines["FNQ3_DISPLAY_NAME"]),
        "compatibility_target": str(defines["FNQ3_COMPATIBILITY_TARGET"]),
        "version": version,
        "version_major": int(defines["FNQ3_VERSION_MAJOR"]),
        "version_minor": int(defines["FNQ3_VERSION_MINOR"]),
        "version_patch": int(defines["FNQ3_VERSION_PATCH"]),
        "version_tweak": int(defines["FNQ3_VERSION_TWEAK"]),
        "tag_prefix": str(defines["FNQ3_TAG_PREFIX"]),
        "artifact_prefix": str(defines["FNQ3_ARTIFACT_PREFIX"]),
        "nightly_tag": str(defines["FNQ3_NIGHTLY_TAG"]),
    }


def normalize_date(value: str | None = None) -> tuple[str, str]:
    if value:
        day = _dt.date.fromisoformat(value)
    else:
        day = _dt.datetime.now(_dt.timezone.utc).date()
    return day.isoformat(), day.strftime("%Y%m%d")


def normalize_commit(value: str | None = None) -> str:
    cleaned = (value or "local").strip()
    return cleaned[:8] if cleaned else "local"


def channel_metadata(
    channel: str,
    *,
    build_date: str | None = None,
    commit: str | None = None,
    ref_name: str | None = None,
) -> dict[str, object]:
    meta = base_metadata()
    iso_date, date_slug = normalize_date(build_date)
    short_commit = normalize_commit(commit)

    if channel not in {"release", "nightly"}:
        raise ValueError(f"Unsupported channel: {channel}")

    if channel == "release":
        release_tag = ref_name or f"{meta['tag_prefix']}{meta['version']}"
        archive_prefix = f"{meta['artifact_prefix']}-{meta['version']}"
        version_label = str(meta["version"])
        release_title = f"{meta['project_name']} {meta['version']}"
    else:
        release_tag = str(meta["nightly_tag"])
        archive_prefix = f"{meta['artifact_prefix']}-nightly-{date_slug}-{short_commit}"
        version_label = f"{meta['version']}-nightly.{date_slug}+{short_commit}"
        release_title = f"{meta['project_name']} Nightly {iso_date}"

    meta.update(
        {
            "channel": channel,
            "build_date": iso_date,
            "build_date_slug": date_slug,
            "commit": short_commit,
            "release_tag": release_tag,
            "archive_prefix": archive_prefix,
            "version_label": version_label,
            "release_title": release_title,
            "release_tag_example": f"{meta['tag_prefix']}{meta['version']}",
        }
    )
    return meta


def package_archive_name(meta: dict[str, object], artifact_dir_name: str) -> str:
    return f"{meta['archive_prefix']}-{artifact_dir_name}.zip"


def to_json(data: dict[str, object]) -> str:
    return json.dumps(data, indent=2, sort_keys=True)
