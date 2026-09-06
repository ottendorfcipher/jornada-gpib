#!/usr/bin/env python3
"""Rewrite sh-elf GCC assembly output into the dialect sh-pe (COFF) gas accepts.

GCC for `sh-elf` emits ELF-flavoured directives.  The COFF/PE assembler built from the same
binutils tree understands the SH instructions and most generic directives, but not the ELF
section syntax or the ELF symbol-typing directives.  The differences we handle:

  .section .rodata*, "..."          ->  .section .rdata,"dr"
  .section .text*                   ->  .text
  .section .data*                   ->  .data
  .section .bss*  /  .bss           ->  .data   (zero-filled data; we compile with
                                                 -fno-zero-initialized-in-bss anyway)
  .type / .size / .ident / .local   ->  dropped
  .comm sym,size,align              ->  .comm sym,size   (alignment argument not accepted)
  .cfi_* / .loc / .file N "name"    ->  dropped (no debug info is produced on this path)

Anything else that starts with a dot and is not on the allow list makes the filter fail
loudly, so a new GCC version cannot silently emit something the assembler mis-parses.
"""
from __future__ import annotations

import re
import sys
from typing import Iterable, List

# Generic directives that sh-pe gas accepts unchanged.
ALLOWED = {
    ".text", ".data", ".align", ".p2align", ".balign", ".globl", ".global", ".long", ".short",
    ".word", ".byte", ".ascii", ".asciz", ".string", ".zero", ".space", ".skip", ".file",
    ".uses", ".set", ".equ", ".lcomm", ".comm", ".fill", ".2byte", ".4byte", ".hword",
    ".weak", ".extern", ".little", ".big",
}
DROPPED = {".type", ".size", ".ident", ".local", ".section_end", ".loc", ".cfi_sections"}

_SECTION_RE = re.compile(r"^\s*\.section\s+([^,\s]+)")
_COMM_RE = re.compile(r"^(\s*\.comm\s+[^,]+,\s*[^,\s]+)\s*,\s*\d+\s*$")
_FILE_NUM_RE = re.compile(r"^\s*\.file\s+\d+\s+\"")


class FilterError(ValueError):
    pass


def map_section(name: str) -> str:
    if name.startswith(".rodata"):
        return "\t.section .rdata,\"dr\""
    if name.startswith(".text"):
        return "\t.text"
    if name.startswith(".data"):
        return "\t.data"
    if name.startswith(".bss"):
        return "\t.data"
    raise FilterError(f"unsupported section {name!r}")


def filter_line(line: str, lineno: int) -> str | None:
    """Return the rewritten line, or None to drop it."""
    stripped = line.strip()
    if not stripped or stripped.startswith(("!", "#", ";")) or stripped.startswith("//"):
        return line
    if stripped.startswith(".cfi_"):
        return None
    if _FILE_NUM_RE.match(line):
        return None  # DWARF file-number form, meaningless without debug output
    if stripped.startswith(".section"):
        m = _SECTION_RE.match(line)
        if not m:
            raise FilterError(f"line {lineno}: cannot parse {stripped!r}")
        return map_section(m.group(1))
    if stripped == ".bss":
        return "\t.data"
    directive = stripped.split(None, 1)[0]
    if not directive.startswith("."):
        return line  # instruction or label
    if directive.endswith(":"):
        return line  # local label such as .L3:
    if directive in DROPPED:
        return None
    if directive == ".comm":
        m = _COMM_RE.match(line)
        return m.group(1) if m else line
    if directive in ALLOWED:
        return line
    raise FilterError(f"line {lineno}: unsupported directive {directive!r}")


def filter_lines(lines: Iterable[str]) -> List[str]:
    out: List[str] = []
    for lineno, line in enumerate(lines, 1):
        result = filter_line(line.rstrip("\n"), lineno)
        if result is not None:
            out.append(result)
    return out


def main(argv: List[str]) -> int:
    if len(argv) != 3:
        sys.stderr.write("usage: asmfilter.py <in.s from sh-elf-gcc -S> <out.s for sh-pe-as>\n")
        return 2
    try:
        with open(argv[1], encoding="utf-8") as fh:
            lines = filter_lines(fh)
        with open(argv[2], "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
    except (OSError, FilterError) as exc:
        sys.stderr.write(f"asmfilter: {argv[1]}: {exc}\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
