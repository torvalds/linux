/*
 * File:         sound/soc/blackfin/bf5xx-ad1836.c
 * Author:       Barry Song <Barry.Song@analog.com>
 *
 * Created:      Aug 4 2009
 * Description:  Board driver for ad1836 sound chip
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <asm/blackfin.h>
#include <asm/cacheflush.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/portmux.h>

#include "../codecs/ad1836.h"

#include "bf5xx-tdm-pcm.h"
#include "bf5xx-tdm.h"

static struct snd_soc_card bf5xx_ad1836;

static int bf5xx_ad1836_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int channel_map[] = {0, 4, 1, 5, 2, 6, 3, 7};
	int ret = 0;

	/* set cpu DAI channel mapping */
	ret = snd_soc_dai_set_channel_map(cpu_dai, ARRAY_SIZE(channel_map),
		channel_map, ARRAY_SIZE(channel_map), channel_map);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops bf5xx_ad1836_ops = {
	.hw_params = bf5xx_ad1836_hw_params,
};

#define BF5XX_AD1836_DAIFMT (SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_IF | \
				SND_SOC_DAIFMT_CBM_CFM)

static struct snd_soc_dai_link bf5xx_ad1836_dai[] = {
	{
		.name = "ad1836",
		.stream_name = "AD1836",
		.cpu_dai_name = "bfin-tdm.0",
		.codec_dai_name = "ad1836-hifi",
		.platform_name = "bfin-tdm-pcm-audio",
		.codec_name = "spi0.4",
		.ops = &bf5xx_ad1836_ops,
		.dai_fmt = BF5XX_AD1836_DAIFMT,
	},
	{
		.name = "ad1836",
		.stream_name = "AD1836",
		.cpu_dai_name = "bfin-tdm.1",
		.codec_dai_name = "ad1836-hifi",
		.platform_name = "bfin-tdm-pcm-audio",
		.codec_name = "spi0.4",
		.ops = &bf5xx_ad1836_ops,
		.dai_fmt = BF5XX_AD1836_DAIFMT,
	},
};

static struct snd_soc_card bf5xx_ad1836 = {
	.name = "bfin-ad1836",
	.owner = THIS_MODULE,
	.dai_link = &bf5xx_ad1836_dai[CONFIG_SND_BF5XX_SPORT_NUM],
	.num_links = 1,
};

static struct platform_device *bfxx_ad1836_snd_device;

static int __init bf5xx_ad1836_init(void)
{
	int ret;

	bfxx_ad1836_snd_device = platform_device_alloc("soc-audio", -1);
	if (!bfxx_ad1836_snd_device)
		return -ENOMEM;

	platform_set_drvdata(bfxx_ad1836_snd_device, &bf5xx_ad1836);
	ret = platform_device_add(bfxx_ad1836_snd_device);

	if (ret)
		platform_device_put(bfxx_ad1836_snd_device);

	return ret;
}

static void __exit bf5xx_ad1836_exit(void)
{
	platform_device_unregister(bfxx_ad1836_snd_device);
}

module_init(bf5xx_ad1836_init);
module_exit(bf5xx_ad1836_exit);

/* Module information */
MODULE_AUTHOR("Barry Song");
MODULE_DESCRIPTION("ALSA SoC AD1836 board driver");
MODULE_LICENSE("GPL");

