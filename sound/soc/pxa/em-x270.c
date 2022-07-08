// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <linux/platform_data/asoc-pxa.h>

SND_SOC_DAILINK_DEFS(ac97,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-ac97")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9712-codec", "wm9712-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

SND_SOC_DAILINK_DEFS(ac97_aux,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-ac97-aux")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9712-codec", "wm9712-aux")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

static struct snd_soc_dai_link em_x270_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		SND_SOC_DAILINK_REG(ac97),
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		SND_SOC_DAILINK_REG(ac97_aux),
	},
};

static struct snd_soc_card em_x270 = {
	.name = "EM-X270",
	.owner = THIS_MODULE,
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
