/*
 * Intel Baytrail SST RT5640 machine driver
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "../codecs/rt5640.h"

#include "sst-dsp.h"

static const struct snd_soc_dapm_widget byt_rt5640_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route byt_rt5640_audio_map[] = {
	{"Headset Mic", NULL, "MICBIAS1"},
	{"IN2P", NULL, "Headset Mic"},
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Speaker", NULL, "SPOLP"},
	{"Speaker", NULL, "SPOLN"},
	{"Speaker", NULL, "SPORP"},
	{"Speaker", NULL, "SPORN"},
};

static const struct snd_soc_dapm_route byt_rt5640_intmic_dmic1_map[] = {
	{"DMIC1", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5640_intmic_dmic2_map[] = {
	{"DMIC2", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5640_intmic_in1_map[] = {
	{"Internal Mic", NULL, "MICBIAS1"},
	{"IN1P", NULL, "Internal Mic"},
};

enum {
	BYT_RT5640_DMIC1_MAP,
	BYT_RT5640_DMIC2_MAP,
	BYT_RT5640_IN1_MAP,
};

#define BYT_RT5640_MAP(quirk)	((quirk) & 0xff)
#define BYT_RT5640_DMIC_EN	BIT(16)

static unsigned long byt_rt5640_quirk = BYT_RT5640_DMIC1_MAP |
					BYT_RT5640_DMIC_EN;

static const struct snd_kcontrol_new byt_rt5640_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static int byt_rt5640_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5640_SCLK_S_PLL1,
				     params_rate(params) * 256,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set codec clock %d\n", ret);
		return ret;
	}
	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5640_PLL1_S_BCLK1,
				  params_rate(params) * 64,
				  params_rate(params) * 256);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}
	return 0;
}

static int byt_rt5640_quirk_cb(const struct dmi_system_id *id)
{
	byt_rt5640_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id byt_rt5640_quirk_table[] = {
	{
		.callback = byt_rt5640_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "T100TA"),
		},
		.driver_data = (unsigned long *)BYT_RT5640_IN1_MAP,
	},
	{
		.callback = byt_rt5640_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "DellInc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Venue 8 Pro 5830"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_DMIC2_MAP |
						 BYT_RT5640_DMIC_EN),
	},
	{}
};

static int byt_rt5640_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	struct snd_soc_codec *codec = runtime->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_card *card = runtime->card;
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;

	card->dapm.idle_bias_off = true;

	ret = snd_soc_add_card_controls(card, byt_rt5640_controls,
					ARRAY_SIZE(byt_rt5640_controls));
	if (ret) {
		dev_err(card->dev, "unable to add card controls\n");
		return ret;
	}

	dmi_check_system(byt_rt5640_quirk_table);
	switch (BYT_RT5640_MAP(byt_rt5640_quirk)) {
	case BYT_RT5640_IN1_MAP:
		custom_map = byt_rt5640_intmic_in1_map;
		num_routes = ARRAY_SIZE(byt_rt5640_intmic_in1_map);
		break;
	case BYT_RT5640_DMIC2_MAP:
		custom_map = byt_rt5640_intmic_dmic2_map;
		num_routes = ARRAY_SIZE(byt_rt5640_intmic_dmic2_map);
		break;
	default:
		custom_map = byt_rt5640_intmic_dmic1_map;
		num_routes = ARRAY_SIZE(byt_rt5640_intmic_dmic1_map);
	}

	ret = snd_soc_dapm_add_routes(dapm, custom_map, num_routes);
	if (ret)
		return ret;

	if (byt_rt5640_quirk & BYT_RT5640_DMIC_EN) {
		ret = rt5640_dmic_enable(codec, 0, 0);
		if (ret)
			return ret;
	}

	snd_soc_dapm_ignore_suspend(&card->dapm, "Headphone");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Speaker");

	return ret;
}

static struct snd_soc_ops byt_rt5640_ops = {
	.hw_params = byt_rt5640_hw_params,
};

static struct snd_soc_dai_link byt_rt5640_dais[] = {
	{
		.name = "Baytrail Audio",
		.stream_name = "Audio",
		.cpu_dai_name = "baytrail-pcm-audio",
		.codec_dai_name = "rt5640-aif1",
		.codec_name = "i2c-10EC5640:00",
		.platform_name = "baytrail-pcm-audio",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.init = byt_rt5640_init,
		.ops = &byt_rt5640_ops,
	},
};

static struct snd_soc_card byt_rt5640_card = {
	.name = "byt-rt5640",
	.dai_link = byt_rt5640_dais,
	.num_links = ARRAY_SIZE(byt_rt5640_dais),
	.dapm_widgets = byt_rt5640_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_rt5640_widgets),
	.dapm_routes = byt_rt5640_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_rt5640_audio_map),
	.fully_routed = true,
};

static int byt_rt5640_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &byt_rt5640_card;

	card->dev = &pdev->dev;
	return devm_snd_soc_register_card(&pdev->dev, card);
}

static struct platform_driver byt_rt5640_audio = {
	.probe = byt_rt5640_probe,
	.driver = {
		.name = "byt-rt5640",
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(byt_rt5640_audio)

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail Machine driver");
MODULE_AUTHOR("Omair Md Abdullah, Jarkko Nikula");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:byt-rt5640");
