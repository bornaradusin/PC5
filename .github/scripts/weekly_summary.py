#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The maintainer's week on one page: the issue "Weekly summary", made again each Monday.

- .github/workflows/weekly-summary.yml runs it each Monday morning (and by hand). Its issue is
  the one labelled "summary" (made the first time, labelled "summary" and "keep open"): its
  description is written again each week, and a short comment says it was, so the maintainer is
  told. It lists, with links:
  - open issues waiting on the maintainer: no answer of theirs yet, or someone else spoke last
    (bots aside, as no_reply.py reads it);
  - open issues waiting on their reporter, and how long before no_reply.py closes them (an
    issue labelled "keep open", "in progress", "planned" or "enhancement" is never closed);
  - issues closed in the last seven days, and how each ended (related_issues.py's words);
  - game compatibility reports posted in the last seven days (from the discussions, through
    compatibility_list.py's GraphQL; left out when they can't be read).
- The summary issue itself is about no game: compatibility_list.py doesn't list it, no_reply.py
  never closes it (keep open), and issue_logs.py and related_issues.py leave it alone.

Run with GITHUB_TOKEN and GITHUB_REPOSITORY; WEEKLY_SUMMARY_DRY_RUN=1 prints the page instead.
"""
import os
import re
import sys
import urllib.error
import urllib.parse
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import compatibility_list as listing   # noqa: E402  the game reading and GraphQL
import no_reply                         # noqa: E402  who spoke last, and the REST calls
from related_issues import labels_of, state_words   # noqa: E402  how an issue stands, in words

TITLE = "Weekly summary"
SUMMARY = listing.SUMMARY_LABEL
KEEP_OPEN = "keep open"
DRY_RUN = os.environ.get("WEEKLY_SUMMARY_DRY_RUN") == "1"
WEEK = timedelta(days=7)


def when(text: str) -> datetime:
    return datetime.fromisoformat(text.replace("Z", "+00:00"))


def link(issue: dict) -> str:
    return f"[#{issue['number']}]({issue.get('html_url', '')}) {listing.cell(issue.get('title') or '', 70)}"


def waiting(issue: dict, comments: list, owner: str, now: datetime):
    """("maintainer", None) or ("reporter", days left, None when it is never closed) for an open issue."""
    spoken = [c for c in comments if not no_reply.is_bot(c.get("user"))]
    if not spoken or spoken[-1]["user"]["login"].casefold() != owner.casefold():
        return "maintainer", None
    if labels_of(issue) & no_reply.KEEP:
        return "reporter", None
    left = no_reply.WAIT - (now - when(spoken[-1]["created_at"]))
    return "reporter", max(0.0, left.total_seconds() / 86400)


def reports_since(owner: str, name: str, since: datetime) -> list:
    """The game compatibility reports posted since then: (game, how it runs, url), or None."""
    repo = listing.graphql("""query($owner: String!, $name: String!) {
        repository(owner: $owner, name: $name) { discussionCategories(first: 50) { nodes { id name } } } }""",
                           fatal=False, owner=owner, name=name)
    if not repo:
        return None
    category = next((c for c in repo["repository"]["discussionCategories"]["nodes"] if c["name"] == listing.CATEGORY), None)
    if not category:
        return []
    page = listing.graphql("""query($owner: String!, $name: String!, $category: ID!) {
        repository(owner: $owner, name: $name) {
          discussions(first: 50, categoryId: $category, orderBy: {field: CREATED_AT, direction: DESC}) {
            nodes { title url body createdAt } } } }""", fatal=False, owner=owner, name=name, category=category["id"])
    if not page:
        return None
    found = []
    for node in page["repository"]["discussions"]["nodes"]:
        if when(node["createdAt"]) < since or node["title"].strip() == listing.LIST_TITLE:
            continue
        fields = listing.answers(node["body"] or "")
        game = fields.get("Game") or re.sub(r"^\[|\]$", "", node["title"]).strip()
        if game and fields.get("How it runs") in listing.RESULTS:
            found.append((game, fields["How it runs"], node["url"]))
    return found


def page(repo: str, owner: str, now: datetime, issues: list, comments_of, closed: list, reports) -> str:
    """The summary, from open issues (with their comments) and the issues closed this week."""
    on_me, on_them = [], []
    for issue in issues:
        who, left = waiting(issue, comments_of(issue), owner, now)
        if who == "maintainer":
            on_me.append(issue)
        else:
            on_them.append((issue, left))
    lines = [f"The week to {now:%A %d %B %Y}, made by a GitHub Action each Monday. "
             f"{len(on_me)} waiting on you, {len(on_them)} waiting on their reporter.", ""]
    lines += [f"### Waiting on you ({len(on_me)})", ""]
    lines += [f"- {link(i)}" for i in on_me] or ["- None."]
    lines += ["", f"### Waiting on their reporter ({len(on_them)})", ""]
    for issue, left in sorted(on_them, key=lambda item: 99 if item[1] is None else item[1]):
        if left is None:
            lines.append(f"- {link(issue)}: kept open")
        elif left < 0.05:
            lines.append(f"- {link(issue)}: closes for no reply at the next run")
        else:
            lines.append(f"- {link(issue)}: closes for no reply in {left:.1f} days")
    if not on_them:
        lines.append("- None.")
    lines += ["", f"### Closed this week ({len(closed)})", ""]
    lines += [f"- {link(i)}: {state_words(i)}" for i in closed] or ["- None."]
    if reports is not None:
        lines += ["", f"### New game compatibility reports ({len(reports)})", ""]
        lines += [f"- [{listing.cell(game, 60)}]({url}): {result}" for game, result, url in reports] or ["- None."]
    return "\n".join(lines) + "\n"


def labels_ready(repo: str) -> None:
    for name, color, about in ((SUMMARY, "ededed", "The weekly summary: about no game"),
                               (KEEP_OPEN, "bfdadc", "Never closed for no reply")):
        try:
            no_reply.api("GET", f"repos/{repo}/labels/{urllib.parse.quote(name)}")
        except urllib.error.HTTPError as error:
            if error.code != 404:
                raise
            no_reply.api("POST", f"repos/{repo}/labels", {"name": name, "color": color, "description": about})


def main() -> int:
    repo = os.environ["GITHUB_REPOSITORY"]
    owner, name = repo.split("/")
    now = datetime.now(timezone.utc)
    since = now - WEEK
    summary, issues = None, []
    for issue in no_reply.pages(f"repos/{repo}/issues?state=open"):
        if "pull_request" in issue:
            continue
        if SUMMARY in labels_of(issue):
            summary = summary or issue
            continue
        issues.append(issue)
    closed = [i for i in no_reply.pages(f"repos/{repo}/issues?state=closed&since={since:%Y-%m-%dT%H:%M:%SZ}")
              if "pull_request" not in i and i.get("closed_at") and when(i["closed_at"]) >= since
              and SUMMARY not in labels_of(i) and i.get("state_reason") != "duplicate"]
    body = page(repo, owner, now, issues, lambda i: list(no_reply.pages(f"repos/{repo}/issues/{i['number']}/comments")),
                closed, reports_since(owner, name, since))
    if DRY_RUN:
        print(body)
        return 0
    labels_ready(repo)
    if summary is None:
        made = no_reply.api("POST", f"repos/{repo}/issues", {"title": TITLE, "body": body, "labels": [SUMMARY, KEEP_OPEN]})
        print(f"weekly summary: made #{made['number']}")
        return 0
    no_reply.api("PATCH", f"repos/{repo}/issues/{summary['number']}", {"body": body})
    first = body.splitlines()[0]
    no_reply.api("POST", f"repos/{repo}/issues/{summary['number']}/comments",
                 {"body": f"This week's summary is in the description above. {first.split('. ', 1)[-1]}"})
    print(f"weekly summary: #{summary['number']} written again")
    return 0


if __name__ == "__main__":
    sys.exit(main())
