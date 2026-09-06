# jornada-gpib

A from-scratch Windows CE 2.11 driver for the National Instruments PCMCIA-GPIB card on the
HP Jornada 680e (Hitachi SH-3), plus a small NI-488.2-style API and tools to talk to
IEEE 488 instruments from a 1999 handheld.

National Instruments never shipped GPIB software for Windows CE on any processor, and no
other PC Card GPIB vendor did either. This repository is the missing piece: the driver is
written against the public TNT4882 documentation, the Windows CE 2.11 Card Services API, and
the behaviour of the open-source Linux driver for the same chip.

Status (2026-09-06): working. The driver loads on the 680e, and a Tektronix TDS 340 at
address 1 answers `*IDN?`, serial polls and waveform downloads through it. See
[docs/plan.md](docs/plan.md) for what is done and what is next.

## How it is built

There is no Microsoft compiler in this project. Code is compiled by a modern `sh-elf` GCC to
SH-3 assembly, rewritten into COFF dialect, assembled and linked by `sh-pe` binutils (which
still carry the Windows CE SuperH PE target) into PE executables and DLLs that Windows CE
2.11 loads. The one hard part, the difference between GCC's and Microsoft's SH-3 calling
conventions, is bridged with generated thunks. [docs/toolchain.md](docs/toolchain.md)
explains the details.

```bash
tools/build-toolchain.sh      # once: binutils (sh-pe, sh-elf) + GCC into ~/.cache/jornada-gpib
make hello                    # build/hello.exe, the toolchain validation program
make test                     # host-side tests (pytest + C)
```

Deployment to the Jornada uses [jornada-link](https://github.com/ottendorfcipher/jornada-link),
the serial PPP + RAPI toolkit for the same device:

```bash
~/Desktop/jornada-link/bin/jornada put build/hello.exe '\hello.exe'
~/Desktop/jornada-link/bin/jornada run '\hello.exe'
~/Desktop/jornada-link/bin/jornada get '\hello.txt'
```

## Layout

```
include/ce/    Windows CE 2.11 API declarations used here (our own, from the public docs)
include/rt/    freestanding runtime interface
src/rt/        memcpy and friends, a printf-style formatter, EXE/DLL entry points
src/ce/        Card Services run-time binding
src/hello/     toolchain validation program
src/gpib/      the driver
src/gpibapi/   NI-488.2 style C API over the driver
src/gpibtest/  command-line bring-up tool
src/gpibterm/  instrument terminal for the H/PC screen
src/gpibsrv/   Prologix-compatible GPIB-to-TCP gateway (PyVISA on the Mac, see docs/pyvisa.md)
src/nettest/   staged probe for threads and Winsock
tools/         toolchain build script, thunk and import-table generators, assembly filter
tests/         pytest suites for the tools, C tests for the runtime
docs/          plan, toolchain notes, hardware and driver-model notes
```

## Hardware facts in one place

| Item | Value |
|---|---|
| Card | NI PCMCIA-GPIB, TNT4882C ASIC, 5 V 16-bit Type II PC Card |
| PCMCIA ids | manufacturer 0x010b, card 0x4882 |
| CIS strings | "National Instruments", "PCMCIA-GPIB" |
| Resources | one 32-byte I/O window, one level-triggered IRQ, config index 1 |
| Host | HP Jornada 680e, SH7709 at 133 MHz, Windows CE 2.11 (H/PC Pro) |

## References

- NI, TNT4882 Programmer Reference Manual (370872A-01)
- NI, Getting Started with Your PCI-GPIB or PCMCIA-GPIB (321289A), appendix A
- linux-gpib, drivers tnt4882 and nec7210 (GPL, used as a behavioural reference only)
- Microsoft, Windows CE 2.11 Device Driver Kit: PC Card drivers and Card Services
- Microsoft, SH-3 calling sequence specification for Windows CE

## License

MIT. No Microsoft or National Instruments code is included.
