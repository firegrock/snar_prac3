#!/bin/sh
# Sequential boots on ONE selected board, not four boards or an RF SSID scan.
set -eu
if [ "$#" -ne 2 ]; then echo 'Usage: sh scripts/check_images.sh SERIAL NEW_OUTPUT_DIRECTORY' >&2; exit 2; fi
port=$1
output=$2
if [ -e "$output" ]; then echo 'Output exists; refusing overwrite' >&2; exit 2; fi
command -v idf.py >/dev/null
mkdir -p "$output"
for n in 1 2 3 4 1; do
  if [ -e "$output/status-01.log" ] && [ "$n" -eq 1 ]; then suffix=01-restored; else suffix="0$n"; fi
  idf.py -B "build-0$n" -DLAB_DEVICE_ID="$n" -p "$port" -b 460800 flash > "$output/flash-$suffix.log" 2>&1
  .venv/bin/python -m snar_imu status --serial "$port" --device-id "$n" > "$output/status-$suffix.log" 2>&1
done
echo 'PASS five boots (01,02,03,04,01); status logs retained; RF SSID NOT RUN'
