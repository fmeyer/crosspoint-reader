# CrossPoint test firmware — side-button highlight experiment

**Version:** `1.3.0-dev-claude/branch-from-1-3-0-kicq4d-e8efb36`
**Source:** branch [`claude/branch-from-1-3-0-kicq4d`](https://github.com/fmeyer/crosspoint-reader/tree/claude/branch-from-1-3-0-kicq4d) @ `e8efb36`
**Build:** `pio run -e default` (debug build: serial logging on, `LOG_LEVEL=2`, `CROSSPOINT_HIGHLIGHT_EXPERIMENT=1`)

Includes the 1.3.0 baseline, Portuguese hyphenation, and the experimental
side-button text highlight mode.

## Download

[`firmware.bin`](https://github.com/fmeyer/crosspoint-reader/raw/firmware/highlight-test/firmware.bin) (~5.7 MB, app-only image)

## Flash (pick one)

1. **Web flasher (recommended):** <https://crosspointreader.com/#flash-tools> → select X4 →
   **Custom .bin** → upload `firmware.bin`.
2. **Command line:**
   ```sh
   esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 firmware.bin
   ```
3. **SD card (no USB):** copy `firmware.bin` anywhere on the SD card, then on the device:
   Settings → firmware update → pick the file.

## Testing the highlight mode

In the EPUB reader, on a text page:

| Action | Result |
|---|---|
| Press **both side buttons** together | Enter highlight mode: first word on screen inverts |
| Both side buttons again (in mode) | Jump to first word of next sentence (wraps to top) |
| **PageForward** ×1 | Extend selection by one word |
| **PageForward** rapid ×2 / ×3 (within ~450 ms) | Expand to whole sentence / whole paragraph |
| **PageBack** ×1 / rapid ×2 / ×3 | Shrink by a word / trim to anchor's sentence / collapse to anchor word |
| **Confirm** | Save highlight ("Highlight saved." popup) and exit |
| **Back** | Exit without saving |

Saved highlights land on the SD card in `/.crosspoint/highlights/<book>.hl`
(binary records: spine, page, word range, and a text snippet).

Notes for this experiment build:
- With `longPressButtonBehavior = OFF` (default), single side-button page turns are deferred
  by up to 250 ms so the chord can be detected — a slight extra latency on page turns.
- Anti-aliased (grayscale) text rendering is temporarily disabled while highlight mode is
  active; it comes back on exit (exit forces a HALF refresh to clear ghosting).
- Side buttons must be enabled (`sideButtonLayout` ≠ disabled) for the chord to work.
