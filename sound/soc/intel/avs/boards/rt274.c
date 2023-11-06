// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
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
#include "../../../codecs/rt274.h"

#define AVS_RT274_FREQ_OUT	24000000
#define AVS_RT274_BE_FIXUP_RATE	48000
#define RT274_CODEC_DAI		"rt274-aif1"

static const struct snd_kcontrol_new card_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
};

static int
avs_rt274_clock_control(struct snd_soc_dapm_widget *w, struct snd_kcontrol *control, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	int ret;

	codec_dai = snd_soc_card_get_codec_dai(card, RT274_CODEC_DAI);
	if (!codec_dai)
		return -EINVAL;

	/* Codec needs clock for Jack detection and button press */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT274_SCLK_S_PLL2, AVS_RT274_FREQ_OUT,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "set codec sysclk failed: %d\n", ret);
		return ret;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		int ratio = 100;

		snd_soc_dai_set_bclk_ratio(codec_dai, ratio);

		ret = snd_soc_dai_set_pll(codec_dai, 0, RT274_PLL2_S_BCLK,
					  AVS_RT274_BE_FIXUP_RATE * ratio, AVS_RT274_FREQ_OUT);
		if (ret) {
			dev_err(codec_dai->dev, "failed to enable PLL2: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0, avs_rt274_clock_control,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route card_base_routes[] = {
	{"Headphone Jack", NULL, "HPO Pin"},
	{"MIC", NULL, "Mic Jack"},

	{"Headphone Jack", NULL, "Platform Clock"},
	{"MIC", NULL, "Platform Clock"},
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

static int avs_rt274_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(runtime, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_soc_jack_pin *pins;
	struct snd_soc_jack *jack;
	struct snd_soc_card *card = runtime->card;
	int num_pins, ret;

	jack = snd_soc_card_get_drvdata(card);
	num_pins = ARRAY_SIZE(card_headset_pins);

	pins = devm_kmemdup(card->dev, card_headset_pins, sizeof(*pins) * num_pins, GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	ret = snd_soc_card_jack_new_pins(card, "Headset", SND_JACK_HEADSET, jack, pins, num_pins);
	if (ret)
		return ret;

	snd_soc_component_set_jack(component, jack, NULL);

	/* TDM 4 slots 24 bit, set Rx & Tx bitmask to 4 active slots */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xF, 0xF, 4, 24);
	if (ret < 0) {
		dev_err(card->dev, "can't set codec pcm format %d\n", ret);
		return ret;
	}

	card->dapm.idle_bias_off = true;

	return 0;
}

static void avs_rt274_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	snd_soc_component_set_jack(asoc_rtd_to_codec(rtd, 0)->component, NULL, NULL);
}

static int avs_rt274_be_fixup(struct snd_soc_pcm_runtime *runtime, struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate, *channels;
	struct snd_mask *fmt;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will convert the FE rate to 48k, stereo */
	rate->min = rate->max = AVS_RT274_BE_FIXUP_RATE;
	channels->min = channels->max = 2;

	/* set SSPN to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int avs_create_dai_link(struct device *dev, const char *platform_name, int ssp_port,
			       struct snd_soc_dai_link **dai_link)
{
	struct snd_soc_dai_link_component *platform;
	struct snd_soc_dai_link *dl;

	dl = devm_kzalloc(dev, sizeof(*dl), GFP_KERNEL);
	platform = devm_kzalloc(dev, sizeof(*platform), GFP_KERNEL);
	if (!dl || !platform)
		return -ENOMEM;

	platform->name = platform_name;

	dl->name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-Codec", ssp_port);
	dl->cpus = devm_kzalloc(dev, sizeof(*dl->cpus), GFP_KERNEL);
	dl->codecs = devm_kzalloc(dev, sizeof(*dl->codecs), GFP_KERNEL);
	if (!dl->name || !dl->cpus || !dl->codecs)
		return -ENOMEM;

	dl->cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", ssp_port);
	dl->codecs->name = devm_kasprintf(dev, GFP_KERNEL, "i2c-INT34C2:00");
	dl->codecs->dai_name = devm_kasprintf(dev, GFP_KERNEL, RT274_CODEC_DAI);
	if (!dl->cpus->dai_name || !dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 1;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
	dl->init = avs_rt274_codec_init;
	dl->exit = avs_rt274_codec_exit;
	dl->be_hw_params_fixup = avs_rt274_be_fixup;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_capture = 1;
	dl->dpcm_playback = 1;

	*dai_link = dl;

	return 0;
}

static int avs_card_suspend_pre(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, RT274_CODEC_DAI);

	return snd_soc_component_set_jack(codec_dai->component, NULL, NULL);
}

static int avs_card_resume_post(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, RT274_CODEC_DAI);
	struct snd_soc_jack *jack = snd_soc_card_get_drvdata(card);

	return snd_soc_component_set_jack(codec_dai->component, jack, NULL);
}

static int avs_rt274_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct snd_soc_jack *jack;
	struct device *dev = &pdev->dev;
	const char *pname;
	int ssp_port, ret;

	mach = dev_get_platdata(dev);
	pname = mach->mach_params.platform;
	ssp_port = __ffs(mach->mach_params.i2s_link_mask);

	ret = avs_create_dai_link(dev, pname, ssp_port, &dai_link);
	if (ret) {
		dev_err(dev, "Failed to create dai link: %d", ret);
		return ret;
	}

	jack = devm_kzalloc(dev, sizeof(*jack), GFP_KERNEL);
	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!jack || !card)
		return -ENOMEM;

	card->name = "avs_rt274";
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

static struct platform_driver avs_rt274_driver = {
	.probe = avs_rt274_probe,
	.driver = {
		.name = "avs_rt274",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(avs_rt274_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:avs_rt274");
