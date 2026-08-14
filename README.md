# CrossPoint test firmware — text highlight experiment

**Version:** `1.3.0-dev-claude/branch-from-1-3-0-kicq4d-1f3a562`
**Source:** branch [`claude/branch-from-1-3-0-kicq4d`](https://github.com/fmeyer/crosspoint-reader/tree/claude/branch-from-1-3-0-kicq4d) @ `1f3a562`
**Build:** `pio run -e default` (debug build: serial logging on, `LOG_LEVEL=2`, `CROSSPOINT_HIGHLIGHT_EXPERIMENT=1`)

Includes the 1.3.0 baseline, Portuguese hyphenation, and the experimental
text highlight mode.

> Rev 2: the original both-side-buttons entry chord turned out to be physically
> impossible (both side buttons share one ADC resistor ladder), so entry moved
> to the reader menu and sentence hopping to the front Left/Right buttons.

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
| **Confirm** → reader menu → **Highlight** | Enter highlight mode: first word on screen inverts |
| **PageForward** ×1 | Extend selection by one word |
| **PageForward** rapid ×2 / ×3 (within ~450 ms) | Expand to whole sentence / whole paragraph |
| **PageBack** ×1 / rapid ×2 / ×3 | Shrink by a word / trim to anchor's sentence / collapse to anchor word |
| Front **Right** / **Left** | Hop to next / previous sentence start (wraps around the page) |
| **Confirm** | Save highlight ("Highlight saved." popup) and exit |
| Reader menu → **Highlights** | List saved highlights: Confirm jumps to the page, hold Confirm deletes |
| **Back** | Exit without saving |

Saved highlights land on the SD card in `/.crosspoint/highlights/<book>.hl`
(binary records: spine, page, word range, and a text snippet).

Notes for this experiment build:
- Anti-aliased (grayscale) text rendering is temporarily disabled while highlight mode is
  active; it comes back on exit (exit forces a HALF refresh to clear ghosting).
- Normal reading controls are completely unaffected — page turns have no added latency.

> Rev 3: saved highlights now stay visible — the highlight remains inverted after
> Confirm, and re-appears whenever you come back to that page.

> Rev 4: new "Highlights" menu item lists a book's saved highlights — Confirm
> jumps to the highlight's page, holding Confirm deletes it.

> Rev 5: new Settings → Controls → "Hold Confirm action" option. Set it to
> "Highlight" and holding Confirm while reading starts a highlight instead of
> adding a bookmark; "Add Bookmark" then appears in the reader menu.
