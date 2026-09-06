#!/usr/bin/env python3
"""Post-link fix-ups and sanity checks for Windows CE 2.11 PE images produced by sh-pe-ld.

Modern binutils marks the `.idata` section read-only because current Windows loaders make
it writable themselves while patching the import address table. The Windows CE 2.11 loader
is not known to do that, so we set IMAGE_SCN_MEM_WRITE on `.idata` ourselves. The tool also
verifies the header fields the CE loader cares about and prints a one-line summary.

Usage: pefix.py <image.exe|image.dll>
"""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from typing import List

IMAGE_FILE_MACHINE_SH3 = 0x1A2
IMAGE_SUBSYSTEM_WINDOWS_CE_GUI = 9
IMAGE_SCN_MEM_WRITE = 0x80000000
WRITABLE_SECTIONS = {".idata", ".data", ".bss"}


class PeError(ValueError):
    pass


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_size: int
    characteristics: int


@dataclass(frozen=True)
class Summary:
    machine: int
    subsystem: int
    os_version: tuple[int, int]
    subsystem_version: tuple[int, int]
    image_base: int
    entry_rva: int
    is_dll: bool
    sections: List[Section]


def _pe_offset(data: bytes) -> int:
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise PeError("not an MZ/PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise PeError("PE signature not found")
    return pe


def parse(data: bytes) -> Summary:
    pe = _pe_offset(data)
    machine, nsec, _, _, _, optsz, chars = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    if optsz < 96 or struct.unpack_from("<H", data, opt)[0] != 0x10B:
        raise PeError("not a PE32 optional header")
    entry_rva = struct.unpack_from("<I", data, opt + 16)[0]
    image_base = struct.unpack_from("<I", data, opt + 28)[0]
    maj_os, min_os, _, _, maj_ss, min_ss = struct.unpack_from("<HHHHHH", data, opt + 40)
    subsystem = struct.unpack_from("<H", data, opt + 68)[0]
    sections = []
    table = opt + optsz
    for i in range(nsec):
        raw = struct.unpack_from("<8sIIIIIIHHI", data, table + 40 * i)
        sections.append(Section(raw[0].rstrip(b"\0").decode("ascii", "replace"),
                                raw[2], raw[1], raw[3], raw[9]))
    return Summary(machine, subsystem, (maj_os, min_os), (maj_ss, min_ss), image_base,
                   entry_rva, bool(chars & 0x2000), sections)


def fix(data: bytes) -> bytes:
    """Return a copy with IMAGE_SCN_MEM_WRITE set on the data sections."""
    pe = _pe_offset(data)
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    table = pe + 24 + optsz
    out = bytearray(data)
    for i in range(nsec):
        off = table + 40 * i
        name = data[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
        if name in WRITABLE_SECTIONS:
            chars = struct.unpack_from("<I", data, off + 36)[0] | IMAGE_SCN_MEM_WRITE
            struct.pack_into("<I", out, off + 36, chars)
    return bytes(out)


def check(summary: Summary) -> List[str]:
    problems = []
    if summary.machine != IMAGE_FILE_MACHINE_SH3:
        problems.append(f"machine {summary.machine:#x} is not SH3 (0x1a2)")
    if summary.subsystem != IMAGE_SUBSYSTEM_WINDOWS_CE_GUI:
        problems.append(f"subsystem {summary.subsystem} is not Windows CE (9)")
    if summary.subsystem_version != (2, 11):
        problems.append(f"subsystem version {summary.subsystem_version} is not 2.11")
    if not summary.is_dll and summary.image_base != 0x10000:
        problems.append(f"EXE image base {summary.image_base:#x} is not 0x10000")
    if summary.entry_rva == 0:
        problems.append("entry point is zero")
    names = {s.name for s in summary.sections}
    for s in summary.sections:
        if s.name in WRITABLE_SECTIONS and not s.characteristics & IMAGE_SCN_MEM_WRITE:
            problems.append(f"section {s.name} is not writable")
    if summary.is_dll and ".reloc" not in names:
        problems.append("DLL has no .reloc section")
    return problems


def describe(summary: Summary) -> str:
    secs = " ".join(f"{s.name}@{s.virtual_address:#x}" for s in summary.sections)
    kind = "dll" if summary.is_dll else "exe"
    return (f"{kind} sh3 wince {summary.subsystem_version[0]}.{summary.subsystem_version[1]} "
            f"base={summary.image_base:#x} entry={summary.entry_rva:#x} sections: {secs}")


def main(argv: List[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write("usage: pefix.py <image>\n")
        return 2
    try:
        with open(argv[1], "rb") as fh:
            data = fh.read()
        fixed = fix(data)
        if fixed != data:
            with open(argv[1], "wb") as fh:
                fh.write(fixed)
        summary = parse(fixed)
        problems = check(summary)
        print(f"pefix: {argv[1]}: {describe(summary)}")
        for p in problems:
            sys.stderr.write(f"pefix: {argv[1]}: {p}\n")
        return 1 if problems else 0
    except (OSError, PeError, struct.error) as exc:
        sys.stderr.write(f"pefix: {argv[1]}: {exc}\n")
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
