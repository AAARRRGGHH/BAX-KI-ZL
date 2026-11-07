# Local two-contact CST9217 extension

Based on the official Waveshare component `waveshare/esp_lcd_touch_cst9217` 2.0.0,
repository commit `9daffb7168b3c093a330da700988a22092294eef`, path
`display/touch/esp_lcd_touch_cst9217`. Original license is in `license.txt`.

Changes are deliberately local; no files in managed_components or vendor are patched:

- Read two contacts (15-byte report), instead of the upstream one-contact limit.
- ACK each successfully read report, following Waveshare's bundled SensorLib
  `TouchDrvCST92xx::getPoint`, C-board examples commit
  `4bfdc80eedbf95031e384610796bed15e0240312`.
- Decode track IDs from the high nibble of each contact record and expose
  `get_track_id` to `esp_lcd_touch_get_data`.
- Compact active contacts when a report also contains a released contact.
- Reject duplicate IDs/malformed reports; clear cached contacts on failed reads.
- Report transition to two active contacts in the serial log.

Shared parser: `include/cst9217_report.h`; tested on synthetic two-contact reports,
reordered IDs, releases, malformed ACKs/counts/lengths and duplicate IDs.
Actual two-contact operation on the delivered panel still requires hardware testing.

The BSP registration call is redirected to `firmware/main/touch_control.cpp`,
which enables the adapter's two independent pointer devices. Panel and touch
pin configuration remain the official C-BSP defaults.
