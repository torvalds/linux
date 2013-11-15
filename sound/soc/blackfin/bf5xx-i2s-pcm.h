/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BF5XX_TDM_PCM_H
#define _BF5XX_TDM_PCM_H

#define BFIN_TDM_DAI_MAX_SLOTS 8

struct bf5xx_i2s_pcm_data {
	unsigned int map[BFIN_TDM_DAI_MAX_SLOTS];
	bool tdm_mode;
};

#endif
