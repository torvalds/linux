/*
 * ADAU1977/ADAU1978/ADAU1979 driver
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#ifndef __SOUND_SOC_CODECS_ADAU1977_H__
#define __SOUND_SOC_CODECS_ADAU1977_H__

#include <linux/regmap.h>

struct device;

enum adau1977_type {
	ADAU1977,
	ADAU1978,
	ADAU1979,
};

int adau1977_probe(struct device *dev, struct regmap *regmap,
	enum adau1977_type type, void (*switch_mode)(struct device *dev));

extern const struct regmap_config adau1977_regmap_config;

enum adau1977_clk_id {
	ADAU1977_SYSCLK,
};

enum adau1977_sysclk_src {
	ADAU1977_SYSCLK_SRC_MCLK,
	ADAU1977_SYSCLK_SRC_LRCLK,
};

#endif
