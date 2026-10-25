#ifndef FSM_TEST_NRF_SYS_EVENT_H_
#define FSM_TEST_NRF_SYS_EVENT_H_

#include <stdbool.h>
#include <stdint.h>

/* The host has no power domains; accept the benchmark's constant-latency hold. */
static inline int nrf_sys_event_register(uint32_t context, bool active)
{
	(void)context;
	(void)active;
	return 0;
}

#endif
