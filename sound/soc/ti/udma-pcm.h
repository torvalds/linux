/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com
 */

#ifndef __UDMA_PCM_H__
#define __UDMA_PCM_H__

#if IS_ENABLED(CONFIG_SND_SOC_TI_UDMA_PCM)
int udma_pcm_platform_register(struct device *dev);
#else
static inline int udma_pcm_platform_register(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_SND_SOC_TI_UDMA_PCM */

#endif /* __UDMA_PCM_H__ */
