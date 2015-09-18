/*
 * Platform data for the TI bq24190 battery charger driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BQ24190_CHARGER_H_
#define _BQ24190_CHARGER_H_

struct bq24190_platform_data {
	unsigned int	gpio_int;	/* GPIO pin that's connected to INT# */
};

#endif
