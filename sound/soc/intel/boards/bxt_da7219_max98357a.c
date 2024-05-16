// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Broxton-P I2S Machine Driver
 *
 * Copyright (C) 2016, Intel Corporation. All rights reserved.
 *
 * Modified from:
 *   Intel Skylake I2S Machine driver
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
#include "../../codecs/hdac_hdmi.h"
#include "../../codecs/da7219.h"
#include "../common/soc-intel-quirks.h"
#include "hda_dsp_common.h"

#define BXT_DIALOG_CODEC_DAI	"da7219-hifi"
#define BXT_MAXIM_CODEC_DAI	"HiFi"
#define MAX98390_DEV0_NAME	"i2c-MX98390:00"
#define MAX98390_DEV1_NAME	"i2c-MX98390:01"
#define DUAL_CHANNEL		2
#define QUAD_CHANNEL		4

#define SPKAMP_MAX98357A	1
#define SPKAMP_MAX98390	2

static struct snd_soc_jack broxton_headset;
static struct snd_soc_jack broxton_hdmi[3];

struct bxt_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct bxt_card_private {
	struct list_head hdmi_pcm_list;
	bool common_hdmi_codec_drv;
	int spkamp;
};

enum {
	BXT_DPCM_AUDIO_PB = 0,
	BXT_DPCM_AUDIO_CP,
	BXT_DPCM_AUDIO_HS_PB,
	BXT_DPCM_AUDIO_REF_CP,
	BXT_DPCM_AUDIO_DMIC_CP,
	BXT_DPCM_AUDIO_HDMI1_PB,
	BXT_DPCM_AUDIO_HDMI2_PB,
	BXT_DPCM_AUDIO_HDMI3_PB,
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int  event)
{
	int ret = 0;
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;

	codec_dai = snd_soc_card_get_codec_dai(card, BXT_DIALOG_CODEC_DAI);
	if (!codec_dai) {
		dev_err(card->dev, "Codec dai not found; Unable to set/unset codec pll\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		ret = snd_soc_dai_set_pll(codec_dai, 0,
			DA7219_SYSCLK_MCLK, 0, 0);
		if (ret)
			dev_err(card->dev, "failed to stop PLL: %d\n", ret);
	} else if(SND_SOC_DAPM_EVENT_ON(event)) {
		ret = snd_soc_dai_set_pll(codec_dai, 0,
			DA7219_SYSCLK_PLL_SRM, 0, DA7219_PLL_FREQ_OUT_98304);
		if (ret)
			dev_err(card->dev, "failed to start PLL: %d\n", ret);
	}

	return ret;
}

static const struct snd_kcontrol_new broxton_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Line Out"),
};

static const struct snd_kcontrol_new max98357a_controls[] = {
	SOC_DAPM_PIN_SWITCH("Spk"),
};

static const struct snd_kcontrol_new max98390_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget broxton_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
	SND_SOC_DAPM_SPK("HDMI1", NULL),
	SND_SOC_DAPM_SPK("HDMI2", NULL),
	SND_SOC_DAPM_SPK("HDMI3", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			platform_clock_control,	SND_SOC_DAPM_POST_PMD|SND_SOC_DAPM_PRE_PMU),
};

static const struct snd_soc_dapm_widget max98357a_widgets[] = {
	SND_SOC_DAPM_SPK("Spk", NULL),
};

static const struct snd_soc_dapm_widget max98390_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{"Headphone Jack", NULL, "HPL"},
	{"Headphone Jack", NULL, "HPR"},

	/* other jacks */
	{"MIC", NULL, "Headset Mic"},

	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},

	/* CODEC BE connections */
	{"HDMI1", NULL, "hif5-0 Output"},
	{"HDMI2", NULL, "hif6-0 Output"},
	{"HDMI2", NULL, "hif7-0 Output"},

	{"hifi3", NULL, "iDisp3 Tx"},
	{"iDisp3 Tx", NULL, "iDisp3_out"},
	{"hifi2", NULL, "iDisp2 Tx"},
	{"iDisp2 Tx", NULL, "iDisp2_out"},
	{"hifi1", NULL, "iDisp1 Tx"},
	{"iDisp1 Tx", NULL, "iDisp1_out"},

	/* DMIC */
	{"dmic01_hifi", NULL, "DMIC01 Rx"},
	{"DMIC01 Rx", NULL, "DMIC AIF"},

	{ "Headphone Jack", NULL, "Platform Clock" },
	{ "Headset Mic", NULL, "Platform Clock" },
	{ "Line Out", NULL, "Platform Clock" },
};

static const struct snd_soc_dapm_route max98357a_routes[] = {
	/* speaker */
	{"Spk", NULL, "Speaker"},
};

static const struct snd_soc_dapm_route max98390_routes[] = {
	/* Speaker */
	{"Left Spk", NULL, "Left BE_OUT"},
	{"Right Spk", NULL, "Right BE_OUT"},
};

static const struct snd_soc_dapm_route broxton_map[] = {
	{"HiFi Playback", NULL, "ssp5 Tx"},
	{"ssp5 Tx", NULL, "codec0_out"},

	{"Playback", NULL, "ssp1 Tx"},
	{"ssp1 Tx", NULL, "codec1_out"},

	{"codec0_in", NULL, "ssp1 Rx"},
	{"ssp1 Rx", NULL, "Capture"},
};

static const struct snd_soc_dapm_route gemini_map[] = {
	{"HiFi Playback", NULL, "ssp1 Tx"},
	{"ssp1 Tx", NULL, "codec0_out"},

	{"Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec1_out"},

	{"codec0_in", NULL, "ssp2 Rx"},
	{"ssp2 Rx", NULL, "Capture"},
};

static struct snd_soc_jack_pin jack_pins[] = {
	{
		.pin    = "Headphone Jack",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
	{
		.pin    = "Line Out",
		.mask   = SND_JACK_LINEOUT,
	},
};

static int broxton_ssp_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *chan = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will convert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	chan->min = chan->max = DUAL_CHANNEL;

	/* set SSP to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int broxton_da7219_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	int clk_freq;

	/* Configure sysclk for codec */
	if (soc_intel_is_cml())
		clk_freq = 24000000;
	else
		clk_freq = 19200000;

	ret = snd_soc_dai_set_sysclk(codec_dai, DA7219_CLKSRC_MCLK, clk_freq,
				     SND_SOC_CLOCK_IN);

	if (ret) {
		dev_err(rtd->dev, "can't set codec sysclk configuration\n");
		return ret;
	}

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					 SND_JACK_BTN_2 | SND_JACK_BTN_3 | SND_JACK_LINEOUT,
					 &broxton_headset,
					 jack_pins,
					 ARRAY_SIZE(jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	snd_jack_set_key(broxton_headset.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(broxton_headset.jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(broxton_headset.jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(broxton_headset.jack, SND_JACK_BTN_3,
			 KEY_VOICECOMMAND);

	snd_soc_component_set_jack(component, &broxton_headset, NULL);

	snd_soc_dapm_ignore_suspend(&rtd->card->dapm, "SoC DMIC");

	return ret;
}

static int broxton_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct bxt_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = snd_soc_rtd_to_codec(rtd, 0);
	struct bxt_hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->device = BXT_DPCM_AUDIO_HDMI1_PB + dai->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

static int broxton_da7219_fe_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dapm_context *dapm;
	struct snd_soc_component *component = snd_soc_rtd_to_cpu(rtd, 0)->component;

	dapm = snd_soc_component_get_dapm(component);
	snd_soc_dapm_ignore_suspend(dapm, "Reference Capture");

	return 0;
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
	DUAL_CHANNEL,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static const unsigned int channels_quad[] = {
	QUAD_CHANNEL,
};

static const struct snd_pcm_hw_constraint_list constraints_channels_quad = {
	.count = ARRAY_SIZE(channels_quad),
	.list = channels_quad,
	.mask = 0,
};

static int bxt_fe_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	/*
	 * On this platform for PCM device we support,
	 * 48Khz
	 * stereo
	 * 16 bit audio
	 */

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					   &constraints_channels);

	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_msbits(runtime, 0, 16, 16);

	snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);

	return 0;
}

static const struct snd_soc_ops broxton_da7219_fe_ops = {
	.startup = bxt_fe_startup,
};

static int broxton_dmic_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *chan = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	if (params_channels(params) == 2)
		chan->min = chan->max = 2;
	else
		chan->min = chan->max = 4;

	return 0;
}

static int broxton_dmic_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.channels_min = runtime->hw.channels_max = QUAD_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			&constraints_channels_quad);

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);
}

static const struct snd_soc_ops broxton_dmic_ops = {
	.startup = broxton_dmic_startup,
};

static const unsigned int rates_16000[] = {
	16000,
};

static const struct snd_pcm_hw_constraint_list constraints_16000 = {
	.count = ARRAY_SIZE(rates_16000),
	.list  = rates_16000,
};

static const unsigned int ch_mono[] = {
	1,
};

static const struct snd_pcm_hw_constraint_list constraints_refcap = {
	.count = ARRAY_SIZE(ch_mono),
	.list  = ch_mono,
};

static int broxton_refcap_startup(struct snd_pcm_substream *substream)
{
	substream->runtime->hw.channels_max = 1;
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_refcap);

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE,
			&constraints_16000);
};

static const struct snd_soc_ops broxton_refcap_ops = {
	.startup = broxton_refcap_startup,
};

/* broxton digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(system,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));

SND_SOC_DAILINK_DEF(system2,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin2")));

SND_SOC_DAILINK_DEF(reference,
	DAILINK_COMP_ARRAY(COMP_CPU("Reference Pin")));

SND_SOC_DAILINK_DEF(dmic,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC Pin")));

SND_SOC_DAILINK_DEF(hdmi1,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI1 Pin")));

SND_SOC_DAILINK_DEF(hdmi2,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI2 Pin")));

SND_SOC_DAILINK_DEF(hdmi3,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI3 Pin")));

 /* Back End DAI */
SND_SOC_DAILINK_DEF(ssp5_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP5 Pin")));
SND_SOC_DAILINK_DEF(ssp5_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("MX98357A:00",
				      BXT_MAXIM_CODEC_DAI)));
SND_SOC_DAILINK_DEF(max98390_codec,
	DAILINK_COMP_ARRAY(
	/* Left */	COMP_CODEC(MAX98390_DEV0_NAME, "max98390-aif1"),
	/* Right */	COMP_CODEC(MAX98390_DEV1_NAME, "max98390-aif1")));

SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP1 Pin")));
SND_SOC_DAILINK_DEF(ssp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-DLGS7219:00",
				      BXT_DIALOG_CODEC_DAI)));

SND_SOC_DAILINK_DEF(dmic_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC01 Pin")));

SND_SOC_DAILINK_DEF(dmic16k_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC16k Pin")));

SND_SOC_DAILINK_DEF(dmic_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));

SND_SOC_DAILINK_DEF(idisp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp1 Pin")));
SND_SOC_DAILINK_DEF(idisp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi1")));

SND_SOC_DAILINK_DEF(idisp2_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp2 Pin")));
SND_SOC_DAILINK_DEF(idisp2_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2",
				      "intel-hdmi-hifi2")));

SND_SOC_DAILINK_DEF(idisp3_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp3 Pin")));
SND_SOC_DAILINK_DEF(idisp3_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2",
				      "intel-hdmi-hifi3")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:0e.0")));

static struct snd_soc_dai_link broxton_dais[] = {
	/* Front End DAI links */
	[BXT_DPCM_AUDIO_PB] =
	{
		.name = "Bxt Audio Port",
		.stream_name = "Audio",
		.dynamic = 1,
		.nonatomic = 1,
		.init = broxton_da7219_fe_init,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.ops = &broxton_da7219_fe_ops,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[BXT_DPCM_AUDIO_CP] =
	{
		.name = "Bxt Audio Capture Port",
		.stream_name = "Audio Record",
		.dynamic = 1,
		.nonatomic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
		.ops = &broxton_da7219_fe_ops,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[BXT_DPCM_AUDIO_HS_PB] = {
		.name = "Bxt Audio Headset Playback",
		.stream_name = "Headset Playback",
		.dynamic = 1,
		.nonatomic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.ops = &broxton_da7219_fe_ops,
		SND_SOC_DAILINK_REG(system2, dummy, platform),
	},
	[BXT_DPCM_AUDIO_REF_CP] =
	{
		.name = "Bxt Audio Reference cap",
		.stream_name = "Refcap",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &broxton_refcap_ops,
		SND_SOC_DAILINK_REG(reference, dummy, platform),
	},
	[BXT_DPCM_AUDIO_DMIC_CP] =
	{
		.name = "Bxt Audio DMIC cap",
		.stream_name = "dmiccap",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &broxton_dmic_ops,
		SND_SOC_DAILINK_REG(dmic, dummy, platform),
	},
	[BXT_DPCM_AUDIO_HDMI1_PB] =
	{
		.name = "Bxt HDMI Port1",
		.stream_name = "Hdmi1",
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi1, dummy, platform),
	},
	[BXT_DPCM_AUDIO_HDMI2_PB] =
	{
		.name = "Bxt HDMI Port2",
		.stream_name = "Hdmi2",
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi2, dummy, platform),
	},
	[BXT_DPCM_AUDIO_HDMI3_PB] =
	{
		.name = "Bxt HDMI Port3",
		.stream_name = "Hdmi3",
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi3, dummy, platform),
	},
	/* Back End DAI links */
	{
		/* SSP5 - Codec */
		.name = "SSP5-Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = broxton_ssp_fixup,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(ssp5_pin, ssp5_codec, platform),
	},
	{
		/* SSP1 - Codec */
		.name = "SSP1-Codec",
		.id = 1,
		.no_pcm = 1,
		.init = broxton_da7219_codec_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = broxton_ssp_fixup,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp1_pin, ssp1_codec, platform),
	},
	{
		.name = "dmic01",
		.id = 2,
		.ignore_suspend = 1,
		.be_hw_params_fixup = broxton_dmic_fixup,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
	},
	{
		.name = "iDisp1",
		.id = 3,
		.init = broxton_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_pin, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 4,
		.init = broxton_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_pin, idisp2_codec, platform),
	},
	{
		.name = "iDisp3",
		.id = 5,
		.init = broxton_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_pin, idisp3_codec, platform),
	},
	{
		.name = "dmic16k",
		.id = 6,
		.be_hw_params_fixup = broxton_dmic_fixup,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic16k_pin, dmic_codec, platform),
	},
};

static struct snd_soc_codec_conf max98390_codec_confs[] = {
	{
		.dlc = COMP_CODEC_CONF(MAX98390_DEV0_NAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(MAX98390_DEV1_NAME),
		.name_prefix = "Right",
	},
};

#define NAME_SIZE	32
static int bxt_card_late_probe(struct snd_soc_card *card)
{
	struct bxt_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct bxt_hdmi_pcm *pcm;
	struct snd_soc_component *component = NULL;
	const struct snd_kcontrol_new *controls;
	const struct snd_soc_dapm_widget *widgets;
	const struct snd_soc_dapm_route *routes;
	int num_controls, num_widgets, num_routes, err, i = 0;
	char jack_name[NAME_SIZE];

	switch (ctx->spkamp) {
	case SPKAMP_MAX98357A:
		controls = max98357a_controls;
		num_controls = ARRAY_SIZE(max98357a_controls);
		widgets = max98357a_widgets;
		num_widgets = ARRAY_SIZE(max98357a_widgets);
		routes = max98357a_routes;
		num_routes = ARRAY_SIZE(max98357a_routes);
		break;
	case SPKAMP_MAX98390:
		controls = max98390_controls;
		num_controls = ARRAY_SIZE(max98390_controls);
		widgets = max98390_widgets;
		num_widgets = ARRAY_SIZE(max98390_widgets);
		routes = max98390_routes;
		num_routes = ARRAY_SIZE(max98390_routes);
		break;
	default:
		dev_err(card->dev, "Invalid speaker amplifier %d\n", ctx->spkamp);
		return -EINVAL;
	}

	err = snd_soc_dapm_new_controls(&card->dapm, widgets, num_widgets);
	if (err) {
		dev_err(card->dev, "Fail to new widgets\n");
		return err;
	}

	err = snd_soc_add_card_controls(card, controls, num_controls);
	if (err) {
		dev_err(card->dev, "Fail to add controls\n");
		return err;
	}

	err = snd_soc_dapm_add_routes(&card->dapm, routes, num_routes);
	if (err) {
		dev_err(card->dev, "Fail to add routes\n");
		return err;
	}

	if (soc_intel_is_glk())
		snd_soc_dapm_add_routes(&card->dapm, gemini_map,
					ARRAY_SIZE(gemini_map));
	else
		snd_soc_dapm_add_routes(&card->dapm, broxton_map,
					ARRAY_SIZE(broxton_map));

	if (list_empty(&ctx->hdmi_pcm_list))
		return -EINVAL;

	if (ctx->common_hdmi_codec_drv) {
		pcm = list_first_entry(&ctx->hdmi_pcm_list, struct bxt_hdmi_pcm,
				       head);
		component = pcm->codec_dai->component;
		return hda_dsp_hdmi_build_controls(card, component);
	}

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			"HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					SND_JACK_AVOUT, &broxton_hdmi[i]);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
						&broxton_hdmi[i]);
		if (err < 0)
			return err;

		i++;
	}

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}

/* broxton audio machine driver for SPT + da7219 */
static struct snd_soc_card broxton_audio_card = {
	.name = "bxtda7219max",
	.owner = THIS_MODULE,
	.dai_link = broxton_dais,
	.num_links = ARRAY_SIZE(broxton_dais),
	.controls = broxton_controls,
	.num_controls = ARRAY_SIZE(broxton_controls),
	.dapm_widgets = broxton_widgets,
	.num_dapm_widgets = ARRAY_SIZE(broxton_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.fully_routed = true,
	.late_probe = bxt_card_late_probe,
};

static int broxton_audio_probe(struct platform_device *pdev)
{
	struct bxt_card_private *ctx;
	struct snd_soc_acpi_mach *mach;
	const char *platform_name;
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	if (acpi_dev_present("MX98390", NULL, -1))
		ctx->spkamp = SPKAMP_MAX98390;
	else
		ctx->spkamp = SPKAMP_MAX98357A;

	broxton_audio_card.dev = &pdev->dev;
	snd_soc_card_set_drvdata(&broxton_audio_card, ctx);
	if (soc_intel_is_glk()) {
		unsigned int i;

		broxton_audio_card.name = "glkda7219max";
		/* Fixup the SSP entries for geminilake */
		for (i = 0; i < ARRAY_SIZE(broxton_dais); i++) {
			if (!broxton_dais[i].codecs->dai_name)
				continue;

			/* MAXIM_CODEC is connected to SSP1. */
			if (!strcmp(broxton_dais[i].codecs->dai_name,
				    BXT_MAXIM_CODEC_DAI)) {
				broxton_dais[i].name = "SSP1-Codec";
				broxton_dais[i].cpus->dai_name = "SSP1 Pin";
			}
			/* DIALOG_CODE is connected to SSP2 */
			else if (!strcmp(broxton_dais[i].codecs->dai_name,
					 BXT_DIALOG_CODEC_DAI)) {
				broxton_dais[i].name = "SSP2-Codec";
				broxton_dais[i].cpus->dai_name = "SSP2 Pin";
			}
		}
	} else if (soc_intel_is_cml()) {
		unsigned int i;

		if (ctx->spkamp == SPKAMP_MAX98390) {
			broxton_audio_card.name = "cml_max98390_da7219";

			broxton_audio_card.codec_conf = max98390_codec_confs;
			broxton_audio_card.num_configs = ARRAY_SIZE(max98390_codec_confs);
		} else
			broxton_audio_card.name = "cmlda7219max";

		for (i = 0; i < ARRAY_SIZE(broxton_dais); i++) {
			if (!broxton_dais[i].codecs->dai_name)
				continue;

			/* MAXIM_CODEC is connected to SSP1. */
			if (!strcmp(broxton_dais[i].codecs->dai_name,
					BXT_MAXIM_CODEC_DAI)) {
				broxton_dais[i].name = "SSP1-Codec";
				broxton_dais[i].cpus->dai_name = "SSP1 Pin";

				if (ctx->spkamp == SPKAMP_MAX98390) {
					broxton_dais[i].codecs = max98390_codec;
					broxton_dais[i].num_codecs = ARRAY_SIZE(max98390_codec);
					broxton_dais[i].dpcm_capture = 1;
				}
			}
			/* DIALOG_CODEC is connected to SSP0 */
			else if (!strcmp(broxton_dais[i].codecs->dai_name,
					BXT_DIALOG_CODEC_DAI)) {
				broxton_dais[i].name = "SSP0-Codec";
				broxton_dais[i].cpus->dai_name = "SSP0 Pin";
			}
		}
	}

	/* override platform name, if required */
	mach = pdev->dev.platform_data;
	platform_name = mach->mach_params.platform;

	ret = snd_soc_fixup_dai_links_platform_name(&broxton_audio_card,
						    platform_name);
	if (ret)
		return ret;

	ctx->common_hdmi_codec_drv = mach->mach_params.common_hdmi_codec_drv;

	return devm_snd_soc_register_card(&pdev->dev, &broxton_audio_card);
}

static const struct platform_device_id bxt_board_ids[] = {
	{ .name = "bxt_da7219_mx98357a" },
	{ .name = "glk_da7219_mx98357a" },
	{ .name = "cml_da7219_mx98357a" },
	{ }
};
MODULE_DEVICE_TABLE(platform, bxt_board_ids);

static struct platform_driver broxton_audio = {
	.probe = broxton_audio_probe,
	.driver = {
		.name = "bxt_da7219_max98357a",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = bxt_board_ids,
};
module_platform_driver(broxton_audio)

/* Module information */
MODULE_DESCRIPTION("Audio Machine driver-DA7219 & MAX98357A in I2S mode");
MODULE_AUTHOR("Sathyanarayana Nujella <sathyanarayana.nujella@intel.com>");
MODULE_AUTHOR("Rohit Ainapure <rohit.m.ainapure@intel.com>");
MODULE_AUTHOR("Harsha Priya <harshapriya.n@intel.com>");
MODULE_AUTHOR("Conrad Cooke <conrad.cooke@intel.com>");
MODULE_AUTHOR("Naveen Manohar <naveen.m@intel.com>");
MODULE_AUTHOR("Mac Chiang <mac.chiang@intel.com>");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(SND_SOC_INTEL_HDA_DSP_COMMON);
