// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2021 Intel Corporation.
// Copyright(c) 2021 Nuvoton Corporation.

/*
 * Intel SOF Machine Driver with Nuvoton headphone codec NAU8825
 * and speaker codec RT1019P MAX98360a or MAX98373
 */
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include <sound/soc-acpi.h>
#include "../../codecs/nau8825.h"
#include "../common/soc-intel-quirks.h"
#include "sof_board_helpers.h"
#include "sof_realtek_common.h"
#include "sof_maxim_common.h"
#include "sof_nuvoton_common.h"
#include "sof_ssp_common.h"

#define SOF_NAU8825_SSP_CODEC(quirk)		((quirk) & GENMASK(2, 0))
#define SOF_NAU8825_SSP_CODEC_MASK		(GENMASK(2, 0))
#define SOF_NAU8825_SSP_AMP_SHIFT		4
#define SOF_NAU8825_SSP_AMP_MASK		(GENMASK(6, 4))
#define SOF_NAU8825_SSP_AMP(quirk)	\
	(((quirk) << SOF_NAU8825_SSP_AMP_SHIFT) & SOF_NAU8825_SSP_AMP_MASK)
#define SOF_NAU8825_NUM_HDMIDEV_SHIFT		7
#define SOF_NAU8825_NUM_HDMIDEV_MASK		(GENMASK(9, 7))
#define SOF_NAU8825_NUM_HDMIDEV(quirk)	\
	(((quirk) << SOF_NAU8825_NUM_HDMIDEV_SHIFT) & SOF_NAU8825_NUM_HDMIDEV_MASK)

/* BT audio offload: reserve 3 bits for future */
#define SOF_BT_OFFLOAD_SSP_SHIFT		10
#define SOF_BT_OFFLOAD_SSP_MASK		(GENMASK(12, 10))
#define SOF_BT_OFFLOAD_SSP(quirk)	\
	(((quirk) << SOF_BT_OFFLOAD_SSP_SHIFT) & SOF_BT_OFFLOAD_SSP_MASK)
#define SOF_SSP_BT_OFFLOAD_PRESENT		BIT(13)

static unsigned long sof_nau8825_quirk = SOF_NAU8825_SSP_CODEC(0);

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

static int sof_nau8825_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct snd_soc_jack *jack = &ctx->headset_jack;
	int ret;

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3,
					 jack,
					 jack_pins,
					 ARRAY_SIZE(jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

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

static void sof_nau8825_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sof_nau8825_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int clk_freq, ret;

	clk_freq = sof_dai_get_bclk(rtd); /* BCLK freq */

	if (clk_freq <= 0) {
		dev_err(rtd->dev, "get bclk freq failed: %d\n", clk_freq);
		return -EINVAL;
	}

	/* Configure clock for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, NAU8825_CLK_FLL_BLK, 0,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set BCLK clock %d\n", ret);
		return ret;
	}

	/* Configure pll for codec */
	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, clk_freq,
				  params_rate(params) * 256);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set BCLK: %d\n", ret);
		return ret;
	}

	return ret;
}

static struct snd_soc_ops sof_nau8825_ops = {
	.hw_params = sof_nau8825_hw_params,
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

static int sof_card_late_probe(struct snd_soc_card *card)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_dapm_context *dapm = &card->dapm;
	int err;

	if (ctx->amp_type == CODEC_MAX98373) {
		/* Disable Left and Right Spk pin after boot */
		snd_soc_dapm_disable_pin(dapm, "Left Spk");
		snd_soc_dapm_disable_pin(dapm, "Right Spk");
		err = snd_soc_dapm_sync(dapm);
		if (err < 0)
			return err;
	}

	return sof_intel_board_card_late_probe(card);
}

static const struct snd_kcontrol_new sof_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget sof_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_soc_dapm_route sof_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },

	/* other jacks */
	{ "MIC", NULL, "Headset Mic" },
};

/* sof audio machine driver for nau8825 codec */
static struct snd_soc_card sof_audio_card_nau8825 = {
	.name = "nau8825", /* the sof- prefix is added by the core */
	.owner = THIS_MODULE,
	.controls = sof_controls,
	.num_controls = ARRAY_SIZE(sof_controls),
	.dapm_widgets = sof_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sof_widgets),
	.dapm_routes = sof_map,
	.num_dapm_routes = ARRAY_SIZE(sof_map),
	.fully_routed = true,
	.late_probe = sof_card_late_probe,
};

static struct snd_soc_dai_link_component nau8825_component[] = {
	{
		.name = "i2c-10508825:00",
		.dai_name = "nau8825-hifi",
	}
};

static struct snd_soc_dai_link *
sof_card_dai_links_create(struct device *dev, enum sof_ssp_codec amp_type,
			  int ssp_codec, int ssp_amp, int dmic_be_num,
			  int hdmi_num, bool idisp_codec)
{
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	int i;
	int id = 0;
	int ret;

	links = devm_kcalloc(dev, sof_audio_card_nau8825.num_links,
			    sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	cpus = devm_kcalloc(dev, sof_audio_card_nau8825.num_links,
			    sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
	if (!links || !cpus)
		goto devm_err;

	/* codec SSP */
	links[id].name = devm_kasprintf(dev, GFP_KERNEL,
					"SSP%d-Codec", ssp_codec);
	if (!links[id].name)
		goto devm_err;

	links[id].id = id;
	links[id].codecs = nau8825_component;
	links[id].num_codecs = ARRAY_SIZE(nau8825_component);
	links[id].platforms = platform_component;
	links[id].num_platforms = ARRAY_SIZE(platform_component);
	links[id].init = sof_nau8825_codec_init;
	links[id].exit = sof_nau8825_codec_exit;
	links[id].ops = &sof_nau8825_ops;
	links[id].dpcm_playback = 1;
	links[id].dpcm_capture = 1;
	links[id].no_pcm = 1;
	links[id].cpus = &cpus[id];
	links[id].num_cpus = 1;

	links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
						  "SSP%d Pin",
						  ssp_codec);
	if (!links[id].cpus->dai_name)
		goto devm_err;

	id++;

	/* dmic */
	if (dmic_be_num > 0) {
		/* at least we have dmic01 */
		ret = sof_intel_board_set_dmic_link(dev, &links[id], id,
						    SOF_DMIC_01);
		if (ret)
			return NULL;

		id++;
	}

	if (dmic_be_num > 1) {
		/* set up 2 BE links at most */
		ret = sof_intel_board_set_dmic_link(dev, &links[id], id,
						    SOF_DMIC_16K);
		if (ret)
			return NULL;

		id++;
	}

	/* HDMI */
	for (i = 1; i <= hdmi_num; i++) {
		ret = sof_intel_board_set_intel_hdmi_link(dev, &links[id], id,
							  i, idisp_codec);
		if (ret)
			return NULL;

		id++;
	}

	/* speaker amp */
	if (amp_type != CODEC_NONE) {
		links[id].name = devm_kasprintf(dev, GFP_KERNEL,
						"SSP%d-Codec", ssp_amp);
		if (!links[id].name)
			goto devm_err;

		links[id].id = id;

		switch (amp_type) {
		case CODEC_MAX98360A:
			max_98360a_dai_link(&links[id]);
			break;
		case CODEC_MAX98373:
			links[id].codecs = max_98373_components;
			links[id].num_codecs = ARRAY_SIZE(max_98373_components);
			links[id].init = max_98373_spk_codec_init;
			links[id].ops = &max_98373_ops;
			break;
		case CODEC_NAU8318:
			nau8318_set_dai_link(&links[id]);
			break;
		case CODEC_RT1015P:
			sof_rt1015p_dai_link(&links[id]);
			break;
		case CODEC_RT1019P:
			sof_rt1019p_dai_link(&links[id]);
			break;
		default:
			dev_err(dev, "invalid amp type %d\n", amp_type);
			return NULL;
		}

		links[id].platforms = platform_component;
		links[id].num_platforms = ARRAY_SIZE(platform_component);
		links[id].dpcm_playback = 1;
		/* feedback stream or firmware-generated echo reference */
		links[id].dpcm_capture = 1;

		links[id].no_pcm = 1;
		links[id].cpus = &cpus[id];
		links[id].num_cpus = 1;
		links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
							  "SSP%d Pin",
							  ssp_amp);
		if (!links[id].cpus->dai_name)
			goto devm_err;
		id++;
	}

	/* BT audio offload */
	if (sof_nau8825_quirk & SOF_SSP_BT_OFFLOAD_PRESENT) {
		int port = (sof_nau8825_quirk & SOF_BT_OFFLOAD_SSP_MASK) >>
				SOF_BT_OFFLOAD_SSP_SHIFT;

		links[id].id = id;
		links[id].cpus = &cpus[id];
		links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
							  "SSP%d Pin", port);
		if (!links[id].cpus->dai_name)
			goto devm_err;
		links[id].name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-BT", port);
		if (!links[id].name)
			goto devm_err;
		links[id].codecs = &snd_soc_dummy_dlc;
		links[id].num_codecs = 1;
		links[id].platforms = platform_component;
		links[id].num_platforms = ARRAY_SIZE(platform_component);
		links[id].dpcm_playback = 1;
		links[id].dpcm_capture = 1;
		links[id].no_pcm = 1;
		links[id].num_cpus = 1;
	}

	return links;
devm_err:
	return NULL;
}

static int sof_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach = pdev->dev.platform_data;
	struct snd_soc_dai_link *dai_links;
	struct sof_card_private *ctx;
	int ret, ssp_amp, ssp_codec;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		sof_nau8825_quirk = (unsigned long)pdev->id_entry->driver_data;

	ctx->codec_type = sof_ssp_detect_codec_type(&pdev->dev);
	ctx->amp_type = sof_ssp_detect_amp_type(&pdev->dev);

	dev_dbg(&pdev->dev, "sof_nau8825_quirk = %lx\n", sof_nau8825_quirk);

	/* default number of DMIC DAI's */
	ctx->dmic_be_num = 2;
	ctx->hdmi_num = (sof_nau8825_quirk & SOF_NAU8825_NUM_HDMIDEV_MASK) >>
			SOF_NAU8825_NUM_HDMIDEV_SHIFT;
	/* default number of HDMI DAI's */
	if (!ctx->hdmi_num)
		ctx->hdmi_num = 3;

	if (mach->mach_params.codec_mask & IDISP_CODEC_MASK)
		ctx->hdmi.idisp_codec = true;

	ssp_amp = (sof_nau8825_quirk & SOF_NAU8825_SSP_AMP_MASK) >>
			SOF_NAU8825_SSP_AMP_SHIFT;

	ssp_codec = sof_nau8825_quirk & SOF_NAU8825_SSP_CODEC_MASK;

	/* compute number of dai links */
	sof_audio_card_nau8825.num_links = 1 + ctx->dmic_be_num + ctx->hdmi_num;

	if (ctx->amp_type != CODEC_NONE)
		sof_audio_card_nau8825.num_links++;

	if (sof_nau8825_quirk & SOF_SSP_BT_OFFLOAD_PRESENT)
		sof_audio_card_nau8825.num_links++;

	dai_links = sof_card_dai_links_create(&pdev->dev, ctx->amp_type,
					      ssp_codec, ssp_amp,
					      ctx->dmic_be_num, ctx->hdmi_num,
					      ctx->hdmi.idisp_codec);
	if (!dai_links)
		return -ENOMEM;

	sof_audio_card_nau8825.dai_link = dai_links;

	/* update codec_conf */
	switch (ctx->amp_type) {
	case CODEC_MAX98373:
		max_98373_set_codec_conf(&sof_audio_card_nau8825);
		break;
	case CODEC_RT1015P:
		sof_rt1015p_codec_conf(&sof_audio_card_nau8825);
		break;
	case CODEC_NONE:
	case CODEC_MAX98360A:
	case CODEC_NAU8318:
	case CODEC_RT1019P:
		/* no codec conf required */
		break;
	default:
		dev_err(&pdev->dev, "invalid amp type %d\n", ctx->amp_type);
		return -EINVAL;
	}

	sof_audio_card_nau8825.dev = &pdev->dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_audio_card_nau8825,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&sof_audio_card_nau8825, ctx);

	return devm_snd_soc_register_card(&pdev->dev,
					  &sof_audio_card_nau8825);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "sof_nau8825",
		.driver_data = (kernel_ulong_t)(SOF_NAU8825_SSP_CODEC(0) |
					SOF_NAU8825_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),

	},
	{
		.name = "adl_rt1019p_8825",
		.driver_data = (kernel_ulong_t)(SOF_NAU8825_SSP_CODEC(0) |
					SOF_NAU8825_SSP_AMP(2) |
					SOF_NAU8825_NUM_HDMIDEV(4)),
	},
	{
		.name = "adl_max98373_8825",
		.driver_data = (kernel_ulong_t)(SOF_NAU8825_SSP_CODEC(0) |
					SOF_NAU8825_SSP_AMP(1) |
					SOF_NAU8825_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		/* The limitation of length of char array, shorten the name */
		.name = "adl_mx98360a_8825",
		.driver_data = (kernel_ulong_t)(SOF_NAU8825_SSP_CODEC(0) |
					SOF_NAU8825_SSP_AMP(1) |
					SOF_NAU8825_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),

	},
	{
		.name = "adl_rt1015p_8825",
		.driver_data = (kernel_ulong_t)(SOF_NAU8825_SSP_CODEC(0) |
					SOF_NAU8825_SSP_AMP(1) |
					SOF_NAU8825_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_nau8318_8825",
		.driver_data = (kernel_ulong_t)(SOF_NAU8825_SSP_CODEC(0) |
					SOF_NAU8825_SSP_AMP(1) |
					SOF_NAU8825_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "rpl_max98373_8825",
		.driver_data = (kernel_ulong_t)(SOF_NAU8825_SSP_CODEC(0) |
					SOF_NAU8825_SSP_AMP(1) |
					SOF_NAU8825_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "rpl_mx98360a_8825",
		.driver_data = (kernel_ulong_t)(SOF_NAU8825_SSP_CODEC(0) |
					SOF_NAU8825_SSP_AMP(1) |
					SOF_NAU8825_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "rpl_nau8318_8825",
		.driver_data = (kernel_ulong_t)(SOF_NAU8825_SSP_CODEC(0) |
					SOF_NAU8825_SSP_AMP(1) |
					SOF_NAU8825_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver sof_audio = {
	.probe = sof_audio_probe,
	.driver = {
		.name = "sof_nau8825",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(sof_audio)

/* Module information */
MODULE_DESCRIPTION("SOF Audio Machine driver for NAU8825");
MODULE_AUTHOR("David Lin <ctlin0@nuvoton.com>");
MODULE_AUTHOR("Mac Chiang <mac.chiang@intel.com>");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_BOARD_HELPERS);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_MAXIM_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_NUVOTON_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_REALTEK_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_SSP_COMMON);
