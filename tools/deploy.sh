#!/bin/bash
# Deploy build outputs to the Jornada over jornada-link and run the bring-up steps.
#
#   tools/deploy.sh hello      copy hello.exe, run it, fetch \hello.txt
#   tools/deploy.sh cisdump    copy cisdump.exe, run it, fetch \cisdump.txt
#   tools/deploy.sh driver     copy gpib.dll to \Windows and gpibtest.exe to the root
#   tools/deploy.sh test ARGS  run gpibtest.exe with ARGS and fetch \gpibtest.txt
#   tools/deploy.sh log        fetch \gpib.log (the driver's log)
#
# Requires the PPP link (sudo jornada-link/bin/jornada-ppp + PC Link on the device).
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
LINK="${JORNADA_LINK:-$HOME/Desktop/jornada-link}"
J="$LINK/bin/jornada"
OUT="$HERE/build/device"
mkdir -p "$OUT"

need_link() {
    if ! "$J" status >/dev/null 2>&1; then
        echo "jornada-link is not connected; run: sudo $LINK/bin/jornada-ppp  (then PC Link on the device)" >&2
        exit 1
    fi
}

fetch() {  # fetch <device path> <local name>
    "$J" get "$1" "$OUT/$2" >/dev/null && echo "--- $2 ---" && cat "$OUT/$2"
}

wait_for_process() {  # crude: give the program time to write its report
    sleep "${1:-3}"
}

case "${1:-}" in
    hello)
        need_link
        "$J" put "$HERE/build/hello.exe" '\hello.exe'
        "$J" run '\hello.exe'
        wait_for_process 3
        fetch '\hello.txt' hello.txt
        ;;
    cisdump)
        need_link
        "$J" put "$HERE/build/cisdump.exe" '\cisdump.exe'
        "$J" run '\cisdump.exe'
        wait_for_process 4
        fetch '\cisdump.txt' cisdump.txt
        ;;
    driver)
        need_link
        "$J" put "$HERE/build/gpib.dll" '\Windows\gpib.dll'
        "$J" put "$HERE/build/gpibtest.exe" '\gpibtest.exe'
        echo "driver copied; insert the card and type gpib into the Unidentified PCCard Adapter dialog"
        ;;
    test)
        need_link
        shift
        "$J" run '\gpibtest.exe' "$*"
        wait_for_process "${DEPLOY_WAIT:-4}"
        fetch '\gpibtest.txt' gpibtest.txt
        ;;
    log)
        need_link
        fetch '\gpib.log' gpib.log
        ;;
    *)
        sed -n '2,10p' "$0"
        exit 2
        ;;
esac
