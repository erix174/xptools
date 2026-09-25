#!/usr/bin/env python3
"""Check that WED exported every ramp start the way its earth.wed.xml says.

    check_wed_export.py <earth.wed.xml> <exported apt.dat>

check_1313_roundtrip.py compares an export against the file that was IMPORTED,
which stops being the right reference the moment anyone edits a stand. This one
compares against the document itself: for each ramp start in the .wed.xml it
works out the 1301 and 1313 rows WED_RampPosition::Export must write, and looks
for exactly those in the apt.dat.

  1301  size letter  - with weights in use, the highest class carrying a
                       non-zero weight (R23); otherwise the stand's own size
        op type      - the apt.dat token, whatever label the UI shows
        airlines     - lower case, single spaces, as CorrectAirlinesString
  1313               - present if and only if weights are in use and are six
                       integers 0..1000; then exactly those six (R5, R17)

It then reads the exported file back with the reader's rules - a 1313 must be
six integers and must follow a 1300/1301 - so a row WED would skip on the next
import shows up here as a failure rather than as a silent loss.

Exit status is 0 only when every stand matches.
"""
import re
import sys
import xml.etree.ElementTree as ET

LETTERS = "ABCDEF"
OP_TOKEN = {
    "None": "none",
    "General Aviation": "general_aviation", "Private/BizJet": "general_aviation",
    "Airline": "airline", "Passenger": "airline",
    "Cargo": "cargo",
    "Military": "military", "Military/Gov": "military",
}


def legal_weights(raw):
    t = raw.split()
    if len(t) != 6: return None
    if not all(re.fullmatch(r"[0-9]{1,4}", x) and int(x) <= 1000 for x in t): return None
    return [int(x) for x in t]


def expected_from_xml(path):
    root = ET.parse(path).getroot()
    out = {}
    for obj in root.iter("object"):
        if obj.get("class") != "WED_RampPosition": continue
        h = obj.find("hierarchy"); r = obj.find("ramp_start")
        if h is None or r is None: continue
        name = h.get("name")
        airlines = " ".join((r.get("airlines") or "").lower().split())
        w = legal_weights(r.get("weights") or "")
        in_use = (r.get("weights_mode") == "1") and w is not None
        letter = r.get("width") or "C"
        if in_use:
            nz = [i for i, x in enumerate(w) if x > 0]
            if nz: letter = LETTERS[max(nz)]
        out[name] = {
            "letter": letter,
            "op": OP_TOKEN.get(r.get("ramp_op_type") or "None", "?" + (r.get("ramp_op_type") or "")),
            "airlines": airlines,
            "weights": " ".join(str(x) for x in w) if in_use else None,
        }
    return out


def stands_from_apt(path):
    """name -> {'1301': [...tokens], '1313': 'w w w w w w' or None}, plus reader problems."""
    stands, problems, cur = {}, [], None
    for n, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
        t = line.split()
        if not t: continue
        code = t[0]
        if code == "1300":
            name = " ".join(t[6:]) if len(t) > 6 else ""
            cur = stands.setdefault(name, {"1301": None, "1313": None, "line": n})
            continue
        if code == "1301":
            if cur is None: problems.append(f"line {n}: 1301 with no 1300 before it")
            else: cur["1301"] = t[1:]
            continue
        if code == "1313":
            if cur is None:
                problems.append(f"line {n}: 1313 with no ramp start - the reader would skip it"); continue
            if legal_weights(" ".join(t[1:])) is None:
                problems.append(f"line {n}: 1313 '{' '.join(t[1:])}' is malformed - the reader would skip it")
            elif cur["1313"] is not None:
                problems.append(f"line {n}: second 1313 on '{name}' - the reader keeps the first")
            else:
                cur["1313"] = " ".join(t[1:])
            continue
        if not code.startswith("13"):
            cur = None if code in ("1", "16", "17", "99") else cur
    return stands, problems


def main(argv):
    if len(argv) != 3:
        print(__doc__); return 2
    want = expected_from_xml(argv[1])
    got, problems = stands_from_apt(argv[2])
    print(f"{'stand':<24} {'1301 (expected)':<34} {'1313 expected':<16} {'1313 exported':<16}")
    for name in sorted(want):
        e = want[name]
        g = got.get(name)
        exp1301 = f"{e['letter']} {e['op']} {e['airlines']}".strip()
        if g is None:
            problems.append(f"{name}: not in the exported apt.dat"); status = "MISSING"
        else:
            t = g["1301"] or []
            have1301 = " ".join(t).strip()
            if not t:
                problems.append(f"{name}: no 1301 row")
            else:
                if t[0] != e["letter"]:
                    problems.append(f"{name}: 1301 size {t[0]}, expected {e['letter']}")
                if len(t) < 2 or t[1] != e["op"]:
                    problems.append(f"{name}: 1301 op '{t[1] if len(t) > 1 else ''}', expected '{e['op']}'")
                if " ".join(t[2:]) != e["airlines"]:
                    problems.append(f"{name}: 1301 airlines '{' '.join(t[2:])}', expected '{e['airlines']}'")
            if g["1313"] != e["weights"]:
                problems.append(f"{name}: 1313 exported {g['1313']!r}, expected {e['weights']!r}")
        print(f"{name:<24} {exp1301:<34} {e['weights'] or '-':<16} {(g or {}).get('1313') or '-':<16}")
    extra = sorted(set(got) - set(want))
    for name in extra:
        problems.append(f"{name}: in the apt.dat but not a ramp start in the .wed.xml")
    print()
    if problems:
        print(f"FAILED - {len(problems)} problem(s):")
        for p in problems: print("   " + p)
        return 1
    print(f"OK - {len(want)} ramp starts: 1301 and 1313 match the document, and the file reads back clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
