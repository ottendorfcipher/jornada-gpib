# PyVISA through the Jornada

Python cannot run on the Jornada, but the Jornada can be the GPIB adapter for Python on the
Mac. `gpibsrv.exe` on the device is a Prologix GPIB-ETHERNET compatible gateway on TCP port
1234 over the PPP link, so unmodified PyVISA with the pure-Python backend drives instruments
through it. Nothing NI, nothing proprietary, one serial cable.

## Setup

1. PPP link up and PC Link connected (see jornada-link), card inserted, `gpib.dll` loaded.
2. Start the gateway on the device:
   ```bash
   ~/Desktop/jornada-link/bin/jornada run '\gpibsrv.exe'
   ```
   A small "GPIB gateway" window appears on the Jornada; Quit stops it (or send `++quit`).
3. On the Mac: `pip install pyvisa pyvisa-py`.

## Use

```python
import pyvisa

rm = pyvisa.ResourceManager("@py")
gw = rm.open_resource("TCPIP::192.168.131.201::1234::SOCKET")
gw.read_termination = "\n"
gw.write_termination = "\n"
gw.timeout = 8000            # ms; the link is a 115200 baud serial line

gw.write("++addr 1")         # the scope's GPIB address
gw.write("++auto 1")         # read back automatically after a query
print(gw.query("*IDN?"))     # TEKTRONIX,TDS 340,0,CF:91.1CT FV:v1.02
```

`tools/pyvisa_tds340.py` is a worked example that identifies the scope, reads the
measurement of choice, and downloads a waveform.

## Command reference (gateway)

`++addr [pad [sad]]`, `++auto [0|1]`, `++eoi [0|1]`, `++eos [0..3]` (terminator appended to
commands: CR+LF, CR, LF, none; default LF), `++read [eoi|byte]`, `++read_tmo_ms [ms]`,
`++ifc`, `++clr`, `++trg`, `++loc`, `++llo`, `++spoll [pad]`, `++srq`, `++ver`, `++mode`,
`++rst`, `++help`, `++lines` (bus line status byte), `++quit` (stops the gateway).
Instrument data may contain CR, LF, ESC or `+` only if escaped with ESC (0x1B), as on a
Prologix adapter.

## Throughput

Version 1 of the driver polls an 8-bit FIFO: about 15 KB/s on the bus, and the serial link
itself carries about 10 KB/s. A 2500-point ASCII waveform takes a few seconds; keep queries
short and let the scope do the heavy lifting.

## From the shell

jornada-link has the same client built in:

```bash
jornada gpib idn                      # *IDN? at address 1
jornada gpib -a 1 query 'CH1:SCALE?'
jornada gpib write 'ACQUIRE:STATE RUN'
jornada gpib spoll
jornada gpib repl                     # interactive: ? lines are queried, others written
```
