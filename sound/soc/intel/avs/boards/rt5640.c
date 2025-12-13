// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022-2025 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/module.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../../codecs/rt5640.h"
#include "../utils.h"

#define AVS_RT5640_MCLK_HZ		19200000
#define RT5640_CODEC_DAI		"rt5640-aif1"

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route card_routes[] = {
	{ "Headphone Jack", NULL, "HPOR" },
	{ "Headphone Jack", NULL, "HPOL" },
	{ "IN2P", NULL, "Mic Jack" },
	{ "IN2P", NULL, "MICBIAS1" },
	{ "Speaker", NULL, "SPOLP" },
	{ "Speaker", NULL, "SPOLN" },
	{ "Speaker", NULL, "SPORP" },
	{ "Speaker", NULL, "SPORN" },
};

static const struct snd_soc_jack_pin card_headset_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Mic Jack",
		.mask = SND_JACK_MICROPHONE,
	},
};

static int avs_rt5640_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(runtime, 0);
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_jack_pin *pins;
	struct snd_soc_jack *jack;
	int num_pins, ret;

	jack = snd_soc_card_get_drvdata(card);
	num_pins = ARRAY_SIZE(card_headset_pins);

	pins = devm_kmemdup(card->dev, card_headset_pins, sizeof(*pins) * num_pins, GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	ret = snd_soc_card_jack_new_pins(card, "Headset Jack", SND_JACK_HEADSET, jack, pins,
					 num_pins);
	if (ret)
		return ret;

	snd_soc_component_set_jack(codec_dai->component, jack, NULL);
	card->dapm.idle_bias = false;

	return 0;
}

static void avs_rt5640_codec_exit(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(runtime, 0);

	snd_soc_component_set_jack(codec_dai->component, NULL, NULL);
}

static int avs_rt5640_be_fixup(struct snd_soc_pcm_runtime *runtime,
			       struct snd_pcm_hw_params *params)
{
	struct snd_mask *fmask = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* Format 24/32 is MSB-aligned for HDAudio and LSB-aligned for I2S. */
	if (params_format(params) == SNDRV_PCM_FORMAT_S32_LE)
		snd_mask_set_format(fmask, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int avs_rt5640_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(runtime, 0);
	int ret;

	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5640_PLL1_S_MCLK, AVS_RT5640_MCLK_HZ,
				  params_rate(params) * 512);
	if (ret < 0) {
		dev_err(runtime->dev, "Set codec PLL failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5640_SCLK_S_PLL1, params_rate(params) * 512,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(runtime->dev, "Set codec SCLK failed: %d\n", ret);
		return ret;
	}

	ret = rt5640_sel_asrc_clk_src(codec_dai->component,
				      RT5640_DA_STEREO_FILTER | RT5640_AD_STEREO_FILTER |
				      RT5640_DA_MONO_L_FILTER | RT5640_DA_MONO_R_FILTER |
				      RT5640_AD_MONO_L_FILTER | RT5640_AD_MONO_R_FILTER,
				      RT5640_CLK_SEL_ASRC);
	if (ret)
		dev_err(runtime->dev, "Set codec ASRC failed: %d\n", ret);

	return ret;
}

static const struct snd_soc_ops avs_rt5640_ops = {
	.hw_params = avs_rt5640_hw_params,
};

static int avs_create_dai_link(struct device *dev, int ssp_port, int tdm_slot,
			       struct snd_soc_acpi_mach *mach,
			       struct snd_soc_dai_link **dai_link)
{
	struct snd_soc_dai_link_component *platform;
	struct snd_soc_dai_link *dl;
	u32 uid = 0;
	int ret;

	if (mach->uid) {
		ret = kstrtou32(mach->uid, 0, &uid);
		if (ret)
			return ret;
		uid--; /* 0-based indexing. */
	}

	dl = devm_kzalloc(dev, sizeof(*dl), GFP_KERNEL);
	platform = devm_kzalloc(dev, sizeof(*platform), GFP_KERNEL);
	if (!dl || !platform)
		return -ENOMEM;

	dl->name = devm_kasprintf(dev, GFP_KERNEL,
				  AVS_STRING_FMT("SSP", "-Codec", ssp_port, tdm_slot));
	dl->cpus = devm_kzalloc(dev, sizeof(*dl->cpus), GFP_KERNEL);
	dl->codecs = devm_kzalloc(dev, sizeof(*dl->codecs), GFP_KERNEL);
	if (!dl->name || !dl->cpus || !dl->codecs)
		return -ENOMEM;

	dl->cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
					    AVS_STRING_FMT("SSP", " Pin", ssp_port, tdm_slot));
	dl->codecs->name = devm_kasprintf(dev, GFP_KERNEL, "i2c-10EC5640:0%d", uid);
	dl->codecs->dai_name = devm_kasprintf(dev, GFP_KERNEL, RT5640_CODEC_DAI);
	if (!dl->cpus->dai_name || !dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	platform->name = dev_name(dev);
	dl->num_cpus = 1;
	dl->num_codecs = 1;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	dl->init = avs_rt5640_codec_init;
	dl->exit = avs_rt5640_codec_exit;
	dl->be_hw_params_fixup = avs_rt5640_be_fixup;
	dl->ops = &avs_rt5640_ops;
	dl->nonatomic = 1;
	dl->no_pcm = 1;

	*dai_link = dl;

	return 0;
}

static int avs_card_suspend_pre(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, RT5640_CODEC_DAI);

	return snd_soc_component_set_jack(codec_dai->component, NULL, NULL);
}

static int avs_card_resume_post(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, RT5640_CODEC_DAI);
	struct snd_soc_jack *jack = snd_soc_card_get_drvdata(card);

	return snd_soc_component_set_jack(codec_dai->component, jack, NULL);
}

static int avs_rt5640_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai_link;
	struct device *dev = &pdev->dev;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct snd_soc_jack *jack;
	int ssp_port, tdm_slot, ret;

	mach = dev_get_platdata(dev);

	ret = avs_mach_get_ssp_tdm(dev, mach, &ssp_port, &tdm_slot);
	if (ret)
		return ret;

	ret = avs_create_dai_link(dev, ssp_port, tdm_slot, mach, &dai_link);
	if (ret) {
		dev_err(dev, "Failed to create dai link: %d", ret);
		return ret;
	}

	jack = devm_kzalloc(dev, sizeof(*jack), GFP_KERNEL);
	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!jack || !card)
		return -ENOMEM;

	if (mach->uid) {
		card->name = devm_kasprintf(dev, GFP_KERNEL, "AVS I2S ALC5640.%s", mach->uid);
		if (!card->name)
			return -ENOMEM;
	} else {
		card->name = "AVS I2S ALC5640";
	}
	card->driver_name = "avs_rt5640";
	card->long_name = card->name;
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->suspend_pre = avs_card_suspend_pre;
	card->resume_post = avs_card_resume_post;
	card->dai_link = dai_link;
	card->num_links = 1;
	card->dapm_widgets = card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(card_widgets);
	card->dapm_routes = card_routes;
	card->num_dapm_routes = ARRAY_SIZE(card_routes);
	card->fully_routed = true;
	snd_soc_card_set_drvdata(card, jack);

	return devm_snd_soc_register_deferrable_card(dev, card);
}

static const struct platform_device_id avs_rt5640_driver_ids[] = {
	{
		.name = "avs_rt5640",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, avs_rt5640_driver_ids);

static struct platform_driver avs_rt5640_driver = {
	.probe = avs_rt5640_probe,
	.driver = {
		.name = "avs_rt5640",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = avs_rt5640_driver_ids,
};

module_platform_driver(avs_rt5640_driver);

MODULE_DESCRIPTION("Intel rt5640 machine driver");
MODULE_LICENSE("GPL");
