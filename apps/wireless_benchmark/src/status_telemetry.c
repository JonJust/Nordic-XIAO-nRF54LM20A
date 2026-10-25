#include "status_telemetry.h"

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "benchmark.h"
#include "link_state_machine.h"

#define STATUS_PERIOD_US 1000000ULL
#define STATUS_BUFFER_SIZE 512U

volatile bool g_status_telemetry_enabled = true;

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(uart20));
static const char *const role = IS_ENABLED(CONFIG_BENCHMARK_TRANSMITTER) ?
			       "TX" : "RX";
/* EasyDMA reads this RAM until TX_DONE/TX_ABORTED. Never reuse it while busy. */
static char tx_buffer[STATUS_BUFFER_SIZE];
static atomic_t tx_busy;
static atomic_t tx_aborts;
static uint32_t tx_submit_errors;
static bool banner_sent;
static bool state_reported;
static enum link_state reported_state;
static uint32_t reported_run;
static uint64_t reported_result;
static uint32_t reported_publication;
static uint64_t previous_time_us;
static uint64_t started_us;
static uint64_t previous_tx_total;
static uint64_t previous_rx_total;

/* DMA completion only releases ownership; all formatting stays in main. */
static void uart_event(const struct device *device, struct uart_event *event,
		       void *context)
{
	ARG_UNUSED(device);
	ARG_UNUSED(context);
	if (event->type == UART_TX_ABORTED) {
		atomic_inc(&tx_aborts);
	}
	if (event->type == UART_TX_DONE || event->type == UART_TX_ABORTED) {
		atomic_clear(&tx_busy);
	}
}

/* Give bounds-checked text to DMA without waiting for a host or a free UART. */
static bool send_line(int length)
{
	if (length <= 0 || (size_t)length >= sizeof(tx_buffer)) {
		tx_submit_errors++;
		return false;
	}
	/* Set ownership before submission: completion can interrupt uart_tx(). */
	atomic_set(&tx_busy, 1);
	int err = uart_tx(uart, (const uint8_t *)tx_buffer, (size_t)length,
			 SYS_FOREVER_US);

	if (err != 0) {
		atomic_clear(&tx_busy);
		tx_submit_errors++;
		return false;
	}
	return true;
}

/* Use readable names without indexing beyond the enum on a faulted device. */
static const char *state_name(enum link_state state)
{
	static const char *const names[] = {
		"BOOT", "DISCOVERING", "NEGOTIATING", "ARMED", "RUNNING",
		"DRAINING", "WAIT_SUMMARY", "PAUSE", "FAULT",
	};

	return (unsigned int)state < ARRAY_SIZE(names) ? names[state] : "UNKNOWN";
}

/* Queue an immutable, end-of-run summary before the next periodic status. */
static bool report_result(const struct benchmark_result *result,
			  uint64_t generation)
{
	int length = snprintk(tx_buffer, sizeof(tx_buffer),
		"[%s] RESULT #=%" PRIu64 " run=%08" PRIX32
		" complete=%u tx=%" PRIu64 " rx=%" PRIu64 " lost=%" PRIu64
		" busy=%" PRIu64 " missed=%" PRIu64 " errors=%" PRIu64
		" rate=%" PRIu64 ".%03" PRIu64 "/s max_gap_us=%" PRIu64
		" max_late_us=%" PRIu64 "\r\n",
		role, generation, result->run_id, (unsigned int)result->completed,
		result->tx_successes, result->rx_unique_packets,
		result->rx_missing_packets, result->tx_busy_drops,
		result->tx_missed_deadlines,
		result->tx_submit_errors + result->tx_failures,
		result->packet_rate_millihz / 1000U,
		result->packet_rate_millihz % 1000U, result->rx_gap_max_us,
		result->tx_lateness_max_us);

	return send_line(length);
}

/* Set up TX-only EasyDMA. A missing serial host never prevents radio startup. */
int status_telemetry_init(void)
{
	if (!device_is_ready(uart)) {
		return -ENODEV;
	}
	previous_time_us = k_cyc_to_us_floor64(k_cycle_get_64());
	/* The hardware GRTC counter can survive a core reset. Display time since
	 * this application initialized instead of that counter's absolute value.
	 */
	started_us = previous_time_us;
	return uart_callback_set(uart, uart_event, NULL);
}

/* Report at most one DMA frame per pass, with no queues or blocking calls. */
void status_telemetry_process(void)
{
	struct link_debug_status link;
	struct benchmark_counters counters;
	uint64_t now_us;
	int length;

	if (!g_status_telemetry_enabled || atomic_get(&tx_busy)) {
		return;
	}
	now_us = k_cyc_to_us_floor64(k_cycle_get_64());
	/* Most 10 ms passes need no report. These atomic-width volatile reads
	 * are only change hints; the coherent snapshots below supply the text.
	 * This avoids masking radio interrupts 100 times/second just to discover
	 * unchanged status. An ISR transition after this check is seen next pass.
	 */
	if (banner_sent && state_reported &&
	    g_link_status.state == reported_state &&
	    g_link_status.run_id == reported_run &&
	    g_benchmark_metadata.publication_generation == reported_publication &&
	    now_us - previous_time_us < STATUS_PERIOD_US) {
		return;
	}
	link_state_machine_get_status(&link);
	benchmark_get_counters(&counters);

	if (!banner_sent) {
		length = snprintk(tx_buffer, sizeof(tx_buffer),
			"[%s] XIAO V1 UART20 115200 8N1; ESB 4M; period_us=%u; "
			"status_ms=1000; lost=sequence gaps; TX success != delivery\r\n",
			role, BENCHMARK_PERIOD_US);
		banner_sent = send_line(length);
		return;
	}
	if (counters.recorded_runs > reported_result) {
		struct benchmark_result result;
		uint64_t generation;

		/* The large result is copied without an IRQ lock. A publication
		 * overlap is retried on the next main pass, never in a busy loop.
		 */
		if (benchmark_get_latest_result(&result, &generation) &&
		    report_result(&result, generation)) {
			reported_result = generation;
			/* Only main publishes results, so this generation cannot
			 * advance between the result snapshot and this assignment.
			 */
			reported_publication =
				g_benchmark_metadata.publication_generation;
		}
		return;
	}
	if (!state_reported || link.state != reported_state ||
	    link.run_id != reported_run) {
		length = snprintk(tx_buffer, sizeof(tx_buffer),
			"[%s] LINK state=%s run=%08" PRIX32 " error=%d reconnects=%"
			PRIu32 " peer_last_rx=%" PRIu32 " peer_last_lost=%" PRIu32
			" peer_summary_valid=%u\r\n", role, state_name(link.state),
			link.run_id, link.last_error, link.reconnects,
			link.remote_received, link.remote_missing,
			(unsigned int)link.remote_summary_valid);
		if (send_line(length)) {
			state_reported = true;
			reported_state = link.state;
			reported_run = link.run_id;
		}
		return;
	}
	uint64_t elapsed_us = now_us - previous_time_us;

	if (elapsed_us < STATUS_PERIOD_US) {
		return;
	}
	/* Lifetime totals never reset at a run boundary. Divide by the actual
	 * sample window: the 10 ms main loop is intentionally not a precise clock.
	 * These rates count data only, excluding HELLO/ACK/FINISH control traffic.
	 */
	uint64_t tx_rate = (counters.tx_total - previous_tx_total) * 1000000ULL /
			   elapsed_us;
	uint64_t rx_rate = (counters.rx_total - previous_rx_total) * 1000000ULL /
			   elapsed_us;

	length = snprintk(tx_buffer, sizeof(tx_buffer),
		"[%s] t=%" PRIu64 "s state=%s run=%08" PRIX32
		" window_ms=%" PRIu64 " tx/s=%" PRIu64 " rx/s=%" PRIu64
		" tx=%" PRIu32 " rx=%" PRIu32 " lost=%" PRIu32
		" busy=%" PRIu32 " missed=%" PRIu32 " err=%" PRIu32
		" rejected=%" PRIu32 " ctrl_fail=%" PRIu32 " ctrl_overflow=%"
		PRIu32 " uart_err=%" PRIu32 " uart_abort=%ld\r\n",
		role, (now_us - started_us) / 1000000ULL, state_name(link.state), link.run_id,
		elapsed_us / 1000U, tx_rate, rx_rate, counters.tx_sent,
		counters.rx_received, counters.rx_missing, counters.tx_busy,
		counters.tx_missed, counters.tx_errors, link.rejected_packets,
		link.control_tx_failures, link.control_queue_overflows,
		tx_submit_errors, (long)atomic_get(&tx_aborts));
	if (send_line(length)) {
		previous_time_us = now_us;
		previous_tx_total = counters.tx_total;
		previous_rx_total = counters.rx_total;
	}
}
