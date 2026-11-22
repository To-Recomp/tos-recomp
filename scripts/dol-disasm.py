#!/usr/bin/env python3
"""Disassemble a range of a GameCube DOL (capstone, big-endian PowerPC with paired singles) and list
the direct callers of an address with the instructions that set up r3-r5 before each call.

  dol-disasm.py MAIN.DOL --range 0x800F94A8:0x800F9528      # disassemble
  dol-disasm.py MAIN.DOL --callers 0x800F94A8 [--context 8] [--limit 10]
Needs the capstone Python package.
"""
import argparse
import struct
import sys

from capstone import CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN, Cs

try:
    from capstone import CS_MODE_PS
    MODE = CS_MODE_32 | CS_MODE_BIG_ENDIAN | CS_MODE_PS
except ImportError:
    MODE = CS_MODE_32 | CS_MODE_BIG_ENDIAN


def load(path):
    d = open(path, 'rb').read()
    off = struct.unpack('>18I', d[0:72])
    addr = struct.unpack('>18I', d[72:144])
    size = struct.unpack('>18I', d[144:216])
    return [(addr[i], d[off[i]:off[i] + size[i]]) for i in range(18) if size[i] and addr[i]]


def word(secs, a):
    for base, data in secs:
        if base <= a < base + len(data):
            return struct.unpack('>I', data[a - base:a - base + 4])[0]
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dol')
    ap.add_argument('--range')
    ap.add_argument('--callers')
    ap.add_argument('--context', type=int, default=8, help='instructions shown before each call')
    ap.add_argument('--limit', type=int, default=10)
    a = ap.parse_args()
    secs = load(a.dol)
    md = Cs(CS_ARCH_PPC, MODE)

    def dis(addr):
        w = word(secs, addr)
        if w is None:
            return None
        for i in md.disasm(struct.pack('>I', w), addr):
            return f"{addr:08X}: {i.mnemonic} {i.op_str}"
        return f"{addr:08X}: .long 0x{w:08X}"

    if a.range:
        lo, hi = [int(x, 16) for x in a.range.split(':')]
        for ad in range(lo, hi, 4):
            print(dis(ad))
    if a.callers:
        tgt = int(a.callers, 16)
        n = 0
        for base, data in secs:
            for o in range(0, len(data) - 3, 4):
                w = struct.unpack('>I', data[o:o + 4])[0]
                if (w >> 26) == 18 and (w & 3) == 1:  # bl
                    li = w & 0x03FFFFFC
                    if li & 0x02000000:
                        li -= 0x04000000
                    if (base + o + li) & 0xFFFFFFFF == tgt:
                        ad = base + o
                        print(f"-- call at 0x{ad:08X}")
                        for k in range(a.context, -1, -1):
                            s = dis(ad - 4 * k)
                            if s:
                                print("   " + s)
                        n += 1
                        if n >= a.limit:
                            return
        print(f"{n} callers shown")


if __name__ == '__main__':
    sys.exit(main())
