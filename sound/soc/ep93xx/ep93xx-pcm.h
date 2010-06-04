/*
 * sound/soc/ep93xx/ep93xx-pcm.h - EP93xx ALSA PCM interface
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 * Copyright (C) 2006 Applied Data Systems
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _EP93XX_SND_SOC_PCM_H
#define _EP93XX_SND_SOC_PCM_H

struct ep93xx_pcm_dma_params {
	char	*name;
	int	dma_port;
};

extern struct snd_soc_platform ep93xx_soc_platform;

#endif /* _EP93XX_SND_SOC_PCM_H */
