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

#ifndef _PXA2XX_PCM_H
#define _PXA2XX_PCM_H

struct pxa2xx_pcm_dma_params {
	char *name;			/* stream identifier */
	u32 dcmd;			/* DMA descriptor dcmd field */
	volatile u32 *drcmr;		/* the DMA request channel to use */
	u32 dev_addr;			/* device physical address for DMA */
};

struct pxa2xx_gpio {
	u32 sys;
	u32	rx;
	u32 tx;
	u32 clk;
	u32 frm;
};

/* pxa2xx DAI ID's */
#define PXA2XX_DAI_AC97_HIFI	0
#define PXA2XX_DAI_AC97_AUX		1
#define PXA2XX_DAI_AC97_MIC		2
#define PXA2XX_DAI_I2S			0
#define PXA2XX_DAI_SSP1			0
#define PXA2XX_DAI_SSP2			1
#define PXA2XX_DAI_SSP3			2

extern struct snd_soc_cpu_dai pxa_ac97_dai[3];
extern struct snd_soc_cpu_dai pxa_i2s_dai;
extern struct snd_soc_cpu_dai pxa_ssp_dai[3];

/* platform data */
extern struct snd_soc_platform pxa2xx_soc_platform;
extern struct snd_ac97_bus_ops pxa2xx_ac97_ops;

#endif
