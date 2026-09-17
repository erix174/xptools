import os
"""
One-shot merge of WED_CommercialAirports.txt (ICAO -> ISO_COUNTRY, last
column) and WED_AirlineDatabase.txt (ICAO -> researched airline codes) into
a single WED_AirportDatabase.txt, in the format:

    <ICAO> *** <ISO_COUNTRY> *** <AIRLINE_CODE>, <AIRLINE_CODE>, ...

Airline code lists (or the blank/"<NA>" placeholder) are carried over
byte-for-byte from WED_AirlineDatabase.txt - this script only ever inserts
the country field between the ICAO and the existing "*** ..." suffix, it
never re-parses or re-joins the airline list itself.

Usage: python merge_airport_database.py
Run from anywhere; paths below are absolute to this repo checkout.
"""
import sys

from wed_paths import wed_livery_dir
WEDLIVERY = wed_livery_dir()
COMMERCIAL = WEDLIVERY + r"\WED_CommercialAirports.txt"
AIRLINES = WEDLIVERY + r"\WED_AirlineDatabase.txt"
OUT = WEDLIVERY + r"\WED_AirportDatabase.txt"

HEADER = """I
1 WED Aviation Database
# Data source by Eric Xu, contact erix@x-plane.com for updates
#
# ============================================================================
# INSTRUCTIONS FOR WHOEVER IS FILLING THIS FILE IN - READ THIS FIRST
# ============================================================================
#
# WHAT THIS FILE IS: the single reference database WED reads for two,
# independent per-airport facts, keyed by ICAO code:
#   1. the airport's country (for the country-flag banner and the "same
#      country" livery-recommendation tier)
#   2. which airlines currently serve it (for the livery recommendation
#      list itself)
# It's read directly by WED at runtime - no local X-Plane install or
# apt.dat is consulted for either fact anymore.
#
# FORMAT (machine-parsed, follow it precisely):
#     <ICAO> *** <ISO_COUNTRY> *** <AIRLINE_ICAO>, <AIRLINE_ICAO>, ...
#   - ICAO is the airport's ICAO (or X-Plane synthetic) identifier.
#   - ISO_COUNTRY is the airport's plain ISO-3166-1 alpha-2 country code
#     (e.g. "US", "CN", "GB") - WED normalizes this to an IOC-style code
#     itself (see WED_IocCountryCodes.h); do not pre-convert it here.
#   - Airline codes are separated by ", " (comma then one space), use
#     airline ICAO codes (3 letters, e.g. "UAL" for United) - NOT IATA.
#   - The airport ICAO and every airline code must be UPPERCASE.
#
# YOUR TASK (airline research only - the country field is already
# populated for every row and should not need hand-editing): every line
# below of the exact form
#     <ICAO> *** <COUNTRY> ***
# (i.e. the second "***" marker with NOTHING after it) is a TODO - find out
# which airlines currently operate scheduled service at that airport, and
# fill in the line following the rules below. Lines that already have
# content after the second "***" are already done - leave them exactly as
# they are. Don't reorder, delete, or renumber anything; just edit each
# blank line in place.
#
# HOW TO RESEARCH EACH AIRPORT: the fastest reliable method is that
# airport's Wikipedia article, under its "Airlines and destinations"
# section (search "<airport name> Wikipedia" if the ICAO doesn't obviously
# map to an article title). Cross-check with the airport's own official
# website or the airline's own published route map if anything looks
# uncertain.
#
# CRITICAL ACCURACY RULES - read carefully, these are not optional:
#
# 1. Do NOT guess an airline's ICAO code. If you are not independently
#    confident a given airline/code pairing is correct, leave that airline
#    out entirely rather than include a wrong or made-up code. When in
#    doubt, search "<airline name> ICAO code" to confirm before writing it.
#    A wrong code is worse than a missing one.
#
# 2. Do NOT include a regional/marketing BRAND name that covers MULTIPLE
#    different real operating airlines with no single ICAO code of its own.
#    Common examples: "American Eagle", "Delta Connection", "United
#    Express", "Air Canada Express", "QantasLink". These brand names are
#    NOT airlines and have no ICAO code - each is actually flown by several
#    distinct companies (e.g. American Eagle flights are operated by Envoy
#    Air, PSA Airlines, Piedmont Airlines, Republic Airways, or SkyWest
#    Airlines, depending on the specific flight).
#    HOWEVER: if you can identify which SPECIFIC real subsidiary airline(s)
#    with their OWN genuine ICAO code actually serve that airport, DO
#    include those specific subsidiaries as their own separate entries
#    (they are real, independent airlines with likely-distinct aircraft
#    liveries, even though they fly under a shared brand name) - e.g. write
#    "ENY" for Envoy Air, "SKW" for SkyWest Airlines, "JZA" for Jazz
#    Aviation, etc. Only skip the ones you cannot confidently pin down.
#
# 3. If, after actually checking, an airport genuinely has ZERO current
#    scheduled airline service (confirmed - not just "I couldn't find
#    anything"), write the literal text "<NA>" in place of an airline list:
#        <ICAO> *** <COUNTRY> *** <NA>
#    This is a real, meaningful answer meaning "checked, nothing to add" -
#    it's different from leaving a line untouched with nothing after the
#    second "***". It ALSO overrides this airport's default "commercial"
#    status (see below) - WED will treat it as not commercially served.
#
# 4. If you genuinely cannot determine an airport's current airlines (no
#    reliable source found, information too unclear/contradictory), just
#    leave that line as "<ICAO> *** <COUNTRY> ***" with nothing after the
#    second marker. Do not guess. It stays available for a future research
#    pass.
#
# 5. Cargo-only airlines are in scope too - apply the exact same accuracy
#    bar (rule 1) to them as to passenger airlines.
#
# 6. Work through the file steadily; it's fine to do this in batches over
#    multiple sessions. Accuracy matters far more than speed or
#    completeness - a shorter, correct list beats a longer, guessed one.
#
# COMMERCIAL STATUS: every ICAO in this file is, by construction, one
# OurAirports considers to have scheduled commercial service - that fact
# alone (being present in this file at all) is what WED uses to decide
# whether to offer livery recommendations, UNLESS this row's airline field
# is the explicit "<NA>" override from rule 3 above. An ICAO not present in
# this file at all simply gets no recommendations of any kind (no flag, no
# airlines) - that's expected behavior for any airport not yet catalogued,
# not an error.
#
# Lines starting with "#" (and blank lines) are comments and should be left
# alone.
"""


def load_commercial_countries(path):
    countries = {}
    order = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            raw = line.rstrip("\n")
            s = raw.strip()
            if not s or s == "I" or s.startswith("#") or s.startswith("1 WED"):
                continue
            parts = raw.split("\t")
            if len(parts) < 6:
                raise ValueError(f"WED_CommercialAirports.txt row has < 6 fields: {raw!r}")
            icao = parts[0].strip()
            country = parts[-1].strip()
            if not icao or not country:
                raise ValueError(f"missing icao/country in row: {raw!r}")
            if icao in countries:
                raise ValueError(f"duplicate ICAO in WED_CommercialAirports.txt: {icao}")
            countries[icao] = country
            order.append(icao)
    return countries, order


def merge_airline_rows(path, countries):
    out_lines = []
    seen = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            raw = line.rstrip("\n")
            s = raw.strip()
            if not s or s == "I" or s.startswith("#") or s.startswith("1 WED"):
                continue

            # ICAO is everything up to the first whitespace run.
            i = 0
            while i < len(raw) and raw[i] not in " \t":
                i += 1
            icao = raw[:i]
            rest = raw[i:].strip()  # "*** CES, CSN" or just "***"
            if not rest.startswith("***"):
                raise ValueError(f"WED_AirlineDatabase.txt row missing '***' marker: {raw!r}")
            after_marker = rest[3:]  # "" or " CES, CSN" (leading space kept)

            if icao not in countries:
                raise ValueError(f"ICAO {icao!r} in WED_AirlineDatabase.txt has no country (not in WED_CommercialAirports.txt)")
            if icao in seen:
                raise ValueError(f"duplicate ICAO in WED_AirlineDatabase.txt: {icao}")
            seen.add(icao)

            country = countries[icao]
            out_lines.append((icao, f"{icao} *** {country} ***{after_marker}"))

    missing = set(countries) - seen
    if missing:
        raise ValueError(f"{len(missing)} ICAOs present in WED_CommercialAirports.txt but missing from WED_AirlineDatabase.txt: {sorted(missing)[:10]}...")

    return out_lines


def main():
    countries, _ = load_commercial_countries(COMMERCIAL)
    rows = merge_airline_rows(AIRLINES, countries)
    rows.sort(key=lambda r: r[0])

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEADER)
        for icao, line in rows:
            f.write(line + "\n")

    print(f"Wrote {len(rows)} rows to {OUT}")


if __name__ == "__main__":
    main()
