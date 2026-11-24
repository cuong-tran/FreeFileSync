#!/usr/bin/env python3
# Checks the requests fake_sentry.py saved (python3 standard library only). Used by Tests/sentry/run.sh.
#
#   check_report.py crash    <request dir> <release prefix> <environment> <app> [forbidden text...]
#       a crashpad upload: a minidump ("MDMP"), sentry-native's event (msgpack) with that release, environment and app tag,
#       no "user" or "server_name", and none of the forbidden texts (user name, host name, home folder) in the event or the
#       breadcrumbs
#   check_report.py internal <request dir> <release prefix> <environment> <app> [forbidden text...]
#       an envelope with one event: same checks, the message must be scrubbed ("<path>" present, no forbidden text, no
#       "C:\Users", no "secret"), and module paths in debug_meta must be plain file names
# Exit code 0: all checks passed; 1: a check failed (each failure is printed).
import json
import os
import struct
import sys

failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print("FAILED: " + what)


def unpack_msgpack(data, pos=0):
    """minimal msgpack reader: nil, bool, int, float, str, bin, array, map (what sentry-native writes)"""
    b = data[pos]
    pos += 1
    if b <= 0x7f: return b, pos
    if 0x80 <= b <= 0x8f: return unpack_map(data, pos, b & 0x0f)
    if 0x90 <= b <= 0x9f: return unpack_array(data, pos, b & 0x0f)
    if 0xa0 <= b <= 0xbf: n = b & 0x1f; return data[pos:pos + n].decode("utf-8", "replace"), pos + n
    if b >= 0xe0: return b - 0x100, pos
    if b == 0xc0: return None, pos
    if b == 0xc2: return False, pos
    if b == 0xc3: return True, pos
    sizes = {0xcc: (">B", 1), 0xcd: (">H", 2), 0xce: (">I", 4), 0xcf: (">Q", 8),
             0xd0: (">b", 1), 0xd1: (">h", 2), 0xd2: (">i", 4), 0xd3: (">q", 8), 0xca: (">f", 4), 0xcb: (">d", 8)}
    if b in sizes:
        fmt, n = sizes[b]
        return struct.unpack(fmt, data[pos:pos + n])[0], pos + n
    lengths = {0xd9: 1, 0xda: 2, 0xdb: 4, 0xc4: 1, 0xc5: 2, 0xc6: 4}
    if b in lengths:
        k = lengths[b]
        n = int.from_bytes(data[pos:pos + k], "big")
        pos += k
        raw = data[pos:pos + n]
        return (raw.decode("utf-8", "replace") if b in (0xd9, 0xda, 0xdb) else raw), pos + n
    if b in (0xdc, 0xdd):
        k = 2 if b == 0xdc else 4
        return unpack_array(data, pos + k, int.from_bytes(data[pos:pos + k], "big"))
    if b in (0xde, 0xdf):
        k = 2 if b == 0xde else 4
        return unpack_map(data, pos + k, int.from_bytes(data[pos:pos + k], "big"))
    raise ValueError(f"unsupported msgpack type 0x{b:02x} at {pos - 1}")


def unpack_array(data, pos, n):
    items = []
    for _ in range(n):
        item, pos = unpack_msgpack(data, pos)
        items.append(item)
    return items, pos


def unpack_map(data, pos, n):
    result = {}
    for _ in range(n):
        key, pos = unpack_msgpack(data, pos)
        val, pos = unpack_msgpack(data, pos)
        result[key] = val
    return result, pos


def read(path):
    with open(path, "rb") as f:
        return f.read()


def check_event(event, release_prefix, environment, app):
    check(str(event.get("release", "")).startswith(release_prefix), f"release {event.get('release')!r} starts with {release_prefix!r}")
    check(event.get("environment") == environment, f"environment {event.get('environment')!r} is {environment!r}")
    check((event.get("tags") or {}).get("app") == app, f"tag app {(event.get('tags') or {}).get('app')!r} is {app!r}")
    check("user" not in event, f"no user in the event (got {event.get('user')!r})")
    check("server_name" not in event, f"no server_name in the event (got {event.get('server_name')!r})")


def check_forbidden(name, data, forbidden):
    for text in forbidden:
        check(text.encode() not in data, f"{name} does not contain {text!r}")


def main():
    mode, folder, release_prefix, environment, app = sys.argv[1:6]
    forbidden = [t for t in sys.argv[6:] if t]
    request = json.loads(read(os.path.join(folder, "request.json")))

    if mode == "crash":
        check(request["kind"] == "minidump", f"request is a minidump upload (got {request['kind']}: {request['path']})")
        minidump = os.path.join(folder, "part-upload_file_minidump")
        check(os.path.isfile(minidump) and read(minidump)[:4] == b"MDMP", "the upload contains a minidump (MDMP)")
        event_file = os.path.join(folder, "part-__sentry-event")
        check(os.path.isfile(event_file), "the upload contains __sentry-event")
        if os.path.isfile(event_file):
            raw = read(event_file)
            event, _ = unpack_msgpack(raw)
            print("event: " + json.dumps(event, default=repr)[:600])
            check_event(event, release_prefix, environment, app)
            check_forbidden("__sentry-event", raw, forbidden)
        for crumbs in ("part-__sentry-breadcrumb1", "part-__sentry-breadcrumb2"):
            if os.path.isfile(os.path.join(folder, crumbs)):
                check_forbidden(crumbs, read(os.path.join(folder, crumbs)), forbidden)

    elif mode == "internal":
        check(request["kind"] == "envelope", f"request is an envelope (got {request['kind']}: {request['path']})")
        body = read(os.path.join(folder, "body.bin"))
        lines = body.split(b"\n")
        events = []
        i = 1
        while i + 1 < len(lines):  # envelope: header line, then item header + payload lines
            item = json.loads(lines[i]) if lines[i].strip() else {}
            if item.get("type") == "event":
                events.append(json.loads(lines[i + 1]))
            i += 2
        check(len(events) == 1, f"the envelope has one event (got {len(events)})")
        if events:
            event = events[0]
            check_event(event, release_prefix, environment, app)
            message = (event.get("message") or {}).get("formatted", "")
            print("message: " + message)
            check("FFS_TEST_CRASH" in message, "the message is the test message")
            check("<path>" in message, "the message has <path> placeholders")
            for text in forbidden + ["C:\\Users", "secret", "john"]:
                check(text not in message, f"the message does not contain {text!r}")
            images = (event.get("debug_meta") or {}).get("images", [])
            check(len(images) > 0, "debug_meta lists the modules")
            check(all("/" not in img.get("code_file", "") and "\\" not in img.get("code_file", "") for img in images),
                  "module paths are file names only")
        check_forbidden("the envelope", body, forbidden)
    else:
        sys.exit(f"unknown mode {mode}")

    sys.exit(1 if failures else 0)


main()
