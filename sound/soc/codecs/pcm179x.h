/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * definitions for PCM179X
 *
 * Copyright 2013 Amarula Solutions
 */

#ifndef __PCM179X_H__
#define __PCM179X_H__

#define PCM1792A_FORMATS (SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | \
			  SNDRV_PCM_FMTBIT_S16_LE)

extern const struct regmap_config pcm179x_regmap_config;

int pcm179x_common_init(struct device *dev, struct regmap *regmap);

#endif
