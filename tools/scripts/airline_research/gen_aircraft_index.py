# Scans X-Plane's default scenery for static aircraft objects and emits an
# airline-major index. Reports everything it could NOT resolve automatically.
import os, re, sys, collections

XP   = r"D:\X-Plane 12"
SCEN = os.path.join(XP, "Resources", "default scenery")
WEDL = r"C:\Users\Eric\Desktop\Laminar Misc Project\WED\xptools-livery\src\WEDLivery"

# ---------------------------------------------------------------- sibling data
def load_rows(path, ncol):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for l in f:
            l = l.rstrip("\n")
            if l.startswith("#") or "***" not in l: continue
            p = [x.strip() for x in l.split("***")]
            if len(p) >= ncol: out.append(p)
    return out

sizes_raw = {r[0]: r[1] for r in load_rows(os.path.join(WEDL, "WED_AircraftSizeReference.txt"), 2)}
sizes = {k: v for k, v in sizes_raw.items() if v.strip()}   # blank class = NOT a hit
airline_optype = {}
airlines = {}
for r in load_rows(os.path.join(WEDL, "WED_AirlineDirectory.txt"), 5):
    airlines.setdefault(r[0], (r[1], r[2], r[4]))   # code -> (name, country, fleet)
    airline_optype.setdefault(r[0], r[3])          # Pax | Cargo

# ---------------------------------------------------------------- library.txt
# vpath -> list of (real relative path, ratio)
exports = collections.defaultdict(list)
for lib in ("sim objects", "airport scenery"):
    p = os.path.join(SCEN, lib, "library.txt")
    if not os.path.exists(p): continue
    for l in open(p, encoding="utf-8", errors="replace"):
        f = l.split()
        if not f or not f[0].startswith("EXPORT"): continue
        ratio = 1
        if f[0] == "EXPORT_RATIO":
            if len(f) < 4: continue
            ratio = int(f[1]); vpath, real = f[2], f[3]
        else:
            if len(f) < 3: continue
            vpath, real = f[1], f[2]
        if not vpath.startswith("lib/airport/aircraft/"): continue
        exports[real].append((vpath, ratio, lib))

# ---------------------------------------------------------------- derivation
AIRLINE_SUFFIX = re.compile(r"_([a-z]{3})\.obj$")
CATEGORY = re.compile(r"lib/airport/aircraft/([^/]+)/")

# --------------------------------------------------- manual override tables
# Asset filename stem -> ICAO type designator. Only for assets whose own name
# is not an ICAO designator; everything else resolves automatically.
TYPE_ALIAS = {
    "ask_21": "AS21", "ventus_3": "VENT", "Cessna_172": "C172",
    "KingAirC90B": "BE9L", "747United": "B744",
    "FA_18E": "F18S",   # F/A-18E Super Hornet - F18H is the legacy Hornet
    "PA28":   "P28A",   # the ICAO code for the PA-28 family
    "Osprey_GP5": "XPGP5",   # homebuilt, no ICAO designator exists
    "MD80":   "MD82",
}
# Stopgap classes for designators not yet in WED_AircraftSizeReference.txt.
# Empty because everything currently referenced has landed upstream - keep the
# hook so a newly-shipped asset with an unknown type doesn't stall the build.
SIZE_PATCH = {}

# (type, folder-suffix) -> the real ICAO code of the operating subsidiary.
# A subsidiary is a DIFFERENT AIRLINE, not a livery variant of its parent - each
# of these has its own row in WED_AirlineDirectory.txt.
AIRLINE_REMAP = {
    ("B738", "RYR", "EI"): "RYR",   # Ryanair DAC - the Irish parent itself
    ("B738", "RYR", "G"):  "RUK",   # Ryanair UK
    ("B738", "RYR", "SP"): "RYS",   # Buzz
    ("B738", "RYR", "9H"): "MAY",   # Malta Air
}

# vpath category -> operation type, for WED's list filtering.
OPTYPE_BY_CAT = {
    "military": "Military", "cargo": "Cargo",
    "general_aviation": "GA", "GA": "GA", "gliders": "GA", "corporate_biz": "GA",
    "airliners": "Airline", "regional_jet": "Airline",
    "regional_prop": "Airline", "heavy_metal": "Airline",
}
OPTYPE_RANK = ["Military", "Cargo", "Airline", "GA"]   # most specific first

# tokens that appear where an airline code would sit but are NOT airlines
GENERIC_TOKENS = {"white", "blue", "coastguard", "usaf", "navy", "army", "ferrari",
                  "am", "static", "cft"}

def op_type_of(cats, airline):
    """Airline / Cargo / GA / Military - what WED filters the list by."""
    cand = {OPTYPE_BY_CAT[c] for c in cats if c in OPTYPE_BY_CAT}
    for rank in OPTYPE_RANK:
        if rank in cand:
            # a cargo carrier's airliner assets are Cargo, not Airline
            if rank == "Airline" and airline_optype.get(airline) == "Cargo":
                return "Cargo"
            return rank
    return "GA"

def derive(real, vpaths):
    """-> (airline, type, variant, cats, folder)."""
    cats = {CATEGORY.search(v).group(1) for v, _r, _l in vpaths if CATEGORY.search(v)}
    parts = real.replace("\\", "/").split("/")
    folder = parts[-2] if len(parts) >= 2 else ""
    fname  = parts[-1]

    # --- type: folder's leading token, else filename's, else the alias table --
    typ = None
    for cand in (folder.split("_")[0].upper(), fname.split("_")[0].split(".")[0].upper()):
        if cand in sizes_raw: typ = cand; break
    if typ is None:
        stem = re.sub(r"(_static)?\.obj$", "", fname)
        for k, v in TYPE_ALIAS.items():
            if stem == k or stem.startswith(k + "_") or folder.split("_")[0] == k:
                typ = v; break

    # --- airline + variant: prefer the FOLDER (TYPE_AIRLINE[_VARIANT]) --------
    airline, variant = None, None
    toks = folder.split("_")
    raw_head = toks[0].upper()
    # the folder's leading token identifies the type either directly or via the
    # alias table - compare in that direction, NOT raw_head == typ, or aliasing a
    # type silently breaks the airline parse for every folder using it.
    head_is_type = (raw_head == typ
                    or TYPE_ALIAS.get(toks[0]) == typ
                    or TYPE_ALIAS.get(raw_head) == typ)
    if len(toks) >= 2 and head_is_type:
        cand = toks[1].upper()
        if len(cand) == 3 and cand.isalpha() and toks[1].lower() not in GENERIC_TOKENS:
            airline = cand
            if len(toks) > 2:
                variant = "_".join(toks[2:])
                remap = (AIRLINE_REMAP.get((typ, cand, toks[2]))
                         or AIRLINE_REMAP.get((raw_head, cand, toks[2])))
                if remap: airline, variant = remap, None
        elif toks[1].lower() in GENERIC_TOKENS or cand.isdigit() or toks[1].startswith("BBJ"):
            variant = "_".join(toks[1:])
    # --- fall back to the vpath's _<icao> suffix ------------------------------
    if airline is None:
        for v, _r, _l in vpaths:
            m = AIRLINE_SUFFIX.search(v)
            if m: airline = m.group(1).upper(); break
    # --- last resort: filename's trailing English name (needs manual map) -----
    return airline, typ, normalize_variant(variant, fname), cats, folder

# ------------------------------------------------------- variant vocabulary
# Controlled vocabulary. Anything not recognised becomes Special_<token> so the
# value is always self-describing - there are no bare numbers in this column.
REG = re.compile(r"_((?:[A-Z]{1,2}[0-9]{3,5}|N[0-9]{1,5}[A-Z]{0,2}|"
                 r"[0-9][A-Z]{1,2}[A-Z]{3}|[A-Z]{1,2}-?[A-Z]{3,4}))(?:_static)?\.obj$")
AOC = {"9H": "Malta", "EI": "Ireland", "G": "UK", "SP": "Poland", "UK": "UK"}

def reg_of(fname):
    m = REG.search(fname)
    return m.group(1) if m else None

def normalize_variant(tok, fname):
    if not tok:
        return "Default"
    t = tok.strip("_")
    low = t.lower()
    if low in ("modern", "current", "new"):        return "Modern"
    if low in ("legacy", "old", "retro", "heritage"): return "Legacy"
    if low in ("mil", "vip", "gov"):               return "Government"
    if low in ("white", "blank", "generic", "static"): return "Default"
    if re.fullmatch(r"\d+", t):                    return "Default"   # MXD2, RLH1 style
    if "_" in t:                                   # Peony_5177 -> Special_Peony_B5177
        head, _, tail = t.partition("_")
        reg = reg_of(fname)
        return f"Special_{head}" + (f"_{reg}" if reg else f"_{tail}")
    return "Special_" + t

records = []            # (airline, type, variant, real, cat, ratio)
unresolved_type, unresolved_airline, non_airline = [], [], []
NON_AIRLINE_CATS = {"military", "general_aviation", "GA", "gliders",
                    "corporate_biz", "fighter", "helo"}
PSEUDO = {"military": "XPMI", "general_aviation": "XPGA", "GA": "XPGA",
          "gliders": "XPGL", "corporate_biz": "XPBZ",
          "cargo": "XPGN", "airliners": "XPGN", "heavy_metal": "XPGN",
          "regional_jet": "XPGN", "regional_prop": "XPGN"}

for real, vpaths in sorted(exports.items()):
    airline, typ, variant, cats, folder = derive(real, vpaths)
    cat = sorted(cats)[0] if cats else "?"
    ratio = max(r for _v, r, _l in vpaths)
    if airline is None:
        pseudo = next((PSEUDO[c] for c in sorted(cats) if PSEUDO.get(c)), None)
        if pseudo:
            airline = pseudo; non_airline.append((real, cat))
        elif "gliders" in real:
            airline = "XPGL"; non_airline.append((real, cat))
        else:
            unresolved_airline.append((real, cat, folder)); airline = "????"
    if typ is None:
        unresolved_type.append((real, folder, cat))
    records.append([airline, typ, variant, real, op_type_of(cats, airline), ratio])

# key collision detection (same airline+type+variant -> ambiguous key)
bykey = collections.defaultdict(list)
for r in records: bykey[(r[0], r[1], r[2])].append(r)
collisions = []
final = []
for (airline, typ, variant), rs in bykey.items():
    for i, r in enumerate(sorted(rs, key=lambda x: x[3])):
        v = variant
        if len(rs) > 1:                      # disambiguate by registration, not by index
            reg = reg_of(r[3].split("/")[-1])
            v = f"Special_{reg}" if reg else f"{variant}_DUP{i}"
        final.append((airline, typ, v, r[3], r[4], r[5]))
    if len(rs) > 1:
        collisions.append((airline, typ, variant, [x[3] for x in rs]))

# ---------------------------------------------------------------- orphans
on_disk = set()
root = os.path.join(SCEN, "sim objects", "apt_aircraft")
for dp, _dn, fn in os.walk(root):
    for f in fn:
        if f.lower().endswith(".obj"):
            rel = os.path.relpath(os.path.join(dp, f), os.path.join(SCEN, "sim objects"))
            on_disk.add(rel.replace("\\", "/"))
exported = {r.replace("\\", "/") for r in exports}
orphans = sorted(on_disk - exported)

# ---------------------------------------------------------------- report
print("=" * 70)
print("objs exported under lib/airport/aircraft/ :", len(exports))
print("objs on disk under apt_aircraft/          :", len(on_disk))
print("index rows produced                       :", len(final))
print()
print("auto-resolved airline (ICAO from vpath)   :", sum(1 for r in final if r[0] not in ("????",) and not r[0].startswith("XP")))
print("mapped to pseudo-code (non-airline)       :", len(non_airline))
print("NO airline resolvable                     :", len(unresolved_airline))
print("NO aircraft type resolvable               :", len(unresolved_type))
print("key collisions (same airline+type)        :", len(collisions))
print("orphans on disk, never exported           :", len(orphans))
print("=" * 70)

def dump(title, rows, n=25):
    print("\n---", title, f"({len(rows)})")
    for r in rows[:n]: print("   ", r)
    if len(rows) > n: print(f"    ... +{len(rows)-n} more")

dump("NO airline resolvable", unresolved_airline)
dump("NO aircraft type resolvable", unresolved_type)
dump("key collisions", collisions)
dump("orphans", orphans)

unknown_airlines = sorted({r[0] for r in final
                           if r[0] not in airlines and not r[0].startswith("XP") and r[0] != "????"})
dump("airline codes NOT in WED_AirlineDirectory.txt", unknown_airlines)

# ---------------------------------------------------------------- emit index
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "WED_StaticAircraftIndex.txt")
groups = collections.defaultdict(list)
for a, t, v, real, cat, ratio in final: groups[a].append((t, v, real, cat, ratio))

with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("I\n1 WED Aviation Database\n")
    f.write("# WED Static Aircraft Index - GENERATED, do not hand-edit.\n")
    f.write("# Airline header : <ICAO> *** <Name> *** <Country> *** <Fleet size>\n")
    f.write("# Aircraft row   : \\t<TYPE> *** <CLASS> *** <OP TYPE> *** <VARIANT> *** <path>\n")
    f.write("#\n")
    f.write("# OP TYPE is Airline | Cargo | GA | Military - what WED filters the\n")
    f.write("# recommendation list by, per the user's ramp-operation setting.\n")
    f.write("# Lookup key     : <TYPE>_<ICAO>_<VARIANT>   (what apt.dat stores)\n")
    f.write("#\n")
    f.write("# CLASS is the ICAO wingspan class A-F, joined from\n")
    f.write("# WED_AircraftSizeReference.txt by TYPE. It is duplicated here on purpose:\n")
    f.write("# this file is machine-facing and the consumer must not need a second join\n")
    f.write("# to size a stand. Drift is impossible because this file is generated - if\n")
    f.write("# a class is corrected upstream, regenerating propagates it.\n")
    f.write("# '?' means the TYPE is absent from the size reference - needs adding there.\n")
    f.write("#\n")
    f.write("# VARIANT vocabulary: Default | Modern | Legacy | Government |\n")
    f.write("#   Subsidiary_<country> | Special_<token>   (reserved: Star_Alliance,\n")
    f.write("#   OneWorld, SkyTeam - none present in the current asset set)\n#\n")
    for a in sorted(groups):
        name, country, fleet = airlines.get(a, ("?", "?", "?"))
        f.write(f"\n{a} *** {name} *** {country} *** {fleet}\n")
        for t, v, real, cat, ratio in sorted(groups[a], key=lambda x: (x[0] or "~", x[1], x[2])):
            short = real.replace("apt_aircraft/", "")
            cls = (sizes.get(t) or SIZE_PATCH.get(t, "?")) if t else "?"
            f.write(f"\t{t or '????'} *** {cls} *** {cat} *** {v} *** {short}\n")

# class cross-check report
missing_cls = sorted({t for _a, t, _v, _r, _c, _x in final
                      if t and t not in sizes and t not in SIZE_PATCH})
print(f"\nsize reference: {len(sizes_raw)} rows, {len(sizes)} with a class, "
      f"{len(sizes_raw)-len(sizes)} BLANK")
print("\n--- op type distribution")
for k, n in collections.Counter(r[4] for r in final).most_common(): print(f"    {k:<10} {n}")
dump("TYPE present in assets but MISSING from WED_AircraftSizeReference.txt", missing_cls)
vocab = collections.Counter(r[2].split("_")[0] for r in final)
print("\n--- variant vocabulary in use")
for k, n in vocab.most_common(): print(f"    {k:<12} {n}")
print("\nwrote", out)
