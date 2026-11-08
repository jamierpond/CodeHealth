#!/usr/bin/env python3
"""Writes validation/codescene/<name>-sample.json from "<score> <absolute path>"
lines on stdin, the CodeScene scores of a picked sample, and adds the
repository to validation/corpus.json as held out.
Usage: record-sample.py <name> <repository> < scores.txt"""
import json, os, pathlib, subprocess, sys

root = pathlib.Path(__file__).resolve().parents[2]
name, repository = sys.argv[1], os.path.expanduser(sys.argv[2])
files = []
for line in sys.stdin:
    score, path = line.split(maxsplit=1)
    files.append({"path": os.path.relpath(path.strip(), repository), "score": float(score)})

reference = f"codescene/{name}-sample.json"
(root / "validation" / reference).write_text(json.dumps(
    {"project": f"{name} (sampled with code_health_score)", "sampled": True, "files": files}, indent=1))
revision = subprocess.run(["git", "-C", repository, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
corpusPath = root / "validation/corpus.json"
corpus = json.loads(corpusPath.read_text())
corpus["repositories"] = [r for r in corpus["repositories"] if r["name"] != name] + [
    {"name": name, "repository": sys.argv[2], "revision": revision, "reference": reference,
     "referencePrefix": "", "split": "test"}]
corpusPath.write_text(json.dumps(corpus, indent=1))
print(f"{name}: {len(files)} files recorded")
