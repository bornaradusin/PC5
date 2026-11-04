#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Makes the two files a release's update to the PS5 homebrew catalog needs, beside its archives.

    tools/make-catalog-files.py APP_ZIP OUT_DIR [--repo DIR]

- OUT_DIR/PPSA11018.json: the catalog's record of PC5 (darkxex/ps5-homebrew-catalog), for its
  apps/ folder: the version (title/pc5.h's PC5_VERSION), the release's PPSA11018.zip as its
  artifact and that file's SHA-256, and the icon at the release's tag.
- OUT_DIR/catalog-pr.md: the text of the pull request that updates the record, its form filled
  in. What the app does is PC5's one line, then the first line of the release's notes
  (release-notes/<version>.md) without its "PC5 " and its bold marks.

Build's Publish job attaches both to the draft release (.github/workflows/build.yml), so the
maintainer copies them from there once it is published."""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

TITLE = "PPSA11018"
REPO = "bornaradusin/PC5"
ABOUT = "PC5 runs your own PC games on the PS5 through Wine, DXVK and VKD3D-Proton."
DESCRIPTION = ("Launcher for your own PC games via Wine, DXVK and VKD3D-Proton: console or USB installs, Steam "
               "emulator, covers, DualSense, up to four players, web page, crash reports and updates.")
CATALOG = "https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs"
CHECKS = [
    "The app is a native PS5 app (its own title with `eboot.bin` and `sce_sys/`), not a payload, PS4 package or backport.",
    "I am opening this PR from the account that owns `source_repo` (or I am a public member of the owning organization), "
    "or I answered \"no\" above and understand a maintainer must approve it by hand.",
    f"The artifact is a `.zip` of the app folder, the only format accepted at the moment ([artifact formats]({CATALOG}/"
    "artifact-formats.md)) whose app uses this title ID.",
    "For a release: the artifact is attached to a published GitHub release, and `version` is that release's tag "
    "(optionally without a leading `v`).",
    "For a release: `sha256` is the digest of the exact asset in `artifact_url`, and I won't replace that asset.",
    "For a release: `contentVersion` in my repository's `sce_sys/param.json` is in the `01.000.070` format and higher "
    f"than my previous release's, so consoles see updates ([App versions]({CATALOG}/versioning.md)).",
    f"The record's `category` is the shelf people would look in first ([categories]({CATALOG}/metadata.md#categories)).",
    "I have the right to distribute this build under the stated license (for another developer's repository: its "
    "license allows redistribution).",
    "If it builds on an app in the catalog, it is a distinct app (not only a new name, theme or icon) and its name "
    "doesn't include that app's name.",
    f"I have read the [review policy]({CATALOG}/review-policy.md).",
]


def version_of(repo: Path) -> str:
    found = re.search(r'^#define PC5_VERSION "([^"]+)"', (repo / "title" / "pc5.h").read_text(), re.M)
    if not found:
        sys.exit("make-catalog-files: no PC5_VERSION in title/pc5.h")
    return found.group(1)


def news_of(repo: Path, version: str) -> str:
    """The notes' first line, as the record's sentence has it: "2.2.9 fixes keys that ..."."""
    notes = repo / "release-notes" / f"{version}.md"
    first = next((line.strip() for line in notes.read_text().splitlines() if line.strip()), "") if notes.is_file() else ""
    first = re.sub(r"^PC5\s+", "", first.replace("**", ""))
    return first or f"{version}."


def record(version: str, sha256: str) -> str:
    data = {
        "titleid": TITLE, "name": "PC5", "kind": "app", "category": "emulators", "description": DESCRIPTION,
        "license": "LGPL-2.1", "author": REPO.split("/")[0], "version": version,
        "source_repo": f"https://github.com/{REPO}",
        "artifact_url": f"https://github.com/{REPO}/releases/download/v{version}/{TITLE}.zip",
        "sha256": sha256,
        "icon_url": f"https://raw.githubusercontent.com/{REPO}/v{version}/art/icon0.png",
    }
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def pull_request(news: str) -> str:
    lines = [
        f"**Title ID:** {TITLE}",
        "**New listing, update or reservation:** update",
        f"**What the app does:** {ABOUT} {news}",
        "**Category:** emulators",
        "**Fork or derivative of an app in the catalog?** no",
        "**Is `source_repo` yours?** yes",
        "",
    ] + [f"- [x] {check}" for check in CHECKS]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("app_zip", type=Path)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()
    version = version_of(args.repo)
    digest = hashlib.sha256()
    with args.app_zip.open("rb") as app:
        for chunk in iter(lambda: app.read(1 << 20), b""):
            digest.update(chunk)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / f"{TITLE}.json").write_text(record(version, digest.hexdigest()))
    (args.out_dir / "catalog-pr.md").write_text(pull_request(news_of(args.repo, version)))
    print(f"make-catalog-files: {TITLE}.json and catalog-pr.md for {version}, sha256 {digest.hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
