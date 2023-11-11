// SPDX-License-Identifier: GPL-2.0
// Definitions for PCM1789 audio driver
// Copyright (C) 2018 Bootlin
// Mylène Josserand <mylene.josserand@bootlin.com>

#ifndef __PCM1789_H__
#define __PCM1789_H__

#define PCM1789_FORMATS (SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S16_LE)

extern const struct regmap_config pcm1789_regmap_config;

int pcm1789_common_init(struct device *dev, struct regmap *regmap);
void pcm1789_common_exit(struct device *dev);

#endif
