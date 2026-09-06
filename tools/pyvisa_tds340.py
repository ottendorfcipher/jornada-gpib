#!/usr/bin/env python3
"""Drive a Tektronix TDS 300-series scope through the Jornada gateway with plain PyVISA.

Usage: pyvisa_tds340.py [--host 192.168.131.201] [--address 1] [--measure FREQUENCY] [--curve]
"""
from __future__ import annotations

import argparse
import sys
import time

import pyvisa


def open_gateway(host: str, address: int, timeout_ms: int):
    rm = pyvisa.ResourceManager("@py")
    gw = rm.open_resource(f"TCPIP::{host}::1234::SOCKET")
    gw.read_termination = "\n"
    gw.write_termination = "\n"
    gw.timeout = timeout_ms
    gw.write(f"++addr {address}")
    gw.write("++auto 1")
    gw.write(f"++read_tmo_ms {min(timeout_ms, 30000)}")
    return gw


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="192.168.131.201")
    ap.add_argument("--address", type=int, default=1)
    ap.add_argument("--measure", default="FREQUENCY", help="MEASUREMENT:IMMED:TYPE, e.g. FREQUENCY, PK2PK, MEAN, PERIOD")
    ap.add_argument("--curve", action="store_true", help="download CH1 as ASCII and print a summary")
    args = ap.parse_args(argv)

    gw = open_gateway(args.host, args.address, timeout_ms=10000)
    print("gateway:", gw.query("++ver"))
    gw.write("HEADER OFF")       # answers without the command echo (":CURV 1,2,..." becomes "1,2,...")
    print("instrument:", gw.query("*IDN?"))
    print("event status:", gw.query("*ESR?"))
    print("CH1 scale:", gw.query("CH1:SCALE?"), "V/div; timebase:", gw.query("HORIZONTAL:MAIN:SCALE?"), "s/div")
    gw.write(f"MEASUREMENT:IMMED:TYPE {args.measure}")
    gw.write("MEASUREMENT:IMMED:SOURCE CH1")
    print(f"{args.measure}:", gw.query("MEASUREMENT:IMMED:VALUE?"), gw.query("MEASUREMENT:IMMED:UNITS?"))
    if args.curve:
        gw.write("DATA:SOURCE CH1")
        gw.write("DATA:ENCDG ASCII")
        gw.write("DATA:START 1")
        gw.write("DATA:STOP 500")
        t0 = time.time()
        raw = gw.query("CURVE?")
        body = raw.split(" ", 1)[1] if raw.startswith(":") else raw
        points = [int(v) for v in body.split(",") if v.strip()]
        dt = time.time() - t0
        print(f"CURVE?: {len(points)} points in {dt:.1f} s, min {min(points)} max {max(points)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
