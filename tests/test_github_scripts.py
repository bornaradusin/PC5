#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The issue Actions' scripts without GitHub (.github/scripts): weekly_summary.py, and the summary
issue left out of the list."""
import os
import sys
import tempfile
from pathlib import Path

os.environ.setdefault("GITHUB_TOKEN", "test")
sys.dont_write_bytecode = True   # no __pycache__ beside the scripts
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / ".github" / "scripts"))
import compatibility_list   # noqa: E402
import no_reply     # noqa: E402
import weekly_summary   # noqa: E402

def issue(number: int, game: str, state: str = "open", reason=None, labels=(), title: str = "", when: str = "") -> dict:
    return {"number": number, "title": title or f"{game}: closed before showing anything", "state": state,
            "state_reason": reason, "labels": [{"name": l} for l in labels], "html_url": f"https://x/issues/{number}",
            "body": f"### Game\n\n{game}\n\n### PC5 version\n\n2.2.9\n", "closed_at": when or None,
            "user": {"login": "player"}}


def test_state_words() -> None:
    words = weekly_summary.state_words
    assert words(issue(1, "G")) == "open" and words(issue(1, "G", labels=["fix ready"])) == "open, a fix out to test"
    assert words(issue(1, "G", "closed", "completed")) == "fixed"
    assert words(issue(1, "G", "closed", "not_planned")) == "doesn't run"
    assert words(issue(1, "G", "closed", "not_planned", ["needs testing"])) == "closed for no reply"
    assert words(issue(1, "G", "closed", "not_planned", ["anti-cheat"])) == "anti-cheat: no copy runs"
    assert words(issue(1, "G", "closed", "not_planned", ["needs store app"])) == "that copy needs its store's app"
    assert words(issue(1, "G", "closed", "completed", ["playable with issues"])) == "runs, with something to set or still wrong"


def test_summary_left_out() -> None:
    # compatibility_list.py lists no summary issue.
    nodes = [{"id": "a", "number": 5, "title": "Weekly summary", "url": "u5", "body": "### Game\n\nNot a game\n",
              "state": "OPEN", "stateReason": None, "createdAt": "2026-11-02T06:17:00Z", "closedAt": None,
              "author": {"login": "github-actions"}, "labels": {"nodes": [{"name": "summary"}, {"name": "keep open"}]},
              "comments": {"nodes": []}},
             {"id": "b", "number": 6, "title": "G", "url": "u6", "body": "### Game\n\nHollow Bay\n", "state": "OPEN",
              "stateReason": None, "createdAt": "2026-11-02T06:17:00Z", "closedAt": None, "author": {"login": "p"},
              "labels": {"nodes": []}, "comments": {"nodes": []}}]
    graphql, releases = compatibility_list.graphql, compatibility_list.published_releases
    compatibility_list.graphql = lambda query, fatal=True, **v: {"repository": {"issues": {
        "pageInfo": {"hasNextPage": False, "endCursor": None}, "nodes": nodes}}}
    compatibility_list.published_releases = lambda owner, name: []
    try:
        rows = compatibility_list.issue_reports("o", "r", {})
    finally:
        compatibility_list.graphql, compatibility_list.published_releases = graphql, releases
    assert [r["number"] for r in rows] == [6], rows
    assert "keep open" in no_reply.KEEP, "no_reply.py never closes it"


def test_weekly(root: Path) -> None:
    from datetime import datetime, timezone
    now = datetime(2026, 11, 9, 6, 17, tzinfo=timezone.utc)
    owner, bot = "kevin", {"login": "github-actions[bot]", "type": "Bot"}
    open_issues = [issue(1, "A"), issue(2, "B"), issue(3, "C"), issue(4, "D", labels=["in progress"])]
    comments = {1: [], 2: [{"user": {"login": "kevin"}, "body": "Try 2.3.0", "created_at": "2026-11-08T06:17:00Z"},
                           {"user": bot, "body": "<!-- pc5-log-check -->", "created_at": "2026-11-08T07:00:00Z"}],
                3: [{"user": {"login": "kevin"}, "body": "Fixed.", "created_at": "2026-11-06T06:17:00Z"}],
                4: [{"user": {"login": "kevin"}, "body": "On it", "created_at": "2026-11-01T00:00:00Z"}]}
    assert weekly_summary.waiting(open_issues[0], comments[1], owner, now) == ("maintainer", None)
    who, left = weekly_summary.waiting(open_issues[1], comments[2], owner, now)
    assert who == "reporter" and abs(left - 1.0) < 0.01, left
    assert weekly_summary.waiting(open_issues[2], comments[3], owner, now) == ("reporter", 0.0), "closes at the next run"
    assert weekly_summary.waiting(open_issues[3], comments[4], owner, now) == ("reporter", None), "kept open"
    closed = [issue(7, "E", "closed", "completed", when="2026-11-05T00:00:00Z")]
    text = weekly_summary.page("o/r", owner, now, open_issues, lambda i: comments[i["number"]], closed,
                               [("Hollow Bay", "Works perfectly", "https://x/d/1")])
    assert "1 waiting on you, 3 waiting on their reporter" in text, text
    assert "[#1](https://x/issues/1)" in text.split("### Waiting on their reporter")[0], text
    assert "closes for no reply in 1.0 days" in text and "at the next run" in text and ": kept open" in text, text
    assert "[#7](https://x/issues/7) E: closed before showing anything: fixed" in text, text
    assert "[Hollow Bay](https://x/d/1): Works perfectly" in text
    assert "New game compatibility reports" not in weekly_summary.page("o/r", owner, now, [], lambda i: [], [], None)


def main() -> int:
    with tempfile.TemporaryDirectory() as work:
        test_weekly(Path(work))
    test_state_words()
    test_summary_left_out()
    print("GitHub scripts passed: the weekly summary, how an issue stands in words, the summary issue left out of the list")
    return 0

if __name__ == "__main__":
    sys.exit(main())
