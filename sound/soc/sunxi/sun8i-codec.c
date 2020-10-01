// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This driver supports the digital controls for the internal codec
 * found in Allwinner's A33 SoCs.
 *
 * (C) Copyright 2010-2016
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * huangxin <huangxin@Reuuimllatech.com>
 * Mylène Josserand <mylene.josserand@free-electrons.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/log2.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#define SUN8I_SYSCLK_CTL				0x00c
#define SUN8I_SYSCLK_CTL_AIF1CLK_ENA			11
#define SUN8I_SYSCLK_CTL_AIF1CLK_SRC_PLL		(0x2 << 8)
#define SUN8I_SYSCLK_CTL_AIF2CLK_ENA			7
#define SUN8I_SYSCLK_CTL_AIF2CLK_SRC_PLL		(0x2 << 4)
#define SUN8I_SYSCLK_CTL_SYSCLK_ENA			3
#define SUN8I_SYSCLK_CTL_SYSCLK_SRC			0
#define SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF1CLK		(0x0 << 0)
#define SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF2CLK		(0x1 << 0)
#define SUN8I_MOD_CLK_ENA				0x010
#define SUN8I_MOD_CLK_ENA_AIF1				15
#define SUN8I_MOD_CLK_ENA_ADC				3
#define SUN8I_MOD_CLK_ENA_DAC				2
#define SUN8I_MOD_RST_CTL				0x014
#define SUN8I_MOD_RST_CTL_AIF1				15
#define SUN8I_MOD_RST_CTL_ADC				3
#define SUN8I_MOD_RST_CTL_DAC				2
#define SUN8I_SYS_SR_CTRL				0x018
#define SUN8I_SYS_SR_CTRL_AIF1_FS			12
#define SUN8I_SYS_SR_CTRL_AIF2_FS			8
#define SUN8I_AIF1CLK_CTRL				0x040
#define SUN8I_AIF1CLK_CTRL_AIF1_MSTR_MOD		15
#define SUN8I_AIF1CLK_CTRL_AIF1_BCLK_INV		14
#define SUN8I_AIF1CLK_CTRL_AIF1_LRCK_INV		13
#define SUN8I_AIF1CLK_CTRL_AIF1_BCLK_DIV		9
#define SUN8I_AIF1CLK_CTRL_AIF1_LRCK_DIV		6
#define SUN8I_AIF1CLK_CTRL_AIF1_WORD_SIZ		4
#define SUN8I_AIF1CLK_CTRL_AIF1_WORD_SIZ_16		(1 << 4)
#define SUN8I_AIF1CLK_CTRL_AIF1_DATA_FMT		2
#define SUN8I_AIF1_ADCDAT_CTRL				0x044
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_ENA		15
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_ENA		14
#define SUN8I_AIF1_DACDAT_CTRL				0x048
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_ENA		15
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_ENA		14
#define SUN8I_AIF1_MXR_SRC				0x04c
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF1DA0L	15
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACL	14
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_ADCL		13
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACR	12
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF1DA0R	11
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACR	10
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_ADCR		9
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACL	8
#define SUN8I_ADC_DIG_CTRL				0x100
#define SUN8I_ADC_DIG_CTRL_ENAD				15
#define SUN8I_ADC_DIG_CTRL_ADOUT_DTS			2
#define SUN8I_ADC_DIG_CTRL_ADOUT_DLY			1
#define SUN8I_DAC_DIG_CTRL				0x120
#define SUN8I_DAC_DIG_CTRL_ENDA				15
#define SUN8I_DAC_MXR_SRC				0x130
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA0L		15
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA1L		14
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF2DACL		13
#define SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_ADCL		12
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA0R		11
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA1R		10
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF2DACR		9
#define SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_ADCR		8

#define SUN8I_SYSCLK_CTL_AIF1CLK_SRC_MASK	GENMASK(9, 8)
#define SUN8I_SYSCLK_CTL_AIF2CLK_SRC_MASK	GENMASK(5, 4)
#define SUN8I_SYS_SR_CTRL_AIF1_FS_MASK		GENMASK(15, 12)
#define SUN8I_SYS_SR_CTRL_AIF2_FS_MASK		GENMASK(11, 8)
#define SUN8I_AIF1CLK_CTRL_AIF1_BCLK_DIV_MASK	GENMASK(12, 9)
#define SUN8I_AIF1CLK_CTRL_AIF1_LRCK_DIV_MASK	GENMASK(8, 6)
#define SUN8I_AIF1CLK_CTRL_AIF1_WORD_SIZ_MASK	GENMASK(5, 4)
#define SUN8I_AIF1CLK_CTRL_AIF1_DATA_FMT_MASK	GENMASK(3, 2)

struct sun8i_codec_quirks {
	bool legacy_widgets	: 1;
	bool lrck_inversion	: 1;
};

struct sun8i_codec {
	struct regmap			*regmap;
	struct clk			*clk_module;
	const struct sun8i_codec_quirks	*quirks;
};

static int sun8i_codec_runtime_resume(struct device *dev)
{
	struct sun8i_codec *scodec = dev_get_drvdata(dev);
	int ret;

	regcache_cache_only(scodec->regmap, false);

	ret = regcache_sync(scodec->regmap);
	if (ret) {
		dev_err(dev, "Failed to sync regmap cache\n");
		return ret;
	}

	return 0;
}

static int sun8i_codec_runtime_suspend(struct device *dev)
{
	struct sun8i_codec *scodec = dev_get_drvdata(dev);

	regcache_cache_only(scodec->regmap, true);
	regcache_mark_dirty(scodec->regmap);

	return 0;
}

static int sun8i_codec_get_hw_rate(struct snd_pcm_hw_params *params)
{
	unsigned int rate = params_rate(params);

	switch (rate) {
	case 8000:
	case 7350:
		return 0x0;
	case 11025:
		return 0x1;
	case 12000:
		return 0x2;
	case 16000:
		return 0x3;
	case 22050:
		return 0x4;
	case 24000:
		return 0x5;
	case 32000:
		return 0x6;
	case 44100:
		return 0x7;
	case 48000:
		return 0x8;
	case 96000:
		return 0x9;
	case 192000:
		return 0xa;
	default:
		return -EINVAL;
	}
}

static int sun8i_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(dai->component);
	u32 value;

	/* clock masters */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS: /* Codec slave, DAI master */
		value = 0x1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM: /* Codec Master, DAI slave */
		value = 0x0;
		break;
	default:
		return -EINVAL;
	}
	regmap_update_bits(scodec->regmap, SUN8I_AIF1CLK_CTRL,
			   BIT(SUN8I_AIF1CLK_CTRL_AIF1_MSTR_MOD),
			   value << SUN8I_AIF1CLK_CTRL_AIF1_MSTR_MOD);

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF: /* Normal */
		value = 0x0;
		break;
	case SND_SOC_DAIFMT_IB_IF: /* Inversion */
		value = 0x1;
		break;
	default:
		return -EINVAL;
	}
	regmap_update_bits(scodec->regmap, SUN8I_AIF1CLK_CTRL,
			   BIT(SUN8I_AIF1CLK_CTRL_AIF1_BCLK_INV),
			   value << SUN8I_AIF1CLK_CTRL_AIF1_BCLK_INV);

	/*
	 * It appears that the DAI and the codec in the A33 SoC don't
	 * share the same polarity for the LRCK signal when they mean
	 * 'normal' and 'inverted' in the datasheet.
	 *
	 * Since the DAI here is our regular i2s driver that have been
	 * tested with way more codecs than just this one, it means
	 * that the codec probably gets it backward, and we have to
	 * invert the value here.
	 */
	value ^= scodec->quirks->lrck_inversion;
	regmap_update_bits(scodec->regmap, SUN8I_AIF1CLK_CTRL,
			   BIT(SUN8I_AIF1CLK_CTRL_AIF1_LRCK_INV),
			   value << SUN8I_AIF1CLK_CTRL_AIF1_LRCK_INV);

	/* DAI format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		value = 0x0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		value = 0x1;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		value = 0x2;
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		value = 0x3;
		break;
	default:
		return -EINVAL;
	}
	regmap_update_bits(scodec->regmap, SUN8I_AIF1CLK_CTRL,
			   SUN8I_AIF1CLK_CTRL_AIF1_DATA_FMT_MASK,
			   value << SUN8I_AIF1CLK_CTRL_AIF1_DATA_FMT);

	return 0;
}

struct sun8i_codec_clk_div {
	u8	div;
	u8	val;
};

static const struct sun8i_codec_clk_div sun8i_codec_bclk_div[] = {
	{ .div = 1,	.val = 0 },
	{ .div = 2,	.val = 1 },
	{ .div = 4,	.val = 2 },
	{ .div = 6,	.val = 3 },
	{ .div = 8,	.val = 4 },
	{ .div = 12,	.val = 5 },
	{ .div = 16,	.val = 6 },
	{ .div = 24,	.val = 7 },
	{ .div = 32,	.val = 8 },
	{ .div = 48,	.val = 9 },
	{ .div = 64,	.val = 10 },
	{ .div = 96,	.val = 11 },
	{ .div = 128,	.val = 12 },
	{ .div = 192,	.val = 13 },
};

static u8 sun8i_codec_get_bclk_div(struct sun8i_codec *scodec,
				   unsigned int rate,
				   unsigned int word_size)
{
	unsigned long clk_rate = clk_get_rate(scodec->clk_module);
	unsigned int div = clk_rate / rate / word_size / 2;
	unsigned int best_val = 0, best_diff = ~0;
	int i;

	for (i = 0; i < ARRAY_SIZE(sun8i_codec_bclk_div); i++) {
		const struct sun8i_codec_clk_div *bdiv = &sun8i_codec_bclk_div[i];
		unsigned int diff = abs(bdiv->div - div);

		if (diff < best_diff) {
			best_diff = diff;
			best_val = bdiv->val;
		}
	}

	return best_val;
}

static int sun8i_codec_get_lrck_div(unsigned int channels,
				    unsigned int word_size)
{
	unsigned int div = word_size * channels;

	if (div < 16 || div > 256)
		return -EINVAL;

	return ilog2(div) - 4;
}

static int sun8i_codec_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(dai->component);
	int sample_rate, lrck_div;
	u8 bclk_div;

	/*
	 * The CPU DAI handles only a sample of 16 bits. Configure the
	 * codec to handle this type of sample resolution.
	 */
	regmap_update_bits(scodec->regmap, SUN8I_AIF1CLK_CTRL,
			   SUN8I_AIF1CLK_CTRL_AIF1_WORD_SIZ_MASK,
			   SUN8I_AIF1CLK_CTRL_AIF1_WORD_SIZ_16);

	bclk_div = sun8i_codec_get_bclk_div(scodec, params_rate(params), 16);
	regmap_update_bits(scodec->regmap, SUN8I_AIF1CLK_CTRL,
			   SUN8I_AIF1CLK_CTRL_AIF1_BCLK_DIV_MASK,
			   bclk_div << SUN8I_AIF1CLK_CTRL_AIF1_BCLK_DIV);

	lrck_div = sun8i_codec_get_lrck_div(params_channels(params),
					    params_physical_width(params));
	if (lrck_div < 0)
		return lrck_div;

	regmap_update_bits(scodec->regmap, SUN8I_AIF1CLK_CTRL,
			   SUN8I_AIF1CLK_CTRL_AIF1_LRCK_DIV_MASK,
			   lrck_div << SUN8I_AIF1CLK_CTRL_AIF1_LRCK_DIV);

	sample_rate = sun8i_codec_get_hw_rate(params);
	if (sample_rate < 0)
		return sample_rate;

	regmap_update_bits(scodec->regmap, SUN8I_SYS_SR_CTRL,
			   SUN8I_SYS_SR_CTRL_AIF1_FS_MASK,
			   sample_rate << SUN8I_SYS_SR_CTRL_AIF1_FS);

	return 0;
}

static const struct snd_kcontrol_new sun8i_aif1_ad0_mixer_controls[] = {
	SOC_DAPM_DOUBLE("AIF1 Slot 0 Digital ADC Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF1DA0L,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF1DA0R, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 Digital ADC Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACL,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACR, 1, 0),
	SOC_DAPM_DOUBLE("AIF1 Data Digital ADC Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_ADCL,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_ADCR, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 Inv Digital ADC Capture Switch",
			SUN8I_AIF1_MXR_SRC,
			SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACR,
			SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACL, 1, 0),
};

static const struct snd_kcontrol_new sun8i_dac_mixer_controls[] = {
	SOC_DAPM_DOUBLE("AIF1 Slot 0 Digital DAC Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA0L,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA0R, 1, 0),
	SOC_DAPM_DOUBLE("AIF1 Slot 1 Digital DAC Playback Switch",
			SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF1DA1L,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF1DA1R, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 Digital DAC Playback Switch", SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_AIF2DACL,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_AIF2DACR, 1, 0),
	SOC_DAPM_DOUBLE("ADC Digital DAC Playback Switch", SUN8I_DAC_MXR_SRC,
			SUN8I_DAC_MXR_SRC_DACL_MXR_SRC_ADCL,
			SUN8I_DAC_MXR_SRC_DACR_MXR_SRC_ADCR, 1, 0),
};

static const struct snd_soc_dapm_widget sun8i_codec_dapm_widgets[] = {
	/* System Clocks */
	SND_SOC_DAPM_CLOCK_SUPPLY("mod"),

	SND_SOC_DAPM_SUPPLY("AIF1CLK",
			    SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_AIF1CLK_ENA, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SYSCLK",
			    SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_SYSCLK_ENA, 0, NULL, 0),

	/* Module Clocks */
	SND_SOC_DAPM_SUPPLY("CLK AIF1",
			    SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK ADC",
			    SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_ADC, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK DAC",
			    SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_DAC, 0, NULL, 0),

	/* Module Resets */
	SND_SOC_DAPM_SUPPLY("RST AIF1",
			    SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_AIF1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST ADC",
			    SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_ADC, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST DAC",
			    SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_DAC, 0, NULL, 0),

	/* Module Supplies */
	SND_SOC_DAPM_SUPPLY("ADC",
			    SUN8I_ADC_DIG_CTRL,
			    SUN8I_ADC_DIG_CTRL_ENAD, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC",
			    SUN8I_DAC_DIG_CTRL,
			    SUN8I_DAC_DIG_CTRL_ENDA, 0, NULL, 0),

	/* AIF "ADC" Outputs */
	SND_SOC_DAPM_AIF_IN("AIF1 AD0L", "Capture", 0,
			    SUN8I_AIF1_ADCDAT_CTRL,
			    SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_ENA, 0),
	SND_SOC_DAPM_AIF_IN("AIF1 AD0R", "Capture", 0,
			    SUN8I_AIF1_ADCDAT_CTRL,
			    SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_ENA, 0),

	/* AIF "ADC" Mixers */
	SOC_MIXER_ARRAY("AIF1 AD0L Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif1_ad0_mixer_controls),
	SOC_MIXER_ARRAY("AIF1 AD0R Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif1_ad0_mixer_controls),

	/* AIF "DAC" Inputs */
	SND_SOC_DAPM_AIF_IN("AIF1 DA0L", "Playback", 0,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_ENA, 0),
	SND_SOC_DAPM_AIF_IN("AIF1 DA0R", "Playback", 0,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_ENA, 0),

	/* ADC Inputs (connected to analog codec DAPM context) */
	SND_SOC_DAPM_ADC("ADCL", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADCR", NULL, SND_SOC_NOPM, 0, 0),

	/* DAC Outputs (connected to analog codec DAPM context) */
	SND_SOC_DAPM_DAC("DACL", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DACR", NULL, SND_SOC_NOPM, 0, 0),

	/* DAC Mixers */
	SOC_MIXER_ARRAY("DACL Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_dac_mixer_controls),
	SOC_MIXER_ARRAY("DACR Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_dac_mixer_controls),
};

static const struct snd_soc_dapm_route sun8i_codec_dapm_routes[] = {
	/* Clock Routes */
	{ "AIF1CLK", NULL, "mod" },

	{ "SYSCLK", NULL, "AIF1CLK" },

	{ "CLK AIF1", NULL, "AIF1CLK" },
	{ "CLK AIF1", NULL, "SYSCLK" },
	{ "RST AIF1", NULL, "CLK AIF1" },
	{ "AIF1 AD0L", NULL, "RST AIF1" },
	{ "AIF1 AD0R", NULL, "RST AIF1" },
	{ "AIF1 DA0L", NULL, "RST AIF1" },
	{ "AIF1 DA0R", NULL, "RST AIF1" },

	{ "CLK ADC", NULL, "SYSCLK" },
	{ "RST ADC", NULL, "CLK ADC" },
	{ "ADC", NULL, "RST ADC" },
	{ "ADCL", NULL, "ADC" },
	{ "ADCR", NULL, "ADC" },

	{ "CLK DAC", NULL, "SYSCLK" },
	{ "RST DAC", NULL, "CLK DAC" },
	{ "DAC", NULL, "RST DAC" },
	{ "DACL", NULL, "DAC" },
	{ "DACR", NULL, "DAC" },

	/* AIF "ADC" Output Routes */
	{ "AIF1 AD0L", NULL, "AIF1 AD0L Mixer" },
	{ "AIF1 AD0R", NULL, "AIF1 AD0R Mixer" },

	/* AIF "ADC" Mixer Routes */
	{ "AIF1 AD0L Mixer", "AIF1 Slot 0 Digital ADC Capture Switch", "AIF1 DA0L" },
	{ "AIF1 AD0L Mixer", "AIF1 Data Digital ADC Capture Switch", "ADCL" },

	{ "AIF1 AD0R Mixer", "AIF1 Slot 0 Digital ADC Capture Switch", "AIF1 DA0R" },
	{ "AIF1 AD0R Mixer", "AIF1 Data Digital ADC Capture Switch", "ADCR" },

	/* DAC Output Routes */
	{ "DACL", NULL, "DACL Mixer" },
	{ "DACR", NULL, "DACR Mixer" },

	/* DAC Mixer Routes */
	{ "DACL Mixer", "AIF1 Slot 0 Digital DAC Playback Switch", "AIF1 DA0L" },
	{ "DACL Mixer", "ADC Digital DAC Playback Switch", "ADCL" },

	{ "DACR Mixer", "AIF1 Slot 0 Digital DAC Playback Switch", "AIF1 DA0R" },
	{ "DACR Mixer", "ADC Digital DAC Playback Switch", "ADCR" },
};

static const struct snd_soc_dapm_widget sun8i_codec_legacy_widgets[] = {
	/* Legacy ADC Inputs (connected to analog codec DAPM context) */
	SND_SOC_DAPM_ADC("AIF1 Slot 0 Left ADC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("AIF1 Slot 0 Right ADC", NULL, SND_SOC_NOPM, 0, 0),

	/* Legacy DAC Outputs (connected to analog codec DAPM context) */
	SND_SOC_DAPM_DAC("AIF1 Slot 0 Left", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("AIF1 Slot 0 Right", NULL, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route sun8i_codec_legacy_routes[] = {
	/* Legacy ADC Routes */
	{ "ADCL", NULL, "AIF1 Slot 0 Left ADC" },
	{ "ADCR", NULL, "AIF1 Slot 0 Right ADC" },

	/* Legacy DAC Routes */
	{ "AIF1 Slot 0 Left", NULL, "DACL" },
	{ "AIF1 Slot 0 Right", NULL, "DACR" },
};

static int sun8i_codec_component_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(component);
	int ret;

	/* Add widgets for backward compatibility with old device trees. */
	if (scodec->quirks->legacy_widgets) {
		ret = snd_soc_dapm_new_controls(dapm, sun8i_codec_legacy_widgets,
						ARRAY_SIZE(sun8i_codec_legacy_widgets));
		if (ret)
			return ret;

		ret = snd_soc_dapm_add_routes(dapm, sun8i_codec_legacy_routes,
					      ARRAY_SIZE(sun8i_codec_legacy_routes));
		if (ret)
			return ret;
	}

	/*
	 * AIF1CLK and AIF2CLK share a pair of clock parents: PLL_AUDIO ("mod")
	 * and MCLK (from the CPU DAI connected to AIF1). MCLK's parent is also
	 * PLL_AUDIO, so using it adds no additional flexibility. Use PLL_AUDIO
	 * directly to simplify the clock tree.
	 */
	regmap_update_bits(scodec->regmap, SUN8I_SYSCLK_CTL,
			   SUN8I_SYSCLK_CTL_AIF1CLK_SRC_MASK |
			   SUN8I_SYSCLK_CTL_AIF2CLK_SRC_MASK,
			   SUN8I_SYSCLK_CTL_AIF1CLK_SRC_PLL |
			   SUN8I_SYSCLK_CTL_AIF2CLK_SRC_PLL);

	/* Use AIF1CLK as the SYSCLK parent since AIF1 is used most often. */
	regmap_update_bits(scodec->regmap, SUN8I_SYSCLK_CTL,
			   BIT(SUN8I_SYSCLK_CTL_SYSCLK_SRC),
			   SUN8I_SYSCLK_CTL_SYSCLK_SRC_AIF1CLK);

	return 0;
}

static const struct snd_soc_dai_ops sun8i_codec_dai_ops = {
	.hw_params = sun8i_codec_hw_params,
	.set_fmt = sun8i_set_fmt,
};

static struct snd_soc_dai_driver sun8i_codec_dai = {
	.name = "sun8i",
	/* playback capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	/* capture capabilities */
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
		.sig_bits = 24,
	},
	/* pcm operations */
	.ops = &sun8i_codec_dai_ops,
};

static const struct snd_soc_component_driver sun8i_soc_component = {
	.dapm_widgets		= sun8i_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sun8i_codec_dapm_widgets),
	.dapm_routes		= sun8i_codec_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(sun8i_codec_dapm_routes),
	.probe			= sun8i_codec_component_probe,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config sun8i_codec_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= SUN8I_DAC_MXR_SRC,

	.cache_type	= REGCACHE_FLAT,
};

static int sun8i_codec_probe(struct platform_device *pdev)
{
	struct sun8i_codec *scodec;
	void __iomem *base;
	int ret;

	scodec = devm_kzalloc(&pdev->dev, sizeof(*scodec), GFP_KERNEL);
	if (!scodec)
		return -ENOMEM;

	scodec->clk_module = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(scodec->clk_module)) {
		dev_err(&pdev->dev, "Failed to get the module clock\n");
		return PTR_ERR(scodec->clk_module);
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "Failed to map the registers\n");
		return PTR_ERR(base);
	}

	scodec->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "bus", base,
						   &sun8i_codec_regmap_config);
	if (IS_ERR(scodec->regmap)) {
		dev_err(&pdev->dev, "Failed to create our regmap\n");
		return PTR_ERR(scodec->regmap);
	}

	scodec->quirks = of_device_get_match_data(&pdev->dev);

	platform_set_drvdata(pdev, scodec);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = sun8i_codec_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = devm_snd_soc_register_component(&pdev->dev, &sun8i_soc_component,
				     &sun8i_codec_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register codec\n");
		goto err_suspend;
	}

	return ret;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun8i_codec_runtime_suspend(&pdev->dev);

err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int sun8i_codec_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun8i_codec_runtime_suspend(&pdev->dev);

	return 0;
}

static const struct sun8i_codec_quirks sun8i_a33_quirks = {
	.legacy_widgets	= true,
	.lrck_inversion	= true,
};

static const struct sun8i_codec_quirks sun50i_a64_quirks = {
};

static const struct of_device_id sun8i_codec_of_match[] = {
	{ .compatible = "allwinner,sun8i-a33-codec", .data = &sun8i_a33_quirks },
	{ .compatible = "allwinner,sun50i-a64-codec", .data = &sun50i_a64_quirks },
	{}
};
MODULE_DEVICE_TABLE(of, sun8i_codec_of_match);

static const struct dev_pm_ops sun8i_codec_pm_ops = {
	SET_RUNTIME_PM_OPS(sun8i_codec_runtime_suspend,
			   sun8i_codec_runtime_resume, NULL)
};

static struct platform_driver sun8i_codec_driver = {
	.driver = {
		.name = "sun8i-codec",
		.of_match_table = sun8i_codec_of_match,
		.pm = &sun8i_codec_pm_ops,
	},
	.probe = sun8i_codec_probe,
	.remove = sun8i_codec_remove,
};
module_platform_driver(sun8i_codec_driver);

MODULE_DESCRIPTION("Allwinner A33 (sun8i) codec driver");
MODULE_AUTHOR("Mylène Josserand <mylene.josserand@free-electrons.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sun8i-codec");
