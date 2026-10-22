# Debug blinky on the XIAO nRF54LM20A V1.0

Debugging lets you pause the Nordic chip, inspect variables, and execute your C
code a line at a time. **Flashing, verification, hardware breakpoints, variable
inspection, stepping, and UART output have passed on this V1.0 board.**

## What connects to what

```text
VS Code → Arm GDB → OpenOCD → USB → SAMD11 → SWD → nRF54LM20A
```

VS Code presents source lines and variables. GDB matches those lines to machine
instructions using `build-debug/zephyr/zephyr.elf`. OpenOCD translates debugger
requests into commands for the board's SAMD11 CMSIS-DAP probe. The SAMD11 controls
the Nordic chip over its internal SWD wiring. Your application runs on the
Nordic Cortex-M33; the SAMD11 provides the connection.

Your V1.0 board already contains the probe, so a USB data cable is sufficient.
The launch configuration explicitly selects CMSIS-DAP; the other connected
ST-Link is not selected by this server.

### Linux device access

If OpenOCD reports `unable to open CMSIS-DAP device 0x2886:0x68` even though
`lsusb` lists the board, install the supplied device access rules once:

```bash
bash /home/skipper/repos/seeed_studio_nRF54LM20A/scripts/install-xiao-udev.sh
```

The script asks for your administrator password through `sudo`, installs a rule
matching only Seeed USB device `2886:0068`, and applies it to the connected board.
It grants the active desktop user and `plugdev` group access to the debugger and
UART. This machine's `skipper` account already belongs to `plugdev`.
Run VS Code, OpenOCD, and GDB as your normal user afterwards.

## Run and debug from VS Code

Connect the XIAO with its USB data cable and open this repository or `apps/blinky`
in VS Code. Open **Run and Debug** with **Ctrl+Shift+D**. Its configuration
dropdown now contains two entries:

| Entry | Start it with | Result |
| --- | --- | --- |
| **Debug XIAO blinky (build + flash)** | Select it, then **F5** or the green triangle | Builds, flashes, connects the debugger, and stops at `main()` |
| **Run XIAO blinky (build + flash)** | Select it, then the green triangle or **Ctrl+F5** | Builds, flashes, and leaves the application running without a debugger |

For debugging, set a breakpoint in `src/main.c`, select the Debug entry, and press
F5. VS Code runs **nRF: Flash blinky debug build (OpenOCD)** as its prelaunch task;
that task first builds `build-debug/` using `debug.conf`, then programs and verifies
the matching image. It starts OpenOCD on `localhost:3333`, connects GDB, resets the
Nordic CPU, and runs to a temporary hardware breakpoint at `main()`.

The yellow line at the opening of `main()` is an intentional pause. Press **F5**
to continue to your own breakpoint, or **F10** to step. The profile sets
`unknownBreakpointHandling` to `stop` so the temporary GDB breakpoint is displayed
as a normal pause. Without that setting, the C/C++ adapter labels it as
`Exception has occurred: Hit breakpoint ...`; that message does not indicate a
CPU fault.

For ordinary running, select the Run entry and click its green triangle. The same
build/flash task resets the board into the application. The Run terminal then
prints `XIAO nRF54LM20A is running.` and completes. The LED keeps blinking after
the VS Code run session finishes. **Press Shift+F5 to stop an active debug session
before starting Run or flashing again.** Its OpenOCD server owns the USB probe
until the session ends. The flash task now checks for an existing debug server
first and explains how to release it, avoiding the USB `Resource busy` errors.

The C/C++ editor play-button dropdown also offers **Debug C/C++ File** and
**Run C/C++ File**. Choose the corresponding XIAO configuration when prompted.
The separately installed **C/C++ Runner** and **C/C++ Compile Run** commands are
single-file desktop workflows; use the named XIAO entries for this board.

After changing C code, start the selected XIAO entry again. VS Code saves the
active editor group first and aborts launch on build/flash errors. Both entries
use `build-debug/`, keeping the ELF and programmed firmware matched. The ordinary
Ctrl+B task still builds into `build/` without programming the board.

The explicit build and flash tasks remain available through **Tasks: Run Task**.
Flashing uses Seeed's `nrf54lm20a-load` operation. Automatic recovery/erase remains
disabled, so a locked chip causes an error instead of an implicit recovery erase.

The Run entry uses C/C++'s local no-debug launcher only for its completion message
after the flash task succeeds. It does not attempt to execute the Arm ELF on the
Linux PC or start GDB/OpenOCD a second time.

The debug profile disables OpenOCD's GDB memory-map and flash-programming
features. Seeed's configuration declares a legacy `nrf5` flash bank; its automatic
probe fails on this chip with `Couldn't read CONFIGID register`, rejecting the GDB
connection. Programming still uses the explicit, verified Seeed loader. GDB treats
the RRAM code region as read-only and uses hardware breakpoints. A detach handler
resumes the chip when the debugger disconnects.

### NCS 3.4.1 configuration workaround

This SDK's crypto module leaves three unquoted string settings in the CMake
cache. On a later configuration pass, Zephyr interprets them as Kconfig overrides
and reports `malformed string literal` for `MBEDTLS_CONFIG_FILE`,
`TF_PSA_CRYPTO_CONFIG_FILE`, and `TF_PSA_CRYPTO_USER_CONFIG_FILE`.
The application's `CMakeLists.txt` removes only those malformed cache entries
before loading Zephyr. Kconfig restores their values from the normal configuration
sources; explicitly quoted overrides are preserved. No SDK changes or manual
edits to generated files are needed. This applies to both build directories.

## Inspect one LED transition

Open `src/main.c` and click the left gutter beside `led_on = !led_on;` to set a
breakpoint. Press **F5** to continue from `main()` to that breakpoint. The yellow
line marks the next statement to execute.

In **Watch**, add `led_on` and `err`. Select the `main` frame in **Call Stack** if
necessary. `err` becomes meaningful after its first assignment. A variable may
temporarily be unavailable when its value is not live at the current instruction.

Press **F10** once: `led_on` toggles. Step over `gpio_pin_set_dt()` to send that
state to the blue LED. A zero `err` indicates success. The devicetree GPIO API
handles the LED's active-low electrical polarity.

| Key | Action |
| --- | --- |
| F10 | Step over the next source statement |
| F11 | Step into a function; this can enter Zephyr or driver code |
| Shift+F11 | Finish the current function and return to its caller |
| F5 | Continue until another breakpoint or pause |
| Shift+F5 | End the debugging session |

`k_msleep(BLINK_INTERVAL_MS)` yields the application thread; the current interval
is 2,500 ms, so stepping over it can take about 2.5 seconds.
While the CPU is halted, the application cannot change the LED;
some peripherals may continue operating. Remove or disable the breakpoint, then
continue to observe normal blinking.

## Logs and later wireless work

Open a separate serial monitor on the XIAO's UART bridge at **115200 baud, 8-N-1**
to see `printk()` messages. On this machine it is `/dev/ttyACM0`, with stable path:

```text
/dev/serial/by-id/usb-Seeed_Studio_Seeed_Studio_XIAO_nRF54LM20A_CMSIS-DAP_E785BC0F-if02
```

Enable DTR in the serial monitor so the SAMD11 bridge forwards the UART data.
The VS Code **Debug Console** serves debugger commands and diagnostics; this setup
does not route UART output there.

When adding Bluetooth or proprietary radio code, halting the CPU disrupts packet
timing and can break a connection. Use breakpoints for initialization and state
inspection; use serial logs or timing measurements for radio behavior while it runs.

## Hardware validation: October 21, 2026

- The installed udev rule gives normal-user access to the CMSIS-DAP and UART
  interfaces of Seeed device `2886:0068`, probe serial `E785BC0F`.
- Initially, CTRL-AP `APPROTECT.STATUS` at offset `0x014` was `3` (both debug
  protections enabled); `ERASEPROTECT.STATUS` at `0x00C` was `0` (erase allowed).
  An explicit [erase/unlock recovery](https://docs.nordicsemi.com/r/bundle/ps_nrf54lm20a/page/ctrl-ap.html-ctrlap_erase)
  cleared the Nordic firmware/configuration and changed access protection to `0`.
  The ordinary flash/debug configurations still disable automatic recovery.
- The flash task programmed `build-debug/zephyr/zephyr.hex` and verified 64,896
  bytes successfully.
- The installed Microsoft C/C++ debug adapter, using the saved launch profile,
  stopped at `main()` and the hardware breakpoint on `main.c:31`. Source stepping
  changed `led_on` from `false` to `true`; stepping over the GPIO call produced
  `err = 0`.
- After removing breakpoints and disconnecting, the application resumed. UART
  capture at 115200 baud showed `LED off`, `LED on`, and `LED off` over seven
  seconds. The board was left running the debug image.
- The two VS Code launch entries were also tested inside the installed VS Code
  extension host: both automatic build and flash tasks returned zero; Debug
  reached `main()` and stepped the LED state, and Run completed without leaving
  a debug session attached. The Run entry passed both its ordinary start and
  Run Without Debugging paths.
- A subsequent VS Code retest confirmed that the temporary `main()` breakpoint
  reports `reason: breakpoint`, with no exception stop events. Run produced no
  stop events. The availability check rejected an attempt while the debug server
  was active and passed after the session was stopped.

Tool provenance: [Seeed's pinned OpenOCD helper](https://github.com/Seeed-Studio/micropython-seeed-boards/blob/f760904deb68f02f049bffa108b2dd6e2846bb6e/tools/xiao_nrf54lm20a_flash/xiao_nrf54lm20a_flash.py),
[pinned board configuration](https://github.com/Seeed-Studio/platform-seeedboards/blob/a6fd41debbbcda13b5340fff50c9092ed3289194/zephyr/nrf/boards/seeed/xiao_nrf54lm20a/support/openocd.cfg),
and [Microsoft's C/C++ launch reference](https://code.visualstudio.com/docs/cpp/launch-json-reference).
