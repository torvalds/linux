/*
 * leds-att1272.c -  LED Driver
 *
 * Copyright (C) 2011 Rockchips
 * deng dalong <ddl@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Datasheet: http://www.rohm.com/products/databook/driver/pdf/att1272gu-e.pdf
 *
 */
#ifndef _LEDS_ATT1272_H_
#define _LEDS_ATT1272_H_

struct att1272_led_platform_data {
	const char		*name;
	int	en_gpio;
	int flen_gpio;
};

#endif /* _LEDS_ATT1272_H_ */
