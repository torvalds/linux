/*
 * phycore-ac97.c  --  SoC audio for imx_phycore in AC97 mode
 *
 * Copyright 2009 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <asm/mach-types.h>

#include "../codecs/wm9712.h"
#include "imx-ssi.h"

static struct snd_soc_card imx_phycore;

static struct snd_soc_ops imx_phycore_hifi_ops = {
};

static struct snd_soc_dai_link imx_phycore_dai_ac97[] = {
	{
		.name		= "HiFi",
		.stream_name	= "HiFi",
		.codec_dai	= &wm9712_dai[WM9712_DAI_AC97_HIFI],
		.ops		= &imx_phycore_hifi_ops,
	},
};

static struct snd_soc_card imx_phycore = {
	.name		= "PhyCORE-audio",
	.platform	= &imx_soc_platform,
	.dai_link	= imx_phycore_dai_ac97,
	.num_links	= ARRAY_SIZE(imx_phycore_dai_ac97),
};

static struct snd_soc_device imx_phycore_snd_devdata = {
	.card		= &imx_phycore,
	.codec_dev	= &soc_codec_dev_wm9712,
};

static struct platform_device *imx_phycore_snd_device;

static int __init imx_phycore_init(void)
{
	int ret;

	if (!machine_is_pcm043() && !machine_is_pca100())
		/* return happy. We might run on a totally different machine */
		return 0;

	imx_phycore_snd_device = platform_device_alloc("soc-audio", -1);
	if (!imx_phycore_snd_device)
		return -ENOMEM;

	imx_phycore_dai_ac97[0].cpu_dai = &imx_ssi_pcm_dai[0];

	platform_set_drvdata(imx_phycore_snd_device, &imx_phycore_snd_devdata);
	imx_phycore_snd_devdata.dev = &imx_phycore_snd_device->dev;
	ret = platform_device_add(imx_phycore_snd_device);

	if (ret) {
		printk(KERN_ERR "ASoC: Platform device allocation failed\n");
		platform_device_put(imx_phycore_snd_device);
	}

	return ret;
}

static void __exit imx_phycore_exit(void)
{
	platform_device_unregister(imx_phycore_snd_device);
}

late_initcall(imx_phycore_init);
module_exit(imx_phycore_exit);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("PhyCORE ALSA SoC driver");
MODULE_LICENSE("GPL");
