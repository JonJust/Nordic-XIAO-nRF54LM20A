#include "benchmark.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

/* Before the first run, snapshots must be empty and no result may be invented. */
static void test_empty_snapshots(void)
{
	struct benchmark_counters counters;
	struct benchmark_result result = { .run_id = 123U };
	uint64_t generation = 456U;

	/* Inactive callbacks are excluded from both current and lifetime counts. */
	benchmark_rx(0U, 0U);
	benchmark_tx_complete(true);
	benchmark_get_counters(&counters);
	assert(!counters.running);
	assert(counters.tx_total == 0U);
	assert(counters.rx_total == 0U);
	assert(counters.recorded_runs == 0U);
	assert(!benchmark_get_latest_result(&result, &generation));
	assert(result.run_id == 123U);
	assert(generation == 456U);
}

/* Check losses at both boundaries, gap buckets, and duplicate classification. */
static void test_receiver_summary(void)
{
	struct benchmark_counters counters;
	struct benchmark_result latest;
	uint64_t generation;

	benchmark_begin(41U, false, BENCHMARK_PACKET_COUNT);
	assert(benchmark_is_running());
	benchmark_rx(2U, 100U);
	benchmark_rx(3U, 225U);
	benchmark_rx(7U, 375U);
	benchmark_rx(8U, 625U);
	benchmark_rx(9U, 1126U);
	benchmark_rx(9U, 1200U);
	benchmark_rx(4U, 1300U);
	benchmark_rx(BENCHMARK_PACKET_COUNT, 1400U);
	benchmark_get_counters(&counters);
	assert(counters.run_id == 41U);
	assert(counters.running);
	assert(counters.rx_received == 5U);
	assert(counters.rx_missing == 5U);
	/* Duplicate, older, and rejected out-of-range data do not count as RX. */
	assert(counters.rx_total == 5U);
	assert(counters.tx_sent == 0U);
	assert(counters.tx_total == 0U);
	benchmark_finish(true, 2000U);

	assert(!benchmark_is_running());
	assert(g_benchmark_results[0].run_id == 41U);
	assert(g_benchmark_results[0].rx_unique_packets == 5U);
	assert(g_benchmark_results[0].rx_missing_packets == 999995U);
	assert(g_benchmark_results[0].rx_duplicate_packets == 1U);
	assert(g_benchmark_results[0].rx_out_of_order_packets == 1U);
	assert(g_benchmark_results[0].rx_out_of_range_packets == 1U);
	assert(g_benchmark_results[0].first_sequence == 2U);
	assert(g_benchmark_results[0].last_sequence == 9U);
	assert(g_benchmark_results[0].rx_gap_count == 4U);
	assert(g_benchmark_results[0].rx_gap_min_us == 125U);
	assert(g_benchmark_results[0].rx_gap_max_us == 501U);
	assert(g_benchmark_results[0].rx_gap_sum_us == 1026U);
	assert(g_benchmark_results[0].rx_gap_histogram[0] == 1U);
	assert(g_benchmark_results[0].rx_gap_histogram[1] == 1U);
	assert(g_benchmark_results[0].rx_gap_histogram[2] == 1U);
	assert(g_benchmark_results[0].rx_gap_histogram[3] == 0U);
	assert(g_benchmark_results[0].rx_gap_histogram[4] == 1U);
	assert(g_benchmark_results[0].rx_gap_histogram[5] == 0U);
	assert(g_benchmark_results[0].elapsed_us == 1026U);
	assert(g_benchmark_results[0].packet_rate_millihz == 3898635U);
	assert(g_benchmark_results[0].payload_goodput_bps == 499025U);
	assert(g_benchmark_average.run_id == 0U);
	assert(g_benchmark_average.first_activity_us == 0U);
	assert(g_benchmark_metadata.average_count == 1U);
	assert(g_benchmark_metadata.result_generation[0] == 1U);
	benchmark_get_counters(&counters);
	assert(!counters.running);
	assert(counters.rx_missing == 999995U);
	assert(counters.recorded_runs == 1U);
	assert(benchmark_get_latest_result(&latest, &generation));
	assert(generation == 1U);
	assert(latest.run_id == 41U);
	assert(latest.rx_unique_packets == 5U);

	/* A publication in progress must leave the caller's previous result alone. */
	g_benchmark_metadata.publication_generation++;
	assert(!benchmark_get_latest_result(&latest, &generation));
	assert(latest.run_id == 41U);
	assert(generation == 1U);
	g_benchmark_metadata.publication_generation++;
}

/* An empty aborted run must finish, count all loss, and leave the mean alone. */
static void test_empty_abort(void)
{
	struct benchmark_counters counters;
	struct benchmark_result latest;
	uint64_t generation;

	benchmark_begin(42U, false, 100U);
	benchmark_get_counters(&counters);
	assert(counters.run_id == 42U);
	assert(counters.rx_received == 0U);
	assert(counters.rx_total == 5U);
	/* Starting another run must not replace the most recently saved result. */
	assert(benchmark_get_latest_result(&latest, &generation));
	assert(latest.run_id == 41U);
	assert(generation == 1U);
	benchmark_finish(false, UINT64_C(5000000000));
	assert(g_benchmark_results[1].completed == 0U);
	assert(g_benchmark_results[1].rx_missing_packets == 100U);
	assert(g_benchmark_results[1].elapsed_us == 0U);
	assert(g_benchmark_results[1].packet_rate_millihz == 0U);
	assert(g_benchmark_results[1].first_activity_us == UINT64_C(5000000000));
	assert(g_benchmark_metadata.runs_recorded == 2U);
	assert(g_benchmark_metadata.average_count == 1U);
	/* An accidental duplicate finish must not publish a second copy. */
	benchmark_finish(false, UINT64_C(5000000001));
	assert(g_benchmark_metadata.runs_recorded == 2U);
	assert(benchmark_get_latest_result(&latest, &generation));
	assert(latest.run_id == 42U);
	assert(!latest.completed);
	assert(generation == 2U);
}

/* FIFO refusal and deliberately skipped deadlines are different failure modes. */
static void test_transmitter_summary(void)
{
	struct benchmark_counters counters;

	benchmark_begin(43U, true, 10U);
	benchmark_tx_slot(0U, 1000U, 0, 0U);
	benchmark_tx_slot(1U, 1125U, -ENOMEM, 5U);
	benchmark_tx_skip(2U, 3U, 1800U, 550U);
	benchmark_tx_slot(5U, 1875U, -EINVAL, 11U);
	benchmark_tx_slot(6U, 2000U, 0, 10U);
	benchmark_tx_complete(true);
	benchmark_tx_complete(true);
	benchmark_tx_complete(false);
	benchmark_tx_skip(7U, 100U, 2375U, 20U);
	benchmark_get_counters(&counters);
	assert(counters.running);
	assert(counters.tx_sent == 2U);
	assert(counters.tx_total == 2U);
	assert(counters.tx_busy == 1U);
	assert(counters.tx_missed == 6U);
	assert(counters.tx_errors == 2U);
	assert(counters.rx_received == 0U);
	assert(counters.rx_total == 5U);
	benchmark_finish(true, 3000U);

	assert(g_benchmark_results[2].tx_scheduled_slots == 10U);
	assert(g_benchmark_results[2].tx_submitted_packets == 2U);
	assert(g_benchmark_results[2].tx_busy_drops == 1U);
	assert(g_benchmark_results[2].tx_submit_errors == 1U);
	assert(g_benchmark_results[2].tx_successes == 2U);
	assert(g_benchmark_results[2].tx_failures == 1U);
	assert(g_benchmark_results[2].tx_missed_deadlines == 6U);
	assert(g_benchmark_results[2].tx_late_slots == 5U);
	assert(g_benchmark_results[2].tx_lateness_sum_us == 1321U);
	assert(g_benchmark_results[2].tx_lateness_max_us == 550U);
	assert(g_benchmark_results[2].elapsed_us == 1375U);
	assert(g_benchmark_results[2].packet_rate_millihz == 6545454U);
	assert(g_benchmark_results[2].payload_goodput_bps == 0U);
	assert(g_benchmark_results[2].first_sequence == 0U);
	assert(g_benchmark_results[2].last_sequence == 9U);
	assert(g_benchmark_metadata.average_count == 2U);
	assert(g_benchmark_average.transmitter == 2U);
	assert(g_benchmark_average.rx_unique_packets == 2U);
	/* After stopping, late transport callbacks cannot mutate a frozen run. */
	benchmark_tx_complete(false);
	benchmark_tx_complete(true);
	assert(g_benchmark_current.tx_failures == 1U);
	benchmark_get_counters(&counters);
	assert(counters.tx_total == 2U);
	assert(counters.recorded_runs == 3U);
}

/* Ring overwrite must preserve the exact all-history mean, not a recursive one. */
static void test_history_and_average(void)
{
	uint64_t expected_total = 1000000U + 10U;
	struct benchmark_counters counters;
	struct benchmark_result latest;
	uint64_t generation;

	for (uint32_t index = 0U; index < 30U; index++) {
		uint32_t count = index + 1U;

		benchmark_begin(100U + index, false, count);
		benchmark_rx(0U, 0U);
		benchmark_finish(true, 0U);
		expected_total += count;
	}
	assert(g_benchmark_metadata.valid_results == BENCHMARK_RESULT_CAPACITY);
	assert(g_benchmark_metadata.runs_recorded == 33U);
	assert(g_benchmark_metadata.runs_completed == 32U);
	assert(g_benchmark_metadata.average_count == 32U);
	assert(g_benchmark_metadata.write_index == 8U);
	assert(g_benchmark_metadata.result_generation[7] == 33U);
	assert(g_benchmark_results[7].run_id == 129U);
	assert(g_benchmark_average.expected_packets == expected_total / 32U);
	assert((g_benchmark_metadata.publication_generation & 1U) == 0U);
	assert(benchmark_get_latest_result(&latest, &generation));
	assert(latest.run_id == 129U);
	assert(generation == 33U);
	assert(latest.rx_unique_packets == 1U);
	benchmark_begin(999U, false, 0U);
	assert(!benchmark_is_running());
	benchmark_get_counters(&counters);
	assert(counters.run_id == 999U);
	assert(counters.rx_received == 0U);
	assert(counters.rx_total == 35U);
	assert(counters.tx_total == 2U);
	assert(counters.recorded_runs == 33U);
}

/* Exercise summaries through their public API with a tiny host Zephyr stub. */
int main(void)
{
	test_empty_snapshots();
	test_receiver_summary();
	test_empty_abort();
	test_transmitter_summary();
	test_history_and_average();
	puts("benchmark statistics: PASS");
	return 0;
}
