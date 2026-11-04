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
import issue_logs   # noqa: E402
import no_reply     # noqa: E402
import release_replies   # noqa: E402

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


def main() -> int:
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        test_links()
        test_unpack(root)
        test_look(root / "look")
        test_replies(root)
    test_no_reply()
    print("GitHub scripts passed: attachment links, safe unpacking, the copy's own files, installers and Unreal's "
          "starter, OpenGL without a context, no end line, the picture stopped, crash reports, waiting replies, "
          "a release's reply as the maintainer's, opening a closed issue asked to try again")
    return 0


if __name__ == "__main__":
    sys.exit(main())
