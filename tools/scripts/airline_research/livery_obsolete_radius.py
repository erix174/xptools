# Blast radius of an Obsolete mark in livery_index.txt (spec 6.6, R25).
#
# A mark is GLOBAL and has no per-stand undo: the livery stops existing at every
# airport at once. The number that matters is not how many rows were marked, it
# is how many (airline, class) pairs are left with NO usable asset - a pair with
# no asset is a stand that silently parks nothing, the one defect in this
# feature that produces no symptom at all (spec 4.5).
#
# READ-ONLY and needs no X-Plane install: point it at any copy of the index,
# including the hand-edited one on the desktop, before a mark goes in.
#
#   livery_obsolete_radius.py [index]                    the marks already made
#   livery_obsolete_radius.py [index] --what-if B752 UAL:B744 apt_aircraft/...obj
#   livery_obsolete_radius.py [index] --rank             every type, worst first
#
# A --what-if selector is a TYPE (every row of it), AIRLINE:TYPE (one operator's
# rows of that type - the retired-airframe case), or a path (one row). Each is
# answered on top of the marks already in the file, and separately from the
# others.
#
# gen_livery_index.py imports this and prints the same report after its merge,
# so the number is computed on the rows it is about to write.
import collections, os, sys

OBSOLETE_NOTE = "Obsolete"

# Row cells as read from the index, schema 3.
TYPE, CLASS, AIRLINE, NOTE = 0, 1, 2, 5


def load_rows(path):
    """Livery rows of an index file as cell lists; the path is always the last."""
    rows = []
    for l in open(path, encoding="utf-8", errors="replace"):
        if l.startswith("#") or "***" not in l:
            continue
        q = [x.strip() for x in l.split("***")]
        if q[0] == "OPERATOR" or len(q) < 9:
            continue
        q[-1] = q[-1].replace("\\", "/")
        rows.append(q)
    return rows


def _live_pairs(rows, dropped):
    """(airline, class) -> number of rows that can still spawn."""
    live = collections.Counter()
    for r in rows:
        if r[NOTE] != OBSOLETE_NOTE and r[-1] not in dropped:
            live[(r[AIRLINE], r[CLASS])] += 1
    return live


def pairs_emptied_by(rows, dropped):
    """Pairs that can spawn something today and could not once `dropped` go.
    `dropped` is a set of paths, on top of the marks already in `rows`."""
    before, after = _live_pairs(rows, set()), _live_pairs(rows, dropped)
    return sorted(k for k in before if k not in after)


def marked_radius(rows):
    """Pairs the marks already made have emptied: an asset exists, none can spawn."""
    every = {(r[AIRLINE], r[CLASS]) for r in rows}
    live = _live_pairs(rows, set())
    return sorted(k for k in every if k not in live)


def select(rows, sel):
    """Paths a --what-if selector names; see the header."""
    if "/" in sel or sel.lower().endswith(".obj"):
        s = sel.replace("\\", "/")
        if s.startswith("apt_aircraft/"):
            s = s[len("apt_aircraft/"):]
        return {r[-1] for r in rows if r[-1] == s}
    if ":" in sel:
        a, t = sel.upper().split(":", 1)
        return {r[-1] for r in rows if r[AIRLINE] == a and (not t or r[TYPE] == t)}
    return {r[-1] for r in rows if r[TYPE] == sel.upper()}


def _pairs_text(pairs, limit):
    s = " ".join(f"{a}/{c}" for a, c in pairs[:limit])
    return s + (f" ... +{len(pairs) - limit}" if len(pairs) > limit else "")


def report(rows, what_if=(), rank=False, out=print):
    marked = [r for r in rows if r[NOTE] == OBSOLETE_NOTE]
    emptied = marked_radius(rows)
    out(f"obsolete marks        : {len(marked)} rows")
    out(f"  would empty         : {len(emptied)} (airline,class) pair(s)"
        + ("" if not emptied else "  <-- each one is a stand that parks nothing"))
    for a, c in emptied[:20]:
        out(f"      {a} class {c}")
    if len(emptied) > 20:
        out(f"      ... +{len(emptied) - 20} more")

    for sel in what_if:
        hit = select(rows, sel)
        if not hit:
            out(f"what-if {sel:<14}: matches no row")
            continue
        already = sum(1 for r in rows if r[-1] in hit and r[NOTE] == OBSOLETE_NOTE)
        em = pairs_emptied_by(rows, hit)
        out(f"what-if {sel:<14}: {len(hit):>3} rows"
            + (f" ({already} already marked)" if already else "")
            + f", would empty {len(em):>3} pair(s)"
            + ("" if not em else "  " + _pairs_text(em, 12)))

    if rank:
        types = sorted({r[TYPE] for r in rows})
        table = []
        for t in types:
            hit = {r[-1] for r in rows if r[TYPE] == t}
            live = sum(1 for r in rows if r[-1] in hit and r[NOTE] != OBSOLETE_NOTE)
            if live:
                table.append((len(pairs_emptied_by(rows, hit)), t, live))
        table.sort(key=lambda x: (-x[0], x[1]))
        out("\nradius by type, if every live row of it were marked:")
        out("  type   live rows   pairs emptied")
        for n, t, live in table:
            out(f"  {t:<6} {live:>9}   {n:>13}")


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    args = argv[1:]
    what_if, rank = [], False
    if "--rank" in args:
        rank = True
        args.remove("--rank")
    if "--what-if" in args:
        i = args.index("--what-if")
        what_if, args = args[i + 1:], args[:i]
    path = args[0] if args else os.path.join(here, "livery_index.txt")
    rows = load_rows(path)
    if not rows:
        sys.exit(f"{path}: no livery rows - is this a schema 3 livery_index.txt?")
    print(f"index                 : {path} ({len(rows)} liveries)")
    report(rows, what_if, rank)


if __name__ == "__main__":
    main(sys.argv)
