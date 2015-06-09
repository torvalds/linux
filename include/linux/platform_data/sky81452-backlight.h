/*
 * sky81452.h	SKY81452 backlight driver
 *
 * Copyright 2014 Skyworks Solutions Inc.
 * Author : Gyungoh Yoo <jack.yoo@skyworksinc.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SKY81452_BACKLIGHT_H
#define _SKY81452_BACKLIGHT_H

/**
 * struct sky81452_platform_data
 * @name:	backlight driver name.
		If it is not defined, default name is lcd-backlight.
 * @gpio_enable:GPIO number which control EN pin
 * @enable:	Enable mask for current sink channel 1, 2, 3, 4, 5 and 6.
 * @ignore_pwm:	true if DPWMI should be ignored.
 * @dpwm_mode:	true is DPWM dimming mode, otherwise Analog dimming mode.
 * @phase_shift:true is phase shift mode.
 * @short_detecion_threshold:	It should be one of 4, 5, 6 and 7V.
 * @boost_current_limit:	It should be one of 2300, 2750mA.
 */
struct sky81452_bl_platform_data {
	const char *name;
	int gpio_enable;
	unsigned int enable;
	bool ignore_pwm;
	bool dpwm_mode;
	bool phase_shift;
	unsigned int short_detection_threshold;
	unsigned int boost_current_limit;
};

#endif
