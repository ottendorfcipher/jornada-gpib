#!/usr/bin/env python3
"""Generate SH-3 assembly thunks that bridge the GCC and Windows CE calling conventions.

Both conventions pass the first four 32-bit arguments in R4..R7 and return in R0, but they
disagree about the stack:

* Microsoft's SH-3 convention (the one every Windows CE DLL was compiled with) requires each
  non-leaf caller to reserve a four-word "argument build area" at R15.  Stack arguments (the
  fifth word onwards) therefore start at SP+16, and a callee may freely write to SP+0..SP+15.
* GCC's SuperH convention puts the fifth argument at SP+0 and reserves nothing.

So a GCC-compiled caller cannot call a Microsoft-compiled function directly (a function with
more than four words of arguments would read the wrong slots, and any function may trample
the caller's frame), and Microsoft code cannot call a GCC-compiled function that takes more
than four words.  This script emits two kinds of thunks:

  import thunk  (GCC caller -> Microsoft callee, e.g. coredll.dll functions)
      push PR, open a frame of 16 + 4*k bytes (k = number of stack words), copy the k
      stack words up into the Microsoft positions, call through the import address table
      entry `__imp__Name`, tear the frame down.
  export thunk  (Microsoft caller -> GCC callee, e.g. the stream driver's XXX_IOControl)
      copy the k stack words from SP+16.. down to a fresh area at SP+0.., call the C
      implementation, tear down.

Usage:
    gen_thunks.py imports  table.txt  > imports.s
    gen_thunks.py exports  table.txt  > exports.s

Table format (one entry per line, '#' comments allowed):
    Name  <number of 32-bit argument words>
For exports the C implementation must be named `<Name>_impl`.

Only 32-bit arguments are supported (no 64-bit values, no structs larger than a word), which
is all the project needs; the generator refuses anything else by design.
"""
from __future__ import annotations

import sys
from dataclasses import dataclass
from typing import Iterable, List

MAX_DISP_LONG = 60          # mov.l @(disp,Rn),R0 encodes disp/4 in 4 bits: 0..60
HOME_AREA = 16              # Microsoft's four-word argument build area
MAX_ARGS = 16               # plenty for the Win32 subset we use


@dataclass(frozen=True)
class Entry:
    name: str
    nargs: int

    @property
    def stack_words(self) -> int:
        return max(0, self.nargs - 4)


def parse_table(lines: Iterable[str]) -> List[Entry]:
    """Parse `Name nargs` lines; raises ValueError with a clear message on bad input."""
    entries: List[Entry] = []
    seen = set()
    for lineno, raw in enumerate(lines, 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2:
            raise ValueError(f"line {lineno}: expected 'Name nargs', got {raw.rstrip()!r}")
        name, count = parts
        if not (name[0].isalpha() or name[0] == "_") or not all(c.isalnum() or c == "_" for c in name):
            raise ValueError(f"line {lineno}: bad symbol name {name!r}")
        try:
            nargs = int(count)
        except ValueError:
            raise ValueError(f"line {lineno}: argument count must be an integer") from None
        if not 0 <= nargs <= MAX_ARGS:
            raise ValueError(f"line {lineno}: argument count {nargs} outside 0..{MAX_ARGS}")
        if name in seen:
            raise ValueError(f"line {lineno}: duplicate entry {name!r}")
        seen.add(name)
        entries.append(Entry(name, nargs))
    return entries


def _load_store(src_off: int, dst_off: int) -> List[str]:
    """Copy one stack word from @(src_off,r15) to @(dst_off,r15) via R0, using R1 as a
    scratch base when a displacement does not fit the 4-bit encoding."""
    out: List[str] = []
    if src_off <= MAX_DISP_LONG:
        out.append(f"\tmov.l\t@({src_off},r15),r0")
    else:
        out.append(f"\tmov\tr15,r1")
        out.append(f"\tadd\t#{src_off},r1")
        out.append(f"\tmov.l\t@r1,r0")
    if dst_off <= MAX_DISP_LONG:
        out.append(f"\tmov.l\tr0,@({dst_off},r15)")
    else:
        out.append(f"\tmov\tr15,r1")
        out.append(f"\tadd\t#{dst_off},r1")
        out.append(f"\tmov.l\tr0,@r1")
    return out



def import_thunk(e: Entry) -> str:
    """GCC caller -> Microsoft callee.  Symbol `_xt_<Name>` calls through `__imp__<Name>`."""
    k = e.stack_words
    frame = HOME_AREA + 4 * k
    if frame > 127:
        raise ValueError(f"{e.name}: frame of {frame} bytes exceeds the 8-bit immediate; unsupported")
    lines = [
        f"\t.align\t2",
        f"\t.globl\t_xt_{e.name}",
        f"_xt_{e.name}:",
        f"\tsts.l\tpr,@-r15",
        f"\tadd\t#-{frame},r15",
    ]
    # After the two pushes the caller's stack word i sits at frame + 4 + 4*i; it must land at
    # HOME_AREA + 4*i in the Microsoft layout.
    for i in range(k):
        lines += _load_store(frame + 4 + 4 * i, HOME_AREA + 4 * i)
    lines += [
        f"\tmov.l\t.Limp_{e.name},r0",
        f"\tmov.l\t@r0,r0",
        f"\tjsr\t@r0",
        f"\tnop",
        f"\tadd\t#{frame},r15",
        f"\tlds.l\t@r15+,pr",
        f"\trts",
        f"\tnop",
        f"\t.align\t2",
        f".Limp_{e.name}:",
        f"\t.long\t__imp__{e.name}",
        "",
    ]
    return "\n".join(lines)


def export_thunk(e: Entry) -> str:
    """Microsoft caller -> GCC callee.  Symbol `_<Name>` calls `_<Name>_impl`."""
    k = e.stack_words
    if k == 0:
        # No stack words: the conventions agree, so the export is a plain jump.
        return "\n".join([
            f"\t.align\t2",
            f"\t.globl\t_{e.name}",
            f"_{e.name}:",
            f"\tmov.l\t.Lreal_{e.name},r0",
            f"\tjmp\t@r0",
            f"\tnop",
            f"\t.align\t2",
            f".Lreal_{e.name}:",
            f"\t.long\t_{e.name}_impl",
            "",
        ])
    frame = 4 * k
    if frame > 127:
        raise ValueError(f"{e.name}: frame of {frame} bytes exceeds the 8-bit immediate; unsupported")
    lines = [
        f"\t.align\t2",
        f"\t.globl\t_{e.name}",
        f"_{e.name}:",
        f"\tsts.l\tpr,@-r15",
        f"\tadd\t#-{frame},r15",
    ]
    # Microsoft stack word i is at (frame + 4) + HOME_AREA + 4*i after our pushes; GCC wants
    # it at 4*i.
    for i in range(k):
        lines += _load_store(frame + 4 + HOME_AREA + 4 * i, 4 * i)
    lines += [
        f"\tmov.l\t.Lreal_{e.name},r0",
        f"\tjsr\t@r0",
        f"\tnop",
        f"\tadd\t#{frame},r15",
        f"\tlds.l\t@r15+,pr",
        f"\trts",
        f"\tnop",
        f"\t.align\t2",
        f".Lreal_{e.name}:",
        f"\t.long\t_{e.name}_impl",
        "",
    ]
    return "\n".join(lines)


MAX_CALL_ARGS = 10


def call_thunk(n: int) -> str:
    """Generic indirect call into Microsoft code: `UINT32 xt_callN(fn, a1..aN)`.

    Used for every function pointer obtained with GetProcAddress (Card Services) and for any
    other Microsoft entry point reached through a pointer.  On entry the GCC caller has put
    fn in R4, a1..a3 in R5..R7 and a4.. on its stack; the Microsoft callee wants a1..a4 in
    R4..R7, a5.. at SP+16.. and a scratch area at SP+0..15.
    """
    if not 0 <= n <= MAX_CALL_ARGS:
        raise ValueError(f"xt_call{n}: argument count outside 0..{MAX_CALL_ARGS}")
    ms_stack_words = max(0, n - 4)
    frame = HOME_AREA + 4 * ms_stack_words
    lines = [
        f"\t.align\t2",
        f"\t.globl\t_xt_call{n}",
        f"_xt_call{n}:",
        f"\tsts.l\tpr,@-r15",
        f"\tadd\t#-{frame},r15",
    ]
    # GCC stack word i (i >= 0) holds a(4+i) and now sits at frame + 4 + 4*i.
    for j in range(ms_stack_words):          # a(5+j) comes from GCC word j+1
        lines += _load_store(frame + 4 + 4 * (j + 1), HOME_AREA + 4 * j)
    lines += ["\tmov\tr4,r0", "\tmov\tr5,r4", "\tmov\tr6,r5", "\tmov\tr7,r6"]
    if n >= 4:
        lines.append(f"\tmov.l\t@({frame + 4},r15),r7")  # a4 from GCC word 0
    lines += [
        f"\tjsr\t@r0",
        f"\tnop",
        f"\tadd\t#{frame},r15",
        f"\tlds.l\t@r15+,pr",
        f"\trts",
        f"\tnop",
        "",
    ]
    return "\n".join(lines)


def render(kind: str, entries: List[Entry]) -> str:
    if kind not in ("imports", "exports", "calls"):
        raise ValueError(f"kind must be 'imports', 'exports' or 'calls', not {kind!r}")
    header = [
        f"! Generated by gen_thunks.py ({kind}); do not edit.",
        "! Bridges GCC's SuperH calling convention and Microsoft's Windows CE SH-3 convention.",
        "\t.text",
        "",
    ]
    if kind == "calls":
        body = [call_thunk(n) for n in range(MAX_CALL_ARGS + 1)]
    else:
        gen = import_thunk if kind == "imports" else export_thunk
        body = [gen(e) for e in entries]
    return "\n".join(header) + "\n".join(body)


def main(argv: List[str]) -> int:
    if len(argv) == 2 and argv[1] == "calls":
        sys.stdout.write(render("calls", []))
        return 0
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    kind, path = argv[1], argv[2]
    try:
        with open(path, encoding="utf-8") as fh:
            entries = parse_table(fh)
        sys.stdout.write(render(kind, entries))
    except (OSError, ValueError) as exc:
        sys.stderr.write(f"gen_thunks: {exc}\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
