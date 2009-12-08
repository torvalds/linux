/*
 * mcs5000_ts.h
 *
 * Copyright (C) 2009 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __LINUX_MCS5000_TS_H
#define __LINUX_MCS5000_TS_H

/* platform data for the MELFAS MCS-5000 touchscreen driver */
struct mcs5000_ts_platform_data {
	void (*cfg_pin)(void);
	int x_size;
	int y_size;
};

#endif	/* __LINUX_MCS5000_TS_H */
