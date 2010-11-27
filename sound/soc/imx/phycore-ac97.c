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

static struct snd_soc_card imx_phycore;

static struct snd_soc_ops imx_phycore_hifi_ops = {
};

static struct snd_soc_dai_link imx_phycore_dai_ac97[] = {
	{
		.name		= "HiFi",
		.stream_name	= "HiFi",
		.codec_dai_name		= "wm9712-hifi",
		.codec_name	= "wm9712-codec",
		.cpu_dai_name	= "imx-ssi.0",
		.platform_name	= "imx-fiq-pcm-audio.0",
		.ops		= &imx_phycore_hifi_ops,
	},
};

static struct snd_soc_card imx_phycore = {
	.name		= "PhyCORE-ac97-audio",
	.dai_link	= imx_phycore_dai_ac97,
	.num_links	= ARRAY_SIZE(imx_phycore_dai_ac97),
};

static struct platform_device *imx_phycore_snd_ac97_device;
static struct platform_device *imx_phycore_snd_device;

static int __init imx_phycore_init(void)
{
	int ret;

	if (!machine_is_pcm043() && !machine_is_pca100())
		/* return happy. We might run on a totally different machine */
		return 0;

	imx_phycore_snd_ac97_device = platform_device_alloc("soc-audio", -1);
	if (!imx_phycore_snd_ac97_device)
		return -ENOMEM;

	platform_set_drvdata(imx_phycore_snd_ac97_device, &imx_phycore);
	ret = platform_device_add(imx_phycore_snd_ac97_device);
	if (ret)
		goto fail1;

	imx_phycore_snd_device = platform_device_alloc("wm9712-codec", -1);
	if (!imx_phycore_snd_device) {
		ret = -ENOMEM;
		goto fail2;
	}
	ret = platform_device_add(imx_phycore_snd_device);

	if (ret) {
		printk(KERN_ERR "ASoC: Platform device allocation failed\n");
		goto fail3;
	}

	return 0;

fail3:
	platform_device_put(imx_phycore_snd_device);
fail2:
	platform_device_del(imx_phycore_snd_ac97_device);
fail1:
	platform_device_put(imx_phycore_snd_ac97_device);
	return ret;
}

static void __exit imx_phycore_exit(void)
{
	platform_device_unregister(imx_phycore_snd_device);
	platform_device_unregister(imx_phycore_snd_ac97_device);
}

late_initcall(imx_phycore_init);
module_exit(imx_phycore_exit);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("PhyCORE ALSA SoC driver");
MODULE_LICENSE("GPL");
