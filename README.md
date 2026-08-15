# CrossPoint X3 boot-panic triage builds

The `fm-latest` release build (`1.5.0-fm+6aea96e`) panics at boot on X3 shortly
after "Hardware detect: X3". These two DEBUG builds (LOG_LEVEL=2, verbose
serial + on-screen logs) isolate the cause:

| File | Contents | Version stamp |
|---|---|---|
| [`fm-debug.bin`](https://github.com/fmeyer/crosspoint-reader/raw/firmware/highlight-test/fm-debug.bin) | fm-tweaks (all tweaks), verbose | `1.5.0-dev-fm-tweaks-6aea96e` |
| [`master-debug.bin`](https://github.com/fmeyer/crosspoint-reader/raw/firmware/highlight-test/master-debug.bin) | plain upstream master, no tweaks, verbose | `1.5.0-dev-detached-95a847c` |

## Test order

1. Flash **fm-debug.bin**. If it crashes, the panic screen's "Last logs" will
   now show far more detail (debug logging) — photograph it.
2. Flash **master-debug.bin** (zero personal tweaks).
   - If it ALSO crashes → the bug is in upstream master's post-1.5.0 X3 work
     (new X3 display / battery / gyro changes), not the tweaks.
   - If it boots fine → the fm-tweaks port is at fault and the fm-debug logs
     will show where.

## If the device boot-loops

Recovery firmware mode: hold the **left side button (UP) + power** while
powering on — boots directly into the SD-card firmware update screen. Keep a
known-good `firmware.bin` on the SD card.

If serial is available, `python3 scripts/debugging_monitor.py` (or any monitor
at 115200) captures the full trace including the panic reason line, which the
on-screen dump truncated.

> These are sandbox builds (no custom-sdkconfig core rebuild). If fm-debug
> happens to boot fine while the CI fm_release crashed, that also tells us
> something: the suspect becomes the optimized core config, not the code.
