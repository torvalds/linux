/*
 * ASoC simple sound card support
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <sound/simple_card.h>

#define asoc_simple_get_card_info(p) \
	container_of(p->dai_link, struct asoc_simple_card_info, snd_link)

static int asoc_simple_card_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct asoc_simple_card_info *cinfo = asoc_simple_get_card_info(rtd);
	struct asoc_simple_dai_init_info *iinfo = cinfo->init;
	struct snd_soc_dai *codec = rtd->codec_dai;
	struct snd_soc_dai *cpu = rtd->cpu_dai;
	unsigned int cpu_daifmt = iinfo->fmt | iinfo->cpu_daifmt;
	unsigned int codec_daifmt = iinfo->fmt | iinfo->codec_daifmt;
	int ret;

	if (codec_daifmt) {
		ret = snd_soc_dai_set_fmt(codec, codec_daifmt);
		if (ret < 0)
			return ret;
	}

	if (iinfo->sysclk) {
		ret = snd_soc_dai_set_sysclk(codec, 0, iinfo->sysclk, 0);
		if (ret < 0)
			return ret;
	}

	if (cpu_daifmt) {
		ret = snd_soc_dai_set_fmt(cpu, cpu_daifmt);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int asoc_simple_card_probe(struct platform_device *pdev)
{
	struct asoc_simple_card_info *cinfo = pdev->dev.platform_data;

	if (!cinfo) {
		dev_err(&pdev->dev, "no info for asoc-simple-card\n");
		return -EINVAL;
	}

	if (!cinfo->name	||
	    !cinfo->card	||
	    !cinfo->cpu_dai	||
	    !cinfo->codec	||
	    !cinfo->platform	||
	    !cinfo->codec_dai) {
		dev_err(&pdev->dev, "insufficient asoc_simple_card_info settings\n");
		return -EINVAL;
	}

	/*
	 * init snd_soc_dai_link
	 */
	cinfo->snd_link.name		= cinfo->name;
	cinfo->snd_link.stream_name	= cinfo->name;
	cinfo->snd_link.cpu_dai_name	= cinfo->cpu_dai;
	cinfo->snd_link.platform_name	= cinfo->platform;
	cinfo->snd_link.codec_name	= cinfo->codec;
	cinfo->snd_link.codec_dai_name	= cinfo->codec_dai;

	/* enable snd_link.init if cinfo has settings */
	if (cinfo->init)
		cinfo->snd_link.init	= asoc_simple_card_dai_init;

	/*
	 * init snd_soc_card
	 */
	cinfo->snd_card.name		= cinfo->card;
	cinfo->snd_card.owner		= THIS_MODULE;
	cinfo->snd_card.dai_link	= &cinfo->snd_link;
	cinfo->snd_card.num_links	= 1;
	cinfo->snd_card.dev		= &pdev->dev;

	return snd_soc_register_card(&cinfo->snd_card);
}

static int asoc_simple_card_remove(struct platform_device *pdev)
{
	struct asoc_simple_card_info *cinfo = pdev->dev.platform_data;

	return snd_soc_unregister_card(&cinfo->snd_card);
}

static struct platform_driver asoc_simple_card = {
	.driver = {
		.name	= "asoc-simple-card",
	},
	.probe		= asoc_simple_card_probe,
	.remove		= asoc_simple_card_remove,
};

module_platform_driver(asoc_simple_card);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ASoC Simple Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
