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
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/simple_card.h>

static int __asoc_simple_card_dai_init(struct snd_soc_dai *dai,
				       struct asoc_simple_dai *set,
				       unsigned int daifmt)
{
	int ret = 0;

	daifmt |= set->fmt;

	if (daifmt)
		ret = snd_soc_dai_set_fmt(dai, daifmt);

	if (ret == -ENOTSUPP) {
		dev_dbg(dai->dev, "ASoC: set_fmt is not supported\n");
		ret = 0;
	}

	if (!ret && set->sysclk)
		ret = snd_soc_dai_set_sysclk(dai, 0, set->sysclk, 0);

	return ret;
}

static int asoc_simple_card_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct asoc_simple_card_info *info =
				snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec = rtd->codec_dai;
	struct snd_soc_dai *cpu = rtd->cpu_dai;
	unsigned int daifmt = info->daifmt;
	int ret;

	ret = __asoc_simple_card_dai_init(codec, &info->codec_dai, daifmt);
	if (ret < 0)
		return ret;

	ret = __asoc_simple_card_dai_init(cpu, &info->cpu_dai, daifmt);
	if (ret < 0)
		return ret;

	return 0;
}

static int
asoc_simple_card_sub_parse_of(struct device_node *np,
			      struct asoc_simple_dai *dai,
			      struct device_node **node)
{
	struct clk *clk;
	int ret;

	/*
	 * get node via "sound-dai = <&phandle port>"
	 * it will be used as xxx_of_node on soc_bind_dai_link()
	 */
	*node = of_parse_phandle(np, "sound-dai", 0);
	if (!*node)
		return -ENODEV;

	/* get dai->name */
	ret = snd_soc_of_get_dai_name(np, &dai->name);
	if (ret < 0)
		goto parse_error;

	/*
	 * bitclock-inversion, frame-inversion
	 * bitclock-master,    frame-master
	 * and specific "format" if it has
	 */
	dai->fmt = snd_soc_of_parse_daifmt(np, NULL);

	/*
	 * dai->sysclk come from
	 *  "clocks = <&xxx>" (if system has common clock)
	 *  or "system-clock-frequency = <xxx>"
	 *  or device's module clock.
	 */
	if (of_property_read_bool(np, "clocks")) {
		clk = of_clk_get(np, 0);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			goto parse_error;
		}

		dai->sysclk = clk_get_rate(clk);
	} else if (of_property_read_bool(np, "system-clock-frequency")) {
		of_property_read_u32(np,
				     "system-clock-frequency",
				     &dai->sysclk);
	} else {
		clk = of_clk_get(*node, 0);
		if (!IS_ERR(clk))
			dai->sysclk = clk_get_rate(clk);
	}

	ret = 0;

parse_error:
	of_node_put(*node);

	return ret;
}

static int asoc_simple_card_parse_of(struct device_node *node,
				     struct asoc_simple_card_info *info,
				     struct device *dev,
				     struct device_node **of_cpu,
				     struct device_node **of_codec,
				     struct device_node **of_platform)
{
	struct device_node *np;
	char *name;
	int ret;

	/* get CPU/CODEC common format via simple-audio-card,format */
	info->daifmt = snd_soc_of_parse_daifmt(node, "simple-audio-card,") &
		(SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_INV_MASK);

	/* DAPM routes */
	if (of_property_read_bool(node, "simple-audio-card,routing")) {
		ret = snd_soc_of_parse_audio_routing(&info->snd_card,
					"simple-audio-card,routing");
		if (ret)
			return ret;
	}

	/* CPU sub-node */
	ret = -EINVAL;
	np = of_get_child_by_name(node, "simple-audio-card,cpu");
	if (np)
		ret = asoc_simple_card_sub_parse_of(np,
						  &info->cpu_dai,
						  of_cpu);
	if (ret < 0)
		return ret;

	/* CODEC sub-node */
	ret = -EINVAL;
	np = of_get_child_by_name(node, "simple-audio-card,codec");
	if (np)
		ret = asoc_simple_card_sub_parse_of(np,
						  &info->codec_dai,
						  of_codec);
	if (ret < 0)
		return ret;

	if (!info->cpu_dai.name || !info->codec_dai.name)
		return -EINVAL;

	/* card name is created from CPU/CODEC dai name */
	name = devm_kzalloc(dev,
			    strlen(info->cpu_dai.name)   +
			    strlen(info->codec_dai.name) + 2,
			    GFP_KERNEL);
	sprintf(name, "%s-%s", info->cpu_dai.name, info->codec_dai.name);
	info->snd_card.name = name;
	info->snd_link.name = info->snd_link.stream_name = name;

	/* simple-card assumes platform == cpu */
	*of_platform = *of_cpu;

	dev_dbg(dev, "card-name : %s\n", name);
	dev_dbg(dev, "platform : %04x\n", info->daifmt);
	dev_dbg(dev, "cpu : %s / %04x / %d\n",
		info->cpu_dai.name,
		info->cpu_dai.fmt,
		info->cpu_dai.sysclk);
	dev_dbg(dev, "codec : %s / %04x / %d\n",
		info->codec_dai.name,
		info->codec_dai.fmt,
		info->codec_dai.sysclk);

	return 0;
}

static int asoc_simple_card_probe(struct platform_device *pdev)
{
	struct asoc_simple_card_info *cinfo;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *of_cpu, *of_codec, *of_platform;
	struct device *dev = &pdev->dev;
	int ret;

	cinfo		= NULL;
	of_cpu		= NULL;
	of_codec	= NULL;
	of_platform	= NULL;

	cinfo = devm_kzalloc(dev, sizeof(*cinfo), GFP_KERNEL);
	if (!cinfo)
		return -ENOMEM;

	if (np && of_device_is_available(np)) {
		cinfo->snd_card.dev = dev;

		ret = asoc_simple_card_parse_of(np, cinfo, dev,
						&of_cpu,
						&of_codec,
						&of_platform);
		if (ret < 0) {
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "parse error %d\n", ret);
			return ret;
		}
		cinfo->snd_link.cpu_of_node	= of_cpu;
		cinfo->snd_link.codec_of_node	= of_codec;
		cinfo->snd_link.platform_of_node = of_platform;
	} else {
		if (!dev->platform_data) {
			dev_err(dev, "no info for asoc-simple-card\n");
			return -EINVAL;
		}

		memcpy(cinfo, dev->platform_data, sizeof(*cinfo));
		cinfo->snd_card.dev = dev;

		if (!cinfo->name	||
		    !cinfo->card	||
		    !cinfo->codec_dai.name	||
		    !cinfo->codec	||
		    !cinfo->platform	||
		    !cinfo->cpu_dai.name) {
			dev_err(dev, "insufficient asoc_simple_card_info settings\n");
			return -EINVAL;
		}

		cinfo->snd_card.name		= cinfo->card;
		cinfo->snd_link.name		= cinfo->name;
		cinfo->snd_link.stream_name	= cinfo->name;
		cinfo->snd_link.platform_name	= cinfo->platform;
		cinfo->snd_link.codec_name	= cinfo->codec;
	}

	/*
	 * init snd_soc_dai_link
	 */
	cinfo->snd_link.cpu_dai_name	= cinfo->cpu_dai.name;
	cinfo->snd_link.codec_dai_name	= cinfo->codec_dai.name;
	cinfo->snd_link.init		= asoc_simple_card_dai_init;

	/*
	 * init snd_soc_card
	 */
	cinfo->snd_card.owner		= THIS_MODULE;
	cinfo->snd_card.dai_link	= &cinfo->snd_link;
	cinfo->snd_card.num_links	= 1;

	snd_soc_card_set_drvdata(&cinfo->snd_card, cinfo);

	return devm_snd_soc_register_card(&pdev->dev, &cinfo->snd_card);
}

static const struct of_device_id asoc_simple_of_match[] = {
	{ .compatible = "simple-audio-card", },
	{},
};
MODULE_DEVICE_TABLE(of, asoc_simple_of_match);

static struct platform_driver asoc_simple_card = {
	.driver = {
		.name	= "asoc-simple-card",
		.owner = THIS_MODULE,
		.of_match_table = asoc_simple_of_match,
	},
	.probe		= asoc_simple_card_probe,
};

module_platform_driver(asoc_simple_card);

MODULE_ALIAS("platform:asoc-simple-card");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ASoC Simple Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
