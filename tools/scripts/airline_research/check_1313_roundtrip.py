#!/usr/bin/env python3
"""Check that row 1313 survives a trip through WED, and that bad rows do not.

    check_1313_roundtrip.py <source apt.dat> [exported apt.dat] [wed xml]

With one argument it reports what the source file contains. With two or three it
diffs what WED produced against what it was given, which is the check that
matters: every rule this format has about malformed input is a rule about what
WED must NOT write back.

Why this exists: WED_LiveryFormatSpec.md section 5.3 calls V22 - "strip every
1313 row and aircraft placement is byte-identical to today" - "the regression
test worth automating. It is the one guarantee everything else rests on." There
is no automated test anywhere in this tree, and the verification that R5 drops a
malformed row whole and R23 derives the size letter was, until this script, a
thing somebody did once by hand and could not repeat.

What it asserts:

  R5   a 1313 that is not exactly six integers in 0..1000 is dropped WHOLE.
       Not clamped, not padded, not partially applied - and on the way out it
       must not reappear, because a stand whose row was dropped has no weights.
  R17  a stand with no 1313, and a stand whose 1313 was dropped, are the same
       thing: no row on export. Distinct from six zeros, which is a legal way
       to say nothing parks here (section 4.2) and MUST survive.
  R23  1301's size letter equals the highest class carrying a non-zero weight.
       It is derived on export, not copied, so this catches the derivation
       rather than agreeing with the input by luck.

Exit status is 0 only when every stand checks out.
"""
import io
import re
import sys

ROW_STAND   = '1300'
ROW_EXTEND  = '1301'
ROW_WEIGHTS = '1313'


def load_apt(path):
    """-> [{'name','letter','operators','weights'}], in file order."""
    stands, cur = [], None
    for line in io.open(path, encoding='utf-8', errors='replace'):
        f = line.split()
        if not f:
            continue
        if f[0] == ROW_STAND:
            cur = {'name': ' '.join(f[6:]), 'letter': None,
                   'operators': None, 'weights': None}
            stands.append(cur)
        elif f[0] == ROW_EXTEND and cur is not None:
            cur['letter'] = f[1]
            cur['operators'] = ' '.join(f[3:]).lower()
        elif f[0] == ROW_WEIGHTS and cur is not None and cur['weights'] is None:
            # First one wins (R24). A second is discarded, not merged, so
            # reading only the first is what a conforming reader does.
            cur['weights'] = ' '.join(f[1:])
    return stands


def weights_are_legal(raw):
    """R5 and R11: exactly six tokens, each a plain integer in 0..1000."""
    if raw is None:
        return False
    tok = raw.split()
    if len(tok) != 6:
        return False
    for t in tok:
        if not t.isdigit():          # rejects "-5" and "1.5" without naming either
            return False
        if int(t) > 1000:
            return False
    return True


def load_xml_weights(path):
    """-> {stand name: weights attribute}, for the .wed document."""
    out = {}
    blob = io.open(path, encoding='utf-8', errors='replace').read()
    for obj in re.findall(r'<object class="WED_RampPosition".*?</object>', blob, re.S):
        nm = re.search(r'name="([^"]*)"', obj)
        rs = re.search(r'<ramp_start([^>]*)/>', obj)
        if not nm or not rs:
            continue
        attrs = dict(re.findall(r'(\w+)="([^"]*)"', rs.group(1)))
        out[nm.group(1)] = attrs.get('weights', '')
    return out


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    src = load_apt(argv[1])
    print("source: %s  (%d stands)" % (argv[1], len(src)))

    if len(argv) == 2:
        for s in src:
            legal = weights_are_legal(s['weights'])
            note = '' if s['weights'] is None else ('legal' if legal else 'MALFORMED - must be dropped')
            print("  %-22s %s %-18s %s" % (s['name'], s['letter'], s['weights'] or '-', note))
        return 0

    out = {s['name']: s for s in load_apt(argv[2])}
    xml = load_xml_weights(argv[3]) if len(argv) > 3 else None

    problems = []
    print("\n%-22s %-18s %-18s %s" % ("stand", "source 1313", "export 1313", "checks"))
    for s in src:
        o = out.get(s['name'])
        if o is None:
            problems.append("%s: missing from the export entirely" % s['name'])
            continue

        legal = weights_are_legal(s['weights'])
        notes = []

        # R5 / R17 - what must and must not come back out
        if legal and o['weights'] != s['weights']:
            problems.append("%s: weights changed, %r -> %r" % (s['name'], s['weights'], o['weights']))
            notes.append("R5 FAIL")
        elif not legal and o['weights'] is not None:
            problems.append("%s: a dropped row came back as %r" % (s['name'], o['weights']))
            notes.append("R5 FAIL")
        elif not legal and s['weights'] is not None:
            notes.append("R5 ok (dropped whole)")
        elif legal:
            notes.append("R5 ok")

        # R23 - the letter is derived, so check it against the weights that WON
        if o['weights'] is not None and weights_are_legal(o['weights']):
            w = [int(x) for x in o['weights'].split()]
            if any(w):
                want = chr(ord('A') + max(i for i, v in enumerate(w) if v > 0))
                if o['letter'] != want:
                    problems.append("%s: 1301 says %s, top non-zero weight is %s"
                                    % (s['name'], o['letter'], want))
                    notes.append("R23 FAIL")
                else:
                    notes.append("R23 ok")
            else:
                notes.append("all-zero (letter not derived)")

        if s['operators'] != o['operators']:
            problems.append("%s: operators changed, %r -> %r"
                            % (s['name'], s['operators'], o['operators']))
            notes.append("OPERATORS FAIL")

        # The .wed document must agree with the apt.dat it was imported from.
        if xml is not None and s['name'] in xml:
            want_xml = s['weights'] if legal else ''
            if xml[s['name']] != (want_xml or ''):
                problems.append("%s: wed.xml has %r, expected %r"
                                % (s['name'], xml[s['name']], want_xml or ''))
                notes.append("XML FAIL")

        print("%-22s %-18s %-18s %s" % (s['name'], s['weights'] or '-',
                                        o['weights'] or '-', ", ".join(notes)))

    print()
    if problems:
        print("FAILED - %d problem(s):" % len(problems))
        for p in problems:
            print("   " + p)
        return 1
    print("OK - %d stands, weights round-trip, malformed rows stayed dropped, "
          "size letters derive from the weights." % len(src))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
