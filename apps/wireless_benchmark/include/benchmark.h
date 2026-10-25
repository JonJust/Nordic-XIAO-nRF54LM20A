#ifndef WIRELESS_BENCHMARK_H_
#define WIRELESS_BENCHMARK_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BENCHMARK_RESULT_CAPACITY 25U
#define BENCHMARK_PACKET_COUNT 1000000U
#define BENCHMARK_PERIOD_US 125U
#define BENCHMARK_PAYLOAD_BYTES 16U
#define BENCHMARK_LATE_THRESHOLD_US 10U
#define BENCHMARK_GAP_BUCKETS 6U

/*
 * One run's debugger-friendly summary. Counters are 64-bit so arithmetic such
 * as packet_count * 1,000,000 does not overflow a 32-bit intermediate.
 *
 * The receiver accepts increasing sequence numbers. ESB preserves packet
 * order, so an equal sequence is a duplicate and an older sequence is counted
 * as out-of-order/late. This deliberately avoids a million-packet bitmap.
 *
 * Time values are LOCAL software callback timestamps in microseconds, not
 * synchronized clocks or hardware measurements of radio airtime/latency.
 */
struct benchmark_result {
	uint32_t run_id;
	uint32_t expected_packets;
	uint32_t first_sequence;
	uint32_t last_sequence;
	uint8_t transmitter;
	uint8_t completed;
	uint16_t reserved;
	uint64_t first_activity_us;
	uint64_t last_activity_us;
	uint64_t elapsed_us;

	uint64_t tx_scheduled_slots;
	uint64_t tx_submitted_packets;
	uint64_t tx_busy_drops;
	uint64_t tx_submit_errors;
	uint64_t tx_successes;
	uint64_t tx_failures;
	uint64_t tx_missed_deadlines;
	uint64_t tx_late_slots;
	uint64_t tx_lateness_sum_us;
	uint64_t tx_lateness_max_us;

	uint64_t rx_unique_packets;
	uint64_t rx_duplicate_packets;
	uint64_t rx_out_of_order_packets;
	uint64_t rx_out_of_range_packets;
	/* Live: missing through last_sequence; finished: includes the entire tail. */
	uint64_t rx_missing_packets;
	uint64_t rx_gap_count;
	uint64_t rx_gap_min_us;
	uint64_t rx_gap_max_us;
	uint64_t rx_gap_sum_us;
	/* Buckets: <=125, <=150, <=250, <=500, <=1000, and >1000 us. */
	uint64_t rx_gap_histogram[BENCHMARK_GAP_BUCKETS];

	/*
	 * Cadence is (timestamped_samples - 1) / elapsed: TX opportunities,
	 * including skipped slots, or RX accepted unique packets. millihertz
	 * preserves three decimal places without floating-point code in IRQs.
	 * TX bulk skips use their observation time, so RX cadence is the useful
	 * delivered-packet measurement, especially after a debugger pause.
	 */
	uint64_t packet_rate_millihz;
	/* RX payload goodput excludes the first sample's unobserved interval. */
	uint64_t payload_goodput_bps;
};

/*
 * Result publication uses an odd/even generation: an odd value means finish
 * is publishing; an even value means its result/average/metadata are ready.
 * result_generation[index] == 0 means that ring entry has never been used.
 * Otherwise it is the monotonically increasing recorded-run number.
 */
struct benchmark_metadata {
	uint32_t publication_generation;
	uint32_t write_index;
	uint32_t valid_results;
	uint32_t running;
	uint64_t runs_recorded;
	uint64_t runs_completed;
	uint64_t average_count;
	uint64_t result_generation[BENCHMARK_RESULT_CAPACITY];
};

/*
 * A small, coherent snapshot for periodic status reporting. Per-run counters
 * describe the current run (or its frozen result after finish). tx_total and
 * rx_total count successful data TX and accepted unique RX packets since boot;
 * they survive run changes so a one-second delta remains useful at boundaries.
 * ESB control traffic is deliberately excluded from these packet totals.
 */
struct benchmark_counters {
	uint32_t run_id;
	bool running;
	uint32_t tx_sent;
	uint32_t rx_received;
	uint32_t rx_missing;
	uint32_t tx_busy;
	uint32_t tx_missed;
	uint32_t tx_errors;
	uint64_t tx_total;
	uint64_t rx_total;
	uint64_t recorded_runs;
};

/*
 * These names intentionally remain global and volatile for VS Code/GDB.
 * The ring includes aborted runs. The average includes every completed run
 * since boot, even after its individual ring entry has been overwritten.
 *
 * Every numeric average field is an arithmetic mean of per-run fields:
 * histogram bins, per-run minima/maxima, and derived rates are averaged too.
 * It is NOT a pooled histogram, global extremum, or duration-weighted rate.
 * IDs, sequence numbers, and absolute timestamps are zero in the average;
 * transmitter is 2 if completed runs include both device roles.
 */
extern volatile struct benchmark_result
	g_benchmark_results[BENCHMARK_RESULT_CAPACITY];
extern volatile struct benchmark_result g_benchmark_average;
extern volatile struct benchmark_result g_benchmark_current;
extern volatile struct benchmark_metadata g_benchmark_metadata;

/* Start a run from thread context; expected_packets == 0 leaves it stopped. */
void benchmark_begin(uint32_t run_id, bool transmitter,
		     uint32_t expected_packets);

/* Record one scheduled TX opportunity, even when submitting it failed. */
void benchmark_tx_slot(uint32_t sequence, uint64_t now_us, int submit_result,
		       uint32_t late_us);

/*
 * Record missed opportunities in constant time; no catch-up packets are sent.
 * late_us is lateness of first_sequence at now_us; each subsequent skipped
 * opportunity is one BENCHMARK_PERIOD_US newer. The caller must report each
 * sequence exactly once, either here or through benchmark_tx_slot().
 */
void benchmark_tx_skip(uint32_t first_sequence, uint32_t count,
		       uint64_t now_us, uint32_t late_us);

/* Record ESB completion. With no ACK, success means transmitted, not received. */
void benchmark_tx_complete(bool success);

/* Record one received packet after the caller has validated its run ID. */
void benchmark_rx(uint32_t sequence, uint64_t now_us);

/*
 * Stop and publish from THREAD context after draining transport callbacks.
 * completed means the run reached its agreed end, not that delivery was
 * lossless. now_us is a fallback for a run with no timestamped activity.
 */
void benchmark_finish(bool completed, uint64_t now_us);

/* Return whether callbacks currently contribute to a run. */
bool benchmark_is_running(void);

/* Copy only compact counters under a short IRQ lock; output must be non-NULL. */
void benchmark_get_counters(struct benchmark_counters *counters);

/*
 * Copy the latest recorded run, including aborted runs, without locking IRQs
 * across the large result structure. generation is its recorded-run number.
 * Return false if no result exists or publication overlaps the copy; the caller
 * should retry on its next reporting pass instead of spinning. Both outputs
 * must be non-NULL and remain unchanged on failure.
 */
bool benchmark_get_latest_result(struct benchmark_result *result,
				 uint64_t *generation);

#ifdef __cplusplus
}
#endif

#endif /* WIRELESS_BENCHMARK_H_ */
