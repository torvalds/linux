// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <uapi/linux/input-event-codes.h>
#include "../../../codecs/da7219.h"
#include "../../../codecs/da7219-aad.h"

#define DA7219_DAI_NAME		"da7219-hifi"

static const struct snd_kcontrol_new card_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	int ret = 0;

	codec_dai = snd_soc_card_get_codec_dai(card, DA7219_DAI_NAME);
	if (!codec_dai) {
		dev_err(card->dev, "Codec dai not found. Unable to set/unset codec pll\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_MCLK, 0, 0);
		if (ret)
			dev_err(card->dev, "failed to stop PLL: %d\n", ret);
	} else if (SND_SOC_DAPM_EVENT_ON(event)) {
		ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_PLL_SRM,
					  0, DA7219_PLL_FREQ_OUT_98304);
		if (ret)
			dev_err(card->dev, "failed to start PLL: %d\n", ret);
	}

	return ret;
}

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0, platform_clock_control,
			    SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_PRE_PMU),
};

static const struct snd_soc_dapm_route card_base_routes[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{"Headphone Jack", NULL, "HPL"},
	{"Headphone Jack", NULL, "HPR"},

	{"MIC", NULL, "Headset Mic"},

	{ "Headphone Jack", NULL, "Platform Clock" },
	{ "Headset Mic", NULL, "Platform Clock" },
};

static int avs_da7219_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_component *component = asoc_rtd_to_codec(runtime, 0)->component;
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_jack *jack;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(runtime, 0);
	int clk_freq;
	int ret;

	jack = snd_soc_card_get_drvdata(card);
	clk_freq = 19200000;

	ret = snd_soc_dai_set_sysclk(codec_dai, DA7219_CLKSRC_MCLK, clk_freq, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(card->dev, "can't set codec sysclk configuration\n");
		return ret;
	}

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new(card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_BTN_0 |
				    SND_JACK_BTN_1 | SND_JACK_BTN_2 |
				    SND_JACK_BTN_3 | SND_JACK_LINEOUT, jack);
	if (ret) {
		dev_err(card->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);

	da7219_aad_jack_det(component, jack);

	return 0;
}

static int
avs_da7219_be_fixup(struct snd_soc_pcm_runtime *runrime, struct snd_pcm_hw_params *params)
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
	dl->codecs->name = devm_kasprintf(dev, GFP_KERNEL, "i2c-DLGS7219:00");
	dl->codecs->dai_name = devm_kasprintf(dev, GFP_KERNEL, DA7219_DAI_NAME);
	if (!dl->cpus->dai_name || !dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 1;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
	dl->be_hw_params_fixup = avs_da7219_be_fixup;
	dl->init = avs_da7219_codec_init;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_capture = 1;
	dl->dpcm_playback = 1;

	*dai_link = dl;

	return 0;
}

static int avs_create_dapm_routes(struct device *dev, int ssp_port,
				  struct snd_soc_dapm_route **routes, int *num_routes)
{
	struct snd_soc_dapm_route *dr;
	const int num_base = ARRAY_SIZE(card_base_routes);
	const int num_dr = num_base + 2;
	int idx;

	dr = devm_kcalloc(dev, num_dr, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	memcpy(dr, card_base_routes, num_base * sizeof(*dr));

	idx = num_base;
	dr[idx].sink = devm_kasprintf(dev, GFP_KERNEL, "Playback");
	dr[idx].source = devm_kasprintf(dev, GFP_KERNEL, "ssp%d Tx", ssp_port);
	if (!dr[idx].sink || !dr[idx].source)
		return -ENOMEM;

	idx++;
	dr[idx].sink = devm_kasprintf(dev, GFP_KERNEL, "ssp%d Rx", ssp_port);
	dr[idx].source = devm_kasprintf(dev, GFP_KERNEL, "Capture");
	if (!dr[idx].sink || !dr[idx].source)
		return -ENOMEM;

	*routes = dr;
	*num_routes = num_dr;

	return 0;
}

static int avs_card_set_jack(struct snd_soc_card *card, struct snd_soc_jack *jack)
{
	struct snd_soc_component *component;

	for_each_card_components(card, component)
		snd_soc_component_set_jack(component, jack, NULL);
	return 0;
}

static int avs_card_remove(struct snd_soc_card *card)
{
	return avs_card_set_jack(card, NULL);
}

static int avs_card_suspend_pre(struct snd_soc_card *card)
{
	return avs_card_set_jack(card, NULL);
}

static int avs_card_resume_post(struct snd_soc_card *card)
{
	struct snd_soc_jack *jack = snd_soc_card_get_drvdata(card);

	return avs_card_set_jack(card, jack);
}

static int avs_da7219_probe(struct platform_device *pdev)
{
	struct snd_soc_dapm_route *routes;
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct snd_soc_jack *jack;
	struct device *dev = &pdev->dev;
	const char *pname;
	int num_routes, ssp_port, ret;

	mach = dev_get_platdata(dev);
	pname = mach->mach_params.platform;
	ssp_port = __ffs(mach->mach_params.i2s_link_mask);

	ret = avs_create_dai_link(dev, pname, ssp_port, &dai_link);
	if (ret) {
		dev_err(dev, "Failed to create dai link: %d", ret);
		return ret;
	}

	ret = avs_create_dapm_routes(dev, ssp_port, &routes, &num_routes);
	if (ret) {
		dev_err(dev, "Failed to create dapm routes: %d", ret);
		return ret;
	}

	jack = devm_kzalloc(dev, sizeof(*jack), GFP_KERNEL);
	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!jack || !card)
		return -ENOMEM;

	card->name = "avs_da7219";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->remove = avs_card_remove;
	card->suspend_pre = avs_card_suspend_pre;
	card->resume_post = avs_card_resume_post;
	card->dai_link = dai_link;
	card->num_links = 1;
	card->controls = card_controls;
	card->num_controls = ARRAY_SIZE(card_controls);
	card->dapm_widgets = card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(card_widgets);
	card->dapm_routes = routes;
	card->num_dapm_routes = num_routes;
	card->fully_routed = true;
	snd_soc_card_set_drvdata(card, jack);

	ret = snd_soc_fixup_dai_links_platform_name(card, pname);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(dev, card);
}

static struct platform_driver avs_da7219_driver = {
	.probe = avs_da7219_probe,
	.driver = {
		.name = "avs_da7219",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(avs_da7219_driver);

MODULE_AUTHOR("Cezary Rojewski <cezary.rojewski@intel.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:avs_da7219");
