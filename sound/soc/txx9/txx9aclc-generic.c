/*
 * Generic TXx9 ACLC machine driver
 *
 * Copyright (C) 2009 Atsushi Nemoto
 *
 * Based on RBTX49xx patch from CELF patch archive.
 * (C) Copyright TOSHIBA CORPORATION 2004-2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This is a very generic AC97 sound machine driver for boards which
 * have (AC97) audio at ACLC (e.g. RBTX49XX boards).
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include "txx9aclc.h"

static struct snd_soc_dai_link txx9aclc_generic_dai = {
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	.cpu_dai_name = "txx9aclc-ac97",
	.codec_dai_name = "ac97-hifi",
	.platform_name	= "txx9aclc-pcm-audio",
	.codec_name	= "ac97-codec",
};

static struct snd_soc_card txx9aclc_generic_card = {
	.name		= "Generic TXx9 ACLC Audio",
	.owner		= THIS_MODULE,
	.dai_link	= &txx9aclc_generic_dai,
	.num_links	= 1,
};

static struct platform_device *soc_pdev;

static int __init txx9aclc_generic_probe(struct platform_device *pdev)
{
	int ret;

	soc_pdev = platform_device_alloc("soc-audio", -1);
	if (!soc_pdev)
		return -ENOMEM;
	platform_set_drvdata(soc_pdev, &txx9aclc_generic_card);
	ret = platform_device_add(soc_pdev);
	if (ret) {
		platform_device_put(soc_pdev);
		return ret;
	}

	return 0;
}

static int __exit txx9aclc_generic_remove(struct platform_device *pdev)
{
	platform_device_unregister(soc_pdev);
	return 0;
}

static struct platform_driver txx9aclc_generic_driver = {
	.remove = __exit_p(txx9aclc_generic_remove),
	.driver = {
		.name = "txx9aclc-generic",
	},
};

static int __init txx9aclc_generic_init(void)
{
	return platform_driver_probe(&txx9aclc_generic_driver,
				     txx9aclc_generic_probe);
}

static void __exit txx9aclc_generic_exit(void)
{
	platform_driver_unregister(&txx9aclc_generic_driver);
}

module_init(txx9aclc_generic_init);
module_exit(txx9aclc_generic_exit);

MODULE_AUTHOR("Atsushi Nemoto <anemo@mba.ocn.ne.jp>");
MODULE_DESCRIPTION("Generic TXx9 ACLC ALSA SoC audio driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:txx9aclc-generic");
