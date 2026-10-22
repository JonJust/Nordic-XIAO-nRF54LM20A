# Local OpenOCD

The unmodified Seeed OpenOCD executable and scripts are installed under
`openocd/`. This is a project-local tool installation, not a system package.

- Download: https://files.seeedstudio.com/arduino/platformio/forsilicon-openocd-linux.tar.gz
- Archive SHA-256: `e968f3f765ac9a0ad72662f28d614f46adf0f3c76e2ef28c23b33e7c668419a3`
- Binary: `openocd/bin/openocd`
- Script directory: `openocd/share/openocd/scripts`
- Reported version: `0.12.0+dev-01514-g21fa2de70 (2024-02-07-19:19)`
- Package version: `3.1200.0`
- License: GPL-2.0-or-later, as recorded in the package metadata.

The URL is selected by Seeed's [versioned LM20A helper](https://github.com/Seeed-Studio/micropython-seeed-boards/blob/f760904deb68f02f049bffa108b2dd6e2846bb6e/tools/xiao_nrf54lm20a_flash/xiao_nrf54lm20a_flash.py).
The archive's single top-level directory was flattened into `openocd/`.

Host-side checks passed: version output, required dynamic libraries, and parsing
the board configuration with `noinit`.

Hardware validation on October 21, 2026 also passed after installing the prepared
udev rule scoped to Seeed USB ID `2886:0068` and recovering the protected Nordic
target with Seeed's erase/unlock procedure. OpenOCD programmed the blinky debug
image and verified 64,896 bytes. The installed Microsoft C/C++ debug adapter
reached `main()` and a source breakpoint, stepped the LED state from `false` to
`true`, and observed a successful GPIO update. Breakpoints were removed,
execution resumed, and the debugger disconnected. UART capture at 115200 baud
confirmed alternating LED messages afterwards. The regular build has not been
flashed. See
[the debug guide](../apps/blinky/DEBUGGING.md) for the validated workflow.
