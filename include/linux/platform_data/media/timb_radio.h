/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timb_radio.h Platform struct for the Timberdale radio driver
 * Copyright (c) 2009 Intel Corporation
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
