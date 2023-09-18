// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2018 Intel Corporation.

/*
 * Intel Kabylake I2S Machine Driver with MAX98927, MAX98373 & DA7219 Codecs
 *
 * Modified from:
 *   Intel Kabylake I2S Machine driver supporting MAX98927 and
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
#include "../../codecs/da7219.h"
#include "../../codecs/hdac_hdmi.h"

#define KBL_DIALOG_CODEC_DAI	"da7219-hifi"
#define MAX98927_CODEC_DAI	"max98927-aif1"
#define MAX98927_DEV0_NAME	"i2c-MX98927:00"
#define MAX98927_DEV1_NAME	"i2c-MX98927:01"

#define MAX98373_CODEC_DAI	"max98373-aif1"
#define MAX98373_DEV0_NAME	"i2c-MX98373:00"
#define MAX98373_DEV1_NAME	"i2c-MX98373:01"


#define DUAL_CHANNEL	2
#define QUAD_CHANNEL	4
#define NAME_SIZE	32

static struct snd_soc_card *kabylake_audio_card;
static struct snd_soc_jack kabylake_hdmi[3];

struct kbl_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct kbl_codec_private {
	struct snd_soc_jack kabylake_headset;
	struct list_head hdmi_pcm_list;
};

enum {
	KBL_DPCM_AUDIO_PB = 0,
	KBL_DPCM_AUDIO_ECHO_REF_CP,
	KBL_DPCM_AUDIO_REF_CP,
	KBL_DPCM_AUDIO_DMIC_CP,
	KBL_DPCM_AUDIO_HDMI1_PB,
	KBL_DPCM_AUDIO_HDMI2_PB,
	KBL_DPCM_AUDIO_HDMI3_PB,
	KBL_DPCM_AUDIO_HS_PB,
	KBL_DPCM_AUDIO_CP,
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	int ret = 0;

	codec_dai = snd_soc_card_get_codec_dai(card, KBL_DIALOG_CODEC_DAI);
	if (!codec_dai) {
		dev_err(card->dev, "Codec dai not found; Unable to set/unset codec pll\n");
		return -EIO;
	}

	/* Configure sysclk for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, DA7219_CLKSRC_MCLK, 24576000,
				     SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(card->dev, "can't set codec sysclk configuration\n");
		return ret;
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		ret = snd_soc_dai_set_pll(codec_dai, 0,
				     DA7219_SYSCLK_MCLK, 0, 0);
		if (ret)
			dev_err(card->dev, "failed to stop PLL: %d\n", ret);
	} else if (SND_SOC_DAPM_EVENT_ON(event)) {
		ret = snd_soc_dai_set_pll(codec_dai, 0,	DA7219_SYSCLK_PLL_SRM,
				     0, DA7219_PLL_FREQ_OUT_98304);
		if (ret)
			dev_err(card->dev, "failed to start PLL: %d\n", ret);
	}

	return ret;
}

static const struct snd_kcontrol_new kabylake_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
	SOC_DAPM_PIN_SWITCH("Line Out"),
};

static const struct snd_soc_dapm_widget kabylake_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
	SND_SOC_DAPM_SPK("HDMI1", NULL),
	SND_SOC_DAPM_SPK("HDMI2", NULL),
	SND_SOC_DAPM_SPK("HDMI3", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			platform_clock_control, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
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
		.mask   = SND_JACK_MICROPHONE,
	},
};

static const struct snd_soc_dapm_route kabylake_map[] = {
	/* speaker */
	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },

	/* other jacks */
	{ "DMic", NULL, "SoC DMIC" },

	{"HDMI1", NULL, "hif5-0 Output"},
	{"HDMI2", NULL, "hif6-0 Output"},
	{"HDMI3", NULL, "hif7-0 Output"},

	/* CODEC BE connections */
	{ "Left HiFi Playback", NULL, "ssp0 Tx" },
	{ "Right HiFi Playback", NULL, "ssp0 Tx" },
	{ "ssp0 Tx", NULL, "spk_out" },

	/* IV feedback path */
	{ "codec0_fb_in", NULL, "ssp0 Rx"},
	{ "ssp0 Rx", NULL, "Left HiFi Capture" },
	{ "ssp0 Rx", NULL, "Right HiFi Capture" },

	/* AEC capture path */
	{ "echo_ref_out", NULL, "ssp0 Rx" },

	/* DMIC */
	{ "dmic01_hifi", NULL, "DMIC01 Rx" },
	{ "DMIC01 Rx", NULL, "DMIC AIF" },

	{ "hifi1", NULL, "iDisp1 Tx" },
	{ "iDisp1 Tx", NULL, "iDisp1_out" },
	{ "hifi2", NULL, "iDisp2 Tx" },
	{ "iDisp2 Tx", NULL, "iDisp2_out" },
	{ "hifi3", NULL, "iDisp3 Tx"},
	{ "iDisp3 Tx", NULL, "iDisp3_out"},
};

static const struct snd_soc_dapm_route kabylake_ssp1_map[] = {
	{ "Headphone Jack", NULL, "HPL" },
	{ "Headphone Jack", NULL, "HPR" },

	/* other jacks */
	{ "MIC", NULL, "Headset Mic" },

	/* CODEC BE connections */
	{ "Playback", NULL, "ssp1 Tx" },
	{ "ssp1 Tx", NULL, "codec1_out" },

	{ "hs_in", NULL, "ssp1 Rx" },
	{ "ssp1 Rx", NULL, "Capture" },

	{ "Headphone Jack", NULL, "Platform Clock" },
	{ "Headset Mic", NULL, "Platform Clock" },
	{ "Line Out", NULL, "Platform Clock" },
};

static int kabylake_ssp0_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	int ret, j;

	for_each_rtd_codec_dais(runtime, j, codec_dai) {

		if (!strcmp(codec_dai->component->name, MAX98927_DEV0_NAME)) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x30, 3, 8, 16);
			if (ret < 0) {
				dev_err(runtime->dev, "DEV0 TDM slot err:%d\n", ret);
				return ret;
			}
		}
		if (!strcmp(codec_dai->component->name, MAX98927_DEV1_NAME)) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xC0, 3, 8, 16);
			if (ret < 0) {
				dev_err(runtime->dev, "DEV1 TDM slot err:%d\n", ret);
				return ret;
			}
		}
		if (!strcmp(codec_dai->component->name, MAX98373_DEV0_NAME)) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai,
							0x30, 3, 8, 16);
			if (ret < 0) {
				dev_err(runtime->dev,
						"DEV0 TDM slot err:%d\n", ret);
				return ret;
			}
		}
		if (!strcmp(codec_dai->component->name, MAX98373_DEV1_NAME)) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai,
							0xC0, 3, 8, 16);
			if (ret < 0) {
				dev_err(runtime->dev,
						"DEV1 TDM slot err:%d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static int kabylake_ssp0_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	int j, ret;

	for_each_rtd_codec_dais(rtd, j, codec_dai) {
		const char *name = codec_dai->component->name;
		struct snd_soc_component *component = codec_dai->component;
		struct snd_soc_dapm_context *dapm =
				snd_soc_component_get_dapm(component);
		char pin_name[20];

		if (strcmp(name, MAX98927_DEV0_NAME) &&
			strcmp(name, MAX98927_DEV1_NAME) &&
			strcmp(name, MAX98373_DEV0_NAME) &&
			strcmp(name, MAX98373_DEV1_NAME))
			continue;

		snprintf(pin_name, ARRAY_SIZE(pin_name), "%s Spk",
			codec_dai->component->name_prefix);

		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			ret = snd_soc_dapm_enable_pin(dapm, pin_name);
			if (ret) {
				dev_err(rtd->dev, "failed to enable %s: %d\n",
				pin_name, ret);
				return ret;
			}
			snd_soc_dapm_sync(dapm);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			ret = snd_soc_dapm_disable_pin(dapm, pin_name);
			if (ret) {
				dev_err(rtd->dev, "failed to disable %s: %d\n",
				pin_name, ret);
				return ret;
			}
			snd_soc_dapm_sync(dapm);
			break;
		}
	}

	return 0;
}

static struct snd_soc_ops kabylake_ssp0_ops = {
	.hw_params = kabylake_ssp0_hw_params,
	.trigger = kabylake_ssp0_trigger,
};

static int kabylake_ssp_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *chan = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_soc_dpcm *dpcm, *rtd_dpcm = NULL;

	/*
	 * The following loop will be called only for playback stream
	 * In this platform, there is only one playback device on every SSP
	 */
	for_each_dpcm_fe(rtd, SNDRV_PCM_STREAM_PLAYBACK, dpcm) {
		rtd_dpcm = dpcm;
		break;
	}

	/*
	 * This following loop will be called only for capture stream
	 * In this platform, there is only one capture device on every SSP
	 */
	for_each_dpcm_fe(rtd, SNDRV_PCM_STREAM_CAPTURE, dpcm) {
		rtd_dpcm = dpcm;
		break;
	}

	if (!rtd_dpcm)
		return -EINVAL;

	/*
	 * The above 2 loops are mutually exclusive based on the stream direction,
	 * thus rtd_dpcm variable will never be overwritten
	 */

	/*
	 * The ADSP will convert the FE rate to 48k, stereo, 24 bit
	 */
	if (!strcmp(rtd_dpcm->fe->dai_link->name, "Kbl Audio Port") ||
	    !strcmp(rtd_dpcm->fe->dai_link->name, "Kbl Audio Headset Playback") ||
	    !strcmp(rtd_dpcm->fe->dai_link->name, "Kbl Audio Capture Port")) {
		rate->min = rate->max = 48000;
		chan->min = chan->max = 2;
		snd_mask_none(fmt);
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);
	}

	/*
	 * The speaker on the SSP0 supports S16_LE and not S24_LE.
	 * thus changing the mask here
	 */
	if (!strcmp(rtd_dpcm->be->dai_link->name, "SSP0-Codec"))
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);

	return 0;
}

static int kabylake_da7219_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;
	struct snd_soc_jack *jack;
	struct snd_soc_card *card = rtd->card;
	int ret;


	ret = snd_soc_dapm_add_routes(&card->dapm,
			kabylake_ssp1_map,
			ARRAY_SIZE(kabylake_ssp1_map));

	if (ret)
		return ret;

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new_pins(kabylake_audio_card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					 SND_JACK_BTN_2 | SND_JACK_BTN_3 | SND_JACK_LINEOUT,
					 &ctx->kabylake_headset,
					 jack_pins,
					 ARRAY_SIZE(jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	jack = &ctx->kabylake_headset;
	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);

	snd_soc_component_set_jack(component, &ctx->kabylake_headset, NULL);

	return 0;
}

static int kabylake_dmic_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	ret = snd_soc_dapm_ignore_suspend(&rtd->card->dapm, "SoC DMIC");
	if (ret)
		dev_err(rtd->dev, "SoC DMIC - Ignore suspend failed %d\n", ret);

	return ret;
}

static int kabylake_hdmi_init(struct snd_soc_pcm_runtime *rtd, int device)
{
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = asoc_rtd_to_codec(rtd, 0);
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

static int kabylake_hdmi3_init(struct snd_soc_pcm_runtime *rtd)
{
	return kabylake_hdmi_init(rtd, KBL_DPCM_AUDIO_HDMI3_PB);
}

static int kabylake_da7219_fe_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dapm_context *dapm;
	struct snd_soc_component *component = asoc_rtd_to_cpu(rtd, 0)->component;

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

static unsigned int channels_quad[] = {
	QUAD_CHANNEL,
};

static struct snd_pcm_hw_constraint_list constraints_channels_quad = {
	.count = ARRAY_SIZE(channels_quad),
	.list = channels_quad,
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

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					   &constraints_channels);

	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_msbits(runtime, 0, 16, 16);

	snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);

	return 0;
}

static const struct snd_soc_ops kabylake_da7219_fe_ops = {
	.startup = kbl_fe_startup,
};

static int kabylake_dmic_fixup(struct snd_soc_pcm_runtime *rtd,
		struct snd_pcm_hw_params *params)
{
	struct snd_interval *chan = hw_param_interval(params,
				SNDRV_PCM_HW_PARAM_CHANNELS);

	/*
	 * set BE channel constraint as user FE channels
	 */

	if (params_channels(params) == 2)
		chan->min = chan->max = 2;
	else
		chan->min = chan->max = 4;

	return 0;
}

static int kabylake_dmic_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.channels_min = runtime->hw.channels_max = QUAD_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			&constraints_channels_quad);

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);
}

static struct snd_soc_ops kabylake_dmic_ops = {
	.startup = kabylake_dmic_startup,
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

static int kabylake_refcap_startup(struct snd_pcm_substream *substream)
{
	substream->runtime->hw.channels_max = 1;
	snd_pcm_hw_constraint_list(substream->runtime, 0,
					SNDRV_PCM_HW_PARAM_CHANNELS,
					&constraints_refcap);

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				&constraints_16000);
}


static struct snd_soc_ops skylake_refcap_ops = {
	.startup = kabylake_refcap_startup,
};

static struct snd_soc_codec_conf max98927_codec_conf[] = {

	{
		.dlc = COMP_CODEC_CONF(MAX98927_DEV0_NAME),
		.name_prefix = "Right",
	},

	{
		.dlc = COMP_CODEC_CONF(MAX98927_DEV1_NAME),
		.name_prefix = "Left",
	},
};

static struct snd_soc_codec_conf max98373_codec_conf[] = {

	{
		.dlc = COMP_CODEC_CONF(MAX98373_DEV0_NAME),
		.name_prefix = "Right",
	},

	{
		.dlc = COMP_CODEC_CONF(MAX98373_DEV1_NAME),
		.name_prefix = "Left",
	},
};

static struct snd_soc_dai_link_component max98373_ssp0_codec_components[] = {
	{ /* Left */
		.name = MAX98373_DEV0_NAME,
		.dai_name = MAX98373_CODEC_DAI,
	},

	{  /* For Right */
		.name = MAX98373_DEV1_NAME,
		.dai_name = MAX98373_CODEC_DAI,
	},

};

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(system,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));

SND_SOC_DAILINK_DEF(echoref,
	DAILINK_COMP_ARRAY(COMP_CPU("Echoref Pin")));

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

SND_SOC_DAILINK_DEF(system2,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin2")));

SND_SOC_DAILINK_DEF(ssp0_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP0 Pin")));
SND_SOC_DAILINK_DEF(ssp0_codec,
	DAILINK_COMP_ARRAY(
	/* Left */	COMP_CODEC(MAX98927_DEV0_NAME, MAX98927_CODEC_DAI),
	/* For Right */	COMP_CODEC(MAX98927_DEV1_NAME, MAX98927_CODEC_DAI)));

SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP1 Pin")));
SND_SOC_DAILINK_DEF(ssp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-DLGS7219:00",
				      KBL_DIALOG_CODEC_DAI)));

SND_SOC_DAILINK_DEF(dmic_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC01 Pin")));
SND_SOC_DAILINK_DEF(dmic_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));

SND_SOC_DAILINK_DEF(idisp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp1 Pin")));
SND_SOC_DAILINK_DEF(idisp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi1")));

SND_SOC_DAILINK_DEF(idisp2_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp2 Pin")));
SND_SOC_DAILINK_DEF(idisp2_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi2")));

SND_SOC_DAILINK_DEF(idisp3_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp3 Pin")));
SND_SOC_DAILINK_DEF(idisp3_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi3")));

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
		.init = kabylake_da7219_fe_init,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.ops = &kabylake_da7219_fe_ops,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[KBL_DPCM_AUDIO_ECHO_REF_CP] = {
		.name = "Kbl Audio Echo Reference cap",
		.stream_name = "Echoreference Capture",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		SND_SOC_DAILINK_REG(echoref, dummy, platform),
	},
	[KBL_DPCM_AUDIO_REF_CP] = {
		.name = "Kbl Audio Reference cap",
		.stream_name = "Wake on Voice",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &skylake_refcap_ops,
		SND_SOC_DAILINK_REG(reference, dummy, platform),
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
	[KBL_DPCM_AUDIO_HDMI3_PB] = {
		.name = "Kbl HDMI Port3",
		.stream_name = "Hdmi3",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi3, dummy, platform),
	},
	[KBL_DPCM_AUDIO_HS_PB] = {
		.name = "Kbl Audio Headset Playback",
		.stream_name = "Headset Audio",
		.dpcm_playback = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.init = kabylake_da7219_fe_init,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.ops = &kabylake_da7219_fe_ops,
		SND_SOC_DAILINK_REG(system2, dummy, platform),
	},
	[KBL_DPCM_AUDIO_CP] = {
		.name = "Kbl Audio Capture Port",
		.stream_name = "Audio Record",
		.dynamic = 1,
		.nonatomic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
		.ops = &kabylake_da7219_fe_ops,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},

	/* Back End DAI links */
	{
		/* SSP0 - Codec */
		.name = "SSP0-Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_DSP_B |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = kabylake_ssp_fixup,
		.ops = &kabylake_ssp0_ops,
		SND_SOC_DAILINK_REG(ssp0_pin, ssp0_codec, platform),
	},
	{
		/* SSP1 - Codec */
		.name = "SSP1-Codec",
		.id = 1,
		.no_pcm = 1,
		.init = kabylake_da7219_codec_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = kabylake_ssp_fixup,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp1_pin, ssp1_codec, platform),
	},
	{
		.name = "dmic01",
		.id = 2,
		.init = kabylake_dmic_init,
		.be_hw_params_fixup = kabylake_dmic_fixup,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
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
	{
		.name = "iDisp3",
		.id = 5,
		.init = kabylake_hdmi3_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_pin, idisp3_codec, platform),
	},
};

/* kabylake digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link kabylake_max98_927_373_dais[] = {
	/* Front End DAI links */
	[KBL_DPCM_AUDIO_PB] = {
		.name = "Kbl Audio Port",
		.stream_name = "Audio",
		.dynamic = 1,
		.nonatomic = 1,
		.init = kabylake_da7219_fe_init,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.ops = &kabylake_da7219_fe_ops,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[KBL_DPCM_AUDIO_ECHO_REF_CP] = {
		.name = "Kbl Audio Echo Reference cap",
		.stream_name = "Echoreference Capture",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		SND_SOC_DAILINK_REG(echoref, dummy, platform),
	},
	[KBL_DPCM_AUDIO_REF_CP] = {
		.name = "Kbl Audio Reference cap",
		.stream_name = "Wake on Voice",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &skylake_refcap_ops,
		SND_SOC_DAILINK_REG(reference, dummy, platform),
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
	[KBL_DPCM_AUDIO_HDMI3_PB] = {
		.name = "Kbl HDMI Port3",
		.stream_name = "Hdmi3",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi3, dummy, platform),
	},

	/* Back End DAI links */
	{
		/* SSP0 - Codec */
		.name = "SSP0-Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_DSP_B |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = kabylake_ssp_fixup,
		.ops = &kabylake_ssp0_ops,
		SND_SOC_DAILINK_REG(ssp0_pin, ssp0_codec),
	},
	{
		.name = "dmic01",
		.id = 1,
		.init = kabylake_dmic_init,
		.be_hw_params_fixup = kabylake_dmic_fixup,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
	},
	{
		.name = "iDisp1",
		.id = 2,
		.dpcm_playback = 1,
		.init = kabylake_hdmi1_init,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_pin, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 3,
		.init = kabylake_hdmi2_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_pin, idisp2_codec, platform),
	},
	{
		.name = "iDisp3",
		.id = 4,
		.init = kabylake_hdmi3_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_pin, idisp3_codec, platform),
	},
};

static int kabylake_card_late_probe(struct snd_soc_card *card)
{
	struct kbl_codec_private *ctx = snd_soc_card_get_drvdata(card);
	struct kbl_hdmi_pcm *pcm;
	struct snd_soc_dapm_context *dapm = &card->dapm;
	struct snd_soc_component *component = NULL;
	int err, i = 0;
	char jack_name[NAME_SIZE];

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			"HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					SND_JACK_AVOUT, &kabylake_hdmi[i]);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
						&kabylake_hdmi[i]);
		if (err < 0)
			return err;

		i++;
	}

	if (!component)
		return -EINVAL;


	err = hdac_hdmi_jack_port_init(component, &card->dapm);

	if (err < 0)
		return err;

	err = snd_soc_dapm_disable_pin(dapm, "Left Spk");
	if (err) {
		dev_err(card->dev, "failed to disable Left Spk: %d\n", err);
		return err;
	}

	err = snd_soc_dapm_disable_pin(dapm, "Right Spk");
	if (err) {
		dev_err(card->dev, "failed to disable Right Spk: %d\n", err);
		return err;
	}

	return snd_soc_dapm_sync(dapm);
}

/* kabylake audio machine driver for SPT + DA7219 */
static struct snd_soc_card kbl_audio_card_da7219_m98927 = {
	.name = "kblda7219m98927",
	.owner = THIS_MODULE,
	.dai_link = kabylake_dais,
	.num_links = ARRAY_SIZE(kabylake_dais),
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

/* kabylake audio machine driver for Maxim98927 */
static struct snd_soc_card kbl_audio_card_max98927 = {
	.name = "kblmax98927",
	.owner = THIS_MODULE,
	.dai_link = kabylake_max98_927_373_dais,
	.num_links = ARRAY_SIZE(kabylake_max98_927_373_dais),
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

static struct snd_soc_card kbl_audio_card_da7219_m98373 = {
	.name = "kblda7219m98373",
	.owner = THIS_MODULE,
	.dai_link = kabylake_dais,
	.num_links = ARRAY_SIZE(kabylake_dais),
	.controls = kabylake_controls,
	.num_controls = ARRAY_SIZE(kabylake_controls),
	.dapm_widgets = kabylake_widgets,
	.num_dapm_widgets = ARRAY_SIZE(kabylake_widgets),
	.dapm_routes = kabylake_map,
	.num_dapm_routes = ARRAY_SIZE(kabylake_map),
	.codec_conf = max98373_codec_conf,
	.num_configs = ARRAY_SIZE(max98373_codec_conf),
	.fully_routed = true,
	.late_probe = kabylake_card_late_probe,
};

static struct snd_soc_card kbl_audio_card_max98373 = {
	.name = "kblmax98373",
	.owner = THIS_MODULE,
	.dai_link = kabylake_max98_927_373_dais,
	.num_links = ARRAY_SIZE(kabylake_max98_927_373_dais),
	.controls = kabylake_controls,
	.num_controls = ARRAY_SIZE(kabylake_controls),
	.dapm_widgets = kabylake_widgets,
	.num_dapm_widgets = ARRAY_SIZE(kabylake_widgets),
	.dapm_routes = kabylake_map,
	.num_dapm_routes = ARRAY_SIZE(kabylake_map),
	.codec_conf = max98373_codec_conf,
	.num_configs = ARRAY_SIZE(max98373_codec_conf),
	.fully_routed = true,
	.late_probe = kabylake_card_late_probe,
};

static int kabylake_audio_probe(struct platform_device *pdev)
{
	struct kbl_codec_private *ctx;
	struct snd_soc_dai_link *kbl_dai_link;
	struct snd_soc_dai_link_component **codecs;
	int i;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	kabylake_audio_card =
		(struct snd_soc_card *)pdev->id_entry->driver_data;

	kbl_dai_link = kabylake_audio_card->dai_link;

	/* Update codecs for SSP0 with max98373 codec info */
	if (!strcmp(pdev->name, "kbl_da7219_max98373") ||
		(!strcmp(pdev->name, "kbl_max98373"))) {
		for (i = 0; i < kabylake_audio_card->num_links; ++i) {
			if (strcmp(kbl_dai_link[i].name, "SSP0-Codec"))
				continue;

			codecs = &(kbl_dai_link[i].codecs);
			*codecs = max98373_ssp0_codec_components;
			kbl_dai_link[i].num_codecs =
				ARRAY_SIZE(max98373_ssp0_codec_components);
			break;
		}
	}
	kabylake_audio_card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(kabylake_audio_card, ctx);

	return devm_snd_soc_register_card(&pdev->dev, kabylake_audio_card);
}

static const struct platform_device_id kbl_board_ids[] = {
	{
		.name = "kbl_da7219_max98927",
		.driver_data =
			(kernel_ulong_t)&kbl_audio_card_da7219_m98927,
	},
	{
		.name = "kbl_max98927",
		.driver_data =
			(kernel_ulong_t)&kbl_audio_card_max98927,
	},
	{
		.name = "kbl_da7219_max98373",
		.driver_data =
			(kernel_ulong_t)&kbl_audio_card_da7219_m98373,
	},
	{
		.name = "kbl_max98373",
		.driver_data =
			(kernel_ulong_t)&kbl_audio_card_max98373,
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, kbl_board_ids);

static struct platform_driver kabylake_audio = {
	.probe = kabylake_audio_probe,
	.driver = {
		.name = "kbl_da7219_max98_927_373",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = kbl_board_ids,
};

module_platform_driver(kabylake_audio)

/* Module information */
MODULE_DESCRIPTION("Audio KabyLake Machine driver for MAX98927/MAX98373 & DA7219");
MODULE_AUTHOR("Mac Chiang <mac.chiang@intel.com>");
MODULE_LICENSE("GPL v2");
