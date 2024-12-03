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
#include "sof_board_helpers.h"
#include "sof_maxim_common.h"

/* Driver-specific board quirks: from bit 0 to 7 */
#define SOF_DA7219_GLK_BOARD			BIT(0)
#define SOF_DA7219_CML_BOARD			BIT(1)
#define SOF_DA7219_JSL_BOARD			BIT(2)
#define SOF_DA7219_MCLK_EN			BIT(3)

#define DIALOG_CODEC_DAI	"da7219-hifi"

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *codec_dai;
	int ret = 0;

	if (ctx->da7219.pll_bypass)
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
};

static const struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),

	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control, SND_SOC_DAPM_POST_PMD |
			    SND_SOC_DAPM_PRE_PMU),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{ "Headphone Jack", NULL, "HPL" },
	{ "Headphone Jack", NULL, "HPR" },

	{ "MIC", NULL, "Headset Mic" },

	{ "Headphone Jack", NULL, "Platform Clock" },
	{ "Headset Mic", NULL, "Platform Clock" },
	{ "Line Out", NULL, "Platform Clock" },
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
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
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
	if (ctx->da7219.mclk_en &&
	    (mclk_rate == 12288000 || mclk_rate == 24576000)) {
		/* PLL bypass mode */
		dev_dbg(rtd->dev, "pll bypass mode, mclk rate %d\n", mclk_rate);

		ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_MCLK, 0, 0);
		if (ret) {
			dev_err(rtd->dev, "fail to set pll, ret %d\n", ret);
			return ret;
		}

		ctx->da7219.pll_bypass = true;
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

static void da7219_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int card_late_probe(struct snd_soc_card *card)
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

static struct snd_soc_dai_link_component da7219_component[] = {
	{
		.name = "i2c-DLGS7219:00",
		.dai_name = DIALOG_CODEC_DAI,
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
	ctx->codec_link->codecs = da7219_component;
	ctx->codec_link->num_codecs = ARRAY_SIZE(da7219_component);
	ctx->codec_link->init = da7219_codec_init;
	ctx->codec_link->exit = da7219_codec_exit;

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
	case CODEC_MAX98373:
		max_98373_dai_link(dev, ctx->amp_link);
		break;
	case CODEC_MAX98390:
		max_98390_dai_link(dev, ctx->amp_link);
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

#define CML_LINK_ORDER	SOF_LINK_ORDER(SOF_LINK_AMP,         \
					SOF_LINK_CODEC,      \
					SOF_LINK_DMIC01,     \
					SOF_LINK_IDISP_HDMI, \
					SOF_LINK_DMIC16K,    \
					SOF_LINK_NONE,       \
					SOF_LINK_NONE)

#define JSL_LINK_ORDER	SOF_LINK_ORDER(SOF_LINK_AMP,         \
					SOF_LINK_CODEC,      \
					SOF_LINK_DMIC01,     \
					SOF_LINK_IDISP_HDMI, \
					SOF_LINK_DMIC16K,    \
					SOF_LINK_NONE,       \
					SOF_LINK_NONE)

static int audio_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach = pdev->dev.platform_data;
	struct sof_card_private *ctx;
	char *card_name;
	unsigned long board_quirk = 0;
	int ret;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		board_quirk = (unsigned long)pdev->id_entry->driver_data;

	dev_dbg(&pdev->dev, "board_quirk = %lx\n", board_quirk);

	/* initialize ctx with board quirk */
	ctx = sof_intel_board_get_ctx(&pdev->dev, board_quirk);
	if (!ctx)
		return -ENOMEM;

	if (mach->mach_params.codec_mask & IDISP_CODEC_MASK)
		ctx->hdmi.idisp_codec = true;

	if (board_quirk & SOF_DA7219_GLK_BOARD) {
		/* dmic16k not support */
		ctx->dmic_be_num = 1;

		/* overwrite the DAI link order for GLK boards */
		ctx->link_order_overwrite = GLK_LINK_ORDER;

		/* backward-compatible with existing devices */
		switch (ctx->amp_type) {
		case CODEC_MAX98357A:
			card_name = devm_kstrdup(&pdev->dev, "glkda7219max",
						 GFP_KERNEL);
			if (!card_name)
				return -ENOMEM;

			card_da7219.name = card_name;
			break;
		default:
			break;
		}
	} else if (board_quirk & SOF_DA7219_CML_BOARD) {
		/* overwrite the DAI link order for CML boards */
		ctx->link_order_overwrite = CML_LINK_ORDER;

		/* backward-compatible with existing devices */
		switch (ctx->amp_type) {
		case CODEC_MAX98357A:
			card_name = devm_kstrdup(&pdev->dev, "cmlda7219max",
						 GFP_KERNEL);
			if (!card_name)
				return -ENOMEM;

			card_da7219.name = card_name;
			break;
		case CODEC_MAX98390:
			card_name = devm_kstrdup(&pdev->dev,
						 "cml_max98390_da7219",
						 GFP_KERNEL);
			if (!card_name)
				return -ENOMEM;

			card_da7219.name = card_name;
			break;
		default:
			break;
		}
	} else if (board_quirk & SOF_DA7219_JSL_BOARD) {
		/* overwrite the DAI link order for JSL boards */
		ctx->link_order_overwrite = JSL_LINK_ORDER;

		/* backward-compatible with existing devices */
		switch (ctx->amp_type) {
		case CODEC_MAX98360A:
			card_name = devm_kstrdup(&pdev->dev, "da7219max98360a",
						 GFP_KERNEL);
			if (!card_name)
				return -ENOMEM;

			card_da7219.name = card_name;
			break;
		case CODEC_MAX98373:
			card_name = devm_kstrdup(&pdev->dev, "da7219max",
						 GFP_KERNEL);
			if (!card_name)
				return -ENOMEM;

			card_da7219.name = card_name;
			break;
		default:
			break;
		}
	}

	if (board_quirk & SOF_DA7219_MCLK_EN)
		ctx->da7219.mclk_en = true;

	/* update dai_link */
	ret = sof_card_dai_links_create(&pdev->dev, &card_da7219, ctx);
	if (ret)
		return ret;

	/* update codec_conf */
	switch (ctx->amp_type) {
	case CODEC_MAX98373:
		max_98373_set_codec_conf(&card_da7219);
		break;
	case CODEC_MAX98390:
		max_98390_set_codec_conf(&pdev->dev, &card_da7219);
		break;
	case CODEC_MAX98357A:
	case CODEC_MAX98360A:
	case CODEC_NONE:
		/* no codec conf required */
		break;
	default:
		dev_err(&pdev->dev, "invalid amp type %d\n", ctx->amp_type);
		return -EINVAL;
	}

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
		.name = "glk_da7219_def",
		.driver_data = (kernel_ulong_t)(SOF_DA7219_GLK_BOARD |
					SOF_SSP_PORT_CODEC(2) |
					SOF_SSP_PORT_AMP(1)),
	},
	{
		.name = "cml_da7219_def",
		.driver_data = (kernel_ulong_t)(SOF_DA7219_CML_BOARD |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1)),
	},
	{
		.name = "jsl_da7219_def",
		.driver_data = (kernel_ulong_t)(SOF_DA7219_JSL_BOARD |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1)),
	},
	{
		.name = "adl_da7219_def",
		.driver_data = (kernel_ulong_t)(SOF_DA7219_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1) |
					SOF_NUM_IDISP_HDMI(4) |
					SOF_SSP_PORT_BT_OFFLOAD(2) |
					SOF_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "rpl_da7219_def",
		.driver_data = (kernel_ulong_t)(SOF_DA7219_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1) |
					SOF_NUM_IDISP_HDMI(4) |
					SOF_SSP_PORT_BT_OFFLOAD(2) |
					SOF_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "mtl_da7219_def",
		.driver_data = (kernel_ulong_t)(SOF_DA7219_MCLK_EN |
					SOF_SSP_PORT_CODEC(2) |
					SOF_SSP_PORT_AMP(0) |
					SOF_SSP_PORT_BT_OFFLOAD(1) |
					SOF_BT_OFFLOAD_PRESENT),
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
MODULE_IMPORT_NS("SND_SOC_INTEL_SOF_BOARD_HELPERS");
MODULE_IMPORT_NS("SND_SOC_INTEL_SOF_MAXIM_COMMON");
