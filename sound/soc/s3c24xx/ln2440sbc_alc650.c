/*
 * SoC audio for ln2440sbc
 *
 * Copyright 2007 KonekTel, a.s.
 * Author: Ivan Kuten
 *         ivan.kuten@promwad.com
 *
 * Heavily based on smdk2443_wm9710.c
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "../codecs/ac97.h"
#include "s3c24xx-pcm.h"
#include "s3c24xx-ac97.h"

static struct snd_soc_card ln2440sbc;

static struct snd_soc_dai_link ln2440sbc_dai[] = {
{
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	.cpu_dai = &s3c2443_ac97_dai[0],
	.codec_dai = &ac97_dai,
},
};

static struct snd_soc_card ln2440sbc = {
	.name = "LN2440SBC",
	.platform = &s3c24xx_soc_platform,
	.dai_link = ln2440sbc_dai,
	.num_links = ARRAY_SIZE(ln2440sbc_dai),
};

static struct snd_soc_device ln2440sbc_snd_ac97_devdata = {
	.card = &ln2440sbc,
	.codec_dev = &soc_codec_dev_ac97,
};

static struct platform_device *ln2440sbc_snd_ac97_device;

static int __init ln2440sbc_init(void)
{
	int ret;

	ln2440sbc_snd_ac97_device = platform_device_alloc("soc-audio", -1);
	if (!ln2440sbc_snd_ac97_device)
		return -ENOMEM;

	platform_set_drvdata(ln2440sbc_snd_ac97_device,
				&ln2440sbc_snd_ac97_devdata);
	ln2440sbc_snd_ac97_devdata.dev = &ln2440sbc_snd_ac97_device->dev;
	ret = platform_device_add(ln2440sbc_snd_ac97_device);

	if (ret)
		platform_device_put(ln2440sbc_snd_ac97_device);

	return ret;
}

static void __exit ln2440sbc_exit(void)
{
	platform_device_unregister(ln2440sbc_snd_ac97_device);
}

module_init(ln2440sbc_init);
module_exit(ln2440sbc_exit);

/* Module information */
MODULE_AUTHOR("Ivan Kuten");
MODULE_DESCRIPTION("ALSA SoC ALC650 LN2440SBC");
MODULE_LICENSE("GPL");
