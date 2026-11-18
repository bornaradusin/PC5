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

def issue(number, shown, created, closed="", game="BeamNG.drive", notes=""):
    return {"number": number, "game": game, "shown": shown, "created": created, "closed_at": closed,
            "url": f"https://github.com/o/r/issues/{number}", "notes": notes, "id": f"I{number}",
            "open": not closed}


def closed_before_keeps_the_row() -> None:
    c = compatibility_list
    old = issue(132, c.UNRESOLVED, "2026-10-20T00:00:00Z", "2026-10-28T00:00:00Z", notes="Chromium starts programs")
    new = issue(714, c.LOOKED_AT, "2026-11-17T00:00:00Z")
    # The earlier verdict keeps the row while the new issue is open; the new one is named beside it.
    assert c.best_row([new, old]) is old and c.closed_before(new, [new, old]) == [old]
    # Not for another game, nor for an issue closed only after the open one was opened.
    other = issue(9, c.UNRESOLVED, "2026-10-01T00:00:00Z", "2026-10-02T00:00:00Z", game="GoreBox")
    later = issue(800, c.UNRESOLVED, "2026-11-17T00:00:00Z", "2026-11-18T00:00:00Z")
    assert c.closed_before(new, [new, other, later]) == [] and c.best_row([new, other]) is new
    # Copy-specific or open-ended verdicts don't hold it back: another copy may run.
    store = issue(50, c.NEEDS_STORE, "2026-10-01T00:00:00Z", "2026-10-02T00:00:00Z")
    nobody = issue(51, c.NEEDS_TESTING, "2026-10-01T00:00:00Z", "2026-10-02T00:00:00Z")
    assert c.best_row([new, store, nobody]) is new
    # A fix that's out, or a player's report that it runs, still wins.
    ready = issue(715, c.FIX_READY, "2026-11-17T00:00:00Z")
    works = {"number": 3, "game": "BeamNG.drive", "shown": "Works perfectly", "created": "2026-11-01T00:00:00Z", "votes": 0}
    assert c.best_row([ready, old]) is ready and c.best_row([new, old, works]) is works
    # Anti-cheat holds it back too; of two earlier ones, the newest closed keeps the row.
    cheat = issue(140, c.ANTI_CHEAT, "2026-10-21T00:00:00Z", "2026-11-01T00:00:00Z")
    assert c.best_row([new, old, cheat]) is cheat
    # The row keeps the earlier issue's notes, and names the open one beside it as main() does.
    rest = [r for r in c.by_shown([new, old]) if r is not c.best_row([new, old])]
    assert c.best_row([new, old])["notes"] == "Chromium starts programs" and rest == [new]


def main() -> int:
    assert callable(compatibility_list.issue_reports) and "keep open" in no_reply.KEEP
    closed_before_keeps_the_row()
    print("GitHub scripts passed: they load; an earlier issue that closed a game keeps its row")
    return 0

if __name__ == "__main__":
    sys.exit(main())
