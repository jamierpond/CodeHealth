#!/usr/bin/env python3
"""Calibrates codehealth's scoring rules against CodeScene.

  calibrate.py evaluate [rules.json]   scores, smells and hotspots per repo and language
  calibrate.py fit                     fits weights and caps on the train repos and writes
                                       validation/fitted-rules.json, then evaluates them

Corpus and reference data: validation/corpus.json and validation/codescene/*.json
for public repositories. validation/private/ (gitignored) adds private ones: its
corpus.json lists more repositories, and reviews.json holds function-level
findings copied from CodeScene's reviews
(fetch-codescene.py writes those). Measurements are cached per revision in
build/calibration, so only scoring reruns while fitting."""
import json, math, os, pathlib, subprocess, sys

root = pathlib.Path(__file__).resolve().parents[2]
binary = root / "build" / ("codehealth.exe" if os.name == "nt" else "codehealth")
cache = root / "build" / "calibration"
corpus = json.loads((root / "validation/corpus.json").read_text())
privateDirectory = root / "validation/private"
if (privateDirectory / "corpus.json").exists():
    corpus["repositories"] += json.loads((privateDirectory / "corpus.json").read_text())["repositories"]
hotspotListLength = 20

def run(arguments):
    return subprocess.run([str(binary)] + arguments, capture_output=True, text=True, check=True).stdout

def languageFamily(language):
    if language in ("C++", "C"):
        return "C/C++"
    if language.startswith("Objective-C"):
        return "Objective-C/C++"
    return "TypeScript/JavaScript"

def rank(values):
    order = sorted(range(len(values)), key=lambda index: values[index])
    ranks, position = [0.0] * len(values), 0
    while position < len(order):
        end = position
        while end + 1 < len(order) and values[order[end + 1]] == values[order[position]]:
            end += 1
        for tied in range(position, end + 1):
            ranks[order[tied]] = (position + end) / 2 + 1
        position = end + 1
    return ranks

def spearman(left, right):
    if len(left) < 3:
        return float("nan")
    leftRanks, rightRanks = rank(left), rank(right)
    leftMean, rightMean = sum(leftRanks) / len(left), sum(rightRanks) / len(right)
    covariance = sum((a - leftMean) * (b - rightMean) for a, b in zip(leftRanks, rightRanks))
    spread = math.sqrt(sum((a - leftMean) ** 2 for a in leftRanks) * sum((b - rightMean) ** 2 for b in rightRanks))
    return covariance / spread if spread else float("nan")

def measure(entry):
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / f"{entry['name']}-{entry['revision'][:12]}.metrics.json"
    if not path.exists():
        repository = os.path.expanduser(entry["repository"])
        path.write_text(run([repository, "--ref", entry["revision"], "--metrics"]))
    return path

def loadReference(entry):
    data = json.loads((root / "validation" / entry["reference"]).read_text())
    prefix = entry["referencePrefix"]
    return {f["path"][len(prefix):]: f for f in data["files"] if f["path"].startswith(prefix)}

def score(entry, rulesPath):
    arguments = ["--rescore", str(measure(entry))] + (["--rules", str(rulesPath)] if rulesPath else [])
    return {f["path"]: f for f in json.loads(run(arguments))}

def pairScores(entry, rulesPath):
    reference, ours = loadReference(entry), score(entry, rulesPath)
    return [(ours[path], reference[path]) for path in reference if path in ours]

def summarise(pairs):
    theirs = [reference["score"] for _, reference in pairs]
    mine = [file["score"] for file, _ in pairs]
    meanError = sum(abs(a - b) for a, b in zip(theirs, mine)) / len(pairs)
    return spearman(theirs, mine), meanError

def groupByFamily(pairs):
    groups = {}
    for file, reference in pairs:
        groups.setdefault(languageFamily(file["language"]), []).append((file, reference))
    return groups

def objective(pairsBySplit):
    total = 0.0
    for pairs in pairsBySplit:
        for group in groupByFamily(pairs).values():
            correlation, meanError = summarise(group)
            total += meanError + 2.0 * (1.0 - (0.0 if math.isnan(correlation) else correlation))
    return total

def smellAgreement(pairs):
    lines = []
    for ours, theirs in corpus["smellNames"].items():
        flaggedByUs = {f["path"] for f, _ in pairs if any(x["biomarker"] == ours for x in f["findings"])}
        theirNames = theirs if isinstance(theirs, list) else [theirs]
        flaggedByThem = {f["path"] for f, r in pairs if any(name in r["smells"] for name in theirNames)}
        both = len(flaggedByUs & flaggedByThem)
        precision = both / len(flaggedByUs) if flaggedByUs else float("nan")
        recall = both / len(flaggedByThem) if flaggedByThem else float("nan")
        lines.append(f"    {ours:24} files flagged: ours {len(flaggedByUs):4}  theirs {len(flaggedByThem):4}  both {both:4}  precision {precision:4.2f}  recall {recall:4.2f}")
    return lines

def hotspotAgreement(entry, pairs):
    analysis = json.loads(run([os.path.expanduser(entry["repository"]), "--ref", entry["revision"], "--json"]))
    commits = {h["path"]: h["commitCount"] for h in analysis["hotspots"]}
    shared = [(commits[f["path"]], r["changeFrequency"]) for f, r in pairs if f["path"] in commits]
    frequencyCorrelation = spearman([a for a, _ in shared], [b for _, b in shared])
    ourTop = [h["path"] for h in analysis["hotspots"] if h["path"] in {f["path"] for f, _ in pairs}][:hotspotListLength]
    theirTop = [r["path"][len(entry["referencePrefix"]):] for _, r in sorted(pairs, key=lambda p: -p[1]["changeFrequency"] * p[1]["linesOfCode"])][:hotspotListLength]
    overlap = len(set(ourTop) & set(theirTop))
    return f"    change frequency Spearman {frequencyCorrelation:.2f} over {len(shared)} files; top {hotspotListLength} hotspot overlap {overlap}/{hotspotListLength}"

def functionAgreement(entry, rulesPath):
    reviewsFile = privateDirectory / "reviews.json"
    if not reviewsFile.exists():
        return []
    reviews = json.loads(reviewsFile.read_text()).get(entry["name"], {})
    if not reviews:
        return []
    ours = score(entry, rulesPath)
    counts = {}
    for path, findings in reviews.items():
        theirs = {(biomarker, function) for biomarker, function, _ in findings}
        mine = {(x["biomarker"], x["functionName"]) for x in ours[path]["findings"]
                if x["functionName"] and x["biomarker"] in {b for b, _, _ in findings} | {"Large Method", "Deep Nesting", "Bumpy Road", "Complex Method", "Excess Arguments"}}
        for biomarker in {b for b, _ in theirs | mine}:
            tally = counts.setdefault(biomarker, [0, 0, 0])
            tally[0] += len({f for b, f in theirs & mine if b == biomarker})
            tally[1] += len({f for b, f in mine if b == biomarker})
            tally[2] += len({f for b, f in theirs if b == biomarker})
    return [f"    per function {biomarker:20} both {both:3}  ours {mine:3}  theirs {theirs:3}"
            for biomarker, (both, mine, theirs) in sorted(counts.items())]

def isSampled(entry):
    return json.loads((root / "validation" / entry["reference"]).read_text()).get("sampled", False)

def describeScores(label, group):
    correlation, meanError = summarise(group)
    return f"  {label:34} {len(group):5} files  Spearman {correlation:5.2f}  mean absolute error {meanError:4.2f}"

def evaluate(rulesPath):
    for entry in corpus["repositories"]:
        pairs = pairScores(entry, rulesPath)
        print(f"{entry['name']} ({entry['split']}): {len(pairs)} files matched")
        for family, group in sorted(groupByFamily(pairs).items()):
            print(describeScores(family, group))
            imperfect = [(f, r) for f, r in group if f["score"] < 10 or r["score"] < 10]
            if len(imperfect) != len(group) and len(imperfect) >= 3:
                print(describeScores(family + ", either below 10", imperfect))
        if isSampled(entry):
            continue
        print("\n".join(smellAgreement(pairs)))
        print("\n".join(functionAgreement(entry, rulesPath)))
        print(hotspotAgreement(entry, pairs))

def fit():
    rules = json.loads(run(["--print-rules"]))
    train = [e for e in corpus["repositories"] if e["split"] == "train"]
    cache.mkdir(parents=True, exist_ok=True)
    rulesPath = cache / "candidate-rules.json"

    def cost(candidate):
        rulesPath.write_text(json.dumps(candidate))
        return objective([pairScores(entry, rulesPath) for entry in train])

    best = cost(rules)
    print(f"start cost {best:.3f}")
    for sweep in range(4):
        for biomarker in rules["biomarkers"]:
            for field in ("weight", "categoryCap"):
                for factor in (0.5, 0.75, 1.33, 2.0):
                    original = biomarker[field]
                    biomarker[field] = original * factor
                    candidate = cost(rules)
                    if candidate < best - 1e-4:
                        best = candidate
                        print(f"  sweep {sweep}: {biomarker['name']} {field} -> {biomarker[field]:.3f}  cost {best:.3f}")
                    else:
                        biomarker[field] = original
    target = root / "validation/fitted-rules.json"
    target.write_text(json.dumps(rules, indent=1))
    print(f"wrote {target}\n")
    evaluate(target)

if __name__ == "__main__":
    command = sys.argv[1] if len(sys.argv) > 1 else "evaluate"
    if command == "fit":
        fit()
    else:
        evaluate(pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else None)
