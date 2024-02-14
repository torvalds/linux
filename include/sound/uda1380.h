/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UDA1380 ALSA SoC Codec driver
 *
 * Copyright 2009 Philipp Zabel
 */

#ifndef __UDA1380_H
#define __UDA1380_H

struct uda1380_platform_data {
	int gpio_power;
	int gpio_reset;
	int dac_clk;
#define UDA1380_DAC_CLK_SYSCLK 0
#define UDA1380_DAC_CLK_WSPLL  1
};

#endif /* __UDA1380_H */
