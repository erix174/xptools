# First-pass generator for the X-Plane-side livery index.
#
# This file is meant to be MANUALLY MAINTAINED from here on - this script only
# bootstraps it from what can be read off the shipped assets, and marks every
# cell it could not determine so a human can fill it in. Re-running it should be
# a diff-review, never a blind overwrite.
import os, re, sys, collections
import datetime as _dt


def xplane_build(root):
    """Version string of the install this index describes, or 'unknown'."""
    log = os.path.join(root, "Log.txt")
    if os.path.exists(log):
        try:
            with open(log, encoding="utf-8", errors="replace") as f:
                m = re.search(r"X-Plane (\d+\.\d+[\w.-]*)", f.readline())
                if m:
                    return m.group(1)
        except Exception:
            pass
    return "unknown"

from wed_paths import xplane_root, apt_aircraft, default_scenery, wed_livery_dir

XP    = xplane_root()          # argv[1] or $XPLANE_ROOT; validated, never guessed
ROOT  = apt_aircraft(XP)
SCEN  = default_scenery(XP)
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
    "B": "CHN",   "JA": "JPN", "HL": "KOR", "9V": "SGP", "9M": "MAS",
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
    "EZ": "TKM",  "4K": "AZE", "LX": "LUX",
}
# EVERY VALUE ABOVE MUST BE A CODE WED_AirlineDirectory.txt ALSO USES. The two
# files are joined on it - the directory gives the operator's country, this table
# gives the registration's, and a mismatch reads as "foreign-registered aircraft"
# rather than as a typo. "9V": "SIN" sat here until 2026-09-17 making every
# Singapore Airlines livery look foreign-registered, because SIN is the airport
# code and the directory quite correctly says SGP.
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
        #
        # Except for N. Every other one-letter prefix is followed by four
        # letters (G-ABCD, D-AIXB, F-GRJC), but a US registration is N plus a
        # DIGIT plus up to four more, and the short ones are real: N7ER was
        # being thrown away as too short and shipped a "???" country. Requiring
        # that digit is what keeps the exception narrow - "BBJ1" still fails
        # here, because its prefix is B and its body does not start with one.
        if n == 1 and len(reg) - n < 4 and not (p == "N" and reg[1:2].isdigit()):
            continue
        return PREFIX_IOC[p], p not in AMBIGUOUS, p
    return "???", False, ""

# --------------------------------------------------------------- livery note
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

def note_for(folder, fname, raw_variant=None):
    """The livery note. NOTE_RULES only NORMALISE names we recognise; anything
    else is kept as the asset itself spelled it.

    Falling back to "Default" for an unrecognised token was a real regression:
    Nine Air's eleven colours (Blue, Cyan, DarkGreen, Grape, KellyGreen, Purple,
    Red, Wine, Yellow, Canaloupe, Modern) all collapsed into one indistinguishable
    "Default", which is precisely the data this file exists to carry. The note
    column is an OPEN vocabulary - the same reason type exclusions are subtractive.
    A closed fallback throws away exactly the variants nobody thought to enumerate.
    """
    hay = (folder + " " + fname).lower()
    for pat, note in NOTE_RULES:
        if re.search(pat, hay):
            return note

    # Nothing matched: keep the asset's own variant token if the folder had one.
    if raw_variant:
        t = raw_variant.strip("_").replace("_", " ").strip()
        if t:
            return t[:1].upper() + t[1:]

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

# BOOTSTRAP DATA, NOT RUNTIME DATA. These two tables are consulted only for an
# operator or a type the index does not already know - a livery X-Plane ships
# for the first time. Nothing at run time reads them; livery_index.txt is the
# single source of truth for WED and the sim alike, and it is maintained by
# hand. Re-running this script MERGES: every row and operator record already in
# the index is kept exactly as it is, rows for assets that no longer exist are
# dropped, and only genuinely new assets get a guessed row for a human to check.
WEDL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bootstrap")

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
_size_rows = _load_rows(os.path.join(WEDL, "WED_AircraftSizeReference.txt"), 2)
sizes_raw = {r[0]: r[1] for r in _size_rows}
sizes     = {k: v for k, v in sizes_raw.items() if v.strip()}
# type -> typical operating range, km. Optional third column; absent = unknown,
# and unknown is never filtered downstream - see that file's header.
ranges    = {r[0]: r[2].strip() for r in _size_rows if len(r) >= 3 and r[2].strip().isdigit()}

# code -> (name, IOC country, fleet size)
airlines = {}
hub_icaos = {}      # code -> [ICAO, ...]; optional sixth column
for r in _load_rows(os.path.join(WEDL, "WED_AirlineDirectory.txt"), 5):
    airlines.setdefault(r[0], (r[1], r[2], r[4], r[3]))     # name, IOC country, fleet, op class
    if len(r) >= 6 and r[5].strip():
        hub_icaos.setdefault(r[0], r[5].split())

# ICAO -> (lat, lon) for every hub named on an OPERATOR record, read off the
# install's Global Airports apt.dat. A LINT, not an output: since schema 4 the
# index carries hub ICAOs only, and WED and the sim place them themselves at
# load. This tells the maintainer, on every run, which hubs neither reader will
# be able to place. Same lookup the readers use: an airport whose 1302 icao_code
# names the hub wins over one whose header ident does - Ezhou (ZHEC) and
# Chengdu Tianfu (ZUTF) sit under placeholder idents, and the ident ZSQD is the
# closed Liuting while icao_code ZSQD is the new Jiaodong. Position: the 1302
# datum, else the midpoint of the first runway.
def resolve_hubs(xp_root, wanted):
    path = os.path.join(xp_root, "Global Scenery", "Global Airports", "Earth nav data", "apt.dat")
    by_ident, by_code = {}, {}
    if not os.path.exists(path):
        print(f"WARNING: no Global Airports apt.dat at {path} - hubs not checked")
        return {}
    ident = code = None; lat = lon = None; rwy = None
    def flush():
        want_i, want_c = ident in wanted, code in wanted
        if not (want_i or want_c): return
        ll = (lat, lon) if lat is not None and lon is not None else rwy
        if ll is None: return
        if want_c: by_code.setdefault(code, ll)
        if want_i: by_ident.setdefault(ident, ll)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            # 1 land airport, 16 seaplane base, 17 heliport: each starts a block.
            head = line.split(None, 1)[0] if line[:1].isdigit() else ""
            if head in ("1", "16", "17"):
                flush()
                t = line.split(); ident = t[4] if len(t) > 4 else None
                code = None; lat = lon = None; rwy = None
            elif ident is not None:
                if line.startswith("1302 "):
                    t = line.split()
                    if len(t) >= 3:
                        if   t[1] == "icao_code": code = t[2].upper()
                        elif t[1] == "datum_lat": lat = float(t[2])
                        elif t[1] == "datum_lon": lon = float(t[2])
                elif line.startswith("100 ") and rwy is None:
                    t = line.split()
                    try: rwy = ((float(t[9]) + float(t[18])) / 2, (float(t[10]) + float(t[19])) / 2)
                    except (IndexError, ValueError): pass
    flush()
    by_ident.update(by_code)
    return by_ident

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


# "Obsolete" is a NOTE value on the row (R25). A guessed row never carries one:
# marks are made by hand in the index, and the merge keeps them.

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
                              ("corporate_biz", "XPGA")):
                if c in cats: airline = pseudo; break
            else:
                if cats: airline = "XPZZ"      # generic/unpainted airliner or cargo

        m = REG.search(f)
        reg = m.group(1) if m else ""
        ioc, confident, prefix = ioc_for_reg(reg) if reg else ("", True, "")
        if reg and not confident and airline:
            ioc, confident = disambiguate(prefix, ioc, airline)
        if reg and not ioc and confident:
            reg = ""                                  # it was never a registration
        # The folder is <TYPE>_<AIRLINE>_<VARIANT...>; hand the variant part to
        # note_for so an unrecognised one survives instead of collapsing to
        # "Default" - see that function.
        #
        # Except when AIRLINE_REMAP consumed that token: for B738_RYR_9H the "9H"
        # IS the subsidiary, already turned into the airline MAY, and passing it
        # on would put "9H" in the note column as though it were a livery name.
        _toks = folder.split("_")
        _remapped = AIRLINE_REMAP.get((_toks[0].upper(),
                                       _toks[1].upper() if len(_toks) > 1 else "",
                                       _toks[2] if len(_toks) > 2 else ""))
        if _remapped:
            _raw = None
        else:
            # Where the variant starts depends on whether the folder names an
            # airline at all. <TYPE>_<AIRLINE>_<VARIANT> for an operator's livery,
            # but <TYPE>_<VARIANT> for a generic one - C172_skyhawk, C172_waves,
            # B738_BBJ1. Assuming the three-token shape collapsed every generic
            # variant into "Default", losing exactly the distinction they exist to
            # make.
            _airline_in_folder = (len(_toks) > 1 and len(_toks[1]) == 3
                                  and _toks[1].isalpha() and _toks[1].isupper())
            _start = 2 if _airline_in_folder else 1
            _raw = "_".join(_toks[_start:]) if len(_toks) > _start else None
        note = note_for(folder, f, _raw)

        if typ is None or airline is None or (reg and not confident) or ioc == "???":
            flagged.append((full, typ, airline, reg, ioc))
        cls = sizes.get(typ, "?") if typ else "?"

        # GENERIC AND HOUSE LIVERIES ARE CLASSED BY THE AIRCRAFT, NOT THE FOLDER.
        # The folder scan put four unmarked 757s under military, Piaggios under
        # "generic airliner", BBJs and a white DC-10 under general aviation, and
        # Boeing's three demonstrators on one "operator" card. The ramp's op-type
        # filter reads these codes, so each has to mean what it says:
        #   XPGA  general aviation            (light aircraft AND business jets -
        #                                      the ramp has one GA operation type,
        #                                      so splitting them bought nothing)
        #   XPZZ  generic / unpainted airliner (class C and up, no BBJ marking)
        # An UNPAINTED airframe goes to the pseudo-operator matching what it IS,
        # not to one "white" bucket: a white light aircraft is XPGA, a white
        # airliner is XPZZ, a bare military airframe is XPMI.
        # Boeing's house aircraft are the definition of generic and go the same way.
        stem = os.path.splitext(f)[0]
        if stem in ("757PW_static", "757PW_winglet_static", "757RR_static", "757RR_winglet_static"):
            airline = "XPZZ"                       # unmarked airliners misfiled under military
        elif stem == "757_KAF_5701":
            airline = "KAF"                        # Kazakhstan Air Force - a real operator, added to the directory
        if airline in ("XPGA", "XPZZ", "BOE"):
            if "BBJ" in (note or "") or "BBJ" in stem.upper() or (airline == "BOE" and cls in "AB"):
                airline = "XPGA"
            elif cls in "AB":
                airline = "XPGA"
            else:
                airline = "XPZZ"

        # One generic airliner per TYPE: XPZZ_B752, XPZZ_DC10. A stand lists the
        # white airframe it means, and 1301 carries it like any other code.
        if airline == "XPZZ" and typ:
            airline = "XPZZ_" + typ

        # SCOPE is empty on a guessed row: HOME is a human's call, made in the index.
        rows.append((typ or "????", cls, airline or "????", reg, ioc, note,
                     ranges.get(typ, "") if typ else "", "", rel))

# ------------------------------------------------------------------ MERGE
# The index is the source of truth. Read what it already says and prefer it.
existing_rows = {}      # path -> cells (schema 3: 10 cells, OP at index 8)
existing_ops  = {}      # code -> [name, cty, op, fleet, hub icaos]
if os.path.exists(OUT):
    for l in open(OUT, encoding="utf-8", errors="replace"):
        if l.startswith("#") or "***" not in l: continue
        q = [x.strip() for x in l.split("***")]
        if q[0] == "OPERATOR" and len(q) >= 6:
            existing_ops[q[1]] = q[2:7] + [""] * (5 - len(q[2:7]))
        elif len(q) >= 9:
            if len(q) == 9: q.insert(8, "")             # schema 2 row: no OP yet
            existing_rows[q[-1].replace("\\", "/")] = q

# operator facts: the index first, the bootstrap directory only for a newcomer
for code, rec in existing_ops.items():
    airlines[code]  = (rec[0], rec[1], rec[3], rec[2])
    hub_icaos[code] = rec[4].split()
# Bare XPZZ is retired: the generic airliners are one record per type now.
existing_ops.pop("XPZZ", None)
airlines.pop("XPZZ", None)

# Military and government rows are never range-checked (they park at home, or
# anywhere), so hubs on those records would be data nothing reads. Say so and
# drop them rather than keep a column that looks meaningful and is not.
_mil_hubs = sorted(c for c, rec in existing_ops.items() if rec[2] in ("Military", "Gov") and rec[4])
for c in _mil_hubs:
    existing_ops[c][4] = ""
if _mil_hubs:
    print(f"military/gov hubs dropped (never read): {' '.join(_mil_hubs)}")

_wanted = {i for c, rec in existing_ops.items() for i in rec[4].split()}
_placed = resolve_hubs(XP, _wanted)
_unres  = sorted(_wanted - set(_placed))
print(f"hubs                  : {len(_placed)} of {len(_wanted)} ICAOs placed by Global Airports"
      + ("" if not _unres else f"  ** not found: {' '.join(_unres)} **"))

def op_class_for(code):
    if code in PSEUDO_OP: return PSEUDO_OP[code]
    if code.startswith("XPZZ_"): return "Pax"
    a = airlines.get(code)
    return (a[3] if a and len(a) > 3 and a[3] else "Pax")

PSEUDO_OP = {"XPGA": "GA", "XPMI": "Military", "XPZZ": "Pax"}

# Both sections are grouped by operation class before anything else, in the
# order a person looks for them: the airlines that fill most stands, then the
# freighters, then general aviation, then the military, then whatever is left
# (today: government). Alphabetical inside each group. The two sections are
# sorted the same way but stay physically separate - operators first, liveries
# after - so the groups never interleave across the section boundary.
CLASS_ORDER = {"Pax": 0, "Cargo": 1, "GA": 2, "Military": 3}
def class_rank(op):
    return CLASS_ORDER.get(op, 4)

def class_banner(op):
    """Group header inside a section. A comment line, so every reader already
    skips it; it exists purely so a human can find the freighters without
    scrolling past a thousand airlines."""
    label = op or "(unclassified)"
    return "# -- " + label + " " + "-" * max(1, 74 - len(label)) + "\n"

merged, kept, added = [], 0, 0
for r in rows:
    rel = r[-1].replace("\\", "/")
    if rel in existing_rows:
        q = existing_rows[rel]
        # keep the hand-maintained cells. Two are derived and refreshed:
        #   SCOPE - HOME or empty. Schema 3 kept hub coordinates here.
        #   OP    - always the operator record's class; the row only repeats it,
        #           and a copy that is allowed to drift is how four rows ended up
        #           disagreeing with their own records.
        if q[2] == "XPZZ" and q[0] != "????":
            q[2] = "XPZZ_" + q[0]
        q[7] = "HOME" if q[7] == "HOME" else ""
        q[8] = op_class_for(q[2])
        merged.append(tuple(q)); kept += 1
    else:
        r = list(r)
        r.insert(8, op_class_for(r[2]))
        merged.append(tuple(r)); added += 1
dropped = len(existing_rows) - kept
rows = merged
# Sort AFTER the merge, not before. The pre-merge sort ran on the code read off
# the folder name, so a row whose operator was corrected in the index - a
# B738_HNA folder now filed under CHH - kept its old alphabetical slot and the
# file could never round-trip through the generator unchanged.
# The path is the last key so the order is TOTAL: without it, rows that tie on
# type, operator and range fell back to the order the filesystem happened to
# hand them over, which is not reproducible and made the file diff noisily.
rows.sort(key=lambda r: (class_rank(r[8]), r[0], r[2], r[6], r[-1]))
print(f"merge: kept {kept}, new {added}, dropped {dropped} (assets no longer on disk)")

# ---------------------------------------------- obsolescence blast radius
# Computed on the MERGED rows - the ones about to be written - because the marks
# exist only in the hand-maintained index; the rows guessed off the assets above
# never carry one. See livery_obsolete_radius.py, which answers the same question
# for any copy of the index without an install:
#     gen_livery_index.py <XP root> --dry-run --what-if B752 UAL:B744
import livery_obsolete_radius as _radius
_radius.report([list(r[:-1]) + [r[-1].replace("\\", "/")] for r in rows],
               [a for a in sys.argv[sys.argv.index("--what-if") + 1:] if not a.startswith("--")]
               if "--what-if" in sys.argv else ())

# Writing OUT replaces the repo's own index, and the file's header says
# regeneration is a diff-review. A run against the wrong X-Plane install will
# cheerfully replace a release index with a beta one and say nothing.
# --dry-run computes everything, prints the merge and the blast radius above,
# and writes nothing - which is what you want when you came here to read them.
if "--dry-run" in sys.argv:
    print(f"\n--dry-run: nothing written. {OUT} left alone.")
    sys.exit(0)

with open(OUT, "w", encoding="utf-8", newline="\n") as o:
    # TWO versions, deliberately separate.
    #
    #   schema - how many columns there are and what they mean. A reader keys its
    #            parsing off this, so it only moves when the layout moves.
    #   data   - which day's content this is, as <YYYYMMDD>-r<n>. A human keys
    #            "do I need to update this" off it.
    #
    # Folding them into one would force a reader to write `if (date >= 20260916)`,
    # which is wrong both ways: the layout can change twice in a day, or not
    # change for a year.
    #
    # `source` records WHICH install this describes, because the index is
    # install-specific and a mismatch fails SILENTLY - cards with no picture and
    # one line in the log, nothing saying "this index is for another build".
    # This machine alone has 12.4.4-pnl5 with 376 static aircraft and 12.4.3-r2
    # with 298.
    o.write("I\n1 WED Aviation Database\n")
    o.write("# schema 4\n")
    o.write("# data %s-r1\n" % _dt.date.today().strftime("%Y%m%d"))
    o.write("# source X-Plane %s\n" % xplane_build(XP))
    o.write("# assets %d liveries under apt_aircraft/\n#\n" % len(rows))
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
# MANUALLY MAINTAINED, AND THE ONLY FILE EITHER PROGRAM READS. WED and X-Plane
# both take every fact about a livery and its operator from here; there is no
# second file and no fallback. tools/scripts/airline_research/gen_livery_index.py
# MERGES new assets into it - every row and operator record already here is
# kept exactly as written, only genuinely new assets get a guessed row - so
# edit this file directly and re-run the script when X-Plane ships liveries.
#
# FORMAT: <TYPE> *** <CLASS> *** <AIRLINE> *** <REG> *** <REG COUNTRY> *** <NOTE> *** <RANGE_KM> *** <SCOPE> *** <OP> *** <path>
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
#   NOTE        FREE TEXT, an OPEN vocabulary - not an enum. "Default" means "no
#               annotation"; anything else is rendered in parentheses after the
#               operator name ("United (Retro)", "Air China (Pink Peony)"). The
#               generator only normalises spellings it recognises and keeps
#               everything else as the asset spelled it, so refining a caption is
#               a one-word edit here and needs no code change. Do NOT build a
#               reader that validates this column against a list: the shipped
#               data already carries Peony, Peacock, Panda, Mixue and Fictional,
#               and the variants worth having are the ones nobody enumerated.
#   RANGE_KM    Typical operating range of TYPE at a realistic payload, km.
#               Empty = unknown = never filtered.
#   SCOPE       HOME, or empty. HOME on a military or government row means
#               "parks only at an airport in the operator's own country" - the
#               airport's country, not a distance. Any other military or
#               government row parks anywhere: most equipment is flown by many
#               countries. Neither is range-checked. On any other row, empty.
#               (Schema 3 kept hub coordinates in this column. Hubs are the
#               ICAOs on the OPERATOR record now; see HUB ICAOs.)
#   OP          The operator's operation class - Pax, Cargo, GA, Military or
#               Gov - repeated on the row so a row is self-contained. The ramp's
#               None/GA/Airline/Cargo/Military filter reads it.
#   path        Relative to apt_aircraft/. ALWAYS THE LAST COLUMN - readers take
#               it from the end, which is what let schema 2 add columns without
#               breaking a schema 1 reader.
#
# OPERATOR RECORD, one per operator, in the block ahead of the livery rows:
#     OPERATOR *** <CODE> *** <NAME> *** <IOC CTY> *** <Pax|Cargo|GA|Military|Gov> *** <FLEET> *** <HUB ICAOs>
# The block is the operator directory BOTH programs read - WED's recommendation
# tiers (Popular, Same Country) and the ramp's operation-type filter come from
# here - so it carries operators the sim has no paint for yet. A record without
# a livery row is normal and is kept across regeneration.
#   CODE        See the code conventions below.
#   IOC CTY     Country the operator is registered in, IOC 3-letter code.
#   FLEET       Aircraft in service; 0 means "not researched", not "none". It
#               only ranks the Popular tier, so an approximate number is fine.
#               It does NOT apply to a military, government or generic record -
#               those are 0 by definition and nobody should go looking.
#   HUB ICAOs   The operator's hubs and main bases as ICAO codes, space
#               separated - ALL of the big ones, not just the largest. WED and
#               X-Plane place each one from Global Airports when they load this
#               file: the airport whose 1302 icao_code is the hub, else the one
#               whose ident is. The generator warns about any it cannot place.
#               Left empty on military and government records - never read.
#               Empty = unknown = that operator is never range-filtered.
#               A hub belongs in the operator's own country in almost every
#               case. Where it does not, check you have the right company:
#               subsidiaries and franchises (Jetstar Asia/Japan, Tigerair
#               Taiwan, Spring Japan, Ryanair's Malta Air and Buzz AOCs,
#               Atlas Air's customer brands) hold their OWN code and their own
#               base, and the parent's hub is the classic wrong answer.
#
# CODE CONVENTIONS - a code's SHAPE says where it came from, so a human
# skimming the file can tell a researched code from a placeholder at a glance:
#   XXX     3 letters. The operator's real ICAO airline designator.
#   XXX_F   5 chars. The all-cargo division of XXX, which in the real world
#           flies under the PARENT's designator and so has no code of its own:
#           AFR_F Air France Cargo, KLM_F, SVA_F Saudia Cargo, ICE_F, ASA_F.
#           This file needs one record per distinct livery-bearing brand, and
#           a shared designator would otherwise collapse two of them into one.
#   XXX_1   5 chars, _1 _2 _3 ... A numbered sibling of XXX: a separate brand
#   XXX_2   flying on XXX's designator where _F does not fit - a regional
#           brand (DTR_1 DAT Volidellemarche, DTR_2 DAT Volidisicilia) or an
#           e-commerce/wet-lease customer whose paint rides on the operating
#           carrier's code (GTI_1 Flexport on Atlas Air, HUA_1 ZTO Express).
#           The digit is allocation order within that parent, nothing more.
#   XPnn    4 chars, XPA0..XPA9, XPB0..XPB9, XPC0... A real operator for which
#           no ICAO designator could be confirmed. Allocated sequentially as
#           assets arrive, so the digits carry no meaning; the moment a real
#           code is confirmed, replace the code here and in every livery row.
#   XPGA    The RESERVED generic pseudo-operators - not companies, and never
#   XPMI    to be renumbered: XPGA general aviation, XPMI military, and
#   XPZZ_T  XPZZ_<TYPE> the unpainted/house-colours airliner of that type
#           (XPZZ_B752, XPZZ_DC10) - one per type, so a stand can list the one
#           white airframe it means. They exist so every asset in
#           the file has an operator record and therefore an answer for the
#           ramp's operation-type filter. They are also the only records with no
#           country: a generic has no nationality, which is why the reader
#           exempts them from the country requirement.
#           An unpainted airframe takes the code for what it IS - a white light
#           aircraft is XPGA, a white airliner XPZZ_<TYPE>, a bare military airframe
#           XPMI - so the operation-type filter still answers correctly.
#           XPZZ_* IS NEVER PLACED AUTOMATICALLY. It sorts last everywhere it
#           appears, and any auto-fill pass must skip it: it exists so a human
#           can deliberately park a white airframe, and if a machine could pick
#           it, every airport in the world would sprout white 757s. The Z's are
#           the reminder - the code sorts last on purpose.
#           There was a fourth, XPBZ for business jets, retired 2026-09-19:
#           the ramp offers one General Aviation operation type, so a light
#           aircraft and a business jet were never treated differently.
# SPAWN RULE (R26), applied by X-Plane to each candidate row at a stand:
#     Military or Gov row                       -> eligible (HOME: only in its own country)
#     RANGE_KM empty, or no hub placed          -> eligible
#     d = min over the operator's hubs of greatcircle(hub, stand position from the 1300 row)
#     if d > RANGE_KM                           -> skip this row
#     otherwise                                 -> eligible
# The rule is a floor, not a route network: it removes what cannot physically
# reach the stand and says nothing about what an operator chooses to fly there.
# A domestic operator needs no exemption - its nearest hub is close by definition.
# WED evaluates the same rule from the same file and the same Global Airports
# for its preview cards, so the two sides cannot disagree; nothing is written to
# apt.dat for this.
#
# "????" in any column means the bootstrap could not determine it and a human
# must. It is a TODO marker, not a value - nothing should ever ship with one.
# ============================================================================

""")
    # ONE FILE FOR BOTH READERS. Everything WED used to fetch from
    # WED_AirlineDirectory.txt at run time - name, country, operation class,
    # fleet size - is written here per operator, so the sim and WED read the same
    # facts from the same place and the hand-edited directory feeds only this
    # generator. One record per operator rather than the facts repeated on every
    # livery row; a reader that does not know the record kind skips it.
    #
    #   OPERATOR *** <CODE> *** <NAME> *** <IOC CTY> *** <Pax|Cargo|GA|Military|Gov> *** <FLEET> *** <HUB ICAOs>
    #
    # The pseudo-codes get records too, so the ramp's op-type filter has one
    # answer for everything in the file.
    #
    # TAB-ALIGNED, unlike the space-padded livery rows below: the record has a
    # wide free-text NAME cell and people read this section far more often than
    # they read the rows, so it is laid out as a table for a tab width of 4 (the
    # VS Code / Notepad++ default). The stops come from the widest value in each
    # column, recomputed every run. Legal because every reader - this script,
    # WED_LiveryIndex.cpp, WED_AirlineDirectory.cpp - splits on "***" and strips
    # spaces AND tabs; a reader matching a literal " *** " would drop every
    # record. Column stops (tab=4):
    #   OPERATOR  ***  CODE  ***  NAME  ***  CTY  ***  OP  ***  FLEET  ***  HUBS
    #   0         12   16    24   28    60   64   68   72  84   88     96   100
    TAB = chr(9)
    OP_TAB = 4
    PSEUDO = {"XPGA": ("Generic - general aviation", "GA"),
              "XPMI": ("Generic - military", "Military")}

    # Build every record first, because the column stops come from the DATA, not
    # from constants: one long name widens its own column instead of shoving
    # every field after it a tab to the right, which is what made a record like
    # "Gagarin Research & Test Cosmonaut Center" look like it had broken rank.
    # Recomputed on every run, so a longer name arriving later just widens it again.
    # Every operator already in the file survives the merge whether or not a
    # livery row names it: the section is the global operator directory the
    # ramp's recommendation tiers (Popular, Same Country) draw from, and a
    # record is worth keeping for an airline the sim has no paint for yet.
    op_records = []
    for code in sorted({r[2] for r in rows if r[2] != "????"} | set(existing_ops)):
        if code in existing_ops:
            op_records.append([code] + list(existing_ops[code])); continue
        if code in PSEUDO:
            name, opc = PSEUDO[code]; cty = ""; fleet = "0"; hubs = ""
        elif code.startswith("XPZZ_"):
            name, opc = "Generic - unpainted " + code[5:], "Pax"; cty = ""; fleet = "0"; hubs = ""
        elif code in airlines:
            a = airlines[code]; name, cty, fleet = a[0], a[1], a[2]
            opc  = a[3] if len(a) > 3 else "Pax"
            hubs = " ".join(hub_icaos.get(code, []))
        else:
            name, cty, opc, fleet, hubs = code, "", "Pax", "0", ""   # in the index, unknown to the directory
        op_records.append([code, name, cty, opc, fleet, hubs])
    op_records = [(r + [""] * 6)[:6] for r in op_records]
    op_records.sort(key=lambda r: (class_rank(r[3]), r[0]))

    OP_STOPS = []                                   # column of the "***" after CODE, NAME, CTY, OP, FLEET
    _col = 16                                       # past "OPERATOR" + tab + "***" + tab
    for _j in range(5):
        _w = max(len(r[_j]) for r in op_records)
        _col = ((_col + _w) // OP_TAB + 1) * OP_TAB  # at least one tab clear of the longest field
        OP_STOPS.append(_col); _col += 4             # the separator that follows

    def fmt_operator(cells):
        cells = list(cells) + [""] * (6 - len(cells))    # code name cty op fleet hubs
        out = "OPERATOR" + TAB + "***" + TAB; col = 16
        for cell, stop in zip(cells[:5], OP_STOPS):
            out += cell; col += len(cell); n = 0
            while col < stop or n == 0:                  # at least one tab, then up to the stop
                col = (col // OP_TAB + 1) * OP_TAB; out += TAB; n += 1
            out += "***" + TAB; col += 4
        return (out + cells[5] if cells[5] else out[:-1]) + "\n"

    o.write("# ---- operators -------------------------------------------------------------\n")
    # A banner between class groups. 1496 records is too many to scan without
    # one, and a reader that does not know the convention just sees a comment.
    _seen = None
    for rec in op_records:
        if rec[3] != _seen:
            _seen = rec[3]
            o.write(class_banner(_seen))
        o.write(fmt_operator(rec))
    o.write("\n# ---- liveries --------------------------------------------------------------\n")

    # Column-pad so the file is scannable by eye. Safe because the parser
    # tokenizes on "***" rather than matching a fixed " *** " separator, so the
    # extra spaces cost nothing - same convention as WED_AirportDatabase.cpp.
    widths = [max(len(r[i]) for r in rows) for i in range(len(rows[0]) - 1)]
    _seen = None
    for r in rows:
        if r[8] != _seen:
            _seen = r[8]
            o.write(class_banner(_seen))
        cells = [r[i].ljust(widths[i]) for i in range(len(widths))] + [r[-1]]
        o.write(" *** ".join(cells) + "\n")

print(f"rows written          : {len(rows)}")
print(f"rows needing a human  : {len(flagged)}")
print(f"note distribution     : {dict(collections.Counter(r[5] for r in rows))}")
print(f"with registration     : {sum(1 for r in rows if r[3])}")
print(f"\nwrote {OUT}")
for x in flagged[:15]: print("   flag:", x)
if len(flagged) > 15: print(f"   ... +{len(flagged)-15} more")
