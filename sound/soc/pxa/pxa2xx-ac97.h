/*
 * linux/sound/arm/pxa2xx-ac97.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _PXA2XX_AC97_H
#define _PXA2XX_AC97_H

/* pxa2xx DAI ID's */
#define PXA2XX_DAI_AC97_HIFI	0
#define PXA2XX_DAI_AC97_AUX		1
#define PXA2XX_DAI_AC97_MIC		2

extern struct snd_soc_cpu_dai pxa_ac97_dai[3];

/* platform data */
extern struct snd_ac97_bus_ops pxa2xx_ac97_ops;

#endif
