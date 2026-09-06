#!/usr/bin/env python3
"""Tektronix TDS 340 family driver over PyVISA, through the Jornada GPIB gateway.

The command set follows the TDS 340A / 360 / 380 programmer manual (070-9442) and is
grouped the way the front panel is: vertical, horizontal, trigger, acquire, measure,
cursor, display, save/recall, utility. Every setter writes one command; ``state()``
reads the whole panel back in one exchange with response headers on.

    from tds340 import TDS340
    scope = TDS340.open_gateway("192.168.131.201", address=1)
    print(scope.identify())
    scope.vertical.set_scale(1, 0.5)          # CH1 500 mV/div
    scope.trigger.set_level(1.4)              # volts
    wave = scope.waveform(channel=1, points=1000)
    scope.screenshot("screen.bmp")

Any PyVISA message-based resource works; ``PrologixGateway`` adapts the gateway's
Prologix dialect (``++read eoi`` before every read, escaping of instrument data).

Usage as a tool:
    tds340.py [--host H] [--address N] idn | state | set CMD | query TEXT
              | waveform out.csv | screenshot out.bmp | measure FREQUENCY
"""
from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence

import pyvisa

INVALID_MEASUREMENT = 9.9e37
VOLTS_PER_DIVISION = [0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10]
SECONDS_PER_DIVISION = [d * f for d in (1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1, 1)
                        for f in (5, 10, 25) if 5e-9 <= d * f <= 5]


# ---------------------------------------------------------------- response parsing

def split_outside_quotes(text: str, separator: str) -> List[str]:
    fields, current, quoted = [], [], False
    for ch in text:
        if ch == '"':
            quoted = not quoted
        if ch == separator and not quoted:
            fields.append("".join(current))
            current = []
        else:
            current.append(ch)
    fields.append("".join(current))
    return fields


def parse_headered(response: str) -> Dict[str, str]:
    """":CH1:SCALE 2.0E0;POSITION 0.0E0;:TRIGGER:MAIN:MODE AUTO" -> {"CH1:SCALE": "2.0E0", ...}

    Relative headers resolve against the previous compound header's path (IEEE 488.2).
    """
    result: Dict[str, str] = {}
    prefix: List[str] = []
    for raw in split_outside_quotes(response.strip(), ";"):
        item = raw.strip()
        if not item:
            continue
        header, _, value = item.partition(" ")
        mnemonics = [m.upper() for m in header.split(":") if m]
        if not mnemonics:
            continue
        path = mnemonics if header.startswith(":") else prefix + mnemonics
        prefix = path[:-1]
        result[":".join(path)] = value.strip()
    return result


def unquote(text: str) -> str:
    text = text.strip()
    return text[1:-1] if len(text) >= 2 and text[0] == text[-1] == '"' else text


def decode_block(data: bytes) -> bytes:
    """Payload of an IEEE 488.2 definite-length block (#<n><len><bytes>), header text skipped."""
    start = data.find(b"#")
    if start < 0 or start + 1 >= len(data):
        raise ValueError("no #-block header")
    digits = data[start + 1] - 48
    if digits == 0:
        return data[start + 2:].rstrip(b"\n")
    length = int(data[start + 2:start + 2 + digits])
    payload = data[start + 2 + digits:start + 2 + digits + length]
    if len(payload) != length:
        raise ValueError(f"block truncated: {len(payload)} of {length} bytes")
    return payload


def nearest_step(value: float, ladder: Sequence[float], steps: int) -> float:
    index = min(range(len(ladder)), key=lambda i: abs(ladder[i] - value) / ladder[i])
    return ladder[max(0, min(len(ladder) - 1, index + steps))]


# ---------------------------------------------------------------- transport

class PrologixGateway:
    """A PyVISA socket resource to the jornada-gpib gateway, driven Prologix-style."""

    def __init__(self, resource, address: int, timeout_s: float = 6.0):
        self.resource = resource
        self.address = address
        resource.read_termination = "\n"
        resource.write_termination = "\n"
        resource.timeout = int(timeout_s * 1000)
        resource.write("++auto 0")
        resource.write("++eoi 1")
        resource.write("++eos 3")
        resource.write(f"++read_tmo_ms {int(timeout_s * 1000)}")
        resource.write(f"++addr {address}")

    @staticmethod
    def escape(text: str) -> str:
        return re.sub(r"([\r\n\x1b+])", "\x1b\\1", text)

    def write(self, text: str) -> None:
        self.resource.write(self.escape(text) + "\n", termination="")

    def read(self) -> str:
        self.resource.write("++read eoi")
        return self.resource.read()

    def read_raw(self, idle_s: float = 1.0) -> bytes:
        """One message of unknown length: wait for the first bytes, then until idle."""
        self.resource.write("++read eoi")
        chunks = [self.resource.read_bytes(1)]
        original = self.resource.timeout
        self.resource.timeout = int(idle_s * 1000)
        try:
            while True:
                try:
                    chunk = self.resource.read_bytes(4096, break_on_termchar=False)
                except pyvisa.errors.VisaIOError:
                    break
                if not chunk:
                    break
                chunks.append(chunk)
        finally:
            self.resource.timeout = original
        return b"".join(chunks)

    def query(self, text: str) -> str:
        self.write(text)
        return self.read()

    def gateway_command(self, text: str) -> str:
        return self.resource.query(text)


# ---------------------------------------------------------------- data types

@dataclass(frozen=True)
class Reading:
    value: Optional[float]
    units: str

    @classmethod
    def parse(cls, value: str, units: str) -> "Reading":
        number = float(value)
        return cls(None if abs(number) >= INVALID_MEASUREMENT else number, unquote(units))


@dataclass(frozen=True)
class Preamble:
    x_increment: float
    x_zero: float
    y_multiplier: float
    y_offset: float
    y_zero: float
    point_offset: int = 0
    identifier: str = ""


@dataclass(frozen=True)
class Waveform:
    channel: int
    raw: List[int]
    preamble: Preamble
    times: List[float] = field(default_factory=list)
    volts: List[float] = field(default_factory=list)

    @classmethod
    def scaled(cls, channel: int, raw: List[int], preamble: Preamble) -> "Waveform":
        times = [preamble.x_zero + i * preamble.x_increment for i in range(len(raw))]
        volts = [(level - preamble.y_offset) * preamble.y_multiplier + preamble.y_zero for level in raw]
        return cls(channel, raw, preamble, times, volts)

    def csv(self) -> str:
        return "time_s,volts\n" + "".join(f"{t},{v}\n" for t, v in zip(self.times, self.volts))


# ---------------------------------------------------------------- front-panel groups

class Vertical:
    def __init__(self, scope: "TDS340"):
        self._scope = scope

    def set_scale(self, channel: int, volts_per_division: float) -> None:
        self._scope.write(f"CH{channel}:SCALE {volts_per_division:.4E}")

    def step_scale(self, channel: int, up: bool) -> float:
        current = float(self._scope.query(f"CH{channel}:SCALE?"))
        target = nearest_step(current, VOLTS_PER_DIVISION, 1 if up else -1)
        self.set_scale(channel, target)
        return target

    def set_position(self, channel: int, divisions: float) -> None:
        self._scope.write(f"CH{channel}:POSITION {max(-5.0, min(5.0, divisions)):.3f}")

    def set_coupling(self, channel: int, coupling: str) -> None:           # AC | DC | GND
        self._scope.write(f"CH{channel}:COUPLING {coupling}")

    def set_bandwidth(self, channel: int, bandwidth: str) -> None:         # TWENTY | FULL
        self._scope.write(f"CH{channel}:BANDWIDTH {bandwidth}")

    def set_inverted(self, channel: int, inverted: bool) -> None:
        self._scope.write(f"CH{channel}:INVERT {'ON' if inverted else 'OFF'}")

    def set_displayed(self, source: str, displayed: bool) -> None:         # CH1 CH2 MATH1 REF1 REF2
        self._scope.write(f"SELECT:{source} {'ON' if displayed else 'OFF'}")

    def select_control(self, source: str) -> None:
        self._scope.write(f"SELECT:CONTROL {source}")


class Horizontal:
    def __init__(self, scope: "TDS340"):
        self._scope = scope

    def set_scale(self, seconds_per_division: float) -> None:
        self._scope.write(f"HORIZONTAL:MAIN:SCALE {seconds_per_division:.4E}")

    def step_scale(self, up: bool) -> float:
        current = float(self._scope.query("HORIZONTAL:MAIN:SCALE?"))
        target = nearest_step(current, SECONDS_PER_DIVISION, 1 if up else -1)
        self.set_scale(target)
        return target

    def set_position(self, percent: float) -> None:
        self._scope.write(f"HORIZONTAL:POSITION {max(0.0, min(99.9, percent)):.1f}")

    def set_trigger_position(self, percent: int) -> None:
        self._scope.write(f"HORIZONTAL:TRIGGER:POSITION {max(0, min(100, percent))}")


class Trigger:
    def __init__(self, scope: "TDS340"):
        self._scope = scope

    def set_source(self, source: str) -> None:       # CH1 CH2 EXT EXT10 LINE
        self._scope.write(f"TRIGGER:MAIN:EDGE:SOURCE {source}")

    def set_slope(self, slope: str) -> None:         # RISE | FALL
        self._scope.write(f"TRIGGER:MAIN:EDGE:SLOPE {slope}")

    def set_coupling(self, coupling: str) -> None:   # AC DC HFREJ LFREJ NOISEREJ
        self._scope.write(f"TRIGGER:MAIN:EDGE:COUPLING {coupling}")

    def set_level(self, volts: float) -> None:
        self._scope.write(f"TRIGGER:MAIN:LEVEL {volts:.4E}")

    def set_mode(self, mode: str) -> None:           # AUTO | NORMAL
        self._scope.write(f"TRIGGER:MAIN:MODE {mode}")

    def set_type(self, kind: str) -> None:           # EDGE | VIDEO
        self._scope.write(f"TRIGGER:MAIN:TYPE {kind}")

    def set_holdoff(self, seconds: float) -> None:
        self._scope.write(f"TRIGGER:MAIN:HOLDOFF:VALUE {seconds:.4E}")

    def set_level_to_midpoint(self) -> None:         # the SET LEVEL TO 50% button
        self._scope.write("TRIGGER:MAIN SETLEVEL")

    def force(self) -> None:                         # the FORCE TRIGGER button
        self._scope.write("TRIGGER FORCE")

    def status(self) -> str:                         # ARMED AUTO READY SAVE TRIGGER
        return self._scope.query("TRIGGER:STATE?")


class Acquire:
    def __init__(self, scope: "TDS340"):
        self._scope = scope

    def set_mode(self, mode: str) -> None:           # SAMPLE PEAKDETECT AVERAGE ENVELOPE
        self._scope.write(f"ACQUIRE:MODE {mode}")

    def set_averages(self, count: int) -> None:
        self._scope.write(f"ACQUIRE:NUMAVG {max(2, min(256, count))}")

    def set_stop_after(self, mode: str) -> None:     # RUNSTOP | SEQUENCE
        self._scope.write(f"ACQUIRE:STOPAFTER {mode}")

    def run(self) -> None:
        self._scope.write("ACQUIRE:STATE RUN")

    def stop(self) -> None:
        self._scope.write("ACQUIRE:STATE STOP")

    def single(self) -> None:
        self._scope.write("ACQUIRE:STOPAFTER SEQUENCE;:ACQUIRE:STATE RUN")

    def count(self) -> int:
        return int(self._scope.query("ACQUIRE:NUMACQ?"))


class Measure:
    TYPES = ("AMPLITUDE BURST CMEAN CRMS FALL FREQUENCY HIGH LOW MAXIMUM MEAN MINIMUM NDUTY "
             "NOVERSHOOT NWIDTH PDUTY PERIOD PK2PK POVERSHOOT PWIDTH RISE RMS").split()

    def __init__(self, scope: "TDS340"):
        self._scope = scope

    def immediate(self, kind: str, channel: int = 1) -> Reading:
        self._scope.write(f"MEASUREMENT:IMMED:TYPE {kind};:MEASUREMENT:IMMED:SOURCE CH{channel}")
        return Reading.parse(self._scope.query("MEASUREMENT:IMMED:VALUE?"),
                             self._scope.query("MEASUREMENT:IMMED:UNITS?"))

    def configure_slot(self, slot: int, kind: str, source: str, enabled: bool = True) -> None:
        slot = max(1, min(4, slot))
        self._scope.write(f"MEASUREMENT:MEAS{slot}:TYPE {kind};:MEASUREMENT:MEAS{slot}:SOURCE {source};"
                          f":MEASUREMENT:MEAS{slot}:STATE {'ON' if enabled else 'OFF'}")

    def slot_value(self, slot: int) -> Reading:
        slot = max(1, min(4, slot))
        return Reading.parse(self._scope.query(f"MEASUREMENT:MEAS{slot}:VALUE?"),
                             self._scope.query(f"MEASUREMENT:MEAS{slot}:UNITS?"))

    def set_gating(self, enabled: bool) -> None:
        self._scope.write(f"MEASUREMENT:GATING {'ON' if enabled else 'OFF'}")


class Cursor:
    def __init__(self, scope: "TDS340"):
        self._scope = scope

    def set_function(self, function: str) -> None:   # OFF HBARS VBARS PAIRED
        self._scope.write(f"CURSOR:FUNCTION {function}")

    def set_horizontal_bar(self, cursor: int, volts: float) -> None:
        self._scope.write(f"CURSOR:HBARS:POSITION{2 if cursor == 2 else 1} {volts:.4E}")

    def set_vertical_bar(self, cursor: int, value: float) -> None:
        self._scope.write(f"CURSOR:VBARS:POSITION{2 if cursor == 2 else 1} {value:.4E}")

    def set_vertical_units(self, units: str) -> None:  # SECONDS | HERTZ
        self._scope.write(f"CURSOR:VBARS:UNITS {units}")

    def horizontal_delta(self) -> float:
        return float(self._scope.query("CURSOR:HBARS:DELTA?"))

    def vertical_delta(self) -> float:
        return float(self._scope.query("CURSOR:VBARS:DELTA?"))


class Display:
    def __init__(self, scope: "TDS340"):
        self._scope = scope

    def set_style(self, style: str) -> None:         # VECTORS DOTS ACCUMVECTORS ACCUMDOTS
        self._scope.write(f"DISPLAY:STYLE {style}")

    def set_persistence(self, seconds: float) -> None:   # 0 = infinite
        self._scope.write(f"DISPLAY:PERSISTENCE {seconds:.4E}")

    def set_graticule(self, graticule: str) -> None:  # FULL GRID CROSSHAIR FRAME
        self._scope.write(f"DISPLAY:GRATICULE {graticule}")

    def set_format(self, fmt: str) -> None:          # YT | XY
        self._scope.write(f"DISPLAY:FORMAT {fmt}")

    def set_intensity(self, overall: Optional[int] = None, contrast: Optional[int] = None,
                      text: Optional[int] = None, waveform: Optional[str] = None) -> None:
        if overall is not None:
            self._scope.write(f"DISPLAY:INTENSITY:OVERALL {max(20, min(100, overall))}")
        if contrast is not None:
            self._scope.write(f"DISPLAY:INTENSITY:CONTRAST {max(100, min(250, contrast))}")
        if text is not None:
            self._scope.write(f"DISPLAY:INTENSITY:TEXT {max(20, min(100, text))}")
        if waveform is not None:
            self._scope.write(f"DISPLAY:INTENSITY:WAVEFORM {waveform}")


class SaveRecall:
    def __init__(self, scope: "TDS340"):
        self._scope = scope

    def save_setup(self, slot: int) -> None:
        self._scope.write(f"SAVE:SETUP {max(1, min(10, slot))}")

    def recall_setup(self, slot: int) -> None:
        self._scope.write(f"RECALL:SETUP {max(1, min(10, slot))}")

    def recall_factory(self) -> None:
        self._scope.write("RECALL:SETUP FACTORY")

    def save_waveform(self, source: str, reference: str) -> None:
        self._scope.write(f"SAVE:WAVEFORM {source},{reference}")


# ---------------------------------------------------------------- the scope

class TDS340:
    STATE_QUERY = ("CH1?;CH2?;SELECT?;HORIZONTAL?;TRIGGER:MAIN?;TRIGGER:STATE?;ACQUIRE?;"
                   "DISPLAY?;CURSOR?;MEASUREMENT?")

    def __init__(self, transport: PrologixGateway):
        self.transport = transport
        self.vertical = Vertical(self)
        self.horizontal = Horizontal(self)
        self.trigger = Trigger(self)
        self.acquire = Acquire(self)
        self.measure = Measure(self)
        self.cursor = Cursor(self)
        self.display = Display(self)
        self.save_recall = SaveRecall(self)

    @classmethod
    def open_gateway(cls, host: str = "192.168.131.201", address: int = 1, port: int = 1234,
                     timeout_s: float = 6.0) -> "TDS340":
        manager = pyvisa.ResourceManager("@py")
        resource = manager.open_resource(f"TCPIP::{host}::{port}::SOCKET")
        scope = cls(PrologixGateway(resource, address, timeout_s))
        scope.write("HEADER OFF")
        return scope

    # raw access
    def write(self, text: str) -> None:
        self.transport.write(text)

    def query(self, text: str) -> str:
        return self.transport.query(text)

    def query_headered(self, text: str) -> Dict[str, str]:
        self.write("HEADER ON")
        try:
            answer = self.query(text)
        finally:
            self.write("HEADER OFF")
        return parse_headered(answer)

    # identity and status
    def identify(self) -> str:
        return self.query("*IDN?")

    def event_status(self) -> int:
        return int(self.query("*ESR?"))

    def events(self) -> List[str]:
        answer = self.query("ALLEV?")
        return [unquote(item) for item in split_outside_quotes(answer, ",") if item.strip().startswith('"')]

    def reset(self) -> None:
        self.write("*RST")

    def clear_status(self) -> None:
        self.write("*CLS")

    def autoset(self) -> None:
        self.write("AUTOSET EXECUTE")

    def lock_front_panel(self, locked: bool) -> None:
        self.write("LOCK ALL" if locked else "UNLOCK ALL")

    def busy(self) -> bool:
        return int(self.query("BUSY?")) != 0

    # snapshots
    def state(self) -> Dict[str, str]:
        """Every front-panel setting as PATH:LEAF -> value."""
        return self.query_headered(self.STATE_QUERY)

    def preamble(self, channel: int = 1) -> Preamble:
        values = self.query_headered(f"WFMPRE:CH{channel}?")

        def get(leaf: str, default: Optional[float] = None) -> Optional[float]:
            for key, value in values.items():
                if key.endswith(":" + leaf):
                    return float(value)
            return default

        x_increment = get("XINCR")
        y_multiplier = get("YMULT")
        if x_increment is None or y_multiplier is None:
            raise ValueError(f"WFMPRE:CH{channel}? did not include XINCR and YMULT: {values}")
        point_offset = int(get("PT_OFF", 0) or 0)
        identifier = next((unquote(v) for k, v in values.items() if k.endswith(":WFID")), "")
        return Preamble(x_increment, get("XZERO", -point_offset * x_increment) or 0.0, y_multiplier,
                        get("YOFF", 0.0) or 0.0, get("YZERO", 0.0) or 0.0, point_offset, identifier)

    def waveform(self, channel: int = 1, points: int = 1000, binary: bool = True) -> Waveform:
        stop = max(1, min(1000, points))
        encoding = "RIBINARY" if binary else "ASCII"
        self.write(f"DATA:SOURCE CH{channel};:DATA:ENCDG {encoding};:DATA:WIDTH 1;:DATA:START 1;:DATA:STOP {stop}")
        preamble = self.preamble(channel)
        if binary:
            self.write("CURVE?")
            payload = decode_block(self.transport.read_raw())
            raw = [b - 256 if b > 127 else b for b in payload]
        else:
            answer = self.query("CURVE?")
            body = answer.split(" ", 1)[1] if answer.startswith(":") else answer
            raw = [int(v) for v in body.split(",") if v.strip()]
        if not raw:
            raise ValueError("CURVE? returned no points")
        return Waveform.scaled(channel, raw, preamble)

    def screenshot(self, path: Optional[str] = None, fmt: str = "BMP") -> bytes:
        """The scope's own screen image through the hardcopy system (BMP, PCX, TIFF, EPSIMAGE)."""
        self.write(f"HARDCOPY:PORT GPIB;:HARDCOPY:FORMAT {fmt};:HARDCOPY:LAYOUT PORTRAIT")
        self.write("HARDCOPY START")
        image = self.transport.read_raw(idle_s=1.5)
        if path:
            with open(path, "wb") as handle:
                handle.write(image)
        return image


# ---------------------------------------------------------------- command line

def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="192.168.131.201")
    parser.add_argument("--address", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=6.0)
    parser.add_argument("action", choices=["idn", "state", "set", "query", "waveform", "screenshot", "measure", "events"])
    parser.add_argument("text", nargs="*")
    args = parser.parse_args(argv)
    scope = TDS340.open_gateway(args.host, args.address, timeout_s=args.timeout)
    text = " ".join(args.text)
    if args.action == "idn":
        print(scope.identify())
    elif args.action == "state":
        for key, value in sorted(scope.state().items()):
            print(f"{key:36} {value}")
    elif args.action == "set":
        scope.write(text)
        print("event status:", scope.event_status())
    elif args.action == "query":
        print(scope.query(text))
    elif args.action == "events":
        print("\n".join(scope.events()) or "no events")
    elif args.action == "measure":
        reading = scope.measure.immediate(text or "FREQUENCY")
        print(reading.value if reading.value is not None else "no valid reading", reading.units)
    elif args.action == "waveform":
        started = time.time()
        wave = scope.waveform(channel=1, points=1000)
        elapsed = time.time() - started
        print(f"{len(wave.raw)} points in {elapsed:.1f} s, {min(wave.volts):.4g} V to {max(wave.volts):.4g} V, "
              f"{wave.preamble.x_increment:.3g} s/point ({wave.preamble.identifier})")
        if text:
            with open(text, "w") as handle:
                handle.write(wave.csv())
            print("wrote", text)
    elif args.action == "screenshot":
        target = text or "tds340-screen.bmp"
        started = time.time()
        image = scope.screenshot(target)
        print(f"{len(image)} bytes in {time.time() - started:.1f} s -> {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
