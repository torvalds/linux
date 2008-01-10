/*
 * e800-wm9712.c  --  SoC audio for e800
 *
 * Based on tosa.c
 *
 * Copyright 2007 (c) Ian Molton <spyro@f2s.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation; version 2 ONLY.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/hardware.h>
#include <asm/arch/audio.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-ac97.h"

static struct snd_soc_machine e800;

static struct snd_soc_dai_link e800_dai[] = {
{
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
	.codec_dai = &wm9712_dai[WM9712_DAI_AC97_AUX],
},
};

static struct snd_soc_machine e800 = {
	.name = "Toshiba e800",
	.dai_link = e800_dai,
	.num_links = ARRAY_SIZE(e800_dai),
};

static struct snd_soc_device e800_snd_devdata = {
	.machine = &e800,
	.platform = &pxa2xx_soc_platform,
	.codec_dev = &soc_codec_dev_wm9712,
};

static struct platform_device *e800_snd_device;

static int __init e800_init(void)
{
	int ret;

	if (!machine_is_e800())
		return -ENODEV;

	e800_snd_device = platform_device_alloc("soc-audio", -1);
	if (!e800_snd_device)
		return -ENOMEM;

	platform_set_drvdata(e800_snd_device, &e800_snd_devdata);
	e800_snd_devdata.dev = &e800_snd_device->dev;
	ret = platform_device_add(e800_snd_device);

	if (ret)
		platform_device_put(e800_snd_device);

	return ret;
}

static void __exit e800_exit(void)
{
	platform_device_unregister(e800_snd_device);
}

module_init(e800_init);
module_exit(e800_exit);

/* Module information */
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("ALSA SoC driver for e800");
MODULE_LICENSE("GPL");
