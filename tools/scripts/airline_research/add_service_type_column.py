"""
Adds a Pax/Cargo service-type column to WED_AirlineDirectory.txt, changing
its format from

    <CODE> *** <Name> *** <IOC country> *** <Fleet count>
to
    <CODE> *** <Name> *** <IOC country> *** <Pax|Cargo> *** <Fleet count>

Round 1, name-based only: any row whose name contains "cargo" (case-
insensitive) as a substring is tagged Cargo; everything else Pax. This is a
practical first pass, NOT the long-term source of truth - a future pass will
supersede this using the real static livery database's own op_type per the
project's plan (see WED_AirlineDirectory.h).

Also restores, as new XXX_F rows, the handful of genuine "same airline, one
ICAO code shared between a passenger and an all-cargo division" cases that
were dropped as plain duplicates during the earlier ICAO-collision cleanup
pass (fix_directory_duplicates.py) - now that this format has a place to put
them (a 5-char XXX_F code, distinct from the passenger entry's plain XXX),
they belong back in, tagged Cargo, instead of being deleted outright. This
is exactly the fix the Pax/Cargo column exists for: without it, a cargo
airline's livery could get recommended/placed at a passenger gate.

Usage: python add_service_type_column.py
"""
import re

PATH = r"C:\Users\Eric\Desktop\Laminar Misc Project\WED\xptools-livery\src\WEDLivery\WED_AirlineDirectory.txt"

# Rows to re-add (were deleted as "duplicate" before this column existed;
# each is a real all-cargo division of the airline that already owns the
# plain 3-letter code under its passenger name). Inserted right after the
# passenger row for the same parent code, fleet counts as originally listed.
RESTORE_CARGO_ROWS = [
    # (insert_after_code, new_code, name, country, fleet)
    ("AFR", "AFR_F", "Air France Cargo", "FRA", 2),
    ("ASA", "ASA_F", "Alaska Air Cargo", "USA", 5),
    ("GLO", "GLO_F", "GOLLOG", "BRA", 1),
    ("ICE", "ICE_F", "Icelandair Cargo", "ISL", 1),
    ("KLM", "KLM_F", "KLM Cargo", "NED", 3),
    ("SVA", "SVA_F", "Saudia Cargo", "KSA", 6),
]


def main():
    with open(PATH, encoding="utf-8") as f:
        lines = f.readlines()

    kSep = " *** "
    out_lines = []
    tagged_pax = 0
    tagged_cargo = 0
    restored = 0
    pending_inserts = {code: (new_code, name, country, fleet)
                        for code, new_code, name, country, fleet in RESTORE_CARGO_ROWS}

    for line in lines:
        stripped = line.rstrip("\n")
        m = re.match(r"^([A-Z0-9_]{3,5}) \*\*\* (.+) \*\*\* ([A-Z]{3}) \*\*\* (\d+)\s*$", stripped)
        if not m:
            out_lines.append(line)
            continue

        code, name, country, fleet = m.group(1), m.group(2), m.group(3), m.group(4)
        service = "Cargo" if "cargo" in name.lower() else "Pax"
        if service == "Cargo":
            tagged_cargo += 1
        else:
            tagged_pax += 1

        out_lines.append(f"{code}{kSep}{name}{kSep}{country}{kSep}{service}{kSep}{fleet}\n")

        if code in pending_inserts:
            new_code, cname, ccountry, cfleet = pending_inserts.pop(code)
            out_lines.append(f"{new_code}{kSep}{cname}{kSep}{ccountry}{kSep}Cargo{kSep}{cfleet}\n")
            restored += 1

    if pending_inserts:
        raise SystemExit(f"Never found insertion point(s) for: {list(pending_inserts.keys())}")

    with open(PATH, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(out_lines)

    print(f"Tagged {tagged_pax} Pax rows, {tagged_cargo} Cargo rows (name-based).")
    print(f"Restored {restored} cargo-division rows under new XXX_F codes.")


if __name__ == "__main__":
    main()
