#!/usr/bin/env python3
"""Compares codehealth's Python scores and findings with CodeScene's, file by file.

  compare-python.py measure <name> <checkout>   runs codehealth --json on the whole checkout
                                                and records its runtime
  compare-python.py sample <name> [count] [excluded-sample-name]
                                                picks files spread over size and score,
                                                leaving out another sample's files
  compare-python.py codescene <name>            reviews the sampled files through the
                                                CodeScene MCP server (cs-mcp, over stdio)
  compare-python.py reuse <name> <new-name> <codehealth.json>
                                                the same sample and CodeScene reviews, scored by
                                                another codehealth run (for a before/after)
  compare-python.py disagreements <name>...     lists, per rule Jamie decides on, the files where
                                                the two tools disagree, for hand review
  compare-python.py report <label> <name>...    writes python-comparison.md, and adds this run's
                                                numbers under <label> to the history table

Everything lands in build/calibration/python/<name>.*; nothing in the checkouts changes."""
import importlib.util, json, math, os, pathlib, random, subprocess, sys, time

root = pathlib.Path(__file__).resolve().parents[2]
binary = pathlib.Path(os.environ.get("CODEHEALTH_BINARY", root / "build" / "codehealth"))
calibrateSpecification = importlib.util.spec_from_file_location("calibrate", pathlib.Path(__file__).with_name("calibrate.py"))
calibrate = importlib.util.module_from_spec(calibrateSpecification)
calibrateSpecification.loader.exec_module(calibrate)
output = root / "build" / "calibration" / "python"
reportPath = pathlib.Path(__file__).resolve().parent / "python-comparison.md"
defaultSampleSize = 50
sizeBands = 5
scoreBands = 5
codesceneTimeoutSeconds = 300
lineSlack = 2
fileLevelTypes = {"Code Duplication", "Low Cohesion", "Large File", "Overall Code Complexity",
                  "Primitive Obsession", "Brain Class", "Number of Functions in a Single Module",
                  "Lines of Declarations in a Single File"}

codesceneToCodehealth = {
    "Bumpy Road Ahead": "Bumpy Road",
    "Deep, Nested Complexity": "Deep Nesting",
    "Excess Number of Function Arguments": "Excess Arguments",
    "Lines of Code in a Single File": "Large File",
    "Large Method": "Large Method",
    "Complex Method": "Complex Method",
    "Complex Conditional": "Complex Conditional",
    "Overall Code Complexity": "Overall Code Complexity",
    "Code Duplication": "Code Duplication",
    "Primitive Obsession": "Primitive Obsession",
    "Brain Method": "Brain Method",
    "Brain Class": "Brain Class",
    "Low Cohesion": "Low Cohesion",
}

def pathFor(name, suffix):
    output.mkdir(parents=True, exist_ok=True)
    return output / f"{name}.{suffix}"

def measure(name, checkout):
    started = time.monotonic()
    result = subprocess.run([str(binary), checkout, "--json"], capture_output=True, text=True)
    seconds = time.monotonic() - started
    pathFor(name, "codehealth.json").write_text(result.stdout)
    analysed = json.loads(result.stdout)["files"] if result.returncode == 0 else []
    trackedPython = subprocess.run(["git", "-C", checkout, "ls-files", "*.py", "*.pyi"],
                                   capture_output=True, text=True).stdout.split()
    scoredPaths = {f["path"] for f in analysed}
    run = {"checkout": checkout, "exitCode": result.returncode, "seconds": round(seconds, 2),
           "stderr": result.stderr[-4000:], "trackedPython": len(trackedPython),
           "scoredPython": sum(1 for f in analysed if f["language"] == "Python"),
           "unscoredPython": sorted(set(trackedPython) - scoredPaths)}
    pathFor(name, "run.json").write_text(json.dumps(run, indent=1))
    print(f"{name}: exit {run['exitCode']}, {run['seconds']} s, "
          f"{run['scoredPython']} of {run['trackedPython']} Python files scored")

def loadCodehealthPython(name):
    files = json.loads(pathFor(name, "codehealth.json").read_text())["files"]
    return {f["path"]: f for f in files if f["language"] == "Python"}

def bandOf(value, edges):
    return sum(1 for edge in edges if value > edge)

def quantileEdges(values, bands):
    ordered = sorted(values)
    return [ordered[len(ordered) * band // bands] for band in range(1, bands)]

def loadSamplePaths(name):
    return set(pathFor(name, "sample.txt").read_text().split()) if name else set()

def groupBySizeAndScore(files, seed):
    sizeEdges = quantileEdges([f["linesOfCode"] for f in files], sizeBands)
    scoreEdges = quantileEdges([f["score"] for f in files], scoreBands)
    cells = {}
    for f in files:
        cells.setdefault((bandOf(f["linesOfCode"], sizeEdges), bandOf(f["score"], scoreEdges)), []).append(f["path"])
    generator = random.Random(seed)
    for paths in cells.values():
        paths.sort()
        generator.shuffle(paths)
    return cells

def takeRoundRobin(cells, chosen, count):
    while len(chosen) < count and any(cells.values()):
        for key in sorted(cells):
            if cells[key] and len(chosen) < count:
                chosen.append(cells[key].pop())
    return chosen

def findExtremes(files):
    largest = max(files, key=lambda f: f["linesOfCode"])["path"]
    worst = min(files, key=lambda f: (f["score"], -f["linesOfCode"]))["path"]
    return [largest] + ([worst] if worst != largest else [])

def sample(name, count, excludedSampleName=None):
    excluded = loadSamplePaths(excludedSampleName)
    files = [f for f in loadCodehealthPython(name).values() if f["linesOfCode"] > 0 and f["path"] not in excluded]
    chosen = [] if excluded else findExtremes(files)
    remaining = [f for f in files if f["path"] not in chosen]
    chosen = takeRoundRobin(groupBySizeAndScore(remaining, name), chosen, count)
    pathFor(name, "sample.txt").write_text("\n".join(chosen) + "\n")
    print(f"{name}: sampled {len(chosen)} files")

class McpClient:
    def __init__(self, command):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.nextId = 0
        self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                    "clientInfo": {"name": "codehealth-calibration", "version": "1"}})
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def send(self, message):
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

    def request(self, method, params):
        self.nextId += 1
        self.send({"jsonrpc": "2.0", "id": self.nextId, "method": method, "params": params})
        message = self.readMessage()
        while message.get("id") != self.nextId:
            message = self.readMessage()
        return message

    def readMessage(self):
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("cs-mcp closed its output")
        return json.loads(line)

    def callTool(self, tool, arguments):
        answer = self.request("tools/call", {"name": tool, "arguments": arguments})
        if "error" in answer:
            return {"error": answer["error"]}
        text = "".join(part.get("text", "") for part in answer["result"].get("content", []))
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return {"error": text}

def fetchCodescene(name):
    run = json.loads(pathFor(name, "run.json").read_text())
    store = pathFor(name, "codescene.json")
    reviews = json.loads(store.read_text()) if store.exists() else {}
    client = McpClient(["cs-mcp"])
    for path in pathFor(name, "sample.txt").read_text().split():
        if path in reviews and "error" not in reviews[path]:
            continue
        answer = client.callTool("code_health_review", {"file_path": f"{run['checkout']}/{path}"})
        reviews[path] = {"score": answer.get("score"), "review": answer.get("review", []),
                         **({"error": answer["error"]} if "error" in answer else {})}
        store.write_text(json.dumps(reviews, indent=1))
        print(f"{name}: {path} {reviews[path]['score']}", flush=True)

def codesceneFindings(review):
    findings = []
    for smell in review:
        category = codesceneToCodehealth.get(smell["category"], smell["category"])
        for function in smell.get("functions", []) or [{"title": "", "start-line": 0, "end-line": 0}]:
            findings.append({"type": category, "function": function.get("title", "").split(":")[0],
                             "start": function.get("start-line") or 0,
                             "end": function.get("end-line") or 0,
                             "details": function.get("details", "")})
    return findings

def isSameFinding(codescene, codehealth):
    if codescene["type"] != codehealth["biomarker"]:
        return False
    if codescene["start"] == 0:
        return True
    line = codehealth.get("line", 0)
    lastName = codescene["function"].split(".")[-1]
    insideFunction = codescene["start"] - lineSlack <= line <= codescene["end"] + lineSlack
    return insideFunction or (lastName != "" and lastName == codehealth.get("functionName"))

def keepOnePerFileLevelType(findings, typeOf):
    seen, kept = set(), []
    for finding in findings:
        kind = typeOf(finding)
        if kind in fileLevelTypes:
            if kind in seen:
                continue
            seen.add(kind)
        kept.append(finding)
    return kept

def isSameFindingOrFileType(codescene, codehealth):
    if codescene["type"] in fileLevelTypes:
        return codescene["type"] == codehealth["biomarker"]
    return isSameFinding(codescene, codehealth)

def matchFindings(codesceneList, codehealthList):
    """Function findings match within the same function; file-level types (duplication,
    cohesion, size) match once per file, since one side reports them per function."""
    codesceneList = keepOnePerFileLevelType(codesceneList, lambda f: f["type"])
    unmatched = keepOnePerFileLevelType(codehealthList, lambda f: f["biomarker"])
    matched, missed = [], []
    for finding in codesceneList:
        partner = next((c for c in unmatched if isSameFindingOrFileType(finding, c)), None)
        if partner is None:
            missed.append(finding)
        else:
            unmatched.remove(partner)
            matched.append(finding)
    return matched, missed, unmatched

def pearson(left, right):
    if len(left) < 3:
        return float("nan")
    leftMean, rightMean = sum(left) / len(left), sum(right) / len(right)
    covariance = sum((a - leftMean) * (b - rightMean) for a, b in zip(left, right))
    spread = math.sqrt(sum((a - leftMean) ** 2 for a in left) * sum((b - rightMean) ** 2 for b in right))
    return covariance / spread if spread else float("nan")

def describeFinding(finding):
    if "biomarker" in finding:
        return f"{finding['biomarker']} {finding.get('functionName', '')}:{finding.get('line', '')} ({finding.get('value', '')})"
    return f"{finding['type']} {finding['function']}:{finding['start']} ({finding['details']})"

def countFindingTypes(perType, matched, missed, extra):
    for kind, findings, typeOf in (("matched", matched, lambda f: f["type"]),
                                   ("missed", missed, lambda f: f["type"]),
                                   ("extra", extra, lambda f: f["biomarker"])):
        for finding in findings:
            perType.setdefault(typeOf(finding), {"matched": 0, "missed": 0, "extra": 0})[kind] += 1

def compareSample(name):
    codehealth = loadCodehealthPython(name)
    reviews = json.loads(pathFor(name, "codescene.json").read_text())
    comparison = {"rows": [], "perType": {}, "failures": [], "unscoredByCodescene": []}
    for path, review in reviews.items():
        if review.get("error") or path not in codehealth:
            comparison["failures"].append(f"{path}: {review.get('error', 'codehealth did not score it')}"[:300])
        elif review.get("score") is None:
            ours = codehealth[path]
            comparison["unscoredByCodescene"].append(f"{path} (codehealth {ours['score']:.2f}, {ours['linesOfCode']} lines)")
        else:
            comparison["rows"].append(compareFile(path, review, codehealth[path], comparison["perType"]))
    return comparison

def compareFile(path, review, ours, perType):
    matched, missed, extra = matchFindings(codesceneFindings(review["review"]), ours["findings"])
    countFindingTypes(perType, matched, missed, extra)
    return {"path": path, "codescene": review["score"], "codehealth": ours["score"],
            "lines": ours["linesOfCode"], "missed": missed, "extra": extra}

def summariseScores(rows):
    codesceneScores = [r["codescene"] for r in rows]
    codehealthScores = [r["codehealth"] for r in rows]
    count = max(len(rows), 1)
    return {"files": len(rows), "pearson": round(pearson(codesceneScores, codehealthScores), 3),
            "spearman": round(calibrate.spearman(codesceneScores, codehealthScores), 3),
            "meanAbsolute": round(sum(abs(a - b) for a, b in zip(codesceneScores, codehealthScores)) / count, 3),
            "meanSigned": round(sum(b - a for a, b in zip(codesceneScores, codehealthScores)) / count, 3)}

def formatRunAndScores(name, run, comparison, summary):
    lines = [f"## {name}", "",
             f"Whole repository: exit code {run['exitCode']}, {run['seconds']} s wall clock, "
             f"{run['scoredPython']} of {run['trackedPython']} tracked .py files scored.", ""]
    if run["unscoredPython"]:
        shown = ", ".join(run["unscoredPython"][:10])
        lines += [f"Not scored ({len(run['unscoredPython'])}; codehealth skips third_party, vendor and similar directories by design): {shown}", ""]
    lines += [f"Sample: {summary['files']} files compared, {len(comparison['unscoredByCodescene'])} without a CodeScene score, "
              f"{len(comparison['failures'])} failed.", "",
              "| Pearson | Spearman | Mean absolute difference | Mean (codehealth - CodeScene) |", "|---|---|---|---|",
              f"| {summary['pearson']:.2f} | {summary['spearman']:.2f} | {summary['meanAbsolute']:.2f} | {summary['meanSigned']:+.2f} |", ""]
    for heading, entries in (("CodeScene gave no score (empty or declaration-only files):", comparison["unscoredByCodescene"]),
                             ("Not comparable:", comparison["failures"])):
        lines += [heading, ""] + [f"- {entry}" for entry in entries] + [""] if entries else []
    return lines

def formatRate(part, whole):
    return f"{part / whole:.0%}" if whole else "-"

def formatFindingTypes(perType):
    lines = ["Findings by type (a CodeScene function finding matched by one of ours in the same function):", "",
             "| Type | Matched | Missed | Extra | Recall | Precision |", "|---|---|---|---|---|---|"]
    for kind, counts in sorted(perType.items(), key=lambda item: -sum(item[1].values())):
        recall = formatRate(counts["matched"], counts["matched"] + counts["missed"])
        precision = formatRate(counts["matched"], counts["matched"] + counts["extra"])
        lines.append(f"| {kind} | {counts['matched']} | {counts['missed']} | {counts['extra']} | {recall} | {precision} |")
    return lines + [""]

def describeFirstFindings(findings):
    shown = "; ".join(describeFinding(f) for f in findings[:4])
    return shown + (" ..." if len(findings) > 4 else "")

def formatWorstFiles(rows):
    lines = ["Worst 10 files by score difference:", "",
             "| File | Lines | CodeScene | codehealth | Missed | Extra |", "|---|---|---|---|---|---|"]
    for row in sorted(rows, key=lambda r: -abs(r["codescene"] - r["codehealth"]))[:10]:
        lines.append(f"| {row['path']} | {row['lines']} | {row['codescene']:.2f} | {row['codehealth']:.2f} "
                     f"| {describeFirstFindings(row['missed'])} | {describeFirstFindings(row['extra'])} |")
    return lines + [""]

def reportRepository(name):
    run = json.loads(pathFor(name, "run.json").read_text())
    comparison = compareSample(name)
    summary = summariseScores(comparison["rows"]) | comparison["perType"]
    lines = formatRunAndScores(name, run, comparison, summary)
    lines += formatFindingTypes(comparison["perType"]) + formatWorstFiles(comparison["rows"])
    return lines, summary

def formatHistory(history, names):
    lines = ["## History", "", "One row per run of `compare-python.sh`, newest last.", "",
             "| Run | " + " | ".join(f"{name} r / rho / MAD / bias" for name in names) + " |",
             "|---|" + "---|" * len(names)]
    for entry in history:
        cells = []
        for name in names:
            numbers = entry["repositories"].get(name)
            cells.append("-" if numbers is None else
                         f"{numbers['pearson']:.2f} / {numbers['spearman']:.2f} / {numbers['meanAbsolute']:.2f} / {numbers['meanSigned']:+.2f}")
        lines.append(f"| {entry['label']} | " + " | ".join(cells) + " |")
    return lines + [""]

def report(label, names):
    marker = "<!-- Everything below is written by compare-python.py report. -->"
    existing = reportPath.read_text() if reportPath.exists() else "# codehealth against CodeScene on Python\n\n"
    handWritten = existing.split(marker)[0].rstrip() + "\n\n"
    lines = [marker, "", "CodeScene's numbers come from its MCP server's `code_health_review`; "
             "codehealth's from `codehealth --json` on the whole checkout.", ""]
    historyPath = output / "history.json"
    history = json.loads(historyPath.read_text()) if historyPath.exists() else []
    entry = {"label": label, "repositories": {}}
    sections = []
    for name in names:
        section, summary = reportRepository(name)
        sections += section
        entry["repositories"][name] = summary
    history = [h for h in history if h["label"] != label] + [entry]
    historyPath.write_text(json.dumps(history, indent=1))
    lines += formatHistory(history, names) + sections
    reportPath.write_text(handWritten + "\n".join(lines))
    print(f"wrote {reportPath}")

def reuseSample(name, newName, codehealthJson):
    for suffix in ("run.json", "sample.txt", "codescene.json"):
        pathFor(newName, suffix).write_text(pathFor(name, suffix).read_text())
    pathFor(newName, "codehealth.json").write_text(pathlib.Path(codehealthJson).read_text())

def findingsOfType(findings, kind, typeOf):
    return [f for f in findings if typeOf(f) == kind]

def listComparableFiles(name):
    codehealth = loadCodehealthPython(name)
    for path, review in json.loads(pathFor(name, "codescene.json").read_text()).items():
        if review.get("score") is not None and path in codehealth:
            yield path, review, codehealth[path]

def isRuleDisagreement(kind, theirs, ours):
    return kind == "Bumpy Road" or bool(theirs) != bool(ours)

def listRuleDisagreements(name, kind):
    for path, review, ours in listComparableFiles(name):
        theirs = findingsOfType(codesceneFindings(review["review"]), kind, lambda f: f["type"])
        oursOfKind = findingsOfType(ours["findings"], kind, lambda f: f["biomarker"])
        if isRuleDisagreement(kind, theirs, oursOfKind):
            yield (path, ours["linesOfCode"], [describeFinding(f) for f in theirs],
                   [describeFinding(f) for f in oursOfKind])

def listMatchingFindingsWithScoreGap(name):
    for path, review, ours in listComparableFiles(name):
        matched, missed, extra = matchFindings(codesceneFindings(review["review"]), ours["findings"])
        isExactMatch = matched and not (missed or extra)
        if isExactMatch:
            yield path, review["score"], ours["score"], [describeFinding(f) for f in matched]

def printDisagreements(names):
    for name in names:
        for kind in ("Code Duplication", "Large File", "Bumpy Road"):
            for entry in listRuleDisagreements(name, kind):
                print(name, kind, *entry, sep=" | ")
        for entry in listMatchingFindingsWithScoreGap(name):
            print(name, "same findings", *entry, sep=" | ")

if __name__ == "__main__":
    command, arguments = sys.argv[1], sys.argv[2:]
    if command == "measure":
        measure(arguments[0], arguments[1])
    elif command == "sample":
        sample(arguments[0], int(arguments[1]) if len(arguments) > 1 else defaultSampleSize,
               arguments[2] if len(arguments) > 2 else None)
    elif command == "codescene":
        fetchCodescene(arguments[0])
    elif command == "reuse":
        reuseSample(*arguments)
    elif command == "disagreements":
        printDisagreements(arguments)
    elif command == "report":
        report(arguments[0], arguments[1:])
    else:
        sys.exit(__doc__)
