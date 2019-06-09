// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include "nuc900-audio.h"

static struct snd_soc_dai_link nuc900evb_ac97_dai = {
	.name		= "AC97",
	.stream_name	= "AC97 HiFi",
	.cpu_dai_name	= "nuc900-ac97",
	.codec_dai_name	= "ac97-hifi",
	.codec_name	= "ac97-codec",
	.platform_name	= "nuc900-pcm-audio",
};

static struct snd_soc_card nuc900evb_audio_machine = {
	.name		= "NUC900EVB_AC97",
	.owner		= THIS_MODULE,
	.dai_link	= &nuc900evb_ac97_dai,
	.num_links	= 1,
};

static struct platform_device *nuc900evb_asoc_dev;

static int __init nuc900evb_audio_init(void)
{
	int ret;

	ret = -ENOMEM;
	nuc900evb_asoc_dev = platform_device_alloc("soc-audio", -1);
	if (!nuc900evb_asoc_dev)
		goto out;

	/* nuc900 board audio device */
	platform_set_drvdata(nuc900evb_asoc_dev, &nuc900evb_audio_machine);

	ret = platform_device_add(nuc900evb_asoc_dev);

	if (ret) {
		platform_device_put(nuc900evb_asoc_dev);
		nuc900evb_asoc_dev = NULL;
	}

out:
	return ret;
}

static void __exit nuc900evb_audio_exit(void)
{
	platform_device_unregister(nuc900evb_asoc_dev);
}

module_init(nuc900evb_audio_init);
module_exit(nuc900evb_audio_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NUC900 Series ASoC audio support");
MODULE_AUTHOR("Wan ZongShun");
