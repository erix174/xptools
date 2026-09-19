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

# ICAO -> (lat, lon) for every hub named above, read off the install's own Global
# Airports apt.dat. Resolved HERE, once, so the sim receives numbers: it must not
# need an airport lookup at spawn time, and the hand-edited directory must not
# carry coordinates nobody can check at a glance. The datum row is preferred;
# an airport without one gets the midpoint of its first runway.
def resolve_hubs(xp_root, wanted):
    path = os.path.join(xp_root, "Global Scenery", "Global Airports", "Earth nav data", "apt.dat")
    out = {}
    if not os.path.exists(path):
        print(f"WARNING: no Global Airports apt.dat at {path} - hubs left unresolved")
        return out
    cur = None; lat = lon = None; rwy = None
    def flush():
        if cur in wanted and cur not in out:
            if lat is not None:        out[cur] = (lat, lon)
            elif rwy is not None:      out[cur] = rwy
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("1 ") or line.startswith("1	"):
                flush()
                t = line.split(); cur = t[4] if len(t) > 4 else None
                lat = lon = None; rwy = None
            elif cur in wanted:
                if line.startswith("1302 datum_lat"):   lat = float(line.split()[2])
                elif line.startswith("1302 datum_lon"): lon = float(line.split()[2])
                elif line.startswith("100 ") and rwy is None:
                    t = line.split()
                    try: rwy = ((float(t[9]) + float(t[18])) / 2, (float(t[10]) + float(t[19])) / 2)
                    except (IndexError, ValueError): pass
    flush()
    return out

_wanted = {i for v in hub_icaos.values() for i in v}
hub_ll  = resolve_hubs(XP, _wanted)
_unres  = sorted(_wanted - set(hub_ll))
if _unres: print(f"WARNING: {len(_unres)} hub ICAO(s) not in Global Airports: {' '.join(_unres)}")

def hubs_cell(airline):
    """'lat,lon lat,lon ...' for the operator, or '' when nothing resolved."""
    return " ".join("%.2f,%.2f" % hub_ll[i] for i in hub_icaos.get(airline, []) if i in hub_ll)

# Military and government rows that may only park on home soil - see that
# file's header. The token goes in the HUBS column, which such rows never use
# for coordinates, so schema 2 carries it without a new column.
HOME_ONLY = set()          # kept from the existing index rows - see the merge below

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

# Hand-read registrations used to live in a sidecar; they are index rows now,
# and the merge below keeps whatever the index says over anything guessed here.
OVERRIDES = {}

# "Obsolete" is a NOTE value on the row (R25); marks are kept by the merge.
OBSOLETE_NOTE = "Obsolete"
OBSOLETE = {}

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

        # A mark wins over anything derived from the filename. It is the one note
        # value with meaning to a reader rather than to a caption: spec R25 says
        # an Obsolete row never enters the spawn pool, at any stage.
        if rel in OBSOLETE:
            note = OBSOLETE_NOTE

        if typ is None or airline is None or (reg and not confident) or ioc == "???":
            flagged.append((full, typ, airline, reg, ioc))
        cls = sizes.get(typ, "?") if typ else "?"

        # GENERIC AND HOUSE LIVERIES ARE CLASSED BY THE AIRCRAFT, NOT THE FOLDER.
        # The folder scan put four unmarked 757s under military, Piaggios under
        # "generic airliner", BBJs and a white DC-10 under general aviation, and
        # Boeing's three demonstrators on one "operator" card. The ramp's op-type
        # filter reads these codes, so each has to mean what it says:
        #   XPBZ  business jet / corporate    (BBJ, or a class-A/B jet or turboprop
        #                                      already filed as corporate)
        #   XPGA  light general aviation      (class A/B, everything else)
        #   XPGN  generic / unpainted airliner (class C and up)
        # Boeing's house aircraft are the definition of generic and go the same way.
        stem = os.path.splitext(f)[0]
        if stem in ("757PW_static", "757PW_winglet_static", "757RR_static", "757RR_winglet_static"):
            airline = "XPGN"                       # unmarked airliners misfiled under military
        elif stem == "757_KAF_5701":
            airline = "KAF"                        # Kazakhstan Air Force - a real operator, added to the directory
        if airline in ("XPGA", "XPGN", "XPBZ", "BOE"):
            if "BBJ" in (note or "") or "BBJ" in stem.upper() or airline == "XPBZ" or (airline == "BOE" and cls in "AB"):
                airline = "XPBZ"
            elif cls in "AB":
                airline = "XPGA"
            else:
                airline = "XPGN"

        rows.append((typ or "????", cls, airline or "????", reg, ioc, note,
                     ranges.get(typ, "") if typ else "",
                     "HOME" if rel.replace("\\", "/") in HOME_ONLY else (hubs_cell(airline) if airline else ""),
                     rel))

# --------------------------------------------------- obsolescence blast radius
#
# A mark in livery_obsolete.txt is GLOBAL and has no per-stand undo: the livery
# stops existing at every airport at once. The number that matters is not how
# many rows were marked, it is how many (airline, class) pairs are left with NO
# asset - because a pair with no asset is a stand that silently parks nothing,
# which is the one defect in this whole feature that produces no symptom at all
# (spec section 4.5 measures the population at 17.2% of stands).
#
# So: print it, every run, whether or not anything was marked. Marking B752
# would empty twenty pairs and the 757 is still in daily service; that has to be
# visible before the commit, not discovered afterwards.
def _pairs_emptied_by(dropped_rel):
    """(airline, class) pairs that would have no asset left if these paths went."""
    live, doomed = collections.defaultdict(int), collections.defaultdict(int)
    for r in rows:
        key = (r[2], r[1])
        doomed[key] += 1 if r[6] in dropped_rel else 0
        live[key]   += 1
    return sorted(k for k in live if live[k] == doomed[k])

_marked = [r for r in rows if r[5] == OBSOLETE_NOTE]
print(f"obsolete marks        : {len(_marked)} rows"
      + ("" if len(_marked) == len(OBSOLETE)
         else f"  ** {len(OBSOLETE) - len(_marked)} sidecar path(s) matched NOTHING - check for typos **"))
_emptied = _pairs_emptied_by({r[6] for r in _marked})
print(f"  would empty         : {len(_emptied)} (airline,class) pair(s)"
      + ("" if not _emptied else "  <-- each one is a stand that parks nothing"))
for a, c in _emptied[:20]:
    print(f"      {a} class {c}")
if len(_emptied) > 20: print(f"      ... +{len(_emptied)-20} more")

# Ask "what if?" without editing the sidecar:  gen_livery_index.py --what-if B752 B744
if "--what-if" in sys.argv:
    for t in sys.argv[sys.argv.index("--what-if") + 1:]:
        t = t.upper()
        hit = {r[6] for r in rows if r[0] == t}
        if not hit:
            print(f"what-if {t:<14}: not in the index")
            continue
        em = _pairs_emptied_by(hit)
        print(f"what-if {t:<14}: {len(hit)} rows, would empty {len(em)} pair(s)"
              + ("" if not em else "  " + " ".join(f"{a}/{c}" for a, c in em[:12])
                 + (" ..." if len(em) > 12 else "")))


# --------------------------------------------------------------- emit
rows.sort(key=lambda r: (r[0], r[2], r[6]))

# Writing OUT is a BLIND OVERWRITE of the repo's own index, and the file's header
# says regeneration is a diff-review. A run against the wrong X-Plane install
# will cheerfully replace a release index with a beta one and say nothing.
# --dry-run computes everything, prints the blast radius above, and writes
# nothing - which is what you want when you came here to read that number.
if "--dry-run" in sys.argv:
    print(f"\n--dry-run: nothing written. {OUT} left alone.")
    sys.exit(0)

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
_wanted = {i for v in hub_icaos.values() for i in v}
hub_ll.update(resolve_hubs(XP, _wanted - set(hub_ll)))

def op_class_for(code):
    if code in PSEUDO_OP: return PSEUDO_OP[code]
    a = airlines.get(code)
    return (a[3] if a and len(a) > 3 and a[3] else "Pax")

PSEUDO_OP = {"XPGA": "GA", "XPBZ": "GA", "XPMI": "Military", "XPGN": "Pax"}

merged, kept, added = [], 0, 0
for r in rows:
    rel = r[-1].replace("\\", "/")
    if rel in existing_rows:
        q = existing_rows[rel]
        # keep the hand-maintained cells; refresh only what is derived from an
        # operator record the human may have edited since (hub coordinates)
        if q[7] != "HOME":
            q[7] = hubs_cell(q[2])
        if not q[8]:
            q[8] = op_class_for(q[2])
        merged.append(tuple(q)); kept += 1
    else:
        r = list(r)
        r.insert(8, op_class_for(r[2]))
        merged.append(tuple(r)); added += 1
dropped = len(existing_rows) - kept
rows = merged
print(f"merge: kept {kept}, new {added}, dropped {dropped} (assets no longer on disk)")

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
    o.write("# schema 3\n")
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
# FORMAT: <TYPE> *** <CLASS> *** <AIRLINE> *** <REG> *** <REG COUNTRY> *** <NOTE> *** <RANGE_KM> *** <HUBS> *** <OP> *** <path>
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
#   HUBS        The operator's hub positions as "lat,lon" pairs, space separated,
#               OR the single token HOME on a military/government row, meaning
#               "parks only where the operator's country is the airport's". Any
#               other military row parks anywhere - most equipment is operated
#               by many countries. Otherwise: resolved by the generator from
#               the ICAOs on the operator's OPERATOR record against Global
#               Airports, on every run. Numbers rather than codes so the sim
#               needs no airport lookup at spawn time. To move a hub, edit the
#               OPERATOR record, not this cell.
#               Empty = unknown = never filtered.
#   OP          The operator's operation class - Pax, Cargo, GA, Military or
#               Gov - repeated on the row so a row is self-contained. The ramp's
#               None/GA/Airline/Cargo/Military filter reads it.
#   path        Relative to apt_aircraft/. ALWAYS THE LAST COLUMN - readers take
#               it from the end, which is what let schema 2 add columns without
#               breaking a schema 1 reader.
#
# SPAWN RULE, applied by X-Plane to each candidate row at a stand:
#     if RANGE_KM is empty or HUBS is empty     -> eligible
#     d = min over HUBS of greatcircle(hub, stand position from the 1300 row)
#     if d > RANGE_KM                           -> skip this row
#     otherwise                                 -> eligible
# The rule is a floor, not a route network: it removes what cannot physically
# reach the stand and says nothing about what an operator chooses to fly there.
# A domestic operator needs no exemption - its nearest hub is close by definition.
# WED evaluates the same rule from the same file and the same stand position for
# its preview cards, so the two sides cannot disagree; nothing is written to
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
    # VS Code / Notepad++ default). Legal because every reader - this script,
    # WED_LiveryIndex.cpp, WED_AirlineDirectory.cpp - splits on "***" and strips
    # spaces AND tabs; a reader matching a literal " *** " would drop every
    # record. Column stops (tab=4):
    #   OPERATOR  ***  CODE  ***  NAME  ***  CTY  ***  OP  ***  FLEET  ***  HUBS
    #   0         12   16    24   28    60   64   68   72  84   88     96   100
    OP_TAB   = 4
    OP_STOPS = (24, 60, 68, 84, 96)     # column of the "***" after CODE, NAME, CTY, OP, FLEET
    def fmt_operator(cells):
        cells = list(cells) + [""] * (6 - len(cells))    # code name cty op fleet hubs
        out = "OPERATOR\t***\t"; col = 16
        for cell, stop in zip(cells[:5], OP_STOPS):
            out += cell; col += len(cell); n = 0
            while col < stop or n == 0:                  # at least one tab, then up to the stop
                col = (col // OP_TAB + 1) * OP_TAB; out += "\t"; n += 1
            out += "***\t"; col += 4
        return (out + cells[5] if cells[5] else out[:-1]) + "\n"
    PSEUDO = {"XPGA": ("Generic - light aircraft", "GA"), "XPBZ": ("Generic - business jet", "GA"),
              "XPMI": ("Generic - military", "Military"), "XPGN": ("Generic - unpainted airliner", "Pax")}
    o.write("# ---- operators -------------------------------------------------------------\n")
    for code in sorted({r[2] for r in rows if r[2] != "????"}):
        if code in existing_ops:
            rec = existing_ops[code]
            o.write(fmt_operator([code] + rec))
            continue
        if code in PSEUDO:
            name, opc = PSEUDO[code]; cty = ""; fleet = "0"; hubs = ""
        elif code in airlines:
            a = airlines[code]; name, cty, fleet = a[0], a[1], a[2]
            opc  = a[3] if len(a) > 3 else "Pax"
            hubs = " ".join(hub_icaos.get(code, []))
        else:
            name, cty, opc, fleet, hubs = code, "", "Pax", "0", ""   # in the index, unknown to the directory
        o.write(fmt_operator([code, name, cty, opc, fleet, hubs]))
    o.write("\n# ---- liveries --------------------------------------------------------------\n")

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
