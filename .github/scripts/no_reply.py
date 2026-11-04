#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Closes an issue whose reporter hasn't answered for two days, and opens it again when they do.

- Each day (.github/workflows/no-reply.yml): an open issue whose newest comment, bots' aside,
  is the maintainer's (the repository's owner) and two days old or more gets one comment
  saying so, the "needs testing" label, and is closed as not planned. The game compatibility
  list (compatibility_list.py) then shows its game as needing testing: it didn't run for its
  reporter, but another copy or version of it may. An issue labelled "in progress", "planned"
  or "keep open", or an idea ("enhancement"), is left open; so is one the maintainer hasn't
  answered yet.
- A comment by someone other than the maintainer on an issue closed that way opens it again,
  without the label: the game is being looked at again.

The list is made again afterwards (an Action's own changes start no other workflow, but it
can ask for one). Run with GITHUB_TOKEN, GITHUB_REPOSITORY, GITHUB_EVENT_NAME and
GITHUB_EVENT_PATH; NO_REPLY_DRY_RUN=1 only says what it would do.
"""
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import compatibility_list as listing   # noqa: E402  the same reading of an issue's game

LABEL = listing.NO_REPLY
KEEP = {"in progress", "planned", "keep open", "enhancement"}
WAIT = timedelta(days=2)
DRY_RUN = os.environ.get("NO_REPLY_DRY_RUN") == "1"


def api(method: str, path: str, body=None):
    """GitHub's REST API: the answer's JSON (None for no content); raises on an error."""
    url = path if path.startswith("https://") else f"https://api.github.com/{path}"
    request = urllib.request.Request(url, method=method, data=None if body is None else json.dumps(body).encode(),
                                     headers={"Authorization": f"Bearer {os.environ['GITHUB_TOKEN']}",
                                              "Accept": "application/vnd.github+json",
                                              "X-GitHub-Api-Version": "2022-11-28"})
    with urllib.request.urlopen(request, timeout=60) as response:
        text = response.read()
    return json.loads(text) if text else None


def pages(path: str):
    """Every item of a listing, 100 a page."""
    page = 1
    while True:
        items = api("GET", f"{path}{'&' if '?' in path else '?'}per_page=100&page={page}")
        yield from items
        if len(items) < 100:
            return
        page += 1


def is_bot(user) -> bool:
    return not user or user.get("type") == "Bot" or user.get("login", "").endswith("[bot]")


def label_ready(repo: str) -> None:
    """The label, made the first time."""
    try:
        api("GET", f"repos/{repo}/labels/{urllib.parse.quote(LABEL)}")
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        api("POST", f"repos/{repo}/labels", {"name": LABEL, "color": "c5def5",
                                             "description": "Closed for no reply: another copy or version may run"})


def remake_list(repo: str) -> None:
    try:
        api("POST", f"repos/{repo}/actions/workflows/compatibility.yml/dispatches", {"ref": "main"})
    except urllib.error.HTTPError as error:
        print(f"::warning::no reply: the compatibility list wasn't asked for again ({error.code})")


def is_game(issue: dict, by_number: dict) -> bool:
    body = issue.get("body") or ""
    found = {"number": issue["number"], "body": body, "labels": []}
    return bool(listing.issue_games(found, listing.answers(body), {}, by_number))


def sweep(repo: str, owner: str, now: datetime) -> int:
    """The open issues waiting on their reporter for two days, closed. How many."""
    by_number, closed = listing.games_by_issue(), 0
    for issue in pages(f"repos/{repo}/issues?state=open"):
        if "pull_request" in issue:
            continue
        labels = {label["name"].casefold() for label in issue["labels"]}
        if labels & KEEP:
            continue
        comments = [c for c in pages(f"repos/{repo}/issues/{issue['number']}/comments") if not is_bot(c.get("user"))]
        if not comments or comments[-1]["user"]["login"].casefold() != owner.casefold():
            continue   # the maintainer hasn't answered, or the reporter has: not waiting on them
        last = datetime.fromisoformat(comments[-1]["created_at"].replace("Z", "+00:00"))
        if now - last < WAIT:
            continue
        game = is_game(issue, by_number)
        body = "\n".join([
            "No reply for two days, so I'm closing this for now."
            + (f" The [game compatibility list](https://github.com/{repo}/wiki/Game-compatibility) shows the game as "
               "**needs testing**: it didn't run here, but another copy or version of it may." if game else ""),
            "",
            "Reply here (with a new log from the newest PC5, if you can) and this opens again.",
        ])
        print(f"no reply: #{issue['number']} {issue['title'][:80]!r}, the maintainer's last {last:%Y-%m-%d %H:%M}"
              f"{' (a game)' if game else ''}: closed{' (dry run)' if DRY_RUN else ''}")
        if DRY_RUN:
            continue
        if not closed:
            label_ready(repo)
        api("POST", f"repos/{repo}/issues/{issue['number']}/comments", {"body": body})
        api("POST", f"repos/{repo}/issues/{issue['number']}/labels", {"labels": [LABEL]})
        api("PATCH", f"repos/{repo}/issues/{issue['number']}", {"state": "closed", "state_reason": "not_planned"})
        closed += 1
    return closed


def reopen(repo: str, owner: str, event: dict) -> int:
    """A reply on an issue closed for no reply: open again. 1 when it was."""
    issue, comment = event.get("issue") or {}, event.get("comment") or {}
    user = comment.get("user") or {}
    labels = {label["name"].casefold() for label in issue.get("labels", [])}
    if ("pull_request" in issue or issue.get("state") != "closed" or LABEL not in labels or is_bot(user) or
            user.get("login", "").casefold() == owner.casefold()):
        return 0
    print(f"no reply: #{issue['number']} answered by {user.get('login')}: opened again{' (dry run)' if DRY_RUN else ''}")
    if DRY_RUN:
        return 1
    api("DELETE", f"repos/{repo}/issues/{issue['number']}/labels/{urllib.parse.quote(LABEL)}")
    api("PATCH", f"repos/{repo}/issues/{issue['number']}", {"state": "open"})
    return 1


def main() -> int:
    repo = os.environ["GITHUB_REPOSITORY"]
    owner = repo.split("/")[0]
    if os.environ.get("GITHUB_EVENT_NAME") == "issue_comment":
        changed = reopen(repo, owner, json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text()))
    else:
        changed = sweep(repo, owner, datetime.now(timezone.utc))
        print(f"no reply: {changed} closed")
    if changed and not DRY_RUN:
        remake_list(repo)
    return 0


if __name__ == "__main__":
    sys.exit(main())
