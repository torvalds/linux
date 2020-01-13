// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Intel Corporation.

/*
 * Intel Geminilake I2S Machine Driver with MAX98357A & RT5682 Codecs
 *
 * Modified from:
 *   Intel Apollolake I2S Machine driver
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
#include "../../codecs/rt5682.h"
#include "../../codecs/hdac_hdmi.h"
#include "hda_dsp_common.h"

/* The platform clock outputs 19.2Mhz clock to codec as I2S MCLK */
#define GLK_PLAT_CLK_FREQ 19200000
#define RT5682_PLL_FREQ (48000 * 512)
#define GLK_REALTEK_CODEC_DAI "rt5682-aif1"
#define GLK_MAXIM_CODEC_DAI "HiFi"
#define MAXIM_DEV0_NAME "MX98357A:00"
#define DUAL_CHANNEL 2
#define QUAD_CHANNEL 4
#define NAME_SIZE 32

static struct snd_soc_jack geminilake_hdmi[3];

struct glk_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct glk_card_private {
	struct snd_soc_jack geminilake_headset;
	struct list_head hdmi_pcm_list;
	bool common_hdmi_codec_drv;
};

enum {
	GLK_DPCM_AUDIO_PB = 0,
	GLK_DPCM_AUDIO_CP,
	GLK_DPCM_AUDIO_HS_PB,
	GLK_DPCM_AUDIO_ECHO_REF_CP,
	GLK_DPCM_AUDIO_REF_CP,
	GLK_DPCM_AUDIO_DMIC_CP,
	GLK_DPCM_AUDIO_HDMI1_PB,
	GLK_DPCM_AUDIO_HDMI2_PB,
	GLK_DPCM_AUDIO_HDMI3_PB,
};

static const struct snd_kcontrol_new geminilake_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Spk"),
};

static const struct snd_soc_dapm_widget geminilake_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Spk", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
	SND_SOC_DAPM_SPK("HDMI1", NULL),
	SND_SOC_DAPM_SPK("HDMI2", NULL),
	SND_SOC_DAPM_SPK("HDMI3", NULL),
};

static const struct snd_soc_dapm_route geminilake_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },

	/* speaker */
	{ "Spk", NULL, "Speaker" },

	/* other jacks */
	{ "IN1P", NULL, "Headset Mic" },

	/* digital mics */
	{ "DMic", NULL, "SoC DMIC" },

	/* CODEC BE connections */
	{ "HiFi Playback", NULL, "ssp1 Tx" },
	{ "ssp1 Tx", NULL, "codec0_out" },

	{ "AIF1 Playback", NULL, "ssp2 Tx" },
	{ "ssp2 Tx", NULL, "codec1_out" },

	{ "codec0_in", NULL, "ssp2 Rx" },
	{ "ssp2 Rx", NULL, "AIF1 Capture" },

	{ "HDMI1", NULL, "hif5-0 Output" },
	{ "HDMI2", NULL, "hif6-0 Output" },
	{ "HDMI2", NULL, "hif7-0 Output" },

	{ "hifi3", NULL, "iDisp3 Tx" },
	{ "iDisp3 Tx", NULL, "iDisp3_out" },
	{ "hifi2", NULL, "iDisp2 Tx" },
	{ "iDisp2 Tx", NULL, "iDisp2_out" },
	{ "hifi1", NULL, "iDisp1 Tx" },
	{ "iDisp1 Tx", NULL, "iDisp1_out" },

	/* DMIC */
	{ "dmic01_hifi", NULL, "DMIC01 Rx" },
	{ "DMIC01 Rx", NULL, "DMIC AIF" },
};

static int geminilake_ssp_fixup(struct snd_soc_pcm_runtime *rtd,
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

static int geminilake_rt5682_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct glk_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = rtd->codec_dai->component;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_jack *jack;
	int ret;

	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5682_PLL1_S_MCLK,
					GLK_PLAT_CLK_FREQ, RT5682_PLL_FREQ);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	/* Configure sysclk for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682_SCLK_S_PLL1,
					RT5682_PLL_FREQ, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new(rtd->card, "Headset Jack",
			SND_JACK_HEADSET | SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3 | SND_JACK_LINEOUT,
			&ctx->geminilake_headset, NULL, 0);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	jack = &ctx->geminilake_headset;

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, jack, NULL);

	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return ret;
};

static int geminilake_rt5682_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	/* Set valid bitmask & configuration for I2S in 24 bit */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x0, 0x0, 2, 24);
	if (ret < 0) {
		dev_err(rtd->dev, "set TDM slot err:%d\n", ret);
		return ret;
	}

	return ret;
}

static struct snd_soc_ops geminilake_rt5682_ops = {
	.hw_params = geminilake_rt5682_hw_params,
};

static int geminilake_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct glk_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = rtd->codec_dai;
	struct glk_hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->device = GLK_DPCM_AUDIO_HDMI1_PB + dai->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

static int geminilake_rt5682_fe_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = rtd->cpu_dai->component;
	struct snd_soc_dapm_context *dapm;
	int ret;

	dapm = snd_soc_component_get_dapm(component);
	ret = snd_soc_dapm_ignore_suspend(dapm, "Reference Capture");
	if (ret) {
		dev_err(rtd->dev, "Ref Cap ignore suspend failed %d\n", ret);
		return ret;
	}

	return ret;
}

static const unsigned int rates[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list  = rates,
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

static int geminilake_dmic_fixup(struct snd_soc_pcm_runtime *rtd,
		struct snd_pcm_hw_params *params)
{
	struct snd_interval *chan = hw_param_interval(params,
				SNDRV_PCM_HW_PARAM_CHANNELS);

	/*
	 * set BE channel constraint as user FE channels
	 */
	chan->min = chan->max = 4;

	return 0;
}

static int geminilake_dmic_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.channels_min = runtime->hw.channels_max = QUAD_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			&constraints_channels_quad);

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);
}

static const struct snd_soc_ops geminilake_dmic_ops = {
	.startup = geminilake_dmic_startup,
};

static const unsigned int rates_16000[] = {
	16000,
};

static const struct snd_pcm_hw_constraint_list constraints_16000 = {
	.count = ARRAY_SIZE(rates_16000),
	.list  = rates_16000,
};

static int geminilake_refcap_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				&constraints_16000);
};

static const struct snd_soc_ops geminilake_refcap_ops = {
	.startup = geminilake_refcap_startup,
};

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(system,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin")));

SND_SOC_DAILINK_DEF(system2,
	DAILINK_COMP_ARRAY(COMP_CPU("System Pin2")));

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

SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP1 Pin")));
SND_SOC_DAILINK_DEF(ssp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC(MAXIM_DEV0_NAME,
				      GLK_MAXIM_CODEC_DAI)));

SND_SOC_DAILINK_DEF(ssp2_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP2 Pin")));
SND_SOC_DAILINK_DEF(ssp2_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5682:00",
				      GLK_REALTEK_CODEC_DAI)));

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
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:0e.0")));

/* geminilake digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link geminilake_dais[] = {
	/* Front End DAI links */
	[GLK_DPCM_AUDIO_PB] = {
		.name = "Glk Audio Port",
		.stream_name = "Audio",
		.dynamic = 1,
		.nonatomic = 1,
		.init = geminilake_rt5682_fe_init,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[GLK_DPCM_AUDIO_CP] = {
		.name = "Glk Audio Capture Port",
		.stream_name = "Audio Record",
		.dynamic = 1,
		.nonatomic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(system, dummy, platform),
	},
	[GLK_DPCM_AUDIO_HS_PB] = {
		.name = "Glk Audio Headset Playback",
		.stream_name = "Headset Audio",
		.dpcm_playback = 1,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(system2, dummy, platform),
	},
	[GLK_DPCM_AUDIO_ECHO_REF_CP] = {
		.name = "Glk Audio Echo Reference cap",
		.stream_name = "Echoreference Capture",
		.init = NULL,
		.capture_only = 1,
		.nonatomic = 1,
		SND_SOC_DAILINK_REG(echoref, dummy, platform),
	},
	[GLK_DPCM_AUDIO_REF_CP] = {
		.name = "Glk Audio Reference cap",
		.stream_name = "Refcap",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &geminilake_refcap_ops,
		SND_SOC_DAILINK_REG(reference, dummy, platform),
	},
	[GLK_DPCM_AUDIO_DMIC_CP] = {
		.name = "Glk Audio DMIC cap",
		.stream_name = "dmiccap",
		.init = NULL,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.dynamic = 1,
		.ops = &geminilake_dmic_ops,
		SND_SOC_DAILINK_REG(dmic, dummy, platform),
	},
	[GLK_DPCM_AUDIO_HDMI1_PB] = {
		.name = "Glk HDMI Port1",
		.stream_name = "Hdmi1",
		.dpcm_playback = 1,
		.init = NULL,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi1, dummy, platform),
	},
	[GLK_DPCM_AUDIO_HDMI2_PB] =	{
		.name = "Glk HDMI Port2",
		.stream_name = "Hdmi2",
		.dpcm_playback = 1,
		.init = NULL,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi2, dummy, platform),
	},
	[GLK_DPCM_AUDIO_HDMI3_PB] =	{
		.name = "Glk HDMI Port3",
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
		/* SSP1 - Codec */
		.name = "SSP1-Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = geminilake_ssp_fixup,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(ssp1_pin, ssp1_codec, platform),
	},
	{
		/* SSP2 - Codec */
		.name = "SSP2-Codec",
		.id = 1,
		.no_pcm = 1,
		.init = geminilake_rt5682_codec_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = geminilake_ssp_fixup,
		.ops = &geminilake_rt5682_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp2_pin, ssp2_codec, platform),
	},
	{
		.name = "dmic01",
		.id = 2,
		.ignore_suspend = 1,
		.be_hw_params_fixup = geminilake_dmic_fixup,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
	},
	{
		.name = "iDisp1",
		.id = 3,
		.init = geminilake_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_pin, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 4,
		.init = geminilake_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_pin, idisp2_codec, platform),
	},
	{
		.name = "iDisp3",
		.id = 5,
		.init = geminilake_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_pin, idisp3_codec, platform),
	},
};

static int glk_card_late_probe(struct snd_soc_card *card)
{
	struct glk_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_component *component = NULL;
	char jack_name[NAME_SIZE];
	struct glk_hdmi_pcm *pcm;
	int err = 0;
	int i = 0;

	pcm = list_first_entry(&ctx->hdmi_pcm_list, struct glk_hdmi_pcm,
			       head);
	component = pcm->codec_dai->component;

	if (ctx->common_hdmi_codec_drv)
		return hda_dsp_hdmi_build_controls(card, component);

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			"HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					SND_JACK_AVOUT, &geminilake_hdmi[i],
					NULL, 0);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
						&geminilake_hdmi[i]);
		if (err < 0)
			return err;

		i++;
	}

	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}

/* geminilake audio machine driver for SPT + RT5682 */
static struct snd_soc_card glk_audio_card_rt5682_m98357a = {
	.name = "glkrt5682max",
	.owner = THIS_MODULE,
	.dai_link = geminilake_dais,
	.num_links = ARRAY_SIZE(geminilake_dais),
	.controls = geminilake_controls,
	.num_controls = ARRAY_SIZE(geminilake_controls),
	.dapm_widgets = geminilake_widgets,
	.num_dapm_widgets = ARRAY_SIZE(geminilake_widgets),
	.dapm_routes = geminilake_map,
	.num_dapm_routes = ARRAY_SIZE(geminilake_map),
	.fully_routed = true,
	.late_probe = glk_card_late_probe,
};

static int geminilake_audio_probe(struct platform_device *pdev)
{
	struct glk_card_private *ctx;
	struct snd_soc_acpi_mach *mach;
	const char *platform_name;
	struct snd_soc_card *card;
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	card = &glk_audio_card_rt5682_m98357a;
	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, ctx);

	/* override plaform name, if required */
	mach = (&pdev->dev)->platform_data;
	platform_name = mach->mach_params.platform;

	ret = snd_soc_fixup_dai_links_platform_name(card, platform_name);
	if (ret)
		return ret;

	ctx->common_hdmi_codec_drv = mach->mach_params.common_hdmi_codec_drv;

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static const struct platform_device_id glk_board_ids[] = {
	{
		.name = "glk_rt5682_max98357a",
		.driver_data =
			(kernel_ulong_t)&glk_audio_card_rt5682_m98357a,
	},
	{ }
};

static struct platform_driver geminilake_audio = {
	.probe = geminilake_audio_probe,
	.driver = {
		.name = "glk_rt5682_max98357a",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = glk_board_ids,
};
module_platform_driver(geminilake_audio)

/* Module information */
MODULE_DESCRIPTION("Geminilake Audio Machine driver-RT5682 & MAX98357A in I2S mode");
MODULE_AUTHOR("Naveen Manohar <naveen.m@intel.com>");
MODULE_AUTHOR("Harsha Priya <harshapriya.n@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:glk_rt5682_max98357a");
