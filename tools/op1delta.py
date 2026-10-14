#!/usr/bin/env python3
"""Delta firmware package for development: only OP1_vdk.ldr + the files that changed since the last install.

te-boot's "1. Upload firmware" (docs/34) deletes only /yaffs2/te-boot.ldr and /yaffs2/OP1_vdk.ldr, unpacks the
tar (writes exactly the files it contains) and flashes OP1_vdk.ldr to NAND block 4. Everything else on the device
stays, so a package with the changed files is enough and installs much faster than a full update.

    op1delta.py --init op1_246_custom              # the device has this tree (e.g. after a full update)
    op1delta.py op1_246_custom -o dev.op1          # package the changes since the recorded state
    (upload dev.op1 via te-boot, check on the device)
    op1delta.py op1_246_custom --commit            # record the new tree as installed

State: <tree>.delta-state.json next to the tree (or --state FILE): path -> sha256.
Container as op1repacker writes it (CRC-32 LE of the LZMA data, LZMA alone, same filters, GNU tar).
te-boot.ldr is never packaged (blocks 0..3 = the recovery path stay untouched); deleted files cannot be removed by
an update and are only reported."""
import argparse, binascii, hashlib, io, json, lzma, os, struct, sys, tarfile

LZMA_FILTERS = [{'id': lzma.FILTER_LZMA1, 'preset': 9, 'lc': 3, 'lp': 1, 'pb': 2, 'dict_size': 2**23}]  # as op1repacker
NEVER = {'te-boot.ldr'}
ALWAYS = 'OP1_vdk.ldr'


def scan(tree):
    out = {}
    for root, dirs, files in os.walk(tree):
        dirs[:] = sorted(d for d in dirs if not d.startswith('.'))
        for f in sorted(files):
            if f.startswith('.'):
                continue
            p = os.path.join(root, f)
            out[os.path.relpath(p, tree).replace(os.sep, '/')] = hashlib.sha256(open(p, 'rb').read()).hexdigest()
    return out


def package(tree, paths):
    """GNU tar (dirs before their files, uid/gid 0 as op1repacker) -> LZMA alone -> CRC-32 LE + data"""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w', format=tarfile.GNU_FORMAT) as tar:
        seen = set()
        for p in sorted(paths):
            parts = p.split('/')
            for i in range(1, len(parts)):                       # parent directory entries
                d = '/'.join(parts[:i])
                if d not in seen:
                    seen.add(d)
                    ti = tar.gettarinfo(os.path.join(tree, d), arcname=d)
                    ti.uid = ti.gid = 0; ti.uname = ti.gname = 'root'
                    tar.addfile(ti)
            ti = tar.gettarinfo(os.path.join(tree, p), arcname=p)
            ti.uid = ti.gid = 0; ti.uname = ti.gname = 'root'
            with open(os.path.join(tree, p), 'rb') as f:
                tar.addfile(ti, f)
    data = lzma.compress(buf.getvalue(), format=lzma.FORMAT_ALONE, filters=LZMA_FILTERS)
    return struct.pack('<L', binascii.crc32(data)) + data


def unpack_names(blob):
    """check: CRC, LZMA, tar -> member names (as te-boot would see them)"""
    crc, data = struct.unpack('<L', blob[:4])[0], blob[4:]
    assert crc == binascii.crc32(data), 'CRC'
    with tarfile.open(fileobj=io.BytesIO(lzma.decompress(data)), mode='r') as t:
        return {m.name: m for m in t.getmembers()}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tree', help='unpacked firmware tree (e.g. op1_246_custom)')
    ap.add_argument('-o', '--out', help='write the delta package here')
    ap.add_argument('--state', help='state file (default <tree>.delta-state.json)')
    ap.add_argument('--init', action='store_true', help='record the tree as installed (no package)')
    ap.add_argument('--commit', action='store_true', help='record the tree as installed after an upload')
    ap.add_argument('--all', action='store_true', help='package every file (full update, no te-boot.ldr)')
    a = ap.parse_args()
    tree = os.path.normpath(a.tree)
    if not os.path.isfile(os.path.join(tree, ALWAYS)):
        sys.exit(f'{tree}: no {ALWAYS}')
    state_path = a.state or tree + '.delta-state.json'
    now = scan(tree)
    if a.init or a.commit:
        json.dump(now, open(state_path, 'w'), indent=0, sort_keys=True)
        print(f'{state_path}: {len(now)} files recorded as installed')
        return
    if not a.all and not os.path.isfile(state_path):
        sys.exit(f'{state_path} missing: run --init with the tree that is on the device (or use --all)')
    old = {} if a.all else json.load(open(state_path))
    changed = sorted(p for p, h in now.items() if old.get(p) != h and p not in NEVER)
    gone = sorted(p for p in old if p not in now)
    paths = sorted(set(changed) | {ALWAYS})
    for p in paths:
        print(('  new      ' if p not in old else '  changed  ' if p in changed else '  (always) ') + p)
    for p in gone:
        print(f'  WARNING: {p} was deleted; an update cannot remove it from the device')
    if not a.out:
        print(f'{len(paths)} files would be packaged (-o FILE to write)')
        return
    blob = package(tree, paths)
    names = unpack_names(blob)                                    # self-check of the written container
    assert all(p in names for p in paths) and not (NEVER & set(names)), names.keys()
    open(a.out, 'wb').write(blob)
    raw = sum(os.path.getsize(os.path.join(tree, p)) for p in paths)
    print(f'{a.out}: {len(paths)} files, {raw / 1e6:.2f} MB -> {len(blob) / 1e6:.2f} MB '
          f'(full update: {sum(os.path.getsize(os.path.join(tree, p)) for p in now if p not in NEVER) / 1e6:.1f} MB)')
    print(f'after a successful upload: {os.path.basename(sys.argv[0])} {a.tree} --commit')


if __name__ == '__main__':
    main()
