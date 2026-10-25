#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Compile the production FSM itself. Inclusion makes its timer ISR available
 * to this single-threaded event harness without adding a firmware-only test API.
 * benchmark.c remains a separate, unmodified production translation unit.
 */
#include "../../apps/wireless_benchmark/src/link_state_machine.c"

static uint64_t fake_now_us = 1000000U;
static void (*clock_read_hook)(void);
static uint32_t fake_random = 0x12345678U;
static struct esb_transport_callbacks transport_callbacks;
static bool transport_idle = true;
static uint8_t last_send[ESB_TRANSPORT_MAX_PAYLOAD];
static size_t last_send_length;
static bool last_send_requested_ack;
static uint32_t send_count;
static uint32_t reply_count;
static uint8_t last_reply[BENCHMARK_CONTROL_SIZE];
static bool timer_enabled;
static uint32_t timer_count_us;
static uint32_t timer_compare_us;
static nrfx_timer_event_handler_t timer_handler;
static uint32_t fake_submit_duration_us;

/* Return a deterministic local clock and optionally inject an interrupt race. */
uint64_t k_cycle_get_64(void)
{
	uint64_t sampled = fake_now_us;

	if (clock_read_hook != NULL) {
		void (*hook)(void) = clock_read_hook;

		clock_read_hook = NULL;
		hook();
	}
	return sampled;
}

/* Fresh predictable IDs let the tests check stale-session rejection exactly. */
uint32_t sys_rand32_get(void)
{
	return fake_random++;
}

/* Preserve the production role and callback contract without emulating RF. */
int esb_transport_init(bool transmitter,
		       const struct esb_transport_callbacks *callbacks)
{
	assert(transmitter == (CONFIG_BENCHMARK_TRANSMITTER != 0));
	transport_callbacks = *callbacks;
	return 0;
}

/* A single outstanding packet models the transport's no-stale-data policy. */
int esb_transport_send(const uint8_t *data, size_t length, bool request_ack)
{
	if (!transport_idle) {
		return -EBUSY;
	}
	assert(length <= sizeof(last_send));
	memcpy(last_send, data, length);
	last_send_length = length;
	last_send_requested_ack = request_ack;
	transport_idle = false;
	send_count++;
	if (!request_ack && fake_submit_duration_us != 0U) {
		/* Exercise time passing inside the insertion before rearming compare. */
		timer_count_us += fake_submit_duration_us;
		fake_now_us += fake_submit_duration_us;
		fake_submit_duration_us = 0U;
	}
	return 0;
}

/* ACK replies are prequeued, so this does not immediately deliver to a peer. */
int esb_transport_receiver_queue_reply(const uint8_t *data, size_t length)
{
	assert(length == sizeof(last_reply));
	memcpy(last_reply, data, length);
	reply_count++;
	return 0;
}

/* Main must observe the outstanding final data transmission before finishing. */
bool esb_transport_is_idle(void)
{
	return transport_idle;
}

/* Expose the cleanup contract if a production transition needs it. */
int esb_transport_reset_queues(void)
{
	transport_idle = true;
	return 0;
}

/* Record the real timer callback/configuration, then drive events explicitly. */
int nrfx_timer_init(const nrfx_timer_t *timer, const nrfx_timer_config_t *config,
		    nrfx_timer_event_handler_t handler)
{
	assert(timer->instance == NRF_TIMER20);
	assert(config->frequency == 1000000U);
	assert(config->bit_width == NRF_TIMER_BIT_WIDTH_32);
	timer_handler = handler;
	return 0;
}

/* Start the functional timer model without generating an immediate event. */
void nrfx_timer_enable(const nrfx_timer_t *timer)
{
	(void)timer;
	timer_enabled = true;
}

/* A finite run must stop creating additional opportunities. */
void nrfx_timer_disable(const nrfx_timer_t *timer)
{
	(void)timer;
	timer_enabled = false;
}

/* The cadence starts from a fresh free-running 32-bit microsecond counter. */
void nrfx_timer_clear(const nrfx_timer_t *timer)
{
	(void)timer;
	timer_count_us = 0U;
}

/* Keep the armed absolute compare visible to pacing and missed-slot checks. */
void nrfx_timer_compare(const nrfx_timer_t *timer, unsigned int channel,
			uint32_t value, bool enable_interrupt)
{
	(void)timer;
	assert(channel == NRF_TIMER_CC_CHANNEL0);
	assert(enable_interrupt);
	timer_compare_us = value;
}

/* Capture the injected hardware time; the timer never resets at each slot. */
uint32_t nrfx_timer_capture(const nrfx_timer_t *timer, unsigned int channel)
{
	(void)timer;
	assert(channel == NRF_TIMER_CC_CHANNEL1);
	return timer_count_us;
}

/* Deliver a production-encoded control packet through the radio callback. */
static void receive_control(enum benchmark_packet_kind kind, uint32_t run_id)
{
	struct benchmark_control message = {.kind = kind, .run_id = run_id};
	uint8_t packet[BENCHMARK_CONTROL_SIZE];

	benchmark_control_encode(packet, &message);
	transport_callbacks.received(packet, sizeof(packet), fake_now_us);
}

/* Validate the exact contract of the most recently emitted control packet. */
static void assert_last_control(enum benchmark_packet_kind kind, bool reply)
{
	struct benchmark_control message;
	const uint8_t *packet = reply ? last_reply : last_send;

	if (!reply) {
		assert(last_send_length == BENCHMARK_CONTROL_SIZE);
		assert(last_send_requested_ack);
	}
	assert(benchmark_control_decode(packet, BENCHMARK_CONTROL_SIZE, &message));
	assert(message.kind == kind);
	assert(message.run_id == g_link_status.run_id);
}

/* A peer with another version/period/packet/PHY/ACK contract is not compatible. */
static void test_contract_rejection(void)
{
	static const unsigned int fields[] = {1U, 6U, 10U, 12U, 13U, 14U, 23U};
	struct benchmark_control message = {
		.kind = PACKET_HELLO,
		.run_id = g_link_status.run_id,
	};
	uint32_t rejected_before = g_link_status.rejected_packets;

	for (size_t index = 0U; index < sizeof(fields) / sizeof(fields[0]); index++) {
		uint8_t packet[BENCHMARK_CONTROL_SIZE];

		benchmark_control_encode(packet, &message);
		packet[fields[index]] = fields[index] == 23U ? 2U :
					     packet[fields[index]] ^ 0x40U;
		transport_callbacks.received(packet, sizeof(packet), fake_now_us);
	}
	assert(g_link_status.rejected_packets == rejected_before + 7U);
	assert(control_queue.count == 0U);
}

#if CONFIG_BENCHMARK_TRANSMITTER
/* End one pending RF transmission only when the scenario explicitly says so. */
static void complete_transmission(bool success)
{
	assert(!transport_idle);
	transport_idle = true;
	transport_callbacks.transmitted(success);
}

/* Deliver one timer compare at its actual counter value, optionally very late. */
static void fire_timer(uint32_t counter_us, uint64_t run_start_us)
{
	assert(timer_enabled);
	timer_count_us = counter_us;
	fake_now_us = run_start_us + counter_us;
	timer_handler(NRF_TIMER_EVENT_COMPARE0, NULL);
}

/* A complete transmitter run includes negotiation, guard, paced data and drain. */
static void test_transmitter_flow(void)
{
	assert(link_state_machine_init() == 0);
	assert(g_link_status.state == LINK_DISCOVERING);
	test_contract_rejection();
	link_state_machine_process();
	assert_last_control(PACKET_HELLO, false);
	complete_transmission(true);

	receive_control(PACKET_READY, g_link_status.run_id + 1U);
	link_state_machine_process();
	assert(g_link_status.state == LINK_DISCOVERING);
	receive_control(PACKET_READY, g_link_status.run_id);
	link_state_machine_process();
	assert(g_link_status.state == LINK_NEGOTIATING);
	assert_last_control(PACKET_START, false);
	complete_transmission(true);

	receive_control(PACKET_ARMED, g_link_status.run_id + 1U);
	link_state_machine_process();
	assert(g_link_status.state == LINK_NEGOTIATING);
	receive_control(PACKET_ARMED, g_link_status.run_id);
	link_state_machine_process();
	assert(g_link_status.state == LINK_ARMED);
	fake_now_us += START_GUARD_US - 1U;
	link_state_machine_process();
	assert(!timer_enabled);
	fake_now_us++;
	link_state_machine_process();
	assert(g_link_status.state == LINK_RUNNING);
	assert(timer_enabled);
	assert(timer_compare_us == BENCHMARK_PERIOD_US);
	uint64_t run_start_us = fake_now_us;

	fire_timer(125U, run_start_us);
	assert(last_send_length == BENCHMARK_DATA_SIZE);
	assert(!last_send_requested_ack);
	assert(sys_get_le32(&last_send[5]) == 0U);
	assert(timer_compare_us == 250U);
	complete_transmission(true);
	fire_timer(250U, run_start_us);
	assert(sys_get_le32(&last_send[5]) == 1U);
	assert(timer_compare_us == 375U);
	complete_transmission(true);

	/* An IRQ at 675 us misses slots 2/3, then skips slot4 for 50 us jitter.
	 * It must not enqueue three rapid catch-up mouse reports.
	 */
	uint32_t sent_before = send_count;

	fire_timer(675U, run_start_us);
	assert(send_count == sent_before);
	assert(g_link_status.next_sequence == 5U);
	assert(g_benchmark_current.tx_missed_deadlines == 3U);
	assert(timer_compare_us == 750U);
	fire_timer(750U, run_start_us);
	assert(sys_get_le32(&last_send[5]) == 5U);
	complete_transmission(true);

	/* Skip a long outage in constant time, then submit exactly the final slot. */
	fire_timer(BENCHMARK_PACKET_COUNT * BENCHMARK_PERIOD_US, run_start_us);
	assert(g_link_status.next_sequence == BENCHMARK_PACKET_COUNT);
	assert(!timer_enabled);
	assert(!transport_idle);
	assert(sys_get_le32(&last_send[5]) == BENCHMARK_PACKET_COUNT - 1U);
	assert(g_benchmark_current.tx_scheduled_slots == BENCHMARK_PACKET_COUNT);
	link_state_machine_process();
	assert(g_link_status.state == LINK_RUNNING);
	assert(g_benchmark_metadata.runs_recorded == 0U);
	complete_transmission(true);
	link_state_machine_process();
	assert(g_link_status.state == LINK_WAIT_SUMMARY);
	assert(g_benchmark_metadata.runs_recorded == 1U);
	assert(g_benchmark_results[0].completed == 1U);
	assert(g_benchmark_results[0].tx_submitted_packets == 4U);
	assert(g_benchmark_results[0].tx_successes == 4U);
	assert(g_benchmark_results[0].tx_missed_deadlines ==
	       BENCHMARK_PACKET_COUNT - 4U);
	link_state_machine_process();
	assert_last_control(PACKET_FINISH, false);
	complete_transmission(true);

	struct benchmark_control summary = {
		.kind = PACKET_SUMMARY,
		.run_id = g_link_status.run_id,
		.received = 3U,
		.missing = BENCHMARK_PACKET_COUNT - 3U,
		.completed = true,
	};
	uint8_t packet[BENCHMARK_CONTROL_SIZE];

	benchmark_control_encode(packet, &summary);
	transport_callbacks.received(packet, sizeof(packet), fake_now_us);
	link_state_machine_process();
	assert(g_link_status.state == LINK_PAUSE);
	assert(g_link_status.remote_summary_valid);
	assert(g_link_status.remote_received == 3U);
	assert(g_link_status.remote_missing == BENCHMARK_PACKET_COUNT - 3U);
	assert(g_link_status.remote_completed);
}

/* A higher-priority interrupt can advance hardware time after the first capture. */
static void delay_timer_after_initial_capture(void)
{
	timer_count_us += 200U;
	fake_now_us += 200U;
}

/* Programming an expired compare must recover promptly, and a dead timer aborts. */
static void test_compare_recovery_and_transmitter_watchdog(void)
{
	g_link_status.run_id++;
	start_cadence(fake_now_us);
	uint64_t run_start_us = fake_now_us;
	uint32_t sent_before = send_count;

	clock_read_hook = delay_timer_after_initial_capture;
	fire_timer(125U, run_start_us);
	/* The original next deadline250 has passed; physical345 is future-facing.
	 * The logical grid stays250 so the next callback counts the missed slot.
	 */
	assert(next_compare_us == 250U);
	assert(timer_count_us == 325U);
	assert(timer_compare_us == 345U);
	assert(timer_compare_us > timer_count_us);
	assert(send_count == sent_before);
	assert(transport_idle);
	assert(g_benchmark_current.tx_missed_deadlines == 1U);

	fire_timer(345U, run_start_us);
	assert(send_count == sent_before);
	assert(g_link_status.next_sequence == 2U);
	assert(g_benchmark_current.tx_missed_deadlines == 2U);
	assert(timer_compare_us == 375U);

	/* Even work taking longer than a period may submit at most one packet.
	 * Rearming after that work must notice that logical500 has expired.
	 */
	fake_submit_duration_us = 200U;
	fire_timer(375U, run_start_us);
	assert(send_count == sent_before + 1U);
	assert(sys_get_le32(&last_send[5]) == 2U);
	assert(next_compare_us == 500U);
	assert(timer_count_us == 575U);
	assert(timer_compare_us == 595U);
	complete_transmission(true);
	fire_timer(595U, run_start_us);
	assert(send_count == sent_before + 1U);
	assert(g_link_status.next_sequence == 4U);
	assert(timer_compare_us == 625U);
	assert(g_benchmark_current.tx_missed_deadlines == 3U);
	fire_timer(625U, run_start_us);
	assert(send_count == sent_before + 2U);
	assert(sys_get_le32(&last_send[5]) == 4U);
	complete_transmission(true);
	assert(timer_compare_us == 750U);
	/* Never generate another timer event: main must publish a partial run. */
	fake_now_us = run_start_us +
		(uint64_t)BENCHMARK_PACKET_COUNT * BENCHMARK_PERIOD_US +
		FIRST_DATA_TIMEOUT_US + 1U;
	link_state_machine_process();
	assert(!timer_enabled);
	assert(g_link_status.state == LINK_PAUSE);
	assert(g_link_status.last_error == -ETIMEDOUT);
	assert(g_benchmark_metadata.runs_recorded == 2U);
	assert(g_benchmark_results[1].completed == 0U);
	assert(g_benchmark_results[1].tx_scheduled_slots == 5U);
	assert(g_benchmark_results[1].tx_submitted_packets == 2U);
	assert(g_benchmark_results[1].tx_missed_deadlines == 3U);
	assert(g_benchmark_metadata.average_count == 1U);
}

#else
/* Deliver an actual production-format mouse frame with a chosen sequence. */
static void receive_mouse(uint32_t run_id, uint32_t sequence, uint64_t when_us)
{
	uint8_t packet[BENCHMARK_DATA_SIZE];

	benchmark_mouse_encode(packet, run_id, sequence);
	transport_callbacks.received(packet, sizeof(packet), when_us);
}

/* Open a fresh receiver run through the public control queue and FSM process. */
static void start_receiver_run(uint32_t run_id)
{
	receive_control(PACKET_HELLO, run_id);
	link_state_machine_process();
	assert(g_link_status.state == LINK_NEGOTIATING);
	assert_last_control(PACKET_READY, true);
	receive_control(PACKET_START, run_id);
	link_state_machine_process();
	assert(g_link_status.state == LINK_RUNNING);
	assert_last_control(PACKET_ARMED, true);
}

/* Repeated START/FINISH cannot reset counters or publish the same run twice. */
static void test_receiver_flow(void)
{
	assert(link_state_machine_init() == 0);
	assert(g_link_status.state == LINK_DISCOVERING);
	test_contract_rejection();
	start_receiver_run(71U);
	uint64_t original_start = receiver_started_us;

	fake_now_us += 10000U;
	receive_control(PACKET_START, 71U);
	link_state_machine_process();
	assert(receiver_started_us == original_start);
	assert(g_benchmark_current.rx_unique_packets == 0U);
	assert(g_benchmark_metadata.runs_recorded == 0U);
	receive_control(PACKET_FINISH, 72U);
	link_state_machine_process();
	assert(g_link_status.state == LINK_RUNNING);

	uint32_t rejected_before = g_link_status.rejected_packets;

	receive_mouse(72U, 0U, fake_now_us);
	assert(g_link_status.rejected_packets == rejected_before + 1U);
	assert(g_link_status.last_data_rx_us == 0U);
	fake_now_us += 10000U;
	receive_mouse(71U, 2U, fake_now_us);
	fake_now_us += 125U;
	receive_mouse(71U, 3U, fake_now_us);
	fake_now_us += 250U;
	receive_mouse(71U, 5U, fake_now_us);
	uint32_t replies_before = reply_count;

	receive_control(PACKET_START, 71U);
	link_state_machine_process();
	assert(g_benchmark_current.rx_unique_packets == 3U);
	assert(reply_count == replies_before);
	receive_control(PACKET_FINISH, 71U);
	link_state_machine_process();
	assert(g_link_status.state == LINK_WAIT_SUMMARY);
	assert(g_benchmark_metadata.runs_recorded == 1U);
	assert(g_benchmark_results[0].completed == 1U);
	assert(g_benchmark_results[0].rx_unique_packets == 3U);
	assert(g_benchmark_results[0].rx_missing_packets == BENCHMARK_PACKET_COUNT - 3U);
	assert_last_control(PACKET_SUMMARY, true);
	struct benchmark_control summary;

	assert(benchmark_control_decode(last_reply, sizeof(last_reply), &summary));
	assert(summary.received == 3U);
	assert(summary.missing == BENCHMARK_PACKET_COUNT - 3U);
	assert(summary.completed);
	receive_control(PACKET_FINISH, 71U);
	link_state_machine_process();
	assert(g_benchmark_metadata.runs_recorded == 1U);
	assert_last_control(PACKET_SUMMARY, true);
	/* A retried HELLO from the finished run also returns its cached summary. */
	receive_control(PACKET_HELLO, 71U);
	link_state_machine_process();
	assert(g_link_status.state == LINK_WAIT_SUMMARY);
	assert(g_benchmark_metadata.runs_recorded == 1U);
}

/* Receiving START polls must not indefinitely renew the first-data watchdog. */
static void test_first_data_timeout(void)
{
	fake_now_us += 1000U;
	start_receiver_run(81U);
	uint64_t started = fake_now_us;

	fake_now_us += 1000000U;
	receive_control(PACKET_START, 81U);
	link_state_machine_process();
	assert(receiver_started_us == started);
	fake_now_us = started + FIRST_DATA_TIMEOUT_US + 1U;
	link_state_machine_process();
	assert(g_link_status.state == LINK_WAIT_SUMMARY);
	assert(g_link_status.last_error == -ETIMEDOUT);
	assert(g_benchmark_metadata.runs_recorded == 2U);
	assert(g_benchmark_results[1].completed == 0U);
	assert(g_benchmark_results[1].rx_missing_packets == BENCHMARK_PACKET_COUNT);
	assert(g_benchmark_metadata.average_count == 1U);
}

/* Inject RX after process() reads now, so its initially sampled time is older. */
static void inject_newer_arrival(void)
{
	receive_mouse(g_link_status.run_id, 1U, fake_now_us + 10U);
	fake_now_us += 20U;
}

/* A newer ISR timestamp must not underflow an unsigned watchdog subtraction. */
static void test_clock_race_and_midrun_timeout(void)
{
	fake_now_us += 1000U;
	start_receiver_run(91U);
	fake_now_us += 20000U;
	receive_mouse(91U, 0U, fake_now_us);
	fake_now_us += 125U;
	clock_read_hook = inject_newer_arrival;
	link_state_machine_process();
	assert(g_link_status.state == LINK_RUNNING);
	assert(g_benchmark_current.rx_unique_packets == 2U);
	assert(g_benchmark_metadata.runs_recorded == 2U);
	fake_now_us = g_link_status.last_data_rx_us + DATA_TIMEOUT_US + 1U;
	link_state_machine_process();
	assert(g_link_status.state == LINK_WAIT_SUMMARY);
	assert(g_link_status.last_error == -ETIMEDOUT);
	assert(g_benchmark_metadata.runs_recorded == 3U);
	assert(g_benchmark_results[2].completed == 0U);
	assert(g_benchmark_results[2].rx_unique_packets == 2U);
	assert(g_benchmark_results[2].rx_missing_packets == BENCHMARK_PACKET_COUNT - 2U);
	assert(g_benchmark_metadata.average_count == 1U);
}
#endif

/* Run each role against unchanged production protocol, FSM, and stats code. */
int main(void)
{
#if CONFIG_BENCHMARK_TRANSMITTER
	test_transmitter_flow();
	test_compare_recovery_and_transmitter_watchdog();
	puts("link state machine PTX: PASS");
#else
	test_receiver_flow();
	test_first_data_timeout();
	test_clock_race_and_midrun_timeout();
	puts("link state machine PRX: PASS");
#endif
	return 0;
}
