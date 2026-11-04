#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""A first look at the logs players attach to an issue, and the logs kept where the maintainer
can fetch them.

    issue_logs.py fetch DIR   (the workflow then keeps DIR's folders as artifacts)
    issue_logs.py check DIR

- fetch: an issue opened or edited, or a comment on one (.github/workflows/issue-logs.yml), is
  read for the zips attached to it: GitHub's attachment links
  (https://github.com/user-attachments/files/<id>/<name>.zip, or the older
  https://github.com/<owner>/<repo>/files/<id>/<name>.zip). A zip that a first look already
  answered (its link is in one of this script's comments) is left out, so editing an issue
  doesn't answer it again. Each new zip is downloaded and unpacked into
  DIR/issue-<number>-<comment id, or "body">/<zip name>/, with caps on its size, its files' sizes
  and their number, and nothing outside that folder. The workflow keeps that folder as the
  run's artifact of the same name, for 90 days: the maintainer's tools fetch it from there
  (repos/<owner>/<repo>/actions/artifacts?name=...), so a log never needs its link copied.
  The names go to the workflow as its "artifacts" output (GITHUB_OUTPUT).
- check: each log unpacked is read, and the issue gets one comment a zip saying what stands
  out in it, in plain words, with labels for the maintainer (made the first time):
  - "log: copy's own files": the game loaded, from its own folder, OnlineFix64.dll /
    OnlineFix.dll, another Steam library beside Steam's (steam_api_o.dll, steam_api64_o.dll),
    a Steam client (steamclient.dll, steamclient64.dll) or anadius32.dll / anadius64.dll, or
    set its Steam app id to 480: files that aren't the game's as its store ships it, the likely
    reason it stops.
  - "log: installer": the game tried to run an installer (vc_redist, DirectX's, PhysX's...),
    which can't run on the console ("is a helper: not handed off"). An Unreal game's starter
    running Engine\\Extras\\Redist's vc_redist is said to be one PC5 2.3.0 starts past.
  - "log: OpenGL": OpenGL calls made with no OpenGL context ("called without a rendering
    context", with MESA_DEBUG on), or wglCreateContext answering 0 (a +snoop log).
  - "log: no end line": the log has no session_end line: closed with the PS button, or the
    console stopped while the game ran.
  - "log: picture stopped": PC5's frame counter (the pw_pc5_hud lines, with the FPS counter
    on) at 0 for 30 seconds or more after the game had drawn, while its sound went on. Without
    the FPS counter the log can't tell: PC5's own frame counts stop once the game's picture
    goes to the screen by itself.
  An Unreal game's crash report (CrashContext.runtime-xml among the game's files) has its
  error message quoted, and the zip's about.txt gives the program and what PC5 saw.

  The comments start with <!-- pc5-log-check ... -->, which names the zip's link and its
  artifact. They are a bot's (github-actions), so no_reply.py doesn't count them as the
  maintainer's answer. These labels aren't any compatibility_list.py reads.

Run with GITHUB_TOKEN, GITHUB_REPOSITORY, GITHUB_EVENT_NAME, GITHUB_EVENT_PATH and, for the
links back to the run, GITHUB_RUN_ID and GITHUB_SERVER_URL. ISSUE_LOGS_DRY_RUN=1 prints the
comments instead of posting them.
"""
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

MARK = "<!-- pc5-log-check"
ZIP_LINK = re.compile(r"https://github\.com/(?:user-attachments/files|[\w.-]+/[\w.-]+/files)/\d+/[^\s)\]\"'<>]+?\.zip",
                      re.I)
MAX_ZIP = 200 << 20          # a zip of more is left out
MAX_FILE = 300 << 20         # a file unpacked
MAX_TOTAL = 600 << 20        # all of one zip's files
MAX_FILES = 2000
DRY_RUN = os.environ.get("ISSUE_LOGS_DRY_RUN") == "1"
LABELS = {
    "copy": ("log: copy's own files", "d93f0b", "The log: the copy has files that aren't the game's as its store ships it"),
    "installer": ("log: installer", "fbca04", "The log: the game tried to run an installer, which can't run on the console"),
    "opengl": ("log: OpenGL", "1d76db", "The log: OpenGL calls with no OpenGL context"),
    "no_end": ("log: no end line", "c5def5", "The log: no end line (the PS button, or the console stopped)"),
    "picture": ("log: picture stopped", "5319e7", "The log: no new frames for 30 s while the sound went on"),
}
INSTALLERS = re.compile(r"vc_?redist|vcredist|dxsetup|dxwebsetup|directx|physx|oalinst|dotnet|ndp\d|redist|setup|install",
                        re.I)


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
    page = 1
    while True:
        items = api("GET", f"{path}{'&' if '?' in path else '?'}per_page=100&page={page}")
        yield from items
        if len(items) < 100:
            return
        page += 1


def is_bot(user) -> bool:
    return not user or user.get("type") == "Bot" or user.get("login", "").endswith("[bot]")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def download(link: str) -> bytes:
    """The attachment's bytes. GitHub answers its link with a redirect to a signed address on
    another host, which is fetched with no header of ours (it carries its own signature)."""
    opener = urllib.request.build_opener(NoRedirect)
    url = link
    for _ in range(5):
        try:
            with opener.open(urllib.request.Request(url, headers={"User-Agent": "pc5-issue-logs"}), timeout=120) as response:
                data = response.read(MAX_ZIP + 1)
                if len(data) > MAX_ZIP:
                    raise ValueError(f"more than {MAX_ZIP >> 20} MB")
                return data
        except urllib.error.HTTPError as error:
            if error.code in (301, 302, 303, 307, 308) and error.headers.get("Location"):
                url = urllib.parse.urljoin(url, error.headers["Location"])
                continue
            raise
    raise ValueError("too many redirects")


def unpack(data: bytes, into: Path) -> int:
    """The zip's files under into, none outside it, within the caps. How many."""
    import io
    count, total = 0, 0
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            parts = PurePosixPath(info.filename.replace("\\", "/")).parts
            if not parts or info.filename.startswith(("/", "\\")) or any(p in ("", ".", "..") or ":" in p for p in parts):
                continue
            if count >= MAX_FILES or info.file_size > MAX_FILE or total + info.file_size > MAX_TOTAL:
                continue
            target = into.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info) as source, target.open("wb") as out:
                left = MAX_FILE
                while chunk := source.read(1 << 20):
                    left -= len(chunk)
                    if left < 0:
                        break
                    out.write(chunk)
            count += 1
            total += info.file_size
    return count


def event_text() -> tuple:
    """(issue, where: "body" or the comment's id, its text, its author) for the event, or None."""
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    issue = event.get("issue") or {}
    # The weekly summary (weekly_summary.py) is about no game: nothing to look at there.
    if not issue or "pull_request" in issue or any((label.get("name") or "").casefold() == "summary"
                                                   for label in issue.get("labels", [])):
        return None
    if os.environ.get("GITHUB_EVENT_NAME") == "issue_comment":
        comment = event.get("comment") or {}
        return issue, str(comment.get("id", "")), comment.get("body") or "", comment.get("user")
    return issue, "body", issue.get("body") or "", issue.get("user")


def answered(repo: str, number: int) -> str:
    """This script's comments on the issue, run together: a zip named there has had its look."""
    return "\n".join(c.get("body") or "" for c in pages(f"repos/{repo}/issues/{number}/comments")
                     if MARK in (c.get("body") or ""))


def zip_name(link: str) -> str:
    return re.sub(r"[^\w.-]", "_", urllib.parse.unquote(link.rsplit("/", 1)[-1]))[:120]


def fetch(out: Path) -> int:
    repo = os.environ["GITHUB_REPOSITORY"]
    found = event_text()
    names = []
    if found:
        issue, where, text, user = found
        links = list(dict.fromkeys(ZIP_LINK.findall(text)))
        if links and not is_bot(user):
            done = answered(repo, issue["number"])
            artifact = f"issue-{issue['number']}-{where}"
            for link in links:
                if link in done:
                    continue
                folder = out / artifact / zip_name(link)
                try:
                    count = unpack(download(link), folder)
                except (urllib.error.URLError, zipfile.BadZipFile, ValueError, OSError) as error:
                    print(f"::warning::issue logs: #{issue['number']} {link}: {error}")
                    folder.mkdir(parents=True, exist_ok=True)
                    (folder / "fetch-error.txt").write_text(f"{link}\n{error}\n")
                    continue
                (folder / "link.txt").write_text(link + "\n")
                print(f"issue logs: #{issue['number']} {zip_name(link)}: {count} files")
            if (out / artifact).is_dir():
                names.append(artifact)
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write(f"artifacts={' '.join(names)}\n")
    return 0


# ---- what stands out ----------------------------------------------------------------------

ALIVE = re.compile(r"t=([\d.]+) PW_WINE64 alive .*?\baudio=(\d+)")
HUD = re.compile(r"t=([\d.]+) WINESERVER pw_pc5_hud: second fps=(\d+)")
COPY_FILES = re.compile(r'Loaded L"C:\\\\Games\\\\[^"]*?\\\\(OnlineFix(?:64)?\.dll|steam_api(?:64)?_o\.dll|'
                        r'steamclient(?:64)?\.dll|anadius(?:32|64)\.dll)"', re.I)
APP_ID_480 = re.compile(r'SetEnvironmentVariableW \(L"Steam(?:App|Game)I[dD]" L"480"\)')
HELPER = re.compile(r"wine-ps5: (.+?) is a helper: not handed off")
NO_CONTEXT = re.compile(r"called without a rendering context|RET  opengl32\.wglCreateContext\([^)]*\) retval=00000000")


def about_lines(folder: Path) -> dict:
    """about.txt's program, what PC5 saw, and whether it says the log has no end line."""
    about = folder / "about.txt"
    text = about.read_text(errors="replace") if about.is_file() else ""
    get = lambda label: next((m.group(1).strip() for m in [re.search(rf"^{label}:\s*(.+)$", text, re.M)] if m), "")
    return {"program": get("Program"), "saw": get("What PC5 saw"), "no_end": "has no end line" in text}


def crash_message(folder: Path) -> str:
    """An Unreal crash report's error among the game's files, "" when there is none."""
    for report in sorted(folder.glob("game/**/CrashContext.runtime-xml")):
        found = re.search(r"<ErrorMessage>(.*?)</ErrorMessage>", report.read_text(errors="replace"), re.S)
        if found and found.group(1).strip():
            return " ".join(found.group(1).split())[:300]
    return ""


def picture_stopped(text: str) -> bool:
    """With the FPS counter on: 0 frames a second for 30 seconds or more after the game drew,
    while its sound went on (the alive lines' audio count rising)."""
    seconds = [(float(t), int(fps)) for t, fps in HUD.findall(text)]
    audio = [(float(t), int(a)) for t, a in ALIVE.findall(text)]
    drawn, since = False, None
    for t, fps in seconds:
        if fps > 0:
            drawn, since = True, None
            continue
        if not drawn:
            continue
        since = t if since is None else since
        if t - since >= 30:
            heard = [a for at, a in audio if since <= at <= t]
            if len(heard) >= 2 and heard[-1] > heard[0]:
                return True
    return False


def look(folder: Path) -> tuple:
    """(findings as lines of the comment, label keys) for one unpacked zip."""
    logs = sorted(p for p in folder.glob("*.log"))
    main = next((p for p in logs if not re.search(r"-(?:earlier|start|crash)\.log$", p.name)), logs[0] if logs else None)
    text = "\n".join(p.read_text(errors="replace") for p in logs)
    about = about_lines(folder)
    lines, labels = [], []
    copies = sorted({m.lower() for m in COPY_FILES.findall(text)})
    if copies or APP_ID_480.search(text):
        what = ", ".join(f"`{c}`" for c in copies) if copies else "a Steam app id of 480 (Spacewar)"
        lines.append(f"- The game loaded {what}. That isn't part of the game as its store ships it: files like it "
                     "replace the game's own Steam or online files, and they are the likely reason it stops. If you can, "
                     "try a copy as the store ships it and attach that zip.")
        labels.append("copy")
    helpers = [h for h in dict.fromkeys(HELPER.findall(text)) if INSTALLERS.search(h)]
    if helpers:
        name = helpers[0].replace("\\", "/").rsplit("/", 1)[-1]
        if any("\\engine\\extras\\redist\\" in h.lower() for h in helpers):
            lines.append(f"- The program started is an Unreal Engine game's small starter: it tried to run `{name}` "
                         "(the Visual C++ installer), which can't run on the console, and then closed. PC5 already has "
                         "Visual C++, and from PC5 2.3.0 it starts the game's own program in the starter's place. Before "
                         "that, pick the game's `-Win64-Shipping.exe` in **△ › Manage › Program**.")
        else:
            lines.append(f"- The game tried to run `{name}`, an installer, which can't run on the console. What it installs "
                         "is usually in PC5 already, so the game may run without it.")
        labels.append("installer")
    if NO_CONTEXT.search(text):
        lines.append("- The game's OpenGL drawing never got started: every drawing call had nowhere to go.")
        labels.append("opengl")
    if (main and "session_end" not in main.read_text(errors="replace")) or about["no_end"]:
        lines.append("- The log stops without an end line: the game was closed with the PS button, or the console "
                     "stopped while it ran. What was on screen then? A photo helps if there was a message.")
        labels.append("no_end")
    if picture_stopped(text):
        lines.append("- The picture stopped (no new frames for 30 seconds or more) while the game's sound went on.")
        labels.append("picture")
    crash = crash_message(folder)
    if crash:
        lines.append(f"- The game's own crash report says: `{crash}`")
    return lines, labels, about, main


def label_ready(repo: str, key: str) -> None:
    name, color, about = LABELS[key]
    try:
        api("GET", f"repos/{repo}/labels/{urllib.parse.quote(name)}")
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        api("POST", f"repos/{repo}/labels", {"name": name, "color": color, "description": about[:100]})


def check(out: Path) -> int:
    repo = os.environ["GITHUB_REPOSITORY"]
    run = f"{os.environ.get('GITHUB_SERVER_URL', 'https://github.com')}/{repo}/actions/runs/{os.environ.get('GITHUB_RUN_ID', '')}"
    for artifact in sorted(p for p in out.iterdir() if p.is_dir()) if out.is_dir() else []:
        number = int(re.match(r"issue-(\d+)-", artifact.name).group(1))
        for folder in sorted(p for p in artifact.iterdir() if p.is_dir()):
            link = (folder / "link.txt").read_text().strip() if (folder / "link.txt").is_file() else ""
            if not link:
                continue   # it couldn't be fetched: the warning says why
            lines, labels, about, main = look(folder)
            head = [f"{MARK} zip={link} artifact={artifact.name} -->",
                    f"**A first look at `{folder.name}`**, made automatically: the maintainer reads the log too.", ""]
            if about["program"]:
                head.append(f"- Program: `{about['program']}`")
            if about["saw"]:
                head.append(f"- What PC5 saw: {about['saw']}")
            if not main:
                lines = ["- There's no PC5 log in this zip. Please attach the game's zip from `/data/prospero-win/logs/` "
                         "as it is."]
            elif not lines:
                lines = ["- Nothing PC5 knows of stands out in this log."]
            body = "\n".join(head + ([""] if len(head) > 3 else []) + lines +
                             ["", f"<!-- pc5-log-check-artifact {artifact.name} {run} -->"])
            print(f"issue logs: #{number} {folder.name}: {', '.join(labels) or 'nothing stands out'}"
                  f"{' (dry run)' if DRY_RUN else ''}")
            if DRY_RUN:
                print(body)
                continue
            for key in labels:
                label_ready(repo, key)
            api("POST", f"repos/{repo}/issues/{number}/comments", {"body": body})
            if labels:
                api("POST", f"repos/{repo}/issues/{number}/labels", {"labels": [LABELS[k][0] for k in labels]})
    return 0


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in ("fetch", "check"):
        sys.exit("usage: issue_logs.py fetch|check DIR")
    out = Path(sys.argv[2])
    return fetch(out) if sys.argv[1] == "fetch" else check(out)


if __name__ == "__main__":
    sys.exit(main())
