/*
 *  bytcr_rt5651.c - ASoc Machine driver for Intel Byt CR platform
 *  (derived from bytcr_rt5640.c)
 *
 *  Copyright (C) 2015 Intel Corp
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <asm/cpu_device_id.h>
#include <asm/platform_sst_audio.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt5651.h"
#include "../atom/sst-atom-controls.h"

enum {
	BYT_RT5651_DMIC_MAP,
	BYT_RT5651_IN1_MAP,
	BYT_RT5651_IN2_MAP,
	BYT_RT5651_IN1_IN2_MAP,
	BYT_RT5651_IN1_HS_IN3_MAP,
	BYT_RT5651_IN2_HS_IN3_MAP,
};

enum {
	BYT_RT5651_JD_NULL	= (RT5651_JD_NULL << 4),
	BYT_RT5651_JD1_1	= (RT5651_JD1_1 << 4),
	BYT_RT5651_JD1_2	= (RT5651_JD1_2 << 4),
	BYT_RT5651_JD2		= (RT5651_JD2 << 4),
};

enum {
	BYT_RT5651_OVCD_TH_600UA  = (6 << 8),
	BYT_RT5651_OVCD_TH_1500UA = (15 << 8),
	BYT_RT5651_OVCD_TH_2000UA = (20 << 8),
};

enum {
	BYT_RT5651_OVCD_SF_0P5	= (RT5651_OVCD_SF_0P5 << 13),
	BYT_RT5651_OVCD_SF_0P75	= (RT5651_OVCD_SF_0P75 << 13),
	BYT_RT5651_OVCD_SF_1P0	= (RT5651_OVCD_SF_1P0 << 13),
	BYT_RT5651_OVCD_SF_1P5	= (RT5651_OVCD_SF_1P5 << 13),
};

#define BYT_RT5651_MAP(quirk)		((quirk) & GENMASK(3, 0))
#define BYT_RT5651_JDSRC(quirk)		(((quirk) & GENMASK(7, 4)) >> 4)
#define BYT_RT5651_OVCD_TH(quirk)	(((quirk) & GENMASK(12, 8)) >> 8)
#define BYT_RT5651_OVCD_SF(quirk)	(((quirk) & GENMASK(14, 13)) >> 13)
#define BYT_RT5651_DMIC_EN		BIT(16)
#define BYT_RT5651_MCLK_EN		BIT(17)
#define BYT_RT5651_MCLK_25MHZ		BIT(18)
#define BYT_RT5651_SSP2_AIF2		BIT(19) /* default is using AIF1  */
#define BYT_RT5651_SSP0_AIF1		BIT(20)
#define BYT_RT5651_SSP0_AIF2		BIT(21)

/* jack-detect-source + dmic-en + ovcd-th + -sf + terminating empty entry */
#define MAX_NO_PROPS 5

struct byt_rt5651_private {
	struct clk *mclk;
	struct snd_soc_jack jack;
};

/* Default: jack-detect on JD1_1, internal mic on in2, headsetmic on in3 */
static unsigned long byt_rt5651_quirk = BYT_RT5651_MCLK_EN |
					BYT_RT5651_JD1_1 |
					BYT_RT5651_OVCD_TH_2000UA |
					BYT_RT5651_OVCD_SF_0P75 |
					BYT_RT5651_IN2_HS_IN3_MAP;

static void log_quirks(struct device *dev)
{
	if (BYT_RT5651_MAP(byt_rt5651_quirk) == BYT_RT5651_DMIC_MAP)
		dev_info(dev, "quirk DMIC_MAP enabled");
	if (BYT_RT5651_MAP(byt_rt5651_quirk) == BYT_RT5651_IN1_MAP)
		dev_info(dev, "quirk IN1_MAP enabled");
	if (BYT_RT5651_MAP(byt_rt5651_quirk) == BYT_RT5651_IN2_MAP)
		dev_info(dev, "quirk IN2_MAP enabled");
	if (BYT_RT5651_MAP(byt_rt5651_quirk) == BYT_RT5651_IN1_HS_IN3_MAP)
		dev_info(dev, "quirk IN1_HS_IN3_MAP enabled");
	if (BYT_RT5651_MAP(byt_rt5651_quirk) == BYT_RT5651_IN2_HS_IN3_MAP)
		dev_info(dev, "quirk IN2_HS_IN3_MAP enabled");
	if (BYT_RT5651_JDSRC(byt_rt5651_quirk)) {
		dev_info(dev, "quirk realtek,jack-detect-source %ld\n",
			 BYT_RT5651_JDSRC(byt_rt5651_quirk));
		dev_info(dev, "quirk realtek,over-current-threshold-microamp %ld\n",
			 BYT_RT5651_OVCD_TH(byt_rt5651_quirk) * 100);
		dev_info(dev, "quirk realtek,over-current-scale-factor %ld\n",
			 BYT_RT5651_OVCD_SF(byt_rt5651_quirk));
	}
	if (byt_rt5651_quirk & BYT_RT5651_DMIC_EN)
		dev_info(dev, "quirk DMIC enabled");
	if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN)
		dev_info(dev, "quirk MCLK_EN enabled");
	if (byt_rt5651_quirk & BYT_RT5651_MCLK_25MHZ)
		dev_info(dev, "quirk MCLK_25MHZ enabled");
	if (byt_rt5651_quirk & BYT_RT5651_SSP2_AIF2)
		dev_info(dev, "quirk SSP2_AIF2 enabled\n");
	if (byt_rt5651_quirk & BYT_RT5651_SSP0_AIF1)
		dev_info(dev, "quirk SSP0_AIF1 enabled\n");
	if (byt_rt5651_quirk & BYT_RT5651_SSP0_AIF2)
		dev_info(dev, "quirk SSP0_AIF2 enabled\n");
}

#define BYT_CODEC_DAI1	"rt5651-aif1"
#define BYT_CODEC_DAI2	"rt5651-aif2"

static int byt_rt5651_prepare_and_enable_pll1(struct snd_soc_dai *codec_dai,
					      int rate, int bclk_ratio)
{
	int clk_id, clk_freq, ret;

	/* Configure the PLL before selecting it */
	if (!(byt_rt5651_quirk & BYT_RT5651_MCLK_EN)) {
		clk_id = RT5651_PLL1_S_BCLK1,
		clk_freq = rate * bclk_ratio;
	} else {
		clk_id = RT5651_PLL1_S_MCLK;
		if (byt_rt5651_quirk & BYT_RT5651_MCLK_25MHZ)
			clk_freq = 25000000;
		else
			clk_freq = 19200000;
	}
	ret = snd_soc_dai_set_pll(codec_dai, 0, clk_id, clk_freq, rate * 512);
	if (ret < 0) {
		dev_err(codec_dai->component->dev, "can't set pll: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5651_SCLK_S_PLL1,
				     rate * 512, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->component->dev, "can't set clock %d\n", ret);
		return ret;
	}

	return 0;
}

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	struct byt_rt5651_private *priv = snd_soc_card_get_drvdata(card);
	int ret;

	codec_dai = snd_soc_card_get_codec_dai(card, BYT_CODEC_DAI1);
	if (!codec_dai)
		codec_dai = snd_soc_card_get_codec_dai(card, BYT_CODEC_DAI2);
	if (!codec_dai) {
		dev_err(card->dev,
			"Codec dai not found; Unable to set platform clock\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN) {
			ret = clk_prepare_enable(priv->mclk);
			if (ret < 0) {
				dev_err(card->dev,
					"could not configure MCLK state");
				return ret;
			}
		}
		ret = byt_rt5651_prepare_and_enable_pll1(codec_dai, 48000, 50);
	} else {
		/*
		 * Set codec clock source to internal clock before
		 * turning off the platform clock. Codec needs clock
		 * for Jack detection and button press
		 */
		ret = snd_soc_dai_set_sysclk(codec_dai, RT5651_SCLK_S_RCCLK,
					     48000 * 512,
					     SND_SOC_CLOCK_IN);
		if (!ret)
			if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN)
				clk_disable_unprepare(priv->mclk);
	}

	if (ret < 0) {
		dev_err(card->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_dapm_widget byt_rt5651_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMD),

};

static const struct snd_soc_dapm_route byt_rt5651_audio_map[] = {
	{"Headphone", NULL, "Platform Clock"},
	{"Headset Mic", NULL, "Platform Clock"},
	{"Internal Mic", NULL, "Platform Clock"},
	{"Speaker", NULL, "Platform Clock"},
	{"Line In", NULL, "Platform Clock"},

	{"Headset Mic", NULL, "micbias1"}, /* lowercase for rt5651 */
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Speaker", NULL, "LOUTL"},
	{"Speaker", NULL, "LOUTR"},
	{"IN2P", NULL, "Line In"},
	{"IN2N", NULL, "Line In"},

};

static const struct snd_soc_dapm_route byt_rt5651_intmic_dmic_map[] = {
	{"IN2P", NULL, "Headset Mic"},
	{"DMIC L1", NULL, "Internal Mic"},
	{"DMIC R1", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5651_intmic_in1_map[] = {
	{"Internal Mic", NULL, "micbias1"},
	{"IN1P", NULL, "Internal Mic"},
	{"IN2P", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route byt_rt5651_intmic_in2_map[] = {
	{"Internal Mic", NULL, "micbias1"},
	{"IN1P", NULL, "Headset Mic"},
	{"IN2P", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5651_intmic_in1_in2_map[] = {
	{"Internal Mic", NULL, "micbias1"},
	{"IN1P", NULL, "Internal Mic"},
	{"IN2P", NULL, "Internal Mic"},
	{"IN3P", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route byt_rt5651_intmic_in1_hs_in3_map[] = {
	{"Internal Mic", NULL, "micbias1"},
	{"IN1P", NULL, "Internal Mic"},
	{"IN3P", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route byt_rt5651_intmic_in2_hs_in3_map[] = {
	{"Internal Mic", NULL, "micbias1"},
	{"IN2P", NULL, "Internal Mic"},
	{"IN3P", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route byt_rt5651_ssp0_aif1_map[] = {
	{"ssp0 Tx", NULL, "modem_out"},
	{"modem_in", NULL, "ssp0 Rx"},

	{"AIF1 Playback", NULL, "ssp0 Tx"},
	{"ssp0 Rx", NULL, "AIF1 Capture"},
};

static const struct snd_soc_dapm_route byt_rt5651_ssp0_aif2_map[] = {
	{"ssp0 Tx", NULL, "modem_out"},
	{"modem_in", NULL, "ssp0 Rx"},

	{"AIF2 Playback", NULL, "ssp0 Tx"},
	{"ssp0 Rx", NULL, "AIF2 Capture"},
};

static const struct snd_soc_dapm_route byt_rt5651_ssp2_aif1_map[] = {
#if !IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL)
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},

	{"AIF1 Playback", NULL, "ssp2 Tx"},
	{"ssp2 Rx", NULL, "AIF1 Capture"},
#endif
};

static const struct snd_soc_dapm_route byt_rt5651_ssp2_aif2_map[] = {
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},

	{"AIF2 Playback", NULL, "ssp2 Tx"},
	{"ssp2 Rx", NULL, "AIF2 Capture"},
};

static const struct snd_kcontrol_new byt_rt5651_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Line In"),
};

static struct snd_soc_jack_pin bytcr_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static int byt_rt5651_aif1_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	snd_pcm_format_t format = params_format(params);
	int rate = params_rate(params);
	int bclk_ratio;

	if (format == SNDRV_PCM_FORMAT_S16_LE)
		bclk_ratio = 32;
	else
		bclk_ratio = 50;

	return byt_rt5651_prepare_and_enable_pll1(codec_dai, rate, bclk_ratio);
}

static int byt_rt5651_quirk_cb(const struct dmi_system_id *id)
{
	byt_rt5651_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id byt_rt5651_quirk_table[] = {
	{
		.callback = byt_rt5651_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Circuitco"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Minnowboard Max B3 PLATFORM"),
		},
		.driver_data = (void *)(BYT_RT5651_IN1_HS_IN3_MAP),
	},
	{
		.callback = byt_rt5651_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ADI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Minnowboard Turbot"),
		},
		.driver_data = (void *)(BYT_RT5651_MCLK_EN |
					BYT_RT5651_IN1_HS_IN3_MAP),
	},
	{
		.callback = byt_rt5651_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "KIANO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "KIANO SlimNote 14.2"),
		},
		.driver_data = (void *)(BYT_RT5651_MCLK_EN |
					BYT_RT5651_JD1_1 |
					BYT_RT5651_OVCD_TH_2000UA |
					BYT_RT5651_OVCD_SF_0P75 |
					BYT_RT5651_IN1_IN2_MAP),
	},
	{
		/* Chuwi Vi8 Plus (CWI519) */
		.callback = byt_rt5651_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_PRODUCT_NAME, "D2D3_Vi8A1"),
		},
		.driver_data = (void *)(BYT_RT5651_MCLK_EN |
					BYT_RT5651_JD1_1 |
					BYT_RT5651_OVCD_TH_2000UA |
					BYT_RT5651_OVCD_SF_0P75 |
					BYT_RT5651_IN2_HS_IN3_MAP),
	},
	{
		/* VIOS LTH17 */
		.callback = byt_rt5651_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "VIOS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LTH17"),
		},
		.driver_data = (void *)(BYT_RT5651_MCLK_EN |
					BYT_RT5651_JD1_1 |
					BYT_RT5651_OVCD_TH_2000UA |
					BYT_RT5651_OVCD_SF_1P0 |
					BYT_RT5651_IN1_IN2_MAP),
	},
	{}
};

/*
 * Note this MUST be called before snd_soc_register_card(), so that the props
 * are in place before the codec component driver's probe function parses them.
 */
static int byt_rt5651_add_codec_device_props(const char *i2c_dev_name)
{
	struct property_entry props[MAX_NO_PROPS] = {};
	struct device *i2c_dev;
	int ret, cnt = 0;

	i2c_dev = bus_find_device_by_name(&i2c_bus_type, NULL, i2c_dev_name);
	if (!i2c_dev)
		return -EPROBE_DEFER;

	props[cnt++] = PROPERTY_ENTRY_U32("realtek,jack-detect-source",
				BYT_RT5651_JDSRC(byt_rt5651_quirk));

	props[cnt++] = PROPERTY_ENTRY_U32("realtek,over-current-threshold-microamp",
				BYT_RT5651_OVCD_TH(byt_rt5651_quirk) * 100);

	props[cnt++] = PROPERTY_ENTRY_U32("realtek,over-current-scale-factor",
				BYT_RT5651_OVCD_SF(byt_rt5651_quirk));

	if (byt_rt5651_quirk & BYT_RT5651_DMIC_EN)
		props[cnt++] = PROPERTY_ENTRY_BOOL("realtek,dmic-en");

	ret = device_add_properties(i2c_dev, props);
	put_device(i2c_dev);

	return ret;
}

static int byt_rt5651_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_component *codec = runtime->codec_dai->component;
	struct byt_rt5651_private *priv = snd_soc_card_get_drvdata(card);
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;
	int ret;

	card->dapm.idle_bias_off = true;

	/* Start with RC clk for jack-detect (we disable MCLK below) */
	if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN)
		snd_soc_component_update_bits(codec, RT5651_GLB_CLK,
			RT5651_SCLK_SRC_MASK, RT5651_SCLK_SRC_RCCLK);

	switch (BYT_RT5651_MAP(byt_rt5651_quirk)) {
	case BYT_RT5651_IN1_MAP:
		custom_map = byt_rt5651_intmic_in1_map;
		num_routes = ARRAY_SIZE(byt_rt5651_intmic_in1_map);
		break;
	case BYT_RT5651_IN2_MAP:
		custom_map = byt_rt5651_intmic_in2_map;
		num_routes = ARRAY_SIZE(byt_rt5651_intmic_in2_map);
		break;
	case BYT_RT5651_IN1_IN2_MAP:
		custom_map = byt_rt5651_intmic_in1_in2_map;
		num_routes = ARRAY_SIZE(byt_rt5651_intmic_in1_in2_map);
		break;
	case BYT_RT5651_IN1_HS_IN3_MAP:
		custom_map = byt_rt5651_intmic_in1_hs_in3_map;
		num_routes = ARRAY_SIZE(byt_rt5651_intmic_in1_hs_in3_map);
		break;
	case BYT_RT5651_IN2_HS_IN3_MAP:
		custom_map = byt_rt5651_intmic_in2_hs_in3_map;
		num_routes = ARRAY_SIZE(byt_rt5651_intmic_in2_hs_in3_map);
		break;
	default:
		custom_map = byt_rt5651_intmic_dmic_map;
		num_routes = ARRAY_SIZE(byt_rt5651_intmic_dmic_map);
	}
	ret = snd_soc_dapm_add_routes(&card->dapm, custom_map, num_routes);
	if (ret)
		return ret;

	if (byt_rt5651_quirk & BYT_RT5651_SSP2_AIF2) {
		ret = snd_soc_dapm_add_routes(&card->dapm,
					byt_rt5651_ssp2_aif2_map,
					ARRAY_SIZE(byt_rt5651_ssp2_aif2_map));
	} else if (byt_rt5651_quirk & BYT_RT5651_SSP0_AIF1) {
		ret = snd_soc_dapm_add_routes(&card->dapm,
					byt_rt5651_ssp0_aif1_map,
					ARRAY_SIZE(byt_rt5651_ssp0_aif1_map));
	} else if (byt_rt5651_quirk & BYT_RT5651_SSP0_AIF2) {
		ret = snd_soc_dapm_add_routes(&card->dapm,
					byt_rt5651_ssp0_aif2_map,
					ARRAY_SIZE(byt_rt5651_ssp0_aif2_map));
	} else {
		ret = snd_soc_dapm_add_routes(&card->dapm,
					byt_rt5651_ssp2_aif1_map,
					ARRAY_SIZE(byt_rt5651_ssp2_aif1_map));
	}
	if (ret)
		return ret;

	ret = snd_soc_add_card_controls(card, byt_rt5651_controls,
					ARRAY_SIZE(byt_rt5651_controls));
	if (ret) {
		dev_err(card->dev, "unable to add card controls\n");
		return ret;
	}
	snd_soc_dapm_ignore_suspend(&card->dapm, "Headphone");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Speaker");

	if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN) {
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
		ret = clk_prepare_enable(priv->mclk);
		if (!ret)
			clk_disable_unprepare(priv->mclk);

		if (byt_rt5651_quirk & BYT_RT5651_MCLK_25MHZ)
			ret = clk_set_rate(priv->mclk, 25000000);
		else
			ret = clk_set_rate(priv->mclk, 19200000);

		if (ret)
			dev_err(card->dev, "unable to set MCLK rate\n");
	}

	if (BYT_RT5651_JDSRC(byt_rt5651_quirk)) {
		ret = snd_soc_card_jack_new(runtime->card, "Headset",
				    SND_JACK_HEADSET, &priv->jack,
				    bytcr_jack_pins, ARRAY_SIZE(bytcr_jack_pins));
		if (ret) {
			dev_err(runtime->dev, "jack creation failed %d\n", ret);
			return ret;
		}

		ret = snd_soc_component_set_jack(codec, &priv->jack, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct snd_soc_pcm_stream byt_rt5651_dai_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static int byt_rt5651_codec_fixup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	int ret, bits;

	/* The DSP will covert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	if ((byt_rt5651_quirk & BYT_RT5651_SSP0_AIF1) ||
	    (byt_rt5651_quirk & BYT_RT5651_SSP0_AIF2)) {
		/* set SSP0 to 16-bit */
		params_set_format(params, SNDRV_PCM_FORMAT_S16_LE);
		bits = 16;
	} else {
		/* set SSP2 to 24-bit */
		params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);
		bits = 24;
	}

	/*
	 * Default mode for SSP configuration is TDM 4 slot, override config
	 * with explicit setting to I2S 2ch. The word length is set with
	 * dai_set_tdm_slot() since there is no other API exposed
	 */
	ret = snd_soc_dai_set_fmt(rtd->cpu_dai,
				  SND_SOC_DAIFMT_I2S     |
				  SND_SOC_DAIFMT_NB_NF   |
				  SND_SOC_DAIFMT_CBS_CFS
				  );

	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(rtd->cpu_dai, 0x3, 0x3, 2, bits);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
		return ret;
	}

	return 0;
}

static const unsigned int rates_48000[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_48000 = {
	.count = ARRAY_SIZE(rates_48000),
	.list  = rates_48000,
};

static int byt_rt5651_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE,
			&constraints_48000);
}

static const struct snd_soc_ops byt_rt5651_aif1_ops = {
	.startup = byt_rt5651_aif1_startup,
};

static const struct snd_soc_ops byt_rt5651_be_ssp2_ops = {
	.hw_params = byt_rt5651_aif1_hw_params,
};

static struct snd_soc_dai_link byt_rt5651_dais[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.cpu_dai_name = "media-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &byt_rt5651_aif1_ops,
	},
	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.cpu_dai_name = "deepbuffer-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &byt_rt5651_aif1_ops,
	},
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "SSP2-Codec",
		.id = 0,
		.cpu_dai_name = "ssp2-port",
		.platform_name = "sst-mfld-platform",
		.no_pcm = 1,
		.codec_dai_name = "rt5651-aif1",
		.codec_name = "i2c-10EC5651:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = byt_rt5651_codec_fixup,
		.ignore_suspend = 1,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = byt_rt5651_init,
		.ops = &byt_rt5651_be_ssp2_ops,
	},
};

/* SoC card */
static struct snd_soc_card byt_rt5651_card = {
	.name = "bytcr-rt5651",
	.owner = THIS_MODULE,
	.dai_link = byt_rt5651_dais,
	.num_links = ARRAY_SIZE(byt_rt5651_dais),
	.dapm_widgets = byt_rt5651_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_rt5651_widgets),
	.dapm_routes = byt_rt5651_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_rt5651_audio_map),
	.fully_routed = true,
};

static char byt_rt5651_codec_name[SND_ACPI_I2C_ID_LEN];
static char byt_rt5651_codec_aif_name[12]; /*  = "rt5651-aif[1|2]" */
static char byt_rt5651_cpu_dai_name[10]; /*  = "ssp[0|2]-port" */
static char byt_rt5651_long_name[40]; /* = "bytcr-rt5651-*-spk-*-mic" */

#if !IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL)
static bool is_valleyview(void)
{
	static const struct x86_cpu_id cpu_ids[] = {
		{ X86_VENDOR_INTEL, 6, 55 }, /* Valleyview, Bay Trail */
		{}
	};

	if (!x86_match_cpu(cpu_ids))
		return false;
	return true;
}
#endif

struct acpi_chan_package {   /* ACPICA seems to require 64 bit integers */
	u64 aif_value;       /* 1: AIF1, 2: AIF2 */
	u64 mclock_value;    /* usually 25MHz (0x17d7940), ignored */
};

static int snd_byt_rt5651_mc_probe(struct platform_device *pdev)
{
	const char * const intmic_name[] =
		{ "dmic", "in1", "in2", "in12", "in1", "in2" };
	const char * const hsmic_name[] =
		{  "in2", "in2", "in1",  "in3", "in3", "in3" };
	struct byt_rt5651_private *priv;
	struct snd_soc_acpi_mach *mach;
	const char *i2c_name = NULL;
#if !IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL)
	bool is_bytcr = false;
#endif
	int ret_val = 0;
	int dai_index = 0;
	int i;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* register the soc card */
	byt_rt5651_card.dev = &pdev->dev;

	mach = byt_rt5651_card.dev->platform_data;
	snd_soc_card_set_drvdata(&byt_rt5651_card, priv);

	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(byt_rt5651_dais); i++) {
		if (!strcmp(byt_rt5651_dais[i].codec_name, "i2c-10EC5651:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	i2c_name = acpi_dev_get_first_match_name(mach->id, NULL, -1);
	if (!i2c_name) {
		dev_err(&pdev->dev, "Error cannot find '%s' dev\n", mach->id);
		return -ENODEV;
	}
	snprintf(byt_rt5651_codec_name, sizeof(byt_rt5651_codec_name),
		"%s%s", "i2c-", i2c_name);
	byt_rt5651_dais[dai_index].codec_name = byt_rt5651_codec_name;

#if !IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL)
	/*
	 * swap SSP0 if bytcr is detected
	 * (will be overridden if DMI quirk is detected)
	 */
	if (is_valleyview()) {
		struct sst_platform_info *p_info = mach->pdata;
		const struct sst_res_info *res_info = p_info->res_info;

		if (res_info->acpi_ipc_irq_index == 0)
			is_bytcr = true;
	}

	if (is_bytcr) {
		/*
		 * Baytrail CR platforms may have CHAN package in BIOS, try
		 * to find relevant routing quirk based as done on Windows
		 * platforms. We have to read the information directly from the
		 * BIOS, at this stage the card is not created and the links
		 * with the codec driver/pdata are non-existent
		 */

		struct acpi_chan_package chan_package;

		/* format specified: 2 64-bit integers */
		struct acpi_buffer format = {sizeof("NN"), "NN"};
		struct acpi_buffer state = {0, NULL};
		struct snd_soc_acpi_package_context pkg_ctx;
		bool pkg_found = false;

		state.length = sizeof(chan_package);
		state.pointer = &chan_package;

		pkg_ctx.name = "CHAN";
		pkg_ctx.length = 2;
		pkg_ctx.format = &format;
		pkg_ctx.state = &state;
		pkg_ctx.data_valid = false;

		pkg_found = snd_soc_acpi_find_package_from_hid(mach->id,
							       &pkg_ctx);
		if (pkg_found) {
			if (chan_package.aif_value == 1) {
				dev_info(&pdev->dev, "BIOS Routing: AIF1 connected\n");
				byt_rt5651_quirk |= BYT_RT5651_SSP0_AIF1;
			} else  if (chan_package.aif_value == 2) {
				dev_info(&pdev->dev, "BIOS Routing: AIF2 connected\n");
				byt_rt5651_quirk |= BYT_RT5651_SSP0_AIF2;
			} else {
				dev_info(&pdev->dev, "BIOS Routing isn't valid, ignored\n");
				pkg_found = false;
			}
		}

		if (!pkg_found) {
			/* no BIOS indications, assume SSP0-AIF2 connection */
			byt_rt5651_quirk |= BYT_RT5651_SSP0_AIF2;
		}
	}
#endif

	/* check quirks before creating card */
	dmi_check_system(byt_rt5651_quirk_table);

	/* Must be called before register_card, also see declaration comment. */
	ret_val = byt_rt5651_add_codec_device_props(byt_rt5651_codec_name);
	if (ret_val)
		return ret_val;

	log_quirks(&pdev->dev);

	if ((byt_rt5651_quirk & BYT_RT5651_SSP2_AIF2) ||
	    (byt_rt5651_quirk & BYT_RT5651_SSP0_AIF2)) {
		/* fixup codec aif name */
		snprintf(byt_rt5651_codec_aif_name,
			sizeof(byt_rt5651_codec_aif_name),
			"%s", "rt5651-aif2");

		byt_rt5651_dais[dai_index].codec_dai_name =
			byt_rt5651_codec_aif_name;
	}

	if ((byt_rt5651_quirk & BYT_RT5651_SSP0_AIF1) ||
	    (byt_rt5651_quirk & BYT_RT5651_SSP0_AIF2)) {
		/* fixup cpu dai name name */
		snprintf(byt_rt5651_cpu_dai_name,
			sizeof(byt_rt5651_cpu_dai_name),
			"%s", "ssp0-port");

		byt_rt5651_dais[dai_index].cpu_dai_name =
			byt_rt5651_cpu_dai_name;
	}

	if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN) {
		priv->mclk = devm_clk_get(&pdev->dev, "pmc_plt_clk_3");
		if (IS_ERR(priv->mclk)) {
			ret_val = PTR_ERR(priv->mclk);
			dev_err(&pdev->dev,
				"Failed to get MCLK from pmc_plt_clk_3: %d\n",
				ret_val);
			/*
			 * Fall back to bit clock usage for -ENOENT (clock not
			 * available likely due to missing dependencies), bail
			 * for all other errors, including -EPROBE_DEFER
			 */
			if (ret_val != -ENOENT)
				return ret_val;
			byt_rt5651_quirk &= ~BYT_RT5651_MCLK_EN;
		}
	}

	snprintf(byt_rt5651_long_name, sizeof(byt_rt5651_long_name),
		 "bytcr-rt5651-%s-intmic-%s-hsmic",
		 intmic_name[BYT_RT5651_MAP(byt_rt5651_quirk)],
		 hsmic_name[BYT_RT5651_MAP(byt_rt5651_quirk)]);
	byt_rt5651_card.long_name = byt_rt5651_long_name;

	ret_val = devm_snd_soc_register_card(&pdev->dev, &byt_rt5651_card);

	if (ret_val) {
		dev_err(&pdev->dev, "devm_snd_soc_register_card failed %d\n",
			ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, &byt_rt5651_card);
	return ret_val;
}

static struct platform_driver snd_byt_rt5651_mc_driver = {
	.driver = {
		.name = "bytcr_rt5651",
	},
	.probe = snd_byt_rt5651_mc_probe,
};

module_platform_driver(snd_byt_rt5651_mc_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail CR Machine driver for RT5651");
MODULE_AUTHOR("Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcr_rt5651");
