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
#include <sound/tlv.h>

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
#define SUN8I_MOD_CLK_ENA_AIF2				14
#define SUN8I_MOD_CLK_ENA_AIF3				13
#define SUN8I_MOD_CLK_ENA_ADC				3
#define SUN8I_MOD_CLK_ENA_DAC				2
#define SUN8I_MOD_RST_CTL				0x014
#define SUN8I_MOD_RST_CTL_AIF1				15
#define SUN8I_MOD_RST_CTL_AIF2				14
#define SUN8I_MOD_RST_CTL_AIF3				13
#define SUN8I_MOD_RST_CTL_ADC				3
#define SUN8I_MOD_RST_CTL_DAC				2
#define SUN8I_SYS_SR_CTRL				0x018
#define SUN8I_SYS_SR_CTRL_AIF1_FS			12
#define SUN8I_SYS_SR_CTRL_AIF2_FS			8
#define SUN8I_AIF_CLK_CTRL(n)				(0x040 * (1 + (n)))
#define SUN8I_AIF_CLK_CTRL_MSTR_MOD			15
#define SUN8I_AIF_CLK_CTRL_CLK_INV			13
#define SUN8I_AIF_CLK_CTRL_BCLK_DIV			9
#define SUN8I_AIF_CLK_CTRL_LRCK_DIV			6
#define SUN8I_AIF_CLK_CTRL_WORD_SIZ			4
#define SUN8I_AIF_CLK_CTRL_DATA_FMT			2
#define SUN8I_AIF1_ADCDAT_CTRL				0x044
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_ENA		15
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_ENA		14
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_SRC		10
#define SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_SRC		8
#define SUN8I_AIF1_DACDAT_CTRL				0x048
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_ENA		15
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_ENA		14
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_SRC		10
#define SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_SRC		8
#define SUN8I_AIF1_MXR_SRC				0x04c
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF1DA0L	15
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACL	14
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_ADCL		13
#define SUN8I_AIF1_MXR_SRC_AD0L_MXR_SRC_AIF2DACR	12
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF1DA0R	11
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACR	10
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_ADCR		9
#define SUN8I_AIF1_MXR_SRC_AD0R_MXR_SRC_AIF2DACL	8
#define SUN8I_AIF1_VOL_CTRL1				0x050
#define SUN8I_AIF1_VOL_CTRL1_AD0L_VOL			8
#define SUN8I_AIF1_VOL_CTRL1_AD0R_VOL			0
#define SUN8I_AIF1_VOL_CTRL3				0x058
#define SUN8I_AIF1_VOL_CTRL3_DA0L_VOL			8
#define SUN8I_AIF1_VOL_CTRL3_DA0R_VOL			0
#define SUN8I_AIF2_ADCDAT_CTRL				0x084
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_ENA		15
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_ENA		14
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_SRC		10
#define SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_SRC		8
#define SUN8I_AIF2_DACDAT_CTRL				0x088
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_ENA		15
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_ENA		14
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_SRC		10
#define SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_SRC		8
#define SUN8I_AIF2_MXR_SRC				0x08c
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA0L	15
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA1L	14
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF2DACR	13
#define SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_ADCL		12
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA0R	11
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA1R	10
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF2DACL	9
#define SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_ADCR		8
#define SUN8I_AIF2_VOL_CTRL1				0x090
#define SUN8I_AIF2_VOL_CTRL1_ADCL_VOL			8
#define SUN8I_AIF2_VOL_CTRL1_ADCR_VOL			0
#define SUN8I_AIF2_VOL_CTRL2				0x098
#define SUN8I_AIF2_VOL_CTRL2_DACL_VOL			8
#define SUN8I_AIF2_VOL_CTRL2_DACR_VOL			0
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLK_SRC_AIF1		(0x0 << 0)
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLK_SRC_AIF2		(0x1 << 0)
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLK_SRC_AIF1CLK	(0x2 << 0)
#define SUN8I_AIF3_PATH_CTRL				0x0cc
#define SUN8I_AIF3_PATH_CTRL_AIF3_ADC_SRC		10
#define SUN8I_AIF3_PATH_CTRL_AIF2_DAC_SRC		8
#define SUN8I_AIF3_PATH_CTRL_AIF3_PINS_TRI		7
#define SUN8I_ADC_DIG_CTRL				0x100
#define SUN8I_ADC_DIG_CTRL_ENAD				15
#define SUN8I_ADC_DIG_CTRL_ADOUT_DTS			2
#define SUN8I_ADC_DIG_CTRL_ADOUT_DLY			1
#define SUN8I_ADC_VOL_CTRL				0x104
#define SUN8I_ADC_VOL_CTRL_ADCL_VOL			8
#define SUN8I_ADC_VOL_CTRL_ADCR_VOL			0
#define SUN8I_DAC_DIG_CTRL				0x120
#define SUN8I_DAC_DIG_CTRL_ENDA				15
#define SUN8I_DAC_VOL_CTRL				0x124
#define SUN8I_DAC_VOL_CTRL_DACL_VOL			8
#define SUN8I_DAC_VOL_CTRL_DACR_VOL			0
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
#define SUN8I_AIF_CLK_CTRL_CLK_INV_MASK		GENMASK(14, 13)
#define SUN8I_AIF_CLK_CTRL_BCLK_DIV_MASK	GENMASK(12, 9)
#define SUN8I_AIF_CLK_CTRL_LRCK_DIV_MASK	GENMASK(8, 6)
#define SUN8I_AIF_CLK_CTRL_WORD_SIZ_MASK	GENMASK(5, 4)
#define SUN8I_AIF_CLK_CTRL_DATA_FMT_MASK	GENMASK(3, 2)
#define SUN8I_AIF3_CLK_CTRL_AIF3_CLK_SRC_MASK	GENMASK(1, 0)

#define SUN8I_CODEC_PASSTHROUGH_SAMPLE_RATE 48000

#define SUN8I_CODEC_PCM_FORMATS	(SNDRV_PCM_FMTBIT_S8     |\
				 SNDRV_PCM_FMTBIT_S16_LE |\
				 SNDRV_PCM_FMTBIT_S20_LE |\
				 SNDRV_PCM_FMTBIT_S24_LE |\
				 SNDRV_PCM_FMTBIT_S20_3LE|\
				 SNDRV_PCM_FMTBIT_S24_3LE)

#define SUN8I_CODEC_PCM_RATES	(SNDRV_PCM_RATE_8000_48000|\
				 SNDRV_PCM_RATE_88200     |\
				 SNDRV_PCM_RATE_96000     |\
				 SNDRV_PCM_RATE_176400    |\
				 SNDRV_PCM_RATE_192000    |\
				 SNDRV_PCM_RATE_KNOT)

enum {
	SUN8I_CODEC_AIF1,
	SUN8I_CODEC_AIF2,
	SUN8I_CODEC_AIF3,
	SUN8I_CODEC_NAIFS
};

struct sun8i_codec_aif {
	unsigned int	lrck_div_order;
	unsigned int	sample_rate;
	unsigned int	slots;
	unsigned int	slot_width;
	unsigned int	active_streams	: 2;
	unsigned int	open_streams	: 2;
};

struct sun8i_codec_quirks {
	bool legacy_widgets	: 1;
	bool lrck_inversion	: 1;
};

struct sun8i_codec {
	struct regmap			*regmap;
	struct clk			*clk_module;
	const struct sun8i_codec_quirks	*quirks;
	struct sun8i_codec_aif		aifs[SUN8I_CODEC_NAIFS];
	unsigned int			sysclk_rate;
	int				sysclk_refcnt;
};

static struct snd_soc_dai_driver sun8i_codec_dais[];

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

static int sun8i_codec_get_hw_rate(unsigned int sample_rate)
{
	switch (sample_rate) {
	case 7350:
	case 8000:
		return 0x0;
	case 11025:
		return 0x1;
	case 12000:
		return 0x2;
	case 14700:
	case 16000:
		return 0x3;
	case 22050:
		return 0x4;
	case 24000:
		return 0x5;
	case 29400:
	case 32000:
		return 0x6;
	case 44100:
		return 0x7;
	case 48000:
		return 0x8;
	case 88200:
	case 96000:
		return 0x9;
	case 176400:
	case 192000:
		return 0xa;
	default:
		return -EINVAL;
	}
}

static int sun8i_codec_update_sample_rate(struct sun8i_codec *scodec)
{
	unsigned int max_rate = 0;
	int hw_rate, i;

	for (i = SUN8I_CODEC_AIF1; i < SUN8I_CODEC_NAIFS; ++i) {
		struct sun8i_codec_aif *aif = &scodec->aifs[i];

		if (aif->active_streams)
			max_rate = max(max_rate, aif->sample_rate);
	}

	/* Set the sample rate for ADC->DAC passthrough when no AIF is active. */
	if (!max_rate)
		max_rate = SUN8I_CODEC_PASSTHROUGH_SAMPLE_RATE;

	hw_rate = sun8i_codec_get_hw_rate(max_rate);
	if (hw_rate < 0)
		return hw_rate;

	regmap_update_bits(scodec->regmap, SUN8I_SYS_SR_CTRL,
			   SUN8I_SYS_SR_CTRL_AIF1_FS_MASK,
			   hw_rate << SUN8I_SYS_SR_CTRL_AIF1_FS);

	return 0;
}

static int sun8i_codec_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct sun8i_codec *scodec = snd_soc_dai_get_drvdata(dai);
	u32 dsp_format, format, invert, value;

	/* clock masters */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC: /* Codec slave, DAI master */
		value = 0x1;
		break;
	case SND_SOC_DAIFMT_CBP_CFP: /* Codec Master, DAI slave */
		value = 0x0;
		break;
	default:
		return -EINVAL;
	}

	if (dai->id == SUN8I_CODEC_AIF3) {
		/* AIF3 only supports master mode. */
		if (value)
			return -EINVAL;

		/* Use the AIF2 BCLK and LRCK for AIF3. */
		regmap_update_bits(scodec->regmap, SUN8I_AIF_CLK_CTRL(dai->id),
				   SUN8I_AIF3_CLK_CTRL_AIF3_CLK_SRC_MASK,
				   SUN8I_AIF3_CLK_CTRL_AIF3_CLK_SRC_AIF2);
	} else {
		regmap_update_bits(scodec->regmap, SUN8I_AIF_CLK_CTRL(dai->id),
				   BIT(SUN8I_AIF_CLK_CTRL_MSTR_MOD),
				   value << SUN8I_AIF_CLK_CTRL_MSTR_MOD);
	}

	/* DAI format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format = 0x0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format = 0x1;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		format = 0x2;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		format = 0x3;
		dsp_format = 0x0; /* Set LRCK_INV to 0 */
		break;
	case SND_SOC_DAIFMT_DSP_B:
		format = 0x3;
		dsp_format = 0x1; /* Set LRCK_INV to 1 */
		break;
	default:
		return -EINVAL;
	}

	if (dai->id == SUN8I_CODEC_AIF3) {
		/* AIF3 only supports DSP mode. */
		if (format != 3)
			return -EINVAL;
	} else {
		regmap_update_bits(scodec->regmap, SUN8I_AIF_CLK_CTRL(dai->id),
				   SUN8I_AIF_CLK_CTRL_DATA_FMT_MASK,
				   format << SUN8I_AIF_CLK_CTRL_DATA_FMT);
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF: /* Normal */
		invert = 0x0;
		break;
	case SND_SOC_DAIFMT_NB_IF: /* Inverted LRCK */
		invert = 0x1;
		break;
	case SND_SOC_DAIFMT_IB_NF: /* Inverted BCLK */
		invert = 0x2;
		break;
	case SND_SOC_DAIFMT_IB_IF: /* Both inverted */
		invert = 0x3;
		break;
	default:
		return -EINVAL;
	}

	if (format == 0x3) {
		/* Inverted LRCK is not available in DSP mode. */
		if (invert & BIT(0))
			return -EINVAL;

		/* Instead, the bit selects between DSP A/B formats. */
		invert |= dsp_format;
	} else {
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
		invert ^= scodec->quirks->lrck_inversion;
	}

	regmap_update_bits(scodec->regmap, SUN8I_AIF_CLK_CTRL(dai->id),
			   SUN8I_AIF_CLK_CTRL_CLK_INV_MASK,
			   invert << SUN8I_AIF_CLK_CTRL_CLK_INV);

	return 0;
}

static int sun8i_codec_set_tdm_slot(struct snd_soc_dai *dai,
				    unsigned int tx_mask, unsigned int rx_mask,
				    int slots, int slot_width)
{
	struct sun8i_codec *scodec = snd_soc_dai_get_drvdata(dai);
	struct sun8i_codec_aif *aif = &scodec->aifs[dai->id];

	if (slot_width && !is_power_of_2(slot_width))
		return -EINVAL;

	aif->slots = slots;
	aif->slot_width = slot_width;

	return 0;
}

static const unsigned int sun8i_codec_rates[] = {
	  7350,   8000,  11025,  12000,  14700,  16000,  22050,  24000,
	 29400,  32000,  44100,  48000,  88200,  96000, 176400, 192000,
};

static const struct snd_pcm_hw_constraint_list sun8i_codec_all_rates = {
	.list	= sun8i_codec_rates,
	.count	= ARRAY_SIZE(sun8i_codec_rates),
};

static const struct snd_pcm_hw_constraint_list sun8i_codec_22M_rates = {
	.list	= sun8i_codec_rates,
	.count	= ARRAY_SIZE(sun8i_codec_rates),
	.mask	= 0x5555,
};

static const struct snd_pcm_hw_constraint_list sun8i_codec_24M_rates = {
	.list	= sun8i_codec_rates,
	.count	= ARRAY_SIZE(sun8i_codec_rates),
	.mask	= 0xaaaa,
};

static int sun8i_codec_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct sun8i_codec *scodec = snd_soc_dai_get_drvdata(dai);
	const struct snd_pcm_hw_constraint_list *list;

	/* hw_constraints is not relevant for codec2codec DAIs. */
	if (dai->id != SUN8I_CODEC_AIF1)
		return 0;

	if (!scodec->sysclk_refcnt)
		list = &sun8i_codec_all_rates;
	else if (scodec->sysclk_rate == 22579200)
		list = &sun8i_codec_22M_rates;
	else if (scodec->sysclk_rate == 24576000)
		list = &sun8i_codec_24M_rates;
	else
		return -EINVAL;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE, list);
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

static int sun8i_codec_get_bclk_div(unsigned int sysclk_rate,
				    unsigned int lrck_div_order,
				    unsigned int sample_rate)
{
	unsigned int div = sysclk_rate / sample_rate >> lrck_div_order;
	int i;

	for (i = 0; i < ARRAY_SIZE(sun8i_codec_bclk_div); i++) {
		const struct sun8i_codec_clk_div *bdiv = &sun8i_codec_bclk_div[i];

		if (bdiv->div == div)
			return bdiv->val;
	}

	return -EINVAL;
}

static int sun8i_codec_get_lrck_div_order(unsigned int slots,
					  unsigned int slot_width)
{
	unsigned int div = slots * slot_width;

	if (div < 16 || div > 256)
		return -EINVAL;

	return order_base_2(div);
}

static unsigned int sun8i_codec_get_sysclk_rate(unsigned int sample_rate)
{
	return (sample_rate % 4000) ? 22579200 : 24576000;
}

static int sun8i_codec_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct sun8i_codec *scodec = snd_soc_dai_get_drvdata(dai);
	struct sun8i_codec_aif *aif = &scodec->aifs[dai->id];
	unsigned int sample_rate = params_rate(params);
	unsigned int slots = aif->slots ?: params_channels(params);
	unsigned int slot_width = aif->slot_width ?: params_width(params);
	unsigned int sysclk_rate = sun8i_codec_get_sysclk_rate(sample_rate);
	int bclk_div, lrck_div_order, ret, word_size;
	u32 clk_reg;

	/* word size */
	switch (params_width(params)) {
	case 8:
		word_size = 0x0;
		break;
	case 16:
		word_size = 0x1;
		break;
	case 20:
		word_size = 0x2;
		break;
	case 24:
		word_size = 0x3;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(scodec->regmap, SUN8I_AIF_CLK_CTRL(dai->id),
			   SUN8I_AIF_CLK_CTRL_WORD_SIZ_MASK,
			   word_size << SUN8I_AIF_CLK_CTRL_WORD_SIZ);

	/* LRCK divider (BCLK/LRCK ratio) */
	lrck_div_order = sun8i_codec_get_lrck_div_order(slots, slot_width);
	if (lrck_div_order < 0)
		return lrck_div_order;

	if (dai->id == SUN8I_CODEC_AIF2 || dai->id == SUN8I_CODEC_AIF3) {
		/* AIF2 and AIF3 share AIF2's BCLK and LRCK generation circuitry. */
		int partner = (SUN8I_CODEC_AIF2 + SUN8I_CODEC_AIF3) - dai->id;
		const struct sun8i_codec_aif *partner_aif = &scodec->aifs[partner];
		const char *partner_name = sun8i_codec_dais[partner].name;

		if (partner_aif->open_streams &&
		    (lrck_div_order != partner_aif->lrck_div_order ||
		     sample_rate != partner_aif->sample_rate)) {
			dev_err(dai->dev,
				"%s sample and bit rates must match %s when both are used\n",
				dai->name, partner_name);
			return -EBUSY;
		}

		clk_reg = SUN8I_AIF_CLK_CTRL(SUN8I_CODEC_AIF2);
	} else {
		clk_reg = SUN8I_AIF_CLK_CTRL(dai->id);
	}

	regmap_update_bits(scodec->regmap, clk_reg,
			   SUN8I_AIF_CLK_CTRL_LRCK_DIV_MASK,
			   (lrck_div_order - 4) << SUN8I_AIF_CLK_CTRL_LRCK_DIV);

	/* BCLK divider (SYSCLK/BCLK ratio) */
	bclk_div = sun8i_codec_get_bclk_div(sysclk_rate, lrck_div_order, sample_rate);
	if (bclk_div < 0)
		return bclk_div;

	regmap_update_bits(scodec->regmap, clk_reg,
			   SUN8I_AIF_CLK_CTRL_BCLK_DIV_MASK,
			   bclk_div << SUN8I_AIF_CLK_CTRL_BCLK_DIV);

	/*
	 * SYSCLK rate
	 *
	 * Clock rate protection is reference counted; but hw_params may be
	 * called many times per substream, without matching calls to hw_free.
	 * Protect the clock rate once per AIF, on the first hw_params call
	 * for the first substream. clk_set_rate() will allow clock rate
	 * changes on subsequent calls if only one AIF has open streams.
	 */
	ret = (aif->open_streams ? clk_set_rate : clk_set_rate_exclusive)(scodec->clk_module,
									  sysclk_rate);
	if (ret == -EBUSY)
		dev_err(dai->dev,
			"%s sample rate (%u Hz) conflicts with other audio streams\n",
			dai->name, sample_rate);
	if (ret < 0)
		return ret;

	if (!aif->open_streams)
		scodec->sysclk_refcnt++;
	scodec->sysclk_rate = sysclk_rate;

	aif->lrck_div_order = lrck_div_order;
	aif->sample_rate = sample_rate;
	aif->open_streams |= BIT(substream->stream);

	return sun8i_codec_update_sample_rate(scodec);
}

static int sun8i_codec_hw_free(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct sun8i_codec *scodec = snd_soc_dai_get_drvdata(dai);
	struct sun8i_codec_aif *aif = &scodec->aifs[dai->id];

	/* Drop references when the last substream for the AIF is freed. */
	if (aif->open_streams != BIT(substream->stream))
		goto done;

	clk_rate_exclusive_put(scodec->clk_module);
	scodec->sysclk_refcnt--;
	aif->lrck_div_order = 0;
	aif->sample_rate = 0;

done:
	aif->open_streams &= ~BIT(substream->stream);
	return 0;
}

static const struct snd_soc_dai_ops sun8i_codec_dai_ops = {
	.set_fmt	= sun8i_codec_set_fmt,
	.set_tdm_slot	= sun8i_codec_set_tdm_slot,
	.startup	= sun8i_codec_startup,
	.hw_params	= sun8i_codec_hw_params,
	.hw_free	= sun8i_codec_hw_free,
};

static struct snd_soc_dai_driver sun8i_codec_dais[] = {
	{
		.name	= "sun8i-codec-aif1",
		.id	= SUN8I_CODEC_AIF1,
		.ops	= &sun8i_codec_dai_ops,
		/* capture capabilities */
		.capture = {
			.stream_name	= "AIF1 Capture",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SUN8I_CODEC_PCM_RATES,
			.formats	= SUN8I_CODEC_PCM_FORMATS,
			.sig_bits	= 24,
		},
		/* playback capabilities */
		.playback = {
			.stream_name	= "AIF1 Playback",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SUN8I_CODEC_PCM_RATES,
			.formats	= SUN8I_CODEC_PCM_FORMATS,
		},
		.symmetric_rate		= true,
		.symmetric_channels	= true,
		.symmetric_sample_bits	= true,
	},
	{
		.name	= "sun8i-codec-aif2",
		.id	= SUN8I_CODEC_AIF2,
		.ops	= &sun8i_codec_dai_ops,
		/* capture capabilities */
		.capture = {
			.stream_name	= "AIF2 Capture",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SUN8I_CODEC_PCM_RATES,
			.formats	= SUN8I_CODEC_PCM_FORMATS,
			.sig_bits	= 24,
		},
		/* playback capabilities */
		.playback = {
			.stream_name	= "AIF2 Playback",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SUN8I_CODEC_PCM_RATES,
			.formats	= SUN8I_CODEC_PCM_FORMATS,
		},
		.symmetric_rate		= true,
		.symmetric_channels	= true,
		.symmetric_sample_bits	= true,
	},
	{
		.name	= "sun8i-codec-aif3",
		.id	= SUN8I_CODEC_AIF3,
		.ops	= &sun8i_codec_dai_ops,
		/* capture capabilities */
		.capture = {
			.stream_name	= "AIF3 Capture",
			.channels_min	= 1,
			.channels_max	= 1,
			.rates		= SUN8I_CODEC_PCM_RATES,
			.formats	= SUN8I_CODEC_PCM_FORMATS,
			.sig_bits	= 24,
		},
		/* playback capabilities */
		.playback = {
			.stream_name	= "AIF3 Playback",
			.channels_min	= 1,
			.channels_max	= 1,
			.rates		= SUN8I_CODEC_PCM_RATES,
			.formats	= SUN8I_CODEC_PCM_FORMATS,
		},
		.symmetric_rate		= true,
		.symmetric_channels	= true,
		.symmetric_sample_bits	= true,
	},
};

static const DECLARE_TLV_DB_SCALE(sun8i_codec_vol_scale, -12000, 75, 1);

static const struct snd_kcontrol_new sun8i_codec_controls[] = {
	SOC_DOUBLE_TLV("AIF1 AD0 Capture Volume",
		       SUN8I_AIF1_VOL_CTRL1,
		       SUN8I_AIF1_VOL_CTRL1_AD0L_VOL,
		       SUN8I_AIF1_VOL_CTRL1_AD0R_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("AIF1 DA0 Playback Volume",
		       SUN8I_AIF1_VOL_CTRL3,
		       SUN8I_AIF1_VOL_CTRL3_DA0L_VOL,
		       SUN8I_AIF1_VOL_CTRL3_DA0R_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("AIF2 ADC Capture Volume",
		       SUN8I_AIF2_VOL_CTRL1,
		       SUN8I_AIF2_VOL_CTRL1_ADCL_VOL,
		       SUN8I_AIF2_VOL_CTRL1_ADCR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("AIF2 DAC Playback Volume",
		       SUN8I_AIF2_VOL_CTRL2,
		       SUN8I_AIF2_VOL_CTRL2_DACL_VOL,
		       SUN8I_AIF2_VOL_CTRL2_DACR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("ADC Capture Volume",
		       SUN8I_ADC_VOL_CTRL,
		       SUN8I_ADC_VOL_CTRL_ADCL_VOL,
		       SUN8I_ADC_VOL_CTRL_ADCR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
	SOC_DOUBLE_TLV("DAC Playback Volume",
		       SUN8I_DAC_VOL_CTRL,
		       SUN8I_DAC_VOL_CTRL_DACL_VOL,
		       SUN8I_DAC_VOL_CTRL_DACR_VOL,
		       0xc0, 0, sun8i_codec_vol_scale),
};

static int sun8i_codec_aif_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct sun8i_codec *scodec = snd_soc_component_get_drvdata(component);
	struct sun8i_codec_aif *aif = &scodec->aifs[w->sname[3] - '1'];
	int stream = w->id == snd_soc_dapm_aif_out;

	if (SND_SOC_DAPM_EVENT_ON(event))
		aif->active_streams |= BIT(stream);
	else
		aif->active_streams &= ~BIT(stream);

	return sun8i_codec_update_sample_rate(scodec);
}

static const char *const sun8i_aif_stereo_mux_enum_values[] = {
	"Stereo", "Reverse Stereo", "Sum Mono", "Mix Mono"
};

static SOC_ENUM_DOUBLE_DECL(sun8i_aif1_ad0_stereo_mux_enum,
			    SUN8I_AIF1_ADCDAT_CTRL,
			    SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_SRC,
			    SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_SRC,
			    sun8i_aif_stereo_mux_enum_values);

static const struct snd_kcontrol_new sun8i_aif1_ad0_stereo_mux_control =
	SOC_DAPM_ENUM("AIF1 AD0 Stereo Capture Route",
		      sun8i_aif1_ad0_stereo_mux_enum);

static SOC_ENUM_DOUBLE_DECL(sun8i_aif2_adc_stereo_mux_enum,
			    SUN8I_AIF2_ADCDAT_CTRL,
			    SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_SRC,
			    SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_SRC,
			    sun8i_aif_stereo_mux_enum_values);

static const struct snd_kcontrol_new sun8i_aif2_adc_stereo_mux_control =
	SOC_DAPM_ENUM("AIF2 ADC Stereo Capture Route",
		      sun8i_aif2_adc_stereo_mux_enum);

static const char *const sun8i_aif3_adc_mux_enum_values[] = {
	"None", "AIF2 ADCL", "AIF2 ADCR"
};

static SOC_ENUM_SINGLE_DECL(sun8i_aif3_adc_mux_enum,
			    SUN8I_AIF3_PATH_CTRL,
			    SUN8I_AIF3_PATH_CTRL_AIF3_ADC_SRC,
			    sun8i_aif3_adc_mux_enum_values);

static const struct snd_kcontrol_new sun8i_aif3_adc_mux_control =
	SOC_DAPM_ENUM("AIF3 ADC Source Capture Route",
		      sun8i_aif3_adc_mux_enum);

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

static const struct snd_kcontrol_new sun8i_aif2_adc_mixer_controls[] = {
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer AIF1 DA0 Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA0L,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA0R, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer AIF1 DA1 Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF1DA1L,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF1DA1R, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer AIF2 DAC Rev Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_AIF2DACR,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_AIF2DACL, 1, 0),
	SOC_DAPM_DOUBLE("AIF2 ADC Mixer ADC Capture Switch",
			SUN8I_AIF2_MXR_SRC,
			SUN8I_AIF2_MXR_SRC_ADCL_MXR_SRC_ADCL,
			SUN8I_AIF2_MXR_SRC_ADCR_MXR_SRC_ADCR, 1, 0),
};

static const char *const sun8i_aif2_dac_mux_enum_values[] = {
	"AIF2", "AIF3+2", "AIF2+3"
};

static SOC_ENUM_SINGLE_DECL(sun8i_aif2_dac_mux_enum,
			    SUN8I_AIF3_PATH_CTRL,
			    SUN8I_AIF3_PATH_CTRL_AIF2_DAC_SRC,
			    sun8i_aif2_dac_mux_enum_values);

static const struct snd_kcontrol_new sun8i_aif2_dac_mux_control =
	SOC_DAPM_ENUM("AIF2 DAC Source Playback Route",
		      sun8i_aif2_dac_mux_enum);

static SOC_ENUM_DOUBLE_DECL(sun8i_aif1_da0_stereo_mux_enum,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_SRC,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_SRC,
			    sun8i_aif_stereo_mux_enum_values);

static const struct snd_kcontrol_new sun8i_aif1_da0_stereo_mux_control =
	SOC_DAPM_ENUM("AIF1 DA0 Stereo Playback Route",
		      sun8i_aif1_da0_stereo_mux_enum);

static SOC_ENUM_DOUBLE_DECL(sun8i_aif2_dac_stereo_mux_enum,
			    SUN8I_AIF2_DACDAT_CTRL,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_SRC,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_SRC,
			    sun8i_aif_stereo_mux_enum_values);

static const struct snd_kcontrol_new sun8i_aif2_dac_stereo_mux_control =
	SOC_DAPM_ENUM("AIF2 DAC Stereo Playback Route",
		      sun8i_aif2_dac_stereo_mux_enum);

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
	SND_SOC_DAPM_SUPPLY("AIF2CLK",
			    SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_AIF2CLK_ENA, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SYSCLK",
			    SUN8I_SYSCLK_CTL,
			    SUN8I_SYSCLK_CTL_SYSCLK_ENA, 0, NULL, 0),

	/* Module Clocks */
	SND_SOC_DAPM_SUPPLY("CLK AIF1",
			    SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK AIF2",
			    SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK AIF3",
			    SUN8I_MOD_CLK_ENA,
			    SUN8I_MOD_CLK_ENA_AIF3, 0, NULL, 0),
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
	SND_SOC_DAPM_SUPPLY("RST AIF2",
			    SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_AIF2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RST AIF3",
			    SUN8I_MOD_RST_CTL,
			    SUN8I_MOD_RST_CTL_AIF3, 0, NULL, 0),
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
	SND_SOC_DAPM_AIF_OUT_E("AIF1 AD0L", "AIF1 Capture", 0,
			       SUN8I_AIF1_ADCDAT_CTRL,
			       SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0L_ENA, 0,
			       sun8i_codec_aif_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT("AIF1 AD0R", "AIF1 Capture", 1,
			     SUN8I_AIF1_ADCDAT_CTRL,
			     SUN8I_AIF1_ADCDAT_CTRL_AIF1_AD0R_ENA, 0),

	SND_SOC_DAPM_AIF_OUT_E("AIF2 ADCL", "AIF2 Capture", 0,
			       SUN8I_AIF2_ADCDAT_CTRL,
			       SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCL_ENA, 0,
			       sun8i_codec_aif_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT("AIF2 ADCR", "AIF2 Capture", 1,
			     SUN8I_AIF2_ADCDAT_CTRL,
			     SUN8I_AIF2_ADCDAT_CTRL_AIF2_ADCR_ENA, 0),

	SND_SOC_DAPM_AIF_OUT_E("AIF3 ADC", "AIF3 Capture", 0,
			       SND_SOC_NOPM, 0, 0,
			       sun8i_codec_aif_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* AIF "ADC" Mono/Stereo Muxes */
	SND_SOC_DAPM_MUX("AIF1 AD0L Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_ad0_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF1 AD0R Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_ad0_stereo_mux_control),

	SND_SOC_DAPM_MUX("AIF2 ADCL Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_adc_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF2 ADCR Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_adc_stereo_mux_control),

	/* AIF "ADC" Output Muxes */
	SND_SOC_DAPM_MUX("AIF3 ADC Source Capture Route", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif3_adc_mux_control),

	/* AIF "ADC" Mixers */
	SOC_MIXER_ARRAY("AIF1 AD0L Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif1_ad0_mixer_controls),
	SOC_MIXER_ARRAY("AIF1 AD0R Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif1_ad0_mixer_controls),

	SOC_MIXER_ARRAY("AIF2 ADCL Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif2_adc_mixer_controls),
	SOC_MIXER_ARRAY("AIF2 ADCR Mixer", SND_SOC_NOPM, 0, 0,
			sun8i_aif2_adc_mixer_controls),

	/* AIF "DAC" Input Muxes */
	SND_SOC_DAPM_MUX("AIF2 DACL Source", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_mux_control),
	SND_SOC_DAPM_MUX("AIF2 DACR Source", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_mux_control),

	/* AIF "DAC" Mono/Stereo Muxes */
	SND_SOC_DAPM_MUX("AIF1 DA0L Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_da0_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF1 DA0R Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif1_da0_stereo_mux_control),

	SND_SOC_DAPM_MUX("AIF2 DACL Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_stereo_mux_control),
	SND_SOC_DAPM_MUX("AIF2 DACR Stereo Mux", SND_SOC_NOPM, 0, 0,
			 &sun8i_aif2_dac_stereo_mux_control),

	/* AIF "DAC" Inputs */
	SND_SOC_DAPM_AIF_IN_E("AIF1 DA0L", "AIF1 Playback", 0,
			      SUN8I_AIF1_DACDAT_CTRL,
			      SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0L_ENA, 0,
			      sun8i_codec_aif_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN("AIF1 DA0R", "AIF1 Playback", 1,
			    SUN8I_AIF1_DACDAT_CTRL,
			    SUN8I_AIF1_DACDAT_CTRL_AIF1_DA0R_ENA, 0),

	SND_SOC_DAPM_AIF_IN_E("AIF2 DACL", "AIF2 Playback", 0,
			      SUN8I_AIF2_DACDAT_CTRL,
			      SUN8I_AIF2_DACDAT_CTRL_AIF2_DACL_ENA, 0,
			      sun8i_codec_aif_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN("AIF2 DACR", "AIF2 Playback", 1,
			    SUN8I_AIF2_DACDAT_CTRL,
			    SUN8I_AIF2_DACDAT_CTRL_AIF2_DACR_ENA, 0),

	SND_SOC_DAPM_AIF_IN_E("AIF3 DAC", "AIF3 Playback", 0,
			      SND_SOC_NOPM, 0, 0,
			      sun8i_codec_aif_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

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

	{ "CLK AIF2", NULL, "AIF2CLK" },
	{ "CLK AIF2", NULL, "SYSCLK" },
	{ "RST AIF2", NULL, "CLK AIF2" },
	{ "AIF2 ADCL", NULL, "RST AIF2" },
	{ "AIF2 ADCR", NULL, "RST AIF2" },
	{ "AIF2 DACL", NULL, "RST AIF2" },
	{ "AIF2 DACR", NULL, "RST AIF2" },

	{ "CLK AIF3", NULL, "AIF1CLK" },
	{ "CLK AIF3", NULL, "SYSCLK" },
	{ "RST AIF3", NULL, "CLK AIF3" },
	{ "AIF3 ADC", NULL, "RST AIF3" },
	{ "AIF3 DAC", NULL, "RST AIF3" },

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
	{ "AIF1 AD0L", NULL, "AIF1 AD0L Stereo Mux" },
	{ "AIF1 AD0R", NULL, "AIF1 AD0R Stereo Mux" },

	{ "AIF2 ADCL", NULL, "AIF2 ADCL Stereo Mux" },
	{ "AIF2 ADCR", NULL, "AIF2 ADCR Stereo Mux" },

	{ "AIF3 ADC", NULL, "AIF3 ADC Source Capture Route" },

	/* AIF "ADC" Mono/Stereo Mux Routes */
	{ "AIF1 AD0L Stereo Mux", "Stereo", "AIF1 AD0L Mixer" },
	{ "AIF1 AD0L Stereo Mux", "Reverse Stereo", "AIF1 AD0R Mixer" },
	{ "AIF1 AD0L Stereo Mux", "Sum Mono", "AIF1 AD0L Mixer" },
	{ "AIF1 AD0L Stereo Mux", "Sum Mono", "AIF1 AD0R Mixer" },
	{ "AIF1 AD0L Stereo Mux", "Mix Mono", "AIF1 AD0L Mixer" },
	{ "AIF1 AD0L Stereo Mux", "Mix Mono", "AIF1 AD0R Mixer" },

	{ "AIF1 AD0R Stereo Mux", "Stereo", "AIF1 AD0R Mixer" },
	{ "AIF1 AD0R Stereo Mux", "Reverse Stereo", "AIF1 AD0L Mixer" },
	{ "AIF1 AD0R Stereo Mux", "Sum Mono", "AIF1 AD0L Mixer" },
	{ "AIF1 AD0R Stereo Mux", "Sum Mono", "AIF1 AD0R Mixer" },
	{ "AIF1 AD0R Stereo Mux", "Mix Mono", "AIF1 AD0L Mixer" },
	{ "AIF1 AD0R Stereo Mux", "Mix Mono", "AIF1 AD0R Mixer" },

	{ "AIF2 ADCL Stereo Mux", "Stereo", "AIF2 ADCL Mixer" },
	{ "AIF2 ADCL Stereo Mux", "Reverse Stereo", "AIF2 ADCR Mixer" },
	{ "AIF2 ADCL Stereo Mux", "Sum Mono", "AIF2 ADCL Mixer" },
	{ "AIF2 ADCL Stereo Mux", "Sum Mono", "AIF2 ADCR Mixer" },
	{ "AIF2 ADCL Stereo Mux", "Mix Mono", "AIF2 ADCL Mixer" },
	{ "AIF2 ADCL Stereo Mux", "Mix Mono", "AIF2 ADCR Mixer" },

	{ "AIF2 ADCR Stereo Mux", "Stereo", "AIF2 ADCR Mixer" },
	{ "AIF2 ADCR Stereo Mux", "Reverse Stereo", "AIF2 ADCL Mixer" },
	{ "AIF2 ADCR Stereo Mux", "Sum Mono", "AIF2 ADCL Mixer" },
	{ "AIF2 ADCR Stereo Mux", "Sum Mono", "AIF2 ADCR Mixer" },
	{ "AIF2 ADCR Stereo Mux", "Mix Mono", "AIF2 ADCL Mixer" },
	{ "AIF2 ADCR Stereo Mux", "Mix Mono", "AIF2 ADCR Mixer" },

	/* AIF "ADC" Output Mux Routes */
	{ "AIF3 ADC Source Capture Route", "AIF2 ADCL", "AIF2 ADCL Mixer" },
	{ "AIF3 ADC Source Capture Route", "AIF2 ADCR", "AIF2 ADCR Mixer" },

	/* AIF "ADC" Mixer Routes */
	{ "AIF1 AD0L Mixer", "AIF1 Slot 0 Digital ADC Capture Switch", "AIF1 DA0L Stereo Mux" },
	{ "AIF1 AD0L Mixer", "AIF2 Digital ADC Capture Switch", "AIF2 DACL Source" },
	{ "AIF1 AD0L Mixer", "AIF1 Data Digital ADC Capture Switch", "ADCL" },
	{ "AIF1 AD0L Mixer", "AIF2 Inv Digital ADC Capture Switch", "AIF2 DACR Source" },

	{ "AIF1 AD0R Mixer", "AIF1 Slot 0 Digital ADC Capture Switch", "AIF1 DA0R Stereo Mux" },
	{ "AIF1 AD0R Mixer", "AIF2 Digital ADC Capture Switch", "AIF2 DACR Source" },
	{ "AIF1 AD0R Mixer", "AIF1 Data Digital ADC Capture Switch", "ADCR" },
	{ "AIF1 AD0R Mixer", "AIF2 Inv Digital ADC Capture Switch", "AIF2 DACL Source" },

	{ "AIF2 ADCL Mixer", "AIF2 ADC Mixer AIF1 DA0 Capture Switch", "AIF1 DA0L Stereo Mux" },
	{ "AIF2 ADCL Mixer", "AIF2 ADC Mixer AIF2 DAC Rev Capture Switch", "AIF2 DACR Source" },
	{ "AIF2 ADCL Mixer", "AIF2 ADC Mixer ADC Capture Switch", "ADCL" },

	{ "AIF2 ADCR Mixer", "AIF2 ADC Mixer AIF1 DA0 Capture Switch", "AIF1 DA0R Stereo Mux" },
	{ "AIF2 ADCR Mixer", "AIF2 ADC Mixer AIF2 DAC Rev Capture Switch", "AIF2 DACL Source" },
	{ "AIF2 ADCR Mixer", "AIF2 ADC Mixer ADC Capture Switch", "ADCR" },

	/* AIF "DAC" Input Mux Routes */
	{ "AIF2 DACL Source", "AIF2", "AIF2 DACL Stereo Mux" },
	{ "AIF2 DACL Source", "AIF3+2", "AIF3 DAC" },
	{ "AIF2 DACL Source", "AIF2+3", "AIF2 DACL Stereo Mux" },

	{ "AIF2 DACR Source", "AIF2", "AIF2 DACR Stereo Mux" },
	{ "AIF2 DACR Source", "AIF3+2", "AIF2 DACR Stereo Mux" },
	{ "AIF2 DACR Source", "AIF2+3", "AIF3 DAC" },

	/* AIF "DAC" Mono/Stereo Mux Routes */
	{ "AIF1 DA0L Stereo Mux", "Stereo", "AIF1 DA0L" },
	{ "AIF1 DA0L Stereo Mux", "Reverse Stereo", "AIF1 DA0R" },
	{ "AIF1 DA0L Stereo Mux", "Sum Mono", "AIF1 DA0L" },
	{ "AIF1 DA0L Stereo Mux", "Sum Mono", "AIF1 DA0R" },
	{ "AIF1 DA0L Stereo Mux", "Mix Mono", "AIF1 DA0L" },
	{ "AIF1 DA0L Stereo Mux", "Mix Mono", "AIF1 DA0R" },

	{ "AIF1 DA0R Stereo Mux", "Stereo", "AIF1 DA0R" },
	{ "AIF1 DA0R Stereo Mux", "Reverse Stereo", "AIF1 DA0L" },
	{ "AIF1 DA0R Stereo Mux", "Sum Mono", "AIF1 DA0L" },
	{ "AIF1 DA0R Stereo Mux", "Sum Mono", "AIF1 DA0R" },
	{ "AIF1 DA0R Stereo Mux", "Mix Mono", "AIF1 DA0L" },
	{ "AIF1 DA0R Stereo Mux", "Mix Mono", "AIF1 DA0R" },

	{ "AIF2 DACL Stereo Mux", "Stereo", "AIF2 DACL" },
	{ "AIF2 DACL Stereo Mux", "Reverse Stereo", "AIF2 DACR" },
	{ "AIF2 DACL Stereo Mux", "Sum Mono", "AIF2 DACL" },
	{ "AIF2 DACL Stereo Mux", "Sum Mono", "AIF2 DACR" },
	{ "AIF2 DACL Stereo Mux", "Mix Mono", "AIF2 DACL" },
	{ "AIF2 DACL Stereo Mux", "Mix Mono", "AIF2 DACR" },

	{ "AIF2 DACR Stereo Mux", "Stereo", "AIF2 DACR" },
	{ "AIF2 DACR Stereo Mux", "Reverse Stereo", "AIF2 DACL" },
	{ "AIF2 DACR Stereo Mux", "Sum Mono", "AIF2 DACL" },
	{ "AIF2 DACR Stereo Mux", "Sum Mono", "AIF2 DACR" },
	{ "AIF2 DACR Stereo Mux", "Mix Mono", "AIF2 DACL" },
	{ "AIF2 DACR Stereo Mux", "Mix Mono", "AIF2 DACR" },

	/* DAC Output Routes */
	{ "DACL", NULL, "DACL Mixer" },
	{ "DACR", NULL, "DACR Mixer" },

	/* DAC Mixer Routes */
	{ "DACL Mixer", "AIF1 Slot 0 Digital DAC Playback Switch", "AIF1 DA0L Stereo Mux" },
	{ "DACL Mixer", "AIF2 Digital DAC Playback Switch", "AIF2 DACL Source" },
	{ "DACL Mixer", "ADC Digital DAC Playback Switch", "ADCL" },

	{ "DACR Mixer", "AIF1 Slot 0 Digital DAC Playback Switch", "AIF1 DA0R Stereo Mux" },
	{ "DACR Mixer", "AIF2 Digital DAC Playback Switch", "AIF2 DACR Source" },
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

	/* Program the default sample rate. */
	sun8i_codec_update_sample_rate(scodec);

	return 0;
}

static const struct snd_soc_component_driver sun8i_soc_component = {
	.controls		= sun8i_codec_controls,
	.num_controls		= ARRAY_SIZE(sun8i_codec_controls),
	.dapm_widgets		= sun8i_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sun8i_codec_dapm_widgets),
	.dapm_routes		= sun8i_codec_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(sun8i_codec_dapm_routes),
	.probe			= sun8i_codec_component_probe,
	.idle_bias_on		= 1,
	.endianness		= 1,
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
					      sun8i_codec_dais,
					      ARRAY_SIZE(sun8i_codec_dais));
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
