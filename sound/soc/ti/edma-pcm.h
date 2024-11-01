/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * edma-pcm.h - eDMA PCM driver using dmaengine for AM3xxx, AM4xxx
 *
 * Copyright (C) 2014 Texas Instruments, Inc.
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * Based on: sound/soc/tegra/tegra_pcm.h
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
