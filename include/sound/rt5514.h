/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/rt5514.h -- Platform data for RT5514
 *
 * Copyright 2016 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 */

#ifndef __LINUX_SND_RT5514_H
#define __LINUX_SND_RT5514_H

struct rt5514_platform_data {
	unsigned int dmic_init_delay;
	const char *dsp_calib_clk_name;
	unsigned int dsp_calib_clk_rate;
};

#endif

