/*
 *  dma-wrapper.c  --  I2S DMA Platform Wrapper Driver
 *
 *  * Copyright (c) 2010 Samsung Electronics Co. Ltd
 *
 *    This program is free software; you can redistribute  it and/or modify it
 *    under  the terms of  the GNU General  Public License as published by the
 *    Free Software Foundation;  either version 2 of the  License, or (at your
 *    option) any later version.
 */

#include <sound/soc.h>
#include "dma.h"
#include "idma.h"

#ifdef CONFIG_SND_SOC_SAMSUNG_USE_DMA_WRAPPER
static struct snd_soc_platform_driver *asoc_get_platform(struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	const char *cpu_dai_name = rtd->cpu_dai->name;

	if (!strcmp(cpu_dai_name, "samsung-i2s.4"))
		return &samsung_asoc_idma_platform;
	else
		return &samsung_asoc_platform;
}

static int asoc_platform_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_platform_driver *platform =
					asoc_get_platform(substream->pcm);

	if (platform->ops->hw_params)
		return platform->ops->hw_params(substream, params);
	else
		return 0;
}

static int asoc_platform_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_platform_driver *platform =
					asoc_get_platform(substream->pcm);

	if (platform->ops->hw_free)
		return platform->ops->hw_free(substream);
	else
		return 0;
}

static int asoc_platform_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_platform_driver *platform =
					asoc_get_platform(substream->pcm);

	if (platform->ops->prepare)
		return platform->ops->prepare(substream);
	else
		return 0;
}

static int asoc_platform_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_platform_driver *platform =
					asoc_get_platform(substream->pcm);

	if (platform->ops->trigger)
		return platform->ops->trigger(substream, cmd);
	else
		return 0;
}

static snd_pcm_uframes_t
asoc_platform_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_platform_driver *platform =
					asoc_get_platform(substream->pcm);

	if (platform->ops->pointer)
		return platform->ops->pointer(substream);
	else
		return 0;
}

static int asoc_platform_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_platform_driver *platform =
					asoc_get_platform(substream->pcm);

	if (platform->ops->open)
		return platform->ops->open(substream);
	else
		return 0;
}

static int asoc_platform_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_platform_driver *platform =
					asoc_get_platform(substream->pcm);

	if (platform->ops->close)
		return platform->ops->close(substream);
	else
		return 0;
}

static int asoc_platform_ioctl(struct snd_pcm_substream *substream,
				unsigned int cmd, void *arg)
{
	struct snd_soc_platform_driver *platform =
					asoc_get_platform(substream->pcm);

	if (platform->ops->ioctl)
		return platform->ops->ioctl(substream, cmd, arg);
	else
		return 0;
}

static int asoc_platform_mmap(struct snd_pcm_substream *substream,
				struct vm_area_struct *vma)
{
	struct snd_soc_platform_driver *platform =
					asoc_get_platform(substream->pcm);

	if (platform->ops->mmap)
		return platform->ops->mmap(substream, vma);
	else
		return 0;
}

static struct snd_pcm_ops asoc_platform_ops = {
	.open		= asoc_platform_open,
	.close		= asoc_platform_close,
	.ioctl		= asoc_platform_ioctl,
	.hw_params	= asoc_platform_hw_params,
	.hw_free	= asoc_platform_hw_free,
	.prepare	= asoc_platform_prepare,
	.trigger	= asoc_platform_trigger,
	.pointer	= asoc_platform_pointer,
	.mmap		= asoc_platform_mmap,
};

static void asoc_platform_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_soc_platform_driver *platform = asoc_get_platform(pcm);

	if (platform->pcm_free)
		platform->pcm_free(pcm);
}

static int asoc_platform_new(struct snd_card *card,
				struct snd_soc_dai *dai, struct snd_pcm *pcm)
{
	struct snd_soc_platform_driver *platform = asoc_get_platform(pcm);

	if (platform->pcm_new)
		platform->pcm_new(card, dai, pcm);

	return 0;
}

static struct snd_soc_platform_driver asoc_dma_platform = {
	.ops		= &asoc_platform_ops,
	.pcm_new	= asoc_platform_new,
	.pcm_free	= asoc_platform_free_dma_buffers,
};

static int __devinit samsung_asoc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &asoc_dma_platform);
}

static int __devexit samsung_asoc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver asoc_platform_driver = {
	.driver = {
		.name = "samsung-audio",
		.owner = THIS_MODULE,
	},

	.probe = samsung_asoc_platform_probe,
	.remove = __devexit_p(samsung_asoc_platform_remove),
};

static int __init samsung_asoc_init(void)
{
	return platform_driver_register(&asoc_platform_driver);
}
module_init(samsung_asoc_init);

static void __exit samsung_asoc_exit(void)
{
	platform_driver_unregister(&asoc_platform_driver);
}
module_exit(samsung_asoc_exit);
#endif

MODULE_DESCRIPTION("Samsung ASoC DMA wrapper");
MODULE_LICENSE("GPL");
