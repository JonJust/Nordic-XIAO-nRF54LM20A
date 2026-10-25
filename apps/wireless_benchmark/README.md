# Two-board 4 Mbit/s ESB benchmark

This application evaluates a fixed-channel radio link between two XIAO
nRF54LM20A **V1.0** boards. One board runs the **TX** image, the other the **RX**
image. The transmitter schedules one synthetic mouse packet every **125 µs**;
the receiver measures what actually arrives. It is an initial measurement tool,
and an 8 kHz schedule alone does not establish reliable 8 kHz wireless delivery.

The application uses four encapsulated modules:

| Module | Responsibility |
| --- | --- |
| ESB transport | Configure the 4 Mbit/s radio, submit packets, drain received packets, and report radio events |
| Link state machine | Discover the peer, agree on a test run, start and finish it, exchange the receiver's summary, and reconnect after a timeout |
| Benchmark | Account for scheduled slots and received packets, derive statistics, and maintain debugger-visible measurements |
| UART telemetry | Send state changes, one-second status updates, and completed-run summaries through the SAMD11 USB serial bridge |

The two roles deliberately differ. They run the same application source with
`tx.conf` or `rx.conf`, producing `build-tx/` and `build-rx/`. The target remains
`xiao_nrf54lm20a@1.0.0/nrf54lm20a/cpuapp`. The base configuration enables debug
information and `-Og` so that the structures are practical to inspect in GDB.

## Build and identify the two boards

Open this folder in VS Code (the repository root and existing blinky workspace
also contain the wireless Run/Debug profiles):

```text
/home/skipper/repos/seeed_studio_nRF54LM20A/apps/wireless_benchmark
```

Use **Tasks: Run Task** to select **nRF: Build wireless TX (NCS 3.4.1)** or
**nRF: Build wireless RX (NCS 3.4.1)**. TX is the default build task in this
application folder, so your existing **Ctrl+B** binding builds TX here. Separate
clean tasks remove compiled outputs for each role while retaining its build
configuration.

Each board's SAMD11 debug probe has its own USB serial. Before programming,
run **nRF: List XIAO wireless probes**, or use a terminal:

```bash
python3 /home/skipper/repos/seeed_studio_nRF54LM20A/scripts/wireless-benchmark.py probes
```

The supplied VS Code tasks and profiles select these exact boards automatically:

| Role | XIAO USB serial |
| --- | --- |
| TX / mouse side | `E785BC0F` |
| RX / dongle side | `18B7FED4` |

Label the boards accordingly. The helper lists only Seeed USB device
`2886:0068`; an unrelated ST-Link or J-Link is excluded. If you replace a board,
update the matching `--serial` argument in your workspace's `tasks.json` and
`launch.json`. Unknown or ambiguous serials fail before programming. The terminal
helper accepts an omitted serial only when exactly one supported XIAO is connected.

The equivalent terminal commands are:

```bash
python3 /home/skipper/repos/seeed_studio_nRF54LM20A/scripts/wireless-benchmark.py build --role rx
python3 /home/skipper/repos/seeed_studio_nRF54LM20A/scripts/wireless-benchmark.py flash --role rx --serial 18B7FED4
python3 /home/skipper/repos/seeed_studio_nRF54LM20A/scripts/wireless-benchmark.py build --role tx
python3 /home/skipper/repos/seeed_studio_nRF54LM20A/scripts/wireless-benchmark.py flash --role tx --serial E785BC0F
```

The terminal `flash` command requires an existing build. VS Code's Flash and
Run/Debug entries build automatically. They use the installed NCS v3.4.1
toolchain and the previously verified Seeed OpenOCD HEX loader, including its
RRAM write-buffer commit and image verification. Automatic erase/unlock recovery
is disabled. These commands replace the selected Nordic chip's application.

## Run the test and inspect RAM

1. Select **Run wireless RX (build + flash)** in VS Code's Run and Debug view and
   click the green triangle. It selects `18B7FED4`.
2. Select **Run wireless TX (build + flash)** and start it. It selects `E785BC0F`.
   The firmware negotiates runs automatically and then repeats them.
3. Let both boards run for several minutes, then select
   **Attach wireless RX (preserve RAM)**. This uses the existing RX ELF for
   symbols and connects without building, flashing, or resetting the board.
   **Attach wireless TX (preserve RAM)** does the same for TX.
4. Click **Pause** to inspect the live globals in **Watch** or **Debug Console**.
   RX is the most useful first board to inspect because it can count packets
   actually delivered. TX's no-ACK completion events establish transmission,
   rather than reception.

Use **Debug wireless RX (build + flash)** or its TX counterpart when installing
new firmware or starting from reset. These profiles build, program, reset, and
stop at `main()`; press **F5** to start the application. **Run wireless RX/TX
(build + flash)** also installs a new image and resets RAM. Attaching assumes
the existing `build-rx/zephyr/zephyr.elf` or `build-tx/zephyr/zephyr.elf` matches
the firmware already on the selected board; rebuild and flash together after
source changes.

Add these expressions to Watch:

```text
g_benchmark_current
g_benchmark_results
g_benchmark_average
g_benchmark_metadata
g_link_status
```

`g_benchmark_current` is the run in progress. `g_benchmark_results` is the
**25-element array of run summaries**, including interrupted runs. Metadata identifies the next
write position, valid entries, and completed runs, so that array index zero is
not mistaken for the newest run after the ring wraps. `g_benchmark_average` is
the singleton containing running averages across completed runs, including runs
that have subsequently been overwritten in the 25-element history. Inspect the
status/completion fields alongside rates so a partial run is not mistaken for a
successful million-packet test. Each average is an arithmetic mean of per-run
fields; averaged minima/maxima are not lifetime extrema, and averaged histogram
bins are not pooled counts. Identifiers and absolute timestamps are not averaged.
See the benchmark header for each field's units and averaging rules.

A full test offers **1,000,000 scheduled opportunities**, taking approximately
**125 seconds**, plus negotiation and summary exchange. Twenty-five such
summaries cover roughly **52 minutes** before the oldest history slot is
overwritten. The summaries and averages live in RAM; a reset or reflash clears
them. UART telemetry reads the same measurements without clearing the history
or the running average.

Only one supplied debug session can use local GDB port 3333 at a time. Stop it
with **Shift+F5** before starting another Run/Debug/Attach operation. F5 continues
an existing paused session; it does not program the other board. The flash task
and debug/attach server use the same fixed serial for their selected role.

Connecting GDB briefly pauses the processor, and breakpoints and Pause halt it
longer. These interruptions interfere with radio timing and can make the partner
time out. Attaching preserves previous completed results in RAM, but treat the
current interrupted run as diagnostic or partial. Resume to allow negotiation
to recover, or reset both boards for a clean new session. The scheduler skips
late opportunities rather than transmitting a burst to catch up. Remove
measurement-path breakpoints while collecting useful results.

## Watch live status over USB serial

The V1.0 board already contains the required UART wiring. Nordic **UART20**
sends on **P1.11** and receives on **P1.10**. These signals cross the board's
level shifter to the SAMD11: Nordic TX reaches **SAMD11 PA05/RX**, and
**SAMD11 PA08/TX** reaches Nordic RX. The SAMD11 presents a **USB CDC serial
port** to the PC alongside its CMSIS-DAP debug interface. This path uses the
SAMD11's USB peripheral; Nordic native USB-HS is not needed.

The UART is configured for **115200 baud, 8 data bits, no parity, one stop bit**,
with no hardware flow control. The host must assert **DTR** for the SAMD11
firmware to forward the UART data. The supplied monitor sets DTR explicitly,
finds boards by their exact USB serials, and handles both serial ports
concurrently so a silent board cannot hold up output from its partner.

In **Tasks: Run Task**, select one of:

- **nRF: Monitor wireless RX (UART)**
- **nRF: Monitor wireless TX (UART)**
- **nRF: Monitor wireless TX + RX (UART)**

These tasks are available in the repository root, blinky, and wireless benchmark
workspaces. They open serial ports only: they do not build, flash, reset, or
clear the Nordic chips' RAM. Stop monitoring with **Ctrl+C** in the task
terminal. Only one serial monitor should open a given board at a time. A normal
running CMSIS-DAP debug session can coexist with UART monitoring, but halting
the Nordic also stops its telemetry and disturbs the radio test.

The same monitor can be run directly:

```bash
/usr/bin/python3 /home/skipper/repos/seeed_studio_nRF54LM20A/scripts/monitor-wireless.py --role both
/usr/bin/python3 /home/skipper/repos/seeed_studio_nRF54LM20A/scripts/monitor-wireless.py --role rx
/usr/bin/python3 /home/skipper/repos/seeed_studio_nRF54LM20A/scripts/monitor-wireless.py --role tx --duration 15
```

`--role` defaults to `both`; `--duration` takes a positive number of seconds and
is useful for a bounded capture. Redirect standard output to save firmware
messages; monitor connection messages go to standard error. Linux's persistent
port names end in `CMSIS-DAP_E785BC0F-if02` for TX and
`CMSIS-DAP_18B7FED4-if02` for RX under `/dev/serial/by-id/`. The helper verifies
USB vendor/product `2886:0068` and the expected serial instead of guessing which
role `/dev/ttyACM0` happens to have after a reconnect.

The firmware sends role-prefixed state changes, a status update approximately
once per second, and summaries when runs finish. Formatting and transmission
stay out of the per-packet radio path. The telemetry UART uses asynchronous
DMA transmit with interrupt priority **5**, below the RF, packet cadence, and
GRTC interrupts; it does not poll each character while those time-critical
operations wait. The application only transmits diagnostics on this UART; it
does not implement serial commands.
The module takes coherent counter snapshots only when a report is needed;
ordinary 10 ms main-loop passes use small change hints without masking IRQs.
`g_status_telemetry_enabled` defaults to `true`. It is a debugger-visible
switch for comparison tests: clearing it stops future reporting without
changing radio operation or UART initialization. A DMA line already queued may
finish. Re-enabling it resumes output, with the first rate window spanning the
time reporting was disabled. The usual debugger-halt timing caveat still applies.

Interpret the counters as follows:

- **RX/s and TX/s** come from changes in boot-lifetime packet counters over the
  reporting interval. They continue to work across the reset of per-run counters
  when a new million-packet run starts. RX counts unique accepted data packets;
  TX counts local successful data transmissions.
- **Run counts** describe the active run and restart at zero for a new run.
  These are `tx`, `rx`, `lost`, `busy`, `missed`, and `err`. RX's live missing
  count covers sequence gaps through the newest received
  packet. It can include packets the transmitter never submitted, as well as
  packets lost over the air or rejected before acceptance; it is not a pure
  measurement of RF corruption.
- **Link/UART diagnostics** (`rejected`, `ctrl_fail`, `ctrl_overflow`,
  `uart_err`, and `uart_abort`) accumulate since boot. For example, reflashing
  RX while TX is still sending an older run produces rejected packets until
  the boards negotiate the next shared run; this is separate from that new
  run's `lost` count.
- **Completed-run loss** also includes any unreceived tail after the final
  sequence seen. Use that final summary for the full run's delivery result.
  No-ACK TX success by itself does not prove reception or continuing peer
  presence; inspect RX output or the returned receiver summary.

The RAM ring and all-completed-run average remain available for debugging.
Opening the monitor partway through a run shows future updates; it does not
replay earlier output or replace the saved RAM results. Rebuilding and flashing
the telemetry firmware, like any firmware update, resets those RAM results.

## What the measurements mean

The radio uses **ESB at 4 Mbit/s**, with hardware CRC16. Control messages use
acknowledgments; the high-rate data phase uses **no-ACK packets** to avoid adding
an acknowledgment transaction to every 125 µs slot. A data packet contains a
type byte, 32-bit run ID, 32-bit sequence number, and seven bytes of synthetic
mouse data, for **16 payload bytes**. The mouse fields are buttons, signed 16-bit
X/Y movement, and signed 8-bit wheel/pan. The radio CRC is additional on-air
framing, rather than another field in that payload.

The link exchanges HELLO/READY, then START/ARMED, waits a 20 ms start guard, runs the
data phase, and repeats FINISH requests to obtain the queued SUMMARY. It ends based on
the scheduled attempt count and timeouts, rather than waiting indefinitely for
one million successful receptions. Run IDs distinguish old packets from a new
test; sequence numbers expose missing or duplicate packets. A receiver watchdog
records a partial result and reconnects when its peer disappears.
The transmitter checks the receiver again at the end of the run. Continuous
no-ACK data carries no reverse feedback, so a mid-run receiver outage is visible
to TX only when its subsequent control requests fail; TX success counters alone
cannot establish that the link remains alive.

Scheduling uses a dedicated hardware timer; elapsed time and receive-event
timestamps use the local 1 MHz GRTC cycle counter through Zephyr. One timer
interrupt every 125 µs is the intended cadence. A receive callback timestamp
includes interrupt/driver latency, so its gap distribution is not a direct
measurement of antenna-to-antenna airtime. The two boards' clocks are independent:
subtracting a TX timestamp from an RX timestamp does not establish one-way
latency. A later logic-analyzer test should compare GPIO markers on a common
clock and verify the actual on-air schedule.

This timing build deliberately forces Nordic constant-latency operation and
holds instruction RRAM in standby. Both reduce interrupt wake-up latency at the
cost of higher current. These results do not predict a battery-powered mouse's
power consumption; power optimization needs its own measured configuration.
The kernel clock interrupt also runs below the radio/event/cadence interrupts,
and main services slow control every 10 ms. The data schedule remains 125 µs;
slowing the control loop reduces interference with that schedule. Samples over
30 µs late are skipped, and a still-busy radio also rejects a new submission;
both appear in the counters rather than accumulating an old-data queue.

`packet_rate_millihz` is the scheduled-opportunity rate on TX and the unique
delivery rate on RX; divide by 1,000 to obtain packets/second. TX can show an
8,000 Hz opportunity rate while busy or missed-slot counters reveal fewer actual
transmissions. Rates and averages are derived when a run finishes. Live elapsed
time and counters remain inspectable during the run; live RX missing count
covers losses through the latest received sequence, and the final count adds
the unreceived tail.

Inspect scheduled slots, submitted/transmitted packets, unique received packets,
missing sequences, late slots, failures/overflow, duration, and interarrival
statistics together. A high average packet rate alone can conceal gaps. The
4 Mbit/s PHY rate is a configured signaling rate; delivered packets per second
and useful payload bytes per second are the measured outcomes.

This version evaluates the two-board radio path. It does not implement the
STM32 SPI transport, USB HID dongle, production pairing buttons, encryption,
authentication, or adaptive channel hopping. The V1.0 XIAO's USB-C is its debug
bridge; it does not expose the Nordic chip's native USB-HS. Those stages are
described in [the wireless evaluation plan](../../docs/WIRELESS_EVALUATION.md).

## Software verification

Both role images compile with NCS v3.4.1 for the XIAO V1.0; application warnings
are treated as errors. The pinned vendor board still emits its existing
`NRF_PLATFORM_LUMOS` deprecation notice. Host checks exercise the production
statistics and state machine, including ring wrap, all-history averages,
negotiation retries, loss, stale slots, and timeouts:

- [Statistics host checks](../../tests/benchmark_stats/README.md)
- [Link state machine host checks](../../tests/link_state_machine/README.md)

Host checks do not emulate the Nordic RF peripheral. For a meaningful delivery
measurement, inspect RX's results from a test with two physical boards.
The [validation record](VALIDATION.md) includes the completed single-board and
two-board tests, plus live UART validation and the reporting-overhead comparison.
