#!/usr/bin/env python3
"""Recompute the "empty stand" measurement of WED_LiveryFormatSpec.md section 4.5.

Question: if every airline/cargo stand's weights were set to the size class it
already declares, how many stands would spawn nothing at all?

A listed airline code is eligible at a stand declaring class X when it has at
least one livery row with
    CLASS == X,
    NOTE  != Obsolete                       (switchable: --no-obsolete in the table),
    operator OP class matching the stand    (airline -> Pax, cargo -> Cargo, XPZZ_* both),
    in range (R26)                          (switchable),
where "in range" means min haversine distance stand -> operator hub <= RANGE_KM,
failing open when RANGE_KM is empty/0 or no hub can be placed.

Reports four rule combinations plus the cause split for the full rule set.
Standard library only. Read-only: writes nothing.

usage: measure_empty_stands.py [APT_DAT] [--index livery_index.txt]
"""
import argparse, math, os, sys, time
from collections import Counter, defaultdict

DEFAULT_APT = (r"D:\SteamLibrary\steamapps\common\X-Plane 12\Global Scenery"
               r"\Global Airports\Earth nav data\apt.dat")
DEFAULT_INDEX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "livery_index.txt")
EARTH_R_KM = 6371.0
STAND_OP_TO_CLASS = {"airline": "Pax", "cargo": "Cargo"}


def cells(line):
    return [c.strip() for c in line.split("***")]


def load_index(path):
    """-> operators {CODE: (op_class, [hub icao...])}, rows {CODE: [row dict...]}"""
    operators, rows = {}, defaultdict(list)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#") or "***" not in s:
                continue
            c = cells(s)
            if c[0] == "OPERATOR":
                if len(c) < 5:
                    continue
                code = c[1].upper()
                hubs = c[6].upper().split() if len(c) > 6 else []
                operators[code] = (c[4], hubs)
                continue
            if len(c) < 10:
                continue
            typ, cls, airline, _reg, _cty, note, rng, _scope, op = c[:9]
            try:
                rng_km = float(rng) if rng else 0.0
            except ValueError:
                rng_km = 0.0
            rows[airline.upper()].append({
                "type": typ, "cls": cls.upper(), "obsolete": note.lower() == "obsolete",
                "range": rng_km, "row_op": op, "path": c[-1],
            })
    return operators, rows


def scan_apt(path):
    """One streaming pass.
    -> airports {key: (lat, lon)} keyed by 1302 icao_code and by header ident
       (icao_code wins, as in gen_livery_index.resolve_hubs),
       stands [(airport_idx, lat, lon, size, ramp_op, [codes])],
       n_ramp_starts"""
    by_ident, by_code = {}, {}
    stands = []
    n_1300 = 0
    apt_idx = -1
    ident = code = None
    dlat = dlon = None
    rwy = None
    pending = None          # (lat, lon) of a 1300 awaiting its 1301

    def flush():
        if ident is None:
            return
        ll = (dlat, dlon) if dlat is not None and dlon is not None else rwy
        if ll is None:
            return
        by_ident.setdefault(ident, ll)
        if code:
            by_code.setdefault(code, ll)

    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line[:1].isdigit():
                continue
            t = line.split()
            if not t:
                continue
            h = t[0]
            if h in ("1", "16", "17"):
                flush()
                apt_idx += 1
                ident = t[4].upper() if len(t) > 4 else None
                code = None; dlat = dlon = None; rwy = None; pending = None
            elif h == "1300":
                n_1300 += 1
                try:
                    pending = (float(t[1]), float(t[2]))
                except (IndexError, ValueError):
                    pending = None
            elif h == "1301":
                if pending is not None and len(t) >= 3:
                    codes = [x.upper() for x in " ".join(t[3:]).replace(",", " ").split()]
                    stands.append((apt_idx, pending[0], pending[1], t[1].upper(), t[2].lower(), codes))
                pending = None
            elif h == "1302" and len(t) >= 3:
                if   t[1] == "icao_code": code = t[2].upper()
                elif t[1] == "datum_lat":
                    try: dlat = float(t[2])
                    except ValueError: pass
                elif t[1] == "datum_lon":
                    try: dlon = float(t[2])
                    except ValueError: pass
            elif h == "100" and rwy is None:
                try:
                    rwy = ((float(t[9]) + float(t[18])) / 2, (float(t[10]) + float(t[19])) / 2)
                except (IndexError, ValueError):
                    pass
    flush()
    by_ident.update(by_code)
    return by_ident, stands, n_1300


def haversine(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * EARTH_R_KM * math.asin(min(1.0, math.sqrt(h)))


# Operators that no longer exist at all: every Obsolete row of theirs is a
# defunct-operator mark. Every other Obsolete row is a retired-airframe mark.
DEFUNCT = set("AZA EGF AWE BER LPV RUS WLC RVF SWG SWQ".split())


def by_mark(pop, rows, op_ok, in_range, op_map, empty_all, empty_no_obs, pct):
    """Stands empty with all rules but not with Obsolete off: which marked
    (airline, class) pair was filling each one under Obsolete-off."""
    newly = [s for s in pop if id(s) in empty_all and id(s) not in empty_no_obs]
    single, single_apt = Counter(), defaultdict(set)
    several = Counter(); several_apt = defaultdict(set)
    stale = 0; stale_apt = set()
    for s in newly:
        ai, lat, lon, cls, sop, codes = s
        want = op_map[sop]
        fillers = {c for c in codes for r in rows.get(c, ())
                   if r["cls"] == cls and op_ok(c, r, want) and in_range(c, r, (lat, lon))}
        if len(fillers) == 1:
            k = (next(iter(fillers)), cls); single[k] += 1; single_apt[k].add(ai)
        else:
            g = ("defunct only" if fillers <= DEFUNCT else
                 "retired only" if not (fillers & DEFUNCT) else "mixed")
            several[g] += 1; several_apt[g].add(ai)
        if DEFUNCT & set(codes):
            stale += 1; stale_apt.add(ai)
    marked = sorted({(c, r["cls"]) for c, rs in rows.items() for r in rs if r["obsolete"]})
    nrows = Counter((c, r["cls"]) for c, rs in rows.items() for r in rs if r["obsolete"])
    n = len(newly); na = len({s[0] for s in newly})
    print(f"\nby mark: {n} stands newly empty (Obsolete off -> on) at {na} airports")
    print(f"  {'pair':8s} {'grp':8s} {'rows':>4s} {'stands':>7s} {'airports':>8s}")
    for k in sorted(marked, key=lambda k: (-single[k], -len(single_apt[k]), k)):
        grp = "defunct" if k[0] in DEFUNCT else "retired"
        print(f"  {k[0] + '/' + k[1]:8s} {grp:8s} {nrows[k]:>4d} {single[k]:>7d} {len(single_apt[k]):>8d}")
    for grp, test in (("retired", lambda c: c not in DEFUNCT), ("defunct", lambda c: c in DEFUNCT)):
        ks = [k for k in marked if test(k[0])]
        st = sum(single[k] for k in ks); ap_ = set().union(*(single_apt[k] for k in ks)) if ks else set()
        print(f"  total single-cause {grp}: {len(ks)} pairs, {sum(nrows[k] for k in ks)} rows, "
              f"{st} stands {pct(st, n):.1f}%, {len(ap_)} airports")
    for g in ("retired only", "defunct only", "mixed"):
        print(f"  several ({g}): {several[g]} stands, {len(several_apt[g])} airports")
    tot_sev = sum(several.values()); sev_apt = set().union(*several_apt.values()) if several_apt else set()
    print(f"  several total: {tot_sev} stands {pct(tot_sev, n):.1f}%, {len(sev_apt)} airports")
    print(f"  sanity: newly empty stands listing a defunct-11 code: {stale} {pct(stale, n):.1f}%, "
          f"{len(stale_apt)} airports")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("apt_dat", nargs="?", default=DEFAULT_APT)
    ap.add_argument("--index", default=DEFAULT_INDEX)
    ap.add_argument("--by-mark", action="store_true",
                    help="break the (Obsolete off -> on) step down by which (airline, class) mark empties each stand")
    args = ap.parse_args()
    t0 = time.time()

    operators, rows = load_index(args.index)
    airports, stands, n_1300 = scan_apt(args.apt_dat)

    # operator code -> [hub positions]; report unplaced hubs
    hub_pos, unplaced = {}, []
    for code, (_op, hubs) in operators.items():
        ps = []
        for hcode in hubs:
            if hcode in airports: ps.append(airports[hcode])
            else: unplaced.append((code, hcode))
        hub_pos[code] = ps

    def op_class(code, row):
        rec = operators.get(code)
        return rec[0] if rec else row["row_op"]

    def op_ok(code, row, want):
        if code.startswith("XPZZ_"):
            return True
        return op_class(code, row) == want

    op_mismatch = sum(1 for code, rs in rows.items() for r in rs
                      if code in operators and operators[code][0] != r["row_op"])
    n_obsolete = sum(1 for rs in rows.values() for r in rs if r["obsolete"])

    def in_range(code, row, pos):
        if row["range"] <= 0:
            return True
        hs = hub_pos.get(code)
        if not hs:
            return True
        return min(haversine(pos, h) for h in hs) <= row["range"]

    pop = [s for s in stands if s[4] in STAND_OP_TO_CLASS and s[5]]
    empty_list = [s for s in stands if s[4] in STAND_OP_TO_CLASS and not s[5]]
    pop_airports = {s[0] for s in pop}

    MODES = [("all rules (range + Obsolete)", True, True),
             ("range OFF, Obsolete ON", False, True),
             ("Obsolete OFF, range ON", True, False),
             ("neither", False, False)]
    empty = {m[0]: [] for m in MODES}
    causes = Counter()
    diag_noop_empty = []       # neither rule AND no op-class match (closest to draft-7 method?)

    for s in pop:
        _ai, lat, lon, cls, sop, codes = s
        pos = (lat, lon)
        want = STAND_OP_TO_CLASS[sop]
        for name, use_range, use_obs in MODES:
            ok = False
            for c in codes:
                for r in rows.get(c, ()):
                    if r["cls"] != cls: continue
                    if use_obs and r["obsolete"]: continue
                    if not op_ok(c, r, want): continue
                    if use_range and not in_range(c, r, pos): continue
                    ok = True; break
                if ok: break
            if not ok:
                empty[name].append(s)
                if use_range and use_obs:
                    usable = [(c, r) for c in codes for r in rows.get(c, ())
                              if not r["obsolete"] and op_ok(c, r, want)]
                    if not usable: causes["no usable livery at all"] += 1
                    elif not any(r["cls"] == cls for _c, r in usable): causes["liveries, none at class X"] += 1
                    else: causes["class X exists, all out of range"] += 1
        if not any(r["cls"] == cls for c in codes for r in rows.get(c, ())):
            diag_noop_empty.append(s)

    n, na = len(pop), len(pop_airports)
    pct = lambda a, b: 100.0 * a / b if b else 0.0
    print(f"index: {args.index}")
    print(f"  operators {len(operators)}, livery rows {sum(len(v) for v in rows.values())}, "
          f"Obsolete rows {n_obsolete}, rows whose OP differs from operator record {op_mismatch}")
    print(f"  hubs unplaced: {len(unplaced)}" + (f"  e.g. {unplaced[:8]}" if unplaced else ""))
    print(f"apt.dat: {args.apt_dat}")
    print(f"  ramp starts (1300) {n_1300}, with 1301 {len(stands)}")
    print(f"  population (airline/cargo op, non-empty list): {n} stands at {na} airports")
    print(f"    of which airline {sum(1 for s in pop if s[4]=='airline')}, cargo {sum(1 for s in pop if s[4]=='cargo')}")
    print(f"  airline/cargo op with EMPTY list (excluded): {len(empty_list)}")
    other = Counter(s[4] for s in stands if s[4] not in STAND_OP_TO_CLASS and s[5])
    print(f"  other ops that carry airline codes: {dict(other)} (total {sum(other.values())})")
    print()
    print(f"{'mode':34s} {'empty stands':>20s} {'airports':>20s}")
    for name, _r, _o in MODES:
        es = empty[name]; ea = {s[0] for s in es}
        print(f"{name:34s} {len(es):>7d} / {n} {pct(len(es), n):5.1f}%   {len(ea):>5d} / {na} {pct(len(ea), na):5.1f}%")
    ea = {s[0] for s in diag_noop_empty}
    print(f"{'diag: neither, no op-class match':34s} {len(diag_noop_empty):>7d} / {n} {pct(len(diag_noop_empty), n):5.1f}%   "
          f"{len(ea):>5d} / {na} {pct(len(ea), na):5.1f}%")
    # Draft 7's population: every ramp start that carries airline codes, any ramp op.
    orig = [st for st in stands if st[5]]
    orig_e = [st for st in orig if not any(r["cls"] == st[3] for c in st[5] for r in rows.get(c, ()))]
    oa, oea = {st[0] for st in orig}, {st[0] for st in orig_e}
    print(f"{'diag: draft-7 pop (any op), class only':34s} {len(orig_e):>7d} / {len(orig)} {pct(len(orig_e), len(orig)):5.1f}%   "
          f"{len(oea):>5d} / {len(oa)} {pct(len(oea), len(oa)):5.1f}%")
    print()
    e1 = len(empty[MODES[0][0]])
    print("cause split, all rules:")
    for k in ("no usable livery at all", "liveries, none at class X", "class X exists, all out of range"):
        print(f"  {k:36s} {causes[k]:>7d}  {pct(causes[k], e1):5.1f}% of empty  {pct(causes[k], n):5.1f}% of stands")
    if args.by_mark:
        by_mark(pop, rows, op_ok, in_range, STAND_OP_TO_CLASS, set(map(id, empty[MODES[0][0]])),
                set(map(id, empty[MODES[2][0]])), pct)
    print(f"\nruntime {time.time() - t0:.1f} s")


if __name__ == "__main__":
    sys.exit(main())
