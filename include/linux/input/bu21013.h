/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 */

#ifndef _BU21013_H
#define _BU21013_H

/**
 * struct bu21013_platform_device - Handle the platform data
 * @touch_x_max: touch x max
 * @touch_y_max: touch y max
 * @ext_clk: external clock flag
 * @x_flip: x flip flag
 * @y_flip: y flip flag
 * @wakeup: wakeup flag
 *
 * This is used to handle the platform data
 */
struct bu21013_platform_device {
	int touch_x_max;
	int touch_y_max;
	bool ext_clk;
	bool x_flip;
	bool y_flip;
	bool wakeup;
};

#endif
