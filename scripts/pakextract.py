#!/usr/bin/env python3
"""Extractor for RedLynx data.pak (Blackbeard, N-Gage).

Container format (reverse-engineered, verified):
  Header  (8 bytes):  u32 dataStart (== directory size), u32 fileCount
  Entry  (17 bytes, packed LE): u32 nameHash, u32 compSize, u32 uncompSize,
                                u8 flags (1 = zlib, 0 = stored), u32 offset
  Payloads: zlib streams (78 DA) or stored bytes.

Name hash (recovered from the routine at 0x100df7fc in main.dll):
  canonical path = uppercase, forward slashes, "DATA/..." prefix
  h = 0; k = len(path)
  for byte c: h += c*k;  k = 18000*(k & 0xFFFF) + (k >> 16)   (mod 2^32)
  stored hash = h & 0x7FFFFFFF
The multiplier update is Marsaglia's multiply-with-carry step, seeded with
the string length. Filenames are recovered by hashing candidate paths mined
from main.dll and from extracted file contents, iterated to a fixpoint.
"""

import argparse
import csv
import re
import struct
import zlib
from pathlib import Path

ENTRY = struct.Struct("<IIIBI")


def parse_directory(data: bytes):
    data_start, count = struct.unpack_from("<II", data, 0)
    assert 8 + count * ENTRY.size == data_start, "directory size mismatch"
    return [ENTRY.unpack_from(data, 8 + i * ENTRY.size) for i in range(count)]


def rl_hash(path: str) -> int:
    """RedLynx pak name hash over the canonical path string."""
    b = path.encode("latin1")
    h = 0
    k = len(b)
    for c in b:
        h = (h + c * k) & 0xFFFFFFFF
        k = (18000 * (k & 0xFFFF) + (k >> 16)) & 0xFFFFFFFF
    return h & 0x7FFFFFFF


def canonical(path: str) -> str:
    p = path.replace("\\", "/").upper().lstrip("/")
    if not p.startswith("DATA/"):
        p = "DATA/" + p
    return p


PRINTABLE = re.compile(rb"[ -~]{4,}")
TOKEN = re.compile(r"[A-Za-z0-9_\-\\/\.]{3,}")
HAS_EXT = re.compile(r"\.[A-Za-z0-9]{1,4}$")


FILE_KEY = re.compile(rb"^\s*(?:FILE|filename)\s+(\S+)", re.MULTILINE)


def mine_paths(blob: bytes):
    """Pull path-looking ASCII strings out of a binary or text blob."""
    out = set()
    for m in PRINTABLE.finditer(blob):
        for token in TOKEN.findall(m.group().decode("ascii")):
            if HAS_EXT.search(token) and not token.startswith("."):
                out.add(token)
    # The brace-format data files name their assets with `FILE` or `filename`
    # and escape a backslash as `\\`. That both doubles the separators and
    # lets a name contain a comma or a `!` -- `sps3_hunter,_hunted.ndl` and
    # `spt5_all_aboard!.ndl` are real level names -- so the generic token
    # scanner cannot see them. Reading the key directly gets all 66 levels and
    # all 21 unit animation sheets.
    for m in FILE_KEY.finditer(blob):
        value = m.group(1).decode("latin-1").replace("\\\\", "\\")
        if HAS_EXT.search(value):
            out.add(value)
    return out


def sniff_ext(b: bytes) -> str:
    if not b:
        return "empty"
    if b.startswith(b"<?xml") or b.startswith(b"<"):
        return "xml"
    if b.startswith(b"RIFF"):
        return "wav"
    if b.startswith(b"#!AMR"):
        return "amr"
    if b.startswith(b"\x89PNG"):
        return "png"
    if b.startswith(b"MThd"):
        return "mid"
    if b.startswith(b"7\x00\x00\x10"):  # 0x10000037 = Symbian MBM UID
        return "mbm"
    sample = b[:512]
    printable = sum(32 <= c < 127 or c in (9, 10, 13) for c in sample)
    if printable / len(sample) > 0.95:
        return "txt"
    return "bin"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pak", type=Path)
    ap.add_argument("--dll", type=Path, help="main.dll (source of path strings)")
    ap.add_argument("--wordlist", type=Path, help="extra candidate paths, one per line")
    ap.add_argument("-o", "--out", type=Path, default=Path("extracted"))
    args = ap.parse_args()

    data = args.pak.read_bytes()
    entries = parse_directory(data)
    print(f"{len(entries)} entries, directory OK")

    unresolved = {e[0] for e in entries}
    names: dict[int, str] = {}

    def absorb(cands):
        new = 0
        for cand in cands:
            c = canonical(cand)
            h = rl_hash(c)
            if h in unresolved:
                names[h] = c
                unresolved.discard(h)
                new += 1
        return new

    if args.dll and args.dll.exists():
        n = absorb(mine_paths(args.dll.read_bytes()))
        print(f"from {args.dll.name}: {n} names")
    if args.wordlist and args.wordlist.exists():
        n = absorb(args.wordlist.read_text().splitlines())
        print(f"from wordlist: {n} names")

    # decompress everything once, keep in memory (25 MB compressed, fits fine)
    payloads = {}
    for (nhash, csize, usize, flags, off) in entries:
        p = data[off:off + csize]
        if flags == 1:
            p = zlib.decompress(p)
            assert len(p) == usize
        payloads[nhash] = p

    # iterate: mine text/xml payloads for more paths until fixpoint
    rounds = 0
    while True:
        rounds += 1
        cands = set()
        for nhash, p in payloads.items():
            ext = sniff_ext(p)
            if ext in ("xml", "txt"):
                cands |= mine_paths(p)
        n = absorb(cands)
        print(f"content pass {rounds}: +{n} names")
        if n == 0:
            break

    args.out.mkdir(parents=True, exist_ok=True)
    with open(args.out / "manifest.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["hash", "name", "sniffed_type", "size", "flags", "offset"])
        for (nhash, csize, usize, flags, off) in entries:
            p = payloads[nhash]
            ext = sniff_ext(p)
            name = names.get(nhash, "")
            rel = Path(name) if name else Path(f"_UNNAMED/{nhash:08X}.{ext}")
            dest = args.out / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(p)
            w.writerow([f"{nhash:08X}", name, ext, usize, flags, off])

    print(f"done: {len(entries)} files, {len(names)} named "
          f"({100 * len(names) // len(entries)}%) -> {args.out}")


if __name__ == "__main__":
    main()
