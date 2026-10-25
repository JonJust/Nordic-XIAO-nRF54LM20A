#ifndef FSM_TEST_NRFX_TIMER_H_
#define FSM_TEST_NRFX_TIMER_H_

#include <stdbool.h>
#include <stdint.h>

#define NRF_TIMER20 20
#define TIMER20_IRQn 20
#define NRF_TIMER_BIT_WIDTH_32 32
#define NRF_TIMER_CC_CHANNEL0 0
#define NRF_TIMER_CC_CHANNEL1 1

typedef enum { NRF_TIMER_EVENT_COMPARE0 = 0 } nrf_timer_event_t;
typedef void (*nrfx_timer_event_handler_t)(nrf_timer_event_t event, void *context);
typedef struct {
	unsigned int instance;
} nrfx_timer_t;
typedef struct {
	uint32_t frequency;
	unsigned int bit_width;
	unsigned int interrupt_priority;
} nrfx_timer_config_t;

#define NRFX_TIMER_INSTANCE(instance) { instance }
#define NRFX_TIMER_DEFAULT_CONFIG(frequency_hz) { .frequency = frequency_hz }

int nrfx_timer_init(const nrfx_timer_t *timer, const nrfx_timer_config_t *config,
		    nrfx_timer_event_handler_t handler);
void nrfx_timer_enable(const nrfx_timer_t *timer);
void nrfx_timer_disable(const nrfx_timer_t *timer);
void nrfx_timer_clear(const nrfx_timer_t *timer);
void nrfx_timer_compare(const nrfx_timer_t *timer, unsigned int channel,
			uint32_t value, bool enable_interrupt);
uint32_t nrfx_timer_capture(const nrfx_timer_t *timer, unsigned int channel);

#endif
