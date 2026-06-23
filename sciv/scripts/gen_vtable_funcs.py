#!/usr/bin/env python3
"""
gen_vtable_funcs.py - structural vtable / code-pointer-table scanner (SCIV).

Adapted from the Metal Slug XX reference script for SoulCalibur IV. See that
project's history for the full rationale; the short version:

ReXGlue's built-in vtable discovery (src/codegen/vtable_scanner.cpp) is gated on
MSVC RTTI Complete Object Locators. SCIV game code is compiled with RTTI
effectively disabled for game classes, so that scanner finds essentially no
vtables. Every function reachable *only* through a vtable slot (adjustor thunks,
no-op `blr` virtuals, ordinary virtual methods, E_NOTIMPL stubs) is therefore
never discovered or registered, and the first indirect call to one traps at
runtime with "Call to invalid or unregistered function at guest address 0x...".
(SCIV boot bring-up Run: 0x823AD288 -> 0x823A1BB0 -> ... in the GPU
interrupt-callback dispatch path.)

This script recovers those entry points structurally, without RTTI:
  * scan .rdata and the file-backed part of .data for 4-byte big-endian values
    that are valid, 4-aligned pointers into the executable range;
  * group them into maximal runs of consecutive code-pointers (a vtable / fnptr
    table is exactly such a run);
  * within runs of length >= MIN_RUN whose targets span >= MIN_SPREAD bytes,
    accept a slot target T only if the instruction at T-4 is an *unconditional*
    function terminator (blr / bctr / unconditional b/ba / zero padding) -- i.e.
    T begins right after a previous function ends. That single check makes T a
    genuine function entry and rules out mid-function false positives that would
    otherwise create overlapping functions and break codegen.

Output is a TOML file of size-less `[functions]` entries (codegen auto-discovers
each extent). Entries already present in sciv_config.toml / sciv_fixups.toml are
skipped so their explicit size hints / hand-authored boundaries are preserved.

Run from the sciv project dir:
    python3 scripts/gen_vtable_funcs.py
"""
import re
import struct
import sys
from pathlib import Path

BASE = 0x82000000
EXLO, EXHI = 0x82110000, 0x82717CF0  # executable range (.text + PSFD00)
# XEX import-thunk table (tail of .text): 0x10-byte stubs the SDK maps to
# __imp__<Kernel> by ordinal. The game can store a thunk's address in a vtable
# (e.g. KiApcNormalRoutineNop_0 @ 0x8271172C, the one import with no SDK export),
# but registering an import thunk as a guest [functions] entry overrides the
# SDK's import handling and leaves an undefined symbol at link. Never emit these.
IMPLO, IMPHI = 0x8271111C, 0x8271202C
MIN_RUN = 2                          # minimum consecutive code-pointers to call it a table
# A run is treated as a vtable / fnptr-table (targets are distinct functions)
# only if its targets span at least MIN_SPREAD bytes. Smaller-spread runs are
# jump/switch tables: their "targets" are internal case blocks of ONE function,
# and registering those as functions would split it and break codegen.
MIN_SPREAD = 0x2000

PROJ = Path(__file__).resolve().parent.parent
BASEFILE = PROJ.parent / "Assets" / "SCIV" / "default.basefile.exe"
# Files whose existing function keys must be preserved (never re-emit here).
EXISTING_FILES = [PROJ / "sciv_config.toml", PROJ / "sciv_fixups.toml"]
OUT = PROJ / "sciv_vtables.toml"

# data regions to scan: (name, lo_addr, hi_addr) -- file-backed bytes only.
# .rdata is fully file-backed; .data raw size is 0x15FE00, so only the first
# 0x15FE00 bytes (up to 0x8287FE00) are real -- beyond that is zero-init BSS.
REGIONS = [
    ("rdata", 0x82000400, 0x820DA82C),
    ("data",  0x82720000, 0x8287FE00),
]


def main():
    d = BASEFILE.read_bytes()

    def be32(off):
        return struct.unpack_from(">I", d, off)[0]

    def insn(addr):
        return be32(addr - BASE)

    def is_cp(v):
        return EXLO <= v < EXHI and (v & 3) == 0

    def is_terminator(w):
        # unconditional terminators only (so T-4 ending => T is a function start)
        if w == 0x4E800020:      # blr
            return True
        if w == 0x4E800420:      # bctr
            return True
        if w == 0:               # alignment padding
            return True
        if (w >> 26) == 18 and (w & 1) == 0:  # b / ba (LK=0; bl/bla are calls)
            return True
        return False

    # existing config function keys (bare or quoted hex) -- preserve their hints
    existing = set()
    for f in EXISTING_FILES:
        if f.exists():
            for m in re.finditer(r'"?0x([0-9A-Fa-f]{8})"?\s*=', f.read_text()):
                existing.add(int(m.group(1), 16))

    targets = {}     # addr -> source vtable address (first seen)
    vtable_count = 0
    slot_total = 0

    for nm, lo, hi in REGIONS:
        o = lo - BASE
        end = hi - BASE
        while o < end:
            if not is_cp(be32(o)):
                o += 4
                continue
            # extend a run of consecutive code-pointers
            run_start = o
            while o < end and is_cp(be32(o)):
                o += 4
            run_len = (o - run_start) // 4
            if run_len < MIN_RUN:
                continue
            run_vals = [be32(run_start + i * 4) for i in range(run_len)]
            if (max(run_vals) - min(run_vals)) < MIN_SPREAD:
                continue  # jump/switch table (targets within one function), not a vtable
            vtable_count += 1
            for i in range(run_len):
                slot_off = run_start + i * 4
                t = be32(slot_off)
                slot_total += 1
                if t <= EXLO:
                    continue
                if IMPLO <= t < IMPHI:
                    continue  # SDK-managed import thunk (see IMPLO/IMPHI)
                if not is_terminator(insn(t - 4)):
                    continue
                if t in existing:
                    continue
                targets.setdefault(t, BASE + run_start)

    out = sorted(targets)
    with OUT.open("w", encoding="utf-8") as f:
        f.write("# AUTO-GENERATED by scripts/gen_vtable_funcs.py -- do not hand-edit.\n")
        f.write("#\n")
        f.write("# Vtable / code-pointer-table entry points recovered structurally\n")
        f.write("# (the game has RTTI disabled, so the SDK's RTTI-based vtable scanner\n")
        f.write("# finds nothing). Each is a CONFIG function entry; codegen auto-sizes\n")
        f.write("# it. Entries already in sciv_config.toml / sciv_fixups.toml are\n")
        f.write("# omitted here.\n")
        f.write("#\n")
        f.write(f"# regions scanned : {', '.join(n for n,_,_ in REGIONS)}\n")
        f.write(f"# exec range      : 0x{EXLO:08X}-0x{EXHI:08X}\n")
        f.write(f"# min run length  : {MIN_RUN}\n")
        f.write(f"# min target span : 0x{MIN_SPREAD:X} (smaller = jump table, excluded)\n")
        f.write(f"# tables found    : {vtable_count}  (slots inspected: {slot_total})\n")
        f.write(f"# NEW entries     : {len(out)}  (already in config/fixups: skipped)\n")
        f.write("\n[functions]\n")
        for t in out:
            f.write(f'"0x{t:08X}" = {{ }}\n')

    print(f"basefile        : {BASEFILE}")
    print(f"tables found    : {vtable_count} (>= {MIN_RUN} ptrs)")
    print(f"slots inspected : {slot_total}")
    print(f"existing config : {len(existing)} keys (skipped)")
    print(f"NEW entries     : {len(out)}")
    print(f"wrote           : {OUT}")


if __name__ == "__main__":
    sys.exit(main())
