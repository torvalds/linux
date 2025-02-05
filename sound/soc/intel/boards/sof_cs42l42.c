// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2021 Intel Corporation.

/*
 * Intel SOF Machine Driver with Cirrus Logic CS42L42 Codec
 * and speaker codec MAX98357A
 */
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/dmi.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include <sound/soc-acpi.h>
#include <dt-bindings/sound/cs42l42.h>
#include "../common/soc-intel-quirks.h"
#include "sof_board_helpers.h"
#include "sof_maxim_common.h"

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

/* Default: SSP2 */
static unsigned long sof_cs42l42_quirk = SOF_SSP_PORT_CODEC(2);

static int sof_cs42l42_init(struct snd_soc_pcm_runtime *rtd)
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
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return ret;
};

static void sof_cs42l42_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sof_cs42l42_hw_params(struct snd_pcm_substream *substream,
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

	/* Configure sysclk for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
				     clk_freq, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);

	return ret;
}

static const struct snd_soc_ops sof_cs42l42_ops = {
	.hw_params = sof_cs42l42_hw_params,
};

static int sof_card_late_probe(struct snd_soc_card *card)
{
	return sof_intel_board_card_late_probe(card);
}

static const struct snd_kcontrol_new sof_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget sof_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route sof_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{"Headphone Jack", NULL, "HP"},

	/* other jacks */
	{"HS", NULL, "Headset Mic"},
};

/* sof audio machine driver for cs42l42 codec */
static struct snd_soc_card sof_audio_card_cs42l42 = {
	.name = "cs42l42", /* the sof- prefix is added by the core */
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

static struct snd_soc_dai_link_component cs42l42_component[] = {
	{
		.name = "i2c-10134242:00",
		.dai_name = "cs42l42",
	}
};

static int
sof_card_dai_links_create(struct device *dev, struct snd_soc_card *card,
			  struct sof_card_private *ctx)
{
	int ret;

	ret = sof_intel_board_set_dai_link(dev, card, ctx);
	if (ret)
		return ret;

	if (!ctx->codec_link) {
		dev_err(dev, "codec link not available");
		return -EINVAL;
	}

	/* codec-specific fields for headphone codec */
	ctx->codec_link->codecs = cs42l42_component;
	ctx->codec_link->num_codecs = ARRAY_SIZE(cs42l42_component);
	ctx->codec_link->init = sof_cs42l42_init;
	ctx->codec_link->exit = sof_cs42l42_exit;
	ctx->codec_link->ops = &sof_cs42l42_ops;

	if (ctx->amp_type == CODEC_NONE)
		return 0;

	if (!ctx->amp_link) {
		dev_err(dev, "amp link not available");
		return -EINVAL;
	}

	/* codec-specific fields for speaker amplifier */
	switch (ctx->amp_type) {
	case CODEC_MAX98357A:
		max_98357a_dai_link(ctx->amp_link);
		break;
	case CODEC_MAX98360A:
		max_98360a_dai_link(ctx->amp_link);
		break;
	default:
		dev_err(dev, "invalid amp type %d\n", ctx->amp_type);
		return -EINVAL;
	}

	return 0;
}

#define GLK_LINK_ORDER	SOF_LINK_ORDER(SOF_LINK_AMP,         \
					SOF_LINK_CODEC,      \
					SOF_LINK_DMIC01,     \
					SOF_LINK_IDISP_HDMI, \
					SOF_LINK_NONE,       \
					SOF_LINK_NONE,       \
					SOF_LINK_NONE)

static int sof_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach = pdev->dev.platform_data;
	struct sof_card_private *ctx;
	int ret;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		sof_cs42l42_quirk = (unsigned long)pdev->id_entry->driver_data;

	dev_dbg(&pdev->dev, "sof_cs42l42_quirk = %lx\n", sof_cs42l42_quirk);

	/* initialize ctx with board quirk */
	ctx = sof_intel_board_get_ctx(&pdev->dev, sof_cs42l42_quirk);
	if (!ctx)
		return -ENOMEM;

	if (soc_intel_is_glk()) {
		ctx->dmic_be_num = 1;

		/* overwrite the DAI link order for GLK boards */
		ctx->link_order_overwrite = GLK_LINK_ORDER;
	}

	if (mach->mach_params.codec_mask & IDISP_CODEC_MASK)
		ctx->hdmi.idisp_codec = true;

	/* update dai_link */
	ret = sof_card_dai_links_create(&pdev->dev, &sof_audio_card_cs42l42, ctx);
	if (ret)
		return ret;

	sof_audio_card_cs42l42.dev = &pdev->dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_audio_card_cs42l42,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&sof_audio_card_cs42l42, ctx);

	return devm_snd_soc_register_card(&pdev->dev,
					  &sof_audio_card_cs42l42);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "glk_cs4242_mx98357a",
		.driver_data = (kernel_ulong_t)(SOF_SSP_PORT_CODEC(2) |
					SOF_SSP_PORT_AMP(1)),
	},
	{
		.name = "jsl_cs4242_mx98360a",
		.driver_data = (kernel_ulong_t)(SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1)),
	},
	{
		.name = "adl_cs42l42_def",
		.driver_data = (kernel_ulong_t)(SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1) |
					SOF_NUM_IDISP_HDMI(4) |
					SOF_BT_OFFLOAD_PRESENT |
					SOF_SSP_PORT_BT_OFFLOAD(2)),
	},
	{
		.name = "rpl_cs42l42_def",
		.driver_data = (kernel_ulong_t)(SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1) |
					SOF_NUM_IDISP_HDMI(4) |
					SOF_BT_OFFLOAD_PRESENT |
					SOF_SSP_PORT_BT_OFFLOAD(2)),
	},
	{
		.name = "mtl_cs42l42_def",
		.driver_data = (kernel_ulong_t)(SOF_SSP_PORT_CODEC(2) |
					SOF_SSP_PORT_AMP(0) |
					SOF_BT_OFFLOAD_PRESENT |
					SOF_SSP_PORT_BT_OFFLOAD(1)),
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver sof_audio = {
	.probe = sof_audio_probe,
	.driver = {
		.name = "sof_cs42l42",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(sof_audio)

/* Module information */
MODULE_DESCRIPTION("SOF Audio Machine driver for CS42L42");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_INTEL_SOF_BOARD_HELPERS");
MODULE_IMPORT_NS("SND_SOC_INTEL_SOF_MAXIM_COMMON");
