/*
 * ADAU1361/ADAU1461/ADAU1761/ADAU1961 driver
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#ifndef __SOUND_SOC_CODECS_ADAU1761_H__
#define __SOUND_SOC_CODECS_ADAU1761_H__

#include <linux/regmap.h>
#include "adau17x1.h"

struct device;

int adau1761_probe(struct device *dev, struct regmap *regmap,
	enum adau17x1_type type, void (*switch_mode)(struct device *dev));

extern const struct regmap_config adau1761_regmap_config;

#endif
