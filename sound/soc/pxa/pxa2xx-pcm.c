// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/sound/arm/pxa2xx-pcm.c -- ALSA PCM interface for the Intel PXA2xx chip
 *
 * Author:	Nicolas Pitre
 * Created:	Nov 30, 2004
 * Copyright:	(C) 2004 MontaVista Software, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/of.h>

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pxa2xx-lib.h>
#include <sound/dmaengine_pcm.h>

static const struct snd_soc_component_driver pxa2xx_soc_platform = {
	.pcm_construct	= pxa2xx_soc_pcm_new,
	.pcm_destruct	= pxa2xx_soc_pcm_free,
	.open		= pxa2xx_soc_pcm_open,
	.close		= pxa2xx_soc_pcm_close,
	.hw_params	= pxa2xx_soc_pcm_hw_params,
	.hw_free	= pxa2xx_soc_pcm_hw_free,
	.prepare	= pxa2xx_soc_pcm_prepare,
	.trigger	= pxa2xx_soc_pcm_trigger,
	.pointer	= pxa2xx_soc_pcm_pointer,
	.mmap		= pxa2xx_soc_pcm_mmap,
};

static int pxa2xx_soc_platform_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev, &pxa2xx_soc_platform,
					       NULL, 0);
}

static struct platform_driver pxa_pcm_driver = {
	.driver = {
		.name = "pxa-pcm-audio",
	},

	.probe = pxa2xx_soc_platform_probe,
};

module_platform_driver(pxa_pcm_driver);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("Intel PXA2xx PCM DMA module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa-pcm-audio");
