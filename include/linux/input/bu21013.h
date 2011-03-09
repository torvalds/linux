/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 * License terms:GNU General Public License (GPL) version 2
 */

#ifndef _BU21013_H
#define _BU21013_H

/**
 * struct bu21013_platform_device - Handle the platform data
 * @cs_en:	pointer to the cs enable function
 * @cs_dis:	pointer to the cs disable function
 * @irq_read_val:    pointer to read the pen irq value function
 * @x_max_res: xmax resolution
 * @y_max_res: ymax resolution
 * @touch_x_max: touch x max
 * @touch_y_max: touch y max
 * @cs_pin: chip select pin
 * @irq: irq pin
 * @ext_clk: external clock flag
 * @x_flip: x flip flag
 * @y_flip: y flip flag
 * @wakeup: wakeup flag
 *
 * This is used to handle the platform data
 */
struct bu21013_platform_device {
	int (*cs_en)(int reset_pin);
	int (*cs_dis)(int reset_pin);
	int (*irq_read_val)(void);
	int x_max_res;
	int y_max_res;
	int touch_x_max;
	int touch_y_max;
	unsigned int cs_pin;
	unsigned int irq;
	bool ext_clk;
	bool x_flip;
	bool y_flip;
	bool wakeup;
};

#endif
