"""Tests for tools/pefix.py using a synthetic minimal PE image."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import pefix  # noqa: E402


def make_pe(*, machine=0x1A2, subsystem=9, ssver=(2, 11), base=0x10000, entry=0x1000,
            dll=False, sections=(("text", 0x60000020), (".idata", 0x40000040))):
    """Build a tiny PE32 image: DOS stub, PE header, optional header (96 bytes), sections."""
    pe_off = 0x40
    dos = bytearray(b"MZ" + b"\0" * 0x3A + struct.pack("<I", pe_off))
    chars = 0x102 | (0x2000 if dll else 0)
    hdr = b"PE\0\0" + struct.pack("<HHIIIHH", machine, len(sections), 0, 0, 0, 96, chars)
    opt = bytearray(96)
    struct.pack_into("<H", opt, 0, 0x10B)
    struct.pack_into("<I", opt, 16, entry)
    struct.pack_into("<I", opt, 28, base)
    struct.pack_into("<HHHHHH", opt, 40, 2, 11, 0, 0, ssver[0], ssver[1])
    struct.pack_into("<H", opt, 68, subsystem)
    table = bytearray()
    for i, (name, ch) in enumerate(sections):
        table += struct.pack("<8sIIIIIIHHI", name.encode(), 0x100, 0x1000 * (i + 1), 0x200,
                             0x400 * (i + 1), 0, 0, 0, 0, ch)
    return bytes(dos + hdr + opt + table)


@pytest.mark.unit
def test_parse_reads_header_fields():
    s = pefix.parse(make_pe())
    assert s.machine == 0x1A2 and s.subsystem == 9 and s.subsystem_version == (2, 11)
    assert s.image_base == 0x10000 and s.entry_rva == 0x1000 and not s.is_dll
    assert [sec.name for sec in s.sections] == ["text", ".idata"]


@pytest.mark.unit
def test_fix_sets_write_on_idata_only():
    fixed = pefix.fix(make_pe())
    s = pefix.parse(fixed)
    by_name = {sec.name: sec.characteristics for sec in s.sections}
    assert by_name[".idata"] & pefix.IMAGE_SCN_MEM_WRITE
    assert not by_name["text"] & pefix.IMAGE_SCN_MEM_WRITE
    assert pefix.fix(fixed) == fixed  # idempotent


@pytest.mark.unit
def test_check_reports_problems():
    assert pefix.check(pefix.parse(pefix.fix(make_pe()))) == []
    bad = pefix.parse(make_pe(machine=0x1C0, subsystem=2, ssver=(3, 0), base=0x400000, entry=0))
    problems = " ".join(pefix.check(bad))
    for needle in ("not SH3", "not Windows CE", "not 2.11", "image base", "entry point", "not writable"):
        assert needle in problems
    dll = pefix.parse(pefix.fix(make_pe(dll=True, base=0x10000000)))
    assert pefix.check(dll) == ["DLL has no .reloc section"]


@pytest.mark.unit
@pytest.mark.parametrize("data", [b"", b"MZ" + b"\0" * 100, make_pe()[:0x50]])
def test_bad_images_raise(data):
    with pytest.raises((pefix.PeError, struct.error)):
        pefix.parse(data)


@pytest.mark.unit
def test_cli(tmp_path, capsys):
    img = tmp_path / "a.exe"
    img.write_bytes(make_pe())
    assert pefix.main(["pefix.py", str(img)]) == 0
    assert "exe sh3 wince 2.11" in capsys.readouterr().out
    assert pefix.parse(img.read_bytes()).sections[1].characteristics & pefix.IMAGE_SCN_MEM_WRITE
    img.write_bytes(make_pe(machine=0x1C0))
    assert pefix.main(["pefix.py", str(img)]) == 1
    assert pefix.main(["pefix.py"]) == 2
    assert pefix.main(["pefix.py", str(tmp_path / "missing")]) == 1
