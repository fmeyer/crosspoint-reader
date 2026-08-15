# CrossPoint test firmware — text highlight experiment (fm-tweaks)

**Version:** `1.5.0-dev-fm-tweaks-7eb8c26`
**Source:** branch [`fm-tweaks`](https://github.com/fmeyer/crosspoint-reader/tree/fm-tweaks) @ `7eb8c26`
**Build:** `pio run -e default` (debug build: serial logging on, `LOG_LEVEL=2`, `CROSSPOINT_HIGHLIGHT_EXPERIMENT=1`)

Rev 10: rebased onto **upstream 1.5.0** (was a stale 1.3.0-dev base — 243 commits
behind, including the bold style-leak fix and antialiasing fixes). Carries:
upstream 1.5.0, Portuguese hyphenation, the text highlight experiment, and the
personal boot logo.

> Note: this sandbox-built binary omits upstream's custom-sdkconfig core rebuild
> (~35 KB extra free heap in official builds) because the build environment
> cannot reach Espressif's component registry. Functionally identical otherwise.

## Download

[`firmware.bin`](https://github.com/fmeyer/crosspoint-reader/raw/firmware/highlight-test/firmware.bin) (~5.6 MB, app-only image)

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
| **Back** | Exit without saving |
| Reader menu → **Highlights** | Chapter list with counts → per-chapter list: Confirm jumps, hold Confirm deletes |

Optional shortcut: Settings → Controls → **"Long-press menu function"** now has a
**Highlight** option — holding Confirm (~0.4 s) while reading then starts a
highlight directly (this replaces the separate hold-swap setting from rev 5;
bookmark toggling is available in the reader menu regardless).

Saved highlights render as **underlines** and persist in
`/.crosspoint/highlights/<book>.hl` (limit: 64 per chapter). Anti-aliased text
stays enabled on pages with highlights; only live selection renders BW.
