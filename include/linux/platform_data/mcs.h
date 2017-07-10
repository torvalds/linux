/*
 * Copyright (C) 2009 - 2010 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 * Author: HeungJun Kim <riverful.kim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __LINUX_MCS_H
#define __LINUX_MCS_H

#define MCS_KEY_MAP(v, c)	((((v) & 0xff) << 16) | ((c) & 0xffff))
#define MCS_KEY_VAL(v)		(((v) >> 16) & 0xff)
#define MCS_KEY_CODE(v)		((v) & 0xffff)

struct mcs_platform_data {
	void (*poweron)(bool);
	void (*cfg_pin)(void);

	/* touchscreen */
	unsigned int x_size;
	unsigned int y_size;

	/* touchkey */
	const u32 *keymap;
	unsigned int keymap_size;
	unsigned int key_maxval;
	bool no_autorepeat;
};

#endif	/* __LINUX_MCS_H */
