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

static struct snd_soc_card bf5xx_ad1836;

static int bf5xx_ad1836_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int channel_map[] = {0, 4, 1, 5, 2, 6, 3, 7};
	int ret = 0;

	/* set cpu DAI channel mapping */
	ret = snd_soc_dai_set_channel_map(cpu_dai, ARRAY_SIZE(channel_map),
		channel_map, ARRAY_SIZE(channel_map), channel_map);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0xFF, 0xFF, 8, 32);
	if (ret < 0)
		return ret;

	return 0;
}

#define BF5XX_AD1836_DAIFMT (SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_IF | \
				SND_SOC_DAIFMT_CBM_CFM)

static struct snd_soc_dai_link bf5xx_ad1836_dai = {
	.name = "ad1836",
	.stream_name = "AD1836",
	.codec_dai_name = "ad1836-hifi",
	.platform_name = "bfin-i2s-pcm-audio",
	.dai_fmt = BF5XX_AD1836_DAIFMT,
	.init = bf5xx_ad1836_init,
};

static struct snd_soc_card bf5xx_ad1836 = {
	.name = "bfin-ad1836",
	.owner = THIS_MODULE,
	.dai_link = &bf5xx_ad1836_dai,
	.num_links = 1,
};

static int bf5xx_ad1836_driver_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &bf5xx_ad1836;
	const char **link_name;
	int ret;

	link_name = pdev->dev.platform_data;
	if (!link_name) {
		dev_err(&pdev->dev, "No platform data supplied\n");
		return -EINVAL;
	}
	bf5xx_ad1836_dai.cpu_dai_name = link_name[0];
	bf5xx_ad1836_dai.codec_name = link_name[1];

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "Failed to register card\n");
	return ret;
}

static int bf5xx_ad1836_driver_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver bf5xx_ad1836_driver = {
	.driver = {
		.name = "bfin-snd-ad1836",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = bf5xx_ad1836_driver_probe,
	.remove = bf5xx_ad1836_driver_remove,
};
module_platform_driver(bf5xx_ad1836_driver);

/* Module information */
MODULE_AUTHOR("Barry Song");
MODULE_DESCRIPTION("ALSA SoC AD1836 board driver");
MODULE_LICENSE("GPL");

