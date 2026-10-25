#include "benchmark.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/irq.h>

volatile struct benchmark_result g_benchmark_results[BENCHMARK_RESULT_CAPACITY];
volatile struct benchmark_result g_benchmark_average;
volatile struct benchmark_result g_benchmark_current;
volatile struct benchmark_metadata g_benchmark_metadata;

/*
 * A separate exact sum prevents recursive integer averaging from steadily
 * rounding the running average toward zero. All divisions happen at finish,
 * outside the time-sensitive radio/timer callbacks.
 */
#define AVERAGED_FIELDS(X)                   \
	X(expected_packets)                  \
	X(elapsed_us)                        \
	X(tx_scheduled_slots)                \
	X(tx_submitted_packets)              \
	X(tx_busy_drops)                     \
	X(tx_submit_errors)                  \
	X(tx_successes)                      \
	X(tx_failures)                       \
	X(tx_missed_deadlines)               \
	X(tx_late_slots)                     \
	X(tx_lateness_sum_us)                \
	X(tx_lateness_max_us)                \
	X(rx_unique_packets)                \
	X(rx_duplicate_packets)             \
	X(rx_out_of_order_packets)          \
	X(rx_out_of_range_packets)          \
	X(rx_missing_packets)               \
	X(rx_gap_count)                     \
	X(rx_gap_min_us)                    \
	X(rx_gap_max_us)                    \
	X(rx_gap_sum_us)                    \
	X(packet_rate_millihz)              \
	X(payload_goodput_bps)

struct benchmark_totals {
#define DECLARE_TOTAL(field) uint64_t field;
	AVERAGED_FIELDS(DECLARE_TOTAL)
#undef DECLARE_TOTAL
	uint64_t rx_gap_histogram[BENCHMARK_GAP_BUCKETS];
};

static struct benchmark_totals average_totals;
static bool have_activity;
static uint8_t averaged_role;
/* These boot-lifetime totals cannot jump backward when begin resets a run. */
static uint64_t lifetime_tx_successes;
static uint64_t lifetime_rx_unique_packets;

/* Identify one inter-arrival bucket without division or floating point. */
static size_t gap_bucket(uint64_t gap_us)
{
	if (gap_us <= 125U) {
		return 0U;
	}
	if (gap_us <= 150U) {
		return 1U;
	}
	if (gap_us <= 250U) {
		return 2U;
	}
	if (gap_us <= 500U) {
		return 3U;
	}
	if (gap_us <= 1000U) {
		return 4U;
	}
	return 5U;
}

/* Update sample bounds while the caller holds the short IRQ critical section. */
static void record_activity(uint32_t sequence, uint64_t now_us)
{
	if (!have_activity) {
		g_benchmark_current.first_activity_us = now_us;
		g_benchmark_current.first_sequence = sequence;
		have_activity = true;
	}
	g_benchmark_current.last_activity_us = now_us;
	g_benchmark_current.last_sequence = sequence;
	g_benchmark_current.elapsed_us = now_us - g_benchmark_current.first_activity_us;
}

/* Accumulate one slot's scheduling jitter while interrupts are locked. */
static void record_lateness(uint32_t late_us)
{
	g_benchmark_current.tx_lateness_sum_us += late_us;
	if (late_us > g_benchmark_current.tx_lateness_max_us) {
		g_benchmark_current.tx_lateness_max_us = late_us;
	}
	if (late_us > BENCHMARK_LATE_THRESHOLD_US) {
		g_benchmark_current.tx_late_slots++;
	}
}

/* Start with a fresh live summary; history and its all-run average survive. */
void benchmark_begin(uint32_t run_id, bool transmitter,
		     uint32_t expected_packets)
{
	struct benchmark_result fresh = {
		.run_id = run_id,
		.expected_packets = expected_packets,
		.transmitter = transmitter ? 1U : 0U,
	};
	unsigned int key = irq_lock();

	g_benchmark_current = fresh;
	have_activity = false;
	g_benchmark_metadata.running = expected_packets != 0U;
	irq_unlock(key);
}

/* Count an attempted opportunity separately from an accepted ESB submission. */
void benchmark_tx_slot(uint32_t sequence, uint64_t now_us, int submit_result,
		       uint32_t late_us)
{
	unsigned int key = irq_lock();

	if (!g_benchmark_metadata.running || !g_benchmark_current.transmitter) {
		irq_unlock(key);
		return;
	}
	if (sequence >= g_benchmark_current.expected_packets) {
		g_benchmark_current.tx_submit_errors++;
		irq_unlock(key);
		return;
	}
	record_activity(sequence, now_us);
	g_benchmark_current.tx_scheduled_slots++;
	record_lateness(late_us);
	if (submit_result == 0) {
		g_benchmark_current.tx_submitted_packets++;
	} else if (submit_result == -EBUSY || submit_result == -ENOBUFS ||
		   submit_result == -EAGAIN || submit_result == -ENOMEM) {
		/* Nordic ESB reports a full software FIFO as -ENOMEM. */
		g_benchmark_current.tx_busy_drops++;
	} else {
		g_benchmark_current.tx_submit_errors++;
	}
	irq_unlock(key);
}

/* Summarize a missed block arithmetically, rather than looping in a timer IRQ. */
void benchmark_tx_skip(uint32_t first_sequence, uint32_t count,
		       uint64_t now_us, uint32_t late_us)
{
	unsigned int key = irq_lock();
	uint64_t positive_count;
	uint64_t late_count;
	uint64_t lateness_sum;
	uint32_t remaining;

	if (!g_benchmark_metadata.running || !g_benchmark_current.transmitter ||
	    count == 0U || first_sequence >= g_benchmark_current.expected_packets) {
		irq_unlock(key);
		return;
	}
	remaining = g_benchmark_current.expected_packets - first_sequence;
	if (count > remaining) {
		count = remaining;
	}

	/*
	 * Lateness is max(late_us - index * period, 0). Sum the positive
	 * arithmetic progression, using 64-bit products even for long pauses.
	 */
	positive_count = ((uint64_t)late_us + BENCHMARK_PERIOD_US - 1U) /
			 BENCHMARK_PERIOD_US;
	if (positive_count > count) {
		positive_count = count;
	}
	lateness_sum = positive_count * late_us;
	if (positive_count > 0U) {
		lateness_sum -= BENCHMARK_PERIOD_US * positive_count *
				(positive_count - 1U) / 2U;
	}
	late_count = late_us > BENCHMARK_LATE_THRESHOLD_US ?
		((uint64_t)late_us - BENCHMARK_LATE_THRESHOLD_US +
		 BENCHMARK_PERIOD_US - 1U) / BENCHMARK_PERIOD_US : 0U;
	if (late_count > count) {
		late_count = count;
	}
	if (!have_activity) {
		/* The first observed block still identifies its earliest sequence. */
		record_activity(first_sequence, now_us);
	}
	record_activity(first_sequence + count - 1U, now_us);
	g_benchmark_current.tx_scheduled_slots += count;
	g_benchmark_current.tx_missed_deadlines += count;
	g_benchmark_current.tx_late_slots += late_count;
	g_benchmark_current.tx_lateness_sum_us += lateness_sum;
	if (late_us > g_benchmark_current.tx_lateness_max_us) {
		g_benchmark_current.tx_lateness_max_us = late_us;
	}
	irq_unlock(key);
}

/* ESB no-ACK success proves only that a packet left this transmitter. */
void benchmark_tx_complete(bool success)
{
	unsigned int key = irq_lock();

	if (g_benchmark_metadata.running && g_benchmark_current.transmitter) {
		if (success) {
			g_benchmark_current.tx_successes++;
			lifetime_tx_successes++;
		} else {
			g_benchmark_current.tx_failures++;
		}
	}
	irq_unlock(key);
}

/* Count ordered unique deliveries and their local callback-to-callback gaps. */
void benchmark_rx(uint32_t sequence, uint64_t now_us)
{
	unsigned int key = irq_lock();
	uint64_t gap_us;

	if (!g_benchmark_metadata.running || g_benchmark_current.transmitter) {
		irq_unlock(key);
		return;
	}
	if (sequence >= g_benchmark_current.expected_packets) {
		g_benchmark_current.rx_out_of_range_packets++;
		irq_unlock(key);
		return;
	}
	if (have_activity && sequence <= g_benchmark_current.last_sequence) {
		if (sequence == g_benchmark_current.last_sequence) {
			g_benchmark_current.rx_duplicate_packets++;
		} else {
			/* Older retransmissions cannot be distinguished from reordering. */
			g_benchmark_current.rx_out_of_order_packets++;
		}
		irq_unlock(key);
		return;
	}
	if (have_activity) {
		gap_us = now_us - g_benchmark_current.last_activity_us;
		if (g_benchmark_current.rx_gap_count == 0U ||
		    gap_us < g_benchmark_current.rx_gap_min_us) {
			g_benchmark_current.rx_gap_min_us = gap_us;
		}
		if (gap_us > g_benchmark_current.rx_gap_max_us) {
			g_benchmark_current.rx_gap_max_us = gap_us;
		}
		g_benchmark_current.rx_gap_count++;
		g_benchmark_current.rx_gap_sum_us += gap_us;
		g_benchmark_current.rx_gap_histogram[gap_bucket(gap_us)]++;
	}
	record_activity(sequence, now_us);
	g_benchmark_current.rx_unique_packets++;
	lifetime_rx_unique_packets++;
	/* During a run only losses up through the newest received sequence are
	 * knowable. finish() adds the unreceived tail of the agreed packet count.
	 */
	g_benchmark_current.rx_missing_packets = (uint64_t)sequence + 1U -
						g_benchmark_current.rx_unique_packets;
	irq_unlock(key);
}

/* Derive rates once per run, keeping integer division outside radio IRQs. */
static void finalize_result(struct benchmark_result *result)
{
	uint64_t samples = result->transmitter ? result->tx_scheduled_slots :
						 result->rx_unique_packets;

	result->elapsed_us = result->last_activity_us - result->first_activity_us;
	if (!result->transmitter) {
		/* Includes losses before the first and after the last received packet. */
		result->rx_missing_packets = result->expected_packets -
					    result->rx_unique_packets;
	}
	if (samples > 1U && result->elapsed_us > 0U) {
		/* samples is bounded by the 32-bit expected count, so these fit. */
		result->packet_rate_millihz = (samples - 1U) * UINT64_C(1000000000) /
					    result->elapsed_us;
		if (!result->transmitter) {
			result->payload_goodput_bps = (samples - 1U) *
				BENCHMARK_PAYLOAD_BYTES * 8U * UINT64_C(1000000) /
				result->elapsed_us;
		}
	}
}

/* Add exact totals and return the arithmetic mean of completed run summaries. */
static struct benchmark_result update_average(const struct benchmark_result *result,
					     uint64_t count)
{
	struct benchmark_result average = { .completed = 1U };

	if (count == 1U) {
		averaged_role = result->transmitter;
	} else if (averaged_role != result->transmitter) {
		averaged_role = 2U;
	}
	average.transmitter = averaged_role;
#define ADD_AND_AVERAGE(field)                       \
	average_totals.field += result->field;        \
	average.field = average_totals.field / count;
	AVERAGED_FIELDS(ADD_AND_AVERAGE)
#undef ADD_AND_AVERAGE
	for (size_t index = 0U; index < BENCHMARK_GAP_BUCKETS; index++) {
		average_totals.rx_gap_histogram[index] += result->rx_gap_histogram[index];
		average.rx_gap_histogram[index] =
			average_totals.rx_gap_histogram[index] / count;
	}
	return average;
}

/* Freeze live counters, then publish one ring entry and its all-history mean. */
void benchmark_finish(bool completed, uint64_t now_us)
{
	struct benchmark_result result;
	struct benchmark_result average;
	uint64_t recorded_runs;
	uint64_t completed_runs;
	uint32_t index;
	unsigned int key = irq_lock();

	if (!g_benchmark_metadata.running) {
		irq_unlock(key);
		return;
	}
	g_benchmark_metadata.running = false;
	result = g_benchmark_current;
	if (!have_activity) {
		result.first_activity_us = now_us;
		result.last_activity_us = now_us;
	}
	irq_unlock(key);

	result.completed = completed ? 1U : 0U;
	finalize_result(&result);
	recorded_runs = g_benchmark_metadata.runs_recorded + 1U;
	completed_runs = g_benchmark_metadata.runs_completed + (completed ? 1U : 0U);
	if (completed) {
		average = update_average(&result, completed_runs);
	}

	/*
	 * No callbacks can mutate this stopped run. Publication may copy with
	 * interrupts enabled; debugger consumers check for an even generation
	 * before interpreting a snapshot taken inside this function.
	 */
	g_benchmark_metadata.publication_generation++;
	index = g_benchmark_metadata.write_index;
	g_benchmark_metadata.result_generation[index] = 0U;
	g_benchmark_results[index] = result;
	g_benchmark_current = result;
	if (completed) {
		g_benchmark_average = average;
	}
	g_benchmark_metadata.result_generation[index] = recorded_runs;
	/* The compact telemetry snapshot also reads this 64-bit value on a 32-bit
	 * CPU. Protect this small store so a reporting thread cannot see half of it.
	 */
	key = irq_lock();
	g_benchmark_metadata.runs_recorded = recorded_runs;
	irq_unlock(key);
	g_benchmark_metadata.runs_completed = completed_runs;
	g_benchmark_metadata.average_count = completed_runs;
	g_benchmark_metadata.write_index = (index + 1U) % BENCHMARK_RESULT_CAPACITY;
	if (g_benchmark_metadata.valid_results < BENCHMARK_RESULT_CAPACITY) {
		g_benchmark_metadata.valid_results++;
	}
	g_benchmark_metadata.publication_generation++;
}

/* Read the shared running flag consistently on the single application CPU. */
bool benchmark_is_running(void)
{
	unsigned int key = irq_lock();
	bool running = g_benchmark_metadata.running != 0U;

	irq_unlock(key);
	return running;
}

/* Keep status reporting out of the large per-run summary's IRQ-locked copy. */
void benchmark_get_counters(struct benchmark_counters *counters)
{
	unsigned int key = irq_lock();

	counters->run_id = g_benchmark_current.run_id;
	counters->running = g_benchmark_metadata.running != 0U;
	/* Each normal run has at most one million data slots/packets, so its
	 * counters fit in 32 bits. Lifetime totals remain 64-bit across runs.
	 */
	counters->tx_sent = (uint32_t)g_benchmark_current.tx_successes;
	counters->rx_received = (uint32_t)g_benchmark_current.rx_unique_packets;
	counters->rx_missing = (uint32_t)g_benchmark_current.rx_missing_packets;
	counters->tx_busy = (uint32_t)g_benchmark_current.tx_busy_drops;
	counters->tx_missed = (uint32_t)g_benchmark_current.tx_missed_deadlines;
	counters->tx_errors = (uint32_t)(g_benchmark_current.tx_submit_errors +
					 g_benchmark_current.tx_failures);
	counters->tx_total = lifetime_tx_successes;
	counters->rx_total = lifetime_rx_unique_packets;
	counters->recorded_runs = g_benchmark_metadata.runs_recorded;
	irq_unlock(key);
}

/* Use the publication generation to reject a large copy interrupted by finish. */
bool benchmark_get_latest_result(struct benchmark_result *result,
				 uint64_t *generation)
{
	struct benchmark_result snapshot;
	uint64_t recorded_generation;
	uint32_t before = g_benchmark_metadata.publication_generation;
	uint32_t index;
	uint32_t after;

	if ((before & 1U) != 0U || g_benchmark_metadata.valid_results == 0U) {
		return false;
	}
	index = (g_benchmark_metadata.write_index + BENCHMARK_RESULT_CAPACITY - 1U) %
		BENCHMARK_RESULT_CAPACITY;
	snapshot = g_benchmark_results[index];
	recorded_generation = g_benchmark_metadata.result_generation[index];
	after = g_benchmark_metadata.publication_generation;
	/* All shared publication fields are volatile. If a publisher ran during
	 * this interruptible copy, ignore even a plausible-looking partial result.
	 */
	if (before != after || (after & 1U) != 0U || recorded_generation == 0U) {
		return false;
	}
	*result = snapshot;
	*generation = recorded_generation;
	return true;
}
