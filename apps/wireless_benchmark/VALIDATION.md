# Validation — October 22, 2026

Both fixed-role images build for `xiao_nrf54lm20a@1.0.0/nrf54lm20a/cpuapp`
using NCS v3.4.1. Application C files use `-Og`, debug symbols, and
`-Wall -Wextra -Werror`. The pinned board package emits its existing
`NRF_PLATFORM_LUMOS` deprecation warning.

The initial radio-only images used the allocations below. UART-enabled image
allocations and their separate validation are recorded later in this document.

| Image | Flash allocation | RAM allocation |
| --- | ---: | ---: |
| TX | 113,252 bytes | 20,688 bytes |
| RX | 112,020 bytes | 20,688 bytes |

The statistics and both FSM role host tests pass, including AddressSanitizer
and UndefinedBehaviorSanitizer. Leak detection was disabled because the execution
environment's ptrace conflicts with LeakSanitizer; these tests use static storage.
Host checks cover accounting and control decisions, not the RF peripheral.

## Full single-board TX test

TX was programmed and verified on XIAO **E785BC0F**. Because only one XIAO probe
was visible to Linux, GDB deliberately advanced that board to the armed data
phase, bypassing the peer handshake for this isolated test. It then ran without
debugger pauses for a complete million-slot test. This validates scheduling,
ESB transmit completion, and RAM result publication; it does not measure
receiver delivery or prove the over-the-air handshake.

| Recorded TX field | Result |
| --- | ---: |
| Scheduled slots | 1,000,000 |
| Submitted packets | 1,000,000 |
| ESB successful no-ACK completions | 1,000,000 |
| Busy drops / submit errors / failed completions | 0 / 0 / 0 |
| Missed deadlines | 0 |
| First-to-last callback interval | 124,995,241 µs |
| Scheduled opportunity rate | 8,000.296 Hz |
| Maximum observed slot lateness | 12 µs |
| Slots over 10 µs late | 1,500 |

The tiny rate difference from nominal 8,000 Hz reflects measurement using the
local GRTC counter against TIMER20's schedule. These are software timestamps;
a logic analyzer is still needed to qualify on-air spacing.

After completion, the debugger showed `results[0]` containing that run,
`write_index = 1`, `valid_results = 1`, `runs_recorded = 1`,
`average_count = 1`, and an even publication generation of 2. The singleton
average matched the numeric fields of the first completed result, with its
identifiers and absolute timestamps cleared as documented.

The subsequent control exchange timed out because there was no receiver;
`remote_summary_valid` stayed false. That timeout is expected for this isolated
test. The board was reset to normal discovery afterwards; it does not bypass
negotiation in normal firmware operation.

Test image SHA-256:

```text
TX HEX 63f89d86ac9d0863781da12004a3d89aea1b1bb2b7184711c25332913e9e9f5c
RX HEX 5de00bc62d889662c41fc239f993f786bc46795d13f9d4378b94943334538699
```

## Two-board wireless test

Both boards are now programmed and communicating at 4 Mbit/s ESB:

| Role | USB probe serial |
| --- | --- |
| TX | `E785BC0F` |
| RX | `18B7FED4` |

The new RX chip initially had debug protection enabled (`APPROTECTSTATUS = 3`);
its erase protection allowed recovery. Vendor recovery cleared only that
Nordic chip, and its RX image was programmed and verified (112,008 image bytes).
The SAMD11 debugger was unaffected. Both boards were reset into normal operation.

The normal firmware negotiated a shared run ID, without forced GDB state
changes. OpenOCD memory reads observed progress without halting either CPU.
The first completed wireless run had ID `3366426258`:

| Measurement | Result |
| --- | ---: |
| TX scheduled / submitted / successfully transmitted | 1,000,000 / 1,000,000 / 1,000,000 |
| TX busy drops / missed deadlines / failed completions | 0 / 0 / 0 |
| RX unique packets | 999,827 |
| RX missing packets | 173 (0.0173%) |
| RX delivery | 99.9827% |
| RX duplicate / out-of-order packets | 0 / 0 |
| RX first-to-last callback interval | 124,994,130 µs |
| RX unique delivery rate | 7,998.983 packets/s |
| RX payload goodput | 1,023,869 bit/s |
| RX minimum / maximum callback gap | 106 / 1,506 µs |
| TX maximum observed slot lateness | 12 µs |

TX received RX's summary over the radio: `remote_received = 999827`,
`remote_missing = 173`, and `remote_completed = true`. Both boards saved result
index 0 and updated their averages (`average_count = 1`). They then automatically
negotiated the next run, ID `169677401`, and continued sending/receiving. The
`remote_summary_valid` flag becomes false when a new run begins; the retained
remote counts still describe the preceding summary.

The [saved first-run measurements](first-wireless-run.json) contain the complete
TX/RX structs, including the gap histogram. Live structs can change during
non-halting multiword reads; the figures above come from the immutable completed
entries with matching run IDs and even publication generations.

This proves automatic negotiation, nominal 8 kHz transmission, high-rate
reception, summary exchange, and automatic repetition with the current bench
placement. It does **not** establish lossless delivery: the 173 missing packets
and 1.506 ms maximum callback gap remain relevant to the mouse design. Distance,
interference, channel choice, and loss recovery require further measured tests.

A second uninterrupted run (`169677401`) also completed before the debugger
attach check: TX transmitted another 1,000,000 packets and RX received 999,983,
missing 17 (0.0017%). TX received that matching summary too. Across these two
runs, **2,000,000 were transmitted and 1,999,810 received**, with 190 missing
(0.0095%). Both boards held two completed entries and `average_count = 2`.
The [two-run results and averages](wireless-runs.json) preserve those structs.

The subsequent run (`3409687853`) was disturbed deliberately by the VS Code
attach/Pause verification. Do not use that run to characterize ordinary RF
loss. After detaching, a non-halting ten-second health check confirmed both
boards remained in RUNNING with the same run ID and advancing packet counters.
Their earlier completed results remained intact, and the diagnostic servers
were stopped without resetting either board.

## VS Code debugger

The actual TX launch configuration was exercised through an isolated VS Code
extension-test window against the already-flashed image. It successfully:

- Started the selected-probe OpenOCD server.
- Stopped at `main()` and a main-loop source breakpoint with breakpoint events.
- Confirmed radio/timer initialization returned zero.
- Resumed, paused, and expanded the link, live benchmark, average, and metadata
  structures through the same debugger interface used by Watch.
- Confirmed the results array contains 25 elements and disconnected cleanly.

The GDB setup explicitly uses `set mem inaccessible-by-default off` before
declaring the read-only code region. Otherwise GDB treats RAM outside that
manually defined region as inaccessible and cannot display the structures.
Hardware breakpoints remain required. The verification window did not rerun the
build/flash task; both commands were verified separately against the same image.
The Run profiles are configured using the existing verified flash-and-run task
pattern; this check specifically exercised Debug.

After the two-board test, the actual **Attach wireless RX (preserve RAM)**
profile also passed an isolated VS Code test. It connected without a build,
flash, or reset, paused RX, expanded the existing result and average structures,
and confirmed result index 0 still contained run `3366426258`, 999,827 received,
and 173 missing. It then resumed and detached. The saved history survived.


## Live UART telemetry

UART20 EasyDMA transmit was built, programmed, and verified on both V1.0
boards. The SAMD11 USB CDC bridges produced readable lines at **115200 8N1**
with DTR asserted, using `scripts/monitor-wireless.py`. Both roles reported
link transitions, one-second counters, completed runs, and automatic repetition.
No console/log backend shares this UART. The UART completion IRQ is priority 5,
below radio, ESB events, cadence, and kernel clock work.

An earlier telemetry build took compact IRQ-locked counter snapshots on every
10 ms main-loop pass. An on/off/on comparison in that same binary measured
**20 / 0 / 20 TX busy drops** in approximately 20-second windows. The UART
stayed initialized throughout, and the debug reads/writes did not halt or reset
either processor. The [comparison data](uart-reporting-comparison.json) preserves
those measurements. Final code checks small change hints first and takes
coherent snapshots only when a status/event/result line is needed.

The final image then completed run `D2CBBD40` with reporting enabled throughout.
Only USB serial was used during this run; there were no debug memory reads,
breakpoints, or processor pauses. The [saved UART result](uart-telemetry-run.json)
contains the raw result lines, representative status, exchanged summary, and
image hashes.

| Measurement | Result |
| --- | ---: |
| TX successfully transmitted | 1,000,000 |
| TX busy / missed deadlines / data errors | 0 / 0 / 0 |
| TX scheduled opportunity rate | 8,000.301 Hz |
| TX maximum observed lateness | 14 µs |
| RX unique / missing | 970,469 / 29,531 |
| RX loss | 2.9531% |
| RX unique delivery rate | 7,764.113 packets/s |
| RX maximum callback gap | 12,381 µs |
| Observed UART submission errors / aborted lines | 0 / 0 |

TX received the matching receiver summary: 970,469 received, 29,531 missing,
and summary-valid true. Both boards then negotiated another run and continued.
The monitor closed cleanly, leaving both boards running with reporting enabled.

This validates live telemetry and removes the observed local TX-drop regression
in the final full run. It does not reproduce the earlier radio-only loss figure
of 0.0095% across two runs. RX loss also continued in the reporting-disabled
comparison window, and varied strongly between windows. Its remaining cause
has not been isolated; RF conditions, channel choice, and radio timing still
need controlled follow-up. A momentary `rx/s=8000` does not erase missing
packets elsewhere in the run or establish reliable 8 kHz mouse delivery.

| Final UART-enabled image | Flash allocation | RAM allocation |
| --- | ---: | ---: |
| TX | 120,968 bytes | 21,512 bytes |
| RX | 119,720 bytes | 21,512 bytes |

```text
TX HEX 3d50efe0634f7cfc04e48052a16d5b7b5069e6417144bc86be06a881acc862d3
RX HEX f1e83c7b3d44c08476aee32d425e8569943e6df8129d94d239898455fba0942c
```
