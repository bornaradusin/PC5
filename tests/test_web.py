#!/usr/bin/env python3
"""PC5's browser page (title/pc5_web.c) as a browser sees it: Python's urllib and raw sockets
against web-serve (tests/pc5_ftp_host.c), on a copy of the library tests/test_pc5.c installed.
Usage: test_web.py <web-serve> <work dir> <prospero-win root with games>."""
import base64
import glob
import json
import os
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Server:
    def __init__(self, serve, root, install, login=None):
        self.port = free_port()
        args = [serve, root, install, str(self.port)] + (list(login) if login else [])
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        assert self.process.stdout.readline().strip() == "ready"
        self.base = "http://127.0.0.1:%d/" % self.port

    def counts(self):
        self.process.stdin.write("\n")
        self.process.stdin.flush()
        return dict(part.split("=") for part in self.process.stdout.readline().split())

    def stop(self):
        self.process.stdin.close()
        line = self.process.stdout.readline().strip()
        self.process.wait(timeout=10)
        return line


def request(base, path, method="GET", body=None, auth=None):
    req = urllib.request.Request(base + path, data=body, method=method)
    if auth:
        req.add_header("Authorization", "Basic " + base64.b64encode(("%s:%s" % auth).encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, r.read(), r.headers
    except urllib.error.HTTPError as e:
        return e.code, e.read(), e.headers


def raw(port, data):
    """The answer's status code to bytes sent as they are (a path urllib would tidy)."""
    with socket.create_connection(("127.0.0.1", port), timeout=10) as s:
        s.sendall(data)
        answer = b""
        while b"\r\n" not in answer:
            more = s.recv(4096)
            if not more:
                break
            answer += more
    return int(answer.split(b" ")[1])


def main():
    serve, work, games_root = sys.argv[1], sys.argv[2], sys.argv[3]
    root = os.path.join(work, "web-root")
    install = os.path.join(work, "web-install")
    shutil.rmtree(root, ignore_errors=True)
    shutil.rmtree(install, ignore_errors=True)
    shutil.copytree(games_root, root, symlinks=True)
    os.makedirs(install)
    os.makedirs(os.path.join(root, "logs"), exist_ok=True)
    with open(os.path.join(root, "logs", "issue-2026-10-30_10-00_old.zip"), "wb") as f:
        f.write(b"PK old")
    with open(os.path.join(root, "logs", "session-3.log"), "w") as f:
        f.write("not a kept log")
    newer = os.path.join(root, "logs", "log-2026-10-30_12-00_new.zip")
    with open(newer, "wb") as f:
        f.write(b"PK newer one")
    os.utime(newer, (time.time() + 60, time.time() + 60))
    with open(os.path.join(work, "web-outside.zip"), "w") as f:
        f.write("not for browsers")
    checks = 0

    # Without a username or password: open to anyone on the network.
    server = Server(serve, root, install)
    try:
        status, page, headers = request(server.base, "")
        assert status == 200 and headers["Content-Type"].startswith("text/html"), (status, headers)
        page = page.decode()
        assert page.startswith("<!doctype html>") and "Send a game" in page and "webkitdirectory" in page
        assert "…" in page and page.rstrip().endswith("</html>")
        assert 'data-i18n>Send a game<' in page and 'fetch("lang.json"' in page
        checks += 2

        # The page's text in PC5's language (English here: no language in use).
        status, body, headers = request(server.base, "lang.json")
        assert status == 200 and headers["Content-Type"].startswith("application/json"), (status, headers)
        assert json.loads(body) == {"lang": "en", "strings": {}}, body
        checks += 1

        status, body, _ = request(server.base, "api/status")
        s = json.loads(body)
        assert status == 200 and s["version"] and s["install"] is True and s["free"] > 0, s
        assert s["address"] == "127.0.0.1:%d" % server.port, s
        checks += 2

        # The games, as the library has them; one PC5 installed has its launch.txt to edit.
        status, body, _ = request(server.base, "api/games")
        games = json.loads(body)
        assert status == 200 and games, body
        assert all(g["id"] and g["title"] for g in games)
        editable = [g for g in games if g["editable"]]
        assert editable, games
        game = editable[0]
        checks += 2
        status, body, _ = request(server.base, "api/games/%s/launch" % game["id"], "PUT", b"-windowed -nointro\n")
        assert status == 200, (status, body)
        written = [p for p in glob.glob(os.path.join(root, "prefixes", game["id"], "drive_c", "Games", "**", "launch.txt"),
                                        recursive=True)]
        assert len(written) == 1 and open(written[0]).read() == "-windowed -nointro\n", written
        status, body, _ = request(server.base, "api/games/%s/launch" % game["id"])
        assert status == 200 and body == b"-windowed -nointro\n", body
        status, body, _ = request(server.base, "api/games")
        assert [g for g in json.loads(body) if g["id"] == game["id"]][0]["launch"] == "-windowed -nointro\n"
        # Empty: the file goes.
        status, body, _ = request(server.base, "api/games/%s/launch" % game["id"], "PUT", b"  \n")
        assert status == 200 and not os.path.exists(written[0]), status
        checks += 4
        status, body, _ = request(server.base, "api/games/%s/env" % game["id"], "PUT", b"DXVK_HUD=fps\n")
        env = os.path.join(root, "prefixes", game["id"], "pc5", "env.txt")
        assert status == 200 and open(env).read() == "DXVK_HUD=fps\n", status
        status, body, _ = request(server.base, "api/games/%s/env" % game["id"], "PUT", b"x" * (64 * 1024 + 1))
        assert status == 413 and open(env).read() == "DXVK_HUD=fps\n", status
        # Extra logging for a report: the game's pc5/winedebug.txt.
        status, body, _ = request(server.base, "api/games/%s/debug" % game["id"], "PUT", b"err+all,+seh\n")
        debug = os.path.join(root, "prefixes", game["id"], "pc5", "winedebug.txt")
        assert status == 200 and open(debug).read() == "err+all,+seh\n", status
        status, body, _ = request(server.base, "api/games")
        listed = [g for g in json.loads(body) if g["id"] == game["id"]][0]
        assert listed["debug"] == "err+all,+seh\n" and "Graphics API" in listed["settings"], listed
        assert listed["report"] == "" or listed["report"].startswith("https://github.com/bornaradusin/PC5/issues/new?"), listed
        status, body, _ = request(server.base, "api/games/%s/debug" % game["id"], "PUT", b"\n")
        assert status == 200 and not os.path.exists(debug), status
        checks += 3
        # Files: the game's folder and its Windows; a text file read as UTF-8 and written back as it was.
        files = "api/games/%s/files/" % urllib.parse.quote(game["id"])
        top = [p for p in glob.glob(os.path.join(root, "prefixes", game["id"], "**", game["folder"]), recursive=True)
               if os.path.isdir(p)][0]
        with open(os.path.join(top, "settings.ini"), "wb") as f:
            f.write(b"\xff\xfe" + "[Video]\r\nWidth=1280\r\n".encode("utf-16-le"))
        with open(os.path.join(top, "old.cfg"), "wb") as f:
            f.write(b"name=caf\xe9\n")
        with open(os.path.join(top, "data.bin"), "wb") as f:
            f.write(b"MZ\x90\x00\x03\x00\x00\x00")
        os.symlink("/", os.path.join(top, "z"))
        status, body, _ = request(server.base, files + "game")
        listing = json.loads(body)
        names = [e["name"] for e in listing["entries"]]
        assert status == 200 and listing["max"] == 8 * 1024 * 1024 and "settings.ini" in names and "z" not in names, body
        status, body, _ = request(server.base, files + "game/settings.ini")
        doc = json.loads(body)
        assert status == 200 and doc["text"] == "[Video]\nWidth=1280\n" and doc["encoding"] == "UTF-16" and doc["line_ends"] == "CRLF", doc
        status, body, _ = request(server.base, files + "game/settings.ini", "PUT", b"[Video]\nWidth=1920\nHeight=1080\n")
        assert status == 200 and open(os.path.join(top, "settings.ini"), "rb").read() == \
            b"\xff\xfe" + "[Video]\r\nWidth=1920\r\nHeight=1080\r\n".encode("utf-16-le"), (status, body)
        checks += 3
        status, body, _ = request(server.base, files + "game/old.cfg")
        assert status == 200 and json.loads(body)["text"] == "name=caf\u00e9\n", body
        status, body, _ = request(server.base, files + "game/old.cfg", "PUT", "name=\u20ac\n".encode())
        assert status == 400 and open(os.path.join(top, "old.cfg"), "rb").read() == b"name=caf\xe9\n", (status, body)
        status, body, _ = request(server.base, files + "game/old.cfg", "PUT", "name=th\u00e9\n".encode())
        assert status == 200 and open(os.path.join(top, "old.cfg"), "rb").read() == b"name=th\xe9\n", (status, body)
        checks += 3
        assert request(server.base, files + "game/data.bin")[0] == 415
        assert request(server.base, files + "game/z/etc")[0] == 400
        os.remove(os.path.join(top, "z"))   # nothing after this follows it to the whole machine
        assert request(server.base, files + "game/new.txt", "PUT", b"x")[0] == 404
        assert request(server.base, files + "game", "PUT", b"x")[0] == 405
        assert request(server.base, files + "elsewhere")[0] == 404
        assert raw(server.port, ("GET /%sgame/../../pc5/env.txt HTTP/1.1\r\nHost: x\r\n\r\n" % files).encode()) in (400, 404)
        listed = [g for g in json.loads(request(server.base, "api/games")[1]) if g["id"] == game["id"]][0]
        if listed["windows"]:
            assert request(server.base, files + "windows")[0] == 200
        checks += 4
        # Which game it is: a Steam app ID by hand, kept with source "you"; empty reads the folder again.
        status, body, _ = request(server.base, "api/games/%s/steam_appid" % game["id"], "PUT", b"1091500\n")
        identity = os.path.join(root, "prefixes", game["id"], "pc5", "identity.txt")
        assert status == 200 and "steam_appid=1091500\n" in open(identity).read() and "source=you" in open(identity).read(), status
        status, body, _ = request(server.base, "api/games")
        listed = [g for g in json.loads(body) if g["id"] == game["id"]][0]
        assert listed["steam_appid"] == 1091500 and listed["identity_source"] == "you", listed
        assert request(server.base, "api/games/%s/steam_appid" % game["id"], "PUT", b"12ab")[0] == 400
        assert request(server.base, "api/games/%s/steam_appid" % game["id"], "GET")[0] == 405
        status, body, _ = request(server.base, "api/games/%s/steam_appid" % game["id"], "PUT", b"")
        assert status == 200 and not os.path.exists(identity), status
        checks += 3
        assert request(server.base, "api/games/nobody/launch", "PUT", b"-x")[0] == 404
        assert request(server.base, "api/games/..%2F..%2Fetc/launch")[0] == 404
        checks += 4

        # The logs: kept zips only, newest first; one to download, nothing outside the folder.
        status, body, _ = request(server.base, "api/logs")
        logs = [l["name"] for l in json.loads(body)]
        assert logs[0] == "log-2026-10-30_12-00_new.zip" and "issue-2026-10-30_10-00_old.zip" in logs, logs
        assert all(name.endswith(".zip") for name in logs), logs
        status, body, headers = request(server.base, "logs/log-2026-10-30_12-00_new.zip")
        assert status == 200 and body == b"PK newer one", status
        assert 'filename="log-2026-10-30_12-00_new.zip"' in headers["Content-Disposition"], headers
        assert request(server.base, "logs/session-3.log")[0] == 404
        assert raw(server.port, b"GET /logs/../../web-outside.zip HTTP/1.1\r\nHost: x\r\n\r\n") == 404
        assert raw(server.port, b"GET /logs/..%2F..%2Fweb-outside.zip HTTP/1.1\r\nHost: x\r\n\r\n") == 404
        checks += 5

        # A game's folder sent, one PUT a file; paths that leave the folder refused.
        big = os.urandom(3 * 1024 * 1024 + 5)
        for name, data in (("Neon Drift/NeonDrift.exe", b"MZ" + b"\0" * 100), ("Neon Drift/data/level 1.pak", big)):
            status, body, _ = request(server.base, "upload/" + urllib.parse.quote(name), "PUT", data)
            assert status == 201, (status, body)
            with open(os.path.join(install, name), "rb") as f:
                assert f.read() == data
        assert not glob.glob(os.path.join(install, "**", "*.pc5part"), recursive=True)
        assert server.counts() == {"uploads": "2", "transfers": "0"}
        checks += 3
        for bad in ("../outside.txt", "Neon%20Drift/../../outside.txt", "%2E%2E/outside.txt", "a//b", "./a", "a/.", "a\\b",
                    "", "%2Fetc%2Fx"):
            code = raw(server.port, ("PUT /upload/%s HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\nx" % bad).encode())
            assert code in (400, 404), (bad, code)
        assert not os.path.exists(os.path.join(work, "outside.txt"))
        assert raw(server.port, b"PUT /upload/a.txt HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n") == 411
        assert raw(server.port, b"PUT /upload/a.txt HTTP/1.1\r\nHost: x\r\n\r\n") == 411
        assert raw(server.port, b"GET /nothing HTTP/1.1\r\n\r\n") == 404
        assert raw(server.port, b"nonsense\r\n\r\n") == 400
        checks += 5

        # A body cut short: nothing kept, not even its part.
        with socket.create_connection(("127.0.0.1", server.port), timeout=10) as s:
            s.sendall(b"PUT /upload/Cut/short.bin HTTP/1.1\r\nHost: x\r\nContent-Length: 100000\r\n\r\n" + b"x" * 1000)
        for _ in range(50):
            if server.counts()["transfers"] == "0":
                break
            time.sleep(0.1)
        assert not os.path.exists(os.path.join(install, "Cut", "short.bin"))
        assert not os.path.exists(os.path.join(install, "Cut", "short.bin.pc5part"))
        assert server.counts()["uploads"] == "2"
        checks += 2
    finally:
        stopped = server.stop()
    assert stopped == "stopped running=0", stopped
    checks += 1

    # With a username and password: asked for (HTTP Basic), wrong ones refused.
    server = Server(serve, root, install, ("kevin", "pass word"))
    try:
        status, _, headers = request(server.base, "")
        assert status == 401 and headers["WWW-Authenticate"].startswith("Basic realm=\"PC5\""), (status, headers)
        assert request(server.base, "api/games", auth=("kevin", "nope"))[0] == 401
        assert request(server.base, "api/games", auth=("other", "pass word"))[0] == 401
        assert request(server.base, "upload/x.txt", "PUT", b"x", auth=("kevin", "nope"))[0] == 401
        assert not os.path.exists(os.path.join(install, "x.txt"))
        assert request(server.base, "api/status", auth=("kevin", "pass word"))[0] == 200
        assert request(server.base, "upload/x.txt", "PUT", b"x", auth=("kevin", "pass word"))[0] == 201
        checks += 6
    finally:
        server.stop()
    print("web page passed: %d checks" % checks)


if __name__ == "__main__":
    main()
