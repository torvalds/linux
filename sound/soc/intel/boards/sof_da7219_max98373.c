// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2019 Intel Corporation.

/*
 * Intel SOF Machine driver for DA7219 + MAX98373/MAX98360A codec
 */

#include <linux/input.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../codecs/da7219.h"
#include "../../codecs/da7219-aad.h"
#include "hda_dsp_common.h"

#define DIALOG_CODEC_DAI	"da7219-hifi"
#define MAX98373_CODEC_DAI	"max98373-aif1"
#define MAXIM_DEV0_NAME		"i2c-MX98373:00"
#define MAXIM_DEV1_NAME		"i2c-MX98373:01"

struct hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct card_private {
	struct snd_soc_jack headset;
	struct list_head hdmi_pcm_list;
	struct snd_soc_jack hdmi[3];
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	int ret = 0;

	codec_dai = snd_soc_card_get_codec_dai(card, DIALOG_CODEC_DAI);
	if (!codec_dai) {
		dev_err(card->dev, "Codec dai not found; Unable to set/unset codec pll\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_MCLK,
					  0, 0);
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

static const struct snd_kcontrol_new controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_kcontrol_new m98360a_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Spk"),
};

/* For MAX98373 amp */
static const struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),

	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),

	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control, SND_SOC_DAPM_POST_PMD |
			    SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{ "Headphone Jack", NULL, "HPL" },
	{ "Headphone Jack", NULL, "HPR" },

	{ "MIC", NULL, "Headset Mic" },

	{ "Headphone Jack", NULL, "Platform Clock" },
	{ "Headset Mic", NULL, "Platform Clock" },

	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },

	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},
};

/* For MAX98360A amp */
static const struct snd_soc_dapm_widget max98360a_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),

	SND_SOC_DAPM_SPK("Spk", NULL),

	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control, SND_SOC_DAPM_POST_PMD |
			    SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route max98360a_map[] = {
	{ "Headphone Jack", NULL, "HPL" },
	{ "Headphone Jack", NULL, "HPR" },

	{ "MIC", NULL, "Headset Mic" },

	{ "Headphone Jack", NULL, "Platform Clock" },
	{ "Headset Mic", NULL, "Platform Clock" },

	{"Spk", NULL, "Speaker"},

	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},
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
};

static struct snd_soc_jack headset;

static int da7219_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_soc_jack *jack;
	int ret;

	/* Configure sysclk for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, DA7219_CLKSRC_MCLK, 24000000,
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
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3 | SND_JACK_LINEOUT,
					 &headset,
					 jack_pins,
					 ARRAY_SIZE(jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	jack = &headset;
	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);
	da7219_aad_jack_det(component, jack);

	return ret;
}

static int ssp1_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = asoc_substream_to_rtd(substream);
	int ret, j;

	for (j = 0; j < runtime->num_codecs; j++) {
		struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(runtime, j);

		if (!strcmp(codec_dai->component->name, MAXIM_DEV0_NAME)) {
			/* vmon_slot_no = 0 imon_slot_no = 1 for TX slots */
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x3, 3, 4, 16);
			if (ret < 0) {
				dev_err(runtime->dev, "DEV0 TDM slot err:%d\n", ret);
				return ret;
			}
		}
		if (!strcmp(codec_dai->component->name, MAXIM_DEV1_NAME)) {
			/* vmon_slot_no = 2 imon_slot_no = 3 for TX slots */
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xC, 3, 4, 16);
			if (ret < 0) {
				dev_err(runtime->dev, "DEV1 TDM slot err:%d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static struct snd_soc_ops ssp1_ops = {
	.hw_params = ssp1_hw_params,
};

static struct snd_soc_codec_conf max98373_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(MAXIM_DEV0_NAME),
		.name_prefix = "Right",
	},
	{
		.dlc = COMP_CODEC_CONF(MAXIM_DEV1_NAME),
		.name_prefix = "Left",
	},
};

static int hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = asoc_rtd_to_codec(rtd, 0);
	struct hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->device = dai->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

static int card_late_probe(struct snd_soc_card *card)
{
	struct card_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_acpi_mach *mach = (card->dev)->platform_data;
	struct hdmi_pcm *pcm;

	if (mach->mach_params.common_hdmi_codec_drv) {
		pcm = list_first_entry(&ctx->hdmi_pcm_list, struct hdmi_pcm,
				       head);
		return hda_dsp_hdmi_build_controls(card,
						   pcm->codec_dai->component);
	}

	return -EINVAL;
}

SND_SOC_DAILINK_DEF(ssp0_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP0 Pin")));
SND_SOC_DAILINK_DEF(ssp0_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-DLGS7219:00", DIALOG_CODEC_DAI)));

SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP1 Pin")));
SND_SOC_DAILINK_DEF(ssp1_amps,
	DAILINK_COMP_ARRAY(
	/* Left */	COMP_CODEC(MAXIM_DEV0_NAME, MAX98373_CODEC_DAI),
	/* Right */	COMP_CODEC(MAXIM_DEV1_NAME, MAX98373_CODEC_DAI)));

SND_SOC_DAILINK_DEF(ssp1_m98360a,
	DAILINK_COMP_ARRAY(COMP_CODEC("MX98360A:00", "HiFi")));

SND_SOC_DAILINK_DEF(dmic_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC01 Pin")));
SND_SOC_DAILINK_DEF(dmic_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));

SND_SOC_DAILINK_DEF(dmic16k_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC16k Pin")));

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

SND_SOC_DAILINK_DEF(platform, /* subject to be overridden during probe */
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:1f.3")));

static struct snd_soc_dai_link dais[] = {
	/* Back End DAI links */
	{
		.name = "SSP1-Codec",
		.id = 0,
		.ignore_pmdown_time = 1,
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1, /* IV feedback */
		.ops = &ssp1_ops,
		SND_SOC_DAILINK_REG(ssp1_pin, ssp1_amps, platform),
	},
	{
		.name = "SSP0-Codec",
		.id = 1,
		.no_pcm = 1,
		.init = da7219_codec_init,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp0_pin, ssp0_codec, platform),
	},
	{
		.name = "dmic01",
		.id = 2,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
	},
	{
		.name = "iDisp1",
		.id = 3,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_pin, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 4,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_pin, idisp2_codec, platform),
	},
	{
		.name = "iDisp3",
		.id = 5,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_pin, idisp3_codec, platform),
	},
	{
		.name = "dmic16k",
		.id = 6,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic16k_pin, dmic_codec, platform),
	}
};

static struct snd_soc_card card_da7219_m98373 = {
	.name = "da7219max",
	.owner = THIS_MODULE,
	.dai_link = dais,
	.num_links = ARRAY_SIZE(dais),
	.controls = controls,
	.num_controls = ARRAY_SIZE(controls),
	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.codec_conf = max98373_codec_conf,
	.num_configs = ARRAY_SIZE(max98373_codec_conf),
	.fully_routed = true,
	.late_probe = card_late_probe,
};

static struct snd_soc_card card_da7219_m98360a = {
	.name = "da7219max98360a",
	.owner = THIS_MODULE,
	.dai_link = dais,
	.num_links = ARRAY_SIZE(dais),
	.controls = m98360a_controls,
	.num_controls = ARRAY_SIZE(m98360a_controls),
	.dapm_widgets = max98360a_widgets,
	.num_dapm_widgets = ARRAY_SIZE(max98360a_widgets),
	.dapm_routes = max98360a_map,
	.num_dapm_routes = ARRAY_SIZE(max98360a_map),
	.fully_routed = true,
	.late_probe = card_late_probe,
};

static int audio_probe(struct platform_device *pdev)
{
	static struct snd_soc_card *card;
	struct snd_soc_acpi_mach *mach;
	struct card_private *ctx;
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	/* By default dais[0] is configured for max98373 */
	if (!strcmp(pdev->name, "sof_da7219_mx98360a")) {
		dais[0] = (struct snd_soc_dai_link) {
			.name = "SSP1-Codec",
			.id = 0,
			.no_pcm = 1,
			.dpcm_playback = 1,
			.ignore_pmdown_time = 1,
			SND_SOC_DAILINK_REG(ssp1_pin, ssp1_m98360a, platform) };
	}

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);
	card = (struct snd_soc_card *)pdev->id_entry->driver_data;
	card->dev = &pdev->dev;

	mach = pdev->dev.platform_data;
	ret = snd_soc_fixup_dai_links_platform_name(card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(card, ctx);

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "sof_da7219_mx98373",
		.driver_data = (kernel_ulong_t)&card_da7219_m98373,
	},
	{
		.name = "sof_da7219_mx98360a",
		.driver_data = (kernel_ulong_t)&card_da7219_m98360a,
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver audio = {
	.probe = audio_probe,
	.driver = {
		.name = "sof_da7219_max98_360a_373",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(audio)

/* Module information */
MODULE_DESCRIPTION("ASoC Intel(R) SOF Machine driver");
MODULE_AUTHOR("Yong Zhi <yong.zhi@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(SND_SOC_INTEL_HDA_DSP_COMMON);
