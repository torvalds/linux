// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../../codecs/nau8825.h"
#include "../utils.h"

#define SKL_SSM_CODEC_DAI	"ssm4567-hifi"

static struct snd_soc_codec_conf card_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF("i2c-INT343B:00"),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF("i2c-INT343B:01"),
		.name_prefix = "Right",
	},
};

static const struct snd_kcontrol_new card_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Speaker"),
	SOC_DAPM_PIN_SWITCH("Right Speaker"),
};

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_SPK("Left Speaker", NULL),
	SND_SOC_DAPM_SPK("Right Speaker", NULL),
	SND_SOC_DAPM_SPK("DP1", NULL),
	SND_SOC_DAPM_SPK("DP2", NULL),
};

static const struct snd_soc_dapm_route card_base_routes[] = {
	{"Left Speaker", NULL, "Left OUT"},
	{"Right Speaker", NULL, "Right OUT"},
};

static int avs_ssm4567_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;

	/* Slot 1 for left */
	ret = snd_soc_dai_set_tdm_slot(snd_soc_rtd_to_codec(runtime, 0), 0x01, 0x01, 2, 48);
	if (ret < 0)
		return ret;

	/* Slot 2 for right */
	ret = snd_soc_dai_set_tdm_slot(snd_soc_rtd_to_codec(runtime, 1), 0x02, 0x02, 2, 48);
	if (ret < 0)
		return ret;

	return 0;
}

static int
avs_ssm4567_be_fixup(struct snd_soc_pcm_runtime *runrime, struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate, *channels;
	struct snd_mask *fmt;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will convert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP0 to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);
	return 0;
}

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
	dl->codecs[0].name = devm_kasprintf(dev, GFP_KERNEL, "i2c-INT343B:00");
	dl->codecs[0].dai_name = devm_kasprintf(dev, GFP_KERNEL, "ssm4567-hifi");
	dl->codecs[1].name = devm_kasprintf(dev, GFP_KERNEL, "i2c-INT343B:01");
	dl->codecs[1].dai_name = devm_kasprintf(dev, GFP_KERNEL, "ssm4567-hifi");
	if (!dl->cpus->dai_name || !dl->codecs[0].name || !dl->codecs[0].dai_name ||
	    !dl->codecs[1].name || !dl->codecs[1].dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 2;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBS_CFS;
	dl->init = avs_ssm4567_codec_init;
	dl->be_hw_params_fixup = avs_ssm4567_be_fixup;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_capture = 1;
	dl->dpcm_playback = 1;
	dl->ignore_pmdown_time = 1;

	*dai_link = dl;

	return 0;
}

static int avs_ssm4567_probe(struct platform_device *pdev)
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

	card->name = "avs_ssm4567-adi";
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

static const struct platform_device_id avs_ssm4567_driver_ids[] = {
	{
		.name = "avs_ssm4567",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, avs_ssm4567_driver_ids);

static struct platform_driver avs_ssm4567_driver = {
	.probe = avs_ssm4567_probe,
	.driver = {
		.name = "avs_ssm4567",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = avs_ssm4567_driver_ids,
};

module_platform_driver(avs_ssm4567_driver)

MODULE_DESCRIPTION("Intel ssm4567 machine driver");
MODULE_LICENSE("GPL");
