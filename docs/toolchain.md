# Building Windows CE 2.11 SH-3 code with GNU tools

## Why not the Microsoft tools

The period tools (eMbedded Visual Tools 3.0 with the H/PC Pro SDK) do produce CE 2.11 SH-3
binaries, but only run on 32-bit Windows XP because of a 16-bit installer stub. That route
is kept as a fallback; this project builds everything on macOS.

## Pipeline

1. `sh-elf-gcc -m3 -ml -S` compiles C to little-endian SH-3 assembly. GCC's mainline has no
   Windows CE target, but its SuperH code generator is exactly what we need. Flags in the
   Makefile keep the output free of ELF-only features (no .bss, no unwind tables, no
   section splitting) and make `wchar_t` 16-bit (`-fshort-wchar`).
2. `tools/asmfilter.py` rewrites the ELF directives (`.section .rodata`, `.type`, `.size`,
   ...) into what the COFF assembler accepts and refuses anything unexpected.
3. `sh-pe-as` assembles to PE/COFF objects (BFD target `pe-shl`, machine 0x1a2).
4. `sh-pe-ld --subsystem wince:2.11` links. EXEs are linked at 0x10000 (Windows CE always
   maps the running process there), DLLs get a `.reloc` section.

GNU binutils still supports the SH PE target (`bfd/pe-sh.c`, `ld` emulation `shpe` with DLL
support, `.rva` in gas). `dlltool` lost its SH machine table, so `tools/mkimplib.py` writes
the import table for `coredll.dll` directly as assembly.

## The calling-convention problem

Both GCC and Microsoft pass the first four argument words in R4..R7, return in R0 and keep
R8..R14 callee-saved. They differ on the stack:

| | Microsoft SH-3 (Windows CE) | GCC SuperH |
|---|---|---|
| fifth argument | SP+16 | SP+0 |
| four-word area at SP | reserved by every non-leaf caller, callee may write it | absent |
| 64-bit return | hidden pointer in R4 | R0/R1 |

So GCC code cannot call Microsoft code directly (wrong slots for the fifth argument onwards,
and the callee may scribble over the caller's frame), and Microsoft code cannot call a GCC
function that takes more than four words. `tools/gen_thunks.py` generates three families of
tiny assembly thunks:

- import thunks `_xt_<Name>` for every coredll import: reserve the 16-byte area, copy stack
  arguments up, call through the import address table.
- generic call thunks `xt_call0..xt_call10` for function pointers obtained with
  GetProcAddressW (Card Services lives in pcmcia.dll and has no import library).
- export thunks for our own entry points with more than four words, i.e. the stream driver's
  `XXX_IOControl` (seven arguments): copy Microsoft's stack words down to GCC positions.

`include/ce/ce_api.h` declares each imported function with an `__asm__` label so ordinary C
calls land on the thunk. Rules that follow from this design: no 64-bit or struct-by-value
parameters across the boundary, no calls into Microsoft varargs functions (we carry our own
formatter), and the two-byte `CARD_SOCKET_HANDLE` is passed as a 32-bit word.

## Runtime

There is no C library. `src/rt/crt.c` provides memcpy, memset, memcmp, memmove and the
integer division helpers GCC calls on SH-3 (`__udivsi3`, `__sdivsi3`, `__umodsi3`,
`__modsi3`); `src/rt/fmt.c` is a small printf. EXEs start at `WinMainCRTStartup`, which the
kernel calls with the WinMain arguments; DLLs start at `DllMainCRTStartup`.

## Validation

`build/hello.exe` exercises seven-argument imports (CreateFileW), five-argument imports
(WriteFile), structure-filling APIs, the division helpers and 16-bit strings, writes a
report to `\hello.txt` on the device and shows a message box.
