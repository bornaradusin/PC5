#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""A small HTTPS server for tests/test_net.c: plain and chunked answers, a redirect, a 404.

    tests/https_server.py PORT CERT KEY READY_FILE

Writes READY_FILE once it listens; serves until killed.
"""
import http.server
import ssl
import sys
import threading
from pathlib import Path

BIG = bytes((i * 7 + (i >> 9)) & 255 for i in range(1 << 20))


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send_body(self, body: bytes, kind: str = "application/json"):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        agent = self.headers.get("User-Agent", "")
        if self.path == "/small":
            self.send_body(b'{"hello":"world"}')
        elif self.path == "/agent":
            self.send_body(agent.encode())
        elif self.path == "/big":
            self.send_body(BIG, "application/octet-stream")
        elif self.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            for part in (b'{"parts":', b'[1,2,3]', b"}"):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/small")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
        elif self.path == "/loop":
            self.send_response(301)
            self.send_header("Location", "/loop")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
        else:
            self.send_response(404)
            self.send_header("Content-Length", "9")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"not found")
        self.close_connection = True


def main() -> int:
    port, cert, key, ready = int(sys.argv[1]), sys.argv[2], sys.argv[3], Path(sys.argv[4])
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    ready.write_text("ready\n")
    threading.Thread(target=server.serve_forever, daemon=True).start()
    threading.Event().wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
