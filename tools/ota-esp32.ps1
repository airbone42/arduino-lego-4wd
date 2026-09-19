# Push new firmware to the ESP32 gamepad bridge over WiFi -- no USB cable.
#
#   .\tools\ota-esp32.ps1                     # over WiFi (the normal case)
#   .\tools\ota-esp32.ps1 -Usb                # over the cable, e.g. the first time
#   .\tools\ota-esp32.ps1 -Address 192.168.1.157
#
# WHY THIS MATTERS: once the ESP32 takes its 5 V from VIN, no USB cable may
# be plugged in at all -- the DEVKIT V1 ties USB 5 V and VIN together and
# cannot take two supplies at once. Without OTA you would have to pull the
# power lead before every single update.
#
# PARTITIONS: we build with "min_spiffs" instead of the default. That makes
# each of the two program halves 1.9 MB instead of 1.25 MB -- necessary,
# because WiFi AND Bluetooth together need a lot of room (with the default
# layout we were at 88 %, so nearly full).
# NOTE: the partition table is only written on a CABLE upload, never over
# the air. Change the scheme and you have to reach for the cable once.

param(
  [string]$Sketch   = "firmware/esp32-gamepad",
  [string]$Address  = "lego4wd-gamepad.lan",
  [string]$Password = "lego4wd",
  [switch]$Usb,
  [string]$Port     = "COM5"
)

$ErrorActionPreference = "Stop"
$cli  = "arduino-cli"
$fqbn = "esp32-bluepad32:esp32:esp32doit-devkit-v1"
$core = "$env:LOCALAPPDATA\Arduino15\packages\esp32-bluepad32\hardware\esp32\4.1.0"

# These two belong together: the first says HOW the flash is split up, the
# second how big the sketch may then be.
$buildArgs = @(
  "compile", "--fqbn", $fqbn, "-e",
  "--build-property", "build.partitions=min_spiffs",
  "--build-property", "upload.maximum_size=1966080",
  $Sketch
)

Write-Host "Compiling $Sketch ..." -ForegroundColor Cyan
& $cli @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Compile failed" }

if ($Usb) {
  Write-Host "Uploading over the cable on $Port ..." -ForegroundColor Cyan
  # --input-dir instead of the sketch folder: that way arduino-cli takes
  # exactly the files just built -- including partitions.bin with the new
  # layout. The upload command itself does not understand --build-property.
  $buildDir = Get-ChildItem "$Sketch/build" -Directory | Select-Object -First 1
  $uploadArgs = @(
    "upload", "-p", $Port, "--fqbn", $fqbn,
    "--input-dir", $buildDir.FullName
  )
  & $cli @uploadArgs
  if ($LASTEXITCODE -ne 0) { throw "Upload failed" }
  Write-Host "Done. From here on it works over WiFi, without -Usb." -ForegroundColor Green
  return
}

$bin = Get-ChildItem "$Sketch/build" -Recurse -Filter "*.ino.bin" |
       Where-Object { $_.Name -notmatch "bootloader|partitions" } |
       Select-Object -First 1
if (-not $bin) { throw "No .bin found -- did the compile really succeed?" }

$espota = Join-Path $core "tools\espota.exe"
if (-not (Test-Path $espota)) { throw "espota.exe not found: $espota" }

Write-Host "Sending $($bin.Name) ($([math]::Round($bin.Length/1KB)) KB) to $Address ..." -ForegroundColor Cyan

# Port 3232 is the fixed update port of ArduinoOTA on the ESP32.
$otaArgs = @(
  "-i", $Address,
  "-p", "3232",
  "-a", $Password,
  "-f", $bin.FullName
)

# Two quirks of espota you need to know about:
#  1. It writes perfectly ordinary status messages to the ERROR stream.
#     With ErrorActionPreference = "Stop" PowerShell mistakes that for a
#     crash and aborts, even though the upload is running fine.
#  2. The ESP32 restarts the instant the write is finished. espota then
#     waits for an acknowledgement it will never get -- and its own error
#     path is broken ("NameError: global name 'e' is not defined", a
#     Python 2 leftover). So it reports a failure although all went well.
# We therefore do not take espota at its word and check the device itself.
$saved = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $espota @otaArgs 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $saved

# Did it even get as far as writing? With a wrong password the output says
# "Authentication Failed" and never "Uploading".
$didWrite = $output -match "Uploading"
if (-not $didWrite) {
  Write-Host $output
  throw "Upload never started (espota $code)"
}

Write-Host "Written. Waiting for the reboot ..." -ForegroundColor Cyan

# We check with ping rather than by connecting to port 3232: the update
# receiver listens there over UDP, so an ordinary connection attempt always
# comes to nothing. Ping is meaningful anyway -- the ESP32 only gets itself
# onto the WiFi if our sketch is actually running.
function EspResponds($address) {
  return Test-Connection -ComputerName $address -Count 1 -Quiet -ErrorAction SilentlyContinue
}

# First wait for it to DISAPPEAR -- that is the proof it really restarted
# and is not simply still running the old firmware.
$wasGone = $false
$until = (Get-Date).AddSeconds(15)
while ((Get-Date) -lt $until) {
  if (-not (EspResponds $Address)) { $wasGone = $true; break }
  Start-Sleep -Milliseconds 200
}

# ... and then for it to come back.
$ready = $false
$until = (Get-Date).AddSeconds(40)
while (-not $ready -and (Get-Date) -lt $until) {
  if (EspResponds $Address) { $ready = $true; break }
  Start-Sleep -Milliseconds 500
}

if (-not $ready) {
  throw "ESP32 did not come back after the update -- check it over the cable (-Usb)"
}
if (-not $wasGone) {
  Write-Host "Note: I did not catch the restart itself (too quick?)." -ForegroundColor Yellow
  Write-Host "It is reachable -- please confirm the NEW build is running." -ForegroundColor Yellow
}

Write-Host "Done -- the ESP32 is back up and ready." -ForegroundColor Green

# espota exits with an error code although everything worked (see above).
# Without this line PowerShell would pass that code on and every caller
# would wrongly think the upload had failed.
exit 0
