#!/usr/bin/env python3
"""Install an OP-1 firmware update (.op1 or unpacked directory) into an existing op1emu NAND image, offline.

    op1nand_install.py nand.bin op1_246_custom-repacked.op1             # in place (keeps nand.bin.bak)
    op1nand_install.py nand.bin fw.op1 -o new.bin                       # write a new image
    op1nand_install.py nand.bin fw.op1 --fast                           # skip te-boot and the read-back check
    op1nand_install.py nand.bin --list                                  # show the /yaffs2 tree of an image
    op1nand_install.py nand.bin --get user/op1.db=op1.db                # copy a file out of the image
    op1nand_install.py nand.bin --put user/op1.db=op1.db                # replace / add files (no firmware)

Does what te-boot's "Upload firmware" does, without the emulator or USB: the package files are written into
/yaffs2 (replacing files of the same name), all other files stay (user data: presets, tape, the FAT disk image
from the first boot), and the boot area gets the package's LDRs (block 0 te-boot.ldr, block 4 OP1_vdk.ldr).
The YAFFS2 area is rebuilt from the merged tree with op1emu's mknand writer (fresh, no checkpoint: the firmware
scans it at mount). Files keep their contents, not their modes / times.

Note: te-boot only replaces itself when the package's te-boot version is higher; here the package's te-boot.ldr
is always written (identical for stock-based custom firmware). --keep-teboot keeps the image's own.

Needs op1emu's tools/mknand/mknand.py (default ~/src/op1emu/tools/mknand, or --mknand DIR).
"""
import argparse, os, shutil, struct, sys

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('image', help='op1emu NAND image (emu layout, as written by mknand.py / op1emu)')
ap.add_argument('firmware', nargs='?', help='.op1 update file or unpacked firmware directory')
ap.add_argument('-o', '--output', help='output image (default: replace IMAGE, keeping IMAGE.bak)')
ap.add_argument('--list', action='store_true', help='list the /yaffs2 files of IMAGE and exit')
ap.add_argument('--get', action='append', default=[], metavar='PATH=FILE', help='copy /yaffs2/PATH out of IMAGE')
ap.add_argument('--put', action='append', default=[], metavar='PATH=FILE', help='write FILE as /yaffs2/PATH')
ap.add_argument('--keep-teboot', action='store_true', help="keep the image's te-boot (block 0)")
ap.add_argument('--no-verify', action='store_true', help='do not read the written image back')
ap.add_argument('--fast', action='store_true', help='= --keep-teboot --no-verify')
ap.add_argument('--mknand', default=os.path.expanduser('~/src/op1emu/tools/mknand'), help='dir of mknand.py')
a = ap.parse_args()
if a.fast:
    a.keep_teboot = a.no_verify = True
sys.path.insert(0, a.mknand)
try:
    import mknand as M
except ImportError:
    sys.exit(f'mknand.py not found in {a.mknand} (use --mknand DIR)')

OBJ_SYMLINK, OBJ_HARDLINK = 2, 4
UNLINKED, DELETED = 3, 4                                  # yaffs pseudo directories (object ids)


def read_image(path):
    """-> (data bytes of all pages, oob bytes of all pages); emu layout = all page data, then all OOB areas"""
    raw = open(path, 'rb').read()
    want = M.NPAGES * (M.PAGE + M.OOB)
    if len(raw) != want:
        sys.exit(f'{path}: {len(raw)} bytes, expected {want} (op1emu "emu" layout)')
    return raw[:M.NPAGES * M.PAGE], raw[M.NPAGES * M.PAGE:]


def read_yaffs(data, oob):
    """YAFFS2 scan of blocks YAFFS_START..YAFFS_END -> ({path: bytes}, [dir paths], {path: {chunk id: (page, n_bytes)}})"""
    chunks = {}                                            # (obj, chunk) -> (seq, page, n_bytes)
    headers = {}                                           # obj -> (seq, page)
    for blk in range(M.YAFFS_START, M.YAFFS_END + 1):
        if oob[blk * M.PPB * M.OOB] != 0xFF:               # bad block marker (first page, byte 0)
            continue
        for pg in range(M.PPB):
            p = blk * M.PPB + pg
            o = oob[p * M.OOB:(p + 1) * M.OOB]
            seq, obj, cid, nb = struct.unpack_from('<IIII', o, 4)
            if seq == 0xFFFFFFFF or seq < M.SEQ_START:     # erased, or checkpoint / summary data
                continue
            key = (seq, pg)
            if cid & 0x80000000 or cid == 0:               # object header (with / without extra info)
                oid = obj & 0x0FFFFFFF
                if oid not in headers or key > headers[oid][0]:
                    headers[oid] = (key, p)
            else:
                k = (obj & 0x0FFFFFFF, cid)
                if k not in chunks or key > chunks[k][0]:
                    chunks[k] = (key, p, nb)
    objs = {}                                              # oid -> (type, parent, name, size)
    for oid, (_, p) in headers.items():
        h = data[p * M.PAGE:p * M.PAGE + 0x200]
        otype, parent = struct.unpack_from('<ii', h, 0)
        name = h[10:10 + 256].split(b'\0', 1)[0].decode('utf-8', 'replace')
        size = struct.unpack_from('<i', h, 0x124)[0]
        objs[oid] = (otype, parent, name, size)

    def path_of(oid, depth=0):
        if oid == 1:
            return ''
        if oid not in objs or depth > 64:
            return None
        t, parent, name, _ = objs[oid]
        if parent in (UNLINKED, DELETED):
            return None
        up = path_of(parent, depth + 1)
        return None if up is None else (up + '/' + name if up else name)

    files, dirs, stored, skipped = {}, [], {}, 0
    for oid, (t, parent, name, size) in objs.items():
        path = path_of(oid)
        if path is None:
            continue
        if t == M.OBJ_DIR:
            dirs.append(path)
        elif t == M.OBJ_FILE:
            buf = bytearray()
            for c in range(1, (size + M.PAGE - 1) // M.PAGE + 1):
                if (oid, c) in chunks:
                    _, p, nb = chunks[(oid, c)]
                    part = data[p * M.PAGE:p * M.PAGE + min(nb, M.PAGE)]
                else:
                    part = b''                              # hole
                buf += part.ljust(min(M.PAGE, size - (c - 1) * M.PAGE), b'\0')
            files[path] = bytes(buf[:size])
            stored[path] = {c: chunks[(oid, c)][1:] for c in range(1, (size + M.PAGE - 1) // M.PAGE + 1)
                            if (oid, c) in chunks}
        else:
            skipped += 1
    if skipped:
        print(f'  note: {skipped} symlink / hardlink / special objects not copied')
    return files, sorted(dirs), stored


data, oob = read_image(a.image)
old_files, old_dirs, old_stored = read_yaffs(data, oob)
print(f'{a.image}: {len(old_files)} files, {len(old_dirs)} dirs, '
      f'{sum(map(len, old_files.values())) / 1e6:.1f} MB in /yaffs2')
if a.list:
    for p in sorted(old_files):
        print(f'  {len(old_files[p]):10d}  {p}')
    sys.exit(0)
for spec in a.get:
    src, dst = spec.split('=', 1)
    if src not in old_files:
        sys.exit(f'{src}: not in the image')
    open(dst, 'wb').write(old_files[src])
    print(f'  {src} -> {dst} ({len(old_files[src])} bytes)')
if a.get and not a.put and not a.firmware:
    sys.exit(0)
if not a.firmware and not a.put:
    sys.exit('firmware missing (or use --list / --get / --put)')

if a.firmware:
    new_files, new_dirs = M.load_firmware(a.firmware)
    if 'OP1_vdk.ldr' not in new_files:
        sys.exit(f'OP1_vdk.ldr missing in {a.firmware}')
    if 'te-boot.ldr' not in new_files:                     # delta package (tools/op1delta.py): as te-boot, keep it
        a.keep_teboot = True
else:                                                      # --put only: keep the image's own boot area / LDRs
    new_files, new_dirs = {}, []
    a.keep_teboot = True
for spec in a.put:
    dst, src = spec.split('=', 1)
    new_files[dst.strip('/')] = open(src, 'rb').read()
    print(f'  put {src} -> {dst}')
replaced = [p for p in new_files if p in old_files and old_files[p] != new_files[p]]
added = [p for p in new_files if p not in old_files]
files = dict(old_files); files.update(new_files)
print(f'{a.firmware or "--put"}: {len(new_files)} files -> {len(replaced)} replaced, {len(added)} added, '
      f'{len(files) - len(new_files)} kept (user data)')

nand = M.Nand()
print('boot area:')
if a.keep_teboot:                                          # copy the image's blocks 0..3 page by page
    for p in range(M.TEBOOT_BLOCK * M.PPB, M.VDK_BLOCK * M.PPB):
        o = oob[p * M.OOB:(p + 1) * M.OOB]
        if o != b'\xff' * M.OOB:
            nand.put(p, data[p * M.PAGE:(p + 1) * M.PAGE], o)
    print('  te-boot.ldr: kept from the image')
else:
    M.put_ldr(nand, M.TEBOOT_BLOCK, files['te-boot.ldr'], M.VDK_BLOCK - M.TEBOOT_BLOCK, 'te-boot.ldr')
if 'OP1_vdk.ldr' in new_files:
    M.put_ldr(nand, M.VDK_BLOCK, new_files['OP1_vdk.ldr'], M.YAFFS_START - M.VDK_BLOCK, 'OP1_vdk.ldr')
else:                                                      # keep the image's blocks 4..YAFFS_START-1
    for p in range(M.VDK_BLOCK * M.PPB, M.YAFFS_START * M.PPB):
        o = oob[p * M.OOB:(p + 1) * M.OOB]
        if o != b'\xff' * M.OOB:
            nand.put(p, data[p * M.PAGE:(p + 1) * M.PAGE], o)
    print('  OP1_vdk.ldr: kept from the image')
fs = M.Yaffs(nand)
for d in sorted(set(old_dirs) | set(new_dirs)):
    fs.mkdir(d)
def copy_chunk(fs, page_no, oid, chunk_id, n_bytes):
    """a stored chunk moved as is: same page data, so its data ECC (OOB 32..63) is reused; only the tags are new
    (mknand's Yaffs._chunk would recompute the ECC in Python: ~97 % of the run time)"""
    if fs.page == M.PPB:
        fs.block += 1; fs.page = 0; fs.seq += 1
        assert fs.block <= M.YAFFS_END - 5, 'yaffs partition full'
    page = data[page_no * M.PAGE:(page_no + 1) * M.PAGE]
    tags = struct.pack('<IIII', fs.seq, oid, chunk_id, n_bytes)
    col, lp, lpp = M.yaffs_ecc_other(tags)
    o = b'\xff' * 4 + tags + struct.pack('<B3xII', col, lp, lpp) + oob[page_no * M.OOB + 32:(page_no + 1) * M.OOB]
    fs.nand.put(fs.block * M.PPB + fs.page, page, o)
    fs.page += 1

def add_sparse(fs, path, size, keep):
    """user file as stored: only its stored chunks (holes stay holes: the empty tape is mostly holes), copied"""
    parent = fs.mkdir(os.path.dirname(path))
    oid = fs.next_id; fs.next_id += 1
    for c in sorted(keep):
        copy_chunk(fs, keep[c][0], oid, c, keep[c][1])
    fs._header(oid, M.OBJ_FILE, parent, os.path.basename(path), size)

for p in sorted(files):
    if p in new_files:
        fs.add_file(p, files[p])                           # package file: all chunks
    else:
        add_sparse(fs, p, len(files[p]), old_stored[p])    # user file: as stored
print(f'yaffs2: {len(files)} files, {len(fs.dirs) - 1} dirs -> blocks {M.YAFFS_START}..{fs.block}')

out = a.output or a.image
if not a.output:
    shutil.copyfile(a.image, a.image + '.bak')
    print(f'backup: {a.image}.bak')
tmp = out + '.tmp'
nand.write(tmp, 'emu')
os.replace(tmp, out)

if a.no_verify:
    print(f'wrote {out} (not verified)')
    sys.exit(0)
chk_files, _, _ = read_yaffs(*read_image(out))                # verify: the written image reads back as the merge
bad = [p for p in files if chk_files.get(p) != files[p]] + [p for p in chk_files if p not in files]
if bad:
    sys.exit(f'VERIFY FAILED for {len(bad)} files, e.g. {bad[:3]}')
print(f'wrote {out}: verified ({len(chk_files)} files read back identical)')
