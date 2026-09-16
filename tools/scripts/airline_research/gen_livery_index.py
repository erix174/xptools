# First-pass generator for the X-Plane-side livery index.
#
# This file is meant to be MANUALLY MAINTAINED from here on - this script only
# bootstraps it from what can be read off the shipped assets, and marks every
# cell it could not determine so a human can fill it in. Re-running it should be
# a diff-review, never a blind overwrite.
import os, re, sys, collections

XP    = r"D:\X-Plane 12"
ROOT  = os.path.join(XP, "Resources", "default scenery", "sim objects", "apt_aircraft")
SCEN  = os.path.join(XP, "Resources", "default scenery")
OUT   = os.path.join(os.path.dirname(os.path.abspath(__file__)), "livery_index.txt")

# --------------------------------------------------------------- IOC country by
# ICAO nationality registration prefix. Filenames drop the dash, so this is a
# LONGEST-MATCH table: "CGXTB" -> "C-" (Canada), "9MLDC" -> "9M-" (Malaysia).
# Only prefixes actually seen in the shipped assets are listed; anything else
# falls through to "???" and gets flagged rather than guessed.
PREFIX_IOC = {
    "N": "USA",   "C": "CAN",  "G": "GBR",  "D": "GER",  "F": "FRA",
    "I": "ITA",   "EC": "ESP", "PH": "NED", "OO": "BEL", "OE": "AUT",
    "HB": "SUI",  "SE": "SWE", "LN": "NOR", "OY": "DEN", "OH": "FIN",
    "TF": "ISL",  "EI": "IRL", "SP": "POL", "OK": "CZE", "OM": "SVK",
    "HA": "HUN",  "YR": "ROU", "LZ": "BUL", "SX": "GRE", "TC": "TUR",
    "9H": "MLT",  "CS": "POR", "UR": "UKR", "RA": "RUS", "RF": "RUS",
    "4X": "ISR",  "A6": "UAE", "A7": "QAT", "A9C": "BRN", "HZ": "KSA",
    "JY": "JOR",  "SU": "EGY", "7T": "ALG", "CN": "MAR", "TS": "TUN",
    "ET": "ETH",  "5Y": "KEN", "ZS": "RSA", "9J": "ZAM",
    "VT": "IND",  "AP": "PAK", "S2": "BAN", "4R": "SRI", "8Q": "MDV",
    "B": "CHN",   "JA": "JPN", "HL": "KOR", "9V": "SIN", "9M": "MAS",
    "PK": "INA",  "HS": "THA", "XU": "CAM", "RDPL": "LAO", "VN": "VIE",
    "RP": "PHI",  "VH": "AUS", "ZK": "NZL", "DQ": "FIJ",
    "XA": "MEX",  "XB": "MEX", "XC": "MEX", "LV": "ARG", "PP": "BRA",
    "PR": "BRA",  "PT": "BRA", "PS": "BRA", "CC": "CHI", "HK": "COL",
    "OB": "PER",  "YV": "VEN", "CU": "CUB", "TI": "CRC", "HP": "PAN",
    # Hong Kong shares the B- prefix with mainland China but always follows it
    # with a letter (B-KJH), where the mainland uses digits (B-1995). Listing the
    # two-character forms lets longest-match separate them without a special case.
    "BK": "HKG",  "BH": "HKG", "BL": "HKG", "BM": "MAC",
    "YI": "IRQ",  "A4O": "OMA", "A40": "OMA", "5A": "LBA", "ST": "SUD",
    "EK": "ARM",  "4L": "GEO", "UK": "UZB", "UP": "KAZ", "EY": "TJK",
}
# Prefixes that are genuinely ambiguous and must be reviewed by hand rather than
# trusted: B- covers mainland China, Taiwan, Hong Kong and Macau.
AMBIGUOUS = {"B"}

# Tokens that sit where a registration would but are not one.
NOT_A_REG = re.compile(r"^(BBJ\d*|FICTION|STATIC|WINGLET|MODERN|LEGACY)$")

def ioc_for_reg(reg):
    """-> (ioc, confident). '???' when nothing matches, '' when not a reg at all."""
    if NOT_A_REG.match(reg):
        return "", True, ""      # caller drops the reg entirely
    for n in (4, 3, 2, 1):
        p = reg[:n]
        if p not in PREFIX_IOC:
            continue
        # A one-letter nationality prefix needs a long body, or three-letter
        # model tokens like "BBJ1" get read as Chinese registrations.
        if n == 1 and len(reg) - n < 4:
            continue
        return PREFIX_IOC[p], p not in AMBIGUOUS, p
    return "???", False, ""

# --------------------------------------------------------------- livery note
# Controlled vocabulary, same spirit as the index's VARIANT column.
# NOTE is FREE TEXT, deliberately not a closed enum. WED renders anything other
# than "Default" in parentheses after the airline name - "United (Retro)",
# "Air China (Pink Peony)", "Hainan Airlines (Mixue)" - so making a caption more
# specific is a one-word edit to this column and needs no code change. The rules
# below only seed a first guess from the folder name; a human is expected to
# replace "Peony" with "Pink Peony" and so on.
NOTE_RULES = [
    (r"legacy|retro|heritage",     "Retro"),
    (r"star.?alliance",            "Star Alliance"),
    (r"oneworld",                  "OneWorld"),
    (r"skyteam",                   "SkyTeam"),
    (r"modern|current|_new",       "Default"),
    (r"vip|_mil|government",     "Government"),
    (r"peony",                     "Peony"),
    (r"peacock",                   "Peacock"),
    (r"mixue",                     "Mixue"),
    (r"panda",                     "Panda"),
    (r"salmon",                    "Salmon"),
    (r"white|blank|generic|house", "Unpainted"),
    (r"fiction",                   "Fictional"),
]

def note_for(folder, fname):
    hay = (folder + " " + fname).lower()
    for pat, note in NOTE_RULES:
        if re.search(pat, hay):
            return note
    return "Default"

# --------------------------------------------------------------- asset scan
REG = re.compile(r"_([A-Z0-9]{4,7})(?:_static)?\.obj$")
AIRLINE_SUFFIX = re.compile(r"_([a-z]{3})\.obj$")

# real path -> the vpaths library.txt publishes it under (for the airline code)
exports = collections.defaultdict(list)
for lib in ("sim objects", "airport scenery"):
    p = os.path.join(SCEN, lib, "library.txt")
    if not os.path.exists(p): continue
    for l in open(p, encoding="utf-8", errors="replace"):
        f = l.split()
        if not f or not f[0].startswith("EXPORT"): continue
        vpath, real = (f[2], f[3]) if f[0] == "EXPORT_RATIO" else (f[1], f[2])
        if vpath.startswith("lib/airport/aircraft/"):
            exports[real.replace("\\", "/")].append(vpath)

WEDL = "C:/Users/Eric/Desktop/Laminar Misc Project/WED/xptools-livery/src/WEDLivery"

def _load_rows(path, ncol):
    out = []
    for l in open(path, encoding="utf-8", errors="replace"):
        l = l.rstrip()
        if l.startswith("#") or "***" not in l: continue
        q = [x.strip() for x in l.split("***")]
        if len(q) >= ncol: out.append(q)
    return out

# type -> wingspan class. A key present with a BLANK value is a deliberate
# "not yet researched" marker (see that file's own header), so sizes_raw says
# whether a designator exists at all and `sizes` only holds real classes.
sizes_raw = {r[0]: r[1] for r in _load_rows(os.path.join(WEDL, "WED_AircraftSizeReference.txt"), 2)}
sizes     = {k: v for k, v in sizes_raw.items() if v.strip()}

# code -> (name, IOC country, fleet size)
airlines = {}
for r in _load_rows(os.path.join(WEDL, "WED_AirlineDirectory.txt"), 5):
    airlines.setdefault(r[0], (r[1], r[2], r[4]))

# Asset filename stem -> ICAO type designator, for the assets whose own name is
# not one. Everything else resolves from the folder or filename directly.
TYPE_ALIAS = {
    "ask_21": "AS21", "ventus_3": "VENT", "Cessna_172": "C172",
    "KingAirC90B": "BE9L", "747United": "B744",
    "FA_18E": "F18S",        # F/A-18E Super Hornet - F18H is the legacy Hornet
    "PA28":   "P28A",        # the ICAO code for the PA-28 family
    "Osprey_GP5": "XPGP5",   # homebuilt, no ICAO designator exists
    "MD80":   "MD82",
}

# (type, folder token, suffix) -> the real ICAO code of the operating subsidiary.
# A subsidiary is a DIFFERENT AIRLINE, not a livery of its parent - each of these
# has its own row in WED_AirlineDirectory.txt.
AIRLINE_REMAP = {
    ("B738", "RYR", "EI"): "RYR",   # Ryanair DAC - the Irish parent itself
    ("B738", "RYR", "G"):  "RUK",   # Ryanair UK
    ("B738", "RYR", "SP"): "RYS",   # Buzz
    ("B738", "RYR", "9H"): "MAY",   # Malta Air
}

# An ambiguous nationality prefix (B- is shared by mainland China, Taiwan, Hong
# Kong and Macau) is resolved by asking WED_AirlineDirectory.txt what country the
# OPERATOR is in. A Chinese carrier's B- aircraft is Chinese; China Airlines' is
# Taiwanese. Only when the operator is unknown does the row stay flagged.
# IOC codes, not ISO: Hong Kong is HKG, Taiwan is TPE, Macau is MAC. The whole
# data family uses IOC (see WED_IocCountryCodes.h), so this must too.
AMBIG_OK = {"B": {"CHN", "TPE", "HKG", "MAC"}}

def disambiguate(prefix, ioc, airline):
    allowed = AMBIG_OK.get(prefix)
    if not allowed or airline not in airlines:
        return ioc, False
    op_country = airlines[airline][1]
    return (op_country, True) if op_country in allowed else (ioc, False)

# Registrations read off the textures by hand - see the file's own header. These
# win over anything derived from a filename, and an EMPTY value there is a
# positive "inspected, no tail number painted" finding, not a gap.
OVERRIDES = {}
_ovr = os.path.join(os.path.dirname(os.path.abspath(__file__)), "livery_reg_overrides.txt")
if os.path.exists(_ovr):
    for l in open(_ovr, encoding="utf-8"):
        if l.startswith("#") or "***" not in l: continue
        k, _, v = l.partition("***")
        OVERRIDES[k.strip()] = v.strip()

rows, flagged = [], []
for dp, _dn, fn in os.walk(ROOT):
    for f in sorted(fn):
        if not f.lower().endswith(".obj"): continue
        folder = os.path.basename(dp)
        rel = os.path.relpath(os.path.join(dp, f), ROOT).replace("\\", "/")
        full = "apt_aircraft/" + rel

        # type: folder head, else filename head, else the alias table
        typ = None
        for cand in (folder.split("_")[0].upper(), f.split("_")[0].split(".")[0].upper()):
            if cand in sizes_raw: typ = cand; break
        if typ is None:
            stem = re.sub(r"(_static)?\.obj$", "", f)
            for k, v in TYPE_ALIAS.items():
                if stem == k or stem.startswith(k + "_") or folder.split("_")[0] == k:
                    typ = v; break

        # airline: folder's second token if it looks like a code, else the vpath suffix
        airline = None
        toks = folder.split("_")
        # Folders named <TYPE>_<PARENT>_<AOC> are separate operating companies,
        # not liveries of the parent - each has its own ICAO code and its own row
        # in WED_AirlineDirectory.txt - see AIRLINE_REMAP above.
        remap = AIRLINE_REMAP.get((toks[0].upper(),
                                   toks[1].upper() if len(toks) > 1 else "",
                                   toks[2] if len(toks) > 2 else ""))
        if remap:
            airline = remap
        if airline is None and len(toks) >= 2 and len(toks[1]) == 3 and toks[1].isalpha() and toks[1].isupper():
            airline = toks[1]
        if airline is None:
            for v in exports.get(full, []):
                m = AIRLINE_SUFFIX.search(v)
                if m: airline = m.group(1).upper(); break
        if airline is None:
            cats = {v.split("/")[3] for v in exports.get(full, []) if v.count("/") > 3}
            if not cats:
                # Orphan: on disk but library.txt never exports it, so there is no
                # vpath to read a category off. The apt_aircraft/<category>/ folder
                # says the same thing.
                cats = {{"fighter": "military", "helo": "general_aviation",
                         "prop": "general_aviation"}.get(rel.split("/")[0], "airliners")}
            for c, pseudo in (("military", "XPMI"), ("gliders", "XPGL"),
                              ("general_aviation", "XPGA"), ("GA", "XPGA"),
                              ("corporate_biz", "XPBZ")):
                if c in cats: airline = pseudo; break
            else:
                if cats: airline = "XPGN"      # generic/unpainted airliner or cargo

        m = REG.search(f)
        reg = m.group(1) if m else ""
        if rel in OVERRIDES:
            reg = OVERRIDES[rel]
        ioc, confident, prefix = ioc_for_reg(reg) if reg else ("", True, "")
        if reg and not confident and airline:
            ioc, confident = disambiguate(prefix, ioc, airline)
        if reg and not ioc and confident:
            reg = ""                                  # it was never a registration
        note = note_for(folder, f)

        if typ is None or airline is None or (reg and not confident) or ioc == "???":
            flagged.append((full, typ, airline, reg, ioc))
        cls = sizes.get(typ, "?") if typ else "?"
        rows.append((typ or "????", cls, airline or "????", reg, ioc, note, rel))

# --------------------------------------------------------------- emit
rows.sort(key=lambda r: (r[0], r[2], r[6]))
with open(OUT, "w", encoding="utf-8", newline="\n") as o:
    o.write("I\n1 WED Aviation Database\n")
    o.write("""# X-Plane Static Livery Index
# ============================================================================
# WHAT THIS FILE IS: the authoritative catalogue of every static-aircraft
# livery shipped with X-Plane. It is the one place that says "this .obj is a
# B738, operated by United, registered N78540 in the USA, wearing the retro
# livery" - facts that are NOT recoverable from library.txt, which only groups
# objects into (operation type, size class, airline) buckets and has no
# aircraft-type or livery axis at all.
#
# WHY IT LIVES HERE AND NOT IN library.txt: library.txt describes how the sim
# PICKS an object. This file describes WHAT an object is. Those change for
# different reasons and on different schedules - library.txt is regenerated
# whenever the bucket scheme changes, while this file survives that, and will
# also survive a future move to glTF where one model carries many liveries.
# It sits next to the assets it describes so that any PR adding a livery is
# already touching this directory.
#
# MANUALLY MAINTAINED. Bootstrapped by tools/scripts/airline_research/
# gen_livery_index.py from what could be read off the shipped filenames, but
# from here on it is edited by hand. Re-running the generator is a
# diff-review, never a blind overwrite.
#
# FORMAT: <TYPE> *** <CLASS> *** <AIRLINE> *** <REG> *** <REG COUNTRY> *** <NOTE> *** <path>
#   TYPE        ICAO type designator (B738, A21N, ...).
#   CLASS       ICAO wingspan class A-F - which ramp size this aircraft needs.
#               Carried HERE rather than in a separate type->class file on
#               purpose: at this data volume a second file buys nothing and
#               costs a hard dependency, where one changed designator stalls
#               both files at once. One row, all the facts about one livery.
#   AIRLINE     ICAO airline code, or a reserved XP-prefixed pseudo code for
#               non-airline assets (military, GA, generic/unpainted).
#   REG         Registration WITHOUT the dash, as it appears in the filename.
#               Empty is legal - older assets simply have no registration.
#   REG COUNTRY IOC 3-letter code of the country of registration. Empty when
#               REG is empty.
#   NOTE        Default | Retro | Special | Alliance | Government | Unpainted
#   path        Relative to apt_aircraft/.
#
# "????" in any column means the bootstrap could not determine it and a human
# must. It is a TODO marker, not a value - nothing should ever ship with one.
# ============================================================================

""")
    # Column-pad so the file is scannable by eye. Safe because the parser
    # tokenizes on "***" rather than matching a fixed " *** " separator, so the
    # extra spaces cost nothing - same convention as WED_AirportDatabase.cpp.
    widths = [max(len(r[i]) for r in rows) for i in range(len(rows[0]) - 1)]
    for r in rows:
        cells = [r[i].ljust(widths[i]) for i in range(len(widths))] + [r[-1]]
        o.write(" *** ".join(cells) + "\n")

print(f"rows written          : {len(rows)}")
print(f"rows needing a human  : {len(flagged)}")
print(f"note distribution     : {dict(collections.Counter(r[5] for r in rows))}")
print(f"with registration     : {sum(1 for r in rows if r[3])}")
print(f"\nwrote {OUT}")
for x in flagged[:15]: print("   flag:", x)
if len(flagged) > 15: print(f"   ... +{len(flagged)-15} more")
