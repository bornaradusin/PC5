#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Makes the wiki's "Game compatibility" page: one table of how games run in PC5, from two
places.

- A game that runs: a report in the "Game compatibility" discussions category, made with
  .github/DISCUSSION_TEMPLATE/game-compatibility.yml (GitHub writes its body as "### <label>"
  headings, each followed by its answer). It lists as posted: works perfectly, or playable with
  issues. Each one gets one comment once the page with it is published.
- A game that doesn't run: an issue in this repository, with the game's log. It lists by its
  state: being looked at while it is open (a fix out, needing testing, while it is labelled "fix
  ready"); working perfectly once it is closed as completed
  (unless a player's report lists the game already); needs testing once it is closed for no
  reply (labelled "needs testing" by no_reply.py: it didn't run for its reporter, but another
  copy or version may); unresolved (it doesn't run) once it is closed as not planned
  otherwise; playable with issues once it is closed labelled "playable with issues" (it runs, with
  a setting or with something still wrong, which the issue says). An issue labelled "needs store app" (its copy of the game needs Steam, the Xbox app or another
  store's client running, which PC5 can't do) shows as such, not as a game that doesn't run:
  another copy may. Labelled while open, it gets one comment saying so and is closed as not
  planned. An issue labelled "anti-cheat" (the game runs anti-cheat, which needs Windows' own
  kernel and can't run on the PS5, whatever the copy) shows as such, and is closed the same way
  when labelled while open. An issue closed as a duplicate
  isn't listed. The game is the issue's "Game" field (.github/ISSUE_TEMPLATE/1-game.yml, which
  PC5's Report on GitHub opens filled in), or the "**Game:**" line older PC5s wrote. For an
  issue written by hand, before the form, games-by-issue.txt beside this script names it, or
  a report names it.

A game has one row (game_key: the same name but for case, punctuation, a leading "The" and
Roman numerals; editions stay apart): the best of everything said about it, reports and issues
alike, in SHOWN's order (works perfectly first), the newest of equals. A game that runs for
someone can run, given the right copy or version, so a report that it works stands over an
issue that it doesn't, and the row names the game's other reports and issues beside it. An open
issue whose game an earlier report lists as running gets one comment with what that report says
(its settings may help).

The repository owner can add to an issue's row: a comment of theirs on the issue with a line
"List notes: ..." gives its Notes, "List settings: ..." its Settings that helped, "List version:
..." its PC5 version (the one that fixed it, say), the newest such line winning. That works on a
closed issue too. Without one, an issue closed as fixed or playable shows the newest PC5 published
when it was closed.

A report that a game doesn't run, made before issues were the way (the form no longer offers
it), names its game's issue: without one it gets one comment saying so and is closed. The
category's "Game compatibility list" discussion (made the first time, for pinning) points to
the page and to the report form: an Action can make a discussion but not edit one afterwards.

Run by .github/workflows/compatibility.yml with GITHUB_TOKEN and GITHUB_REPOSITORY.
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
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
NOT_RUNNING = ("Starts but not playable", "Doesn't start")   # older reports: their issue lists the game
LOOKED_AT = "🔍 Being looked at"     # an issue still open
FIX_READY = "🔧 Fix out, needs testing"   # an open issue labelled FIX_LABEL: a PC5 out now should fix it
FIX_LABEL = "fix ready"
FIX_ABOUT = "A PC5 that's out now should fix this: waiting for the reporter to test it"
FIXED = "Works perfectly"           # an issue closed as completed
UNRESOLVED = "❌ Unresolved: doesn't run"   # an issue closed as not planned
NEEDS_TESTING = "🧪 Needs testing"    # an issue closed for no reply (no_reply.py's label)
NO_REPLY = "needs testing"
PLAYABLE = "playable with issues"   # an issue closed with this label: it runs, with a setting or a problem
NEEDS_STORE = "🏪 Copy needs its store's app"   # an issue labelled STORE: the copy, not the game
STORE = "needs store app"   # "needs steam" before 2.0.3: the label was renamed, so its issues kept it
STORE_ABOUT = "This copy needs its store's app (Steam, Xbox app, EA app...) running: another copy may run"
ANTI_CHEAT = "🛡️ Anti-cheat: can't run"   # an issue labelled CHEAT_LABEL: the game itself, any copy
CHEAT_LABEL = "anti-cheat"
CHEAT_ABOUT = "Runs anti-cheat (Easy Anti-Cheat, BattlEye...), which needs Windows' kernel: no copy runs"   # 100 at most
CHEATED = "<!-- pc5-compatibility: anti-cheat -->"   # its comment when it closes such an issue
STORED = "<!-- pc5-compatibility: needs steam -->"   # its comment when it closes such an issue (unchanged: it marks the old ones too)
SHOWN = ["Works perfectly", "Playable with issues", FIX_READY, LOOKED_AT, NEEDS_TESTING, NEEDS_STORE, ANTI_CHEAT, UNRESOLVED]
MARK = "<!-- pc5-compatibility: needs an issue -->"   # the Action's comment, so it is made once
UNKNOWN = {}   # an issue that couldn't be looked up: the report waits for the next run
LISTED = "<!-- pc5-compatibility: listed -->"   # the Action's comment once a report is in the list
LISTED_ALREADY = "<!-- pc5-compatibility: listed already -->"   # its comment on an issue whose game a report lists
OWNER_LINE = re.compile(r"^\s*(?:\*\*)?List (notes|settings|version):(?:\*\*)?\s*(.+?)\s*$", re.I | re.M)
NUMERALS = {"ii": "2", "iii": "3", "iv": "4", "v": "5", "vi": "6", "vii": "7", "viii": "8", "ix": "9"}
ISSUE_STATE = {FIXED: "fixed", "Playable with issues": "playable with issues", "🔍 Being looked at": "open", FIX_READY: "fix out, needs testing", "🧪 Needs testing": "needs testing", "❌ Unresolved: doesn't run": "doesn't run",
               NEEDS_STORE: "this copy needs its store's app", ANTI_CHEAT: "anti-cheat: can't run"}


def game_key(name: str) -> str:
    """What two names of one game have the same: "Baldur's Gate 2" and "baldurs gate II", "The
    Binding of Isaac: Rebirth" and "Binding of Isaac Rebirth". Case, punctuation, a leading
    "The" and Roman numerals (II to IX as words) aside; an edition named stays (the Enhanced
    Edition isn't the original)."""
    text = name.casefold().replace("&", " and ")
    for apostrophe in ("'", "\u2019"):
        text = text.replace(apostrophe, "")
    words = [w for w in re.split(r"[\W_]+", text) if w]
    if len(words) > 1 and words[0] == "the":
        words = words[1:]
    return "".join(NUMERALS.get(w, w) for w in words) or name.casefold()


def graphql(query: str, fatal: bool = True, **variables):
    """The answer's data. An error stops the run, or, with fatal False, is a warning and None."""
    request = urllib.request.Request(
        "https://api.github.com/graphql",
        data=json.dumps({"query": query, "variables": variables}).encode(),
        headers={"Authorization": f"bearer {os.environ['GITHUB_TOKEN']}", "Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        answer = json.load(response)
    if answer.get("errors") and not fatal:
        print(f"::warning::compatibility list: {json.dumps(answer['errors'])[:900]}")
        return None
    if answer.get("errors"):
        # As an annotation too, which the run's page shows without opening its log.
        print(f"::error::compatibility list: {json.dumps(answer['errors'])[:900]}")
        sys.exit(f"compatibility list: {answer['errors']}")
    return answer["data"]


LABELS = ("Game", "Store and version", "How it runs", "Issue", "PC5 version", "Controller mode",
          "Settings that helped", "Notes")


def answers(body: str) -> dict:
    """The form's answers by label; "" for one left empty. A heading with its answer on the same
    line, as a report edited by hand may have it ("### Issue #43"), counts too."""
    fields, label, lines = {}, None, []
    for line in body.replace("\r\n", "\n").split("\n"):
        heading = re.match(r"^ {0,3}###\s+(.+?)\s*$", line)   # Markdown allows 3 spaces before it
        if heading:
            if label is not None:
                fields[label] = "\n".join(lines).strip()
            label, lines = heading.group(1), []
            for known in LABELS:
                rest = label[len(known):]
                if label.casefold().startswith(known.casefold()) and rest[:1] in (" ", ":", "\t"):
                    label, lines = known, [rest.lstrip(" :\t")]
                    break
        elif label is not None:
            lines.append(line)
    if label is not None:
        fields[label] = "\n".join(lines).strip()
    return {key: "" if value == "_No response_" else value for key, value in fields.items()}


def pc5_version(text: str) -> str:
    """A report's PC5 version, as "2.0.3": "" for one that can't be PC5's (a firmware version such
    as 10.00 or 13.60, which the form's field sometimes gets)."""
    found = re.match(r"\s*(?:PC5\s*)?v?(\d+)\.(\d+)(?:\.(\d+))?", text, re.I)
    if not found or int(found.group(1)) > 2:
        return ""
    return ".".join(part for part in found.groups() if part is not None)


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

    reports, named, listing, after = [], {}, None, None
    while True:
        page = graphql("""query($owner: String!, $name: String!, $category: ID!, $after: String) {
            repository(owner: $owner, name: $name) {
              discussions(first: 100, after: $after, categoryId: $category, states: [OPEN, CLOSED],
                          orderBy: {field: CREATED_AT, direction: ASC}) {
                pageInfo { hasNextPage endCursor }
                nodes { id number title url body createdAt author { login } upvoteCount locked closed
                        comments(first: 50) { nodes { body author { login } } } }
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
            if result in NOT_RUNNING:
                # Listed by its issue, under this report's name for the game when the issue has none.
                issue = linked_issue(owner, name, fields.get("Issue", ""))
                asked = any(MARK in (c["body"] or "") for c in discussion["comments"]["nodes"])
                if issue is None and (discussion["author"] or {}).get("login", "").casefold() != owner.casefold():
                    needs_issue(discussion, owner, name, asked)
                elif issue and issue is not UNKNOWN:
                    named.setdefault(issue["number"], game)
                    if asked and discussion["closed"] and graphql(
                            """mutation($id: ID!) { reopenDiscussion(input: {discussionId: $id}) { discussion { id } } }""",
                            fatal=False, id=discussion["id"]) is not None:
                        print(f"compatibility list: #{discussion['number']} names issue #{issue['number']}: opened again")
                continue
            shown, issue = result, None
            told = any(LISTED in (c["body"] or "") for c in discussion["comments"]["nodes"])
            reports.append({
                "id": discussion["id"], "told": told, "locked": discussion["locked"],
                "game": game, "result": result, "shown": shown, "issue": issue, "pc5": pc5_version(fields.get("PC5 version", "")),
                "edition": fields.get("Store and version", ""), "controller": fields.get("Controller mode", ""),
                "settings": fields.get("Settings that helped", ""), "notes": fields.get("Notes", ""),
                "url": discussion["url"], "number": discussion["number"],
                "author": (discussion["author"] or {}).get("login", "someone"),
                "date": discussion["createdAt"][:10], "created": discussion["createdAt"], "votes": discussion["upvoteCount"],
                "closed": discussion["closed"],
            })
        if not discussions["pageInfo"]["hasNextPage"]:
            break
        after = discussions["pageInfo"]["endCursor"]
    # A game with an issue is listed by what was said last: while one of its issues is open, by
    # that issue alone (a report posted meanwhile is turned away, one posted before waits); once
    # they are closed, by the newest one's state, or by the reports posted after it closed, which
    # name it beside them (a fixed one not at all). An open issue that earlier reports list as
    # running is told once what they say.
    store_label(owner, name)
    issues = issue_reports(owner, name, named)
    listed = {}
    for r in reports:
        listed.setdefault(game_key(r["game"]), []).append(r)
    told_issues = 0
    for row in issues:
        same = listed.get(game_key(row["game"]), [])
        if row["open"] and same and not row["told_listed"]:
            told_issues += tell_issue(row, [r for r in same if r["created"] < row["created"]], owner, name)
    # One row a game: the best said of it, reports and issues alike (the newest of equals), with
    # the others named beside it. A game that runs for someone can run, given the right copy.
    games = {}
    for r in reports + issues:
        games.setdefault(game_key(r["game"]), []).append(r)
    reports = []
    for group in games.values():
        group = sorted(sorted(group, key=lambda r: r["created"], reverse=True), key=lambda r: SHOWN.index(r["shown"]))
        best = group[0]
        for other in group[1:]:
            best.setdefault("earlier" if "votes" in other else "issues", []).append(other)
        reports.append(best)
    if told_issues:
        print(f"::notice::compatibility list: told {told_issues} issue{'s' * (told_issues != 1)} what reports say")

    reports.sort(key=lambda r: (game_key(r["game"]), SHOWN.index(r["shown"]), r["date"]))
    counts = {shown: sum(1 for r in reports if r["shown"] == shown) for shown in SHOWN}
    add = f"https://github.com/{owner}/{name}/discussions/new?category=game-compatibility"
    lines = [
        f"**[➕ Add a game]({add})**: the report form, which asks what this list needs.",
        "",
        ", ".join(f"{label(shown)} {counts[shown]}" for shown in SHOWN) + ".",
        "",
        "| Game | How it runs | PC5 | Store and version | Controller | Settings that helped | Notes | Report or issue |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for r in reports:
        how = label(r["shown"])
        lines.append(f"| {cell(r['game'], 60)} | {how} | {cell(r['pc5'], 12)} | {cell(r['edition'], 40)} | "
                     f"{cell(r['controller'], 20)} | {cell(r['settings'], 1500)} | {cell(r['notes'], 1500)} | "
                     f"[#{r['number']}]({r['url']}) by "
                     f"{cell(r['author'], 40)}, {r['date']}"
                     + "".join(f"; also [#{e['number']}]({e['url']}) {label(e['shown'])}"
                               for e in sorted(r.get("earlier", []), key=lambda e: e["date"]))
                     + "".join(f"; issue [#{i['number']}]({i['url']}) {ISSUE_STATE.get(i['shown'], '')}"
                               for i in r.get("issues", [])) + " |")
    if not reports:
        lines.append("| *No reports yet* | | | | | | | |")
    lines += ["", "A game that crashes, doesn't start or isn't playable: open an "
              f"[issue](https://github.com/{owner}/{name}/issues/new/choose) with its log (PC5's **Report on GitHub** "
              "writes one for you). The game shows as being looked at while the issue is open (or as a fix out, needing testing, "
              "once a PC5 that should fix it is out), then as working "
              "perfectly once the issue is closed as completed, or as unresolved (it doesn't run) once it is closed "
              "as not planned. A copy of a game that needs its store's app running (Steam, the Xbox app or another), which PC5 can't "
              "do, shows as such: another copy of it may run. A game that runs anti-cheat (Easy Anti-Cheat, BattlEye or its "
              "own) shows as **anti-cheat: can't run**: anti-cheat needs Windows' own kernel, which the PS5 doesn't have, "
              "so no copy of it runs. An issue whose reporter doesn't answer for two days is closed, and the game shows as "
              "needing testing: it didn't run for them, but another copy or version of it may. Tested it? Reply on "
              "the issue, or open a new one with your log. A game has one row: the best result anyone has had with it "
              "(a game that runs for one player can run, given the right copy or version), with its other reports and "
              "issues named beside it."]

    page = "\n".join([
        "Which games run in PC5: the games players report running, from the [Game compatibility]"
        f"(https://github.com/{owner}/{name}/discussions/categories/game-compatibility) discussions, and the games "
        f"with an [issue](https://github.com/{owner}/{name}/issues?q=is%3Aissue), by how it stands. A GitHub Action "
        "makes it again each time either changes.",
        "",
    ] + lines) + "\n"
    publish(owner, name, page)
    told = sum(tell_listed(r, owner, name) for r in reports
               if not r["told"] and not r["locked"] and r["author"].casefold() != owner.casefold())
    if told:
        print(f"::notice::compatibility list: told {told} report{'s' * (told != 1)} they are in the list")

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


GAME_LINE = re.compile(r"^\s*(?:\*\*)?Game:(?:\*\*)?\s*(.+?)\s*$", re.I | re.M)
PC5_LINE = re.compile(r"^\s*(?:\*\*)?PC5:(?:\*\*)?\s*v?([0-9][0-9.]*)", re.I | re.M)
SAW_LINE = re.compile(r"^\s*(?:\*\*)?PC5 saw:(?:\*\*)?\s*(.+?)\s*$", re.I | re.M)
GAMES_BY_ISSUE = Path(__file__).with_name("games-by-issue.txt")


def games_by_issue() -> dict:
    """games-by-issue.txt: the games of issues written by hand, by the issue's number."""
    games = {}
    for line in GAMES_BY_ISSUE.read_text(encoding="utf-8").splitlines() if GAMES_BY_ISSUE.exists() else []:
        found = re.match(r"\s*(\d+)\s+(\S.*?)\s*$", line)   # "# …" lines don't match: they say what it is
        if found:
            games.setdefault(int(found.group(1)), []).append(found.group(2))
    return games


def issue_games(issue: dict, fields: dict, named: dict, by_number: dict) -> list:
    """The games an issue is about: games-by-issue.txt's for it, its form's "Game" field, the
    "**Game:**" line of an older PC5's Report on GitHub (without the program in brackets), or
    the game a report names it for. An empty list for an issue about no one game."""
    if issue["number"] in by_number:
        return by_number[issue["number"]]
    if fields.get("Game"):
        return [fields["Game"]]
    found = GAME_LINE.search(issue["body"] or "")
    if found:
        return [re.sub(r"\s*\((?:`[^`]*`|[^()]*\.exe)\)\s*$", "", found.group(1), flags=re.I).strip()]
    if issue["number"] in named:
        return [named[issue["number"]]]
    return []


def first_line(text: str) -> str:
    """A field's first line with something on it, without code marks."""
    return next((line.strip(" `") for line in text.splitlines() if line.strip(" `")), "")


def published_releases(owner: str, name: str) -> list:
    """The published releases (not drafts or pre-releases), as (published at, version), oldest first."""
    url = f"https://api.github.com/repos/{owner}/{name}/releases?per_page=100"
    headers = {"Authorization": f"Bearer {os.environ['GITHUB_TOKEN']}", "Accept": "application/vnd.github+json"}
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60) as response:
            releases = json.load(response)
    except (urllib.error.URLError, ValueError) as error:
        print(f"::warning::compatibility list: the releases couldn't be read ({error})")
        return []
    return sorted((r["published_at"], r["tag_name"].lstrip("vV")) for r in releases
                  if not r.get("draft") and not r.get("prerelease") and r.get("published_at"))


def issue_reports(owner: str, name: str, named: dict) -> list:
    """A row for each issue and game it is about, by the issue's state (main() keeps a game's best
    row). A fixed one shows the PC5 that was out when it was closed."""
    rows, after, by_number = [], None, games_by_issue()
    releases = published_releases(owner, name)
    while True:
        page = graphql("""query($owner: String!, $name: String!, $after: String) {
            repository(owner: $owner, name: $name) {
              issues(first: 100, after: $after, states: [OPEN, CLOSED], orderBy: {field: CREATED_AT, direction: ASC}) {
                pageInfo { hasNextPage endCursor }
                nodes { id number title url body state stateReason createdAt closedAt author { login }
                        labels(first: 20) { nodes { name } } comments(last: 100) { nodes { body author { login } } } }
              } } }""", owner=owner, name=name, after=after)["repository"]["issues"]
        for node in page["nodes"]:
            body = node["body"] or ""
            issue = {"number": node["number"], "title": node["title"], "body": body,
                     "labels": [label["name"].casefold() for label in node["labels"]["nodes"]]}
            fields = answers(body)
            if node["stateReason"] == "DUPLICATE":
                continue
            if CHEAT_LABEL in issue["labels"]:
                shown = ANTI_CHEAT
            elif STORE in issue["labels"]:
                shown = NEEDS_STORE
            elif node["state"] == "OPEN":
                shown = FIX_READY if FIX_LABEL in issue["labels"] else LOOKED_AT
            elif NO_REPLY in issue["labels"]:
                shown = NEEDS_TESTING
            elif PLAYABLE in issue["labels"]:
                shown = "Playable with issues"
            elif node["stateReason"] == "NOT_PLANNED" or "wontfix" in issue["labels"]:
                shown = UNRESOLVED
            else:
                shown = FIXED
            pc5 = re.match(r"\s*v?([0-9][0-9.]*)", fields.get("PC5 version", "")) or PC5_LINE.search(body)
            saw = first_line(fields.get("What PC5 saw", "")) or first_line(fields.get("What goes wrong", ""))
            if not saw and SAW_LINE.search(body):
                saw = SAW_LINE.search(body).group(1)
            # The owner's own words for the list, from their newest comment that has them.
            extra = {}
            for comment in node["comments"]["nodes"]:
                if (comment["author"] or {}).get("login", "").casefold() != owner.casefold():
                    continue
                for kind, text in OWNER_LINE.findall(comment["body"] or ""):
                    extra[kind.casefold()] = text
            for game in issue_games(issue, fields, named, by_number):
                row = {
                    "told": True, "locked": True, "game": game, "shown": shown,
                    "pc5": pc5.group(1).rstrip(".") if pc5 else "", "edition": "", "controller": "",
                    "settings": extra.get("settings", ""),
                    "notes": ("No reply: another copy or version may run. " + saw).strip() if shown == NEEDS_TESTING
                             else "This copy needs its store's app running (Steam, the Xbox app or another), which PC5 "
                                  "can't do: another copy of the game may run." if shown == NEEDS_STORE
                             else "It runs anti-cheat, which needs Windows' own kernel and can't run on the PS5, "
                                  "whichever copy." if shown == ANTI_CHEAT
                             else "It runs: the issue says what to set, or what is still wrong."
                             if shown == "Playable with issues"
                             else saw if shown != FIXED else "",
                    "created": node["createdAt"], "closed_at": node["closedAt"] or "",
                    "url": node["url"], "number": node["number"],
                    "author": (node["author"] or {}).get("login", "someone"), "date": node["createdAt"][:10],
                    "id": node["id"], "open": node["state"] == "OPEN",
                    "told_listed": any(LISTED_ALREADY in (c["body"] or "") for c in node["comments"]["nodes"]),
                }
                rows.append(row)
                if extra.get("notes"):
                    row["notes"] = extra["notes"]
                if extra.get("version"):
                    row["pc5"] = extra["version"].lstrip("vV")
                elif shown in (FIXED, "Playable with issues") and node["closedAt"]:
                    out = [version for published, version in releases if published <= node["closedAt"]]
                    if out:
                        row["pc5"] = out[-1]
            if shown == NEEDS_STORE and node["state"] == "OPEN":
                close_store_copy(node, owner, name)
            if shown == ANTI_CHEAT and node["state"] == "OPEN":
                close_anti_cheat(node, owner, name)
        if not page["pageInfo"]["hasNextPage"]:
            return rows
        after = page["pageInfo"]["endCursor"]


def close_store_copy(node: dict, owner: str, name: str) -> None:
    """An open issue labelled STORE: one comment saying why, and closed as not planned."""
    if not any(STORED in (c["body"] or "") for c in node["comments"]["nodes"]):
        body = "\n".join([
            STORED,
            "This copy of the game needs its store's app running (Steam, the Xbox app or another), and the PS5 can't run "
            "a store's app next to the game. PC5's Steam emulator stands in for Steam for most Steam copies, but this "
            "copy needs more than that. So I'm closing this. The "
            f"[game compatibility list](https://github.com/{owner}/{name}/wiki/Game-compatibility) shows the game as "
            "**copy needs its store's app**, not as a game that doesn't run.",
            "",
            "Another copy of the game may run. If you try one, please open a new issue with its log.",
        ])
        if graphql("""mutation($id: ID!, $body: String!) { addComment(input: {subjectId: $id, body: $body}) { clientMutationId } }""",
                   fatal=False, id=node["id"], body=body) is None:
            return
    if graphql("""mutation($id: ID!) { closeIssue(input: {issueId: $id, stateReason: NOT_PLANNED}) { issue { id } } }""",
               fatal=False, id=node["id"]) is not None:
        print(f"compatibility list: #{node['number']} needs its store's app: closed")


def close_anti_cheat(node: dict, owner: str, name: str) -> None:
    """An open issue labelled CHEAT_LABEL: one comment saying why, and closed as not planned."""
    if not any(CHEATED in (c["body"] or "") for c in node["comments"]["nodes"]):
        body = "\n".join([
            CHEATED,
            "This game runs anti-cheat (Easy Anti-Cheat, BattlEye or its own), which works as a driver inside Windows' own "
            "kernel. There's no Windows kernel on the PS5, so the game can't run in PC5, whichever copy it is. So I'm "
            f"closing this. The [game compatibility list](https://github.com/{owner}/{name}/wiki/Game-compatibility) shows "
            "the game as **anti-cheat: can't run**.",
        ])
        if graphql("""mutation($id: ID!, $body: String!) { addComment(input: {subjectId: $id, body: $body}) { clientMutationId } }""",
                   fatal=False, id=node["id"], body=body) is None:
            return
    if graphql("""mutation($id: ID!) { closeIssue(input: {issueId: $id, stateReason: NOT_PLANNED}) { issue { id } } }""",
               fatal=False, id=node["id"]) is not None:
        print(f"compatibility list: #{node['number']} runs anti-cheat: closed")


def store_label(owner: str, name: str) -> None:
    """The STORE, CHEAT_LABEL and FIX_LABEL labels, made the first time, so they are there to choose."""
    url = f"https://api.github.com/repos/{owner}/{name}/labels"
    headers = {"Authorization": f"Bearer {os.environ['GITHUB_TOKEN']}", "Accept": "application/vnd.github+json"}
    for label_name, color, about in ((STORE, "fbca04", STORE_ABOUT), (CHEAT_LABEL, "5319e7", CHEAT_ABOUT),
                                     (FIX_LABEL, "0e8a16", FIX_ABOUT)):
        try:
            urllib.request.urlopen(urllib.request.Request(f"{url}/{urllib.parse.quote(label_name)}", headers=headers),
                                   timeout=60)
            continue
        except urllib.error.HTTPError as error:
            if error.code != 404:
                print(f"::warning::compatibility list: the '{label_name}' label couldn't be looked up ({error.code})")
                continue
        try:
            urllib.request.urlopen(urllib.request.Request(url, method="POST", headers=headers, data=json.dumps(
                {"name": label_name, "color": color, "description": about}).encode()), timeout=60)
            print(f"compatibility list: the '{label_name}' label made")
        except urllib.error.HTTPError as error:
            print(f"::warning::compatibility list: the '{label_name}' label couldn't be made ({error.code})")


def label(shown: str) -> str:
    """The table's mark for a result, or for one being looked at."""
    return RESULTS.get(shown, shown)


def linked_issue(owner: str, name: str, text: str):
    """The issue (not a pull request) of this repository that text names, as #61, 61 or its
    link: its number, url, state, stateReason and label names; None when there is none."""
    found = re.search(rf"(?:github\.com/{re.escape(owner)}/{re.escape(name)}/issues/|#|^\s*)(\d+)\b", text, re.I)
    if not found:
        return None
    data = graphql("""query($owner: String!, $name: String!, $number: Int!) {
        repository(owner: $owner, name: $name) {
          issueOrPullRequest(number: $number) {
            __typename
            ... on Issue { number url state stateReason labels(first: 20) { nodes { name } } }
          } } }""", fatal=False, owner=owner, name=name, number=int(found.group(1)))
    if data is None:
        return UNKNOWN
    node = data["repository"]["issueOrPullRequest"]
    if not node or node["__typename"] != "Issue":
        return None
    return {"number": node["number"], "url": node["url"], "state": node["state"], "stateReason": node["stateReason"],
            "labels": [label["name"].casefold() for label in node["labels"]["nodes"]]}


def tell_issue(issue: dict, reports: list, owner: str, name: str) -> int:
    """One comment on an open issue whose game reports list: what each says. 1 when made."""
    shown = [r for r in sorted(reports, key=lambda r: (SHOWN.index(r["shown"]), r["date"])) if r["shown"] in RESULTS]
    if not shown:
        return 0
    whose = "a player's report" if len(shown) == 1 else "players' reports"
    lines = [
        LISTED_ALREADY,
        f"**{issue['game']}** is in the [game compatibility list](https://github.com/{owner}/{name}/wiki/"
        f"Game-compatibility) already, from {whose}:",
        "",
    ]
    for r in shown[:5]:
        about = [f"PC5 {r['pc5']}" if r["pc5"] else "", r["edition"], r["controller"]]
        lines.append(f"- {label(r['shown'])}: " + " · ".join(cell(a, 60) for a in about if a)
                     + (f". Settings that helped: {cell(r['settings'], 400)}" if r["settings"] else "")
                     + (f". Notes: {cell(r['notes'], 300)}" if r["notes"] else "")
                     + f" ([#{r['number']}]({r['url']}) by {r['author']})")
    lines += ["", "What helped there may help here too. Please say here if it does; if not, this issue goes on with your log."]
    if graphql("""mutation($id: ID!, $body: String!) { addComment(input: {subjectId: $id, body: $body}) {
            commentEdge { node { id } } } }""", fatal=False, id=issue["id"], body="\n".join(lines)) is None:
        return 0
    print(f"compatibility list: issue #{issue['number']} told what {len(shown)} report(s) say")
    return 1


def tell_listed(report: dict, owner: str, name: str) -> int:
    """One comment on a report now in the list, with how it shows there: 1 when made."""
    how = label(report["shown"])
    body = "\n".join([
        LISTED,
        f"Thanks for the report! **{report['game']}** is now in the [game compatibility list]"
        f"(https://github.com/{owner}/{name}/wiki/Game-compatibility) as **{how}**.",
        "",
        "If it runs differently with a newer PC5 or other settings, post a new report, so the list stays up to date.",
    ])
    if graphql("""mutation($id: ID!, $body: String!) { addDiscussionComment(input: {discussionId: $id, body: $body}) {
            comment { id } } }""", fatal=False, id=report["id"], body=body) is None:
        return 0
    print(f"compatibility list: #{report['number']} told it is in the list")
    return 1


def needs_issue(discussion: dict, owner: str, name: str, asked: bool) -> None:
    """A report that a game doesn't run, without an issue: said once, and closed. A failure here
    is a warning: the list is still made."""
    if not asked and not discussion["locked"]:
        body = "\n".join([
            MARK,
            "Thanks for the report! A game that doesn't start or isn't playable is listed by its issue, with its "
            "log, so the cause can be found: it could be the game, its copy, a setting or PC5. This report is "
            "closed.",
            "",
            f"Open an [issue](https://github.com/{owner}/{name}/issues/new/choose) and attach the game's zip "
            "from `/data/prospero-win/logs/` (PC5's **Report on GitHub** writes it for you). The game is then "
            "listed as being looked at, then as working perfectly or unresolved, as the issue is closed.",
        ])
        if graphql("""mutation($id: ID!, $body: String!) { addDiscussionComment(input: {discussionId: $id, body: $body}) {
                comment { id } } }""", fatal=False, id=discussion["id"], body=body) is not None:
            print(f"compatibility list: #{discussion['number']} has no issue: asked for one")
    if not discussion["closed"] and graphql(
            """mutation($id: ID!) { closeDiscussion(input: {discussionId: $id, reason: OUTDATED}) { discussion { id } } }""",
            fatal=False, id=discussion["id"]) is not None:
        print(f"compatibility list: #{discussion['number']} has no issue: closed")


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
