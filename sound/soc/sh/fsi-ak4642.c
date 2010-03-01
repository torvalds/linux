/*
 * FSI-AK464x sound support for ms7724se
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <sound/sh_fsi.h>
#include <../sound/soc/codecs/ak4642.h>

static struct snd_soc_dai_link fsi_dai_link = {
	.name		= "AK4642",
	.stream_name	= "AK4642",
	.cpu_dai	= &fsi_soc_dai[0], /* fsi */
	.codec_dai	= &ak4642_dai,
	.ops		= NULL,
};

static struct snd_soc_card fsi_soc_card  = {
	.name		= "FSI",
	.platform	= &fsi_soc_platform,
	.dai_link	= &fsi_dai_link,
	.num_links	= 1,
};

static struct snd_soc_device fsi_snd_devdata = {
	.card		= &fsi_soc_card,
	.codec_dev	= &soc_codec_dev_ak4642,
};

static struct platform_device *fsi_snd_device;

static int __init fsi_ak4642_init(void)
{
	int ret = -ENOMEM;

	fsi_snd_device = platform_device_alloc("soc-audio", -1);
	if (!fsi_snd_device)
		goto out;

	platform_set_drvdata(fsi_snd_device,
			     &fsi_snd_devdata);
	fsi_snd_devdata.dev = &fsi_snd_device->dev;
	ret = platform_device_add(fsi_snd_device);

	if (ret)
		platform_device_put(fsi_snd_device);

out:
	return ret;
}

static void __exit fsi_ak4642_exit(void)
{
	platform_device_unregister(fsi_snd_device);
}

module_init(fsi_ak4642_init);
module_exit(fsi_ak4642_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic SH4 FSI-AK4642 sound card");
MODULE_AUTHOR("Kuninori Morimoto <morimoto.kuninori@renesas.com>");
