# Benchmark statistics host check

This checks the production statistics module without a board or SDK runtime:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -Itests/benchmark_stats/stubs -Iapps/wireless_benchmark/include \
  apps/wireless_benchmark/src/benchmark.c \
  tests/benchmark_stats/test_benchmark.c -o /tmp/benchmark-stats-test
/tmp/benchmark-stats-test
```

The scenarios cover leading/trailing loss, duplicate and older sequences,
inter-arrival buckets, large timestamps, aborted empty runs, TX FIFO refusal,
constant-time missed-slot accounting, and a 25-entry ring wrapping while the
all-history arithmetic mean keeps accumulating. The IRQ stub does not simulate
interrupt timing or radio hardware; those need the two-board test.

Snapshot checks also cover compact live counters, data-only lifetime totals
surviving run boundaries, rejected/duplicate packets not inflating received
counts, latest completed or aborted result selection after ring wrap, and
refusing an in-progress publication while preserving the caller's old result.
