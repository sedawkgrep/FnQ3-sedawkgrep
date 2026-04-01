from __future__ import annotations

import argparse
import os
import sys

from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fnq3_meta import channel_metadata, to_json


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="FnQuake3 version metadata helper")
    parser.add_argument(
        "command",
        choices=("summary", "json", "github-env"),
        nargs="?",
        default="summary",
    )
    parser.add_argument("--channel", choices=("release", "nightly"), default="release")
    parser.add_argument("--build-date", default=os.environ.get("FNQ3_BUILD_DATE"))
    parser.add_argument("--commit", default=os.environ.get("GITHUB_SHA"))
    parser.add_argument("--ref-name", default=os.environ.get("GITHUB_REF_NAME"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    meta = channel_metadata(
        args.channel,
        build_date=args.build_date,
        commit=args.commit,
        ref_name=args.ref_name,
    )

    if args.command == "json":
        print(to_json(meta))
        return 0

    if args.command == "github-env":
        mapping = {
            "FNQ3_PROJECT_NAME": meta["project_name"],
            "FNQ3_VERSION": meta["version"],
            "FNQ3_VERSION_LABEL": meta["version_label"],
            "FNQ3_RELEASE_TAG": meta["release_tag"],
            "FNQ3_RELEASE_TITLE": meta["release_title"],
            "FNQ3_ARCHIVE_PREFIX": meta["archive_prefix"],
            "FNQ3_BUILD_DATE": meta["build_date"],
            "FNQ3_BUILD_DATE_SLUG": meta["build_date_slug"],
            "FNQ3_COMMIT": meta["commit"],
            "FNQ3_CHANNEL": meta["channel"],
        }
        for key, value in mapping.items():
            print(f"{key}={value}")
        return 0

    print(f"{meta['project_name']} {meta['version_label']} [{meta['channel']}]")
    print(f"tag: {meta['release_tag']}")
    print(f"title: {meta['release_title']}")
    print(f"archives: {meta['archive_prefix']}-<artifact>.zip")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
