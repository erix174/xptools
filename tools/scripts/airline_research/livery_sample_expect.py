#!/usr/bin/env python3
"""What every stand in an apt.dat should spawn, by WED_LiveryFormatSpec.md §4.1.

    livery_sample_expect.py <apt.dat> [<X-Plane root>] [--index <livery_index.txt>]

Reads the stands (1300 + 1301 + 1313) of the first airport in the file and prints,
per stand, the classes its weights open and the operators and types eligible at
each - op class, Obsolete (R25), range from the nearest hub (R26) and HOME (R27)
all applied, the same predicate the sim and WED's Liveries tab use. Hubs are
placed from the install's Global Airports, so run it against the install the
index came from.

This is how docs/livery_sample/README.md's expected-results table was produced.

NOT THE REFERENCE (2026-09-28). It predates the equipment filter (spec P3), HOME
on every class, GA without range, pool stands (GA / military with no list) and
the legacy step-down, and disagrees with WED on all of them. The reference is
WED's own rule: WED_LiveryConformanceReport, see docs/livery_conformance/.
"""
import math, os, sys

# The report quotes the spec's section signs; a cp1252/cp437 console must not
# turn that into a UnicodeEncodeError halfway through.
sys.stdout.reconfigure(errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
args = [a for a in sys.argv[1:] if not a.startswith("--")]
if not args:
    sys.exit(__doc__)
APT = args[0]
XP = args[1] if len(args) > 1 else os.environ.get("XPLANE_ROOT")		# never guessed - see wed_paths.py
if not XP:
    sys.exit("usage: livery_sample_expect.py <apt.dat> <X-Plane root>   (or set XPLANE_ROOT)")
INDEX = sys.argv[sys.argv.index("--index") + 1] if "--index" in sys.argv else os.path.join(HERE, "livery_index.txt")

def cells(line):
    return [c.strip() for c in line.split("***")]

ops, rows = {}, []
for line in open(INDEX, encoding="utf-8"):
    if line.startswith("#") or "***" not in line:
        continue
    c = cells(line)
    if c[0] == "OPERATOR":
        ops[c[1].upper()] = {"name": c[2], "cty": c[3], "op": c[4], "hubs": c[6].split() if len(c) > 6 else []}
    elif len(c) == 10:
        rows.append({"type": c[0], "cls": c[1], "code": c[2].upper(), "regcty": c[4], "note": c[5],
                     "range": int(c[6]) if c[6].isdigit() else 0, "home": c[7] == "HOME", "op": c[8]})

# Hub positions: 1302 icao_code wins over the header ident, datum over runway
# midpoint - the same resolution gen_livery_index.py and WED make.
def resolve_hubs(xp_root, wanted):
    path = os.path.join(xp_root, "Global Scenery", "Global Airports", "Earth nav data", "apt.dat")
    by_ident, by_code = {}, {}
    if not os.path.exists(path):
        print(f"WARNING: no Global Airports at {path} - range not applied")
        return {}
    st = {"ident": None, "code": None, "lat": None, "lon": None, "rwy": None}
    def flush():
        i, c = st["ident"], st["code"]
        if not (i in wanted or c in wanted): return
        ll = (st["lat"], st["lon"]) if st["lat"] is not None and st["lon"] is not None else st["rwy"]
        if ll is None: return
        if c in wanted: by_code.setdefault(c, ll)
        if i in wanted: by_ident.setdefault(i, ll)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            head = line.split(None, 1)[0] if line[:1].isdigit() else ""
            if head in ("1", "16", "17"):
                flush()
                t = line.split()
                st.update(ident=t[4] if len(t) > 4 else None, code=None, lat=None, lon=None, rwy=None)
            elif st["ident"] is not None:
                if line.startswith("1302 "):
                    t = line.split()
                    if len(t) >= 3:
                        if   t[1] == "icao_code": st["code"] = t[2].upper()
                        elif t[1] == "datum_lat": st["lat"] = float(t[2])
                        elif t[1] == "datum_lon": st["lon"] = float(t[2])
                elif line.startswith("100 ") and st["rwy"] is None:
                    t = line.split()
                    try: st["rwy"] = ((float(t[9]) + float(t[18])) / 2, (float(t[10]) + float(t[19])) / 2)
                    except (IndexError, ValueError): pass
    flush()
    by_ident.update(by_code)
    return by_ident

HUBS = resolve_hubs(XP, {h for o in ops.values() for h in o["hubs"]})

def km(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * 6371.0 * math.asin(math.sqrt(h))

OP_FOR = {"airline": ("Pax",), "cargo": ("Cargo",), "military": ("Military", "Gov"), "general_aviation": ("GA",)}

def op_class(code):
    return ops[code]["op"] if code in ops else ""

def eligible(code, cls, stand_op, pos, country):
    """(rows that may park, rows refused by range) for one operator at one class."""
    want = OP_FOR.get(stand_op, ())
    generic = code.startswith("XPZZ_")
    if not (generic and stand_op in ("airline", "cargo")) and op_class(code) not in want:
        return [], []
    ok, far = [], []
    for r in rows:
        if r["code"] != code or r["cls"] != cls or r["note"] == "Obsolete":
            continue
        mil = op_class(code) in ("Military", "Gov") or code == "XPMI"
        if mil:
            home = ops.get(code, {}).get("cty") or r["regcty"]
            if r["home"] and home and country and home != country:
                continue
            ok.append(r); continue
        hubs = [HUBS[h] for h in ops.get(code, {}).get("hubs", []) if h in HUBS]
        if r["range"] and hubs and min(km(h, pos) for h in hubs) > r["range"]:
            far.append(r); continue
        ok.append(r)
    return ok, far

# ---- the sample's stands
country, stands, cur = "", [], None
for line in open(APT, encoding="utf-8", errors="replace"):
    t = line.split()
    if not t: continue
    if t[0] == "1302" and len(t) >= 3 and t[1] == "country": country = t[2].upper()
    elif t[0] == "1300":
        cur = {"name": " ".join(t[6:]), "pos": (float(t[1]), float(t[2])), "letter": None,
               "op": None, "codes": [], "weights": None, "notes": []}
        stands.append(cur)
    elif cur is None: continue
    elif t[0] == "1301":
        cur["letter"], cur["op"], cur["codes"] = t[1], t[2], [c.upper() for c in t[3:]]
    elif t[0] == "1313":
        if cur["weights"] is not None: cur["notes"].append("second 1313 ignored (R24)"); continue
        w = t[1:]
        if len(w) == 6 and all(x.isdigit() and len(x) <= 4 and int(x) <= 1000 for x in w):
            cur["weights"] = [int(x) for x in w]
        else:
            cur["notes"].append("1313 malformed, dropped (R5)")
    # the stand-row block 1310-1399: anything there but 1313 is a row this reader
    # does not know (1400 truck parking and the rest are real, other rows)
    elif t[0].isdigit() and 1310 <= int(t[0]) <= 1399 and t[0] != "1313":
        cur["notes"].append(f"row {t[0]} unknown, skipped (R15)")

print(f"# {APT}\n# index {INDEX}, {len(HUBS)} hubs placed, airport country {country or '?'}\n")
for s in stands:
    notes = ("  [" + "; ".join(s["notes"]) + "]") if s["notes"] else ""
    if s["op"] == "none":
        print(f"{s['name']}: none - no static aircraft (R29){notes}"); continue
    if s["weights"] is None:
        print(f"{s['name']}: no 1313 - today's behaviour, 1301 letter {s['letter']} (R17){notes}"); continue
    w = s["weights"]; total = sum(w)
    if total == 0:
        print(f"{s['name']}: all weights zero - nothing parks, by choice (§4.2){notes}"); continue
    print(f"{s['name']}: {s['op']} {' '.join(s['codes']) or '(none listed)'}{notes}")
    empty = 0
    for k in range(6):
        if not w[k]: continue
        cls = "ABCDEF"[k]
        got, gone = {}, {}
        for code in s["codes"]:
            ok, far = eligible(code, cls, s["op"], s["pos"], country)
            if ok: got[code] = sorted({r["type"] for r in ok})
            if far: gone[code] = sorted({r["type"] for r in far})
        if not got: empty += w[k]
        share = 100.0 * w[k] / total
        spawn = ", ".join(f"{c}:{'/'.join(v)}" for c, v in got.items()) or "NOTHING"
        far_s = ("   out of range: " + ", ".join(f"{c}:{'/'.join(v)}" for c, v in gone.items())) if gone else ""
        print(f"    {cls} {share:5.1f}%  {spawn}{far_s}")
    print(f"    P(empty) = {100.0 * empty / total:.0f}%")
