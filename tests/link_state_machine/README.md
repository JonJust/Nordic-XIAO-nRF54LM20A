# Link state machine host checks

These deterministic event tests compile the production state machine and
benchmark statistics, using small functional substitutes for the clock, timer,
control queue, byte-order helpers, and transport boundary. They exercise both
fixed radio roles without emulating Nordic RF hardware.

From the repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror -DCONFIG_BENCHMARK_TRANSMITTER=1 \
  -Itests/link_state_machine/stubs -Iapps/wireless_benchmark/include \
  tests/link_state_machine/test_link_state_machine.c \
  apps/wireless_benchmark/src/benchmark.c -o /tmp/link-state-machine-ptx-test
/tmp/link-state-machine-ptx-test
cc -std=c11 -Wall -Wextra -Werror -DCONFIG_BENCHMARK_TRANSMITTER=0 \
  -Itests/link_state_machine/stubs -Iapps/wireless_benchmark/include \
  tests/link_state_machine/test_link_state_machine.c \
  apps/wireless_benchmark/src/benchmark.c -o /tmp/link-state-machine-prx-test
/tmp/link-state-machine-prx-test
```

Coverage includes HELLO/READY/START/ARMED negotiation, the startup guard, no-ACK
mouse packets at absolute deadlines, skipping expired slots without bursts,
rearming a physical compare after an interrupt-induced delay, a stalled-TX-timer
watchdog, draining the final data packet before publication, FINISH/SUMMARY exchange,
wrong session and incompatible contract rejection, repeated START/FINISH,
leading/trailing packet loss, first-data and mid-run timeouts, and an RX ISR
timestamp newer than the thread's initial clock sample.

These tests validate control decisions and accounting. They do not establish
radio throughput, interrupt latency, or the SDK driver's hardware behavior.
