#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""A new issue told of the other issues about its game, and how each stands.

- When an issue is opened (.github/workflows/related-issues.yml), its game is read as the game
  compatibility list reads it (compatibility_list.py's issue_games and game_key). Every other
  issue about the same game, open or closed (not a pull request, not one closed as a duplicate,
  not the weekly summary), is listed in one comment, newest first, at most ten: its number and
  title, and how it stands in plain words (state_words). One closed as fixed, or as running
  with something to set, also gets the maintainer's "List settings:" and "List notes:" lines
  from its comments: what made it run.
- An issue with no other issue of its game gets no comment, and none is made twice (the comment
  starts with <!-- pc5-related -->). What players' reports in the compatibility list say about
  the game isn't repeated here: compatibility_list.py tells an open issue that once already.
- The comment is a bot's (github-actions), so no_reply.py doesn't count it as the maintainer's.

Run with GITHUB_TOKEN, GITHUB_REPOSITORY and GITHUB_EVENT_PATH (an issues event);
RELATED_DRY_RUN=1 prints the comment instead of posting it.
"""
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import compatibility_list as listing   # noqa: E402  the same reading of an issue's game
from no_reply import api, is_bot, pages   # noqa: E402  the same REST calls

MARK = "<!-- pc5-related -->"
SUMMARY = listing.SUMMARY_LABEL
DRY_RUN = os.environ.get("RELATED_DRY_RUN") == "1"
MOST = 10


def labels_of(issue: dict) -> set:
    return {(label.get("name") if isinstance(label, dict) else str(label)).casefold() for label in issue.get("labels", [])}


def state_words(issue: dict) -> str:
    """How an issue stands, as a player would say it."""
    labels = labels_of(issue)
    if listing.CHEAT_LABEL in labels:
        return "anti-cheat: no copy runs"
    if listing.STORE in labels or "needs steam" in labels:
        return "that copy needs its store's app"
    if issue.get("state") == "open":
        return "open, a fix out to test" if listing.FIX_LABEL in labels else "open"
    if listing.NO_REPLY in labels:
        return "closed for no reply"
    if listing.PLAYABLE in labels:
        return "runs, with something to set or still wrong"
    if issue.get("state_reason") == "not_planned" or "wontfix" in labels:
        return "doesn't run"
    return "fixed"


def keys_of(issue: dict, by_number: dict) -> tuple:
    """The game_keys of an issue's games, and its first game's name."""
    body = issue.get("body") or ""
    games = listing.issue_games({"number": issue["number"], "body": body, "labels": []}, listing.answers(body), {}, by_number)
    return {listing.game_key(game) for game in games}, games[0] if games else ""


def owner_lines(repo: str, number: int, owner: str) -> dict:
    """The maintainer's newest "List settings:" and "List notes:" lines on an issue."""
    found = {}
    for comment in pages(f"repos/{repo}/issues/{number}/comments"):
        if (comment.get("user") or {}).get("login", "").casefold() != owner.casefold():
            continue
        for kind, text in listing.OWNER_LINE.findall(comment.get("body") or ""):
            found[kind.casefold()] = text
    return found


def related(repo: str, issue: dict, owner: str, by_number: dict) -> list:
    """The other issues of the new issue's game, newest first: (issue, its words, what helped)."""
    keys, _ = keys_of(issue, by_number)
    if not keys:
        return []
    found = []
    for other in pages(f"repos/{repo}/issues?state=all"):
        if ("pull_request" in other or other["number"] == issue["number"] or other.get("state_reason") == "duplicate"
                or SUMMARY in labels_of(other)):
            continue
        if not keys & keys_of(other, by_number)[0]:
            continue
        words = state_words(other)
        helped = owner_lines(repo, other["number"], owner) if words in ("fixed", "runs, with something to set or still wrong") else {}
        found.append((other, words, helped))
    found.sort(key=lambda item: item[0]["number"], reverse=True)
    return found[:MOST]


def comment(game: str, found: list) -> str:
    lines = [MARK, f"Other issues about **{game}**:" if game else "Other issues about this game:", ""]
    for other, words, helped in found:
        line = f"- [#{other['number']}]({other.get('html_url', '')}) {listing.cell(other.get('title') or '', 80)}: {words}"
        if helped.get("settings"):
            line += f". What helped: {listing.cell(helped['settings'], 300)}"
        if helped.get("notes"):
            line += f". {listing.cell(helped['notes'], 300)}"
        lines.append(line)
    lines += ["", "What helped there may help here too. If one of them is your problem, you can add to it instead."]
    return "\n".join(lines)


def main() -> int:
    repo = os.environ["GITHUB_REPOSITORY"]
    owner = repo.split("/")[0]
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    issue = event.get("issue") or {}
    if not issue or "pull_request" in issue or SUMMARY in labels_of(issue) or is_bot(issue.get("user")):
        return 0
    if any(MARK in (c.get("body") or "") for c in pages(f"repos/{repo}/issues/{issue['number']}/comments")):
        return 0
    by_number = listing.games_by_issue()
    found = related(repo, issue, owner, by_number)
    if not found:
        print(f"related issues: #{issue['number']}: none")
        return 0
    body = comment(keys_of(issue, by_number)[1], found)
    print(f"related issues: #{issue['number']}: {len(found)}{' (dry run)' if DRY_RUN else ''}")
    if DRY_RUN:
        print(body)
        return 0
    api("POST", f"repos/{repo}/issues/{issue['number']}/comments", {"body": body})
    return 0


if __name__ == "__main__":
    sys.exit(main())
