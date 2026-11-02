# Whale validation variant

Build: `python scripts/build.py waveshare/esp32-s3-touch-amoled-2.06 --name esp32-s3-touch-amoled-2.06-whale --language zh-CN`

Hardware pins are unchanged. `CONFIG_WAVESHARE_WHALE_VALIDATION` enables the board-local display subclass.
UI uses neutral, thinking and happy GIFs. A user transcript selects thinking; speech/idle clears it.
Touch plays happy for ten seconds, then restores the latest conversation state. Timers run in LVGL;
application wake-up is scheduled on the application task. Automatic power-off is disabled during validation.

NVS, PHY, OTA metadata, factory and OTA-slot addresses match this device's verified backup.
The former storage region and unused tail are now a 10MiB assets partition.
This preserves the factory app bytes, but the renamed storage partition means the original application
requires its original partition table and storage backup to recover fully.

Do NOT use the generic merged image or `idf.py flash`: IDF selects the factory partition for the app.
The dedicated flashing procedure must put the new application in ota_1 at 0x1000000,
preserve NVS, preserve the original ota_0 app, explicitly select ota_1, and verify each written image.
The original backup and its checksum must be checked before flashing.

## Quad Flash above 16MiB

The retained partition layout puts ota_1 at 0x1000000 and assets at 0x1600000.
A 32MB size declaration alone does not enable CPU cache mapping above 16MiB.
This variant explicitly enables CONFIG_IDF_EXPERIMENTAL_FEATURES and
CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH in both bootloader and application.
Without these settings, startup reads aliased low-address data and reports a
bootloader-stack overlap before falling back to the original ota_0 firmware.
The high-address cache feature is experimental in ESP-IDF; validate this device's
Flash reliability, including cold boots, before treating the variant as stable.

## Larger character trial

The board-local display scales the existing 224px GIFs by 307/256 (119.9%).
A 272px centered viewport prevents parent clipping; a 24px extended click area
and the LVGL image advanced hit test follow the enlarged image bounds.
Source GIFs, decoder buffer dimensions, resource package and partitions remain
the same. Actual rendering cost and appearance require device validation.

## v5 common-ten development UI

160px GIFs are scaled by 430/256 in the same 272px viewport. Short click gives happy feedback; long press opens a scrollable ten-action selector. Manual playback expires after 10.2 seconds or is interrupted by dialogue. Emotion mappings and device acceptance status are documented in docs/whale/ACTIONS.md. New interactions require hardware acceptance.
