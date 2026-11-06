"""Serve the web page from web/ on this computer, sending /api/ calls to a real clock.

Usage: python tools/dev_web.py CLOCK_HOST [--port 8000]

The page is rebuilt from the files on every request, so a change shows with a reload and nothing has
to be flashed. The API calls are the clock's own, and they really change it.
"""

import argparse
import http.client
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from build_web import build_page

HOP_BY_HOP = {"connection", "keep-alive", "transfer-encoding", "te", "upgrade", "proxy-authorization", "trailer"}


def make_handler(clock):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, format, *args):
            sys.stderr.write("%s %s\n" % (self.command, self.path))

        def do_GET(self):
            self.route()

        def do_POST(self):
            self.route()

        def route(self):
            if self.path in ("/", "/index.html"):
                body = build_page()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path.startswith("/api/"):
                self.forward()
            else:
                self.send_error(404)

        def forward(self):
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length) if length else None
            headers = {k: v for k, v in self.headers.items() if k.lower() not in HOP_BY_HOP | {"host", "content-length"}}
            try:
                connection = http.client.HTTPConnection(clock, 80, timeout=60)
                connection.request(self.command, self.path, body=body, headers=headers)
                response = connection.getresponse()
                data = response.read()
            except OSError as error:
                self.send_error(502, str(error))
                return
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in HOP_BY_HOP | {"content-length", "date", "server"}:
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            connection.close()

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("clock", help="the address of the clock, such as elekstube.local")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(args.clock))
    print(f"http://127.0.0.1:{args.port}/  ->  {args.clock}")
    server.serve_forever()


if __name__ == "__main__":
    main()
