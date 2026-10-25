#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Collects the reports in the "Game compatibility" discussions category into one table, the
wiki's "Game compatibility" page. The category's "Game compatibility list" discussion (made
the first time, for pinning) points there and to the report form: an Action can make a
discussion but not edit one afterwards ("Resource not accessible by integration").

Run by .github/workflows/compatibility.yml with GITHUB_TOKEN and GITHUB_REPOSITORY. Each
report is a discussion made with .github/DISCUSSION_TEMPLATE/game-compatibility.yml, whose
body GitHub writes as "### <label>" headings, each followed by its answer.
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

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
        f"**[➕ Add a game]({add})**: the report form, which asks what this list needs.",
        "",
        f"**{len(games)} game{'s' * (len(games) != 1)}, {len(reports)} report{'s' * (len(reports) != 1)}:** "
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

    page = "\n".join([
        "Which games run in PC5, as players found: every report of the [Game compatibility]"
        f"(https://github.com/{owner}/{name}/discussions/categories/game-compatibility) discussions, collected by a "
        "GitHub Action each time one is posted or edited.",
        "",
    ] + lines) + "\n"
    publish(owner, name, page)

    pointer = "\n".join([
        f"## ➕ [Add a game: tell how it runs]({add})",
        "",
        "That link opens the report form: the game's name, how it runs, your PC5 version and controller mode. "
        "Post it, and within a minute or two the game is in the list:",
        "",
        f"## 📋 [The game compatibility list](https://github.com/{owner}/{name}/wiki/Game-compatibility)",
        "",
        "Please don't reply here: each report is its own discussion.",
    ]) + "\n"
    if listing is None:
        made = graphql("""mutation($repo: ID!, $category: ID!, $title: String!, $body: String!) {
            createDiscussion(input: {repositoryId: $repo, categoryId: $category, title: $title, body: $body}) {
              discussion { url } } }""", repo=repo["id"], category=category["id"], title=LIST_TITLE, body=pointer)
        print(f"compatibility list: made {made['createDiscussion']['discussion']['url']} (pin it)")
    return 0


def publish(owner: str, name: str, page: str) -> None:
    """The page as the wiki's Game-compatibility.md, pushed when it changed."""
    token = os.environ["GITHUB_TOKEN"]
    with tempfile.TemporaryDirectory() as work:
        wiki = Path(work) / "wiki"
        subprocess.run(["git", "clone", "-q", "--depth", "1",
                        f"https://x-access-token:{token}@github.com/{owner}/{name}.wiki.git", str(wiki)], check=True)
        target = wiki / "Game-compatibility.md"
        if target.is_file() and target.read_text() == page:
            print("compatibility list: the wiki page is up to date")
            return
        target.write_text(page)
        git = ["git", "-C", str(wiki), "-c", "user.name=github-actions[bot]",
               "-c", "user.email=41898282+github-actions[bot]@users.noreply.github.com"]
        subprocess.run(git + ["add", "Game-compatibility.md"], check=True)
        subprocess.run(git + ["commit", "-q", "-m", "Game compatibility list from the reports"], check=True)
        subprocess.run(git + ["push", "-q", "origin", "HEAD"], check=True)
        print("compatibility list: the wiki page is updated")

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:   # shown on the run's page as an annotation
        import traceback
        print("::error::compatibility list: " + " | ".join(traceback.format_exception(error))[-900:].replace("\n", " "))
        raise
