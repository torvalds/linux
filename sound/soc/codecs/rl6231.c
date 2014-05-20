/*
 * rl6231.c - RL6231 class device shared support
 *
 * Copyright 2014 Realtek Semiconductor Corp.
 *
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rl6231.h"

/**
 * rl6231_calc_dmic_clk - Calculate the parameter of dmic.
 *
 * @rate: base clock rate.
 *
 * Choose dmic clock between 1MHz and 3MHz.
 * It is better for clock to approximate 3MHz.
 */
int rl6231_calc_dmic_clk(int rate)
{
	int div[] = {2, 3, 4, 6, 8, 12}, idx = -EINVAL;
	int i, red, bound, temp;

	red = 3000000 * 12;
	for (i = 0; i < ARRAY_SIZE(div); i++) {
		bound = div[i] * 3000000;
		if (rate > bound)
			continue;
		temp = bound - rate;
		if (temp < red) {
			red = temp;
			idx = i;
		}
	}

	return idx;
}
EXPORT_SYMBOL_GPL(rl6231_calc_dmic_clk);

MODULE_DESCRIPTION("RL6231 class device shared support");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
