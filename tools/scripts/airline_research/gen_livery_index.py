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

WEDL = wed_livery_dir()        # derived from this script's own location

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

# Liveries that must not spawn. Same sidecar pattern as the registrations above,
# and for the same reason: a mark is a human decision and has to survive the next
# regeneration. See livery_obsolete.txt's own header for what a mark means and
# why the blast radius below has to be read before adding one.
OBSOLETE_NOTE = "Obsolete"
OBSOLETE = {}
_obs = os.path.join(os.path.dirname(os.path.abspath(__file__)), "livery_obsolete.txt")
if os.path.exists(_obs):
    for l in open(_obs, encoding="utf-8"):
        if l.startswith("#") or "***" not in l: continue
        k, _, v = l.partition("***")
        OBSOLETE[k.strip()] = v.strip()

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
        rows.append((typ or "????", cls, airline or "????", reg, ioc, note, rel))

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
    o.write("# schema 1\n")
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
#   NOTE        FREE TEXT, an OPEN vocabulary - not an enum. "Default" means "no
#               annotation"; anything else is rendered in parentheses after the
#               operator name ("United (Retro)", "Air China (Pink Peony)"). The
#               generator only normalises spellings it recognises and keeps
#               everything else as the asset spelled it, so refining a caption is
#               a one-word edit here and needs no code change. Do NOT build a
#               reader that validates this column against a list: the shipped
#               data already carries Peony, Peacock, Panda, Mixue and Fictional,
#               and the variants worth having are the ones nobody enumerated.
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
