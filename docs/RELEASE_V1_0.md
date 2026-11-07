# BAX:KI:ZL — V1.0

7 November 2026

- LOAD / SAVE includes all Kinetic settings: scale, octave range, BPM, divisor, timing modifier, quantize, lifetime and gate. Live balls are not stored.
- The experimental collision splitting option is removed. Ordinary physical ball collisions remain.
- Face-down reset clears Kinetic balls, queued notes and hit feedback, preserving all settings.
- New Kinetic defaults: octaves 3–5, straight 1/8, 120 BPM, 15 s lifetime, 50% gate, quantize on.
- The centre double-tap window is 400 ms (previously 350 ms).
- New Travel defaults: enabled, Square, 50 ms attack, 400 ms release (labelled DECAY on the instrument).
- Boot screen displays V1.0; V uses the current UI accent and 1.0 is white. Version text fades with the logo.

## Existing settings and saved slots

Existing settings remain unchanged; defaults apply to a fresh configuration. Old collision flags are ignored. Old 32-byte and 40-byte slots, including the existing eight-slot bank, remain readable. Since they contain no Kinetic state, loading them uses the new Kinetic defaults, with the shared scale when GLOBAL SCALE is enabled. Save the slot again to capture the desired Kinetic configuration.

A slot load writes all four modes together: one atomic file replacement in the simulator, or one NVS runtime blob commit on the ESP32. Loading preserves the global MIDI, UI colour and Travel settings. Do not downgrade to older firmware after saving in the new slot format without exporting a backup first; older versions cannot interpret the extended bank.

Builds and automated tests verify software behaviour. Flashing and physical confirmation remain separate device tests.
