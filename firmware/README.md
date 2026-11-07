# BAX:KI:ZL V1.0 firmware

Target: Waveshare ESP32-S3-Touch-AMOLED-1.75C, SKU 33691, 32 MB flash / 8 MB PSRAM.

See [source build instructions](../docs/BUILD.md), the [English manual](../docs/BAX-KI-ZL_Manual_EN.pdf) or [German manual](../docs/BAX-KI-ZL_Manual_DE.pdf).

Use ESP-IDF 5.5.4 and the included component manifests. Run `python tools/prepare_idf_lock.py` from the project root before the first IDF build. The portable lock template records the tested release dependencies without developer-specific absolute paths.

The modified Apache-2.0 1.75C BSP under `vendor/waveshare-bsp-1.75c` and the local CST9217 touch driver are required. Retain the two internal 20-row DMA buffers, PSRAM graphics caches and protected internal RAM reserve. The compatibility header in `compat/` is applied by the component CMake configuration.

Modes: Chords, Arpeggiator, Kinetic, Keyboard. Short PWR cycles modes; hold PWR for Setup. Hold BOOT for the power-off countdown. The manuals describe gesture/menu exceptions and USB recovery.

USB uses MIDI + CDC in application mode. ROM download and application COM numbers can differ. Close other serial monitors before flashing. Companion installs prebuilt releases; `Flashen.cmd` performs a development build and opens a monitor.

The initiator tested the device, audio, USB MIDI and WIDI. Windows BLE output showed timing jitter on the tested setup. Builds and automated tests do not replace device-specific verification.
