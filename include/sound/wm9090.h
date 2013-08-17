/*
 * linux/sound/wm9090.h -- Platform data for WM9090
 *
 * Copyright 2009, 2010 Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_WM9090_H
#define __LINUX_SND_WM9090_H

struct wm9090_platform_data {
	/* Line inputs 1 & 2 can optionally be differential */
	unsigned int lin1_diff:1;
	unsigned int lin2_diff:1;

	/* AGC configuration.  This is intended to protect the speaker
	 * against overdriving and will therefore depend on the
	 * hardware setup with incorrect runtime configuration
	 * potentially causing hardware damage.
	 */
	unsigned int agc_ena:1;
	u16 agc[3];
};

#endif
