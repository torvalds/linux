/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/rt5660.h -- Platform data for RT5660
 *
 * Copyright 2016 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 */

#ifndef __LINUX_SND_RT5660_H
#define __LINUX_SND_RT5660_H

enum rt5660_dmic1_data_pin {
	RT5660_DMIC1_NULL,
	RT5660_DMIC1_DATA_GPIO2,
	RT5660_DMIC1_DATA_IN1P,
};

struct rt5660_platform_data {
	/* IN1 & IN3 can optionally be differential */
	bool in1_diff;
	bool in3_diff;
	bool use_ldo2;
	bool poweroff_codec_in_suspend;

	enum rt5660_dmic1_data_pin dmic1_data_pin;
};

#endif
