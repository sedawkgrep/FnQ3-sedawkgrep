from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fnq3_meta import ROOT, channel_metadata, package_archive_name


DEFAULT_DOCS = [
    (ROOT / "LICENSE", Path("LICENSE")),
    (ROOT / "docs" / "fnquake3" / "TECHNICAL.md", Path("docs") / "fnquake3" / "TECHNICAL.md"),
    (ROOT / ".install" / "README.html", Path("README.html")),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Package FnQuake3 nightly/release artifacts")
    parser.add_argument("--channel", choices=("nightly", "release"), required=True)
    parser.add_argument("--artifact-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--temp-dir", type=Path, default=ROOT / ".tmp" / "release")
    parser.add_argument("--build-date")
    parser.add_argument("--commit")
    parser.add_argument("--ref-name")
    return parser.parse_args()


def sha256sum(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_tree_contents(source: Path, target: Path) -> None:
    target.mkdir(parents=True, exist_ok=True)
    for item in source.iterdir():
        destination = target / item.name
        if item.is_dir():
            shutil.copytree(item, destination, dirs_exist_ok=True)
        else:
            shutil.copy2(item, destination)


def copy_docs(stage_root: Path) -> None:
    for source, dest_relative in DEFAULT_DOCS:
        destination = stage_root / dest_relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def build_archives(args: argparse.Namespace) -> dict[str, object]:
    subprocess.run([sys.executable, str(ROOT / "scripts" / "generate_docs.py")], check=True)

    meta = channel_metadata(
        args.channel,
        build_date=args.build_date,
        commit=args.commit,
        ref_name=args.ref_name,
    )

    artifact_root = args.artifact_root.resolve()
    if not artifact_root.exists():
        raise FileNotFoundError(f"Artifact root does not exist: {artifact_root}")

    output_dir = args.output_dir.resolve()
    packages_dir = output_dir / "packages"
    temp_dir = args.temp_dir.resolve() / args.channel

    packages_dir.mkdir(parents=True, exist_ok=True)
    temp_dir.mkdir(parents=True, exist_ok=True)

    archives: list[dict[str, object]] = []

    for artifact_dir in sorted(path for path in artifact_root.iterdir() if path.is_dir()):
        archive_name = package_archive_name(meta, artifact_dir.name)
        archive_base = packages_dir / archive_name[:-4]
        stage_root = temp_dir / archive_name[:-4]

        if stage_root.exists():
            shutil.rmtree(stage_root)
        stage_root.mkdir(parents=True, exist_ok=True)
        copy_tree_contents(artifact_dir, stage_root)
        copy_docs(stage_root)

        archive_path = Path(shutil.make_archive(str(archive_base), "zip", root_dir=stage_root))
        checksum = sha256sum(archive_path)
        archives.append(
            {
                "artifact_dir": artifact_dir.name,
                "archive": archive_path.name,
                "path": archive_path.relative_to(ROOT).as_posix(),
                "sha256": checksum,
            }
        )
        print(archive_path.relative_to(ROOT).as_posix())

    manifest = {
        "project": meta["project_name"],
        "channel": meta["channel"],
        "base_version": meta["base_version"],
        "version": meta["version"],
        "version_label": meta["version_label"],
        "release_tag": meta["release_tag"],
        "release_title": meta["release_title"],
        "build_date": meta["build_date"],
        "commit": meta["commit"],
        "archives": archives,
    }

    (output_dir / "release-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    checksum_lines = [f"{archive['sha256']}  {Path(archive['path']).name}" for archive in archives]
    (output_dir / "SHA256SUMS.txt").write_text(
        "\n".join(checksum_lines) + ("\n" if checksum_lines else ""),
        encoding="utf-8",
        newline="\n",
    )
    return manifest


def main() -> int:
    args = parse_args()
    build_archives(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
