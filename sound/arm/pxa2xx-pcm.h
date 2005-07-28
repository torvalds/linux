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

typedef struct {
	char *name;			/* stream identifier */
	u32 dcmd;			/* DMA descriptor dcmd field */
	volatile u32 *drcmr;		/* the DMA request channel to use */
	u32 dev_addr;			/* device physical address for DMA */
} pxa2xx_pcm_dma_params_t;
	
typedef struct {
	pxa2xx_pcm_dma_params_t *playback_params;
	pxa2xx_pcm_dma_params_t *capture_params;
	int (*startup)(snd_pcm_substream_t *);
	void (*shutdown)(snd_pcm_substream_t *);
	int (*prepare)(snd_pcm_substream_t *);
} pxa2xx_pcm_client_t;

extern int pxa2xx_pcm_new(snd_card_t *, pxa2xx_pcm_client_t *, snd_pcm_t **);

