// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/dmi.h>
#include <linux/module.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../../codecs/rt298.h"
#include "../utils.h"

#define RT298_CODEC_DAI		"rt298-aif1"

static const struct dmi_system_id kblr_dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Kabylake R DDR4 RVP"),
		},
	},
	{}
};

static const struct snd_kcontrol_new card_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route card_base_routes[] = {
	/* HP jack connectors - unknown if we have jack detect */
	{"Headphone Jack", NULL, "HPO Pin"},
	{"MIC1", NULL, "Mic Jack"},

	{"Speaker", NULL, "SPOR"},
	{"Speaker", NULL, "SPOL"},
};

static struct snd_soc_jack_pin card_headset_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Mic Jack",
		.mask = SND_JACK_MICROPHONE,
	},
};

static int avs_rt298_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_jack_pin *pins;
	struct snd_soc_jack *jack;
	int num_pins, ret;

	jack = snd_soc_card_get_drvdata(card);
	num_pins = ARRAY_SIZE(card_headset_pins);

	pins = devm_kmemdup(card->dev, card_headset_pins, sizeof(*pins) * num_pins, GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	ret = snd_soc_card_jack_new_pins(card, "Headset", SND_JACK_HEADSET | SND_JACK_BTN_0, jack,
					 pins, num_pins);
	if (ret)
		return ret;

	return snd_soc_component_set_jack(snd_soc_rtd_to_codec(runtime, 0)->component, jack, NULL);
}

static void avs_rt298_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	snd_soc_component_set_jack(snd_soc_rtd_to_codec(rtd, 0)->component, NULL, NULL);
}

static int avs_rt298_be_fixup(struct snd_soc_pcm_runtime *runtime, struct snd_pcm_hw_params *params)
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

static int
avs_rt298_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int clk_freq;
	int ret;

	if (dmi_first_match(kblr_dmi_table))
		clk_freq = 24000000;
	else
		clk_freq = 19200000;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT298_SCLK_S_PLL, clk_freq, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "Set codec sysclk failed: %d\n", ret);

	return ret;
}

static const struct snd_soc_ops avs_rt298_ops = {
	.hw_params = avs_rt298_hw_params,
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
	dl->codecs->name = devm_kasprintf(dev, GFP_KERNEL, "i2c-INT343A:00");
	dl->codecs->dai_name = devm_kasprintf(dev, GFP_KERNEL, RT298_CODEC_DAI);
	if (!dl->cpus->dai_name || !dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 1;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	if (dmi_first_match(kblr_dmi_table))
		dl->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
	else
		dl->dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
	dl->init = avs_rt298_codec_init;
	dl->exit = avs_rt298_codec_exit;
	dl->be_hw_params_fixup = avs_rt298_be_fixup;
	dl->ops = &avs_rt298_ops;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_capture = 1;
	dl->dpcm_playback = 1;

	*dai_link = dl;

	return 0;
}

static int avs_card_suspend_pre(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, RT298_CODEC_DAI);

	return snd_soc_component_set_jack(codec_dai->component, NULL, NULL);
}

static int avs_card_resume_post(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, RT298_CODEC_DAI);
	struct snd_soc_jack *jack = snd_soc_card_get_drvdata(card);

	return snd_soc_component_set_jack(codec_dai->component, jack, NULL);
}

static int avs_rt298_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct snd_soc_jack *jack;
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

	jack = devm_kzalloc(dev, sizeof(*jack), GFP_KERNEL);
	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!jack || !card)
		return -ENOMEM;

	card->name = "avs_rt298";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->suspend_pre = avs_card_suspend_pre;
	card->resume_post = avs_card_resume_post;
	card->dai_link = dai_link;
	card->num_links = 1;
	card->controls = card_controls;
	card->num_controls = ARRAY_SIZE(card_controls);
	card->dapm_widgets = card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(card_widgets);
	card->dapm_routes = card_base_routes;
	card->num_dapm_routes = ARRAY_SIZE(card_base_routes);
	card->fully_routed = true;
	snd_soc_card_set_drvdata(card, jack);

	ret = snd_soc_fixup_dai_links_platform_name(card, pname);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(dev, card);
}

static const struct platform_device_id avs_rt298_driver_ids[] = {
	{
		.name = "avs_rt298",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, avs_rt298_driver_ids);

static struct platform_driver avs_rt298_driver = {
	.probe = avs_rt298_probe,
	.driver = {
		.name = "avs_rt298",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = avs_rt298_driver_ids,
};

module_platform_driver(avs_rt298_driver);

MODULE_DESCRIPTION("Intel rt298 machine driver");
MODULE_LICENSE("GPL");
