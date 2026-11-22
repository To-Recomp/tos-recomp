#!/usr/bin/env python3
"""Merge an sdk-identify result CSV (addr,name) into symbols/GQSF.csv and report which names are
bound to a gcrecomp native replacement (HLE).

Binding a name makes the recompiler emit, at the function entry, `lookup_os_func(name)`: any HLE with
that name REPLACES the recompiled SDK function. A name that is only a label is harmless; a name that
has an HLE can change the game's behaviour, so every merge must be followed by a run of the game
(docs/DEVELOPING.md, "Merging SDK names").

Classes of change, relative to the current CSV:
  new      address and name are both absent
  renamed  the address is present with another name (the new name replaces it)
  moved    the name is present at another address: the old entry was inside the function (a fragment
           address); it is removed and the name goes to the new start
Groups (--groups, comma separated; default all): exidspdb (EXI/SI/DSP/DB, 0x8015B000 and up),
dvd (0x80100E00-0x80104FFF), os (below 0x80100E00), misc (VI/PAD/AI/AR/CARD/GX/MTX, the rest),
renamed, moved.  A `new` entry is in the group of its address; `renamed` and `moved` entries are in
their own groups whatever their address.  --exclude NAME[,NAME] keeps names out (a name whose HLE is
incompatible with the game's SDK build).

usage: merge-sdk-names.py --v2 V2.csv [--csv symbols/GQSF.csv] [--gcrecomp DIR] [--groups G,G]
                          [--exclude N,N] [--out FILE] [--evidence TSV --markdown FILE] [--dry-run]
"""
import argparse
import os
import re
import sys

HDR = ("# Dolphin SDK function names for a stripped DOL, from tools/sdk-identify. addr,name\n"
       "# Only entries whose start is verified (first instructions equal the reference's) or that two\n"
       "# independent references name identically. Manual entries: symbols/GQSF.manual.csv.\n")


def read_csv(path):
    d = {}
    for line in open(path):
        line = line.split('#')[0].strip()
        if not line:
            continue
        a, n = [x.strip() for x in line.split(',')[:2]]
        d[int(a, 16)] = n
    return d


def hle_names(gcrecomp):
    """Names in the runtime's lookup tables: entries like { "Name", func }."""
    names = {}
    d = os.path.join(gcrecomp, 'src', 'os')
    for f in sorted(os.listdir(d)):
        if not f.endswith('.cpp'):
            continue
        for m in re.finditer(r'\{\s*"([A-Za-z_0-9]+)"\s*,\s*([A-Za-z_0-9:]+)\s*\}', open(os.path.join(d, f)).read()):
            names[m.group(1)] = f"{f}:{m.group(2)}"
    return names


def group_of(addr):
    if addr >= 0x8015B000:
        return 'exidspdb'
    if 0x80100E00 <= addr < 0x80105000:
        return 'dvd'
    if addr < 0x80100E00:
        return 'os'
    return 'misc'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--v2', required=True, help='sdk-identify result CSV (addr,name)')
    ap.add_argument('--csv', default=os.path.join(os.path.dirname(__file__), '..', 'symbols', 'GQSF.csv'))
    ap.add_argument('--gcrecomp', default=os.environ.get('GCRECOMP_DIR', os.path.join(os.path.dirname(__file__), '..', '..', 'gcrecomp')),
                    help='gcrecomp checkout, to find the names that have an HLE (default $GCRECOMP_DIR)')
    ap.add_argument('--groups', default='exidspdb,dvd,os,misc,renamed,moved')
    ap.add_argument('--exclude', default='')
    ap.add_argument('--evidence', help='sdk-identify .new.tsv/.matches.tsv: method, score, start_ok, agreed per address')
    ap.add_argument('--markdown', help='write a table (name, address, class, evidence, HLE, applied) here')
    ap.add_argument('--out', help='write the result here instead of over --csv')
    ap.add_argument('--dry-run', action='store_true', help='report only, write nothing')
    a = ap.parse_args()

    groups = set(a.groups.split(','))
    excl = set(x for x in a.exclude.split(',') if x)
    old = read_csv(a.csv)
    new = read_csv(a.v2)
    hle = hle_names(a.gcrecomp)
    old_by_name = {}
    for ad, n in old.items():
        old_by_name.setdefault(n, []).append(ad)

    out = dict(old)
    rows = []  # (group, class, addr, name, old)
    for ad, n in sorted(new.items()):
        if old.get(ad) == n:
            continue
        if ad in old:
            cls, grp, prev = 'renamed', 'renamed', old[ad]
        elif n in old_by_name:
            cls, grp, prev = 'moved', 'moved', ','.join(hex(x) for x in old_by_name[n])
        else:
            cls, grp, prev = 'new', group_of(ad), ''
        applied = grp in groups and n not in excl
        rows.append((grp, cls, ad, n, prev, applied))
        if applied:
            if cls == 'moved':
                for x in old_by_name[n]:
                    out.pop(x, None)
            out[ad] = n

    print(f"{'group':9} {'class':8} {'address':10} {'name':32} {'old':24} {'HLE':3} applied")
    for grp, cls, ad, n, prev, ap_ in rows:
        print(f"{grp:9} {cls:8} 0x{ad:08X} {n:32} {prev:24} {'HLE' if n in hle else '-':3} {'yes' if ap_ else 'no'}")
    bound = [r for r in rows if r[5] and r[3] in hle]
    print(f"\n{len(rows)} changes, {sum(1 for r in rows if r[5])} applied, "
          f"{len(bound)} applied names bound to an HLE: {', '.join(r[3] for r in bound)}")
    if a.markdown:
        ev = {}
        if a.evidence:
            rd = open(a.evidence).read().splitlines()
            cols = rd[0].split('\t')
            for line in rd[1:]:
                r = dict(zip(cols, line.split('\t')))
                ev[int(r['address'], 16)] = r
        with open(a.markdown, 'w') as f:
            f.write('| name | address | class | evidence | HLE | applied |\n|---|---|---|---|---|---|\n')
            for grp, cls, ad, n, prev, ap_ in rows:
                e = ev.get(ad, {})
                evs = (f"{e.get('method','?')}, score {e.get('score','?')}, start {'verified' if e.get('start_ok')=='1' else 'unverified'}, "
                       f"{'2 refs' if e.get('agreed')=='1' else '1 ref'}") if e else ''
                if prev:
                    evs = (evs + '; ' if evs else '') + f"was {prev}"
                f.write(f"| {n} | 0x{ad:08X} | {cls} ({grp}) | {evs} | {hle.get(n, '-')} | {'yes' if ap_ else 'no'} |\n")
    # Duplicate names would be one C++ function twice.
    seen = {}
    for ad, n in out.items():
        seen.setdefault(n, []).append(ad)
    dup = {n: v for n, v in seen.items() if len(v) > 1}
    if dup:
        print("duplicate names in result:", {n: [hex(x) for x in v] for n, v in dup.items()}, file=sys.stderr)
        return 1
    if not a.dry_run:
        dest = a.out or a.csv
        with open(dest, 'w') as f:
            f.write(HDR)
            for ad, n in sorted(out.items()):
                f.write(f"0x{ad:08X},{n}\n")
        print(f"wrote {dest}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
