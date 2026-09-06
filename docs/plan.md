# Plan and status

Goal: use the NI PCMCIA-GPIB card in the HP Jornada 680e (Windows CE 2.11, SH-3) to control
IEEE 488 instruments. First instrument: Tektronix TDS 340A oscilloscope.

## Phases

| # | Phase | Deliverable | Status |
|---|---|---|---|
| 0 | Research | hardware, driver model, toolchain facts (docs/) | done 2026-09-05 |
| 1 | Toolchain | GNU sh-elf GCC + sh-pe binutils pipeline, ABI thunks, `hello.exe` | done 2026-09-06, validated on the 680e |
| 2 | Card probe | CIS dump, PnP ID, window capabilities, captured by the driver at load (`gpibtest cis`) | built, first card insertion pending |
| 3 | Driver v1 | `gpib.dll` stream driver: Card Services bring-up, TNT4882C init, polled FIFO transfers, registry self-install | built, on-device test pending |
| 4 | Bring-up | register self-test, bus lines, `*IDN?` from the TDS 340A | |
| 5 | API + tools | NI-488.2-style C API over DeviceIoControl, `gpibtest.exe` | |
| 6 | App | `gpibterm.exe`, an H/PC terminal for instruments | |
| 7 | Driver v2 | FIFO transfers with the interrupt callback, 16-bit FIFO if the socket allows | |

## Driver v1 design (phase 3)

- `src/gpib/tnt4882.c`: portable chip driver over an 8-bit register I/O interface. One-chip
  mode, 7210 register page for addressing, auxiliary commands, status and byte transfers.
  Timeouts by polling with a millisecond clock callback. Unit-tested on the host against a
  simulated chip.
- `src/gpib/gpib488.c`: controller-level operations (address a device, write with EOI, read
  until END, serial poll, clear, trigger, local, IFC, REN) built on the chip driver.
- `src/gpib/drv_ce.c`: the Windows CE stream driver `GPB1:`. Init reads `Sckt` from the
  active key, binds Card Services, parses the CIS configuration table, requests and maps the
  32-byte I/O window, requests configuration (5 V, config index from the CIS), initialises
  the chip. IOCTLs expose the controller operations; Read/Write map to the addressed device.
- `src/gpib/install.c`: `Install_Driver` export so typing `gpib` into the H/PC "Unidentified
  PCCard Adapter" dialog creates `HKLM\Drivers\PCMCIA\<PnP ID>` with `Dll=gpib.dll`,
  `Prefix=GPB`.
- No interrupts in v1: the card is polled from the calling thread. v2 adds the ISR callback
  and FIFO transfers.

## Findings on the hardware (2026-09-06)

- The entry point receives (hInstance, NULL, command line, SW_SHOW) as assumed; the calling
  convention thunks work, including seven-argument imports.
- GCC calls `__udivsi3` / `__sdivsi3` with a special register contract on SH-3; they must be
  assembly (src/rt/divide.s). A C implementation crashed the first run.
- The CE 2.11 loader resolves a missing import to null instead of refusing the image, so an
  unavailable API only shows up as a crash at the call. coredll on the 680e lacks
  GetCommandLineW, GetCurrentThread, GetCurrentThreadId, SetEvent and ResetEvent
  (the last two are EventModify). hello.exe verifies the whole import list at run time.
- pcmcia.dll cannot be loaded by an ordinary process (DllMain fails, error 1114): Card
  Services are only usable from a driver loaded by device.exe. The CIS dump therefore lives in
  gpib.dll and is fetched with IOCTL_GPIB_CIS.
- Page size is 1024, OS 2.11 build 0, processor type 10003 (SH-3), architecture 4.

## Open questions to settle on the hardware

1. Does HP's pcmcia.dll grant 16-bit I/O windows (`WIN_ATTR_16BIT`)? v1 uses 8-bit only.
2. The exact PnP ID string CE derives for the card (expected `National_Instruments-PCMCIA-GPIB-8B85`).
3. Page size and window granularity reported by CardMapWindow.
4. Whether the HP Enhanced PCMCIA Driver 2.0 is installed on this unit (affects nothing yet).
