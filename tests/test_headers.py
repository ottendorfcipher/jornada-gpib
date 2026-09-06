"""Consistency checks between include/ce/ce_api.h, tools/coredll_imports.txt and mkimplib.py.

Every coredll function declared in the header must have a thunk entry with the right
argument count, and vice versa; otherwise the link fails in a confusing way (or worse, a
thunk with the wrong count silently corrupts the stack).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gen_thunks as gt  # noqa: E402
import mkimplib  # noqa: E402

DECL_RE = re.compile(r"^\s*[\w\s\*]+?\b(\w+)\s*\((.*?)\)\s*CE_IMPORT\((\w+)\);", re.M)


def _header_decls() -> dict[str, int]:
    text = (ROOT / "include/ce/ce_api.h").read_text()
    decls = {}
    for m in DECL_RE.finditer(text):
        name, params, label = m.group(1), m.group(2).strip(), m.group(3)
        assert name == label, f"{name} declared with thunk label {label}"
        nargs = 0 if params in ("", "VOID", "void") else len(params.split(","))
        decls[name] = nargs
    return decls


def _table() -> dict[str, int]:
    with open(ROOT / "tools/coredll_imports.txt", encoding="utf-8") as fh:
        return {e.name: e.nargs for e in gt.parse_table(fh)}


@pytest.mark.unit
def test_header_and_import_table_agree():
    decls = _header_decls()
    table = _table()
    assert decls, "no CE_IMPORT declarations found"
    assert set(decls) == set(table), (set(decls) ^ set(table))
    mismatched = {n: (decls[n], table[n]) for n in decls if decls[n] != table[n]}
    assert not mismatched, f"argument count mismatch (header, table): {mismatched}"


@pytest.mark.unit
def test_all_parameters_are_single_words():
    """64-bit or struct-by-value parameters would break the thunks; forbid them."""
    text = (ROOT / "include/ce/ce_api.h").read_text()
    for m in DECL_RE.finditer(text):
        for p in m.group(2).split(","):
            assert not re.search(r"\b(LONGLONG|INT64|UINT64|double|float|LARGE_INTEGER\b(?!\s*\*)|SYSTEMTIME\b(?!\s*\*))", p), \
                f"{m.group(1)}: parameter {p.strip()!r} is not a 32-bit word"


@pytest.mark.unit
def test_mkimplib_renders_import_table(tmp_path, capsys):
    text = mkimplib.render("coredll.dll", ["Sleep", "CreateThread"])
    lines = [l.strip() for l in text.splitlines()]
    assert '.section .idata$2,"w"' in lines and '.section .idata$7,"w"' in lines
    assert lines.count(".rva\t.Lhn_Sleep") == 2          # lookup table and address table
    assert "__imp__Sleep:" in lines and ".globl\t__imp__CreateThread" in lines
    assert '.asciz\t"coredll.dll"' in lines
    assert lines.index(".Lilt_coredll_dll:") < lines.index(".Liat_coredll_dll:")
    # every hint/name entry is 2-byte aligned and preceded by a WORD hint
    i = lines.index(".Lhn_CreateThread:")
    assert lines[i - 1] == ".align\t1" and lines[i + 1] == ".short\t0"
    with pytest.raises(ValueError):
        mkimplib.render("bad name.dll", ["X"])
    with pytest.raises(ValueError):
        mkimplib.render("x.dll", [])
    table = tmp_path / "t.txt"
    table.write_text("Sleep 1\n")
    assert mkimplib.main(["mkimplib.py", "coredll.dll", str(table)]) == 0
    assert "__imp__Sleep" in capsys.readouterr().out
    assert mkimplib.main(["mkimplib.py"]) == 2
    assert mkimplib.main(["mkimplib.py", "x.dll", str(tmp_path / "missing")]) == 1
