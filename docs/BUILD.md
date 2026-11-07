# Build BAX:KI:ZL V1.0

Most users should use the [prebuilt Windows Companion](https://github.com/AAARRRGGHH/BAX-KI-ZL/releases/download/v1.0/BAX-KI-ZL-Companion-Windows.zip). These instructions are for modifying the source.

## Windows simulator

Requirements: Windows 11 x64, Git, PowerShell, Visual Studio 2022 with **Desktop development with C++** and CMake tools.

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File FetchDependencies.ps1
powershell -ExecutionPolicy Bypass -File Build.ps1
```

Fetching verifies the documented SDL 2.30.12 and LVGL 9.2.2 commits. The modified board component is included; do not replace it with an unmodified download. Generated LVGL fonts are included, so Node.js is not needed for a normal build.

Run `Starten.cmd`, or `build/Release/SmartMidiSimulator.exe`. Settings are stored beside that simulator. Packaged Companion uses `%LOCALAPPDATA%/BAX-KI-ZL` instead.

Core tests without SDL/LVGL:

```powershell
powershell -ExecutionPolicy Bypass -File Build.ps1 -TestsOnly
```

The optional Ripples lab targets are developer experiments, not another firmware mode. Kinetic uses their shared scheduler; do not delete `experimental/ripples/`.

## ESP32-S3 firmware

Install **ESP-IDF 5.5.4**, including its ESP32-S3 compiler and Python environment, following Espressif's instructions. Open its PowerShell environment and fetch project dependencies as above.

```powershell
python tools/prepare_idf_lock.py
cd firmware
idf.py set-target esp32s3
idf.py build
```

The portable `firmware/dependencies.lock.in` template pins the release components. `tools/prepare_idf_lock.py` writes a local `dependencies.lock` with the checkout's board/touch paths; the generated file is not committed. The local 1.75C BSP, CST9217 driver, LVGL compatibility header, internal display DMA buffers and protected RAM reserve are required. Do not substitute another board variant.

For development flashing, return to the root and run `Flashen.cmd`. It prepares the environment, builds, flashes and opens the monitor. For fast prebuilt installation, use Companion. Close other serial monitors first.

ROM recovery: hold BOOT while connecting/resetting, release BOOT when the download port appears, then retry that port. Windows can assign different COM numbers to the ROM loader and the MIDI + CDC application. Do not erase NVS merely to fix a port transition. See the manuals for the complete recovery procedure.

Software builds do not replace testing display, touch, power, audio and MIDI on the actual board.

## Windows Companion

Requires the Release simulator, an ESP-IDF firmware build, Python 3.12 and Visual Studio's x64 redistributable files.

```powershell
powershell -ExecutionPolicy Bypass -File Build-Companion.ps1 -InstallDependencies
```

The builder stages EXE/ZIP under `dist/`, without flashing. It embeds current firmware/simulator binaries, records SHA-256 hashes and includes manuals, source and dependency notices. Run launcher tests and `tests/check_companion_embedding.py` after Companion changes.

## Manuals and assets

See [MANUAL_BUILD.md](MANUAL_BUILD.md). Captures use isolated settings. Barlow Regular/Bold are supplied under the OFL. The editable boot logo is `assets/boot_logo.svg`. Generated fonts, logo masks and RESET text are checked in; regeneration is optional for a normal build.
