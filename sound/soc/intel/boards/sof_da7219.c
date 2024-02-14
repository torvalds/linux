// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2019 Intel Corporation.

/*
 * Intel SOF Machine driver for Dialog headphone codec
 */

#include <linux/input.h>
#include <linux/module.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/sof.h>
#include "../../codecs/da7219.h"
#include "hda_dsp_common.h"
#include "sof_hdmi_common.h"
#include "sof_maxim_common.h"
#include "sof_ssp_common.h"

/* Board Quirks */
#define SOF_DA7219_JSL_BOARD			BIT(2)

#define DIALOG_CODEC_DAI	"da7219-hifi"

struct card_private {
	struct snd_soc_jack headset_jack;
	struct sof_hdmi_private hdmi;
	enum sof_ssp_codec codec_type;
	enum sof_ssp_codec amp_type;

	unsigned int pll_bypass:1;
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct card_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *codec_dai;
	int ret = 0;

	if (ctx->pll_bypass)
		return ret;

	/* PLL SRM mode */
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
		dev_dbg(card->dev, "pll srm mode\n");

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
	SOC_DAPM_PIN_SWITCH("Line Out"),
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),

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
	{ "Line Out", NULL, "Platform Clock" },

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
	{
		.pin    = "Line Out",
		.mask   = SND_JACK_LINEOUT,
	},
};

static int da7219_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_soc_jack *jack = &ctx->headset_jack;
	int mclk_rate, ret;

	mclk_rate = sof_dai_get_mclk(rtd);
	if (mclk_rate <= 0) {
		dev_err(rtd->dev, "invalid mclk freq %d\n", mclk_rate);
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, DA7219_CLKSRC_MCLK, mclk_rate,
				     SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(rtd->dev, "fail to set sysclk, ret %d\n", ret);
		return ret;
	}

	/*
	 * Use PLL bypass mode if MCLK is available, be sure to set the
	 * frequency of MCLK to 12.288 or 24.576MHz on topology side.
	 */
	if (mclk_rate == 12288000 || mclk_rate == 24576000) {
		/* PLL bypass mode */
		dev_dbg(rtd->dev, "pll bypass mode, mclk rate %d\n", mclk_rate);

		ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_MCLK, 0, 0);
		if (ret) {
			dev_err(rtd->dev, "fail to set pll, ret %d\n", ret);
			return ret;
		}

		ctx->pll_bypass = 1;
	}

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3 | SND_JACK_LINEOUT,
					 jack, jack_pins, ARRAY_SIZE(jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "fail to set component jack, ret %d\n", ret);
		return ret;
	}

	return ret;
}

static int max98373_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = snd_soc_substream_to_rtd(substream);
	int ret, j;

	for (j = 0; j < runtime->dai_link->num_codecs; j++) {
		struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(runtime, j);

		if (!strcmp(codec_dai->component->name, MAX_98373_DEV0_NAME)) {
			/* vmon_slot_no = 0 imon_slot_no = 1 for TX slots */
			ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x3, 3, 4, 16);
			if (ret < 0) {
				dev_err(runtime->dev, "DEV0 TDM slot err:%d\n", ret);
				return ret;
			}
		}
		if (!strcmp(codec_dai->component->name, MAX_98373_DEV1_NAME)) {
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

static const struct snd_soc_ops max98373_ops = {
	.hw_params = max98373_hw_params,
};

static int hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = snd_soc_rtd_to_codec(rtd, 0);

	ctx->hdmi.hdmi_comp = dai->component;

	return 0;
}

static int card_late_probe(struct snd_soc_card *card)
{
	struct card_private *ctx = snd_soc_card_get_drvdata(card);

	if (!ctx->hdmi.idisp_codec)
		return 0;

	if (!ctx->hdmi.hdmi_comp)
		return -EINVAL;

	return hda_dsp_hdmi_build_controls(card, ctx->hdmi.hdmi_comp);
}

SND_SOC_DAILINK_DEF(ssp0_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP0 Pin")));
SND_SOC_DAILINK_DEF(ssp0_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-DLGS7219:00", DIALOG_CODEC_DAI)));

SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP1 Pin")));

SND_SOC_DAILINK_DEF(ssp2_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP2 Pin")));
SND_SOC_DAILINK_DEF(dummy_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("snd-soc-dummy", "snd-soc-dummy-dai")));

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

SND_SOC_DAILINK_DEF(idisp4_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp4 Pin")));
SND_SOC_DAILINK_DEF(idisp4_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi4")));

SND_SOC_DAILINK_DEF(platform, /* subject to be overridden during probe */
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:1f.3")));

static struct snd_soc_dai_link jsl_dais[] = {
	/* Back End DAI links */
	{
		.name = "SSP1-Codec",
		.id = 0,
		.ignore_pmdown_time = 1,
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1, /* IV feedback */
		SND_SOC_DAILINK_REG(ssp1_pin, max_98373_components, platform),
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

static struct snd_soc_dai_link adl_dais[] = {
	/* Back End DAI links */
	{
		.name = "SSP0-Codec",
		.id = 0,
		.no_pcm = 1,
		.init = da7219_codec_init,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp0_pin, ssp0_codec, platform),
	},
	{
		.name = "dmic01",
		.id = 1,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
	},
	{
		.name = "dmic16k",
		.id = 2,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic16k_pin, dmic_codec, platform),
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
		.name = "iDisp4",
		.id = 6,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp4_pin, idisp4_codec, platform),
	},
	{
		.name = "SSP1-Codec",
		.id = 7,
		.no_pcm = 1,
		.dpcm_playback = 1,
		/* feedback stream or firmware-generated echo reference */
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp1_pin, max_98373_components, platform),
	},
	{
		.name = "SSP2-BT",
		.id = 8,
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp2_pin, dummy_codec, platform),
	},
};

static struct snd_soc_card card_da7219 = {
	.name = "da7219", /* the sof- prefix is added by the core */
	.owner = THIS_MODULE,
	.controls = controls,
	.num_controls = ARRAY_SIZE(controls),
	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.fully_routed = true,
	.late_probe = card_late_probe,
};

static int audio_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach = pdev->dev.platform_data;
	struct snd_soc_dai_link *dai_links;
	struct card_private *ctx;
	unsigned long board_quirk = 0;
	int ret, amp_idx;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		board_quirk = (unsigned long)pdev->id_entry->driver_data;

	ctx->codec_type = sof_ssp_detect_codec_type(&pdev->dev);
	ctx->amp_type = sof_ssp_detect_amp_type(&pdev->dev);

	if (mach->mach_params.codec_mask & IDISP_CODEC_MASK)
		ctx->hdmi.idisp_codec = true;

	if (board_quirk & SOF_DA7219_JSL_BOARD) {
		/* backward-compatible with existing devices */
		switch (ctx->amp_type) {
		case CODEC_MAX98360A:
			card_da7219.name = devm_kstrdup(&pdev->dev,
							"da7219max98360a",
							GFP_KERNEL);
			break;
		case CODEC_MAX98373:
			card_da7219.name = devm_kstrdup(&pdev->dev, "da7219max",
							GFP_KERNEL);
			break;
		default:
			break;
		}

		dai_links = jsl_dais;
		amp_idx = 0;

		card_da7219.num_links = ARRAY_SIZE(jsl_dais);
	} else {
		dai_links = adl_dais;
		amp_idx = 7;

		card_da7219.num_links = ARRAY_SIZE(adl_dais);
	}

	dev_dbg(&pdev->dev, "board_quirk = %lx\n", board_quirk);

	/* speaker amp */
	switch (ctx->amp_type) {
	case CODEC_MAX98360A:
		max_98360a_dai_link(&dai_links[amp_idx]);
		break;
	case CODEC_MAX98373:
		dai_links[amp_idx].codecs = max_98373_components;
		dai_links[amp_idx].num_codecs = ARRAY_SIZE(max_98373_components);
		dai_links[amp_idx].init = max_98373_spk_codec_init;
		if (board_quirk & SOF_DA7219_JSL_BOARD) {
			dai_links[amp_idx].ops = &max98373_ops; /* use local ops */
		} else {
			/* TBD: implement the amp for later platform */
			dev_err(&pdev->dev, "max98373 not support yet\n");
			return -EINVAL;
		}

		max_98373_set_codec_conf(&card_da7219);
		break;
	default:
		dev_err(&pdev->dev, "invalid amp type %d\n", ctx->amp_type);
		return -EINVAL;
	}

	card_da7219.dai_link = dai_links;

	card_da7219.dev = &pdev->dev;

	ret = snd_soc_fixup_dai_links_platform_name(&card_da7219,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&card_da7219, ctx);

	return devm_snd_soc_register_card(&pdev->dev, &card_da7219);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "jsl_mx98373_da7219",
		.driver_data = (kernel_ulong_t)(SOF_DA7219_JSL_BOARD),
	},
	{
		.name = "jsl_mx98360_da7219",
		.driver_data = (kernel_ulong_t)(SOF_DA7219_JSL_BOARD),
	},
	{
		.name = "adl_mx98360_da7219",
		/* no quirk needed for this board */
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver audio = {
	.probe = audio_probe,
	.driver = {
		.name = "sof_da7219",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(audio)

/* Module information */
MODULE_DESCRIPTION("ASoC Intel(R) SOF Machine driver for Dialog codec");
MODULE_AUTHOR("Yong Zhi <yong.zhi@intel.com>");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(SND_SOC_INTEL_HDA_DSP_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_MAXIM_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_SSP_COMMON);
