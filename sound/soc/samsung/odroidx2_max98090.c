/*
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/of.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "i2s.h"

struct odroidx2_drv_data {
	const struct snd_soc_dapm_widget *dapm_widgets;
	unsigned int num_dapm_widgets;
};

/* The I2S CDCLK output clock frequency for the MAX98090 codec */
#define MAX98090_MCLK 19200000

static struct snd_soc_dai_link odroidx2_dai[];

static int odroidx2_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;
	int ret;

	rtd = snd_soc_get_pcm_runtime(card, card->dai_link[0].name);
	codec_dai = rtd->codec_dai;
	cpu_dai = rtd->cpu_dai;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, MAX98090_MCLK,
						SND_SOC_CLOCK_IN);

	if (ret < 0 || of_find_property(odroidx2_dai[0].codec_of_node,
					"clocks", NULL))
		return ret;

	/* Set the cpu DAI configuration in order to use CDCLK */
	return snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK,
					0, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_dapm_widget odroidx2_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static const struct snd_soc_dapm_widget odroidu3_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speakers", NULL),
};

static struct snd_soc_dai_link odroidx2_dai[] = {
	{
		.name		= "MAX98090",
		.stream_name	= "MAX98090 PCM",
		.codec_dai_name	= "HiFi",
		.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM,
	}
};

static struct snd_soc_card odroidx2 = {
	.owner			= THIS_MODULE,
	.dai_link		= odroidx2_dai,
	.num_links		= ARRAY_SIZE(odroidx2_dai),
	.fully_routed		= true,
	.late_probe		= odroidx2_late_probe,
};

static const struct odroidx2_drv_data odroidx2_drvdata = {
	.dapm_widgets		= odroidx2_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(odroidx2_dapm_widgets),
};

static const struct odroidx2_drv_data odroidu3_drvdata = {
	.dapm_widgets		= odroidu3_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(odroidu3_dapm_widgets),
};

static const struct of_device_id odroidx2_audio_of_match[] = {
	{
		.compatible	= "samsung,odroidx2-audio",
		.data		= &odroidx2_drvdata,
	}, {
		.compatible	= "samsung,odroidu3-audio",
		.data		= &odroidu3_drvdata,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, odroidx2_audio_of_match);

static int odroidx2_audio_probe(struct platform_device *pdev)
{
	struct device_node *snd_node = pdev->dev.of_node;
	struct snd_soc_card *card = &odroidx2;
	struct device_node *i2s_node, *codec_node;
	struct odroidx2_drv_data *dd;
	const struct of_device_id *of_id;
	int ret;

	of_id = of_match_node(odroidx2_audio_of_match, snd_node);
	dd = (struct odroidx2_drv_data *)of_id->data;

	card->num_dapm_widgets = dd->num_dapm_widgets;
	card->dapm_widgets = dd->dapm_widgets;

	card->dev = &pdev->dev;

	ret = snd_soc_of_parse_card_name(card, "samsung,model");
	if (ret < 0)
		return ret;

	ret = snd_soc_of_parse_audio_routing(card, "samsung,audio-routing");
	if (ret < 0)
		return ret;

	codec_node = of_parse_phandle(snd_node, "samsung,audio-codec", 0);
	if (!codec_node) {
		dev_err(&pdev->dev,
			"Failed parsing samsung,i2s-codec property\n");
		return -EINVAL;
	}

	i2s_node = of_parse_phandle(snd_node, "samsung,i2s-controller", 0);
	if (!i2s_node) {
		dev_err(&pdev->dev,
			"Failed parsing samsung,i2s-controller property\n");
		ret = -EINVAL;
		goto err_put_codec_n;
	}

	odroidx2_dai[0].codec_of_node = codec_node;
	odroidx2_dai[0].cpu_of_node = i2s_node;
	odroidx2_dai[0].platform_of_node = i2s_node;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		goto err_put_i2s_n;
	}
	return 0;

err_put_i2s_n:
	of_node_put(i2s_node);
err_put_codec_n:
	of_node_put(codec_node);
	return ret;
}

static int odroidx2_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	of_node_put(odroidx2_dai[0].cpu_of_node);
	of_node_put(odroidx2_dai[0].codec_of_node);

	return 0;
}

static struct platform_driver odroidx2_audio_driver = {
	.driver = {
		.name		= "odroidx2-audio",
		.of_match_table	= odroidx2_audio_of_match,
		.pm		= &snd_soc_pm_ops,
	},
	.probe	= odroidx2_audio_probe,
	.remove	= odroidx2_audio_remove,
};
module_platform_driver(odroidx2_audio_driver);

MODULE_AUTHOR("Chen Zhen <zhen1.chen@samsung.com>");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_DESCRIPTION("ALSA SoC Odroid X2/U3 Audio Support");
MODULE_LICENSE("GPL v2");
