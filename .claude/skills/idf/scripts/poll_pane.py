#!/usr/bin/env python3
"""
Poll a tmux pane until a regex pattern appears or timeout is reached.

Usage: poll_pane.py <session> <pattern> [timeout_seconds] [interval_seconds]

Exits 0 if pattern found, 1 if timeout.
Prints the matching line to stdout.
"""
import subprocess
import sys
import re
import time

session = sys.argv[1] if len(sys.argv) > 1 else "idf"
pattern = sys.argv[2] if len(sys.argv) > 2 else "."
timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 60
interval = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0

deadline = time.time() + timeout
seen_lines = set()

while time.time() < deadline:
    result = subprocess.run(
        ["tmux", "capture-pane", "-t", session, "-p", "-S", "-500"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"ERROR: {result.stderr}", file=sys.stderr)
        sys.exit(2)

    for line in result.stdout.splitlines():
        if line in seen_lines:
            continue
        seen_lines.add(line)
        if re.search(pattern, line):
            print(line)
            sys.exit(0)

    time.sleep(interval)

print(f"TIMEOUT: pattern '{pattern}' not found within {timeout}s", file=sys.stderr)
sys.exit(1)
