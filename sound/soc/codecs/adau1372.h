/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ADAU1372 driver
 *
 * Copyright 2016 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#ifndef SOUND_SOC_CODECS_ADAU1372_H
#define SOUND_SOC_CODECS_ADAU1372_H

#include <linux/regmap.h>

struct device;

extern const struct of_device_id adau1372_of_match[];
int adau1372_probe(struct device *dev, struct regmap *regmap,
		   void (*switch_mode)(struct device *dev));

extern const struct regmap_config adau1372_regmap_config;

#endif
