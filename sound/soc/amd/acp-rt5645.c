/*
 * Machine driver for AMD ACP Audio engine using Realtek RT5645 codec
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * This file is modified from rt288 machine driver
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>

#include "../codecs/rt5645.h"

#define CZ_PLAT_CLK 24000000

static struct snd_soc_jack cz_jack;

static int cz_aif1_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5645_PLL1_S_MCLK,
				  CZ_PLAT_CLK, params_rate(params) * 512);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5645_SCLK_S_PLL1,
				params_rate(params) * 512, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

	return ret;
}

static int cz_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_card *card;
	struct snd_soc_component *codec;

	codec = rtd->codec_dai->component;
	card = rtd->card;

	ret = snd_soc_card_jack_new(card, "Headset Jack",
				SND_JACK_HEADPHONE | SND_JACK_MICROPHONE |
				SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				SND_JACK_BTN_2 | SND_JACK_BTN_3,
				&cz_jack, NULL, 0);
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}

	rt5645_set_jack_detect(codec, &cz_jack, &cz_jack, &cz_jack);

	return 0;
}

static struct snd_soc_ops cz_aif1_ops = {
	.hw_params = cz_aif1_hw_params,
};

static struct snd_soc_dai_link cz_dai_rt5650[] = {
	{
		.name = "amd-rt5645-play",
		.stream_name = "RT5645_AIF1",
		.platform_name = "acp_audio_dma.0.auto",
		.cpu_dai_name = "designware-i2s.1.auto",
		.codec_dai_name = "rt5645-aif1",
		.codec_name = "i2c-10EC5650:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.init = cz_init,
		.ops = &cz_aif1_ops,
	},
	{
		.name = "amd-rt5645-cap",
		.stream_name = "RT5645_AIF1",
		.platform_name = "acp_audio_dma.0.auto",
		.cpu_dai_name = "designware-i2s.2.auto",
		.codec_dai_name = "rt5645-aif1",
		.codec_name = "i2c-10EC5650:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.ops = &cz_aif1_ops,
	},
};

static const struct snd_soc_dapm_widget cz_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_SPK("Speakers", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
};

static const struct snd_soc_dapm_route cz_audio_route[] = {
	{"Headphones", NULL, "HPOL"},
	{"Headphones", NULL, "HPOR"},
	{"RECMIXL", NULL, "Headset Mic"},
	{"RECMIXR", NULL, "Headset Mic"},
	{"Speakers", NULL, "SPOL"},
	{"Speakers", NULL, "SPOR"},
	{"DMIC L2", NULL, "Int Mic"},
	{"DMIC R2", NULL, "Int Mic"},
};

static const struct snd_kcontrol_new cz_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphones"),
	SOC_DAPM_PIN_SWITCH("Speakers"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
};

static struct snd_soc_card cz_card = {
	.name = "acprt5650",
	.owner = THIS_MODULE,
	.dai_link = cz_dai_rt5650,
	.num_links = ARRAY_SIZE(cz_dai_rt5650),
	.dapm_widgets = cz_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cz_widgets),
	.dapm_routes = cz_audio_route,
	.num_dapm_routes = ARRAY_SIZE(cz_audio_route),
	.controls = cz_mc_controls,
	.num_controls = ARRAY_SIZE(cz_mc_controls),
};

static int cz_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card;

	card = &cz_card;
	cz_card.dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	ret = devm_snd_soc_register_card(&pdev->dev, &cz_card);
	if (ret) {
		dev_err(&pdev->dev,
				"devm_snd_soc_register_card(%s) failed: %d\n",
				cz_card.name, ret);
		return ret;
	}
	return 0;
}

static const struct acpi_device_id cz_audio_acpi_match[] = {
	{ "AMDI1002", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, cz_audio_acpi_match);

static struct platform_driver cz_pcm_driver = {
	.driver = {
		.name = "cz-rt5645",
		.acpi_match_table = ACPI_PTR(cz_audio_acpi_match),
		.pm = &snd_soc_pm_ops,
	},
	.probe = cz_probe,
};

module_platform_driver(cz_pcm_driver);

MODULE_AUTHOR("akshu.agrawal@amd.com");
MODULE_DESCRIPTION("cz-rt5645 audio support");
MODULE_LICENSE("GPL v2");
