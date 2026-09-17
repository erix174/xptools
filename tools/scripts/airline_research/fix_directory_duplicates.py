import os
"""
One-time fix for WED_AirlineDirectory.txt's duplicate-ICAO-code rows,
based on a web-research pass (4 parallel batches) verifying which name
actually currently holds each contested code. See conversation history for
the full per-code source citations.

For each of the 43 duplicate codes found, one of:
  - DELETE: name confirmed wrong/redundant for this code - drop that row,
    keep the other (real ICAO code shared 1:1 with the surviving name).
  - RECODE: name's OWN real ICAO code was found (different from the
    contested one) - move it to a new row under its correct code instead
    of deleting it outright.
  - SKIP: independent sources genuinely conflict and no authoritative
    (ICAO Doc 8585-level) source could resolve it - left untouched,
    flagged for manual follow-up.

Usage: python fix_directory_duplicates.py
"""
import re

from wed_paths import wed_livery_dir
PATH = os.path.join(wed_livery_dir(), "WED_AirlineDirectory.txt")

# code -> exact name string of the row to DELETE
DELETE = {
    "ABS": "Fly Gabon",
    "ACA": "Air Canada Jetz",           # real code unconfirmed (possibly ACJ/AJZ) - don't guess, just drop
    "ADZ": "ChukotAVIA",                # sourcing doubtful; Compass Air Cargo confirmed
    "AFR": "Air France Cargo",
    "AGR": "ASL",
    "AMF": "Moon Flights",              # real code unverifiable - don't guess
    "ANE": "Iberia Regional",
    "APF": "Amapola",                   # keep current brand "populAir"
    "ASA": "Alaska Air Cargo",
    "AVJ": "Avjet Corporation",         # real code unverifiable - don't guess
    "BRH": None,                        # SKIP - conflicting sources
    "DAP": ["Antarctic Airways", "Mineral Airways"],  # keep Aerovias DAP
    "FVS": "Falcon Aviation Services San Marino",
    "GBB": "Global Aviation",           # keep current public brand "Lift"
    "GLO": "GOLLOG",
    "HLF": "YunExpress",
    "IBB": "Binter Airlines",
    "IBX": "ANA Connection",
    "ICE": "Icelandair Cargo",
    "JDL": "Kuayue Express",            # real code unverifiable - don't guess
    "KLM": "KLM Cargo",
    "KNE": "National Air Services (NAS)",
    "LYM": "Denver Air Connection",
    "MMD": "Alsie Express",
    "MPH": "Martinair",                 # keep current cargo-era brand "Martinair Cargo"
    "MUS": "Moving Up Services",        # no evidence this airline exists
    "PMI": "Air Europa Express",        # current Air Europa Express uses OVA, not PMI
    "RAC": None,                        # SKIP - genuine real-world duplicate, no authoritative source
    "ROJ": "RoyalJet Bermuda",
    "SFR": "Safair",
    "SKP": "Skippers Aviation",         # real code unverifiable - don't guess
    "SLI": "Aerolitoral",               # keep current name "Aeromexico Connect"
    "SRY": "Aleutian Airways",
    "STT": None,                        # SKIP - conflicting sources, no authority to arbitrate
    "SVA": "Saudia Cargo",
    "SVG": "SVG Air",                   # likely IATA code mis-copied into ICAO field
    "TOK": None,                        # SKIP - conflicting sources
    "TRA": None,                        # SKIP - conflicting sources
    "VQI": "Flyme",
    "VTE": "Contour Aviation",
}

# code -> (name to move, new_code) - recode instead of delete
RECODE = {
    "AYG": ("Air Thanlwin", "RTL"),
    "EIN": ("Aer Lingus Regional", "STK"),
}


def main():
    with open(PATH, encoding="utf-8") as f:
        lines = f.readlines()

    kSep = " *** "
    out_lines = []
    deleted = []
    recoded = []
    skipped_codes = set()

    for line in lines:
        stripped = line.rstrip("\n")
        m = re.match(r"^([A-Z0-9]{3,5}) \*\*\* (.+?) \*\*\* ", stripped)
        if not m:
            out_lines.append(line)
            continue
        code, name = m.group(1), m.group(2)

        if code in DELETE:
            victim = DELETE[code]
            if victim is None:
                skipped_codes.add(code)
                out_lines.append(line)
                continue
            victims = victim if isinstance(victim, list) else [victim]
            if name in victims:
                deleted.append((code, name))
                continue  # drop this row

        if code in RECODE:
            target_name, new_code = RECODE[code]
            if name == target_name:
                new_line = re.sub(r"^" + re.escape(code) + r" \*\*\* ", new_code + " *** ", stripped) + "\n"
                out_lines.append(new_line)
                recoded.append((code, name, new_code))
                continue

        out_lines.append(line)

    with open(PATH, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(out_lines)

    print(f"Deleted {len(deleted)} rows:")
    for code, name in deleted:
        print(f"  {code}: {name}")
    print(f"\nRecoded {len(recoded)} rows:")
    for code, name, new_code in recoded:
        print(f"  {code} -> {new_code}: {name}")
    print(f"\nSkipped (left as-is, still duplicated) codes: {sorted(skipped_codes)}")


if __name__ == "__main__":
    main()
