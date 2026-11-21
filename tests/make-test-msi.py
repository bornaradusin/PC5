#!/usr/bin/env python3
# PC5's tests: Windows Installer packages made here, for pc5_msi.c to read and install
# (tests/test_pc5.c). A compound file ([MS-CFB]) with Windows Installer's tables, and
# cabinets stored, MSZIP (zlib) and LZX (a small LZX encoder of its own, written from
# Microsoft's "LZX DELTA Compression and Decompression"; cabextract reads its cabinets).
#
#   make-test-msi.py OUT     writes OUT/physx.msi, OUT/ext.cab beside it, OUT/setup.exe (a
#                            program carrying physx.msi), OUT/lzx.cab and OUT/expected/ (each
#                            file as it must be installed, under drive_c)
import heapq
import os
import struct
import sys
import zlib

# ---- bits -------------------------------------------------------------------------------


class Bits:
    """LZX's bit order: 16-bit little-endian words, each filled from its top bit."""

    def __init__(self):
        self.out = bytearray()
        self.word = 0
        self.used = 0

    def put(self, value, count):
        for i in range(count - 1, -1, -1):
            self.word = (self.word << 1) | ((value >> i) & 1)
            self.used += 1
            if self.used == 16:
                self.out += struct.pack('<H', self.word)
                self.word = self.used = 0

    def align(self):
        if self.used:
            self.put(0, 16 - self.used)

    def aligned(self):
        return self.used == 0


# ---- Huffman ------------------------------------------------------------------------------


def code_lengths(freqs, limit):
    """Huffman code lengths of at most limit bits; at least two symbols get one."""
    freqs = list(freqs)
    used = [i for i, f in enumerate(freqs) if f]
    while len(used) < 2:
        spare = next(i for i in range(len(freqs)) if not freqs[i])
        freqs[spare] = 1
        used.append(spare)
    while True:
        heap = [(f, i, (i,)) for i, f in enumerate(freqs) if f]
        heapq.heapify(heap)
        lengths = [0] * len(freqs)
        tie = len(freqs)
        while len(heap) > 1:
            f1, _, s1 = heapq.heappop(heap)
            f2, _, s2 = heapq.heappop(heap)
            for s in s1 + s2:
                lengths[s] += 1
            heapq.heappush(heap, (f1 + f2, tie, s1 + s2))
            tie += 1
        if max(lengths) <= limit:
            return lengths
        freqs = [(f >> 1) | 1 if f else 0 for f in freqs]


def canonical(lengths):
    codes, code = {}, 0
    for length in range(1, 17):
        for sym, l in enumerate(lengths):
            if l == length:
                codes[sym] = (code, length)
                code += 1
        code <<= 1
    return codes


# ---- LZX ----------------------------------------------------------------------------------
FRAME = 32768
SLOTS = {15: 30, 16: 32, 17: 34, 18: 36, 19: 38, 20: 42, 21: 50}
EXTRA, BASE = [], []
j = 0
for i in range(0, 51, 2):
    EXTRA += [j, j]
    if i and j < 17:
        j += 1
EXTRA = EXTRA[:51]
b = 0
for e in EXTRA:
    BASE.append(b)
    b += 1 << e


def e8_forward(data, filesize):
    """What the decoder turns back: each E8's relative target as an absolute one, per frame."""
    data = bytearray(data)
    for start in range(0, len(data), FRAME):
        size = min(FRAME, len(data) - start)
        if start // FRAME >= 32768 or size <= 10:
            continue
        i = 0
        while i < size - 10:
            at = start + i
            if data[at] != 0xE8:
                i += 1
                continue
            curpos = at
            rel = struct.unpack_from('<i', data, at + 1)[0]
            if -curpos <= rel < filesize:
                absolute = rel + curpos if rel < filesize - curpos else rel - filesize
                struct.pack_into('<i', data, at + 1, absolute)
            i += 5
    return bytes(data)


def lzx_compress(data, window_bits, plan, filesize=0):
    """data as one LZX stream; plan: [(block type, bytes)] covering it. Returns the stream
    split at each frame's end (one cabinet data block each)."""
    wsize = 1 << window_bits
    slots = SLOTS[window_bits]
    if filesize:
        data = e8_forward(data, filesize)
    bits = Bits()
    chunks, chunk_start = [], 0
    bits.put(1 if filesize else 0, 1)
    if filesize:
        bits.put(filesize >> 16, 16)
        bits.put(filesize & 0xFFFF, 16)
    prev_main = [0] * (256 + slots * 8)
    prev_len = [0] * 249
    reps = [1, 1, 1]
    pos = 0
    next_frame = FRAME
    head = {}

    def frame_end_check():
        nonlocal next_frame, chunk_start
        while pos >= next_frame and next_frame <= len(data):
            bits.align()
            chunks.append(bytes(bits.out[chunk_start:]))
            chunk_start = len(bits.out)
            next_frame += FRAME

    def write_lengths(lengths, prev, first, last):
        # each as its change from prev (mod 17); runs of zeros as 17 and 18
        syms = []
        x = first
        while x < last:
            if lengths[x] == 0:
                run = 0
                while x + run < last and lengths[x + run] == 0 and run < 51:
                    run += 1
                if run >= 20:
                    syms.append((18, run - 20, 5))
                    x += run
                    continue
                if run >= 4:
                    syms.append((17, run - 4, 4))
                    x += run
                    continue
            syms.append(((prev[x] - lengths[x]) % 17, None, 0))
            x += 1
        freqs = [0] * 20
        for s, _, _ in syms:
            freqs[s] += 1
        pre = code_lengths(freqs, 15)
        for l in pre:
            bits.put(l, 4)
        codes = canonical(pre)
        for s, extra, n in syms:
            bits.put(*codes[s])
            if extra is not None:
                bits.put(extra, n)

    for block_type, block_size in plan:
        end = pos + block_size
        bits.put(block_type, 3)
        bits.put(block_size >> 8, 16)
        bits.put(block_size & 0xFF, 8)
        if block_type == 3:
            if bits.aligned():
                bits.put(0, 16)
            else:
                bits.align()
            for r in reps:
                bits.out += struct.pack('<I', r)
            raw = data[pos:end]
            # frames inside: nothing to align (no bits pending)
            for k in range(0, len(raw)):
                bits.out.append(raw[k])
                pos += 1
                if pos == next_frame and next_frame <= len(data):
                    chunks.append(bytes(bits.out[chunk_start:]))
                    chunk_start = len(bits.out)
                    next_frame += FRAME
            if block_size & 1:
                bits.out.append(0)
            for k in range(max(0, pos - 3), pos):
                if k + 3 <= len(data):
                    head.setdefault(bytes(data[k:k + 3]), []).append(k)
            continue
        # The block's symbols: literals, new offsets and repeated ones.
        symbols = []
        p = pos
        r = list(reps)
        while p < end:
            best_len, best_off = 0, 0
            for idx, off in enumerate(r):
                if off > p or off >= wsize - 3:
                    continue
                n = 0
                while p + n < end and n < 257 and data[p + n] == data[p + n - off]:
                    n += 1
                    if (p + n) % wsize == 0:
                        break
                if n >= 2 and n > best_len:
                    best_len, best_off = n, ('rep', idx)
            key = bytes(data[p:p + 3])
            for cand in reversed(head.get(key, [])[-16:]):
                off = p - cand
                if off >= wsize - 3:
                    continue
                n = 0
                while p + n < end and n < 257 and data[p + n] == data[cand + n]:
                    n += 1
                    if (p + n) % wsize == 0:
                        break
                if n >= 3 and n > best_len + 1:
                    best_len, best_off = n, ('new', off)
            if best_len >= 2:
                kind, value = best_off
                if kind == 'rep':
                    slot = value
                    off = r[value]
                    if value == 1:
                        r[0], r[1] = r[1], r[0]
                    elif value == 2:
                        r[0], r[2] = r[2], r[0]
                    symbols.append(('match', best_len, slot, 0))
                else:
                    formatted = value + 2
                    slot = max(s for s in range(3, slots) if BASE[s] <= formatted)
                    symbols.append(('match', best_len, slot, formatted - BASE[slot]))
                    r = [value, r[0], r[1]]
                for k in range(p, p + best_len):
                    if k + 3 <= len(data):
                        head.setdefault(bytes(data[k:k + 3]), []).append(k)
                p += best_len
            else:
                symbols.append(('lit', data[p]))
                if p + 3 <= len(data):
                    head.setdefault(bytes(data[p:p + 3]), []).append(p)
                p += 1
        main_f = [0] * (256 + slots * 8)
        len_f = [0] * 249
        align_f = [0] * 8
        main_f[0xE8] += 1   # keeps the E8 translation on, as the stream says
        for s in symbols:
            if s[0] == 'lit':
                main_f[s[1]] += 1
            else:
                _, length, slot, verbatim = s
                header = min(length - 2, 7)
                main_f[256 + slot * 8 + header] += 1
                if header == 7:
                    len_f[length - 9] += 1
                if block_type == 2 and slot >= 3 and EXTRA[slot] >= 3:
                    align_f[verbatim & 7] += 1
        main_l = code_lengths(main_f, 16)
        len_l = code_lengths(len_f, 16) if any(len_f) else [0] * 249
        if block_type == 2:
            align_l = code_lengths(align_f, 7)
            for l in align_l:
                bits.put(l, 3)
            align_c = canonical(align_l)
        write_lengths(main_l, prev_main, 0, 256)
        write_lengths(main_l, prev_main, 256, 256 + slots * 8)
        write_lengths(len_l, prev_len, 0, 249)
        prev_main, prev_len = main_l, len_l
        main_c, len_c = canonical(main_l), canonical(len_l)
        for s in symbols:
            if s[0] == 'lit':
                bits.put(*main_c[s[1]])
                pos += 1
            else:
                _, length, slot, verbatim = s
                header = min(length - 2, 7)
                bits.put(*main_c[256 + slot * 8 + header])
                if header == 7:
                    bits.put(*len_c[length - 9])
                if slot >= 3:
                    extra = EXTRA[slot]
                    if block_type == 2 and extra >= 3:
                        if extra > 3:
                            bits.put(verbatim >> 3, extra - 3)
                        bits.put(*align_c[verbatim & 7])
                    elif extra:
                        bits.put(verbatim, extra)
                pos += length
            frame_end_check()
        reps = r
    bits.align()
    if chunk_start < len(bits.out) or len(chunks) * FRAME < len(data):
        chunks.append(bytes(bits.out[chunk_start:]))
    return chunks


# ---- cabinets -------------------------------------------------------------------------------


def cabinet(files, kind, lzx_plan=None, window_bits=16, filesize=0, same=()):
    """files: [(name, bytes)] in one folder; kind 0 stored, 1 MSZIP, 3 LZX. same: [(name, of)],
    a file listed right after the file of that name, with its bytes (stored once), as NVIDIA's
    PhysX installer lists two copies of one DLL."""
    stream = b''.join(data for _, data in files)
    blocks = []   # (compressed, uncompressed size)
    if kind == 0:
        for at in range(0, len(stream), FRAME):
            blocks.append((stream[at:at + FRAME], len(stream[at:at + FRAME])))
    elif kind == 1:
        for at in range(0, len(stream), FRAME):
            piece = stream[at:at + FRAME]
            history = stream[max(0, at - FRAME):at]
            z = zlib.compressobj(6, zlib.DEFLATED, -15, zdict=history) if history else zlib.compressobj(6, zlib.DEFLATED, -15)
            blocks.append((b'CK' + z.compress(piece) + z.flush(), len(piece)))
    else:
        chunks = lzx_compress(stream, window_bits, lzx_plan, filesize)
        for i, chunk in enumerate(chunks):
            blocks.append((chunk, min(FRAME, len(stream) - i * FRAME)))
    type_compress = kind | (window_bits << 8 if kind == 3 else 0)
    entries, offset = [], 0   # (name, size, offset)
    for name, data in files:
        entries.append((name, len(data), offset))
        entries += [(copy, len(data), offset) for copy, of in same if of == name]
        offset += len(data)
    names = [name.encode() + b'\0' for name, _, _ in entries]
    header_size, folder_size = 36, 8
    files_at = header_size + folder_size
    files_size = sum(16 + len(n) for n in names)
    data_at = files_at + files_size
    total = data_at + sum(8 + len(c) for c, _ in blocks)
    out = bytearray(b'MSCF' + struct.pack('<IIIIIBBHHHHH', 0, total, 0, files_at, 0, 3, 1, 1, len(entries), 0, 0x1234, 0))
    out += struct.pack('<IHH', data_at, len(blocks), type_compress)
    for (name, size, at), raw in zip(entries, names):
        out += struct.pack('<IIHHHH', size, at, 0, 0x5821, 0, 0x20) + raw
    for comp, size in blocks:
        out += struct.pack('<IHH', 0, len(comp), size) + comp
    assert len(out) == total
    return bytes(out)


# ---- the compound file ---------------------------------------------------------------------
MIME = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._'


def encode_name(name, table):
    units = [0x4840] if table else []
    i = 0
    while i < len(name):
        c = name[i]
        if c in MIME:
            if i + 1 < len(name) and name[i + 1] in MIME:
                units.append(0x3800 + MIME.index(c) + (MIME.index(name[i + 1]) << 6))
                i += 2
                continue
            units.append(0x4800 + MIME.index(c))
        else:
            units.append(ord(c))
        i += 1
    return units


def compound_file(streams):
    """streams: [(name units, bytes)]. A version 3 compound file, small streams in the mini
    stream, the directory a balanced tree."""
    sector, mini, cutoff = 512, 64, 4096
    ENDOFCHAIN, FREESECT, FATSECT = 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFD
    ministream = bytearray()
    minifat = []
    entries = []   # (units, start, size)
    big = []       # (entry index, bytes)
    for units, data in streams:
        if len(data) < cutoff:
            start = len(ministream) // mini if data else ENDOFCHAIN
            count = (len(data) + mini - 1) // mini
            for k in range(count):
                minifat.append(start + k + 1 if k + 1 < count else ENDOFCHAIN)
            ministream += data + b'\0' * (count * mini - len(data))
            entries.append([units, start, len(data)])
        else:
            entries.append([units, None, len(data)])
            big.append((len(entries) - 1, data))
    sectors = []   # each: bytes of one sector
    fat = []

    def place(data):
        count = max(1, (len(data) + sector - 1) // sector)
        start = len(sectors)
        for k in range(count):
            sectors.append(data[k * sector:(k + 1) * sector].ljust(sector, b'\0'))
            fat.append(start + k + 1 if k + 1 < count else ENDOFCHAIN)
        return start
    for index, data in big:
        entries[index][1] = place(data)
    root_start = place(bytes(ministream)) if ministream else ENDOFCHAIN
    minifat_bytes = b''.join(struct.pack('<I', v) for v in minifat)
    minifat_start = place(minifat_bytes) if minifat else ENDOFCHAIN
    minifat_count = (len(minifat_bytes) + sector - 1) // sector if minifat else 0

    # The directory: the root, then the streams in a balanced tree by CFB's order.
    def order(units):
        return (len(units), [u - 32 if 0x61 <= u <= 0x7A else u for u in units])
    sorted_ids = sorted(range(len(entries)), key=lambda i: order(entries[i][0]))
    left, right = {}, {}

    def build(ids):
        if not ids:
            return 0xFFFFFFFF
        mid = len(ids) // 2
        node = ids[mid] + 1
        left[node] = build(ids[:mid])
        right[node] = build(ids[mid + 1:])
        return node
    child = build(sorted_ids)

    def entry(units, kind, l, r, c, start, size, clsid=b'\0' * 16):
        name = b''.join(struct.pack('<H', u) for u in units) + b'\0\0'
        return (name.ljust(64, b'\0') + struct.pack('<HBBIII', len(name), kind, 1, l, r, c) + clsid +
                struct.pack('<I', 0) + b'\0' * 16 + struct.pack('<III', start, size, 0))
    root_units = [ord(c) for c in 'Root Entry']
    msi_database = bytes([0x84, 0x10, 0x0C, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46])
    directory = entry(root_units, 5, 0xFFFFFFFF, 0xFFFFFFFF, child, root_start, len(ministream), msi_database)
    for i, (units, start, size) in enumerate(entries):
        node = i + 1
        directory += entry(units, 2, left[node], right[node], 0xFFFFFFFF, start, size)
    while len(directory) % sector:
        directory += entry([], 0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0)
    dir_start = place(directory)
    # The FAT, counting its own sectors.
    fat_sectors = 1
    while (len(fat) + fat_sectors) > fat_sectors * (sector // 4):
        fat_sectors += 1
    assert fat_sectors <= 109
    fat_start = len(sectors)
    fat += [FATSECT] * fat_sectors
    fat += [FREESECT] * (fat_sectors * (sector // 4) - len(fat))
    fat_bytes = b''.join(struct.pack('<I', v) for v in fat)
    for k in range(fat_sectors):
        sectors.append(fat_bytes[k * sector:(k + 1) * sector])
    difat = [fat_start + k for k in range(fat_sectors)] + [FREESECT] * (109 - fat_sectors)
    header = (bytes([0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1]) + b'\0' * 16 +
              struct.pack('<HHHHH', 0x3E, 3, 0xFFFE, 9, 6) + b'\0' * 6 +
              struct.pack('<IIIIIIIII', 0, fat_sectors, dir_start, 0, cutoff, minifat_start, minifat_count,
                          ENDOFCHAIN, 0) +
              b''.join(struct.pack('<I', v) for v in difat))
    assert len(header) == 512
    return header + b''.join(sectors)


# ---- Windows Installer's tables -----------------------------------------------------------
S, SN, I2, I2N, I4, I4N, KEY = 0x0D00, 0x1D00, 0x0502, 0x1502, 0x0104, 0x1104, 0x2000


def package(tables, streams):
    """tables: {name: ([(column, type)], rows)} with type S/SN (+ size) or I2/I4 (+N nullable)."""
    strings, index = [None], {}

    def sid(text):
        if not text:   # an empty string is none, as Windows Installer keeps it
            return 0
        if text not in index:
            index[text] = len(strings)
            strings.append(text)
        return index[text]
    out = []
    tables = dict(tables)
    names = sorted(tables)
    columns_rows = []
    for name in names:
        for number, (column, kind) in enumerate(tables[name][0], 1):
            columns_rows.append((name, number, column, kind))

    def column_major(cols, rows):
        data = bytearray()
        for c, (_, kind) in enumerate(cols):
            for row in rows:
                v = row[c]
                if kind & 0x0800:   # a string
                    data += struct.pack('<H', sid(v))
                elif (kind & 0xFF) <= 2:
                    data += struct.pack('<H', 0 if v is None else (v + 0x8000) & 0xFFFF)
                else:
                    data += struct.pack('<I', 0 if v is None else (v + 0x80000000) & 0xFFFFFFFF)
        return bytes(data)
    for name in names:
        cols, rows = tables[name]
        out.append((encode_name(name, True), column_major(cols, rows)))
    out.append((encode_name('_Tables', True), b''.join(struct.pack('<H', sid(n)) for n in names)))
    columns_rows.sort()
    cols = [('Table', S | 64), ('Number', I2), ('Name', S | 64), ('Type', I2)]
    out.append((encode_name('_Columns', True), column_major(cols, [(t, n, c, k) for t, n, c, k in columns_rows])))
    pool, data = bytearray(struct.pack('<HH', 1252, 0)), bytearray()
    for text in strings[1:]:
        raw = text.encode('cp1252')
        pool += struct.pack('<HH', len(raw), 1)
        data += raw
    out.append((encode_name('_StringPool', True), bytes(pool)))
    out.append((encode_name('_StringData', True), bytes(data)))
    for name, data in streams:
        out.append((encode_name(name, False), data))
    return compound_file(out)


# ---- the test package -----------------------------------------------------------------------


def main(out):
    os.makedirs(out, exist_ok=True)
    expected = os.path.join(out, 'expected')
    # Contents: text, and code-like bytes with E8s for the LZX cabinet, which spans frames and
    # wraps its 32 KiB window.
    common = [('PhysXLoader.dll', b'PhysX loader ' * 900), ('PhysXCooking.dll', bytes(range(256)) * 40),
              ('cudart32.dll', b'')]
    rng = 12345
    code = bytearray()
    while len(code) < 90000:
        rng = (rng * 1103515245 + 12345) & 0x7FFFFFFF
        if rng % 7 == 0:
            code += b'\xE8' + struct.pack('<i', (rng >> 8) % 40000 - 20000)
        elif rng % 5 == 0:
            code += code[-200:-180] if len(code) > 200 else b'push ebp'
        else:
            code += bytes([rng & 0xFF, (rng >> 8) & 0x3F])
    lzx_files = [('PhysXCore.dll', bytes(code)), ('PhysXDevice.dll', b'device ' * 3001 + b'end')]
    ext_files = [('readme.txt', b'NVIDIA PhysX test package\r\n'), ('old.dll', b'per user')]
    tables = {
        'Property': ([('Property', S | 72 | KEY), ('Value', S)],
                     [('ProductName', 'PC5 Test PhysX'), ('ProductVersion', '9.13.0604'), ('ALLUSERS', '1')]),
        'Directory': ([('Directory', S | 72 | KEY), ('Directory_Parent', SN | 72), ('DefaultDir', S | 255)], [
            ('TARGETDIR', None, 'SourceDir'), ('ProgramFilesFolder', 'TARGETDIR', '.'),
            ('NVIDIA', 'ProgramFilesFolder', 'NVIDIA~1|NVIDIA Corporation'), ('PHYSX', 'NVIDIA', 'PhysX'),
            ('COMMON', 'PHYSX', 'Common'), ('ENGINE', 'PHYSX', 'Engine:Src'), ('V2', 'ENGINE', 'v2.8.3'),
            ('SystemFolder', 'TARGETDIR', '.'), ('System64Folder', 'TARGETDIR', '.'),
            ('AppDataFolder', 'TARGETDIR', '.'), ('DOCS', 'COMMON', '.')]),
        'Component': ([('Component', S | 72 | KEY), ('ComponentId', SN | 38), ('Directory_', S | 72),
                       ('Attributes', I2), ('Condition', SN | 255), ('KeyPath', SN | 72)], [
            ('Common', '{1}', 'COMMON', 0, None, 'loader'), ('Engine', '{2}', 'V2', 0, '', 'core'),
            ('Wow', '{3}', 'SystemFolder', 0, 'VersionNT64', 'wowloader'),
            ('Sys32', '{4}', 'SystemFolder', 0, 'NOT VersionNT64', 'sysloader'),
            ('Sys64', '{5}', 'System64Folder', 0x100, 'VersionNT64 AND (Msix64 OR Intel64)', 'cuda64'),
            ('User', '{6}', 'AppDataFolder', 0, None, 'old'), ('Docs', '{7}', 'DOCS', 0, 'ProductName >< "PhysX"', 'readme')]),
        'File': ([('File', S | 72 | KEY), ('Component_', S | 72), ('FileName', S | 255), ('FileSize', I4),
                  ('Version', SN | 72), ('Language', SN | 20), ('Attributes', I2N), ('Sequence', I2)], [
            ('loader', 'Common', 'PHYSXL~1.DLL|PhysXLoader.dll', 1, None, None, 0x4000, 1),
            ('cooking', 'Common', 'PhysXCooking.dll', 1, None, None, 0x4000, 2),
            ('cuda64', 'Sys64', 'cudart32.dll', 0, None, None, 0x4000, 3),
            ('core', 'Engine', 'PhysXCore.dll', 1, None, None, 0x4000, 4),
            ('device', 'Engine', 'PhysXDevice.dll', 1, None, None, 0x4000, 5),
            ('readme', 'Docs', 'readme.txt', 1, None, None, 0x4000, 6),
            ('old', 'User', 'old.dll', 1, None, None, 0x4000, 7)]),
        'Media': ([('DiskId', I2 | KEY), ('LastSequence', I4), ('DiskPrompt', SN | 64), ('Cabinet', SN | 255),
                   ('VolumeLabel', SN | 32), ('Source', SN | 72)],
                  [(1, 3, None, '#data1.cab', None, None), (2, 5, None, '#data2.cab', None, None),
                   (3, 7, None, 'ext.cab', None, None)]),
        'Registry': ([('Registry', S | 72 | KEY), ('Root', I2), ('Key', S | 255), ('Name', SN | 255),
                      ('Value', SN | 0), ('Component_', S | 72)], [
            ('r1', 2, 'Software\\AGEIA Technologies', 'PhysX Version', '9.13.0604', 'Common'),
            ('r2', 2, 'Software\\AGEIA Technologies', 'enableLocalPhysXCore', '#1', 'Common'),
            ('r3', -1, 'Software\\AGEIA Technologies', 'PhysXCore Path', '[#core]', 'Engine'),
            ('r4', 2, 'Software\\NVIDIA Corporation\\PhysX', None, '[COMMON]', 'Sys64'),
            ('r5', 2, 'Software\\AGEIA Technologies', 'Bytes', '#x0a0B', 'Common'),
            ('r6', 2, 'Software\\AGEIA Technologies', 'Expand', '#%[ProgramFilesFolder]x "q"', 'Common'),
            ('r7', 1, 'Software\\PhysX', 'User', 'yes', 'Common'),
            ('r8', 2, 'Software\\Skipped', 'Multi', 'a[~]b', 'Common'),
            ('r9', 2, 'Software\\Off', 'Gone', 'x', 'Sys32')]),
        'Environment': ([('Environment', S | 72 | KEY), ('Name', S | 255), ('Value', SN | 255), ('Component_', S | 72)],
                        [('e1', '=-*PATH', '[~];[COMMON]', 'Common'), ('e2', '*CUDA', 'x', 'Common')]),
    }
    streams = [('data1.cab', cabinet([('loader', common[0][1]), ('cooking', common[1][1]), ('cuda64', common[2][1])], 1)),
               ('data2.cab', cabinet([('core', lzx_files[0][1]), ('device', lzx_files[1][1])], 3,
                                     [(1, 40000), (2, 30001), (3, 9999), (1, len(lzx_files[0][1]) + len(lzx_files[1][1]) - 80000)],
                                     window_bits=15, filesize=12000000))]
    msi = package(tables, streams)
    with open(os.path.join(out, 'physx.msi'), 'wb') as f:
        f.write(msi)
    with open(os.path.join(out, 'ext.cab'), 'wb') as f:
        f.write(cabinet([('readme', ext_files[0][1]), ('old', ext_files[1][1])], 0))
    # A setup program carrying it, as NVIDIA's do.
    with open(os.path.join(out, 'setup.exe'), 'wb') as f:
        f.write(b'MZ' + b'\x90' * 4093 + b'\xd0\xcf\x11\xe0 not a package' + b'\0' * 999 + msi + b'trailer' * 50)
    # The LZX cabinet alone, for cabextract.
    with open(os.path.join(out, 'lzx.cab'), 'wb') as f:
        f.write(streams[1][1])
    # Files that share their bytes, as NVIDIA's PhysX 9.13.0604 cabinet has them: two copies at
    # the folder's start, then a file, then a copy of a file that spans MSZIP blocks.
    shared = [('first', b'first copy ' * 3000), ('middle', b'middle'), ('last', bytes(code))]
    with open(os.path.join(out, 'shared.cab'), 'wb') as f:
        f.write(cabinet(shared, 1, same=[('first-again', 'first'), ('last-again', 'last')]))
    os.makedirs(os.path.join(expected, 'shared'), exist_ok=True)
    for name, data in shared + [('first-again', shared[0][1]), ('last-again', shared[2][1])]:
        with open(os.path.join(expected, 'shared', name), 'wb') as f:
            f.write(data)
    files = {
        'Program Files (x86)/NVIDIA Corporation/PhysX/Common/PhysXLoader.dll': common[0][1],
        'Program Files (x86)/NVIDIA Corporation/PhysX/Common/PhysXCooking.dll': common[1][1],
        'windows/system32/cudart32.dll': common[2][1],
        'Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.3/PhysXCore.dll': lzx_files[0][1],
        'Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.3/PhysXDevice.dll': lzx_files[1][1],
        'Program Files (x86)/NVIDIA Corporation/PhysX/Common/readme.txt': ext_files[0][1],
    }
    for path, data in files.items():
        full = os.path.join(expected, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'wb') as f:
            f.write(data)


if __name__ == '__main__':
    main(sys.argv[1])
