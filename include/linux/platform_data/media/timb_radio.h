/*
 * timb_radio.h Platform struct for the Timberdale radio driver
 * Copyright (c) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _TIMB_RADIO_
#define _TIMB_RADIO_ 1

#include <linux/i2c.h>

struct timb_radio_platform_data {
	int i2c_adapter; /* I2C adapter where the tuner and dsp are attached */
	struct i2c_board_info *tuner;
	struct i2c_board_info *dsp;
};

#endif
