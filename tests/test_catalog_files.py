#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""tools/make-catalog-files.py makes 2.2.9's catalog files as they were sent by hand
(tests/fixtures/catalog), and from an app zip and a repository writes both with its SHA-256."""
import hashlib
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE.parent / "tools" / "make-catalog-files.py"
FIXTURES = HERE / "fixtures" / "catalog"
SHA_229 = "8390a0ff4e83443d735c6776835019717cb681a4003a61e2d4433ccbab93bfc4"

sys.dont_write_bytecode = True   # no __pycache__ beside the tools
spec = importlib.util.spec_from_file_location("make_catalog_files", SCRIPT)
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)


def main() -> int:
    assert catalog.record("2.2.9", SHA_229) == (FIXTURES / "PPSA11018.json").read_text(), "the record"
    news = "2.2.9 fixes keys that stayed pressed with Keyboard & mouse."
    assert catalog.pull_request(news) == (FIXTURES / "catalog-pr.md").read_text(), "the pull request"
    with tempfile.TemporaryDirectory() as work:
        repo, out = Path(work) / "repo", Path(work) / "out"
        (repo / "title").mkdir(parents=True)
        (repo / "release-notes").mkdir()
        (repo / "title" / "pc5.h").write_text('#pragma once\n#define PC5_VERSION "2.2.9"\n')
        (repo / "release-notes" / "2.2.9.md").write_text(
            "PC5 2.2.9 fixes keys that stayed pressed with **Keyboard & mouse**.\n\n### Install\n")
        app = Path(work) / "PPSA11018.zip"
        app.write_bytes(b"not really a zip")
        subprocess.run([sys.executable, str(SCRIPT), str(app), str(out), "--repo", str(repo)], check=True,
                       stdout=subprocess.DEVNULL)
        sha = hashlib.sha256(b"not really a zip").hexdigest()
        assert (out / "PPSA11018.json").read_text() == catalog.record("2.2.9", sha), "written with the zip's SHA-256"
        assert (out / "catalog-pr.md").read_text() == (FIXTURES / "catalog-pr.md").read_text(), "the notes' first line"
        # No notes yet: the version alone.
        (repo / "release-notes" / "2.2.9.md").unlink()
        assert catalog.news_of(repo, "2.2.9") == "2.2.9.", "without notes"
    print("catalog files passed: 2.2.9's record and pull request, the zip's SHA-256, the notes' first line")
    return 0


if __name__ == "__main__":
    sys.exit(main())
