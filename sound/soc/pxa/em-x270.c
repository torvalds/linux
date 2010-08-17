/*
 * SoC audio driver for EM-X270, eXeda and CM-X300
 *
 * Copyright 2007, 2009 CompuLab, Ltd.
 *
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Copied from tosa.c:
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Authors: Liam Girdwood <lrg@slimlogic.co.uk>
 *          Richard Purdie <richard@openedhand.com>
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

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <mach/audio.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-ac97.h"

static struct snd_soc_dai_link em_x270_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "pxa-ac97.0",
		.codec_dai_name = "wm9712-hifi",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9712-codec",
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai_name = "pxa-ac97.1",
		.codec_dai_name ="wm9712-aux",
		.platform_name = "pxa-pcm-audio",
		.codec_name = "wm9712-codec",
	},
};

static struct snd_soc_card em_x270 = {
	.name = "EM-X270",
	.dai_link = em_x270_dai,
	.num_links = ARRAY_SIZE(em_x270_dai),
};

static struct platform_device *em_x270_snd_device;

static int __init em_x270_init(void)
{
	int ret;

	if (!(machine_is_em_x270() || machine_is_exeda()
	      || machine_is_cm_x300()))
		return -ENODEV;

	em_x270_snd_device = platform_device_alloc("soc-audio", -1);
	if (!em_x270_snd_device)
		return -ENOMEM;

	platform_set_drvdata(em_x270_snd_device, &em_x270);
	ret = platform_device_add(em_x270_snd_device);

	if (ret)
		platform_device_put(em_x270_snd_device);

	return ret;
}

static void __exit em_x270_exit(void)
{
	platform_device_unregister(em_x270_snd_device);
}

module_init(em_x270_init);
module_exit(em_x270_exit);

/* Module information */
MODULE_AUTHOR("Mike Rapoport");
MODULE_DESCRIPTION("ALSA SoC EM-X270, eXeda and CM-X300");
MODULE_LICENSE("GPL");
