#!/usr/bin/env python3
"""Picks about 18 files of a repository spread across codehealth's score range,
to score with CodeScene's code_health_score for a validation sample.
Usage: pick-sample.py <repository> [count] [extensions] [path prefixes], lists comma separated"""
import json, os, pathlib, random, subprocess, sys

root = pathlib.Path(__file__).resolve().parents[2]
binary = root / "build" / ("codehealth.exe" if os.name == "nt" else "codehealth")
minimumLinesOfCode = 40
repository = os.path.expanduser(sys.argv[1])
count = int(sys.argv[2]) if len(sys.argv) > 2 else 18
extensions = tuple(sys.argv[3].split(",")) if len(sys.argv) > 3 and sys.argv[3] else None
prefixes = tuple(sys.argv[4].split(",")) if len(sys.argv) > 4 else ("",)

analysis = json.loads(subprocess.run([str(binary), repository, "--json"], capture_output=True, text=True, check=True).stdout)
files = [f for f in analysis["files"] if f["linesOfCode"] >= minimumLinesOfCode and f["functionCount"] > 0
         and (extensions is None or f["path"].endswith(extensions)) and f["path"].startswith(prefixes)]
files.sort(key=lambda f: f["score"])
random.seed(len(files))
for band in range(count):
    start, end = band * len(files) // count, (band + 1) * len(files) // count
    if start < end:
        chosen = random.choice(files[start:end])
        print(f"{chosen['score']:5.2f}  {repository}/{chosen['path']}")
