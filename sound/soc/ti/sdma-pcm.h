/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com
 *  Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#ifndef __SDMA_PCM_H__
#define __SDMA_PCM_H__

#if IS_ENABLED(CONFIG_SND_SOC_TI_SDMA_PCM)
int sdma_pcm_platform_register(struct device *dev,
			       char *txdmachan, char *rxdmachan);
#else
static inline int sdma_pcm_platform_register(struct device *dev,
					     char *txdmachan, char *rxdmachan)
{
	return -ENODEV;
}
#endif /* CONFIG_SND_SOC_TI_SDMA_PCM */

#endif /* __SDMA_PCM_H__ */
