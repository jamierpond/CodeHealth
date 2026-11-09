#!/usr/bin/env python3
"""Checks that a Python change left every other language alone: compares each
non-Python file's score and findings in build/calibration/baseline/<name>.codehealth.json
(copied before the change) with build/calibration/python/<name>.codehealth.json.
Exits 1 and lists the files if any differ."""
import json, pathlib, sys

root = pathlib.Path(__file__).resolve().parents[2] / "build" / "calibration"

def loadOtherLanguages(path):
    files = json.loads(path.read_text())["files"]
    return {f["path"]: (f["language"], round(f["score"], 9), json.dumps(f["findings"], sort_keys=True))
            for f in files if f["language"] != "Python"}

changed, compared = [], 0
for baseline in sorted((root / "baseline").glob("*.codehealth.json")):
    before = loadOtherLanguages(baseline)
    after = loadOtherLanguages(root / "python" / baseline.name)
    compared += len(before)
    changed += [f"{baseline.name}: {path}" for path in sorted(set(before) | set(after))
                if before.get(path) != after.get(path)]
print(f"{compared} non-Python files compared, {len(changed)} changed")
for line in changed[:20]:
    print("  " + line)
sys.exit(1 if changed else 0)
