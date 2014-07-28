/*
 * ALSA SoC SPDIF DIT driver
 *
 *  This driver is used by controllers which can operate in DIT (SPDI/F) where
 *  no codec is needed.  This file provides stub codec that can be used
 *  in these configurations. TI DaVinci Audio controller uses this driver.
 *
 * Author:      Steve Chen,  <schen@mvista.com>
 * Copyright:   (C) 2009 MontaVista Software, Inc., <source@mvista.com>
 * Copyright:   (C) 2009  Texas Instruments, India
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#undef  DEBUG_HDMI_SPDIF
#define DEBUG_HDMI_SPDIF 0

#if DEBUG_HDMI_SPDIF
#define RK_HDMISPDIF_DBG(x...) pr_info("hdmi_spdif:"x)
#else
#define RK_HDMISPDIF_DBG(x...) do { } while (0)
#endif


#define DRV_NAME "spdif-dit"

#define STUB_RATES	SNDRV_PCM_RATE_8000_96000
#define STUB_FORMATS	SNDRV_PCM_FMTBIT_S16_LE


static struct snd_soc_codec_driver soc_codec_spdif_dit;

static struct snd_soc_dai_driver dit_stub_dai = {
	.name		= "rk-hdmi-spdif-hifi",
	.playback	= {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
};

static int hdmi_spdif_audio_probe(struct platform_device *pdev)
{
	int ret;

	/* set dev name to driver->name for sound card register. */
	dev_set_name(&pdev->dev, "%s", pdev->dev.driver->name);

	ret = snd_soc_register_codec(&pdev->
		dev, &soc_codec_spdif_dit, &dit_stub_dai, 1);

	if (ret)
		RK_HDMISPDIF_DBG("%s register codec failed:%d\n"
		, __func__, ret);

	return ret;
}

static int hdmi_spdif_audio_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id hdmi_spdif_of_match[] = {
	{ .compatible = "hdmi-spdif", },
	{},
};
MODULE_DEVICE_TABLE(of, hdmi_spdif_of_match);
#endif /* CONFIG_OF */

static struct platform_driver hdmi_spdif_audio_driver = {
	.driver	   = {
		.name           = "hdmi-spdif",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(hdmi_spdif_of_match),
	},
	.probe     = hdmi_spdif_audio_probe,
	.remove    = hdmi_spdif_audio_remove,
};

module_platform_driver(hdmi_spdif_audio_driver);

MODULE_AUTHOR("Steve Chen <schen@mvista.com>");
MODULE_DESCRIPTION("SPDIF dummy codec driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
