// SPDX-License-Identifier: GPL-2.0-only
/*
 * ASoC machine driver for Intel Broadwell platforms with RT5650 codec
 *
 * Copyright 2019, The Chromium OS Authors.  All rights reserved.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>

#include "../../codecs/rt5645.h"

struct bdw_rt5650_priv {
	struct gpio_desc *gpio_hp_en;
	struct snd_soc_component *component;
};

static const struct snd_soc_dapm_widget bdw_rt5650_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("DMIC Pair1", NULL),
	SND_SOC_DAPM_MIC("DMIC Pair2", NULL),
};

static const struct snd_soc_dapm_route bdw_rt5650_map[] = {
	/* Speakers */
	{"Speaker", NULL, "SPOL"},
	{"Speaker", NULL, "SPOR"},

	/* Headset jack connectors */
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"IN1P", NULL, "Headset Mic"},
	{"IN1N", NULL, "Headset Mic"},

	/* Digital MICs
	 * DMIC Pair1 are the two DMICs connected on the DMICN1 connector.
	 * DMIC Pair2 are the two DMICs connected on the DMICN2 connector.
	 * Facing the camera, DMIC Pair1 are on the left side, DMIC Pair2
	 * are on the right side.
	 */
	{"DMIC L1", NULL, "DMIC Pair1"},
	{"DMIC R1", NULL, "DMIC Pair1"},
	{"DMIC L2", NULL, "DMIC Pair2"},
	{"DMIC R2", NULL, "DMIC Pair2"},

	/* CODEC BE connections */
	{"SSP0 CODEC IN", NULL, "AIF1 Capture"},
	{"AIF1 Playback", NULL, "SSP0 CODEC OUT"},
};

static const struct snd_kcontrol_new bdw_rt5650_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("DMIC Pair1"),
	SOC_DAPM_PIN_SWITCH("DMIC Pair2"),
};


static struct snd_soc_jack headphone_jack;
static struct snd_soc_jack mic_jack;

static struct snd_soc_jack_pin headphone_jack_pin = {
	.pin	= "Headphone",
	.mask	= SND_JACK_HEADPHONE,
};

static struct snd_soc_jack_pin mic_jack_pin = {
	.pin	= "Headset Mic",
	.mask	= SND_JACK_MICROPHONE,
};

static int broadwell_ssp0_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
						      SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *chan = hw_param_interval(params,
						      SNDRV_PCM_HW_PARAM_CHANNELS);

	/* The ADSP will covert the FE rate to 48k, max 4-channels */
	rate->min = rate->max = 48000;
	chan->min = 2;
	chan->max = 4;

	/* set SSP0 to 24 bit */
	snd_mask_set_format(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			    SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int bdw_rt5650_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int ret;

	/* Workaround: set codec PLL to 19.2MHz that PLL source is
	 * from MCLK(24MHz) to conform 2.4MHz DMIC clock.
	 */
	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5645_PLL1_S_MCLK,
		24000000, 19200000);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	/* The actual MCLK freq is 24MHz. The codec is told that MCLK is
	 * 24.576MHz to satisfy the requirement of rl6231_get_clk_info.
	 * ASRC is enabled on AD and DA filters to ensure good audio quality.
	 */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5645_SCLK_S_PLL1, 24576000,
		SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk configuration\n");
		return ret;
	}

	return ret;
}

static struct snd_soc_ops bdw_rt5650_ops = {
	.hw_params = bdw_rt5650_hw_params,
};

static const unsigned int channels[] = {
	2, 4,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static int bdw_rt5650_fe_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	/* Board supports stereo and quad configurations for capture */
	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE)
		return 0;

	runtime->hw.channels_max = 4;
	return snd_pcm_hw_constraint_list(runtime, 0,
					  SNDRV_PCM_HW_PARAM_CHANNELS,
					  &constraints_channels);
}

static const struct snd_soc_ops bdw_rt5650_fe_ops = {
	.startup = bdw_rt5650_fe_startup,
};

static int bdw_rt5650_init(struct snd_soc_pcm_runtime *rtd)
{
	struct bdw_rt5650_priv *bdw_rt5650 =
		snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	int ret;

	/* Enable codec ASRC function for Stereo DAC/Stereo1 ADC/DMIC/I2S1.
	 * The ASRC clock source is clk_i2s1_asrc.
	 */
	rt5645_sel_asrc_clk_src(component,
				RT5645_DA_STEREO_FILTER |
				RT5645_DA_MONO_L_FILTER |
				RT5645_DA_MONO_R_FILTER |
				RT5645_AD_STEREO_FILTER |
				RT5645_AD_MONO_L_FILTER |
				RT5645_AD_MONO_R_FILTER,
				RT5645_CLK_SEL_I2S1_ASRC);

	/* TDM 4 slots 24 bit, set Rx & Tx bitmask to 4 active slots */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xF, 0xF, 4, 24);

	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec TDM slot %d\n", ret);
		return ret;
	}

	/* Create and initialize headphone jack */
	if (snd_soc_card_jack_new(rtd->card, "Headphone Jack",
			SND_JACK_HEADPHONE, &headphone_jack,
			&headphone_jack_pin, 1)) {
		dev_err(component->dev, "Can't create headphone jack\n");
	}

	/* Create and initialize mic jack */
	if (snd_soc_card_jack_new(rtd->card, "Mic Jack", SND_JACK_MICROPHONE,
			&mic_jack, &mic_jack_pin, 1)) {
		dev_err(component->dev, "Can't create mic jack\n");
	}

	rt5645_set_jack_detect(component, &headphone_jack, &mic_jack, NULL);

	bdw_rt5650->component = component;

	return 0;
}

/* broadwell digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(fe,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("haswell-pcm-audio")));

SND_SOC_DAILINK_DEF(be,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5650:00", "rt5645-aif1")));

SND_SOC_DAILINK_DEF(ssp0_port,
	    DAILINK_COMP_ARRAY(COMP_CPU("ssp0-port")));

static struct snd_soc_dai_link bdw_rt5650_dais[] = {
	/* Front End DAI links */
	{
		.name = "System PCM",
		.stream_name = "System Playback",
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &bdw_rt5650_fe_ops,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(fe, dummy, platform),
	},

	/* Back End DAI links */
	{
		/* SSP0 - Codec */
		.name = "Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = broadwell_ssp0_fixup,
		.ops = &bdw_rt5650_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = bdw_rt5650_init,
		SND_SOC_DAILINK_REG(ssp0_port, be, platform),
	},
};

/* use space before codec name to simplify card ID, and simplify driver name */
#define SOF_CARD_NAME "bdw rt5650" /* card name will be 'sof-bdw rt5650' */
#define SOF_DRIVER_NAME "SOF"

#define CARD_NAME "bdw-rt5650"
#define DRIVER_NAME NULL /* card name will be used for driver name */

/* ASoC machine driver for Broadwell DSP + RT5650 */
static struct snd_soc_card bdw_rt5650_card = {
	.name = CARD_NAME,
	.driver_name = DRIVER_NAME,
	.owner = THIS_MODULE,
	.dai_link = bdw_rt5650_dais,
	.num_links = ARRAY_SIZE(bdw_rt5650_dais),
	.dapm_widgets = bdw_rt5650_widgets,
	.num_dapm_widgets = ARRAY_SIZE(bdw_rt5650_widgets),
	.dapm_routes = bdw_rt5650_map,
	.num_dapm_routes = ARRAY_SIZE(bdw_rt5650_map),
	.controls = bdw_rt5650_controls,
	.num_controls = ARRAY_SIZE(bdw_rt5650_controls),
	.fully_routed = true,
};

static int bdw_rt5650_probe(struct platform_device *pdev)
{
	struct bdw_rt5650_priv *bdw_rt5650;
	struct snd_soc_acpi_mach *mach;
	int ret;

	bdw_rt5650_card.dev = &pdev->dev;

	/* Allocate driver private struct */
	bdw_rt5650 = devm_kzalloc(&pdev->dev, sizeof(struct bdw_rt5650_priv),
		GFP_KERNEL);
	if (!bdw_rt5650)
		return -ENOMEM;

	/* override platform name, if required */
	mach = pdev->dev.platform_data;
	ret = snd_soc_fixup_dai_links_platform_name(&bdw_rt5650_card,
						    mach->mach_params.platform);

	if (ret)
		return ret;

	/* set card and driver name */
	if (snd_soc_acpi_sof_parent(&pdev->dev)) {
		bdw_rt5650_card.name = SOF_CARD_NAME;
		bdw_rt5650_card.driver_name = SOF_DRIVER_NAME;
	} else {
		bdw_rt5650_card.name = CARD_NAME;
		bdw_rt5650_card.driver_name = DRIVER_NAME;
	}

	snd_soc_card_set_drvdata(&bdw_rt5650_card, bdw_rt5650);

	return devm_snd_soc_register_card(&pdev->dev, &bdw_rt5650_card);
}

static struct platform_driver bdw_rt5650_audio = {
	.probe = bdw_rt5650_probe,
	.driver = {
		.name = "bdw-rt5650",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(bdw_rt5650_audio)

/* Module information */
MODULE_AUTHOR("Ben Zhang <benzh@chromium.org>");
MODULE_DESCRIPTION("Intel Broadwell RT5650 machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bdw-rt5650");
