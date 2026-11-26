#!/usr/bin/env python3
# Summary of macOS crash reports (.ips, ~/Library/Logs/DiagnosticReports) for Tests/sentry/run.sh: exception, signal and the
# top frames of the crashing thread, so that a crash the test did not cause on purpose explains itself in the log. run.sh
# uses it on GitHub's runners only, never on a person's Mac (the reports hold machine details).
#   crash_summary.py <report.ips>...
import json
import os
import sys

for path in sys.argv[1:]:
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            _, _, body = f.read().partition("\n")  # line 1: a JSON header of its own
        report = json.loads(body)
    except (OSError, ValueError) as e:
        print(f"crash report {os.path.basename(path)}: not readable ({e})")
        continue
    exc = report.get("exception", {})
    term = report.get("termination", {})
    print(f"crash report {os.path.basename(path)}: {exc.get('type', '?')} {exc.get('signal', '')} {exc.get('subtype', '')}"
          f"; termination: {term.get('indicator', '')}; process: {report.get('procName', '?')}")
    images = report.get("usedImages", [])
    threads = report.get("threads", [])
    faulting = report.get("faultingThread", 0)
    if isinstance(faulting, int) and 0 <= faulting < len(threads):
        thread = threads[faulting]
        print(f"    crashing thread {faulting}{' (' + thread['queue'] + ')' if thread.get('queue') else ''}:")
        for frame in thread.get("frames", [])[:25]:
            index = frame.get("imageIndex")
            image = images[index].get("name", "?") if isinstance(index, int) and 0 <= index < len(images) else "?"
            print(f"      {image}  {frame.get('symbol', '?')} + {frame.get('symbolLocation', frame.get('imageOffset', '?'))}")
