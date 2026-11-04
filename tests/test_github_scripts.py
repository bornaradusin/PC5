#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The issue Actions' scripts without GitHub (.github/scripts): issue_logs.py's links, its safe
unpacking and what it finds in a log; release_replies.py's waiting replies; no_reply.py taking a
release's reply as the maintainer's and opening a closed issue asked to try again."""
import io
import json
import os
import sys
import tempfile
import zipfile
from pathlib import Path

os.environ.setdefault("GITHUB_TOKEN", "test")
sys.dont_write_bytecode = True   # no __pycache__ beside the scripts
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / ".github" / "scripts"))
import compatibility_list   # noqa: E402
import issue_logs   # noqa: E402
import no_reply     # noqa: E402
import related_issues   # noqa: E402
import release_replies   # noqa: E402
import weekly_summary   # noqa: E402

ALIVE = ("REC seq=1 t={t} PW_WINE64 alive tick=60 sink_calls=1 frames_put=5 shown=1 rejected=0 last=0x0 inputs=0 "
         "refused=0 vk_shown=0 vk_busy=0 vk_kept=0 vk_held=0 cpu_ms=1 audio={a} audible=0 peak=0\n")
HUD = "REC seq=2 t={t} WINESERVER pw_pc5_hud: second fps={fps} frame_us=16612 gpu=1 cpu=-1 busiest_cpu=-1\n"
END = "REC seq=9 t=99.0 PW_WINE64 session_end reason=wine-exit\n"


def folder_with(root: Path, name: str, log: str, about: str = "", files: dict = None) -> Path:
    folder = root / name
    folder.mkdir(parents=True)
    (folder / f"log-{name}.log").write_text(log)
    if about:
        (folder / "about.txt").write_text(about)
    for path, text in (files or {}).items():
        (folder / path).parent.mkdir(parents=True, exist_ok=True)
        (folder / path).write_text(text)
    return folder


def test_links() -> None:
    text = ("[log.zip](https://github.com/user-attachments/files/34370016/log-2026-11-04_12-05_rv-there-yet.zip) and "
            "https://github.com/bornaradusin/PC5/files/123/old.zip, not https://example.com/a.zip or a.png")
    found = issue_logs.ZIP_LINK.findall(text)
    assert found == ["https://github.com/user-attachments/files/34370016/log-2026-11-04_12-05_rv-there-yet.zip",
                     "https://github.com/bornaradusin/PC5/files/123/old.zip"], found
    assert issue_logs.zip_name(found[0]) == "log-2026-11-04_12-05_rv-there-yet.zip"


def test_unpack(root: Path) -> None:
    data = io.BytesIO()
    with zipfile.ZipFile(data, "w") as archive:
        archive.writestr("about.txt", "Program: C:\\Games\\X\\x.exe\n")
        archive.writestr("game/users/a/b.txt", "kept")
        archive.writestr("../outside.txt", "no")
        archive.writestr("/absolute.txt", "no")
        archive.writestr("C:/drive.txt", "no")
    into = root / "unpacked"
    assert issue_logs.unpack(data.getvalue(), into) == 2, "only the files inside"
    assert (into / "about.txt").is_file() and (into / "game/users/a/b.txt").read_text() == "kept"
    assert not (root / "outside.txt").exists() and not list(into.rglob("*drive*")), "nothing outside"


def test_look(root: Path) -> None:
    # The copy's own Steam files, and app id 480.
    folder = folder_with(root, "avp", 'REC t=1 WINE 0024:trace:loaddll:build_module Loaded L"C:\\\\Games\\\\Aliens vs '
                         'Predator\\\\steam_api_o.dll" at 3B400000: native\n' + END,
                         "Program: C:\\Games\\Aliens vs Predator\\AvP.exe\nWhat PC5 saw: closed before showing anything\n")
    lines, labels, about, main = issue_logs.look(folder)
    assert labels == ["copy"] and "`steam_api_o.dll`" in lines[0], (labels, lines)
    assert about == {"program": "C:\\Games\\Aliens vs Predator\\AvP.exe", "saw": "closed before showing anything",
                     "no_end": False}, about
    folder = folder_with(root, "app480", 'REC t=1 WINE 0024:trace:process:SetEnvironmentVariableW (L"SteamAppID" L"480")\n'
                         + END)
    assert issue_logs.look(folder)[1] == ["copy"], "app id 480"
    # Wine's own Steam library, or the game's steam_api64.dll, is no copy's file.
    folder = folder_with(root, "own", 'REC t=1 WINE Loaded L"C:\\\\Games\\\\G\\\\steam_api64.dll" at 1: native\n' + END)
    assert issue_logs.look(folder)[1] == [], "the game's own Steam library"
    # An Unreal starter running Visual C++'s installer.
    folder = folder_with(root, "rv", "REC t=3604.8 WINESERVER wine-ps5: C:\\Games\\RV There Yet\\Engine\\Extras\\Redist\\"
                         "en-us\\vc_redist.x64.exe is a helper: not handed off\n" + END)
    lines, labels, _, _ = issue_logs.look(folder)
    assert labels == ["installer"] and "Unreal" in lines[0] and "vc_redist.x64.exe" in lines[0], (labels, lines)
    folder = folder_with(root, "dx", "REC t=1 WINESERVER wine-ps5: C:\\Games\\G\\DXSETUP.exe is a helper: not handed off\n"
                         "REC t=2 WINESERVER wine-ps5: C:\\Games\\G\\Launcher.exe is a helper: not handed off\n" + END)
    lines, labels, _, _ = issue_logs.look(folder)
    assert labels == ["installer"] and "DXSETUP.exe" in lines[0] and "Unreal" not in lines[0], lines
    folder = folder_with(root, "helper", "REC t=2 WINESERVER wine-ps5: C:\\Games\\G\\Launcher.exe is a helper: not handed off\n"
                         + END)
    assert issue_logs.look(folder)[1] == [], "a helper that isn't an installer"
    # OpenGL without a context, as Mesa's debug says it or as +snoop shows it.
    folder = folder_with(root, "gl", "REC t=1 WINESERVER Mesa: error: GL User Error: glClear called without a rendering "
                         "context\n" + END)
    assert issue_logs.look(folder)[1] == ["opengl"]
    folder = folder_with(root, "snoop", "REC t=1 WINE 0024:RET  opengl32.wglCreateContext(0f010045) retval=00000000 "
                         "ret=00496a69\n" + END)
    assert issue_logs.look(folder)[1] == ["opengl"]
    # No end line, in the log or as about.txt says it.
    folder = folder_with(root, "noend", ALIVE.format(t=1, a=1))
    assert issue_logs.look(folder)[1] == ["no_end"]
    folder = folder_with(root, "noend2", END, "Its log has no end line: it was closed with the PS button.\n")
    assert issue_logs.look(folder)[1] == ["no_end"]
    # The picture stopped for 30 s with the sound going on, after the game drew.
    log = "".join(HUD.format(t=t, fps=60) + ALIVE.format(t=t, a=t * 100) for t in range(10, 20))
    log += "".join(HUD.format(t=t, fps=0) + ALIVE.format(t=t, a=t * 100) for t in range(20, 55))
    assert issue_logs.look(folder_with(root, "stopped", log + END))[1] == ["picture"]
    silent = log.replace("audio=", "audio=0 x=")
    assert issue_logs.look(folder_with(root, "silent", silent + END))[1] == [], "no sound: not that"
    short = "".join(HUD.format(t=t, fps=0 if t > 15 else 60) + ALIVE.format(t=t, a=t) for t in range(10, 30))
    assert issue_logs.look(folder_with(root, "short", short + END))[1] == [], "under 30 s"
    loading = "".join(HUD.format(t=t, fps=0) + ALIVE.format(t=t, a=t) for t in range(10, 60))
    assert issue_logs.look(folder_with(root, "loading", loading + END))[1] == [], "never drew yet"
    # An Unreal crash report's message.
    folder = folder_with(root, "ue", END, files={"game/users/p/AppData/Local/G/Saved/Crashes/UECC/CrashContext.runtime-xml":
                         "<FGenericCrashContext><ErrorMessage>Unhandled Exception: EXCEPTION_ACCESS_VIOLATION writing "
                         "address 0x0</ErrorMessage></FGenericCrashContext>"})
    lines, labels, _, _ = issue_logs.look(folder)
    assert labels == [] and "EXCEPTION_ACCESS_VIOLATION writing address 0x0" in lines[0], lines


def test_replies(root: Path) -> None:
    replies = root / "replies"
    replies.mkdir()
    (replies / "README.md").write_text("version: 2.3.0\n\nnot a reply\n")
    (replies / "433.md").write_text("version: 2.3.0\n\nThanks for the log!\n\nPlease try 2.3.0.\n")
    (replies / "239.md").write_text("version: v2.3.0\n\nThe OpenGL fix.\n")
    (replies / "100.md").write_text("version: 2.3.1\n\nLater.\n")
    (replies / "101.md").write_text("no version line\n")
    release_replies.REPLIES = replies
    found = release_replies.waiting("2.3.0")
    assert [(n, b) for n, _, b in found] == [(239, "The OpenGL fix."), (433, "Thanks for the log!\n\nPlease try 2.3.0.")], found
    assert release_replies.waiting("2.4.0") == []


def test_no_reply() -> None:
    owner, bot = "kevin", {"login": "github-actions[bot]", "type": "Bot"}
    reply = {"user": bot, "body": "<!-- pc5-release-reply 2.3.0 -->\nFixed."}
    assert no_reply.speaker(reply, owner) == owner, "a release's reply is the maintainer's"
    assert no_reply.speaker({"user": bot, "body": "<!-- pc5-log-check zip=x -->"}, owner) == "github-actions[bot]"
    assert no_reply.speaker({"user": {"login": "player"}, "body": "hi"}, owner) == "player"
    # A comment on an issue closed as not planned opens it again only when a release asked to try again.
    calls = []
    comments = {7: [{"user": bot, "body": "<!-- pc5-retest 2.3.0 -->\nTry again."}], 8: []}
    no_reply.pages = lambda path: iter(comments[int(path.split("/")[-2])])
    no_reply.api = lambda method, path, body=None: calls.append((method, path, body))
    event = lambda n, labels: {"issue": {"number": n, "state": "closed", "state_reason": "not_planned",
                                         "labels": [{"name": l} for l in labels]},
                               "comment": {"user": {"login": "player"}}}
    assert no_reply.reopen("o/r", owner, event(7, [])) == 1 and calls == [("PATCH", "repos/o/r/issues/7", {"state": "open"})]
    calls.clear()
    assert no_reply.reopen("o/r", owner, event(8, [])) == 0 and not calls, "closed as not planned, not asked"
    assert no_reply.reopen("o/r", owner, event(8, ["needs testing"])) == 1 and calls[0][0] == "DELETE", "for no reply"


def issue(number: int, game: str, state: str = "open", reason=None, labels=(), title: str = "", when: str = "") -> dict:
    return {"number": number, "title": title or f"{game}: closed before showing anything", "state": state,
            "state_reason": reason, "labels": [{"name": l} for l in labels], "html_url": f"https://x/issues/{number}",
            "body": f"### Game\n\n{game}\n\n### PC5 version\n\n2.2.9\n", "closed_at": when or None,
            "user": {"login": "player"}}


def test_related() -> None:
    words = related_issues.state_words
    assert words(issue(1, "G")) == "open" and words(issue(1, "G", labels=["fix ready"])) == "open, a fix out to test"
    assert words(issue(1, "G", "closed", "completed")) == "fixed"
    assert words(issue(1, "G", "closed", "not_planned")) == "doesn't run"
    assert words(issue(1, "G", "closed", "not_planned", ["needs testing"])) == "closed for no reply"
    assert words(issue(1, "G", "closed", "not_planned", ["anti-cheat"])) == "anti-cheat: no copy runs"
    assert words(issue(1, "G", "closed", "not_planned", ["needs store app"])) == "that copy needs its store's app"
    assert words(issue(1, "G", "closed", "completed", ["playable with issues"])) == "runs, with something to set or still wrong"
    all_issues = [issue(50, "Baldur's Gate II"), issue(40, "baldurs gate 2", "closed", "completed"),
                  issue(30, "Baldur's Gate II", "closed", "duplicate"), issue(20, "Other Game", "closed", "completed"),
                  issue(10, "Baldur's Gate II", labels=["summary"]), dict(issue(9, "Baldur's Gate II"), pull_request={})]
    comments = {40: [{"user": {"login": "kevin"}, "body": "Fixed.\nList settings: Graphics mode Alternative\nList notes: Runs well"},
                     {"user": {"login": "player"}, "body": "List settings: not mine"}]}

    def pages(path):
        if path.endswith("/comments"):
            return iter(comments.get(int(path.split("/")[-2]), []))
        return iter(all_issues)
    related_issues.pages = pages
    new = issue(60, "Baldur's Gate 2")
    found = related_issues.related("o/r", new, "kevin", {})
    assert [(o["number"], w) for o, w, _ in found] == [(50, "open"), (40, "fixed")], found
    assert found[1][2] == {"settings": "Graphics mode Alternative", "notes": "Runs well"}, found[1][2]
    text = related_issues.comment("Baldur's Gate 2", found)
    assert text.startswith(related_issues.MARK) and "[#40](https://x/issues/40)" in text and \
        "What helped: Graphics mode Alternative. Runs well" in text and "#30" not in text and "#20" not in text, text
    assert related_issues.related("o/r", {"number": 61, "body": "no game here"}, "kevin", {}) == [], "an issue about no game"


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
    assert compatibility_list.SUMMARY_LABEL in release_replies.LEFT_ALONE


def test_weekly(root: Path) -> None:
    from datetime import datetime, timezone
    now = datetime(2026, 11, 9, 6, 17, tzinfo=timezone.utc)
    owner, bot = "kevin", {"login": "github-actions[bot]", "type": "Bot"}
    open_issues = [issue(1, "A"), issue(2, "B"), issue(3, "C"), issue(4, "D", labels=["in progress"])]
    comments = {1: [], 2: [{"user": {"login": "kevin"}, "body": "Try 2.3.0", "created_at": "2026-11-08T06:17:00Z"},
                           {"user": bot, "body": "<!-- pc5-log-check -->", "created_at": "2026-11-08T07:00:00Z"}],
                3: [{"user": bot, "body": "<!-- pc5-release-reply 2.3.0 -->\nFixed.", "created_at": "2026-11-06T06:17:00Z"}],
                4: [{"user": {"login": "kevin"}, "body": "On it", "created_at": "2026-11-01T00:00:00Z"}]}
    assert weekly_summary.waiting(open_issues[0], comments[1], owner, now) == ("maintainer", None)
    who, left = weekly_summary.waiting(open_issues[1], comments[2], owner, now)
    assert who == "reporter" and abs(left - 1.0) < 0.01, left
    assert weekly_summary.waiting(open_issues[2], comments[3], owner, now) == ("reporter", 0.0), "the release's reply"
    assert weekly_summary.waiting(open_issues[3], comments[4], owner, now) == ("reporter", None), "kept open"
    replies = root / "weekly-replies"
    replies.mkdir()
    (replies / "433.md").write_text("version: 2.3.0\n\nThanks!\n")
    (replies / "README.md").write_text("x")
    weekly_summary.REPLIES = replies
    assert weekly_summary.replies_waiting() == [(433, "2.3.0")]
    closed = [issue(7, "E", "closed", "completed", when="2026-11-05T00:00:00Z")]
    text = weekly_summary.page("o/r", owner, now, open_issues, lambda i: comments[i["number"]], closed,
                               weekly_summary.replies_waiting(), [("Hollow Bay", "Works perfectly", "https://x/d/1")])
    assert "1 waiting on you, 3 waiting on their reporter" in text, text
    assert "[#1](https://x/issues/1)" in text.split("### Waiting on their reporter")[0], text
    assert "closes for no reply in 1.0 days" in text and "at the next run" in text and ": kept open" in text, text
    assert "[#7](https://x/issues/7) E: closed before showing anything: fixed" in text, text
    assert "[#433](https://github.com/o/r/issues/433): 2.3.0" in text and "[Hollow Bay](https://x/d/1): Works perfectly" in text
    assert "New game compatibility reports" not in weekly_summary.page("o/r", owner, now, [], lambda i: [], [], [], None)


def main() -> int:
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        test_links()
        test_unpack(root)
        test_look(root / "look")
        test_replies(root)
        test_weekly(root)
    test_no_reply()
    test_related()
    test_summary_left_out()
    print("GitHub scripts passed: attachment links, safe unpacking, the copy's own files, installers and Unreal's "
          "starter, OpenGL without a context, no end line, the picture stopped, crash reports, waiting replies, "
          "a release's reply as the maintainer's, opening a closed issue asked to try again, an issue's related "
          "issues, the weekly summary, the summary issue left out of the list")
    return 0


if __name__ == "__main__":
    sys.exit(main())
