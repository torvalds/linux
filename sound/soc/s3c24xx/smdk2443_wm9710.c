/*
 * smdk2443_wm9710.c  --  SoC audio for smdk2443
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "../codecs/ac97.h"
#include "s3c-dma.h"
#include "s3c24xx-ac97.h"

static struct snd_soc_card smdk2443;

static struct snd_soc_dai_link smdk2443_dai[] = {
{
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	.cpu_dai = &s3c2443_ac97_dai[0],
	.codec_dai = &ac97_dai,
},
};

static struct snd_soc_card smdk2443 = {
	.name = "SMDK2443",
	.platform = &s3c24xx_soc_platform,
	.dai_link = smdk2443_dai,
	.num_links = ARRAY_SIZE(smdk2443_dai),
};

static struct snd_soc_device smdk2443_snd_ac97_devdata = {
	.card = &smdk2443,
	.codec_dev = &soc_codec_dev_ac97,
};

static struct platform_device *smdk2443_snd_ac97_device;

static int __init smdk2443_init(void)
{
	int ret;

	smdk2443_snd_ac97_device = platform_device_alloc("soc-audio", -1);
	if (!smdk2443_snd_ac97_device)
		return -ENOMEM;

	platform_set_drvdata(smdk2443_snd_ac97_device,
				&smdk2443_snd_ac97_devdata);
	smdk2443_snd_ac97_devdata.dev = &smdk2443_snd_ac97_device->dev;
	ret = platform_device_add(smdk2443_snd_ac97_device);

	if (ret)
		platform_device_put(smdk2443_snd_ac97_device);

	return ret;
}

static void __exit smdk2443_exit(void)
{
	platform_device_unregister(smdk2443_snd_ac97_device);
}

module_init(smdk2443_init);
module_exit(smdk2443_exit);

/* Module information */
MODULE_AUTHOR("Graeme Gregory, graeme.gregory@wolfsonmicro.com, www.wolfsonmicro.com");
MODULE_DESCRIPTION("ALSA SoC WM9710 SMDK2443");
MODULE_LICENSE("GPL");
