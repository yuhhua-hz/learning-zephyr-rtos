#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>

#include "z_ds1302.h"
#include "z_display.h"
#include "z_serial_console.h"

#define CONFIG_TIME 0

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{

#if CONFIG_TIME
	ds1302_time_t initial_time = {
		.seconds = 0,
		.minutes = 40,
		.hours = 8,
		.date = 12,
		.month = 3,
		.year = 26
	};
#endif
	int ret;
	char date_buf[16], time_buf[9];
	
	ret = ds1302_init();
	if (ret < 0) return ret;

	ret = app_display_setup();
	if (ret < 0) return ret;

	ret = uart_cmd_init();
	if (ret < 0) return ret;

	LOG_INF("Main initialized");

#if CONFIG_TIME
	ds1302_setTime(&initial_time);
#endif

	for (;;) {

		ds1302_getDate_str(date_buf, sizeof(date_buf));
		ds1302_getTime_str(time_buf, sizeof(time_buf));
		
		app_display_clear();
		app_display_line(date_buf, 8, 8);
		app_display_line(time_buf, 18, 32);
		app_display_flush();

		k_msleep(500);
	}

	return 0;
}
