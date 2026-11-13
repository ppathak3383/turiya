# SPDX-License-Identifier: AGPL-3.0-or-later
"""Merge a local-gemma result file ({"results": [{id, summary, proposed_actions}]}) into a run's outputs.json.
Usage: merge_gemma.py <result_xxx.md> <runs/<date>/outputs.json>"""
import json, re, sys
raw = open(sys.argv[1], encoding="utf-8").read()
body = raw.split("# Result", 1)[-1] if "# Result" in raw else raw
m = re.search(r"\{.*\}", body, re.S)
data = json.loads(m.group(0))
outs = json.load(open(sys.argv[2], encoding="utf-8"))
for r in data["results"]:
    outs[r["id"]] = json.dumps({"summary": r.get("summary", ""), "proposed_actions": r.get("proposed_actions", [])},
                               ensure_ascii=False)
json.dump(outs, open(sys.argv[2], "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(f"merged {len(data['results'])} -> {len(outs)} outputs")
