/*
 * The transport owns Nordic ESB and its queues. Protocol and benchmark code
 * exchange byte strings here without depending on ESB's payload structures.
 */
#ifndef ESB_TRANSPORT_H_
#define ESB_TRANSPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESB_TRANSPORT_MAX_PAYLOAD 32U

struct esb_transport_callbacks {
	/* Called in ESB event interrupt context; data is valid only in this call. */
	void (*received)(const uint8_t *data, size_t length, uint64_t now_us);
	/* No-ACK success means transmitted, not confirmed reception by the peer. */
	void (*transmitted)(bool success);
};

/* Initialize a fixed PTX or PRX role, 4 Mbps, CRC16 and the test RF address. */
int esb_transport_init(bool transmitter,
		       const struct esb_transport_callbacks *callbacks);

/*
 * Submit one PTX payload. A busy transmitter returns -EBUSY instead of building
 * a queue of old mouse samples. Safe from main or the benchmark timer ISR.
 */
int esb_transport_send(const uint8_t *data, size_t length, bool request_ack);

/*
 * Replace the PRX's next ACK payload during a control phase. The reply is sent
 * in response to a FUTURE request; it cannot amend an ACK already on the air.
 * Retry -EBUSY from main after the current ACK finishes. This operation briefly
 * pauses reception and must not be called during a timed data run.
 */
int esb_transport_receiver_queue_reply(const uint8_t *data, size_t length);

/* Return whether a PTX has finished its previous packet and can send another. */
bool esb_transport_is_idle(void);

/* Clear stale packets between control phases; never call during a timed run. */
int esb_transport_reset_queues(void);

#endif /* ESB_TRANSPORT_H_ */
