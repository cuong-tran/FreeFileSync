#!/usr/bin/env python3
# Fake Sentry ingest endpoint for Tests/sentry/run.sh (python3 standard library only).
#
# Accepts what sentry-native and crashpad_handler send for a DSN http://<key>@127.0.0.1:<port>/<project>:
#   POST /api/<project>/envelope/   envelopes (reportInternalError() events), possibly gzip/deflate compressed
#   POST /api/<project>/minidump/   crash uploads from crashpad_handler: multipart/form-data with the minidump
#                                   ("upload_file_minidump") and sentry-native's scope ("__sentry-event", breadcrumbs)
# Each request is saved as <out dir>/<n>-<kind>/ with request.json (path, headers) and body.bin (decoded body); a multipart
# upload also gets one file per part (part-<name>). Answers 200 with an event id, like Sentry.
#
# Usage: fake_sentry.py <out dir> [port]   (port 0 or none: any free one; prints "port <n>" once listening on 127.0.0.1)
import email.parser
import email.policy
import gzip
import http.server
import json
import os
import sys
import threading
import uuid
import zlib

out_dir = sys.argv[1]
os.makedirs(out_dir, exist_ok=True)
counter_lock = threading.Lock()
counter = 0


def decode_body(body, encoding):
    encoding = (encoding or "").strip().lower()
    if encoding == "gzip":
        return gzip.decompress(body)
    if encoding == "deflate":
        try:
            return zlib.decompress(body)
        except zlib.error:
            return zlib.decompress(body, -zlib.MAX_WBITS)
    return body


def split_multipart(content_type, body):
    """returns [(name, filename, bytes)] of a multipart/form-data body"""
    message = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(
        b"Content-Type: " + content_type.encode() + b"\r\n\r\n" + body)
    parts = []
    if message.is_multipart():
        for part in message.iter_parts():
            parts.append((part.get_param("name", header="content-disposition") or "",
                          part.get_filename() or "",
                          part.get_payload(decode=True) or b""))
    return parts


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # quiet: run.sh prints what it needs
        pass

    def do_POST(self):
        global counter
        length = int(self.headers.get("Content-Length") or 0)
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            raw = b""
            while True:
                size = int(self.rfile.readline().strip() or b"0", 16)
                if size == 0:
                    self.rfile.readline()
                    break
                raw += self.rfile.read(size)
                self.rfile.readline()
        else:
            raw = self.rfile.read(length)
        body = decode_body(raw, self.headers.get("Content-Encoding"))

        path = self.path.split("?")[0]
        kind = "minidump" if path.rstrip("/").endswith("/minidump") else "envelope" if path.rstrip("/").endswith("/envelope") else "other"
        with counter_lock:
            counter += 1
            folder = os.path.join(out_dir, f"{counter:03d}-{kind}")
        os.makedirs(folder)
        with open(os.path.join(folder, "body.bin"), "wb") as f:
            f.write(body)
        content_type = self.headers.get("Content-Type", "")
        if content_type.startswith("multipart/"):
            for name, filename, data in split_multipart(content_type, body):
                with open(os.path.join(folder, "part-" + (name or filename or "unnamed").replace("/", "_")), "wb") as f:
                    f.write(data)
        with open(os.path.join(folder, "request.json.tmp"), "w") as f:
            json.dump({"path": self.path, "kind": kind, "headers": dict(self.headers.items())}, f, indent=1)
        os.rename(os.path.join(folder, "request.json.tmp"), os.path.join(folder, "request.json"))  # complete marker

        answer = json.dumps({"id": uuid.uuid4().hex}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(answer)))
        self.end_headers()
        self.wfile.write(answer)


server = http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[2]) if len(sys.argv) > 2 else 0), Handler)
print(f"port {server.server_address[1]}", flush=True)
server.serve_forever()
