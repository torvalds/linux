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
#include <linux/platform_data/asoc-kirkwood.h>
#include "../codecs/cs42l51.h"

static int openrd_client_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int freq;

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
	.cpu_dai_name = "mvebu-audio",
	.platform_name = "mvebu-audio",
	.codec_dai_name = "cs42l51-hifi",
	.codec_name = "cs42l51-codec.0-004a",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS,
	.ops = &openrd_client_ops,
},
};


static struct snd_soc_card openrd_client = {
	.name = "OpenRD Client",
	.owner = THIS_MODULE,
	.dai_link = openrd_client_dai,
	.num_links = ARRAY_SIZE(openrd_client_dai),
};

static int openrd_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &openrd_client;
	int ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
	return ret;
}

static int openrd_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver openrd_driver = {
	.driver		= {
		.name	= "openrd-client-audio",
		.owner	= THIS_MODULE,
	},
	.probe		= openrd_probe,
	.remove		= openrd_remove,
};

module_platform_driver(openrd_driver);

/* Module information */
MODULE_AUTHOR("Arnaud Patard <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("ALSA SoC OpenRD Client");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:openrd-client-audio");
