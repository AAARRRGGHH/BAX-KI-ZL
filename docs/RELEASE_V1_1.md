# BAX:KI:ZL V1.1

## New: Motion CC

- New MOTION CC setup page immediately after MIDI, available in every play mode.
- Independent MIDI channel; X-AXIS and Y-AXIS choose OFF or CC 1–119.
- All three wheels wrap in both directions. Both axes default to OFF.
- CALIBRATE stores a comfortable neutral pose: value 64, ±45° to 0/127,
  a 2° dead zone, smoothing and at most 25 updates/s per axis.
- X is inverted; Y retains its original direction.
- Calibration confirmation lights up subtly for three seconds.
- Channel, assignments and calibration persist separately from musical slots.
- Ctrl+arrows / Ctrl+Home simulate tilt in the Windows simulator.
- Travel Mode continues to use the local synth and suppresses external MIDI.

## Improvements

- Color selection updates only its text and marker instead of recoloring hidden
  setup pages on every touch sample. The static ring uses a native RGB565 cache.
- Rendering regression test: about 16,500 rather than 217,000 transferred pixels
  per color change, with partial/full redraw agreement. This is a test workload,
  not a guarantee of a specific hardware frame rate.
- Updated German and English manuals with current screenshots.

## Compatibility

Normal updates retain NVS settings and musical slots. Old experimental Pitch
assignments become OFF; other motion settings and calibration are preserved.
Pitch Bend is not part of this release. The existing V1.0 release stays available.

Use the Windows ZIP to try the simulator or flash the supported Waveshare
ESP32-S3-Touch-AMOLED-1.75C, SKU 33691, 32 MB flash / 8 MB PSRAM. Close serial
monitors before flashing. See the manuals for installation and recovery.
