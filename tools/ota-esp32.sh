#!/usr/bin/env bash
# Push new firmware to the ESP32 gamepad bridge over WiFi -- no USB cable.
#
#   ./tools/ota-esp32.sh
#   ESP=192.168.1.157 ./tools/ota-esp32.sh
#   USB=/dev/ttyUSB0 ./tools/ota-esp32.sh      # over the cable
#
# WHY THIS MATTERS: once the ESP32 takes its 5 V from VIN, no USB cable may
# be plugged in at all -- the DEVKIT V1 ties USB 5 V and VIN together and
# cannot take two supplies at once.
#
# PARTITIONS: we build with "min_spiffs" instead of the default, because
# WiFi AND Bluetooth together do not fit in the default layout. The
# partition table is only written on a CABLE upload, never over the air.
set -euo pipefail

SKETCH="${SKETCH:-firmware/esp32-gamepad}"
ESP="${ESP:-lego4wd-gamepad.lan}"
OTA_PASS="${OTA_PASS:-lego4wd}"
FQBN="esp32-bluepad32:esp32:esp32doit-devkit-v1"

echo "Compiling $SKETCH ..."
arduino-cli compile --fqbn "$FQBN" -e \
  --build-property build.partitions=min_spiffs \
  --build-property upload.maximum_size=1966080 \
  "$SKETCH"

if [ -n "${USB:-}" ]; then
  echo "Uploading over the cable on $USB ..."
  # --input-dir instead of the sketch folder, so arduino-cli takes exactly
  # the files just built -- including partitions.bin with the new layout.
  BUILD_DIR=$(find "$SKETCH/build" -mindepth 1 -maxdepth 1 -type d | head -1)
  arduino-cli upload -p "$USB" --fqbn "$FQBN" --input-dir "$BUILD_DIR"
  echo "Done. From here on it works over WiFi."
  exit 0
fi

BIN=$(find "$SKETCH/build" -name '*.ino.bin' ! -name '*bootloader*' ! -name '*partitions*' | head -1)
[ -n "$BIN" ] || { echo "No .bin found -- did the compile really succeed?" >&2; exit 1; }

ESPOTA=$(find ~/.arduino15/packages/esp32-bluepad32 -name espota.py | head -1)
[ -n "$ESPOTA" ] || { echo "espota.py not found" >&2; exit 1; }

echo "Sending $(basename "$BIN") ($(($(wc -c < "$BIN") / 1024)) KB) to $ESP ..."

# espota lies about the result: the ESP32 restarts the instant the write is
# finished, so the acknowledgement espota waits for never arrives -- and its
# own error path is broken (a Python 2 leftover). It therefore reports a
# failure although everything worked. We ignore its exit code and check the
# device itself instead.
set +e
OUTPUT=$(python3 "$ESPOTA" -i "$ESP" -p 3232 -a "$OTA_PASS" -f "$BIN" 2>&1)
set -e

# Did it even get as far as writing? A wrong password says "Authentication
# Failed" and never "Uploading".
case "$OUTPUT" in
  *Uploading*) ;;
  *) echo "$OUTPUT"; echo "Upload never started." >&2; exit 1 ;;
esac

echo "Written. Waiting for the reboot ..."

# First wait for it to DISAPPEAR -- that is the proof it really restarted
# and is not simply still running the old firmware. Then wait for it back.
was_gone=0
for _ in $(seq 1 75); do
  if ! ping -c1 -W1 "$ESP" >/dev/null 2>&1; then was_gone=1; break; fi
  sleep 0.2
done

for _ in $(seq 1 80); do
  if ping -c1 -W1 "$ESP" >/dev/null 2>&1; then
    [ "$was_gone" = 1 ] || echo "Note: I did not catch the restart itself -- please confirm the NEW build is running."
    echo "Done -- the ESP32 is back up and ready."
    exit 0
  fi
  sleep 0.5
done

echo "ESP32 did not come back after the update -- check it over the cable (USB=...)." >&2
exit 1
