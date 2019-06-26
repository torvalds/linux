// SPDX-License-Identifier: GPL-2.0
//
// Phytec pcm030 driver for the PSC of the Freescale MPC52xx
// configured as AC97 interface
//
// Copyright 2008 Jon Smirl, Digispeaker
// Author: Jon Smirl <jonsmirl@gmail.com>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <sound/soc.h>

#include "mpc5200_dma.h"

#define DRV_NAME "pcm030-audio-fabric"

struct pcm030_audio_data {
	struct snd_soc_card *card;
	struct platform_device *codec_device;
};

SND_SOC_DAILINK_DEFS(analog,
	DAILINK_COMP_ARRAY(COMP_CPU("mpc5200-psc-ac97.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9712-codec", "wm9712-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(iec958,
	DAILINK_COMP_ARRAY(COMP_CPU("mpc5200-psc-ac97.1")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9712-codec", "wm9712-aux")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link pcm030_fabric_dai[] = {
{
	.name = "AC97.0",
	.stream_name = "AC97 Analog",
	SND_SOC_DAILINK_REG(analog),
},
{
	.name = "AC97.1",
	.stream_name = "AC97 IEC958",
	SND_SOC_DAILINK_REG(iec958),
},
};

static struct snd_soc_card pcm030_card = {
	.name = "pcm030",
	.owner = THIS_MODULE,
	.dai_link = pcm030_fabric_dai,
	.num_links = ARRAY_SIZE(pcm030_fabric_dai),
};

static int pcm030_fabric_probe(struct platform_device *op)
{
	struct device_node *np = op->dev.of_node;
	struct device_node *platform_np;
	struct snd_soc_card *card = &pcm030_card;
	struct pcm030_audio_data *pdata;
	struct snd_soc_dai_link *dai_link;
	int ret;
	int i;

	if (!of_machine_is_compatible("phytec,pcm030"))
		return -ENODEV;

	pdata = devm_kzalloc(&op->dev, sizeof(struct pcm030_audio_data),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	card->dev = &op->dev;

	pdata->card = card;

	platform_np = of_parse_phandle(np, "asoc-platform", 0);
	if (!platform_np) {
		dev_err(&op->dev, "ac97 not registered\n");
		return -ENODEV;
	}

	for_each_card_prelinks(card, i, dai_link)
		dai_link->platforms->of_node = platform_np;

	ret = request_module("snd-soc-wm9712");
	if (ret)
		dev_err(&op->dev, "request_module returned: %d\n", ret);

	pdata->codec_device = platform_device_alloc("wm9712-codec", -1);
	if (!pdata->codec_device)
		dev_err(&op->dev, "platform_device_alloc() failed\n");

	ret = platform_device_add(pdata->codec_device);
	if (ret)
		dev_err(&op->dev, "platform_device_add() failed: %d\n", ret);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&op->dev, "snd_soc_register_card() failed: %d\n", ret);

	platform_set_drvdata(op, pdata);

	return ret;
}

static int pcm030_fabric_remove(struct platform_device *op)
{
	struct pcm030_audio_data *pdata = platform_get_drvdata(op);
	int ret;

	ret = snd_soc_unregister_card(pdata->card);
	platform_device_unregister(pdata->codec_device);

	return ret;
}

static const struct of_device_id pcm030_audio_match[] = {
	{ .compatible = "phytec,pcm030-audio-fabric", },
	{}
};
MODULE_DEVICE_TABLE(of, pcm030_audio_match);

static struct platform_driver pcm030_fabric_driver = {
	.probe		= pcm030_fabric_probe,
	.remove		= pcm030_fabric_remove,
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table    = pcm030_audio_match,
	},
};

module_platform_driver(pcm030_fabric_driver);


MODULE_AUTHOR("Jon Smirl <jonsmirl@gmail.com>");
MODULE_DESCRIPTION(DRV_NAME ": mpc5200 pcm030 fabric driver");
MODULE_LICENSE("GPL");

