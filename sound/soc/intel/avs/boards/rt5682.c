// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/clk.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/rt5682.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../common/soc-intel-quirks.h"
#include "../../../codecs/rt5682.h"

#define AVS_RT5682_SSP_CODEC(quirk)	((quirk) & GENMASK(2, 0))
#define AVS_RT5682_SSP_CODEC_MASK	(GENMASK(2, 0))
#define AVS_RT5682_MCLK_EN		BIT(3)
#define AVS_RT5682_MCLK_24MHZ		BIT(4)
#define AVS_RT5682_CODEC_DAI_NAME	"rt5682-aif1"

/* Default: MCLK on, MCLK 19.2M, SSP0 */
static unsigned long avs_rt5682_quirk = AVS_RT5682_MCLK_EN | AVS_RT5682_SSP_CODEC(0);

static int avs_rt5682_quirk_cb(const struct dmi_system_id *id)
{
	avs_rt5682_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id avs_rt5682_quirk_table[] = {
	{
		.callback = avs_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WhiskeyLake Client"),
		},
		.driver_data = (void *)(AVS_RT5682_MCLK_EN |
					AVS_RT5682_MCLK_24MHZ |
					AVS_RT5682_SSP_CODEC(1)),
	},
	{
		.callback = avs_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Ice Lake Client"),
		},
		.driver_data = (void *)(AVS_RT5682_MCLK_EN |
					AVS_RT5682_SSP_CODEC(0)),
	},
	{}
};

static const struct snd_kcontrol_new card_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route card_base_routes[] = {
	/* HP jack connectors - unknown if we have jack detect */
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },

	/* other jacks */
	{ "IN1P", NULL, "Headset Mic" },
};

static int avs_rt5682_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_component *component = asoc_rtd_to_codec(runtime, 0)->component;
	struct snd_soc_jack *jack;
	struct snd_soc_card *card = runtime->card;
	int ret;

	jack = snd_soc_card_get_drvdata(card);

	/* Need to enable ASRC function for 24MHz mclk rate */
	if ((avs_rt5682_quirk & AVS_RT5682_MCLK_EN) &&
	    (avs_rt5682_quirk & AVS_RT5682_MCLK_24MHZ)) {
		rt5682_sel_asrc_clk_src(component, RT5682_DA_STEREO1_FILTER |
					RT5682_AD_STEREO1_FILTER, RT5682_CLK_SEL_I2S1_ASRC);
	}

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new(card, "Headset", SND_JACK_HEADSET | SND_JACK_BTN_0 |
				    SND_JACK_BTN_1 | SND_JACK_BTN_2 | SND_JACK_BTN_3, jack);
	if (ret) {
		dev_err(card->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(card->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return 0;
};

static void avs_rt5682_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	snd_soc_component_set_jack(asoc_rtd_to_codec(rtd, 0)->component, NULL, NULL);
}

static int
avs_rt5682_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(runtime, 0);
	int clk_id, clk_freq;
	int pll_out, ret;

	if (avs_rt5682_quirk & AVS_RT5682_MCLK_EN) {
		clk_id = RT5682_PLL1_S_MCLK;
		if (avs_rt5682_quirk & AVS_RT5682_MCLK_24MHZ)
			clk_freq = 24000000;
		else
			clk_freq = 19200000;
	} else {
		clk_id = RT5682_PLL1_S_BCLK1;
		clk_freq = params_rate(params) * 50;
	}

	pll_out = params_rate(params) * 512;

	ret = snd_soc_dai_set_pll(codec_dai, 0, clk_id, clk_freq, pll_out);
	if (ret < 0)
		dev_err(runtime->dev, "snd_soc_dai_set_pll err = %d\n", ret);

	/* Configure sysclk for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682_SCLK_S_PLL1, pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(runtime->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);

	/* slot_width should equal or large than data length, set them be the same */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x0, 0x0, 2, params_width(params));
	if (ret < 0) {
		dev_err(runtime->dev, "set TDM slot err:%d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops avs_rt5682_ops = {
	.hw_params = avs_rt5682_hw_params,
};

static int
avs_rt5682_be_fixup(struct snd_soc_pcm_runtime *runtime, struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate, *channels;
	struct snd_mask *fmt;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will convert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
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
	dl->codecs->name = devm_kasprintf(dev, GFP_KERNEL, "i2c-10EC5682:00");
	dl->codecs->dai_name = devm_kasprintf(dev, GFP_KERNEL, AVS_RT5682_CODEC_DAI_NAME);
	if (!dl->cpus->dai_name || !dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 1;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->init = avs_rt5682_codec_init;
	dl->exit = avs_rt5682_codec_exit;
	dl->be_hw_params_fixup = avs_rt5682_be_fixup;
	dl->ops = &avs_rt5682_ops;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_capture = 1;
	dl->dpcm_playback = 1;

	*dai_link = dl;

	return 0;
}

static int avs_card_suspend_pre(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, AVS_RT5682_CODEC_DAI_NAME);

	return snd_soc_component_set_jack(codec_dai->component, NULL, NULL);
}

static int avs_card_resume_post(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, AVS_RT5682_CODEC_DAI_NAME);
	struct snd_soc_jack *jack = snd_soc_card_get_drvdata(card);

	return snd_soc_component_set_jack(codec_dai->component, jack, NULL);
}

static int avs_rt5682_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct snd_soc_jack *jack;
	struct device *dev = &pdev->dev;
	const char *pname;
	int ssp_port, ret;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		avs_rt5682_quirk = (unsigned long)pdev->id_entry->driver_data;

	dmi_check_system(avs_rt5682_quirk_table);
	dev_dbg(dev, "avs_rt5682_quirk = %lx\n", avs_rt5682_quirk);

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

	card->name = "avs_rt5682";
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

static struct platform_driver avs_rt5682_driver = {
	.probe = avs_rt5682_probe,
	.driver = {
		.name = "avs_rt5682",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(avs_rt5682_driver)

MODULE_AUTHOR("Cezary Rojewski <cezary.rojewski@intel.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:avs_rt5682");
