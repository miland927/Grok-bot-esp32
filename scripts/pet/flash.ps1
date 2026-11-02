# Flash the pet firmware without losing the USB connection.
#
# esptool's default "hard reset" leaves this board's USB-Serial/JTAG and the
# AXP2101 display rail in a state where the screen stays black and the port
# disappears until the cable is re-plugged. A watchdog reset reboots the chip
# cleanly and keeps the port listed, so use that instead.
#
# Usage (from an ESP-IDF shell, after `python scripts/build.py ...`):
#   pwsh -File scripts/pet/flash.ps1 -Port COM3

param(
    [string]$Port = "COM3",
    [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $root "build"

if (-not (Test-Path (Join-Path $build "flash_args"))) {
    throw "build/flash_args not found - run scripts/build.py first"
}

Push-Location $build
try {
    python -m esptool --chip esp32s3 -p $Port -b $Baud `
        --before default-reset --after watchdog-reset write-flash @flash_args
} finally {
    Pop-Location
}
