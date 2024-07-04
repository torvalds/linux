// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2023 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/clk.h>
#include <linux/input.h>
#include <linux/module.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../../codecs/rt5514.h"
#include "../utils.h"

#define RT5514_CODEC_DAI	"rt5514-aif1"

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static const struct snd_soc_dapm_route card_base_routes[] = {
	/* DMIC */
	{ "DMIC1L", NULL, "DMIC" },
	{ "DMIC1R", NULL, "DMIC" },
	{ "DMIC2L", NULL, "DMIC" },
	{ "DMIC2R", NULL, "DMIC" },
};

static int avs_rt5514_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret = snd_soc_dapm_ignore_suspend(&runtime->card->dapm, "DMIC");

	if (ret)
		dev_err(runtime->dev, "DMIC - Ignore suspend failed = %d\n", ret);

	return ret;
}

static int avs_rt5514_be_fixup(struct snd_soc_pcm_runtime *runtime,
			       struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate, *channels;
	struct snd_mask *fmt;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	rate->min = rate->max = 48000;
	channels->min = channels->max = 4;

	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);

	return 0;
}

static int avs_rt5514_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int ret;

	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xF, 0, 8, 16);
	if (ret < 0) {
		dev_err(rtd->dev, "set TDM slot err:%d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5514_SCLK_S_MCLK, 24576000, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "set sysclk err: %d\n", ret);

	return ret;
}

static const struct snd_soc_ops avs_rt5514_ops = {
	.hw_params = avs_rt5514_hw_params,
};

static int avs_create_dai_link(struct device *dev, const char *platform_name, int ssp_port,
			       int tdm_slot, struct snd_soc_dai_link **dai_link)
{
	struct snd_soc_dai_link_component *platform;
	struct snd_soc_dai_link *dl;

	dl = devm_kzalloc(dev, sizeof(*dl), GFP_KERNEL);
	platform = devm_kzalloc(dev, sizeof(*platform), GFP_KERNEL);
	if (!dl || !platform)
		return -ENOMEM;

	platform->name = platform_name;

	dl->name = devm_kasprintf(dev, GFP_KERNEL,
				  AVS_STRING_FMT("SSP", "-Codec", ssp_port, tdm_slot));
	dl->cpus = devm_kzalloc(dev, sizeof(*dl->cpus), GFP_KERNEL);
	dl->codecs = devm_kzalloc(dev, sizeof(*dl->codecs), GFP_KERNEL);
	if (!dl->name || !dl->cpus || !dl->codecs)
		return -ENOMEM;

	dl->cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
					    AVS_STRING_FMT("SSP", " Pin", ssp_port, tdm_slot));
	dl->codecs->name = devm_kasprintf(dev, GFP_KERNEL, "i2c-10EC5514:00");
	dl->codecs->dai_name = devm_kasprintf(dev, GFP_KERNEL, RT5514_CODEC_DAI);
	if (!dl->cpus->dai_name || !dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 1;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
	dl->init = avs_rt5514_codec_init;
	dl->be_hw_params_fixup = avs_rt5514_be_fixup;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_capture = 1;
	dl->ops = &avs_rt5514_ops;

	*dai_link = dl;

	return 0;
}

static int avs_rt5514_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct device *dev = &pdev->dev;
	const char *pname;
	int ssp_port, tdm_slot, ret;

	mach = dev_get_platdata(dev);
	pname = mach->mach_params.platform;

	ret = avs_mach_get_ssp_tdm(dev, mach, &ssp_port, &tdm_slot);
	if (ret)
		return ret;

	ret = avs_create_dai_link(dev, pname, ssp_port, tdm_slot, &dai_link);
	if (ret) {
		dev_err(dev, "Failed to create dai link: %d", ret);
		return ret;
	}

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->name = "avs_rt5514";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dai_link = dai_link;
	card->num_links = 1;
	card->dapm_widgets = card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(card_widgets);
	card->dapm_routes = card_base_routes;
	card->num_dapm_routes = ARRAY_SIZE(card_base_routes);
	card->fully_routed = true;

	ret = snd_soc_fixup_dai_links_platform_name(card, pname);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(dev, card);
}

static const struct platform_device_id avs_rt5514_driver_ids[] = {
	{
		.name = "avs_rt5514",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, avs_rt5514_driver_ids);

static struct platform_driver avs_rt5514_driver = {
	.probe = avs_rt5514_probe,
	.driver = {
		.name = "avs_rt5514",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = avs_rt5514_driver_ids,
};

module_platform_driver(avs_rt5514_driver);

MODULE_DESCRIPTION("Intel rt5514 machine driver");
MODULE_LICENSE("GPL");
