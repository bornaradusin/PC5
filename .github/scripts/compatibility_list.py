#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Collects the reports in the "Game compatibility" discussions category into one table,
the body of the category's "Game compatibility list" discussion (made the first time).

Run by .github/workflows/compatibility.yml with GITHUB_TOKEN and GITHUB_REPOSITORY. Each
report is a discussion made with .github/DISCUSSION_TEMPLATE/game-compatibility.yml, whose
body GitHub writes as "### <label>" headings, each followed by its answer.
"""
import json
import os
import re
import sys
import urllib.request

CATEGORY = "Game compatibility"
LIST_TITLE = "Game compatibility list"
RESULTS = {  # the form's answers, best first, with the mark the table shows
    "Works perfectly": "✅ Works perfectly",
    "Playable with issues": "🟡 Playable with issues",
    "Starts but not playable": "🟠 Starts but not playable",
    "Doesn't start": "❌ Doesn't start",
}


def graphql(query: str, **variables) -> dict:
    request = urllib.request.Request(
        "https://api.github.com/graphql",
        data=json.dumps({"query": query, "variables": variables}).encode(),
        headers={"Authorization": f"bearer {os.environ['GITHUB_TOKEN']}", "Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        answer = json.load(response)
    if answer.get("errors"):
        # As an annotation too, which the run's page shows without opening its log.
        print(f"::error::compatibility list: {json.dumps(answer['errors'])[:900]}")
        sys.exit(f"compatibility list: {answer['errors']}")
    return answer["data"]


def answers(body: str) -> dict:
    """The form's answers by label; "" for one left empty."""
    fields, label, lines = {}, None, []
    for line in body.replace("\r\n", "\n").split("\n"):
        heading = re.match(r"^###\s+(.+?)\s*$", line)
        if heading:
            if label is not None:
                fields[label] = "\n".join(lines).strip()
            label, lines = heading.group(1), []
        elif label is not None:
            lines.append(line)
    if label is not None:
        fields[label] = "\n".join(lines).strip()
    return {key: "" if value == "_No response_" else value for key, value in fields.items()}


def cell(text: str, limit: int = 120) -> str:
    """One table cell: one line, no pipes, cut at limit characters."""
    text = " ".join(text.split()).replace("|", "/")
    return text if len(text) <= limit else text[:limit - 1].rstrip() + "…"


def main() -> int:
    owner, name = os.environ["GITHUB_REPOSITORY"].split("/")
    repo = graphql("""query($owner: String!, $name: String!) {
        repository(owner: $owner, name: $name) {
          id
          discussionCategories(first: 50) { nodes { id name } }
        } }""", owner=owner, name=name)["repository"]
    category = next((c for c in repo["discussionCategories"]["nodes"] if c["name"] == CATEGORY), None)
    if not category:
        print(f"compatibility list: no '{CATEGORY}' category yet: nothing to do")
        return 0

    reports, listing, after = [], None, None
    while True:
        page = graphql("""query($owner: String!, $name: String!, $category: ID!, $after: String) {
            repository(owner: $owner, name: $name) {
              discussions(first: 100, after: $after, categoryId: $category,
                          orderBy: {field: CREATED_AT, direction: ASC}) {
                pageInfo { hasNextPage endCursor }
                nodes { id number title url body createdAt author { login } upvoteCount locked }
              } } }""", owner=owner, name=name, category=category["id"], after=after)
        discussions = page["repository"]["discussions"]
        for discussion in discussions["nodes"]:
            if discussion["title"].strip() == LIST_TITLE:
                listing = listing or discussion
                continue
            fields = answers(discussion["body"] or "")
            game = fields.get("Game") or re.sub(r"^\[|\]$", "", discussion["title"]).strip()
            result = fields.get("How it runs", "")
            if not game or result not in RESULTS:
                continue   # not made with the form
            reports.append({
                "game": game, "result": result, "pc5": fields.get("PC5 version", ""),
                "edition": fields.get("Store and version", ""), "controller": fields.get("Controller mode", ""),
                "settings": fields.get("Settings that helped", ""), "notes": fields.get("Notes", ""),
                "url": discussion["url"], "number": discussion["number"],
                "author": (discussion["author"] or {}).get("login", "someone"),
                "date": discussion["createdAt"][:10], "votes": discussion["upvoteCount"],
            })
        if not discussions["pageInfo"]["hasNextPage"]:
            break
        after = discussions["pageInfo"]["endCursor"]

    order = list(RESULTS)
    reports.sort(key=lambda r: (r["game"].casefold(), order.index(r["result"]), r["date"]))
    games = {r["game"].casefold() for r in reports}
    counts = {result: sum(1 for r in reports if r["result"] == result) for result in RESULTS}
    add = f"https://github.com/{owner}/{name}/discussions/new?category=game-compatibility"
    lines = [
        f"## ➕ [Add a game: tell how it runs]({add})",
        "",
        f"That link opens the report form: the game's name, how it runs (works, playable with issues, "
        "starts but isn't playable, doesn't start), your PC5 version and controller mode. Post it, and the game "
        "appears in the table below within a minute or two. Please don't reply here: each report is its own discussion.",
        "",
        f"**{len(games)} games, {len(reports)} reports:** "
        + ", ".join(f"{RESULTS[result]} {counts[result]}" for result in RESULTS) + ".",
        "",
        "| Game | How it runs | PC5 | Store and version | Controller | Settings that helped | Report |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for r in reports:
        lines.append(f"| {cell(r['game'], 60)} | {RESULTS[r['result']]} | {cell(r['pc5'], 12)} | {cell(r['edition'], 40)} | "
                     f"{cell(r['controller'], 20)} | {cell(r['settings'])} | [#{r['number']}]({r['url']}) by "
                     f"{cell(r['author'], 40)}, {r['date']} |")
    if not reports:
        lines.append("| *No reports yet* | | | | | | |")
    lines += ["", "A game that crashes or doesn't start: please also open an "
              f"[issue](https://github.com/{owner}/{name}/issues) with its log, so it can be fixed."]
    body = "\n".join(lines) + "\n"

    if listing is None:
        made = graphql("""mutation($repo: ID!, $category: ID!, $title: String!, $body: String!) {
            createDiscussion(input: {repositoryId: $repo, categoryId: $category, title: $title, body: $body}) {
              discussion { url } } }""", repo=repo["id"], category=category["id"], title=LIST_TITLE, body=body)
        print(f"compatibility list: made {made['createDiscussion']['discussion']['url']} (pin it)")
    elif listing["body"].replace("\r\n", "\n") != body:
        # A locked discussion (replies off, so reports go through the form) can't be edited
        # by the Action: unlocked for the edit, and locked again.
        if listing.get("locked"):
            graphql("""mutation($id: ID!) { unlockLockable(input: {lockableId: $id}) { unlockedRecord { locked } } }""",
                    id=listing["id"])
        graphql("""mutation($id: ID!, $body: String!) {
            updateDiscussion(input: {discussionId: $id, body: $body}) { discussion { url } } }""",
                id=listing["id"], body=body)
        if listing.get("locked"):
            graphql("""mutation($id: ID!) { lockLockable(input: {lockableId: $id}) { lockedRecord { locked } } }""",
                    id=listing["id"])
        print(f"compatibility list: {len(reports)} reports of {len(games)} games, in {listing['url']}")
    else:
        print("compatibility list: already up to date")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:   # shown on the run's page as an annotation
        import traceback
        print("::error::compatibility list: " + " | ".join(traceback.format_exception(error))[-900:].replace("\n", " "))
        raise
