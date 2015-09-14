/*
 * mt8173-rt5650-rt5676.c  --  MT8173 machine driver with RT5650/5676 codecs
 *
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Koro Chen <koro.chen@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "../codecs/rt5645.h"
#include "../codecs/rt5677.h"

#define MCLK_FOR_CODECS		12288000

static const struct snd_soc_dapm_widget mt8173_rt5650_rt5676_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route mt8173_rt5650_rt5676_routes[] = {
	{"Speaker", NULL, "SPOL"},
	{"Speaker", NULL, "SPOR"},
	{"Speaker", NULL, "Sub AIF2TX"}, /* IF2 ADC to 5650  */
	{"Sub DMIC L1", NULL, "Int Mic"}, /* DMIC from 5676 */
	{"Sub DMIC R1", NULL, "Int Mic"},
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Headphone", NULL, "Sub AIF2TX"}, /* IF2 ADC to 5650  */
	{"Headset Mic", NULL, "micbias1"},
	{"Headset Mic", NULL, "micbias2"},
	{"IN1P", NULL, "Headset Mic"},
	{"IN1N", NULL, "Headset Mic"},
	{"Sub AIF2RX", NULL, "Headset Mic"}, /* IF2 DAC from 5650  */
};

static const struct snd_kcontrol_new mt8173_rt5650_rt5676_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static int mt8173_rt5650_rt5676_hw_params(struct snd_pcm_substream *substream,
					  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int i, ret;

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[i];

		/* pll from mclk 12.288M */
		ret = snd_soc_dai_set_pll(codec_dai, 0, 0, MCLK_FOR_CODECS,
					  params_rate(params) * 512);
		if (ret)
			return ret;

		/* sysclk from pll */
		ret = snd_soc_dai_set_sysclk(codec_dai, 1,
					     params_rate(params) * 512,
					     SND_SOC_CLOCK_IN);
		if (ret)
			return ret;
	}
	return 0;
}

static struct snd_soc_ops mt8173_rt5650_rt5676_ops = {
	.hw_params = mt8173_rt5650_rt5676_hw_params,
};

static struct snd_soc_jack mt8173_rt5650_rt5676_jack;

static int mt8173_rt5650_rt5676_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_codec *codec = runtime->codec_dais[0]->codec;
	struct snd_soc_codec *codec_sub = runtime->codec_dais[1]->codec;
	int ret;

	rt5645_sel_asrc_clk_src(codec,
				RT5645_DA_STEREO_FILTER |
				RT5645_AD_STEREO_FILTER,
				RT5645_CLK_SEL_I2S1_ASRC);
	rt5677_sel_asrc_clk_src(codec_sub,
				RT5677_DA_STEREO_FILTER |
				RT5677_AD_STEREO1_FILTER,
				RT5677_CLK_SEL_I2S1_ASRC);
	rt5677_sel_asrc_clk_src(codec_sub,
				RT5677_AD_STEREO2_FILTER |
				RT5677_I2S2_SOURCE,
				RT5677_CLK_SEL_I2S2_ASRC);

	/* enable jack detection */
	ret = snd_soc_card_jack_new(card, "Headset Jack",
				    SND_JACK_HEADPHONE | SND_JACK_MICROPHONE |
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2 | SND_JACK_BTN_3,
				    &mt8173_rt5650_rt5676_jack, NULL, 0);
	if (ret) {
		dev_err(card->dev, "Can't new Headset Jack %d\n", ret);
		return ret;
	}

	return rt5645_set_jack_detect(codec,
				      &mt8173_rt5650_rt5676_jack,
				      &mt8173_rt5650_rt5676_jack,
				      &mt8173_rt5650_rt5676_jack);
}

static struct snd_soc_dai_link_component mt8173_rt5650_rt5676_codecs[] = {
	{
		.dai_name = "rt5645-aif1",
	},
	{
		.dai_name = "rt5677-aif1",
	},
};

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link mt8173_rt5650_rt5676_dais[] = {
	/* Front End DAI links */
	{
		.name = "rt5650_rt5676 Playback",
		.stream_name = "rt5650_rt5676 Playback",
		.cpu_dai_name = "DL1",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "rt5650_rt5676 Capture",
		.stream_name = "rt5650_rt5676 Capture",
		.cpu_dai_name = "VUL",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.dpcm_capture = 1,
	},

	/* Back End DAI links */
	{
		.name = "Codec",
		.cpu_dai_name = "I2S",
		.no_pcm = 1,
		.codecs = mt8173_rt5650_rt5676_codecs,
		.num_codecs = 2,
		.init = mt8173_rt5650_rt5676_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.ops = &mt8173_rt5650_rt5676_ops,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{ /* rt5676 <-> rt5650 intercodec link: Sets rt5676 I2S2 as master */
		.name = "rt5650_rt5676 intercodec",
		.stream_name = "rt5650_rt5676 intercodec",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "snd-soc-dummy",
		.no_pcm = 1,
		.codec_dai_name = "rt5677-aif2",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBM_CFM,
	},

};

static struct snd_soc_codec_conf mt8173_rt5650_rt5676_codec_conf[] = {
	{
		.name_prefix = "Sub",
	},
};

static struct snd_soc_card mt8173_rt5650_rt5676_card = {
	.name = "mtk-rt5650-rt5676",
	.owner = THIS_MODULE,
	.dai_link = mt8173_rt5650_rt5676_dais,
	.num_links = ARRAY_SIZE(mt8173_rt5650_rt5676_dais),
	.codec_conf = mt8173_rt5650_rt5676_codec_conf,
	.num_configs = ARRAY_SIZE(mt8173_rt5650_rt5676_codec_conf),
	.controls = mt8173_rt5650_rt5676_controls,
	.num_controls = ARRAY_SIZE(mt8173_rt5650_rt5676_controls),
	.dapm_widgets = mt8173_rt5650_rt5676_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8173_rt5650_rt5676_widgets),
	.dapm_routes = mt8173_rt5650_rt5676_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8173_rt5650_rt5676_routes),
};

static int mt8173_rt5650_rt5676_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt8173_rt5650_rt5676_card;
	struct device_node *platform_node;
	int i, ret;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}

	for (i = 0; i < card->num_links; i++) {
		if (mt8173_rt5650_rt5676_dais[i].platform_name)
			continue;
		mt8173_rt5650_rt5676_dais[i].platform_of_node = platform_node;
	}

	mt8173_rt5650_rt5676_codecs[0].of_node =
		of_parse_phandle(pdev->dev.of_node, "mediatek,audio-codec", 0);
	if (!mt8173_rt5650_rt5676_codecs[0].of_node) {
		dev_err(&pdev->dev,
			"Property 'audio-codec' missing or invalid\n");
		return -EINVAL;
	}
	mt8173_rt5650_rt5676_codecs[1].of_node =
		of_parse_phandle(pdev->dev.of_node, "mediatek,audio-codec", 1);
	if (!mt8173_rt5650_rt5676_codecs[1].of_node) {
		dev_err(&pdev->dev,
			"Property 'audio-codec' missing or invalid\n");
		return -EINVAL;
	}
	mt8173_rt5650_rt5676_codec_conf[0].of_node =
		mt8173_rt5650_rt5676_codecs[1].of_node;

	mt8173_rt5650_rt5676_dais[3].codec_of_node =
		mt8173_rt5650_rt5676_codecs[1].of_node;

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
			__func__, ret);
	return ret;
}

static int mt8173_rt5650_rt5676_dev_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	return 0;
}

static const struct of_device_id mt8173_rt5650_rt5676_dt_match[] = {
	{ .compatible = "mediatek,mt8173-rt5650-rt5676", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt8173_rt5650_rt5676_dt_match);

static struct platform_driver mt8173_rt5650_rt5676_driver = {
	.driver = {
		   .name = "mtk-rt5650-rt5676",
		   .of_match_table = mt8173_rt5650_rt5676_dt_match,
#ifdef CONFIG_PM
		   .pm = &snd_soc_pm_ops,
#endif
	},
	.probe = mt8173_rt5650_rt5676_dev_probe,
	.remove = mt8173_rt5650_rt5676_dev_remove,
};

module_platform_driver(mt8173_rt5650_rt5676_driver);

/* Module information */
MODULE_DESCRIPTION("MT8173 RT5650 and RT5676 SoC machine driver");
MODULE_AUTHOR("Koro Chen <koro.chen@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mtk-rt5650-rt5676");

