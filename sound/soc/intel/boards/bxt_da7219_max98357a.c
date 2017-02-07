/*
 * Intel Broxton-P I2S Machine Driver
 *
 * Copyright (C) 2016, Intel Corporation. All rights reserved.
 *
 * Modified from:
 *   Intel Skylake I2S Machine driver
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "../../codecs/hdac_hdmi.h"
#include "../../codecs/da7219.h"
#include "../../codecs/da7219-aad.h"

#define BXT_DIALOG_CODEC_DAI	"da7219-hifi"
#define BXT_MAXIM_CODEC_DAI	"HiFi"
#define DUAL_CHANNEL		2
#define QUAD_CHANNEL		4

static struct snd_soc_jack broxton_headset;
static struct snd_soc_jack broxton_hdmi[3];

struct bxt_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct bxt_card_private {
	struct list_head hdmi_pcm_list;
};

enum {
	BXT_DPCM_AUDIO_PB = 0,
	BXT_DPCM_AUDIO_CP,
	BXT_DPCM_AUDIO_REF_CP,
	BXT_DPCM_AUDIO_DMIC_CP,
	BXT_DPCM_AUDIO_HDMI1_PB,
	BXT_DPCM_AUDIO_HDMI2_PB,
	BXT_DPCM_AUDIO_HDMI3_PB,
};

static const struct snd_kcontrol_new broxton_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Spk"),
};

static const struct snd_soc_dapm_widget broxton_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Spk", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
	SND_SOC_DAPM_SPK("HDMI1", NULL),
	SND_SOC_DAPM_SPK("HDMI2", NULL),
	SND_SOC_DAPM_SPK("HDMI3", NULL),
};

static const struct snd_soc_dapm_route broxton_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{"Headphone Jack", NULL, "HPL"},
	{"Headphone Jack", NULL, "HPR"},

	/* speaker */
	{"Spk", NULL, "Speaker"},

	/* other jacks */
	{"MIC", NULL, "Headset Mic"},

	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},

	/* CODEC BE connections */
	{"HiFi Playback", NULL, "ssp5 Tx"},
	{"ssp5 Tx", NULL, "codec0_out"},

	{"Playback", NULL, "ssp1 Tx"},
	{"ssp1 Tx", NULL, "codec1_out"},

	{"codec0_in", NULL, "ssp1 Rx"},
	{"ssp1 Rx", NULL, "Capture"},

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
};

static int broxton_ssp_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will convert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = DUAL_CHANNEL;

	/* set SSP to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set(fmt, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int broxton_da7219_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_codec *codec = rtd->codec;

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new(rtd->card, "Headset Jack",
			SND_JACK_HEADSET | SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3 | SND_JACK_LINEOUT,
			&broxton_headset, NULL, 0);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	da7219_aad_jack_det(codec, &broxton_headset);

	snd_soc_dapm_ignore_suspend(&rtd->card->dapm, "SoC DMIC");

	return ret;
}

static int broxton_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct bxt_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = rtd->codec_dai;
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
	struct snd_soc_component *component = rtd->cpu_dai->component;

	dapm = snd_soc_component_get_dapm(component);
	snd_soc_dapm_ignore_suspend(dapm, "Reference Capture");

	return 0;
}

static unsigned int rates[] = {
	48000,
};

static struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list  = rates,
	.mask = 0,
};

static unsigned int channels[] = {
	DUAL_CHANNEL,
};

static struct snd_pcm_hw_constraint_list constraints_channels = {
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

static int broxton_da7219_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai,
			DA7219_CLKSRC_MCLK, 19200000, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(codec_dai->dev, "can't set codec sysclk configuration\n");

	ret = snd_soc_dai_set_pll(codec_dai, 0,
			DA7219_SYSCLK_PLL_SRM, 0, DA7219_PLL_FREQ_OUT_98304);
	if (ret < 0) {
		dev_err(codec_dai->dev, "failed to start PLL: %d\n", ret);
		return -EIO;
	}

	return ret;
}

static int broxton_da7219_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_pll(codec_dai, 0,
			DA7219_SYSCLK_MCLK, 0, 0);
	if (ret < 0) {
		dev_err(codec_dai->dev, "failed to stop PLL: %d\n", ret);
		return -EIO;
	}

	return ret;
}

static const struct snd_soc_ops broxton_da7219_ops = {
	.hw_params = broxton_da7219_hw_params,
	.hw_free = broxton_da7219_hw_free,
};

static int broxton_dmic_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	if (params_channels(params) == 2)
		channels->min = channels->max = 2;
	else
		channels->min = channels->max = 4;

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

static int broxton_refcap_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE,
			&constraints_16000);
};

static const struct snd_soc_ops broxton_refcap_ops = {
	.startup = broxton_refcap_startup,
};

/* broxton digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link broxton_dais[] = {
	/* Front End DAI links */
	[BXT_DPCM_AUDIO_PB] =
	{
		.name = "Bxt Audio Port",
		.stream_name = "Audio",
		.cpu_dai_name = "System Pin",
		.platform_name = "0000:00:0e.0",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.nonatomic = 1,
		.init = broxton_da7219_fe_init,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.ops = &broxton_da7219_fe_ops,
	},
	[BXT_DPCM_AUDIO_CP] =
	{
		.name = "Bxt Audio Capture Port",
		.stream_name = "Audio Record",
		.cpu_dai_name = "System Pin",
		.platform_name = "0000:00:0e.0",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.nonatomic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
		.ops = &broxton_da7219_fe_ops,
	},
	[BXT_DPCM_AUDIO_REF_CP] =
	{
		.name = "Bxt Audio Reference cap",
		.stream_name = "Refcap",
		.cpu_dai_name = "Reference Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:0e.0",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &broxton_refcap_ops,
	},
	[BXT_DPCM_AUDIO_DMIC_CP] =
	{
		.name = "Bxt Audio DMIC cap",
		.stream_name = "dmiccap",
		.cpu_dai_name = "DMIC Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:0e.0",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &broxton_dmic_ops,
	},
	[BXT_DPCM_AUDIO_HDMI1_PB] =
	{
		.name = "Bxt HDMI Port1",
		.stream_name = "Hdmi1",
		.cpu_dai_name = "HDMI1 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:0e.0",
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
	},
	[BXT_DPCM_AUDIO_HDMI2_PB] =
	{
		.name = "Bxt HDMI Port2",
		.stream_name = "Hdmi2",
		.cpu_dai_name = "HDMI2 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:0e.0",
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
	},
	[BXT_DPCM_AUDIO_HDMI3_PB] =
	{
		.name = "Bxt HDMI Port3",
		.stream_name = "Hdmi3",
		.cpu_dai_name = "HDMI3 Pin",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.platform_name = "0000:00:0e.0",
		.dpcm_playback = 1,
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
	},
	/* Back End DAI links */
	{
		/* SSP5 - Codec */
		.name = "SSP5-Codec",
		.id = 0,
		.cpu_dai_name = "SSP5 Pin",
		.platform_name = "0000:00:0e.0",
		.no_pcm = 1,
		.codec_name = "MX98357A:00",
		.codec_dai_name = BXT_MAXIM_CODEC_DAI,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = broxton_ssp_fixup,
		.dpcm_playback = 1,
	},
	{
		/* SSP1 - Codec */
		.name = "SSP1-Codec",
		.id = 1,
		.cpu_dai_name = "SSP1 Pin",
		.platform_name = "0000:00:0e.0",
		.no_pcm = 1,
		.codec_name = "i2c-DLGS7219:00",
		.codec_dai_name = BXT_DIALOG_CODEC_DAI,
		.init = broxton_da7219_codec_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = broxton_ssp_fixup,
		.ops = &broxton_da7219_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "dmic01",
		.id = 2,
		.cpu_dai_name = "DMIC01 Pin",
		.codec_name = "dmic-codec",
		.codec_dai_name = "dmic-hifi",
		.platform_name = "0000:00:0e.0",
		.ignore_suspend = 1,
		.be_hw_params_fixup = broxton_dmic_fixup,
		.dpcm_capture = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp1",
		.id = 3,
		.cpu_dai_name = "iDisp1 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi1",
		.platform_name = "0000:00:0e.0",
		.init = broxton_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp2",
		.id = 4,
		.cpu_dai_name = "iDisp2 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi2",
		.platform_name = "0000:00:0e.0",
		.init = broxton_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp3",
		.id = 5,
		.cpu_dai_name = "iDisp3 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi3",
		.platform_name = "0000:00:0e.0",
		.init = broxton_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
};

#define NAME_SIZE	32
static int bxt_card_late_probe(struct snd_soc_card *card)
{
	struct bxt_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct bxt_hdmi_pcm *pcm;
	int err, i = 0;
	char jack_name[NAME_SIZE];

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		snprintf(jack_name, sizeof(jack_name),
			"HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					SND_JACK_AVOUT, &broxton_hdmi[i],
					NULL, 0);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
						&broxton_hdmi[i]);
		if (err < 0)
			return err;

		i++;
	}

	return 0;
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
	.dapm_routes = broxton_map,
	.num_dapm_routes = ARRAY_SIZE(broxton_map),
	.fully_routed = true,
	.late_probe = bxt_card_late_probe,
};

static int broxton_audio_probe(struct platform_device *pdev)
{
	struct bxt_card_private *ctx;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_ATOMIC);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	broxton_audio_card.dev = &pdev->dev;
	snd_soc_card_set_drvdata(&broxton_audio_card, ctx);

	return devm_snd_soc_register_card(&pdev->dev, &broxton_audio_card);
}

static struct platform_driver broxton_audio = {
	.probe = broxton_audio_probe,
	.driver = {
		.name = "bxt_da7219_max98357a_i2s",
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(broxton_audio)

/* Module information */
MODULE_DESCRIPTION("Audio Machine driver-DA7219 & MAX98357A in I2S mode");
MODULE_AUTHOR("Sathyanarayana Nujella <sathyanarayana.nujella@intel.com>");
MODULE_AUTHOR("Rohit Ainapure <rohit.m.ainapure@intel.com>");
MODULE_AUTHOR("Harsha Priya <harshapriya.n@intel.com>");
MODULE_AUTHOR("Conrad Cooke <conrad.cooke@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bxt_da7219_max98357a_i2s");
