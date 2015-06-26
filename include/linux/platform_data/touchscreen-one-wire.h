/*
 * Copyright (c) 2015 FriendlyARM (www.arm9.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __TOUCHSCREEN_ONE_WIRE_H
#define __TOUCHSCREEN_ONE_WIRE_H

struct ts_onewire_platform_data {
	int timer_irq;
	int pwm_id;
	volatile void *pwm_reg_tint;
	int gpio;
};

#endif /*__TOUCHSCREEN_ONE_WIRE_H */
