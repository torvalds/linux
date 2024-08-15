/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/rt5663.h -- Platform data for RT5663
 *
 * Copyright 2017 Realtek Semiconductor Corp.
 */

#ifndef __LINUX_SND_RT5663_H
#define __LINUX_SND_RT5663_H

struct rt5663_platform_data {
	unsigned int dc_offset_l_manual;
	unsigned int dc_offset_r_manual;
	unsigned int dc_offset_l_manual_mic;
	unsigned int dc_offset_r_manual_mic;

	unsigned int impedance_sensing_num;
	unsigned int *impedance_sensing_table;
};

#endif

