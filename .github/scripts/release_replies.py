#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Posts the replies waiting for a release once it is published, and asks the players of the same
games to try again.

- A reply waits in .github/replies/<issue number>.md: its first line "version: X.Y.Z", a blank
  line, then the reply as it is to be posted (.github/replies/README.md). When release vX.Y.Z is
  published (.github/workflows/release-replies.yml), each reply for X.Y.Z is posted on its
  issue while the issue is open, after a line GitHub doesn't show (<!-- pc5-release-reply
  X.Y.Z -->), and the issue gets the "fix ready" label: the game compatibility list shows it as
  a fix out, needing testing. no_reply.py counts that comment as the maintainer's, so the two
  days a reporter has to answer start from it. A reply for an issue closed by then isn't posted.
  The files posted (or left, their issue closed) are deleted from main in one commit.
- Then, for each issue replied on, the other issues of its game (compatibility_list.py's
  reading of an issue's game, and its game_key) closed for no reply ("needs testing") or as not
  planned, but not as duplicates and not labelled "anti-cheat" or "needs store app", get one
  comment each (<!-- pc5-retest X.Y.Z -->): the release fixes something that game needed, and
  a reply with a new zip is welcome. no_reply.py opens such an issue again when someone but the
  maintainer answers it.
- The compatibility list is made again afterwards (an Action's own changes start no other
  workflow, but it can ask for one).

Run with GITHUB_TOKEN, GITHUB_REPOSITORY and RELEASE_VERSION (the tag, with or without its
"v"); RELEASE_REPLIES_DRY_RUN=1 only says what it would do, and changes nothing.
"""
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import compatibility_list as listing   # noqa: E402  the same reading of an issue's game
from no_reply import api, is_bot, pages, remake_list   # noqa: E402  the same REST calls

REPLIES = Path(__file__).resolve().parent.parent / "replies"
REPLY_MARK = "<!-- pc5-release-reply {} -->"
RETEST_MARK = "<!-- pc5-retest {} -->"
DRY_RUN = os.environ.get("RELEASE_REPLIES_DRY_RUN") == "1"
LEFT_ALONE = {listing.CHEAT_LABEL, listing.STORE, "needs steam", listing.SUMMARY_LABEL}


def waiting(version: str) -> list:
    """(issue number, its file, the reply) for each reply waiting for version."""
    found = []
    for path in sorted(REPLIES.glob("*.md")) if REPLIES.is_dir() else []:
        if not path.stem.isdigit():
            continue   # README.md
        head, _, body = path.read_text(encoding="utf-8").partition("\n")
        wanted = re.match(r"\s*version:\s*v?([0-9][0-9.]*)\s*$", head, re.I)
        if not wanted:
            print(f"::warning::release replies: {path.name} has no 'version: X.Y.Z' first line")
            continue
        if wanted.group(1).rstrip(".") == version and body.strip():
            found.append((int(path.stem), path, body.strip()))
    return found


def game_keys(issue: dict, by_number: dict) -> set:
    body = issue.get("body") or ""
    found = {"number": issue["number"], "body": body, "labels": []}
    return {listing.game_key(game) for game in listing.issue_games(found, listing.answers(body), {}, by_number)}


def retest(repo: str, version: str, replied: list, by_number: dict) -> int:
    """One comment on each closed issue of a game a replied issue is about. How many."""
    wanted = {}
    for issue in replied:
        for key in game_keys(issue, by_number):
            wanted.setdefault(key, issue["number"])
    if not wanted:
        return 0
    mark, asked = RETEST_MARK.format(version), 0
    replied_numbers = {issue["number"] for issue in replied}
    for issue in pages(f"repos/{repo}/issues?state=closed"):
        if "pull_request" in issue or issue["number"] in replied_numbers:
            continue
        labels = {label["name"].casefold() for label in issue["labels"]}
        if issue.get("state_reason") == "duplicate" or labels & LEFT_ALONE:
            continue
        if listing.NO_REPLY not in labels and issue.get("state_reason") != "not_planned":
            continue
        fixed_by = next((wanted[key] for key in game_keys(issue, by_number) if key in wanted), None)
        if fixed_by is None:
            continue
        comments = pages(f"repos/{repo}/issues/{issue['number']}/comments")
        if any(is_bot(c.get("user")) and mark in (c.get("body") or "") for c in comments):
            continue
        body = "\n".join([mark, f"PC5 {version} fixes something this game needed (see #{fixed_by}). If you still have it, "
                                "please try again and reply here with the new zip."])
        print(f"release replies: #{issue['number']} asked to try again (#{fixed_by}){' (dry run)' if DRY_RUN else ''}")
        if not DRY_RUN:
            api("POST", f"repos/{repo}/issues/{issue['number']}/comments", {"body": body})
        asked += 1
    return asked


def main() -> int:
    repo = os.environ["GITHUB_REPOSITORY"]
    version = os.environ.get("RELEASE_VERSION", "").strip().lstrip("vV")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        sys.exit(f"release replies: '{version}' isn't a version like 2.3.0")
    replies = waiting(version)
    print(f"release replies: {len(replies)} waiting for {version}")
    posted, done, replied = 0, [], []
    for number, path, body in replies:
        try:
            issue = api("GET", f"repos/{repo}/issues/{number}")
        except urllib.error.HTTPError as error:
            print(f"::warning::release replies: #{number} can't be read ({error.code}): its reply is kept")
            continue
        done.append(path)
        if issue.get("state") != "open" or "pull_request" in issue:
            print(f"release replies: #{number} is closed: its reply isn't posted")
            continue
        if any((label.get("name") or "").casefold() == listing.SUMMARY_LABEL for label in issue.get("labels", [])):
            print(f"release replies: #{number} is the weekly summary: no reply there")
            continue
        print(f"release replies: #{number} {issue['title'][:80]!r}: replied{' (dry run)' if DRY_RUN else ''}")
        replied.append(issue)
        if DRY_RUN:
            continue
        if not posted:
            listing.store_label(*repo.split("/"))   # "fix ready" made the first time
        api("POST", f"repos/{repo}/issues/{number}/comments", {"body": REPLY_MARK.format(version) + "\n" + body})
        api("POST", f"repos/{repo}/issues/{number}/labels", {"labels": [listing.FIX_LABEL]})
        posted += 1
    asked = retest(repo, version, replied, listing.games_by_issue())
    if done and not DRY_RUN:
        git = ["git", "-c", "user.name=github-actions[bot]",
               "-c", "user.email=41898282+github-actions[bot]@users.noreply.github.com"]
        subprocess.run(git + ["rm", "-q", "--"] + [str(p) for p in done], check=True)
        subprocess.run(git + ["commit", "-q", "-m", f"Replies for {version} posted"], check=True)
        subprocess.run(git + ["push", "-q", "origin", "HEAD:main"], check=True)
    print(f"release replies: {posted} posted, {asked} asked to try again{' (dry run)' if DRY_RUN else ''}")
    if (posted or asked) and not DRY_RUN:
        remake_list(repo)
    return 0


if __name__ == "__main__":
    sys.exit(main())
