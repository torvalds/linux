// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Kabylake I2S Machine Driver with MAXIM98927
 * RT5514 and RT5663 Codecs
 *
 * Copyright (C) 2017, Intel Corporation. All rights reserved.
 *
 * Modified from:
 *   Intel Kabylake I2S Machine driver supporting MAXIM98927 and
 *   RT5663 codecs
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt5514.h"
#include "../../codecs/rt5663.h"
#include "../../codecs/hdac_hdmi.h"
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>

#define KBL_REALTEK_CODEC_DAI "rt5663-aif"
#define KBL_REALTEK_DMIC_CODEC_DAI "rt5514-aif1"
#define KBL_MAXIM_CODEC_DAI "max98927-aif1"
#define MAXIM_DEV0_NAME "i2c-MX98927:00"
#define MAXIM_DEV1_NAME "i2c-MX98927:01"
#define RT5514_DEV_NAME "i2c-10EC5514:00"
#define RT5663_DEV_NAME "i2c-10EC5663:00"
#define RT5514_AIF1_BCLK_FREQ (48000 * 8 * 16)
#define RT5514_AIF1_SYSCLK_FREQ 12288000
#define NAME_SIZE 32

#define DMIC_CH(p) p->list[p->count-1]


static struct snd_soc_card kabylake_audio_card;
static const struct snd_pcm_hw_constraint_list *dmic_constraints;

struct kbl_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct kbl_codec_private {
	struct snd_soc_jack kabylake_headset;
	struct list_head hdmi_pcm_list;
	struct snd_soc_jack kabylake_hdmi[2];
	struct clk *mclk;
	struct clk *sclk;
};

enum {
	KBL_DPCM_AUDIO_PB = 0,
	KBL_DPCM_AUDIO_CP,
	KBL_DPCM_AUDIO_HS_PB,
	KBL_DPCM_AUDIO_ECHO_REF_CP,
	KBL_DPCM_AUDIO_DMIC_CP,
	KBL_DPCM_AUDIO_RT5514_DSP,
	KBL_DPCM_AUDIO_HDMI1_PB,
	KBL_DPCM_AUDIO_HDMI2_PB,
};

static const struct snd_kcontrol_new kabylake_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
	SOC_DAPM_PIN_SWITCH("DMIC"),
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct kbl_codec_private *priv = snd_soc_card_get_drvdata(card);
	int ret = 0;

	/*
	 * MCLK/SCLK need to be ON early for a successful synchronization of
	 * codec internal clock. And the clocks are turned off during
	 * POST_PMD after the stream is stopped.
	 */
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable MCLK */
		ret = clk_set_rate(priv->mclk, 24000000);
		if (ret < 0) {
			dev_err(card->dev, "Can't set rate for mclk, err: %d\n",
				ret);
			return ret;
		}

		ret = clk_prepare_enable(priv->mclk);
		if (ret < 0) {
			dev_err(card->dev, "Can't enable mclk, err: %d\n", ret);
			return ret;
		}

		/* Enable SCLK */
		ret = clk_set_rate(priv->sclk, 3072000);
		if (ret < 0) {
			dev_err(card->dev, "Can't set rate for sclk, err: %d\n",
				ret);
			clk_disable_unprepare(priv->mclk);
			return ret;
		}

		ret = clk_prepare_enable(priv->sclk);
		if (ret < 0) {
			dev_err(card->dev, "Can't enable sclk, err: %d\n", ret);
			clk_disable_unprepare(priv->mclk);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		clk_disable_unprepare(priv->mclk);
		clk_disable_unprepare(priv->sclk);
		break;
	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget kabylake_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
	SND_SOC_DAPM_MIC("DMIC", NULL),
	SND_SOC_DAPM_SPK("HDMI1", NULL),
	SND_SOC_DAPM_SPK("HDMI2", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			platform_clock_control, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),

};

static const struct snd_soc_dapm_route kabylake_map[] = {
	/* Headphones */
	{ "Headphone Jack", NULL, "Platform Clock" },
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },

	/* speaker */
	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },

	/* other jacks */
	{ "Headset Mic", NULL, "Platform Clock" },
	{ "IN1P", NULL, "Headset Mic" },
	{ "IN1N", NULL, "Headset Mic" },

	/* CODEC BE connections */
	{ "Left HiFi Playback", NULL, "ssp0 Tx" },
	{ "Right HiFi Playback", NULL, "ssp0 Tx" },
	{ "ssp0 Tx", NULL, "spk_out" },

	{ "AIF Playback", NULL, "ssp1 Tx" },
	{ "ssp1 Tx", NULL, "codec1_out" },

	{ "hs_in", NULL, "ssp1 Rx" },
	{ "ssp1 Rx", NULL, "AIF Capture" },

	{ "codec1_in", NULL, "ssp0 Rx" },
	{ "ssp0 Rx", NULL, "AIF1 Capture" },

	/* IV feedback path */
	{ "codec0_fb_in", NULL, "ssp0 Rx"},
	{ "ssp0 Rx", NULL, "Left HiFi Capture" },
	{ "ssp0 Rx", NULL, "Right HiFi Capture" },

	/* DMIC */
	{ "DMIC1L", NULL, "DMIC" },
	{ "DMIC1R", NULL, "DMIC" },
	{ "DMIC2L", NULL, "DMIC" },
	{ "DMIC2R", NULL, "DMIC" },

	{ "hifi2", NULL, "iDisp2 Tx" },
	{ "iDisp2 Tx", NULL, "iDisp2_out" },
	{ "hifi1", NULL, "iDisp1 Tx" },
	{ "iDisp1 Tx", NULL, "iDisp1_out" },
};

static struct snd_soc_codec_conf max98927_codec_conf[] = {
	{
		.dev_name = MAXIM_DEV0_NAME,
		.name_prefix = "Right",
	},
	{
		.dev_name = MAXIM_DEV1_NAME,
		.name_prefix = "Left",
	},
};


static int kabylake_rt5663_fe_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dapm_context *dapm;
	struct snd_soc_component *component = rtd->cpu_dai->component;
	int ret;

	dapm = snd_soc_component_get_dapm(component);
	ret = snd_soc_dapm_ignore_suspend(dapm, "Reference Capture");
	if (ret)
		dev_err(rtd->dev, "Ref Cap -Ignore suspend failed = %d\n", ret);

	return ret;
}

static int kabylake_rt5663_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = rtd->codec_dai->component;
	struct snd_soc_jack *jack;

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new(&kabylake_audio_card, "Headset Jack",
			SND_JACK_HEADSET | SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3, &ctx->kabylake_headset,
			NULL, 0);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed %d\n", ret);
		return ret;
	}

	jack = &ctx->kabylake_headset;
	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	snd_soc_component_set_jack(component, &ctx->kabylake_headset, NULL);

	ret = snd_soc_dapm_ignore_suspend(&rtd->card->dapm, "DMIC");
	if (ret)
		dev_err(rtd->dev, "DMIC - Ignore suspend failed = %d\n", ret);

	return ret;
}

static int kabylake_hdmi_init(struct snd_soc_pcm_runtime *rtd, int device)
{
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = rtd->codec_dai;
	struct kbl_hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->device = device;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

static int kabylake_hdmi1_init(struct snd_soc_pcm_runtime *rtd)
{
	return kabylake_hdmi_init(rtd, KBL_DPCM_AUDIO_HDMI1_PB);
}

static int kabylake_hdmi2_init(struct snd_soc_pcm_runtime *rtd)
{
	return kabylake_hdmi_init(rtd, KBL_DPCM_AUDIO_HDMI2_PB);
}

static const unsigned int rates[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list  = rates,
	.mask = 0,
};

static const unsigned int channels[] = {
	2,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static int kbl_fe_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	/*
	 * On this platform for PCM device we support,
	 * 48Khz
	 * stereo
	 * 16 bit audio
	 */

	runtime->hw.channels_max = 2;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					   &constraints_channels);

	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_msbits(runtime, 0, 16, 16);

	snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);

	return 0;
}

static const struct snd_soc_ops kabylake_rt5663_fe_ops = {
	.startup = kbl_fe_startup,
};

static int kabylake_ssp_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_soc_dpcm *dpcm = container_of(
			params, struct snd_soc_dpcm, hw_params);
	struct snd_soc_dai_link *fe_dai_link = dpcm->fe->dai_link;
	struct snd_soc_dai_link *be_dai_link = dpcm->be->dai_link;

	/*
	 * The ADSP will convert the FE rate to 48k, stereo, 24 bit
	 */
	if (!strcmp(fe_dai_link->name, "Kbl Audio Port") ||
	    !strcmp(fe_dai_link->name, "Kbl Audio Headset Playback") ||
	    !strcmp(fe_dai_link->name, "Kbl Audio Capture Port")) {
		rate->min = rate->max = 48000;
		channels->min = channels->max = 2;
		snd_mask_none(fmt);
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);
	} else if (!strcmp(fe_dai_link->name, "Kbl Audio DMIC cap")) {
		if (params_channels(params) == 2 ||
				DMIC_CH(dmic_constraints) == 2)
			channels->min = channels->max = 2;
		else
			channels->min = channels->max = 4;
	}
	/*
	 * The speaker on the SSP0 supports S16_LE and not S24_LE.
	 * thus changing the mask here
	 */
	if (!strcmp(be_dai_link->name, "SSP0-Codec"))
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);

	return 0;
}

static int kabylake_rt5663_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	/* use ASRC for internal clocks, as PLL rate isn't multiple of BCLK */
	rt5663_sel_asrc_clk_src(codec_dai->component,
			RT5663_DA_STEREO_FILTER | RT5663_AD_STEREO_FILTER,
			RT5663_CLK_SEL_I2S1_ASRC);

	ret = snd_soc_dai_set_sysclk(codec_dai,
			RT5663_SCLK_S_MCLK, 24576000, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);

	return ret;
}

static struct snd_soc_ops kabylake_rt5663_ops = {
	.hw_params = kabylake_rt5663_hw_params,
};

static int kabylake_ssp0_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai;
	int ret = 0, j;

	for_each_rtd_codec_dai(rtd, j, codec_dai) {
		if (!strcmp(codec_dai->component->name, RT5514_DEV_NAME)) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xF, 0, 8, 16);
			if (ret < 0) {
				dev_err(rtd->dev, "set TDM slot err:%d\n", ret);
				return ret;
			}

			ret = snd_soc_dai_set_sysclk(codec_dai,
				RT5514_SCLK_S_MCLK, 24576000, SND_SOC_CLOCK_IN);
			if (ret < 0) {
				dev_err(rtd->dev, "set sysclk err: %d\n", ret);
				return ret;
			}
		}
		if (!strcmp(codec_dai->component->name, MAXIM_DEV0_NAME)) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x30, 3, 8, 16);
			if (ret < 0) {
				dev_err(rtd->dev, "DEV0 TDM slot err:%d\n", ret);
				return ret;
			}
		}

		if (!strcmp(codec_dai->component->name, MAXIM_DEV1_NAME)) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xC0, 3, 8, 16);
			if (ret < 0) {
				dev_err(rtd->dev, "DEV1 TDM slot err:%d\n", ret);
				return ret;
			}
		}
	}
	return ret;
}

static struct snd_soc_ops kabylake_ssp0_ops = {
	.hw_params = kabylake_ssp0_hw_params,
};

static const unsigned int channels_dmic[] = {
	4,
};

static const struct snd_pcm_hw_constraint_list constraints_dmic_channels = {
	.count = ARRAY_SIZE(channels_dmic),
	.list = channels_dmic,
	.mask = 0,
};

static const unsigned int dmic_2ch[] = {
	2,
};

static const struct snd_pcm_hw_constraint_list constraints_dmic_2ch = {
	.count = ARRAY_SIZE(dmic_2ch),
	.list = dmic_2ch,
	.mask = 0,
};

static int kabylake_dmic_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.channels_max = DMIC_CH(dmic_constraints);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			dmic_constraints);

	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_msbits(runtime, 0, 16, 16);

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);
}

static struct snd_soc_ops kabylake_dmic_ops = {
	.startup = kabylake_dmic_startup,
};

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(system,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));

SND_SOC_DAILINK_DEF(system2,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin2")));

SND_SOC_DAILINK_DEF(echoref,
	DAILINK_COMP_ARRAY(COMP_CPU("Echoref Pin")));

SND_SOC_DAILINK_DEF(spi_cpu,
	DAILINK_COMP_ARRAY(COMP_CPU("spi-PRP0001:00")));
SND_SOC_DAILINK_DEF(spi_platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("spi-PRP0001:00")));

SND_SOC_DAILINK_DEF(dmic,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC Pin")));

SND_SOC_DAILINK_DEF(hdmi1,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI1 Pin")));

SND_SOC_DAILINK_DEF(hdmi2,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI2 Pin")));

SND_SOC_DAILINK_DEF(ssp0_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP0 Pin")));
SND_SOC_DAILINK_DEF(ssp0_codec,
	DAILINK_COMP_ARRAY(
	/* Left */ COMP_CODEC(MAXIM_DEV0_NAME, KBL_MAXIM_CODEC_DAI),
	/* Right */COMP_CODEC(MAXIM_DEV1_NAME, KBL_MAXIM_CODEC_DAI),
	/* dmic */ COMP_CODEC(RT5514_DEV_NAME, KBL_REALTEK_DMIC_CODEC_DAI)));

SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP1 Pin")));
SND_SOC_DAILINK_DEF(ssp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC(RT5663_DEV_NAME, KBL_REALTEK_CODEC_DAI)));

SND_SOC_DAILINK_DEF(idisp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp1 Pin")));
SND_SOC_DAILINK_DEF(idisp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi1")));

SND_SOC_DAILINK_DEF(idisp2_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp2 Pin")));
SND_SOC_DAILINK_DEF(idisp2_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi2")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:1f.3")));

/* kabylake digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link kabylake_dais[] = {
	/* Front End DAI links */
	[KBL_DPCM_AUDIO_PB] = {
		.name = "Kbl Audio Port",
		.stream_name = "Audio",
		.dynamic = 1,
		.nonatomic = 1,
		.init = kabylake_rt5663_fe_init,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.ops = &kabylake_rt5663_fe_ops,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[KBL_DPCM_AUDIO_CP] = {
		.name = "Kbl Audio Capture Port",
		.stream_name = "Audio Record",
		.dynamic = 1,
		.nonatomic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
		.ops = &kabylake_rt5663_fe_ops,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[KBL_DPCM_AUDIO_HS_PB] = {
		.name = "Kbl Audio Headset Playback",
		.stream_name = "Headset Audio",
		.dpcm_playback = 1,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(system2, dummy, platform),
	},
	[KBL_DPCM_AUDIO_ECHO_REF_CP] = {
		.name = "Kbl Audio Echo Reference cap",
		.stream_name = "Echoreference Capture",
		.init = NULL,
		.capture_only = 1,
		.nonatomic = 1,
		SND_SOC_DAILINK_REG(echoref, dummy, platform),
	},
	[KBL_DPCM_AUDIO_RT5514_DSP] = {
		.name = "rt5514 dsp",
		.stream_name = "Wake on Voice",
		SND_SOC_DAILINK_REG(spi_cpu, dummy, spi_platform),
	},
	[KBL_DPCM_AUDIO_DMIC_CP] = {
		.name = "Kbl Audio DMIC cap",
		.stream_name = "dmiccap",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &kabylake_dmic_ops,
		SND_SOC_DAILINK_REG(dmic, dummy, platform),
	},
	[KBL_DPCM_AUDIO_HDMI1_PB] = {
		.name = "Kbl HDMI Port1",
		.stream_name = "Hdmi1",
		.dpcm_playback = 1,
		.init = NULL,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi1, dummy, platform),
	},
	[KBL_DPCM_AUDIO_HDMI2_PB] = {
		.name = "Kbl HDMI Port2",
		.stream_name = "Hdmi2",
		.dpcm_playback = 1,
		.init = NULL,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi2, dummy, platform),
	},
	/* Back End DAI links */
	/* single Back end dai for both max speakers and dmic */
	{
		/* SSP0 - Codec */
		.name = "SSP0-Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_DSP_B |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = kabylake_ssp_fixup,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &kabylake_ssp0_ops,
		SND_SOC_DAILINK_REG(ssp0_pin, ssp0_codec, platform),
	},
	{
		.name = "SSP1-Codec",
		.id = 1,
		.no_pcm = 1,
		.init = kabylake_rt5663_codec_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = kabylake_ssp_fixup,
		.ops = &kabylake_rt5663_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp1_pin, ssp1_codec, platform),
	},
	{
		.name = "iDisp1",
		.id = 3,
		.dpcm_playback = 1,
		.init = kabylake_hdmi1_init,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_pin, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 4,
		.init = kabylake_hdmi2_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_pin, idisp2_codec, platform),
	},
};

static int kabylake_set_bias_level(struct snd_soc_card *card,
	struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level)
{
	struct snd_soc_component *component = dapm->component;
	struct kbl_codec_private *priv = snd_soc_card_get_drvdata(card);
	int ret = 0;

	if (!component || strcmp(component->name, RT5514_DEV_NAME))
		return 0;

	if (IS_ERR(priv->mclk))
		return 0;

	/*
	 * It's required to control mclk directly in the set_bias_level
	 * function for rt5514 codec or the recording function could
	 * break.
	 */
	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level == SND_SOC_BIAS_ON) {
			dev_dbg(card->dev, "Disable mclk");
			clk_disable_unprepare(priv->mclk);
		} else {
			dev_dbg(card->dev, "Enable mclk");
			ret = clk_set_rate(priv->mclk, 24000000);
			if (ret) {
				dev_err(card->dev, "Can't set rate for mclk, err: %d\n",
					ret);
				return ret;
			}

			ret = clk_prepare_enable(priv->mclk);
			if (ret) {
				dev_err(card->dev, "Can't enable mclk, err: %d\n",
					ret);

				/* mclk is already enabled in FW */
				ret = 0;
			}
		}
		break;
	default:
		break;
	}

	return ret;
}

static int kabylake_card_late_probe(struct snd_soc_card *card)
{
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(card);
	struct kbl_hdmi_pcm *pcm;
	struct snd_soc_component *component = NULL;
	int err, i = 0;
	char jack_name[NAME_SIZE];

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			"HDMI/DP,pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
				SND_JACK_AVOUT, &ctx->kabylake_hdmi[i],
				NULL, 0);

		if (err)
			return err;
		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
						&ctx->kabylake_hdmi[i]);
		if (err < 0)
			return err;
		i++;
	}

	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}

/*
 * kabylake audio machine driver for  MAX98927 + RT5514 + RT5663
 */
static struct snd_soc_card kabylake_audio_card = {
	.name = "kbl-r5514-5663-max",
	.owner = THIS_MODULE,
	.dai_link = kabylake_dais,
	.num_links = ARRAY_SIZE(kabylake_dais),
	.set_bias_level = kabylake_set_bias_level,
	.controls = kabylake_controls,
	.num_controls = ARRAY_SIZE(kabylake_controls),
	.dapm_widgets = kabylake_widgets,
	.num_dapm_widgets = ARRAY_SIZE(kabylake_widgets),
	.dapm_routes = kabylake_map,
	.num_dapm_routes = ARRAY_SIZE(kabylake_map),
	.codec_conf = max98927_codec_conf,
	.num_configs = ARRAY_SIZE(max98927_codec_conf),
	.fully_routed = true,
	.late_probe = kabylake_card_late_probe,
};

static int kabylake_audio_probe(struct platform_device *pdev)
{
	struct kbl_codec_private *ctx;
	struct snd_soc_acpi_mach *mach;
	int ret = 0;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	kabylake_audio_card.dev = &pdev->dev;
	snd_soc_card_set_drvdata(&kabylake_audio_card, ctx);

	mach = (&pdev->dev)->platform_data;
	if (mach)
		dmic_constraints = mach->mach_params.dmic_num == 2 ?
			&constraints_dmic_2ch : &constraints_dmic_channels;

	ctx->mclk = devm_clk_get(&pdev->dev, "ssp1_mclk");
	if (IS_ERR(ctx->mclk)) {
		ret = PTR_ERR(ctx->mclk);
		if (ret == -ENOENT) {
			dev_info(&pdev->dev,
				"Failed to get ssp1_mclk, defer probe\n");
			return -EPROBE_DEFER;
		}

		dev_err(&pdev->dev, "Failed to get ssp1_mclk with err:%d\n",
								ret);
		return ret;
	}

	ctx->sclk = devm_clk_get(&pdev->dev, "ssp1_sclk");
	if (IS_ERR(ctx->sclk)) {
		ret = PTR_ERR(ctx->sclk);
		if (ret == -ENOENT) {
			dev_info(&pdev->dev,
				"Failed to get ssp1_sclk, defer probe\n");
			return -EPROBE_DEFER;
		}

		dev_err(&pdev->dev, "Failed to get ssp1_sclk with err:%d\n",
								ret);
		return ret;
	}

	return devm_snd_soc_register_card(&pdev->dev, &kabylake_audio_card);
}

static const struct platform_device_id kbl_board_ids[] = {
	{ .name = "kbl_r5514_5663_max" },
	{ }
};

static struct platform_driver kabylake_audio = {
	.probe = kabylake_audio_probe,
	.driver = {
		.name = "kbl_r5514_5663_max",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = kbl_board_ids,
};

module_platform_driver(kabylake_audio)

/* Module information */
MODULE_DESCRIPTION("Audio Machine driver-RT5663 RT5514 & MAX98927");
MODULE_AUTHOR("Harsha Priya <harshapriya.n@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:kbl_r5514_5663_max");
