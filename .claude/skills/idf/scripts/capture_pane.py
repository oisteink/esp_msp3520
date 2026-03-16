#!/usr/bin/env python3
"""
Capture last N lines from a tmux pane.

Usage: capture_pane.py <session> [lines]
"""
import subprocess
import sys

session = sys.argv[1] if len(sys.argv) > 1 else "idf"
lines = int(sys.argv[2]) if len(sys.argv) > 2 else 200

result = subprocess.run(
    ["tmux", "capture-pane", "-t", session, "-p", "-S", f"-{lines}"],
    capture_output=True, text=True
)

if result.returncode != 0:
    print(f"ERROR: {result.stderr}", file=sys.stderr)
    sys.exit(1)

print(result.stdout)
