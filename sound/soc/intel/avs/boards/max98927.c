// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include "../utils.h"

#define MAX98927_DEV0_NAME	"i2c-MX98927:00"
#define MAX98927_DEV1_NAME	"i2c-MX98927:01"
#define MAX98927_CODEC_NAME	"max98927-aif1"

static struct snd_soc_codec_conf card_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(MAX98927_DEV0_NAME),
		.name_prefix = "Right",
	},
	{
		.dlc = COMP_CODEC_CONF(MAX98927_DEV1_NAME),
		.name_prefix = "Left",
	},
};

static const struct snd_kcontrol_new card_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_soc_dapm_route card_base_routes[] = {
	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },
};

static int
avs_max98927_be_fixup(struct snd_soc_pcm_runtime *runrime, struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate, *channels;
	struct snd_mask *fmt;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will convert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP0 to 16 bit */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);
	return 0;
}

static int avs_max98927_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	int ret = 0;
	int i;

	for_each_rtd_codec_dais(runtime, i, codec_dai) {
		if (!strcmp(codec_dai->component->name, MAX98927_DEV0_NAME))
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x30, 3, 8, 16);
		else if (!strcmp(codec_dai->component->name, MAX98927_DEV1_NAME))
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xC0, 3, 8, 16);

		if (ret < 0) {
			dev_err(runtime->dev, "hw_params for %s failed: %d\n",
				codec_dai->component->name, ret);
			return ret;
		}
	}

	return 0;
}

static const struct snd_soc_ops avs_max98927_ops = {
	.hw_params = avs_max98927_hw_params,
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
	dl->codecs = devm_kzalloc(dev, sizeof(*dl->codecs) * 2, GFP_KERNEL);
	if (!dl->name || !dl->cpus || !dl->codecs)
		return -ENOMEM;

	dl->cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
					    AVS_STRING_FMT("SSP", " Pin", ssp_port, tdm_slot));
	dl->codecs[0].name = devm_kasprintf(dev, GFP_KERNEL, MAX98927_DEV0_NAME);
	dl->codecs[0].dai_name = devm_kasprintf(dev, GFP_KERNEL, MAX98927_CODEC_NAME);
	dl->codecs[1].name = devm_kasprintf(dev, GFP_KERNEL, MAX98927_DEV1_NAME);
	dl->codecs[1].dai_name = devm_kasprintf(dev, GFP_KERNEL, MAX98927_CODEC_NAME);
	if (!dl->cpus->dai_name || !dl->codecs[0].name || !dl->codecs[0].dai_name ||
	    !dl->codecs[1].name || !dl->codecs[1].dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 2;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
	dl->be_hw_params_fixup = avs_max98927_be_fixup;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_capture = 1;
	dl->dpcm_playback = 1;
	dl->ignore_pmdown_time = 1;
	dl->ops = &avs_max98927_ops;

	*dai_link = dl;

	return 0;
}

static int avs_max98927_probe(struct platform_device *pdev)
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

	card->name = "avs_max98927";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dai_link = dai_link;
	card->num_links = 1;
	card->codec_conf = card_codec_conf;
	card->num_configs = ARRAY_SIZE(card_codec_conf);
	card->controls = card_controls;
	card->num_controls = ARRAY_SIZE(card_controls);
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

static const struct platform_device_id avs_max98927_driver_ids[] = {
	{
		.name = "avs_max98927",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, avs_max98927_driver_ids);

static struct platform_driver avs_max98927_driver = {
	.probe = avs_max98927_probe,
	.driver = {
		.name = "avs_max98927",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = avs_max98927_driver_ids,
};

module_platform_driver(avs_max98927_driver)

MODULE_DESCRIPTION("Intel max98927 machine driver");
MODULE_LICENSE("GPL");
