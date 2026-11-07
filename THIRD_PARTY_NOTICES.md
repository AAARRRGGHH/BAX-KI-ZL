# Third-party components

Project-owned code uses GPL-3.0-or-later. Existing components retain their licenses.

| Component | Use | License / source |
|---|---|---|
| LVGL 9.2.2 | Shared UI | MIT; pinned upstream revision in `FetchDependencies.ps1`. |
| SDL 2.30.12 | Windows display, input and audio | zlib; pinned upstream revision in `FetchDependencies.ps1`. |
| Barlow | TTFs and generated LVGL font data | SIL OFL 1.1; `assets/fonts/OFL.txt`, also applicable to `ui/fonts/`. |
| Waveshare 1.75C BSP 3.0.0 | Board support | Apache-2.0; retained under `vendor/waveshare-bsp-1.75c/`. |
| Waveshare CST9217 driver 2.0.0 | Touch, locally extended | Apache-2.0; retained under `firmware/components/waveshare__esp_lcd_touch_cst9217/`. |
| ESP-IDF 5.5.4 and managed components | Firmware platform | Original Espressif/component licenses, resolved through manifests and dependency lock. |
| TinyUSB / esp_tinyusb | USB MIDI and CDC | Original MIT / Apache-2.0 component licenses. |
| esptool 4.12.0 | Companion flash engine | GPL-2.0-or-later; source and license included in `Companion-Sources.zip`. |
| PyInstaller | Portable Windows app | GPL with upstream bootloader exception; original terms included in release. |
| Python, Tcl/Tk, Pillow, pyserial and launcher dependencies | Companion runtime | Original notices copied into release `licenses/`. |
| Microsoft Visual C++ runtime | Native Windows runtime | Microsoft redistributable terms; not the project GPL. |

Local BSP and touch-driver changes are supplied with their original headers and license files. Pinned SDL and LVGL sources are unmodified. Third-party names and trademarks do not imply endorsement.

The release's `licenses/` directory contains notices gathered from actual build inputs. Dependency fetching/building must preserve upstream notices. See `docs/BUILD.md`.
