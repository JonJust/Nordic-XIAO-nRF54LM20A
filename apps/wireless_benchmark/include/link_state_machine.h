#ifndef LINK_STATE_MACHINE_H
#define LINK_STATE_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

enum link_state {
	LINK_BOOT,
	LINK_DISCOVERING,
	LINK_NEGOTIATING,
	LINK_ARMED,
	LINK_RUNNING,
	LINK_DRAINING,
	LINK_WAIT_SUMMARY,
	LINK_PAUSE,
	LINK_FAULT,
};

/* Deliberately public debugger view. Operational state remains private to the
 * module. Pause before inspecting 64-bit fields on this 32-bit CPU.
 */
struct link_debug_status {
	enum link_state state;
	bool transmitter;
	uint32_t run_id;
	int last_error;
	uint32_t rejected_packets;
	uint32_t control_queue_overflows;
	uint32_t control_tx_failures;
	uint32_t reconnects;
	uint32_t next_sequence;
	uint32_t remote_received;
	uint32_t remote_missing;
	bool remote_completed;
	bool remote_summary_valid;
	uint64_t last_data_rx_us;
};

extern volatile struct link_debug_status g_link_status;

/* Initialize the fixed PTX/PRX role selected by this image's Kconfig. */
int link_state_machine_init(void);

/* Service control traffic and timeouts from the main thread, about every 10 ms. */
void link_state_machine_process(void);

/* Copy the compact debugger view coherently while radio callbacks are active. */
void link_state_machine_get_status(struct link_debug_status *status);

#endif
