#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The issue Actions' scripts without GitHub (.github/scripts): they load."""
import os
import sys
from pathlib import Path

os.environ.setdefault("GITHUB_TOKEN", "test")
sys.dont_write_bytecode = True   # no __pycache__ beside the scripts
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / ".github" / "scripts"))
import compatibility_list   # noqa: E402
import no_reply     # noqa: E402

def main() -> int:
    assert callable(compatibility_list.issue_reports) and "keep open" in no_reply.KEEP
    print("GitHub scripts passed: they load")
    return 0

if __name__ == "__main__":
    sys.exit(main())
