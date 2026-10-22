#include <stdbool.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define BLINK_INTERVAL_MS 2500

/* Seeed's board definition maps led0 to the blue LED on P1.23. */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	int err;
	bool led_on = false;

	if (!gpio_is_ready_dt(&led)) {
		printk("LED GPIO device is not ready\n");
		return -1;
	}

	err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		printk("LED configuration failed: %d\n", err);
		return err;
	}

	printk("XIAO nRF54LM20A V1.0 blinky started\n");

	while (true) {
		led_on = !led_on;

		/* The devicetree API applies the LED's active-low polarity. */
		err = gpio_pin_set_dt(&led, led_on);
		if (err < 0) {
			printk("LED update failed: %d\n", err);
			return err;
		}

		printk("LED %s\n", led_on ? "on" : "off");
		k_msleep(BLINK_INTERVAL_MS);
	}

	return 0;
}
