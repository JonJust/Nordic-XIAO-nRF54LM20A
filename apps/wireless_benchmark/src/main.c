#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "link_state_machine.h"
#include "status_telemetry.h"

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* Keep main as glue: modules own radio, connection management, and measurements.
 * The slow heartbeat remains visible without opening a serial monitor.
 * A solid LED indicates an initialization/runtime fault.
 */
int main(void)
{
	bool led_ready = gpio_is_ready_dt(&led) &&
			 gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) == 0;
	int telemetry_error = status_telemetry_init();
	int err = link_state_machine_init();
	uint32_t last_toggle_ms = 0;
	bool illuminated = false;

	while (true) {
		if (err == 0) {
			link_state_machine_process();
		}
		if (telemetry_error == 0) {
			status_telemetry_process();
		}
		uint32_t now_ms = k_uptime_get_32();

		if (led_ready && (now_ms - last_toggle_ms >= 500U)) {
			last_toggle_ms = now_ms;
			illuminated = !illuminated;
			gpio_pin_set_dt(&led, telemetry_error != 0 ||
				       g_link_status.state == LINK_FAULT ||
				       illuminated);
		}
		/* This sleep services only slow control. TIMER20 schedules data. */
		k_msleep(10);
	}
	return 0;
}
