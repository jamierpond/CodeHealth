#!/usr/bin/env python3
"""Downloads a CodeScene project's latest per-file analysis from the local
server into validation/codescene/<project id>.json: path, language, score,
smells with counts, change frequency, hotspot flag, and the analysed revisions.
The token is $CODESCENE_TOKEN, else the one the CodeScene MCP server stored."""
import json, os, pathlib, sys, urllib.request

server = os.environ.get("CODESCENE_URL", "http://localhost:3003")
mcpConfig = pathlib.Path.home() / "Library/Application Support/codehealth-mcp/config.json"
token = os.environ.get("CODESCENE_TOKEN") or json.loads(mcpConfig.read_text())["oauth_token"]
pageSize = 500

def fetch(path):
    request = urllib.request.Request(f"{server}/api/v2/{path}",
                                     headers={"Authorization": f"Bearer {token}"})
    return json.load(urllib.request.urlopen(request))

def fetchProject(projectId):
    analysis = fetch(f"projects/{projectId}/analyses/latest")
    files, page = [], 1
    while True:
        answer = fetch(f"projects/{projectId}/analyses/latest/files?page_size={pageSize}&page={page}")
        files += answer["files"]
        if page >= answer["max_pages"]:
            break
        page += 1
    scored = [{"path": f["path"], "language": f["language"],
               "score": f["code_health"]["current_score"],
               "linesOfCode": f["lines_of_code"],
               "changeFrequency": f.get("change_frequency", 0),
               "hotspot": f.get("hotspot", False),
               "smells": {v["code_smell"]: v.get("count", 1) for v in f.get("code_health_rule_violations", [])}}
              for f in files if isinstance(f.get("code_health", {}).get("current_score"), (int, float))]
    return {"project": analysis["name"], "revisions": analysis["analysis_repo_revisions"], "files": scored}

for projectId in sys.argv[1:]:
    target = pathlib.Path(__file__).resolve().parents[2] / "validation/codescene" / f"{projectId}.json"
    data = fetchProject(projectId)
    target.write_text(json.dumps(data, indent=1))
    print(f"{data['project']}: {len(data['files'])} scored files -> {target}")
