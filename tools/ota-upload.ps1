# Push new firmware to the car over WiFi -- no USB cable needed.
#
#   .\tools\ota-upload.ps1                          # uploads firmware/lego4wd
#   .\tools\ota-upload.ps1 -Address 192.168.1.155   # different address
#   .\tools\ota-upload.ps1 -Sketch firmware/tests/03_drive_test
#
# The very first upload has to go over USB (the sketch must already contain
# the OTA receiver). And if you ever flash a build that cannot join the WiFi,
# only the cable will get you back in.

param(
  [string]$Sketch   = "firmware/lego4wd",
  [string]$Address  = "car.lan",       # IP or local DNS name of your car
  [string]$Password = "lego4wd"        # must match SECRET_OTA_PASS
)

$ErrorActionPreference = "Stop"

# Adjust if arduino-cli is not on your PATH
$cli  = if (Get-Command arduino-cli -ErrorAction SilentlyContinue) { "arduino-cli" }
        else { "C:\Program Files\Arduino CLI\arduino-cli.exe" }
$fqbn = "arduino:renesas_uno:unor4wifi"

Write-Host "Compiling $Sketch ..." -ForegroundColor Cyan
& $cli compile --fqbn $fqbn -e $Sketch
if ($LASTEXITCODE -ne 0) { throw "Compile failed" }

$bin = Get-ChildItem "$Sketch/build/arduino.renesas_uno.unor4wifi/*.ino.bin" | Select-Object -First 1
Write-Host "Sending $($bin.Name) ($([math]::Round($bin.Length/1KB)) KB) to $Address ..." -ForegroundColor Cyan

# Arguments as an array instead of backtick line continuations: those break
# far too easily in PowerShell and curl then only sees fragments.
# -H "Expect:" matters -- the tiny server on the Arduino does not speak
# "100-continue", so curl would otherwise wait forever.
$curlArgs = @(
  "--silent", "--show-error",
  "--output", "NUL",
  "--write-out", "Upload: HTTP %{http_code} in %{time_total}s`n",
  "--max-time", "120",
  "--user", "arduino:$Password",
  "--header", "Expect:",
  "--data-binary", "@$($bin.FullName)",
  "http://${Address}:65280/sketch"
)
& curl.exe @curlArgs
if ($LASTEXITCODE -ne 0) { throw "Upload failed (curl $LASTEXITCODE)" }

Write-Host "Waiting for the reboot ..." -ForegroundColor Cyan
$waitArgs = @(
  "--silent", "--output", "NUL",
  "--write-out", "Car back online: HTTP %{http_code}`n",
  "--retry", "15", "--retry-delay", "2",
  "--retry-all-errors", "--retry-connrefused",
  "--max-time", "60",
  "http://$Address/"
)
& curl.exe @waitArgs
