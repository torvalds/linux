// SPDX-License-Identifier: GPL-2.0-only
/*
 * ASoC driver for PROTO AudioCODEC (with a WM8731)
 *
 * Author:      Florian Meier, <koalo@koalo.de>
 *	      Copyright 2013
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include "../codecs/wm8731.h"

#define XTAL_RATE 12288000	/* This is fixed on this board */

static int snd_proto_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	/* Set proto sysclk */
	int ret = snd_soc_dai_set_sysclk(codec_dai, WM8731_SYSCLK_XTAL,
					 XTAL_RATE, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(card->dev, "Failed to set WM8731 SYSCLK: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_dapm_widget snd_proto_widget[] = {
	SND_SOC_DAPM_MIC("Microphone Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
};

static const struct snd_soc_dapm_route snd_proto_route[] = {
	/* speaker connected to LHPOUT/RHPOUT */
	{"Headphone Jack", NULL, "LHPOUT"},
	{"Headphone Jack", NULL, "RHPOUT"},

	/* mic is connected to Mic Jack, with WM8731 Mic Bias */
	{"MICIN", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Microphone Jack"},
};

/* audio machine driver */
static struct snd_soc_card snd_proto = {
	.name		= "snd_mikroe_proto",
	.owner		= THIS_MODULE,
	.dapm_widgets	= snd_proto_widget,
	.num_dapm_widgets = ARRAY_SIZE(snd_proto_widget),
	.dapm_routes	= snd_proto_route,
	.num_dapm_routes = ARRAY_SIZE(snd_proto_route),
};

static int snd_proto_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai;
	struct snd_soc_dai_link_component *comp;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *codec_np, *cpu_np;
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	unsigned int dai_fmt;
	int ret = 0;

	if (!np) {
		dev_err(&pdev->dev, "No device node supplied\n");
		return -EINVAL;
	}

	snd_proto.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&snd_proto, "model");
	if (ret)
		return ret;

	dai = devm_kzalloc(&pdev->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	/* for cpus/codecs/platforms */
	comp = devm_kzalloc(&pdev->dev, 3 * sizeof(*comp), GFP_KERNEL);
	if (!comp)
		return -ENOMEM;

	snd_proto.dai_link = dai;
	snd_proto.num_links = 1;

	dai->cpus = &comp[0];
	dai->num_cpus = 1;
	dai->codecs = &comp[1];
	dai->num_codecs = 1;
	dai->platforms = &comp[2];
	dai->num_platforms = 1;

	dai->name = "WM8731";
	dai->stream_name = "WM8731 HiFi";
	dai->codecs->dai_name = "wm8731-hifi";
	dai->init = &snd_proto_init;

	codec_np = of_parse_phandle(np, "audio-codec", 0);
	if (!codec_np) {
		dev_err(&pdev->dev, "audio-codec node missing\n");
		return -EINVAL;
	}
	dai->codecs->of_node = codec_np;

	cpu_np = of_parse_phandle(np, "i2s-controller", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "i2s-controller missing\n");
		return -EINVAL;
	}
	dai->cpus->of_node = cpu_np;
	dai->platforms->of_node = cpu_np;

	dai_fmt = snd_soc_of_parse_daifmt(np, NULL,
					  &bitclkmaster, &framemaster);
	if (bitclkmaster != framemaster) {
		dev_err(&pdev->dev, "Must be the same bitclock and frame master\n");
		return -EINVAL;
	}
	if (bitclkmaster) {
		dai_fmt &= ~SND_SOC_DAIFMT_MASTER_MASK;
		if (codec_np == bitclkmaster)
			dai_fmt |= SND_SOC_DAIFMT_CBM_CFM;
		else
			dai_fmt |= SND_SOC_DAIFMT_CBS_CFS;
	}
	of_node_put(bitclkmaster);
	of_node_put(framemaster);
	dai->dai_fmt = dai_fmt;

	of_node_put(codec_np);
	of_node_put(cpu_np);

	ret = snd_soc_register_card(&snd_proto);
	if (ret && ret != -EPROBE_DEFER)
		dev_err(&pdev->dev,
			"snd_soc_register_card() failed: %d\n", ret);

	return ret;
}

static int snd_proto_remove(struct platform_device *pdev)
{
	return snd_soc_unregister_card(&snd_proto);
}

static const struct of_device_id snd_proto_of_match[] = {
	{ .compatible = "mikroe,mikroe-proto", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_proto_of_match);

static struct platform_driver snd_proto_driver = {
	.driver = {
		.name   = "snd-mikroe-proto",
		.of_match_table = snd_proto_of_match,
	},
	.probe	  = snd_proto_probe,
	.remove	 = snd_proto_remove,
};

module_platform_driver(snd_proto_driver);

MODULE_AUTHOR("Florian Meier");
MODULE_DESCRIPTION("ASoC Driver for PROTO board (WM8731)");
MODULE_LICENSE("GPL");
