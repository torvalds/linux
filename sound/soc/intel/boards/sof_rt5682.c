// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2019-2020 Intel Corporation.

/*
 * Intel SOF Machine Driver with Realtek rt5682 Codec
 * and speaker codec MAX98357A or RT1015.
 */
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dmi.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include <sound/rt5682.h>
#include <sound/rt5682s.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt5682.h"
#include "../../codecs/rt5682s.h"
#include "../../codecs/rt5645.h"
#include "../common/soc-intel-quirks.h"
#include "sof_board_helpers.h"
#include "sof_maxim_common.h"
#include "sof_realtek_common.h"

/* Driver-specific board quirks: from bit 0 to 7 */
#define SOF_RT5682_MCLK_EN			BIT(0)

/* Default: MCLK on, MCLK 19.2M, SSP0  */
static unsigned long sof_rt5682_quirk = SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0);

static int sof_rt5682_quirk_cb(const struct dmi_system_id *id)
{
	sof_rt5682_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id sof_rt5682_quirk_table[] = {
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Circuitco"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Minnowboard Max"),
		},
		.driver_data = (void *)(SOF_SSP_PORT_CODEC(2)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_MATCH(DMI_PRODUCT_NAME, "UP-CHT01"),
		},
		.driver_data = (void *)(SOF_SSP_PORT_CODEC(2)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WhiskeyLake Client"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(1)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Volteer"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98373_ALC5682I_I2S_UP4"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(2) |
					SOF_NUM_IDISP_HDMI(4)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alder Lake Client Platform"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-ADL_MAX98373_ALC5682I_I2S"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(2) |
					SOF_NUM_IDISP_HDMI(4)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Brya"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98390_ALC5682I_I2S"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(2) |
					SOF_NUM_IDISP_HDMI(4)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Brya"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98360_ALC5682I_I2S_AMP_SSP2"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(2) |
					SOF_NUM_IDISP_HDMI(4)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Rex"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(2) |
					SOF_SSP_PORT_AMP(0) |
					SOF_SSP_PORT_BT_OFFLOAD(1) |
					SOF_BT_OFFLOAD_PRESENT
					),
	},
	{}
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

static int sof_rt5682_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct snd_soc_jack *jack = &ctx->headset_jack;
	int extra_jack_data;
	int ret, mclk_freq;

	if (ctx->rt5682.mclk_en) {
		mclk_freq = sof_dai_get_mclk(rtd);
		if (mclk_freq <= 0) {
			dev_err(rtd->dev, "invalid mclk freq %d\n", mclk_freq);
			return -EINVAL;
		}

		/* need to enable ASRC function for 24MHz mclk rate */
		if (mclk_freq == 24000000) {
			dev_info(rtd->dev, "enable ASRC\n");

			switch (ctx->codec_type) {
			case CODEC_RT5650:
				rt5645_sel_asrc_clk_src(component,
							RT5645_DA_STEREO_FILTER |
							RT5645_AD_STEREO_FILTER,
							RT5645_CLK_SEL_I2S1_ASRC);
				rt5645_sel_asrc_clk_src(component,
							RT5645_DA_MONO_L_FILTER |
							RT5645_DA_MONO_R_FILTER,
							RT5645_CLK_SEL_I2S2_ASRC);
				break;
			case CODEC_RT5682:
				rt5682_sel_asrc_clk_src(component,
							RT5682_DA_STEREO1_FILTER |
							RT5682_AD_STEREO1_FILTER,
							RT5682_CLK_SEL_I2S1_ASRC);
				break;
			case CODEC_RT5682S:
				rt5682s_sel_asrc_clk_src(component,
							 RT5682S_DA_STEREO1_FILTER |
							 RT5682S_AD_STEREO1_FILTER,
							 RT5682S_CLK_SEL_I2S1_ASRC);
				break;
			default:
				dev_err(rtd->dev, "invalid codec type %d\n",
					ctx->codec_type);
				return -EINVAL;
			}
		}

		if (ctx->rt5682.is_legacy_cpu) {
			/*
			 * The firmware might enable the clock at
			 * boot (this information may or may not
			 * be reflected in the enable clock register).
			 * To change the rate we must disable the clock
			 * first to cover these cases. Due to common
			 * clock framework restrictions that do not allow
			 * to disable a clock that has not been enabled,
			 * we need to enable the clock first.
			 */
			ret = clk_prepare_enable(ctx->rt5682.mclk);
			if (!ret)
				clk_disable_unprepare(ctx->rt5682.mclk);

			ret = clk_set_rate(ctx->rt5682.mclk, 19200000);

			if (ret)
				dev_err(rtd->dev, "unable to set MCLK rate\n");
		}
	}

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

	if (ctx->codec_type == CODEC_RT5650) {
		extra_jack_data = SND_JACK_MICROPHONE | SND_JACK_BTN_0;
		ret = snd_soc_component_set_jack(component, jack, &extra_jack_data);
	} else
		ret = snd_soc_component_set_jack(component, jack, NULL);

	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return ret;
};

static void sof_rt5682_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sof_rt5682_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int pll_id, pll_source, pll_in, pll_out, clk_id, ret;

	if (ctx->rt5682.mclk_en) {
		if (ctx->rt5682.is_legacy_cpu) {
			ret = clk_prepare_enable(ctx->rt5682.mclk);
			if (ret < 0) {
				dev_err(rtd->dev,
					"could not configure MCLK state");
				return ret;
			}
		}

		switch (ctx->codec_type) {
		case CODEC_RT5650:
			pll_source = RT5645_PLL1_S_MCLK;
			break;
		case CODEC_RT5682:
			pll_source = RT5682_PLL1_S_MCLK;
			break;
		case CODEC_RT5682S:
			pll_source = RT5682S_PLL_S_MCLK;
			break;
		default:
			dev_err(rtd->dev, "invalid codec type %d\n",
				ctx->codec_type);
			return -EINVAL;
		}

		/* get the tplg configured mclk. */
		pll_in = sof_dai_get_mclk(rtd);
		if (pll_in <= 0) {
			dev_err(rtd->dev, "invalid mclk freq %d\n", pll_in);
			return -EINVAL;
		}
	} else {
		switch (ctx->codec_type) {
		case CODEC_RT5650:
			pll_source = RT5645_PLL1_S_BCLK1;
			break;
		case CODEC_RT5682:
			pll_source = RT5682_PLL1_S_BCLK1;
			break;
		case CODEC_RT5682S:
			pll_source = RT5682S_PLL_S_BCLK1;
			break;
		default:
			dev_err(rtd->dev, "invalid codec type %d\n",
				ctx->codec_type);
			return -EINVAL;
		}

		/* get the tplg configured bclk. */
		pll_in = sof_dai_get_bclk(rtd);
		if (pll_in <= 0) {
			dev_err(rtd->dev, "invalid bclk freq %d\n", pll_in);
			return -EINVAL;
		}
	}

	pll_out = params_rate(params) * 512;

	/* when MCLK is 512FS, no need to set PLL configuration additionally. */
	if (pll_in == pll_out) {
		switch (ctx->codec_type) {
		case CODEC_RT5650:
			clk_id = RT5645_SCLK_S_MCLK;
			break;
		case CODEC_RT5682:
			clk_id = RT5682_SCLK_S_MCLK;
			break;
		case CODEC_RT5682S:
			clk_id = RT5682S_SCLK_S_MCLK;
			break;
		default:
			dev_err(rtd->dev, "invalid codec type %d\n",
				ctx->codec_type);
			return -EINVAL;
		}
	} else {
		switch (ctx->codec_type) {
		case CODEC_RT5650:
			pll_id = 0; /* not used in codec driver */
			clk_id = RT5645_SCLK_S_PLL1;
			break;
		case CODEC_RT5682:
			pll_id = RT5682_PLL1;
			clk_id = RT5682_SCLK_S_PLL1;
			break;
		case CODEC_RT5682S:
			/* check plla_table and pllb_table in rt5682s.c */
			switch (pll_in) {
			case 3072000:
			case 24576000:
				/*
				 * For MCLK = 24.576MHz and sample rate = 96KHz case, use PLL1  We don't test
				 * pll_out or params_rate() here since rt5682s PLL2 doesn't support 24.576MHz
				 * input, so we have no choice but to use PLL1. Besides, we will not use PLL at
				 * all if pll_in == pll_out. ex, MCLK = 24.576Mhz and sample rate = 48KHz
				 */
				pll_id = RT5682S_PLL1;
				clk_id = RT5682S_SCLK_S_PLL1;
				break;
			default:
				pll_id = RT5682S_PLL2;
				clk_id = RT5682S_SCLK_S_PLL2;
				break;
			}
			break;
		default:
			dev_err(rtd->dev, "invalid codec type %d\n", ctx->codec_type);
			return -EINVAL;
		}

		/* Configure pll for codec */
		ret = snd_soc_dai_set_pll(codec_dai, pll_id, pll_source, pll_in,
					  pll_out);
		if (ret < 0)
			dev_err(rtd->dev, "snd_soc_dai_set_pll err = %d\n", ret);
	}

	/* Configure sysclk for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, clk_id,
				     pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);

	/*
	 * slot_width should equal or large than data length, set them
	 * be the same
	 */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x0, 0x0, 2,
				       params_width(params));
	if (ret < 0) {
		dev_err(rtd->dev, "set TDM slot err:%d\n", ret);
		return ret;
	}

	return ret;
}

static const struct snd_soc_ops sof_rt5682_ops = {
	.hw_params = sof_rt5682_hw_params,
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
};

static const struct snd_soc_dapm_widget sof_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route sof_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },

	/* other jacks */
	{ "IN1P", NULL, "Headset Mic" },
};

static const struct snd_kcontrol_new rt5650_spk_kcontrols[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),

};

static const struct snd_soc_dapm_widget rt5650_spk_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_soc_dapm_route rt5650_spk_dapm_routes[] = {
	/* speaker */
	{ "Left Spk", NULL, "SPOL" },
	{ "Right Spk", NULL, "SPOR" },
};

static int rt5650_spk_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, rt5650_spk_widgets,
					ARRAY_SIZE(rt5650_spk_widgets));
	if (ret) {
		dev_err(rtd->dev, "fail to add rt5650 spk widgets, ret %d\n",
			ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, rt5650_spk_kcontrols,
					ARRAY_SIZE(rt5650_spk_kcontrols));
	if (ret) {
		dev_err(rtd->dev, "fail to add rt5650 spk kcontrols, ret %d\n",
			ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, rt5650_spk_dapm_routes,
				      ARRAY_SIZE(rt5650_spk_dapm_routes));
	if (ret)
		dev_err(rtd->dev, "fail to add dapm routes, ret=%d\n", ret);

	return ret;
}

/* sof audio machine driver for rt5682 codec */
static struct snd_soc_card sof_audio_card_rt5682 = {
	.name = "rt5682", /* the sof- prefix is added by the core */
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

static struct snd_soc_dai_link_component rt5682_component[] = {
	{
		.name = "i2c-10EC5682:00",
		.dai_name = "rt5682-aif1",
	}
};

static struct snd_soc_dai_link_component rt5682s_component[] = {
	{
		.name = "i2c-RTL5682:00",
		.dai_name = "rt5682s-aif1",
	}
};

static struct snd_soc_dai_link_component rt5650_components[] = {
	{
		.name = "i2c-10EC5650:00",
		.dai_name = "rt5645-aif1",
	},
	{
		.name = "i2c-10EC5650:00",
		.dai_name = "rt5645-aif2",
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
	switch (ctx->codec_type) {
	case CODEC_RT5650:
		ctx->codec_link->codecs = &rt5650_components[0];
		ctx->codec_link->num_codecs = 1;
		break;
	case CODEC_RT5682:
		ctx->codec_link->codecs = rt5682_component;
		ctx->codec_link->num_codecs = ARRAY_SIZE(rt5682_component);
		break;
	case CODEC_RT5682S:
		ctx->codec_link->codecs = rt5682s_component;
		ctx->codec_link->num_codecs = ARRAY_SIZE(rt5682s_component);
		break;
	default:
		dev_err(dev, "invalid codec type %d\n", ctx->codec_type);
		return -EINVAL;
	}

	ctx->codec_link->init = sof_rt5682_codec_init;
	ctx->codec_link->exit = sof_rt5682_codec_exit;
	ctx->codec_link->ops = &sof_rt5682_ops;

	if (!ctx->rt5682.is_legacy_cpu) {
		/*
		 * Currently, On SKL+ platforms MCLK will be turned off in sof
		 * runtime suspended, and it will go into runtime suspended
		 * right after playback is stop. However, rt5682 will output
		 * static noise if sysclk turns off during playback. Set
		 * ignore_pmdown_time to power down rt5682 immediately and
		 * avoid the noise.
		 * It can be removed once we can control MCLK by driver.
		 */
		ctx->codec_link->ignore_pmdown_time = 1;
	}

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
	case CODEC_RT1011:
		sof_rt1011_dai_link(dev, ctx->amp_link);
		break;
	case CODEC_RT1015:
		sof_rt1015_dai_link(ctx->amp_link);
		break;
	case CODEC_RT1015P:
		sof_rt1015p_dai_link(ctx->amp_link);
		break;
	case CODEC_RT1019P:
		sof_rt1019p_dai_link(ctx->amp_link);
		break;
	case CODEC_RT5650:
		/* use AIF2 to support speaker pipeline */
		ctx->amp_link->codecs = &rt5650_components[1];
		ctx->amp_link->num_codecs = 1;
		ctx->amp_link->init = rt5650_spk_init;
		ctx->amp_link->ops = &sof_rt5682_ops;
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
	char *card_name;
	int ret;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		sof_rt5682_quirk = (unsigned long)pdev->id_entry->driver_data;

	dmi_check_system(sof_rt5682_quirk_table);

	dev_dbg(&pdev->dev, "sof_rt5682_quirk = %lx\n", sof_rt5682_quirk);

	/* initialize ctx with board quirk */
	ctx = sof_intel_board_get_ctx(&pdev->dev, sof_rt5682_quirk);
	if (!ctx)
		return -ENOMEM;

	if (ctx->codec_type == CODEC_RT5650) {
		card_name = devm_kstrdup(&pdev->dev, "rt5650", GFP_KERNEL);
		if (!card_name)
			return -ENOMEM;

		sof_audio_card_rt5682.name = card_name;

		/* create speaker dai link also */
		if (ctx->amp_type == CODEC_NONE)
			ctx->amp_type = CODEC_RT5650;
	}

	if (mach->mach_params.codec_mask & IDISP_CODEC_MASK)
		ctx->hdmi.idisp_codec = true;

	if (soc_intel_is_byt() || soc_intel_is_cht()) {
		ctx->rt5682.is_legacy_cpu = true;
		ctx->dmic_be_num = 0;
		/* HDMI is not supported by SOF on Baytrail/CherryTrail */
		ctx->hdmi_num = 0;
	} else if (soc_intel_is_glk()) {
		/* dmic16k not support */
		ctx->dmic_be_num = 1;

		/* overwrite the DAI link order for GLK boards */
		ctx->link_order_overwrite = GLK_LINK_ORDER;

		/* backward-compatible with existing devices */
		switch (ctx->amp_type) {
		case CODEC_MAX98357A:
			card_name = devm_kstrdup(&pdev->dev, "glkrt5682max",
						 GFP_KERNEL);
			if (!card_name)
				return -ENOMEM;

			sof_audio_card_rt5682.name = card_name;
			break;
		default:
			break;
		}
	} else if (soc_intel_is_cml()) {
		/* backward-compatible with existing devices */
		switch (ctx->amp_type) {
		case CODEC_RT1011:
			card_name = devm_kstrdup(&pdev->dev, "cml_rt1011_rt5682",
						 GFP_KERNEL);
			if (!card_name)
				return -ENOMEM;

			sof_audio_card_rt5682.name = card_name;
			break;
		default:
			break;
		}
	}

	if (sof_rt5682_quirk & SOF_RT5682_MCLK_EN) {
		ctx->rt5682.mclk_en = true;

		/* need to get main clock from pmc */
		if (ctx->rt5682.is_legacy_cpu) {
			ctx->rt5682.mclk = devm_clk_get(&pdev->dev, "pmc_plt_clk_3");
			if (IS_ERR(ctx->rt5682.mclk)) {
				ret = PTR_ERR(ctx->rt5682.mclk);

				dev_err(&pdev->dev,
					"Failed to get MCLK from pmc_plt_clk_3: %d\n",
					ret);
				return ret;
			}

			ret = clk_prepare_enable(ctx->rt5682.mclk);
			if (ret < 0) {
				dev_err(&pdev->dev,
					"could not configure MCLK state");
				return ret;
			}
		}
	}

	/* update dai_link */
	ret = sof_card_dai_links_create(&pdev->dev, &sof_audio_card_rt5682, ctx);
	if (ret)
		return ret;

	/* update codec_conf */
	switch (ctx->amp_type) {
	case CODEC_MAX98373:
		max_98373_set_codec_conf(&sof_audio_card_rt5682);
		break;
	case CODEC_MAX98390:
		max_98390_set_codec_conf(&pdev->dev, &sof_audio_card_rt5682);
		break;
	case CODEC_RT1011:
		sof_rt1011_codec_conf(&pdev->dev, &sof_audio_card_rt5682);
		break;
	case CODEC_RT1015:
		sof_rt1015_codec_conf(&sof_audio_card_rt5682);
		break;
	case CODEC_RT1015P:
		sof_rt1015p_codec_conf(&sof_audio_card_rt5682);
		break;
	case CODEC_MAX98357A:
	case CODEC_MAX98360A:
	case CODEC_RT1019P:
	case CODEC_RT5650:
	case CODEC_NONE:
		/* no codec conf required */
		break;
	default:
		dev_err(&pdev->dev, "invalid amp type %d\n", ctx->amp_type);
		return -EINVAL;
	}

	sof_audio_card_rt5682.dev = &pdev->dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_audio_card_rt5682,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&sof_audio_card_rt5682, ctx);

	return devm_snd_soc_register_card(&pdev->dev,
					  &sof_audio_card_rt5682);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "sof_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(2)),
	},
	{
		.name = "glk_rt5682_def",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(2) |
					SOF_SSP_PORT_AMP(1)),
	},
	{
		.name = "icl_rt5682_def",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0)),
	},
	{
		.name = "cml_rt5682_def",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1)),
	},
	{
		.name = "jsl_rt5682_def",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1)),
	},
	{
		.name = "tgl_rt5682_def",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1) |
					SOF_NUM_IDISP_HDMI(4) |
					SOF_SSP_PORT_BT_OFFLOAD(2) |
					SOF_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_rt5682_def",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1) |
					SOF_NUM_IDISP_HDMI(4) |
					SOF_SSP_PORT_BT_OFFLOAD(2) |
					SOF_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_mx98357_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(2) |
					SOF_NUM_IDISP_HDMI(4)),
	},
	{
		.name = "adl_rt5682_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(1) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_SSP_MASK_HDMI_CAPTURE(0x5)),
	},
	{
		.name = "rpl_mx98357_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(2) |
					SOF_NUM_IDISP_HDMI(4)),
	},
	{
		.name = "rpl_rt5682_def",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1) |
					SOF_NUM_IDISP_HDMI(4) |
					SOF_SSP_PORT_BT_OFFLOAD(2) |
					SOF_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "rpl_rt5682_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(1) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_SSP_MASK_HDMI_CAPTURE(0x5)),
	},
	{
		.name = "mtl_rt5682_def",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1) |
					SOF_SSP_PORT_BT_OFFLOAD(2) |
					SOF_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "mtl_rt5682_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(1) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_SSP_MASK_HDMI_CAPTURE(0x5)),
	},
	{
		.name = "arl_rt5682_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(1) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_SSP_MASK_HDMI_CAPTURE(0x5)),
	},
	{
		.name = "ptl_rt5682_def",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(0) |
					SOF_SSP_PORT_AMP(1) |
					SOF_SSP_PORT_BT_OFFLOAD(2) |
					SOF_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "ptl_rt5682_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_SSP_PORT_CODEC(1) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_SSP_MASK_HDMI_CAPTURE(0x5)),
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver sof_audio = {
	.probe = sof_audio_probe,
	.driver = {
		.name = "sof_rt5682",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(sof_audio)

/* Module information */
MODULE_DESCRIPTION("SOF Audio Machine driver");
MODULE_AUTHOR("Bard Liao <bard.liao@intel.com>");
MODULE_AUTHOR("Sathya Prakash M R <sathya.prakash.m.r@intel.com>");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_AUTHOR("Mac Chiang <mac.chiang@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("SND_SOC_INTEL_SOF_BOARD_HELPERS");
MODULE_IMPORT_NS("SND_SOC_INTEL_SOF_MAXIM_COMMON");
MODULE_IMPORT_NS("SND_SOC_INTEL_SOF_REALTEK_COMMON");
