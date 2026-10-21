"""Fixture for the PC test: every row of data.json plus what the C++ must produce.

Usage: python3 fixture.py data.json out.tsv
Lines:  NOW <iso>
        B <sci> <name> <last_seen> <week_count> <badge|badge|badge> <slug>   (data.json order)
        T <sci>      expected order of the latest 10 (newest first, ties in file order)
"""
import json
import re
import sys

data = json.load(open(sys.argv[1], encoding="utf-8"))
top = sorted(data, key=lambda b: b["last_seen"], reverse=True)[:10]
with open(sys.argv[2], "w", encoding="utf-8") as f:
    f.write("NOW\t2026-10-18T14:05:00\n")
    for b in data:
        slug = re.sub(r"[^a-z0-9]+", "_", b["sci"].lower()).strip("_")
        badges = "|".join(x["en"] for x in b["badges"][:3])
        f.write("\t".join(["B", b["sci"], b["name"], b["last_seen"], str(b["week_count"]), badges, slug]) + "\n")
    for b in top:
        f.write(f"T\t{b['sci']}\n")
