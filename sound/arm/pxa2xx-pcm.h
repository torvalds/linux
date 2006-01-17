/*
 * linux/sound/arm/pxa2xx-pcm.h -- ALSA PCM interface for the Intel PXA2xx chip
 *
 * Author:	Nicolas Pitre
 * Created:	Nov 30, 2004
 * Copyright:	MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct pxa2xx_pcm_dma_params {
	char *name;			/* stream identifier */
	u32 dcmd;			/* DMA descriptor dcmd field */
	volatile u32 *drcmr;		/* the DMA request channel to use */
	u32 dev_addr;			/* device physical address for DMA */
};
	
struct pxa2xx_pcm_client {
	struct pxa2xx_pcm_dma_params *playback_params;
	struct pxa2xx_pcm_dma_params *capture_params;
	int (*startup)(struct snd_pcm_substream *);
	void (*shutdown)(struct snd_pcm_substream *);
	int (*prepare)(struct snd_pcm_substream *);
};

extern int pxa2xx_pcm_new(struct snd_card *, struct pxa2xx_pcm_client *, struct snd_pcm **);

