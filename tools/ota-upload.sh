#!/usr/bin/env bash
# Push new firmware to the car over WiFi -- no USB cable needed.
#
#   ./tools/ota-upload.sh
#   CAR=192.168.1.155 ./tools/ota-upload.sh
#   SKETCH=firmware/tests/03_drive_test ./tools/ota-upload.sh
#
# The very first upload has to go over USB (the sketch must already contain
# the OTA receiver). And if you ever flash a build that cannot join the WiFi,
# only the cable will get you back in.
set -euo pipefail

SKETCH="${SKETCH:-firmware/lego4wd}"
CAR="${CAR:-car.lan}"              # IP or local DNS name of your car
OTA_PASS="${OTA_PASS:-lego4wd}"    # must match SECRET_OTA_PASS
FQBN="arduino:renesas_uno:unor4wifi"

echo "Compiling $SKETCH ..."
arduino-cli compile --fqbn "$FQBN" -e "$SKETCH"

BIN=$(ls "$SKETCH"/build/arduino.renesas_uno.unor4wifi/*.ino.bin | head -1)
echo "Sending $(basename "$BIN") ($(($(wc -c < "$BIN") / 1024)) KB) to $CAR ..."

# -H "Expect:" matters -- the tiny server on the Arduino does not speak
# "100-continue", so curl would otherwise wait forever.
curl --silent --show-error --output /dev/null \
     --write-out 'Upload: HTTP %{http_code} in %{time_total}s\n' \
     --max-time 120 \
     --user "arduino:$OTA_PASS" \
     --header 'Expect:' \
     --data-binary "@$BIN" \
     "http://$CAR:65280/sketch"

echo "Waiting for the reboot ..."
curl --silent --output /dev/null \
     --write-out 'Car back online: HTTP %{http_code}\n' \
     --retry 15 --retry-delay 2 --retry-all-errors --retry-connrefused \
     --max-time 60 \
     "http://$CAR/"
