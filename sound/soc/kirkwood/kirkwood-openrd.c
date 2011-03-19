/*
 * kirkwood-openrd.c
 *
 * (c) 2010 Arnaud Patard <apatard@mandriva.com>
 * (c) 2010 Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <mach/kirkwood.h>
#include <plat/audio.h>
#include <asm/mach-types.h>
#include "../codecs/cs42l51.h"

static int openrd_client_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;
	unsigned int freq, fmt;

	fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS;
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0)
		return ret;

	switch (params_rate(params)) {
	default:
	case 44100:
		freq = 11289600;
		break;
	case 48000:
		freq = 12288000;
		break;
	case 96000:
		freq = 24576000;
		break;
	}

	return snd_soc_dai_set_sysclk(codec_dai, 0, freq, SND_SOC_CLOCK_IN);

}

static struct snd_soc_ops openrd_client_ops = {
	.hw_params = openrd_client_hw_params,
};


static struct snd_soc_dai_link openrd_client_dai[] = {
{
	.name = "CS42L51",
	.stream_name = "CS42L51 HiFi",
	.cpu_dai_name = "kirkwood-i2s",
	.platform_name = "kirkwood-pcm-audio",
	.codec_dai_name = "cs42l51-hifi",
	.codec_name = "cs42l51-codec.0-004a",
	.ops = &openrd_client_ops,
},
};


static struct snd_soc_card openrd_client = {
	.name = "OpenRD Client",
	.dai_link = openrd_client_dai,
	.num_links = ARRAY_SIZE(openrd_client_dai),
};

static struct platform_device *openrd_client_snd_device;

static int __init openrd_client_init(void)
{
	int ret;

	if (!machine_is_openrd_client() && !machine_is_openrd_ultimate())
		return 0;

	openrd_client_snd_device = platform_device_alloc("soc-audio", -1);
	if (!openrd_client_snd_device)
		return -ENOMEM;

	platform_set_drvdata(openrd_client_snd_device,
			&openrd_client);

	ret = platform_device_add(openrd_client_snd_device);
	if (ret) {
		printk(KERN_ERR "%s: platform_device_add failed\n", __func__);
		platform_device_put(openrd_client_snd_device);
	}

	return ret;
}

static void __exit openrd_client_exit(void)
{
	platform_device_unregister(openrd_client_snd_device);
}

module_init(openrd_client_init);
module_exit(openrd_client_exit);

/* Module information */
MODULE_AUTHOR("Arnaud Patard <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("ALSA SoC OpenRD Client");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:soc-audio");
