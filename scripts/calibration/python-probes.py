#!/usr/bin/env python3
"""Writes small Python files that each isolate one counting question (does `with`
nest? does `raise` add complexity? do docstrings count as lines?), reviews them
through CodeScene's MCP server, and prints what CodeScene reported for each.

  python-probes.py [directory]   default: build/calibration/python/probes"""
import importlib.util, json, pathlib, subprocess, sys

here = pathlib.Path(__file__).resolve().parent
specification = importlib.util.spec_from_file_location("comparePython", here / "compare-python.py")
comparePython = importlib.util.module_from_spec(specification)
specification.loader.exec_module(comparePython)

eightIfs = "".join(f"    if a == {n}:\n        a += 1\n" for n in range(8))

def complexityProbe(construct):
    return f"def probe(a, b):\n{eightIfs}{construct}    return a\n"

complexityConstructs = {
    "baseline": "",
    "try_except": "    try:\n        a += 1\n    except ValueError:\n        a -= 1\n",
    "try_two_excepts": "    try:\n        a += 1\n    except ValueError:\n        a -= 1\n    except KeyError:\n        a -= 2\n",
    "try_finally": "    try:\n        a += 1\n    finally:\n        a -= 1\n",
    "raise": "    raise ValueError(a)\n",
    "with": "    with open(b) as f:\n        a += 1\n",
    "comprehension_for": "    a = [x for x in b]\n",
    "comprehension_if": "    a = [x for x in b if x]\n",
    "conditional_expression": "    a = 1 if b else 2\n",
    "and": "    a = b and a\n",
    "match_three_cases": "    match b:\n        case 1:\n            a += 1\n        case 2:\n            a += 2\n        case _:\n            a += 3\n",
    "while_else": "    while b:\n        b -= 1\n    else:\n        a += 1\n",
    "for_else": "    for x in b:\n        a += x\n    else:\n        a += 1\n",
    "lambda": "    f = lambda v: v + 1\n",
    "assert": "    assert a\n",
    "return_early": "    return b\n",
    "continue_break": "    for x in b:\n        if x:\n            continue\n        break\n",
}

def nestingProbe(wrapper):
    inner = "if a:\n    if b:\n        if a > b:\n            a += 1\n"
    if wrapper == "baseline":
        body = inner
    else:
        body = wrapper + "".join("    " + line + "\n" for line in inner.splitlines())
    return "def probe(a, b):\n" + "".join("    " + line + "\n" for line in body.splitlines()) + "    return a\n"

nestingWrappers = {
    "baseline": "",
    "try": "try:\n",
    "with": "with open(b) as f:\n",
    "for": "for x in b:\n",
}

def bumpProbe(name):
    chunk = "    if a:\n        if b:\n            a += 1\n"
    separator = "    a += 1\n"
    if name == "two_chunks_top_level":
        body = chunk + separator + chunk
    elif name == "three_chunks_in_one_for":
        body = "    for x in b:\n" + "".join("    " + l + "\n" for l in (chunk + separator + chunk + separator + chunk).splitlines())
    elif name == "if_else_both_nested":
        body = "    if a:\n        if b:\n            a += 1\n    else:\n        if b:\n            a -= 1\n" + separator + chunk
    elif name == "adjacent_chunks":
        body = chunk + chunk
    else:
        body = chunk
    return "def probe(a, b):\n" + body + "    return a\n"

def longMethodProbe(statementCount):
    statements = "".join(f"    a{n} = {n}\n" for n in range(statementCount))
    return f"def probe():\n{statements}    return 0\n"

def largeMethodProbe(docstringLines, commentLines=0):
    docstring = '    """' + "\n".join("    word" for _ in range(docstringLines)) + '\n    """\n' if docstringLines else ""
    comments = "".join("    # comment\n" for _ in range(commentLines))
    statements = "".join(f"    a{n} = {n}\n" for n in range(66))
    return f"def probe():\n{docstring}{comments}{statements}    return 0\n"

def functionCountProbe(count):
    return "".join(f"def function_{n}(a):\n    return a + {n}\n\n\n" for n in range(count))

def cohesionModuleProbe(groups):
    lines = []
    for group in range(groups):
        lines.append(f"STATE_{group} = 0\n\n")
        for member in range(3):
            lines.append(f"def group_{group}_member_{member}():\n    global STATE_{group}\n    STATE_{group} += {member}\n    return STATE_{group}\n\n\n")
    return "".join(lines)

def cohesionClassProbe(groups, membersPerGroup=3):
    lines = ["class Probe:\n"]
    for group in range(groups):
        for member in range(membersPerGroup):
            lines.append(f"    def group_{group}_member_{member}(self):\n        self.field_{group} = {member}\n        return self.field_{group}\n\n")
    return "".join(lines)

def testClassProbe(tests):
    lines = ["import unittest\n\n\nclass ProbeTests(unittest.TestCase):\n"]
    for test in range(tests):
        lines.append(f"    def test_{test}(self):\n        value = {test}\n        self.assertEqual(value, {test})\n\n")
    return "".join(lines)

def bodyLines(count, indent):
    return "".join(f"{indent}value_{n} = {n} * 2\n" for n in range(count))

def independentFunctionsProbe(count, linesEach):
    return "".join(f"def function_{n}(a):\n{bodyLines(linesEach, '    ')}    return a + {n}\n\n\n" for n in range(count))

def independentMethodsProbe(count, linesEach):
    return "class Probe:\n" + "".join(f"    def method_{n}(self, a):\n{bodyLines(linesEach, '        ')}        return a + {n}\n\n" for n in range(count))

def sharedStateFunctionsProbe(count, linesEach):
    return "STATE = {}\n\n\n" + "".join(f"def function_{n}(a):\n{bodyLines(linesEach, '    ')}    STATE[{n}] = a\n    return function_{(n + 1) % count}\n\n\n" for n in range(count))

def signalEmitter(name):
    return (f"def emit_{name}_signal(verbosity, interactive, db, **kwargs):\n"
            "    for app_config in apps.get_app_configs():\n"
            "        if app_config.models_module is None:\n"
            "            continue\n"
            "        if verbosity >= 2:\n"
            "            stdout = kwargs.get(\"stdout\", sys.stdout)\n"
            f"            stdout.write(\"Running {name} handlers %s\" % app_config.label)\n"
            f"        models.signals.{name}.send(\n"
            "            sender=app_config,\n"
            "            app_config=app_config,\n"
            "            verbosity=verbosity,\n"
            "            interactive=interactive,\n"
            "            using=db,\n"
            "            **kwargs,\n"
            "        )\n\n\n")

def clonesProbe(count, distinctOthers=0):
    header = "import sys\n\nfrom django.apps import apps\nfrom django.db import models\n\n\n"
    clones = "".join(signalEmitter(f"stage_{n}") for n in range(count))
    others = "".join(f"def other_{n}(a):\n" + "".join(f"    a = a * {n + k} - {k}\n" * 1 for k in range(n % 3 + 1)) + "    return a\n\n\n" for n in range(distinctOthers))
    return header + clones + others

nestingHeaders = {"if": "if a:", "for": "for x in b:", "while": "while a:"}

def nestedChain(chain):
    """`for,if`: a for holding an if holding one statement, as function body lines."""
    lines, indent = "", "    "
    for kind in chain.split(","):
        lines += indent + nestingHeaders[kind] + "\n"
        indent += "    "
    return lines + indent + "a += 1\n"

def shapeProbe(shape):
    """Each entry of shape is one top-level statement of the function."""
    return "def probe(a, b):\n" + "".join(nestedChain(chain) for chain in shape) + "    return a\n"

bumpShapes = {
    "for_if_if": ["for,if", "for,if"],
    "for_for_and_for_if": ["for,for", "for,if"],
    "for_for_twice": ["for,for", "for,for"],
    "if_for_twice": ["if,for", "if,for"],
    "while_if_twice": ["while,if", "while,if"],
    "if_if_if_twice": ["if,if,if", "if,if,if"],
    "for_if_if_twice": ["for,if,if", "for,if,if"],
}

handWrittenShapes = {
    "nesting_match.py": "def probe(a, b):\n    match a:\n        case 1:\n            if a:\n                if b:\n"
                        "                    if a > b:\n                        a += 1\n    return a\n",
    "bump_shape_for_holding_two_ifs.py": "def probe(a, b):\n    for x in b:\n        if a:\n            a += 1\n"
                                         "        if b:\n            a += 2\n    return a\n",
    "bump_shape_for_holding_two_if_chunks.py": "def probe(a, b):\n    for x in b:\n        if a:\n            if b:\n"
                                               "                a += 1\n        a += 1\n        if b:\n            if a:\n"
                                               "                a += 2\n    return a\n",
    "bump_shape_if_elif_else_nested.py": "def probe(a, b):\n    for x in b:\n        if a:\n            a += 1\n        elif b:\n"
                                         "            a += 2\n        else:\n            a += 3\n            if a:\n"
                                         "                a += 4\n            else:\n                a += 5\n    return a\n",
}

def buildComplexityAndNestingProbes():
    probes = {f"complexity_{k}.py": complexityProbe(v) for k, v in complexityConstructs.items()}
    probes |= {f"nesting_{k}.py": nestingProbe(v) for k, v in nestingWrappers.items()}
    probes["nesting_while.py"] = nestingProbe("while b:\n")
    return probes

def buildBumpProbes():
    probes = {f"bumps_{name}.py": bumpProbe(name) for name in
              ("one_chunk", "two_chunks_top_level", "three_chunks_in_one_for", "if_else_both_nested", "adjacent_chunks")}
    return probes | {f"bump_shape_{name}.py": shapeProbe(shape) for name, shape in bumpShapes.items()}

def buildLengthProbes():
    probes = {f"large_method_docstring_{lines}.py": largeMethodProbe(lines) for lines in (0, 10)}
    probes["large_method_comments_10.py"] = largeMethodProbe(0, 10)
    probes |= {f"long_method_{lines}.py": longMethodProbe(lines - 2) for lines in (100, 150, 200, 300, 450, 584, 800)}
    probes["large_method_docstring_10_with_5_blank.py"] = largeMethodProbe(10).replace("    word\n", "    word\n\n", 5)
    return probes

def buildModuleProbes():
    probes = {f"function_count_{count}.py": functionCountProbe(count) for count in (20, 30, 40, 50, 60, 75, 100)}
    for groups in (2, 3, 4, 6):
        probes[f"cohesion_module_{groups}.py"] = cohesionModuleProbe(groups)
        probes[f"cohesion_class_{groups}.py"] = cohesionClassProbe(groups)
    probes |= {f"test_class_{tests}.py": testClassProbe(tests) for tests in (5, 10, 20)}
    probes |= {f"independent_functions_{count}x8.py": independentFunctionsProbe(count, 8) for count in (10, 20, 40, 50, 60, 70, 80, 100)}
    probes |= {f"independent_methods_{count}x8.py": independentMethodsProbe(count, 8) for count in (40, 80)}
    probes |= {f"independent_functions_{count}x3.py": independentFunctionsProbe(count, 3) for count in (80, 160)}
    return probes | {f"shared_state_functions_{count}x8.py": sharedStateFunctionsProbe(count, 8) for count in (50, 60, 70, 80)}

def buildCloneProbes():
    probes = {f"clones_{count}.py": clonesProbe(count) for count in (2, 3, 4, 5, 6)}
    return probes | {f"clones_2_with_{others}_others.py": clonesProbe(2, others) for others in (2, 3, 4, 10)}

def buildProbes():
    return (buildComplexityAndNestingProbes() | buildBumpProbes() | buildLengthProbes()
            | buildModuleProbes() | buildCloneProbes() | handWrittenShapes)

def summarise(answer):
    if "error" in answer:
        return f"error: {answer['error'][:120]}"
    parts = [f"score {answer.get('score')}"]
    for smell in answer.get("review", []):
        functions = smell.get("functions") or [{}]
        details = ", ".join(f"{f.get('title', '')} {f.get('details', '')}".strip() for f in functions[:3]) + (f" +{len(functions) - 3}" if len(functions) > 3 else "")
        parts.append(f"{smell['category']} [{details}]")
    return "; ".join(parts)

def main():
    directory = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else comparePython.output / "probes"
    directory.mkdir(parents=True, exist_ok=True)
    if not (directory / ".git").exists():
        subprocess.run(["git", "init", "-q", str(directory)], check=True)
    probes = buildProbes()
    for name, text in probes.items():
        (directory / name).write_text(text)
    client = comparePython.McpClient(["cs-mcp"])
    results = {}
    for name in sorted(probes):
        answer = client.callTool("code_health_review", {"file_path": str(directory / name)})
        results[name] = answer
        print(f"{name}: {summarise(answer)}", flush=True)
    (directory / "results.json").write_text(json.dumps(results, indent=1))
    printCodehealthSideBySide(directory, results)

def summariseCodehealth(entry):
    parts = [f"score {entry['score']:.2f}"]
    for finding in entry["findings"]:
        parts.append(f"{finding['biomarker']} {finding.get('functionName', '')} {finding['value']:g}".rstrip())
    return "; ".join(parts)

def printCodehealthSideBySide(directory, results):
    """Commits the probes in their own scratch repository so codehealth reads them."""
    subprocess.run(["git", "-C", str(directory), "add", "-A"], check=True)
    subprocess.run(["git", "-C", str(directory), "-c", "user.name=probe", "-c", "user.email=probe@localhost",
                    "commit", "-q", "--allow-empty", "-m", "probes"], check=True)
    analysis = json.loads(subprocess.run([str(comparePython.binary), str(directory), "--json"],
                                         capture_output=True, text=True, check=True).stdout)
    ours = {f["path"]: f for f in analysis["files"]}
    print("\nprobe: CodeScene | codehealth")
    for name in sorted(results):
        if name in ours:
            print(f"{name}: {summarise(results[name])} | {summariseCodehealth(ours[name])}")

if __name__ == "__main__":
    main()
