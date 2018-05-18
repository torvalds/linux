/*
 *  cnl_rt274.c - ASOC Machine driver for CNL
 *
 *  Copyright (C) 2016 Intel Corp
 *  Author: Guneshwor Singh <guneshwor.o.singh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/acpi.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <linux/input.h>

#include "../../codecs/rt274.h"

#define CNL_FREQ_OUT		24000000
#define CNL_BE_FIXUP_RATE	48000
#define RT274_CODEC_DAI		"rt274-aif1"

static int cnl_rt274_clock_control(struct snd_soc_dapm_widget *w,
				   struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	int ret = 0;
	int ratio = 100;

	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card,
							  RT274_CODEC_DAI);
	if (!codec_dai)
		return -EINVAL;

	/* Codec needs clock for Jack detection and button press */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT274_SCLK_S_PLL2,
				     CNL_FREQ_OUT, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "set codec sysclk failed: %d\n", ret);
		return ret;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_dai_set_bclk_ratio(codec_dai, ratio);

		ret = snd_soc_dai_set_pll(codec_dai, 0, RT274_PLL2_S_BCLK,
					  CNL_BE_FIXUP_RATE * ratio,
					  CNL_FREQ_OUT);
		if (ret) {
			dev_err(codec_dai->dev,
				"failed to enable PLL2: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static struct snd_soc_jack cnl_headset;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin cnl_headset_pins[] = {
	{
		.pin = "Mic Jack",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
};

static const struct snd_kcontrol_new cnl_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
};

static const struct snd_soc_dapm_widget cnl_rt274_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    cnl_rt274_clock_control, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMD),
};

#if !IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL)
static int cnl_dmic_fixup(struct snd_soc_pcm_runtime *rtd,
				struct snd_pcm_hw_params *params)
{
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	channels->min = channels->max = 2;

	return 0;
}
#endif

static const struct snd_soc_dapm_route cnl_map[] = {
	{"Headphone Jack", NULL, "HPO Pin"},
	{"MIC", NULL, "Mic Jack"},
	{"DMic", NULL, "SoC DMIC"},
	{"DMIC01 Rx", NULL, "Capture"},
	{"dmic01_hifi", NULL, "DMIC01 Rx"},

	/* ssp2 path */
	{"Dummy Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "ssp2_out"},

	{"ssp2 Rx", NULL, "Dummy Capture"},
	{"ssp2_in", NULL, "ssp2 Rx"},

	/* ssp1 path */
	{"Dummy Playback", NULL, "ssp1 Tx"},
	{"ssp1 Tx", NULL, "ssp1_out"},

	{"AIF1 Playback", NULL, "ssp0 Tx"},
	{"ssp0 Tx", NULL, "codec1_out"},
	{"ssp0 Tx", NULL, "codec0_out"},

	{"ssp0 Rx", NULL, "AIF1 Capture"},
	{"codec0_in", NULL, "ssp0 Rx"},

	{"Headphone Jack", NULL, "Platform Clock"},
	{"MIC", NULL, "Platform Clock"},
};

static int cnl_rt274_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	struct snd_soc_component *component = runtime->codec_dai->component;
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_dai *codec_dai = runtime->codec_dai;

	ret = snd_soc_card_jack_new(runtime->card, "Headset",
				    SND_JACK_HEADSET, &cnl_headset,
				    cnl_headset_pins,
				    ARRAY_SIZE(cnl_headset_pins));

	if (ret)
		return ret;

	snd_soc_component_set_jack(component, &cnl_headset, NULL);

	/* TDM 4 slots 24 bit, set Rx & Tx bitmask to 4 active slots */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xF, 0xF, 4, 24);
	if (ret < 0) {
		dev_err(runtime->dev, "can't set codec pcm format %d\n", ret);
		return ret;
	}

	card->dapm.idle_bias_off = true;

	return 0;
}

static int cnl_be_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = CNL_BE_FIXUP_RATE;
	rate->max = CNL_BE_FIXUP_RATE;
	channels->min = 2;
	channels->max = 2;
	snd_mask_none(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT));
	snd_mask_set(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			(unsigned int __force)SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

#if IS_ENABLED(CONFIG_SND_SOC_INTEL_CNL_FPGA)
static const char pname[] = "0000:02:18.0";
static const char cname[] = "rt274.0-001c";
#else
static const char pname[] = "0000:00:1f.3";
static const char cname[] = "i2c-INT34C2:00";
#endif

static struct snd_soc_dai_link cnl_rt274_msic_dailink[] = {
#if !IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL)
	/* Trace Buffer DAI links */
	{
		.name = "CNL Trace Buffer0",
		.stream_name = "Core 0 Trace Buffer",
		.cpu_dai_name = "TraceBuffer0 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = pname,
		.capture_only = true,
		.ignore_suspend = 1,
	},
	{
		.name = "CNL Trace Buffer1",
		.stream_name = "Core 1 Trace Buffer",
		.cpu_dai_name = "TraceBuffer1 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = pname,
		.capture_only = true,
		.ignore_suspend = 1,
	},
	{
		.name = "CNL Trace Buffer2",
		.stream_name = "Core 2 Trace Buffer",
		.cpu_dai_name = "TraceBuffer2 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = pname,
		.capture_only = true,
		.ignore_suspend = 1,
	},
	{
		.name = "CNL Trace Buffer3",
		.stream_name = "Core 3 Trace Buffer",
		.cpu_dai_name = "TraceBuffer3 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = pname,
		.capture_only = true,
		.ignore_suspend = 1,
	},
	/* Probe DAI-links */
	{
		.name = "CNL Compress Probe playback",
		.stream_name = "Probe Playback",
		.cpu_dai_name = "Compress Probe0 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = pname,
		.init = NULL,
		.ignore_suspend = 1,
		.nonatomic = 1,
	},
	{
		.name = "CNL Compress Probe capture",
		.stream_name = "Probe Capture",
		.cpu_dai_name = "Compress Probe1 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = pname,
		.init = NULL,
		.ignore_suspend = 1,
		.nonatomic = 1,
	},
#endif
	/* back ends */
	{
		.name = "SSP0-Codec",
		.id = 1,
		.cpu_dai_name = "SSP0 Pin",
		.codec_name = cname,
		.codec_dai_name = "rt274-aif1",
		.be_hw_params_fixup = cnl_be_fixup,
		.ignore_suspend = 1,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = cnl_rt274_init,
	},
#if !IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL)
	{
		.name = "dmic01",
		.id = 2,
		.cpu_dai_name = "DMIC01 Pin",
		.codec_name = "dmic-codec",
		.codec_dai_name = "dmic-hifi",
		.ignore_suspend = 1,
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_hw_params_fixup = cnl_dmic_fixup,
	},
#endif
};

static int
cnl_add_dai_link(struct snd_soc_card *card, struct snd_soc_dai_link *link)
{
       link->platform_name = pname;
       link->nonatomic = 1;

       return 0;
}

/* SoC card */
static struct snd_soc_card snd_soc_card_cnl = {
	.name = "cnl-audio",
	.dai_link = cnl_rt274_msic_dailink,
	.num_links = ARRAY_SIZE(cnl_rt274_msic_dailink),
	.dapm_widgets = cnl_rt274_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cnl_rt274_widgets),
	.dapm_routes = cnl_map,
	.num_dapm_routes = ARRAY_SIZE(cnl_map),
	.controls = cnl_controls,
	.num_controls = ARRAY_SIZE(cnl_controls),
	.add_dai_link = cnl_add_dai_link,
	.fully_routed = true,
};

static int snd_cnl_rt274_mc_probe(struct platform_device *pdev)
{
	snd_soc_card_cnl.dev = &pdev->dev;
	return devm_snd_soc_register_card(&pdev->dev, &snd_soc_card_cnl);
}

static struct platform_driver snd_cnl_rt274_driver = {
	.driver = {
		.name = "cnl_rt274",
		.pm = &snd_soc_pm_ops,
	},
	.probe = snd_cnl_rt274_mc_probe,
};

module_platform_driver(snd_cnl_rt274_driver);

MODULE_AUTHOR("Guneshwor Singh <guneshwor.o.singh@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cnl_rt274");
