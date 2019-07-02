/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for AUO in-cell touchscreens
 *
 * Copyright (c) 2011 Heiko Stuebner <heiko@sntech.de>
 *
 * based on auo_touch.h from Dell Streak kernel
 *
 * Copyright (c) 2008 QUALCOMM Incorporated.
 * Copyright (c) 2008 QUALCOMM USA, INC.
 */

#ifndef __AUO_PIXCIR_TS_H__
#define __AUO_PIXCIR_TS_H__

/*
 * Interrupt modes:
 * periodical:		interrupt is asserted periodicaly
 * compare coordinates:	interrupt is asserted when coordinates change
 * indicate touch:	interrupt is asserted during touch
 */
#define AUO_PIXCIR_INT_PERIODICAL	0x00
#define AUO_PIXCIR_INT_COMP_COORD	0x01
#define AUO_PIXCIR_INT_TOUCH_IND	0x02

/*
 * @gpio_int		interrupt gpio
 * @int_setting		one of AUO_PIXCIR_INT_*
 * @init_hw		hardwarespecific init
 * @exit_hw		hardwarespecific shutdown
 * @x_max		x-resolution
 * @y_max		y-resolution
 */
struct auo_pixcir_ts_platdata {
	int gpio_int;
	int gpio_rst;

	int int_setting;

	unsigned int x_max;
	unsigned int y_max;
};

#endif
