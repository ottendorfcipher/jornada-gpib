"""Unit tests for tools/asmfilter.py (ELF -> COFF assembly dialect rewrite)."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import asmfilter as af  # noqa: E402


@pytest.mark.unit
@pytest.mark.parametrize(
    "line, expected",
    [
        ('\t.section\t.rodata.str1.4,"aMS",@progbits,1', '\t.section .rdata,"dr"'),
        ("\t.section\t.rodata", '\t.section .rdata,"dr"'),
        ('\t.section\t.text.startup,"ax",@progbits', "\t.text"),
        ('\t.section\t.data.rel.local,"aw"', "\t.data"),
        ('\t.section\t.bss,"aw",@nobits', "\t.data"),
        ("\t.bss", "\t.data"),
        ("\t.text", "\t.text"),
        ("\t.align 2", "\t.align 2"),
        ("\t.globl\t_main", "\t.globl\t_main"),
        ("_main:", "_main:"),
        (".L3:", ".L3:"),
        ("\tmov.l\t@(4,r15),r0", "\tmov.l\t@(4,r15),r0"),
        ("\t.long\t_foo", "\t.long\t_foo"),
        ('\t.string\t"hi"', '\t.string\t"hi"'),
        ("\t.comm\t_buf,64,4", "\t.comm\t_buf,64"),
        ("\t.comm\t_buf,64", "\t.comm\t_buf,64"),
        ('\t.file\t"hello.c"', '\t.file\t"hello.c"'),
        ("! comment", "! comment"),
        ("", ""),
    ],
)
def test_lines_rewritten_or_kept(line, expected):
    assert af.filter_line(line, 1) == expected


@pytest.mark.unit
@pytest.mark.parametrize(
    "line",
    ["\t.type\t_main, @function", "\t.size\t_main, .-_main", '\t.ident\t"GCC: 15"',
     "\t.local\t_x", "\t.cfi_startproc", '\t.file 1 "hello.c"', "\t.loc 1 5 3"],
)
def test_lines_dropped(line):
    assert af.filter_line(line, 1) is None


@pytest.mark.unit
@pytest.mark.parametrize("line", ['\t.section\t.init_array,"aw"', "\t.uleb128 5", "\t.weird 1"])
def test_unknown_directives_fail_loudly(line):
    with pytest.raises(af.FilterError):
        af.filter_line(line, 7)


@pytest.mark.unit
def test_filter_lines_and_cli(tmp_path):
    src = tmp_path / "in.s"
    dst = tmp_path / "out.s"
    src.write_text('\t.file\t"x.c"\n\t.text\n\t.type\t_f, @function\n_f:\n\trts\n\tnop\n'
                   '\t.size\t_f, .-_f\n\t.section\t.rodata\n.LC0:\n\t.string\t"a"\n')
    assert af.main(["asmfilter.py", str(src), str(dst)]) == 0
    out = dst.read_text().splitlines()
    assert out == ['\t.file\t"x.c"', "\t.text", "_f:", "\trts", "\tnop",
                   '\t.section .rdata,"dr"', ".LC0:", '\t.string\t"a"']
    src.write_text("\t.section .tbss\n")
    assert af.main(["asmfilter.py", str(src), str(dst)]) == 1
    assert af.main(["asmfilter.py"]) == 2
    assert af.main(["asmfilter.py", str(tmp_path / "nope.s"), str(dst)]) == 1
