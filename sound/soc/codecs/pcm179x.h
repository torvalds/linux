/*
 * definitions for PCM179X
 *
 * Copyright 2013 Amarula Solutions
 *
  * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PCM179X_H__
#define __PCM179X_H__

#define PCM1792A_FORMATS (SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | \
			  SNDRV_PCM_FMTBIT_S16_LE)

extern const struct regmap_config pcm179x_regmap_config;

int pcm179x_common_init(struct device *dev, struct regmap *regmap);
int pcm179x_common_exit(struct device *dev);

#endif
