/*
 * definitions for PCM1792A
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

#ifndef __PCM1792A_H__
#define __PCM1792A_H__

#define PCM1792A_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_8000_48000 | \
			SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | \
			SNDRV_PCM_RATE_192000)

#define PCM1792A_FORMATS (SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | \
			  SNDRV_PCM_FMTBIT_S16_LE)

#endif
