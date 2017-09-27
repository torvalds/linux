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
#include <asm/mach-types.h>

#include "imx-audmux.h"

static struct snd_soc_card imx_phycore;

static const struct snd_soc_ops imx_phycore_hifi_ops = {
};

static struct snd_soc_dai_link imx_phycore_dai_ac97[] = {
	{
		.name		= "HiFi",
		.stream_name	= "HiFi",
		.codec_dai_name		= "wm9712-hifi",
		.codec_name	= "wm9712-codec",
		.cpu_dai_name	= "imx-ssi.0",
		.platform_name	= "imx-ssi.0",
		.ops		= &imx_phycore_hifi_ops,
	},
};

static struct snd_soc_card imx_phycore = {
	.name		= "PhyCORE-ac97-audio",
	.owner		= THIS_MODULE,
	.dai_link	= imx_phycore_dai_ac97,
	.num_links	= ARRAY_SIZE(imx_phycore_dai_ac97),
};

static struct platform_device *imx_phycore_snd_ac97_device;
static struct platform_device *imx_phycore_snd_device;

static int __init imx_phycore_init(void)
{
	int ret;

	if (machine_is_pca100()) {
		imx_audmux_v1_configure_port(MX27_AUDMUX_HPCR1_SSI0,
			IMX_AUDMUX_V1_PCR_SYN | /* 4wire mode */
			IMX_AUDMUX_V1_PCR_TFCSEL(3) |
			IMX_AUDMUX_V1_PCR_TCLKDIR | /* clock is output */
			IMX_AUDMUX_V1_PCR_RXDSEL(3));
		imx_audmux_v1_configure_port(3,
			IMX_AUDMUX_V1_PCR_SYN | /* 4wire mode */
			IMX_AUDMUX_V1_PCR_TFCSEL(0) |
			IMX_AUDMUX_V1_PCR_TFSDIR |
			IMX_AUDMUX_V1_PCR_RXDSEL(0));
	} else if (machine_is_pcm043()) {
		imx_audmux_v2_configure_port(3,
			IMX_AUDMUX_V2_PTCR_SYN | /* 4wire mode */
			IMX_AUDMUX_V2_PTCR_TFSEL(0) |
			IMX_AUDMUX_V2_PTCR_TFSDIR,
			IMX_AUDMUX_V2_PDCR_RXDSEL(0));
		imx_audmux_v2_configure_port(0,
			IMX_AUDMUX_V2_PTCR_SYN | /* 4wire mode */
			IMX_AUDMUX_V2_PTCR_TCSEL(3) |
			IMX_AUDMUX_V2_PTCR_TCLKDIR, /* clock is output */
			IMX_AUDMUX_V2_PDCR_RXDSEL(3));
	} else {
		/* return happy. We might run on a totally different machine */
		return 0;
	}

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
