#include "link_state_machine.h"

#include <errno.h>
#include <nrf_sys_event.h>
#include <nrfx_timer.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include "benchmark.h"
#include "benchmark_protocol.h"
#include "esb_transport.h"

#define CONTROL_RETRY_US 50000ULL
#define NEGOTIATION_TIMEOUT_US 5000000ULL
#define FIRST_DATA_TIMEOUT_US 2000000ULL
#define DATA_TIMEOUT_US 200000ULL
#define START_GUARD_US 20000ULL
/* Allow bounded ISR jitter, but never send a sample a whole slot late. A busy
 * transport independently rejects any overlap with the preceding transmission.
 */
#define MAX_SLOT_LATENESS_US 30U

volatile struct link_debug_status g_link_status;

/* Only slow control packets enter a kernel queue. Data goes straight to the
 * benchmark in interrupt context, avoiding 8,000 thread wakeups per second.
 */
K_MSGQ_DEFINE(control_queue, sizeof(struct benchmark_control), 16, 4);
static nrfx_timer_t cadence_timer = NRFX_TIMER_INSTANCE(NRF_TIMER20);
static uint64_t state_started_us;
static uint64_t next_control_us;
static uint64_t receiver_started_us;
static uint32_t next_compare_us;
static volatile bool cadence_finished;
static bool transmitter_run_completed;
static bool reply_pending;
static struct benchmark_control pending_reply;
static struct benchmark_control receiver_summary;
static bool receiver_summary_valid;

/* Read the board's GRTC-backed kernel counter, with the SDK's unit conversion. */
static uint64_t link_now_us(void)
{
	return k_cyc_to_us_floor64(k_cycle_get_64());
}

/* Publish a state transition together with its timeout origin in main context. */
static void change_state(enum link_state state, uint64_t now_us)
{
	g_link_status.state = state;
	state_started_us = now_us;
	next_control_us = now_us;
}

/* Count malformed/unexpected data without logging in the radio callback. */
static void reject_packet(void)
{
	g_link_status.rejected_packets++;
}

/* Route data directly to the counter module and defer control decisions to main. */
static void radio_received(const uint8_t *data, size_t length, uint64_t now_us)
{
	if (length == BENCHMARK_DATA_SIZE && data[0] == PACKET_MOUSE) {
		if (g_link_status.transmitter || g_link_status.state != LINK_RUNNING ||
		    sys_get_le32(&data[1]) != g_link_status.run_id) {
			reject_packet();
			return;
		}
		uint32_t sequence = sys_get_le32(&data[5]);

		if (sequence < BENCHMARK_PACKET_COUNT) {
			g_link_status.last_data_rx_us = now_us;
		}
		/* Invalid sequences remain visible in the benchmark diagnostics,
		 * but cannot keep an otherwise silent link's watchdog alive.
		 */
		benchmark_rx(sequence, now_us);
		return;
	}

	struct benchmark_control message;

	if (!benchmark_control_decode(data, length, &message)) {
		reject_packet();
		return;
	}
	if (k_msgq_put(&control_queue, &message, K_NO_WAIT) != 0) {
		g_link_status.control_queue_overflows++;
	}
}

/* Attribute completions only to the phase that submitted them. No-ACK success
 * proves radio transmission, while delivery is measured separately on the PRX.
 */
static void radio_transmitted(bool success)
{
	if (g_link_status.state == LINK_RUNNING) {
		benchmark_tx_complete(success);
	} else if (!success) {
		g_link_status.control_tx_failures++;
	}
}

/* Stop at a finite number of scheduled slots, whether they succeeded or not. */
static void stop_cadence(void)
{
	nrfx_timer_disable(&cadence_timer);
	cadence_finished = true;
}

/* Keep the physical compare in the future even when higher-priority radio work
 * delays this ISR. The logical deadline stays on the original 125 us grid;
 * the next callback will account for any slots missed in the meantime.
 * Without this check, writing an already-passed compare would wait for the
 * 32-bit timer to wrap. Main also has a run watchdog for debugger halts.
 */
static void arm_next_deadline(void)
{
	unsigned int key = irq_lock();
	uint32_t timer_us = nrfx_timer_capture(&cadence_timer, NRF_TIMER_CC_CHANNEL1);
	uint32_t physical_compare_us = next_compare_us;

	if ((int32_t)(physical_compare_us - timer_us) < 20) {
		physical_compare_us = timer_us + 20U;
	}
	nrfx_timer_compare(&cadence_timer, NRF_TIMER_CC_CHANNEL0,
			   physical_compare_us, true);
	irq_unlock(key);
}

/* Service one absolute deadline. If delayed, skip expired opportunities rather
 * than sending a burst to catch up: a burst is not an 8 kHz mouse stream.
 * TIMER20 runs freely for this 125-second test; its 32-bit 1 MHz count cannot
 * wrap within a normal run. Compare0 schedules; capture1 reads elapsed time.
 */
static void cadence_interrupt(nrf_timer_event_t event, void *context)
{
	ARG_UNUSED(context);
	if (event != NRF_TIMER_EVENT_COMPARE0 || cadence_finished) {
		return;
	}

	uint32_t timer_us = nrfx_timer_capture(&cadence_timer, NRF_TIMER_CC_CHANNEL1);
	uint32_t lateness_us = timer_us - next_compare_us;
	uint32_t sequence = g_link_status.next_sequence;
	uint32_t remaining = BENCHMARK_PACKET_COUNT - sequence;
	uint32_t skipped = MIN(lateness_us / BENCHMARK_PERIOD_US, remaining);
	uint64_t now_us = link_now_us();

	if (skipped != 0U) {
		benchmark_tx_skip(sequence, skipped, now_us, lateness_us);
		sequence += skipped;
		next_compare_us += skipped * BENCHMARK_PERIOD_US;
		lateness_us -= skipped * BENCHMARK_PERIOD_US;
	}
	if (sequence == BENCHMARK_PACKET_COUNT) {
		g_link_status.next_sequence = sequence;
		stop_cadence();
		return;
	}

	uint8_t packet[BENCHMARK_DATA_SIZE];

	benchmark_mouse_encode(packet, g_link_status.run_id, sequence);
	/* A radio IRQ may have preempted packet preparation. Recheck immediately
	 * before submitting so an expired sample is skipped instead of being sent
	 * just before its successor. Mask IRQs only for this small ESB insertion.
	 */
	unsigned int key = irq_lock();

	timer_us = nrfx_timer_capture(&cadence_timer, NRF_TIMER_CC_CHANNEL1);
	lateness_us = timer_us - next_compare_us;
	now_us = link_now_us();
	if (lateness_us > MAX_SLOT_LATENESS_US) {
		benchmark_tx_skip(sequence, 1, now_us, lateness_us);
	} else {
		int err = esb_transport_send(packet, sizeof(packet), false);

		benchmark_tx_slot(sequence, now_us, err, lateness_us);
	}
	irq_unlock(key);
	g_link_status.next_sequence = sequence + 1U;
	if (g_link_status.next_sequence == BENCHMARK_PACKET_COUNT) {
		stop_cadence();
	} else {
		/* Reuse the absolute grid; adding 125 us to "now" after every send
		 * would accumulate execution time as drift. The arming helper handles
		 * a deadline crossed during the work above without waiting for wrap.
		 */
		next_compare_us += BENCHMARK_PERIOD_US;
		arm_next_deadline();
	}
}

/* Start the data clock only after the receiver has explicitly reported ARMED. */
static void start_cadence(uint64_t now_us)
{
	benchmark_begin(g_link_status.run_id, true, BENCHMARK_PACKET_COUNT);
	g_link_status.next_sequence = 0;
	cadence_finished = false;
	transmitter_run_completed = true;
	next_compare_us = BENCHMARK_PERIOD_US;
	nrfx_timer_clear(&cadence_timer);
	nrfx_timer_compare(&cadence_timer, NRF_TIMER_CC_CHANNEL0, next_compare_us, true);
	change_state(LINK_RUNNING, now_us);
	nrfx_timer_enable(&cadence_timer);
}

/* Stage a pipelined ACK reply; main retries if an immediate ACK is still on air. */
static void stage_reply(enum benchmark_packet_kind kind)
{
	pending_reply = (struct benchmark_control){
		.kind = kind,
		.run_id = g_link_status.run_id,
	};
	reply_pending = true;
}

/* Publish a receiver result even on timeout, including unreceived tail slots.
 * No fixed "receive a million" wait exists, so packet loss cannot hang a run.
 */
static void finish_receiver(bool completed, uint64_t now_us)
{
	/* Stop admitting data before the thread snapshots the interrupt counters. */
	unsigned int key = irq_lock();

	g_link_status.state = LINK_DRAINING;
	irq_unlock(key);
	benchmark_finish(completed, now_us);
	receiver_summary = (struct benchmark_control){
		.kind = PACKET_SUMMARY,
		.run_id = g_link_status.run_id,
		.received = g_benchmark_current.rx_unique_packets,
		.missing = g_benchmark_current.rx_missing_packets,
		.completed = completed,
	};
	receiver_summary_valid = true;
	pending_reply = receiver_summary;
	reply_pending = true;
	change_state(LINK_WAIT_SUMMARY, now_us);
}

/* Handle idempotent receiver requests. Retries must never restart a running
 * benchmark or publish a second result for an already-finished run.
 */
static void receiver_control(const struct benchmark_control *message, uint64_t now_us)
{
	bool same_run = message->run_id == g_link_status.run_id;

	if (message->kind == PACKET_HELLO) {
		if (g_link_status.state == LINK_RUNNING) {
			return;
		}
		if (receiver_summary_valid && same_run) {
			pending_reply = receiver_summary;
			reply_pending = true;
			return;
		}
		g_link_status.run_id = message->run_id;
		receiver_summary_valid = false;
		change_state(LINK_NEGOTIATING, now_us);
		stage_reply(PACKET_READY);
		return;
	}
	if (!same_run) {
		return;
	}
	if (message->kind == PACKET_START) {
		if (g_link_status.state == LINK_NEGOTIATING) {
			benchmark_begin(message->run_id, false, BENCHMARK_PACKET_COUNT);
			g_link_status.last_data_rx_us = 0;
			receiver_started_us = now_us;
			change_state(LINK_RUNNING, now_us);
			stage_reply(PACKET_ARMED);
		} else if (g_link_status.state == LINK_RUNNING &&
			   g_link_status.last_data_rx_us == 0) {
			/* A new START poll obtains the reply prepared by the previous
			 * one. It does not reset the receiver's timeout or counters.
			 */
			stage_reply(PACKET_ARMED);
		}
	} else if (message->kind == PACKET_FINISH &&
		   g_link_status.state == LINK_RUNNING) {
		finish_receiver(true, now_us);
	} else if ((message->kind == PACKET_QUERY || message->kind == PACKET_FINISH) &&
		   receiver_summary_valid) {
		pending_reply = receiver_summary;
		reply_pending = true;
	}
}

/* Choose a fresh session token whenever discovery starts. It prevents packets
 * left over from a reset/test from being counted in the next measurement.
 * This random token is not an authentication or pairing mechanism.
 */
static void start_discovery(uint64_t now_us)
{
	g_link_status.run_id = sys_rand32_get();
	g_link_status.remote_summary_valid = false;
	g_link_status.reconnects++;
	change_state(LINK_DISCOVERING, now_us);
}

/* Accept only a reply for this run and only in the expected handshake phase. */
static void transmitter_control(const struct benchmark_control *message, uint64_t now_us)
{
	if (message->run_id != g_link_status.run_id) {
		return;
	}
	if (message->kind == PACKET_READY && g_link_status.state == LINK_DISCOVERING) {
		change_state(LINK_NEGOTIATING, now_us);
	} else if (message->kind == PACKET_ARMED &&
		   g_link_status.state == LINK_NEGOTIATING) {
		change_state(LINK_ARMED, now_us);
	} else if (message->kind == PACKET_SUMMARY &&
		   g_link_status.state == LINK_WAIT_SUMMARY) {
		g_link_status.remote_received = message->received;
		g_link_status.remote_missing = message->missing;
		g_link_status.remote_completed = message->completed;
		g_link_status.remote_summary_valid = true;
		change_state(LINK_PAUSE, now_us);
	}
}

/* Send one reliable control request at a human-scale retry interval. */
static void send_control(enum benchmark_packet_kind kind, uint64_t now_us)
{
	if (now_us < next_control_us || !esb_transport_is_idle()) {
		return;
	}
	struct benchmark_control message = {.kind = kind, .run_id = g_link_status.run_id};
	uint8_t packet[BENCHMARK_CONTROL_SIZE];

	benchmark_control_encode(packet, &message);
	int err = esb_transport_send(packet, sizeof(packet), true);

	if (err == 0) {
		next_control_us = now_us + CONTROL_RETRY_US;
	} else if (err != -EBUSY) {
		g_link_status.last_error = err;
	}
}

/* Drive PTX phases in main; the RUNNING state's per-slot work stays in TIMER20. */
static void process_transmitter(uint64_t now_us)
{
	switch (g_link_status.state) {
	case LINK_DISCOVERING:
	case LINK_NEGOTIATING:
		if (now_us - state_started_us > NEGOTIATION_TIMEOUT_US) {
			if (esb_transport_is_idle()) {
				start_discovery(now_us);
			}
			break;
		}
		send_control(g_link_status.state == LINK_DISCOVERING ?
			     PACKET_HELLO : PACKET_START, now_us);
		break;
	case LINK_ARMED:
		if (now_us - state_started_us >= START_GUARD_US && esb_transport_is_idle()) {
			start_cadence(now_us);
		}
		break;
	case LINK_RUNNING:
		if (!cadence_finished && now_us - state_started_us >
		    (uint64_t)BENCHMARK_PACKET_COUNT * BENCHMARK_PERIOD_US +
		    FIRST_DATA_TIMEOUT_US) {
			/* A stalled timer or debugger halt must not leave the FSM
			 * waiting forever. Preserve the partial counters for inspection.
			 */
			stop_cadence();
			transmitter_run_completed = false;
			g_link_status.last_error = -ETIMEDOUT;
		}
		if (cadence_finished && esb_transport_is_idle()) {
			/* Let the final no-ACK TX completion reach the counter before
			 * publication, then begin the separately timed control phase.
			 */
			benchmark_finish(transmitter_run_completed, now_us);
			change_state(transmitter_run_completed ? LINK_WAIT_SUMMARY : LINK_PAUSE,
				     now_us);
		}
		break;
	case LINK_WAIT_SUMMARY:
		if (now_us - state_started_us > NEGOTIATION_TIMEOUT_US) {
			g_link_status.last_error = -ETIMEDOUT;
			change_state(LINK_PAUSE, now_us);
		} else {
			/* FINISH is idempotent and also polls for the queued SUMMARY.
			 * Repeating it handles a lost first FINISH without a deadlock.
			 */
			send_control(PACKET_FINISH, now_us);
		}
		break;
	case LINK_PAUSE:
		if (now_us - state_started_us > 250000ULL && esb_transport_is_idle()) {
			start_discovery(now_us);
		}
		break;
	default:
		break;
	}
}

/* Publish delayed control replies and detect a silent/disconnected transmitter. */
static void process_receiver(uint64_t now_us)
{
	unsigned int key = irq_lock();
	uint64_t last_rx_us = g_link_status.last_data_rx_us;

	irq_unlock(key);
	/* An RX interrupt may have run since process() sampled its timestamp.
	 * Read time after the protected snapshot so unsigned subtraction cannot
	 * interpret a slightly newer arrival as a huge link timeout.
	 */
	now_us = link_now_us();
	if (g_link_status.state == LINK_RUNNING &&
	    ((last_rx_us == 0 && now_us - receiver_started_us > FIRST_DATA_TIMEOUT_US) ||
	     (last_rx_us != 0 && now_us - last_rx_us > DATA_TIMEOUT_US))) {
		g_link_status.last_error = -ETIMEDOUT;
		finish_receiver(false, now_us);
	}
	if (g_link_status.state == LINK_NEGOTIATING &&
	    now_us - state_started_us > NEGOTIATION_TIMEOUT_US) {
		change_state(LINK_DISCOVERING, now_us);
		reply_pending = false;
	}
	/* ARMED must be queued during startup, while RUNNING precedes the first
	 * data by a 20 ms guard. After the first data packet, never pause RX to
	 * replace an ACK: that would introduce gaps in the measurement itself.
	 */
	if (reply_pending && (g_link_status.state != LINK_RUNNING || last_rx_us == 0)) {
		uint8_t packet[BENCHMARK_CONTROL_SIZE];

		benchmark_control_encode(packet, &pending_reply);
		int err = esb_transport_receiver_queue_reply(packet, sizeof(packet));

		if (err == 0) {
			reply_pending = false;
		} else if (err != -EBUSY) {
			g_link_status.last_error = err;
		}
	}
}

/* Reserve the cadence timer independently of ESB TIMER10 and Zephyr GRTC. */
int link_state_machine_init(void)
{
	static const struct esb_transport_callbacks callbacks = {
		.received = radio_received,
		.transmitted = radio_transmitted,
	};
	bool transmitter = IS_ENABLED(CONFIG_BENCHMARK_TRANSMITTER);
	int err;

	g_link_status.transmitter = transmitter;
	/* CONSTLAT keeps core/peripheral wake-up short; keeping RRAM in standby
	 * separately avoids fetching this ISR from powered-down instruction memory.
	 * Hold the request for this benchmark application's lifetime. This deliberate
	 * power cost is documented; this build measures timing, not battery life.
	 */
	err = nrf_sys_event_register(0, true);
	if (err < 0) {
		goto failure;
	}
	if (transmitter) {
		nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(1000000);

		config.bit_width = NRF_TIMER_BIT_WIDTH_32;
		config.interrupt_priority = 3;
		IRQ_CONNECT(TIMER20_IRQn, 3, nrfx_timer_irq_handler, &cadence_timer, 0);
		err = nrfx_timer_init(&cadence_timer, &config, cadence_interrupt);
		if (err != 0) {
			goto failure;
		}
	}
	err = esb_transport_init(transmitter, &callbacks);
	if (err != 0) {
		goto failure;
	}
	if (transmitter) {
		start_discovery(link_now_us());
	} else {
		change_state(LINK_DISCOVERING, link_now_us());
	}
	return 0;

failure:
	g_link_status.last_error = err;
	g_link_status.state = LINK_FAULT;
	return err;
}

/* Keep FSM mutations in one thread; radio callbacks only count/enqueue events. */
void link_state_machine_process(void)
{
	uint64_t now_us = link_now_us();
	struct benchmark_control message;

	while (k_msgq_get(&control_queue, &message, K_NO_WAIT) == 0) {
		if (g_link_status.transmitter) {
			transmitter_control(&message, now_us);
		} else {
			receiver_control(&message, now_us);
		}
	}
	if (g_link_status.transmitter) {
		process_transmitter(now_us);
	} else {
		process_receiver(now_us);
	}
}

/* Keep the lock short: only this small snapshot, never formatting or UART I/O. */
void link_state_machine_get_status(struct link_debug_status *status)
{
	unsigned int key = irq_lock();

	*status = g_link_status;
	irq_unlock(key);
}
