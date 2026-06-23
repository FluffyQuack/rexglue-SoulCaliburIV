#!/usr/bin/env python3
"""
gen_callback_funcs.py - immediate-constructed code-pointer (callback) scanner (SCIV).

Adapted from the Metal Slug XX reference script for SoulCalibur IV.

Two scanners already recover function entry points for this RTTI-disabled game:
  * ReXGlue's call-graph discovery finds anything reached by a direct `bl`.
  * scripts/gen_vtable_funcs.py recovers entries reached through a *pointer table*
    in .rdata/.data (vtables / fnptr tables).

Neither catches a function whose address is **materialised in code** by a
`lis`/`addi` (or `lis`/`ori`) pair and then passed by register to a callee that
calls it indirectly (`bctrl`). This is the classic "callback / comparator passed
by pointer" shape -- e.g. a compare function handed to a sort/search routine.
There is no static pointer to it anywhere in the image (so the vtable scanner
sees nothing) and nothing ever `bl`s it (so call-graph discovery sees nothing),
so it is never registered. The first indirect call to one traps at runtime with
"Call to invalid or unregistered function at guest address 0x...".

This script recovers those entries structurally:
  * scan the executable range for `lis rD,HI` immediately followed (small window,
    same base register) by `addi rDST,rD,LO` or `ori rDST,rD,LO`;
  * compute the constructed address T = HI<<16 + signext(LO)  (addi)
                                   or  T = (HI<<16) | LO       (ori);
  * accept T only if it is a 4-aligned pointer into the executable range AND the
    instruction at T-4 is an unconditional function terminator (blr/bctr/uncond
    b/ba/zero pad) -- i.e. T begins right after a function ends, which makes it a
    genuine entry and rules out mid-function false positives (the same guard the
    vtable scanner uses), and is neither a switch-table case base nor an internal
    continuation / EH funclet.

Output is a TOML file of size-less `[functions]` entries (codegen auto-sizes
each). Entries already present in sciv_config.toml / sciv_fixups.toml /
sciv_vtables.toml are skipped so their hints / prior recovery are preserved and
no duplicate keys are emitted.

Run from the sciv project dir (after gen_vtable_funcs.py):
    python3 scripts/gen_callback_funcs.py
"""
import bisect
import re
import struct
import sys
from pathlib import Path

BASE = 0x82000000
EXLO, EXHI = 0x82110000, 0x82717CF0   # executable range (.text + PSFD00)
# XEX import-thunk table (tail of .text): SDK-managed __imp__ stubs; never
# register one as a guest function (see gen_vtable_funcs.py for the full note).
IMPLO, IMPHI = 0x8271111C, 0x8271202C
WINDOW = 4                            # max insns from lis to the paired addi/ori

PROJ = Path(__file__).resolve().parent.parent
BASEFILE = PROJ.parent / "Assets" / "SCIV" / "default.basefile.exe"
# Function keys already known (config size hints + fixups + vtable recovery).
EXISTING_FILES = [PROJ / "sciv_config.toml", PROJ / "sciv_fixups.toml",
                  PROJ / "sciv_vtables.toml"]
# Files carrying explicit `size`/`end` ranges (used to exclude interior labels).
SIZED_FILES = [PROJ / "sciv_config.toml", PROJ / "sciv_fixups.toml"]
OUT = PROJ / "sciv_callbacks.toml"


def main():
    d = BASEFILE.read_bytes()

    def be32(off):
        return struct.unpack_from(">I", d, off)[0]

    def insn(addr):
        return be32(addr - BASE)

    def is_terminator(w):
        # unconditional terminators only (so T-4 ending => T is a function start)
        if w == 0x4E800020:                    # blr
            return True
        if w == 0x4E800420:                    # bctr
            return True
        if w == 0:                             # alignment padding
            return True
        if (w >> 26) == 18 and (w & 1) == 0:   # b / ba (LK=0; bl/bla are calls)
            return True
        return False

    # function keys already known (config size hints + fixups + vtable recovery)
    existing = set()
    for f in EXISTING_FILES:
        if f.exists():
            for m in re.finditer(r'"?0x([0-9A-Fa-f]{8})"?\s*=', f.read_text()):
                existing.add(int(m.group(1), 16))

    # sized config functions define [start, start+size) ranges. A constructed
    # pointer landing strictly inside one is an *internal* label (out-of-line
    # continuation / switch case that IDA bounds within the function), not a real
    # entry -- registering it would split the function and break codegen
    # ("Overlapping boundaries"). Collect the ranges and exclude interior targets.
    ranges = []  # (start, end) for entries carrying an explicit size
    for f in SIZED_FILES:
        if not f.exists():
            continue
        for m in re.finditer(
            r'"?0x([0-9A-Fa-f]{8})"?\s*=\s*\{[^}]*\bsize\s*=\s*(0x[0-9A-Fa-f]+|\d+)',
            f.read_text(),
        ):
            start = int(m.group(1), 16)
            size = int(m.group(2), 16) if m.group(2).lower().startswith("0x") else int(m.group(2))
            if size > 0:
                ranges.append((start, start + size))
    ranges.sort()
    starts = [r[0] for r in ranges]

    def interior(addr):
        # is addr strictly inside any sized function range?
        i = bisect.bisect_right(starts, addr) - 1
        return i >= 0 and ranges[i][0] < addr < ranges[i][1]

    def is_switch_base(addi_off):
        # A jump-table case-base is built by lis+addi then consumed by
        # `add; mtctr; bctr` (computed goto) within a few insns. A genuine
        # callback pointer is instead moved to an arg reg / stored and later
        # reached via `bctrl` (a *call*). Discriminate on a `bctr` (LK=0)
        # appearing before any call/return in the small forward window.
        for k in range(1, 7):
            w = be32(addi_off + 4 * k)
            if w == 0x4E800420:            # bctr  -> switch/tail dispatch
                return True
            if w == 0x4E800420 | 1:        # bctrl -> indirect call (genuine use)
                return False
            if w == 0x4E800020:            # blr
                return False
            if (w >> 26) == 18 and (w & 1):  # bl / bla (call)
                return False
        return False

    def is_continuation(T):
        # An internal continuation / EH funclet (whose address is taken for an
        # exception/unwind table) sits right after a parent's terminator but its
        # first block branches *backward* into the code immediately preceding it
        # -- i.e. into the parent body in [T-0x80, T). A self-contained function
        # never does that. Registering such a label as a function splits the
        # parent and orphans the back-edge target. Scan T's first straight-line block.
        addr = T
        for _ in range(64):
            w = insn(addr)
            op = w >> 26
            if op == 18:                      # b / bl / ba / bla
                li = w & 0x03FFFFFC
                if li & 0x02000000:
                    li -= 0x04000000
                tgt = (li if (w >> 1) & 1 else addr + li) & 0xFFFFFFFF
                if (w & 1) == 0:              # unconditional b -> block ends here
                    return T - 0x80 <= tgt < T
                # bl: call, block continues
            elif op == 16:                    # bc (conditional)
                bd = w & 0xFFFC
                if bd & 0x8000:
                    bd -= 0x10000
                tgt = (bd if (w >> 1) & 1 else addr + bd) & 0xFFFFFFFF
                if T - 0x80 <= tgt < T:
                    return True
            elif w in (0x4E800020, 0x4E800420):  # blr / bctr -> block ends
                return False
            addr += 4
        return False

    targets = {}   # addr -> source (lis address) first seen
    lis_seen = 0
    pairs_seen = 0

    o = EXLO - BASE
    end = EXHI - BASE
    while o < end:
        w = be32(o)
        if (w >> 26) == 15 and ((w >> 16) & 0x1F) == 0:   # addis rD,0,HI  (lis)
            D = (w >> 21) & 0x1F
            HI = w & 0xFFFF
            lis_seen += 1
            # scan forward for addi/ori from rD; stop if rD is clobbered or flow leaves
            for k in range(1, WINDOW + 1):
                w2 = be32(o + 4 * k)
                op2 = w2 >> 26
                A2 = (w2 >> 16) & 0x1F
                D2 = (w2 >> 21) & 0x1F
                imm2 = w2 & 0xFFFF
                if op2 == 14 and A2 == D:          # addi rD2,rD,LO
                    lo = struct.unpack(">h", struct.pack(">H", imm2))[0]
                    T = ((HI << 16) + lo) & 0xFFFFFFFF
                elif op2 == 24 and A2 == D:         # ori rD2,rD,LO
                    T = (HI << 16) | imm2
                else:
                    # rD clobbered by an unrelated write, or a branch -> stop
                    if D2 == D or op2 in (16, 18, 19):
                        break
                    continue
                pairs_seen += 1
                if EXLO < T < EXHI and (T & 3) == 0 and is_terminator(insn(T - 4)):
                    if (T not in existing and not interior(T)
                            and not (IMPLO <= T < IMPHI)
                            and not is_switch_base(o + 4 * k)
                            and not is_continuation(T)):
                        targets.setdefault(T, BASE + o)
                # rD2==D means base was overwritten; further adds use new value
                if D2 == D:
                    break
        o += 4

    out = sorted(targets)
    with OUT.open("w", encoding="utf-8") as f:
        f.write("# AUTO-GENERATED by scripts/gen_callback_funcs.py -- do not hand-edit.\n")
        f.write("#\n")
        f.write("# Function entry points reached only through an address materialised in\n")
        f.write("# code by a lis+addi / lis+ori pair and called indirectly (callbacks /\n")
        f.write("# comparators passed by register). No static pointer references them, so\n")
        f.write("# neither call-graph discovery nor the vtable scanner finds them; the\n")
        f.write("# first indirect call traps as 'invalid or unregistered function'.\n")
        f.write("# Each is a CONFIG function entry; codegen auto-sizes it. Entries already\n")
        f.write("# in sciv_config.toml / sciv_fixups.toml / sciv_vtables.toml are omitted.\n")
        f.write("#\n")
        f.write(f"# exec range   : 0x{EXLO:08X}-0x{EXHI:08X}\n")
        f.write(f"# lis insns    : {lis_seen}  (lis+addi/ori pairs into exec range: {pairs_seen})\n")
        f.write(f"# NEW entries  : {len(out)}  (already known: skipped)\n")
        f.write("\n[functions]\n")
        for t in out:
            f.write(f'"0x{t:08X}" = {{ }}\n')

    print(f"basefile     : {BASEFILE}")
    print(f"lis insns    : {lis_seen}")
    print(f"lis+addi/ori : {pairs_seen} (constructed code pointers)")
    print(f"existing keys: {len(existing)} (skipped)")
    print(f"NEW entries  : {len(out)}")
    print(f"wrote        : {OUT}")


if __name__ == "__main__":
    sys.exit(main())
