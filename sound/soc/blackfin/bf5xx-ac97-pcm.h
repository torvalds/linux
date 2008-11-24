/*
 * linux/sound/arm/bf5xx-ac97-pcm.h -- ALSA PCM interface for the Blackfin
 *
 * Copyright 2007 Analog Device Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BF5XX_AC97_PCM_H
#define _BF5XX_AC97_PCM_H

struct bf5xx_pcm_dma_params {
	char *name;			/* stream identifier */
};

struct bf5xx_gpio {
	u32 sys;
	u32 rx;
	u32 tx;
	u32 clk;
	u32 frm;
};

/* platform data */
extern struct snd_soc_platform bf5xx_ac97_soc_platform;

#endif
