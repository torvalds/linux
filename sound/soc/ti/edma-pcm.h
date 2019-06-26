/*
 * edma-pcm.h - eDMA PCM driver using dmaengine for AM3xxx, AM4xxx
 *
 * Copyright (C) 2014 Texas Instruments, Inc.
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * Based on: sound/soc/tegra/tegra_pcm.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __EDMA_PCM_H__
#define __EDMA_PCM_H__

#if IS_ENABLED(CONFIG_SND_SOC_TI_EDMA_PCM)
int edma_pcm_platform_register(struct device *dev);
#else
static inline int edma_pcm_platform_register(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_SND_SOC_TI_EDMA_PCM */

#endif /* __EDMA_PCM_H__ */
