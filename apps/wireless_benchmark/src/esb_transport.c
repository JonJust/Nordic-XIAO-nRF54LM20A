/*
 * A deliberately small ESB adapter: one radio owner, one outstanding PTX
 * payload, no allocation, and no logging in either radio interrupt path.
 */
#include "esb_transport.h"

#include <errno.h>
#include <string.h>

#include <esb.h>
#include <hal/nrf_radio.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

BUILD_ASSERT(CONFIG_ESB_MAX_PAYLOAD_LENGTH >= ESB_TRANSPORT_MAX_PAYLOAD,
	     "The transport requires 32-byte ESB payload storage");
BUILD_ASSERT(CONFIG_ESB_TX_FIFO_SIZE >= 2,
	     "Replacing ACK payloads requires two ESB TX FIFO entries");

static struct esb_transport_callbacks transport_callbacks;
static bool transport_initialized;
static bool transport_transmitter;
static bool transport_tx_busy;

/* Convert the kernel's 64-bit hardware cycle counter to monotonic microseconds. */
static uint64_t transport_now_us(void)
{
	/* On this board the cycle source is GRTC, not the CPU clock or SysTick. */
	return k_cyc_to_us_floor64(k_cycle_get_64());
}

/* Replace an ACK only while the PRX is unambiguously in a receive state. */
static bool receiver_ack_in_progress(void)
{
	nrf_radio_state_t state = nrf_radio_state_get(NRF_RADIO);

	/* Include disable/disabled transitions: hardware shorts may already be
	 * about to ramp TX before the higher-priority radio IRQ has run.
	 */
	return state != NRF_RADIO_STATE_RXRU &&
	       state != NRF_RADIO_STATE_RXIDLE &&
	       state != NRF_RADIO_STATE_RX;
}

/* Deliver short notifications and completely drain ESB's coalesced RX event. */
static void transport_event_handler(const struct esb_evt *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
	case ESB_EVENT_TX_FAILED:
		/* PRX also reports TX_SUCCESS when an ACK payload is retired. That
		 * notification does not complete a benchmark transmission.
		 */
		if (transport_transmitter) {
			unsigned int key = irq_lock();
			bool success = event->evt_id == ESB_EVENT_TX_SUCCESS;

			if (!success) {
				/* ESB retains a failed reliable packet at the FIFO head.
				 * Remove it before admitting the next control packet.
				 * ESB sets itself IDLE before issuing TX_FAILED.
				 */
				(void)esb_flush_tx();
			}
			transport_tx_busy = false;
			irq_unlock(key);

			if (transport_callbacks.transmitted != NULL) {
				transport_callbacks.transmitted(success);
			}
		}
		break;

	case ESB_EVENT_RX_RECEIVED: {
		struct esb_payload payload;

		/* ESB event flags are bits, not packet counts: a single callback
		 * can represent several FIFO entries. Always drain the FIFO.
		 */
		while (esb_read_rx_payload(&payload) == 0) {
			uint64_t now_us = transport_now_us();

			if (transport_callbacks.received != NULL) {
				/* This is callback arrival time, not a hardware RF timestamp.
				 * A delayed event IRQ can therefore affect measured jitter.
				 */
				transport_callbacks.received(payload.data, payload.length,
							     now_us);
			}
		}
		break;
	}
	default:
		break;
	}
}

/* Configure both roles identically except for which endpoint initiates a send. */
int esb_transport_init(bool transmitter,
		       const struct esb_transport_callbacks *callbacks)
{
	/* Fixed, intentionally public bench addresses; these are not pairing keys. */
	static const uint8_t base_address_0[4] = {0xA7, 0xC3, 0x56, 0x91};
	static const uint8_t base_address_1[4] = {0xD2, 0x8B, 0x47, 0x69};
	static const uint8_t prefixes[1] = {0xE7};
	struct esb_config config = ESB_DEFAULT_CONFIG;
	int err;

	if (callbacks == NULL) {
		return -EINVAL;
	}
	if (transport_initialized) {
		return -EALREADY;
	}

	transport_callbacks = *callbacks;
	transport_transmitter = transmitter;
	transport_tx_busy = false;
	config.mode = transmitter ? ESB_MODE_PTX : ESB_MODE_PRX;
	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.bitrate = ESB_BITRATE_4MBPS;
	config.crc = ESB_CRC_16BIT;
	config.selective_auto_ack = true;
	config.use_fast_ramp_up = true;
	config.event_handler = transport_event_handler;
	/* The minimum retransmit delay is validated even for no-ACK data. Keep
	 * the SDK's supported 600 us value; it is not the 125 us data cadence.
	 */
	config.retransmit_delay = 600;
	config.retransmit_count = 3;

	err = esb_init(&config);
	if (err != 0) {
		return err;
	}
	err = esb_set_base_address_0(base_address_0);
	if (err == 0) {
		err = esb_set_base_address_1(base_address_1);
	}
	if (err == 0) {
		err = esb_set_prefixes(prefixes, ARRAY_SIZE(prefixes));
	}
	if (err == 0) {
		err = esb_enable_pipes(BIT(0));
	}
	if (err == 0) {
		err = esb_set_rf_channel(CONFIG_BENCHMARK_RF_CHANNEL);
	}
	if (err == 0 && !transmitter) {
		err = esb_start_rx();
	}
	if (err != 0) {
		esb_disable();
		return err;
	}
	transport_initialized = true;
	return 0;
}

/* Copy one sample into ESB only when there is no older sample waiting to send. */
int esb_transport_send(const uint8_t *data, size_t length, bool request_ack)
{
	struct esb_payload payload = {0};
	unsigned int key;
	int err;

	if (!transport_initialized || !transport_transmitter) {
		return -EACCES;
	}
	if (data == NULL || length == 0 || length > ESB_TRANSPORT_MAX_PAYLOAD) {
		return -EINVAL;
	}
	payload.pipe = 0;
	payload.length = length;
	payload.noack = !request_ack;
	memcpy(payload.data, data, length);

	/* Main and the cadence ISR both call this API. A single critical section
	 * makes the busy test, ownership change and ESB queue insertion atomic.
	 * Radio/event IRQs stay masked only while copying this small payload.
	 */
	key = irq_lock();
	if (transport_tx_busy || !esb_is_idle()) {
		irq_unlock(key);
		return -EBUSY;
	}
	transport_tx_busy = true;
	err = esb_write_payload(&payload);
	if (err != 0) {
		transport_tx_busy = false;
	}
	irq_unlock(key);
	return err;
}

/* Replace stale PRX ACK contents without interrupting an ACK already on air. */
int esb_transport_receiver_queue_reply(const uint8_t *data, size_t length)
{
	struct esb_payload payload = {0};
	unsigned int key;
	int err;
	int restart_err;

	if (!transport_initialized || transport_transmitter) {
		return -EACCES;
	}
	if (data == NULL || length == 0 || length > ESB_TRANSPORT_MAX_PAYLOAD) {
		return -EINVAL;
	}
	payload.pipe = 0;
	payload.length = length;
	memcpy(payload.data, data, length);

	key = irq_lock();
	if (receiver_ack_in_progress()) {
		irq_unlock(key);
		return -EBUSY;
	}
	/* ESB permits queue flushes only in IDLE. PRX normally listens forever,
	 * so a bounded control-phase pause is required before replacing a reply.
	 */
	err = esb_stop_rx();
	if (err == -EALREADY) {
		err = 0;
	}
	if (err == 0) {
		err = esb_flush_tx();
	}
	if (err == 0) {
		err = esb_write_payload(&payload);
	}
	if (err == 0) {
		/* Counter-intuitively, ESB's flush does not clear its per-pipe
		 * "previous ACK carried data" flag. The next NEW request may
		 * retire one queue head before selecting its reply. Two identical
		 * copies make the replacement valid whether that flag is set or
		 * clear; protocol tokens let the peer ignore repeated replies.
		 */
		err = esb_write_payload(&payload);
	}
	restart_err = esb_start_rx();
	irq_unlock(key);
	return err != 0 ? err : restart_err;
}

/* Snapshot the transport's PTX ownership and ESB state as one coherent test. */
bool esb_transport_is_idle(void)
{
	unsigned int key = irq_lock();
	bool idle = transport_initialized && transport_transmitter &&
		    !transport_tx_busy && esb_is_idle();

	irq_unlock(key);
	return idle;
}

/* Empty queues after a completed/aborted run so the next run starts cleanly. */
int esb_transport_reset_queues(void)
{
	unsigned int key;
	int err;
	int restart_err = 0;

	if (!transport_initialized) {
		return -EACCES;
	}
	key = irq_lock();
	if (transport_tx_busy ||
	    (transport_transmitter && !esb_is_idle()) ||
	    (!transport_transmitter && receiver_ack_in_progress())) {
		irq_unlock(key);
		return -EBUSY;
	}
	err = transport_transmitter ? 0 : esb_stop_rx();
	if (err == -EALREADY) {
		err = 0;
	}
	if (err == 0) {
		err = esb_flush_tx();
	}
	if (err == 0) {
		err = esb_flush_rx();
	}
	if (!transport_transmitter) {
		restart_err = esb_start_rx();
	}
	irq_unlock(key);
	return err != 0 ? err : restart_err;
}
