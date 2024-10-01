// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Andrea Venturi
 * Andrea Venturi <be17068@iperbole.bo.it>
 *
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#define SUN4I_I2S_CTRL_REG		0x00
#define SUN4I_I2S_CTRL_SDO_EN_MASK		GENMASK(11, 8)
#define SUN4I_I2S_CTRL_SDO_EN(sdo)			BIT(8 + (sdo))
#define SUN4I_I2S_CTRL_MODE_MASK		BIT(5)
#define SUN4I_I2S_CTRL_MODE_SLAVE			(1 << 5)
#define SUN4I_I2S_CTRL_MODE_MASTER			(0 << 5)
#define SUN4I_I2S_CTRL_TX_EN			BIT(2)
#define SUN4I_I2S_CTRL_RX_EN			BIT(1)
#define SUN4I_I2S_CTRL_GL_EN			BIT(0)

#define SUN4I_I2S_FMT0_REG		0x04
#define SUN4I_I2S_FMT0_LRCLK_POLARITY_MASK	BIT(7)
#define SUN4I_I2S_FMT0_LRCLK_POLARITY_INVERTED		(1 << 7)
#define SUN4I_I2S_FMT0_LRCLK_POLARITY_NORMAL		(0 << 7)
#define SUN4I_I2S_FMT0_BCLK_POLARITY_MASK	BIT(6)
#define SUN4I_I2S_FMT0_BCLK_POLARITY_INVERTED		(1 << 6)
#define SUN4I_I2S_FMT0_BCLK_POLARITY_NORMAL		(0 << 6)
#define SUN4I_I2S_FMT0_SR_MASK			GENMASK(5, 4)
#define SUN4I_I2S_FMT0_SR(sr)				((sr) << 4)
#define SUN4I_I2S_FMT0_WSS_MASK			GENMASK(3, 2)
#define SUN4I_I2S_FMT0_WSS(wss)				((wss) << 2)
#define SUN4I_I2S_FMT0_FMT_MASK			GENMASK(1, 0)
#define SUN4I_I2S_FMT0_FMT_RIGHT_J			(2 << 0)
#define SUN4I_I2S_FMT0_FMT_LEFT_J			(1 << 0)
#define SUN4I_I2S_FMT0_FMT_I2S				(0 << 0)

#define SUN4I_I2S_FMT1_REG		0x08
#define SUN4I_I2S_FMT1_REG_SEXT_MASK		BIT(8)
#define SUN4I_I2S_FMT1_REG_SEXT(sext)			((sext) << 8)

#define SUN4I_I2S_FIFO_TX_REG		0x0c
#define SUN4I_I2S_FIFO_RX_REG		0x10

#define SUN4I_I2S_FIFO_CTRL_REG		0x14
#define SUN4I_I2S_FIFO_CTRL_FLUSH_TX		BIT(25)
#define SUN4I_I2S_FIFO_CTRL_FLUSH_RX		BIT(24)
#define SUN4I_I2S_FIFO_CTRL_TX_MODE_MASK	BIT(2)
#define SUN4I_I2S_FIFO_CTRL_TX_MODE(mode)		((mode) << 2)
#define SUN4I_I2S_FIFO_CTRL_RX_MODE_MASK	GENMASK(1, 0)
#define SUN4I_I2S_FIFO_CTRL_RX_MODE(mode)		(mode)

#define SUN4I_I2S_FIFO_STA_REG		0x18

#define SUN4I_I2S_DMA_INT_CTRL_REG	0x1c
#define SUN4I_I2S_DMA_INT_CTRL_TX_DRQ_EN	BIT(7)
#define SUN4I_I2S_DMA_INT_CTRL_RX_DRQ_EN	BIT(3)

#define SUN4I_I2S_INT_STA_REG		0x20

#define SUN4I_I2S_CLK_DIV_REG		0x24
#define SUN4I_I2S_CLK_DIV_MCLK_EN		BIT(7)
#define SUN4I_I2S_CLK_DIV_BCLK_MASK		GENMASK(6, 4)
#define SUN4I_I2S_CLK_DIV_BCLK(bclk)			((bclk) << 4)
#define SUN4I_I2S_CLK_DIV_MCLK_MASK		GENMASK(3, 0)
#define SUN4I_I2S_CLK_DIV_MCLK(mclk)			((mclk) << 0)

#define SUN4I_I2S_TX_CNT_REG		0x28
#define SUN4I_I2S_RX_CNT_REG		0x2c

#define SUN4I_I2S_TX_CHAN_SEL_REG	0x30
#define SUN4I_I2S_CHAN_SEL_MASK			GENMASK(2, 0)
#define SUN4I_I2S_CHAN_SEL(num_chan)		(((num_chan) - 1) << 0)

#define SUN4I_I2S_TX_CHAN_MAP_REG	0x34
#define SUN4I_I2S_TX_CHAN_MAP(chan, sample)	((sample) << (chan << 2))

#define SUN4I_I2S_RX_CHAN_SEL_REG	0x38
#define SUN4I_I2S_RX_CHAN_MAP_REG	0x3c

/* Defines required for sun8i-h3 support */
#define SUN8I_I2S_CTRL_BCLK_OUT			BIT(18)
#define SUN8I_I2S_CTRL_LRCK_OUT			BIT(17)

#define SUN8I_I2S_CTRL_MODE_MASK		GENMASK(5, 4)
#define SUN8I_I2S_CTRL_MODE_RIGHT		(2 << 4)
#define SUN8I_I2S_CTRL_MODE_LEFT		(1 << 4)
#define SUN8I_I2S_CTRL_MODE_PCM			(0 << 4)

#define SUN8I_I2S_FMT0_LRCLK_POLARITY_MASK	BIT(19)
#define SUN8I_I2S_FMT0_LRCLK_POLARITY_START_HIGH	(1 << 19)
#define SUN8I_I2S_FMT0_LRCLK_POLARITY_START_LOW		(0 << 19)
#define SUN8I_I2S_FMT0_LRCK_PERIOD_MASK		GENMASK(17, 8)
#define SUN8I_I2S_FMT0_LRCK_PERIOD(period)	((period - 1) << 8)
#define SUN8I_I2S_FMT0_BCLK_POLARITY_MASK	BIT(7)
#define SUN8I_I2S_FMT0_BCLK_POLARITY_INVERTED		(1 << 7)
#define SUN8I_I2S_FMT0_BCLK_POLARITY_NORMAL		(0 << 7)

#define SUN8I_I2S_FMT1_REG_SEXT_MASK		GENMASK(5, 4)
#define SUN8I_I2S_FMT1_REG_SEXT(sext)			((sext) << 4)

#define SUN8I_I2S_INT_STA_REG		0x0c
#define SUN8I_I2S_FIFO_TX_REG		0x20

#define SUN8I_I2S_CHAN_CFG_REG		0x30
#define SUN8I_I2S_CHAN_CFG_RX_SLOT_NUM_MASK	GENMASK(7, 4)
#define SUN8I_I2S_CHAN_CFG_RX_SLOT_NUM(chan)	((chan - 1) << 4)
#define SUN8I_I2S_CHAN_CFG_TX_SLOT_NUM_MASK	GENMASK(3, 0)
#define SUN8I_I2S_CHAN_CFG_TX_SLOT_NUM(chan)	(chan - 1)

#define SUN8I_I2S_TX_CHAN_MAP_REG	0x44
#define SUN8I_I2S_TX_CHAN_SEL_REG	0x34
#define SUN8I_I2S_TX_CHAN_OFFSET_MASK		GENMASK(13, 12)
#define SUN8I_I2S_TX_CHAN_OFFSET(offset)	(offset << 12)
#define SUN8I_I2S_TX_CHAN_EN_MASK		GENMASK(11, 4)
#define SUN8I_I2S_TX_CHAN_EN(num_chan)		(((1 << num_chan) - 1) << 4)

#define SUN8I_I2S_RX_CHAN_SEL_REG	0x54
#define SUN8I_I2S_RX_CHAN_MAP_REG	0x58

/* Defines required for sun50i-h6 support */
#define SUN50I_H6_I2S_TX_CHAN_SEL_OFFSET_MASK	GENMASK(21, 20)
#define SUN50I_H6_I2S_TX_CHAN_SEL_OFFSET(offset)	((offset) << 20)
#define SUN50I_H6_I2S_TX_CHAN_SEL_MASK		GENMASK(19, 16)
#define SUN50I_H6_I2S_TX_CHAN_SEL(chan)		((chan - 1) << 16)
#define SUN50I_H6_I2S_TX_CHAN_EN_MASK		GENMASK(15, 0)
#define SUN50I_H6_I2S_TX_CHAN_EN(num_chan)	(((1 << num_chan) - 1))

#define SUN50I_H6_I2S_TX_CHAN_SEL_REG(pin)	(0x34 + 4 * (pin))
#define SUN50I_H6_I2S_TX_CHAN_MAP0_REG(pin)	(0x44 + 8 * (pin))
#define SUN50I_H6_I2S_TX_CHAN_MAP1_REG(pin)	(0x48 + 8 * (pin))

#define SUN50I_H6_I2S_RX_CHAN_SEL_REG	0x64
#define SUN50I_H6_I2S_RX_CHAN_MAP0_REG	0x68
#define SUN50I_H6_I2S_RX_CHAN_MAP1_REG	0x6C

#define SUN50I_R329_I2S_RX_CHAN_MAP0_REG 0x68
#define SUN50I_R329_I2S_RX_CHAN_MAP1_REG 0x6c
#define SUN50I_R329_I2S_RX_CHAN_MAP2_REG 0x70
#define SUN50I_R329_I2S_RX_CHAN_MAP3_REG 0x74

struct sun4i_i2s;

/**
 * struct sun4i_i2s_quirks - Differences between SoC variants.
 * @has_reset: SoC needs reset deasserted.
 * @pcm_formats: available PCM formats.
 * @reg_offset_txdata: offset of the tx fifo.
 * @sun4i_i2s_regmap: regmap config to use.
 * @field_clkdiv_mclk_en: regmap field to enable mclk output.
 * @field_fmt_wss: regmap field to set word select size.
 * @field_fmt_sr: regmap field to set sample resolution.
 * @num_din_pins: input pins
 * @num_dout_pins: output pins (currently set but unused)
 * @bclk_dividers: bit clock dividers array
 * @num_bclk_dividers: number of bit clock dividers
 * @mclk_dividers: mclk dividers array
 * @num_mclk_dividers: number of mclk dividers
 * @get_bclk_parent_rate: callback to get bclk parent rate
 * @get_sr: callback to get sample resolution
 * @get_wss: callback to get word select size
 * @set_chan_cfg: callback to set channel configuration
 * @set_fmt: callback to set format
 */
struct sun4i_i2s_quirks {
	bool				has_reset;
	u64				pcm_formats;
	unsigned int			reg_offset_txdata;	/* TX FIFO */
	const struct regmap_config	*sun4i_i2s_regmap;

	/* Register fields for i2s */
	struct reg_field		field_clkdiv_mclk_en;
	struct reg_field		field_fmt_wss;
	struct reg_field		field_fmt_sr;

	unsigned int			num_din_pins;
	unsigned int			num_dout_pins;

	const struct sun4i_i2s_clk_div	*bclk_dividers;
	unsigned int			num_bclk_dividers;
	const struct sun4i_i2s_clk_div	*mclk_dividers;
	unsigned int			num_mclk_dividers;

	unsigned long (*get_bclk_parent_rate)(const struct sun4i_i2s *i2s);
	int	(*get_sr)(unsigned int width);
	int	(*get_wss)(unsigned int width);

	/*
	 * In the set_chan_cfg() function pointer:
	 * @slots: channels per frame + padding slots, regardless of format
	 * @slot_width: bits per sample + padding bits, regardless of format
	 */
	int	(*set_chan_cfg)(const struct sun4i_i2s *i2s,
				unsigned int channels,	unsigned int slots,
				unsigned int slot_width);
	int	(*set_fmt)(const struct sun4i_i2s *i2s, unsigned int fmt);
};

struct sun4i_i2s {
	struct clk	*bus_clk;
	struct clk	*mod_clk;
	struct regmap	*regmap;
	struct reset_control *rst;

	unsigned int	format;
	unsigned int	mclk_freq;
	unsigned int	slots;
	unsigned int	slot_width;

	struct snd_dmaengine_dai_dma_data	capture_dma_data;
	struct snd_dmaengine_dai_dma_data	playback_dma_data;

	/* Register fields for i2s */
	struct regmap_field	*field_clkdiv_mclk_en;
	struct regmap_field	*field_fmt_wss;
	struct regmap_field	*field_fmt_sr;

	const struct sun4i_i2s_quirks	*variant;
};

struct sun4i_i2s_clk_div {
	u8	div;
	u8	val;
};

static const struct sun4i_i2s_clk_div sun4i_i2s_bclk_div[] = {
	{ .div = 2, .val = 0 },
	{ .div = 4, .val = 1 },
	{ .div = 6, .val = 2 },
	{ .div = 8, .val = 3 },
	{ .div = 12, .val = 4 },
	{ .div = 16, .val = 5 },
	/* TODO - extend divide ratio supported by newer SoCs */
};

static const struct sun4i_i2s_clk_div sun4i_i2s_mclk_div[] = {
	{ .div = 1, .val = 0 },
	{ .div = 2, .val = 1 },
	{ .div = 4, .val = 2 },
	{ .div = 6, .val = 3 },
	{ .div = 8, .val = 4 },
	{ .div = 12, .val = 5 },
	{ .div = 16, .val = 6 },
	{ .div = 24, .val = 7 },
	/* TODO - extend divide ratio supported by newer SoCs */
};

static const struct sun4i_i2s_clk_div sun8i_i2s_clk_div[] = {
	{ .div = 1, .val = 1 },
	{ .div = 2, .val = 2 },
	{ .div = 4, .val = 3 },
	{ .div = 6, .val = 4 },
	{ .div = 8, .val = 5 },
	{ .div = 12, .val = 6 },
	{ .div = 16, .val = 7 },
	{ .div = 24, .val = 8 },
	{ .div = 32, .val = 9 },
	{ .div = 48, .val = 10 },
	{ .div = 64, .val = 11 },
	{ .div = 96, .val = 12 },
	{ .div = 128, .val = 13 },
	{ .div = 176, .val = 14 },
	{ .div = 192, .val = 15 },
};

static unsigned long sun4i_i2s_get_bclk_parent_rate(const struct sun4i_i2s *i2s)
{
	return i2s->mclk_freq;
}

static unsigned long sun8i_i2s_get_bclk_parent_rate(const struct sun4i_i2s *i2s)
{
	return clk_get_rate(i2s->mod_clk);
}

static int sun4i_i2s_get_bclk_div(struct sun4i_i2s *i2s,
				  unsigned long parent_rate,
				  unsigned int sampling_rate,
				  unsigned int channels,
				  unsigned int word_size)
{
	const struct sun4i_i2s_clk_div *dividers = i2s->variant->bclk_dividers;
	int div = parent_rate / sampling_rate / word_size / channels;
	int i;

	for (i = 0; i < i2s->variant->num_bclk_dividers; i++) {
		const struct sun4i_i2s_clk_div *bdiv = &dividers[i];

		if (bdiv->div == div)
			return bdiv->val;
	}

	return -EINVAL;
}

static int sun4i_i2s_get_mclk_div(struct sun4i_i2s *i2s,
				  unsigned long parent_rate,
				  unsigned long mclk_rate)
{
	const struct sun4i_i2s_clk_div *dividers = i2s->variant->mclk_dividers;
	int div = parent_rate / mclk_rate;
	int i;

	for (i = 0; i < i2s->variant->num_mclk_dividers; i++) {
		const struct sun4i_i2s_clk_div *mdiv = &dividers[i];

		if (mdiv->div == div)
			return mdiv->val;
	}

	return -EINVAL;
}

static int sun4i_i2s_oversample_rates[] = { 128, 192, 256, 384, 512, 768 };
static bool sun4i_i2s_oversample_is_valid(unsigned int oversample)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sun4i_i2s_oversample_rates); i++)
		if (sun4i_i2s_oversample_rates[i] == oversample)
			return true;

	return false;
}

static int sun4i_i2s_set_clk_rate(struct snd_soc_dai *dai,
				  unsigned int rate,
				  unsigned int slots,
				  unsigned int slot_width)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int oversample_rate, clk_rate, bclk_parent_rate;
	int bclk_div, mclk_div;
	int ret;

	switch (rate) {
	case 176400:
	case 88200:
	case 44100:
	case 22050:
	case 11025:
		clk_rate = 22579200;
		break;

	case 192000:
	case 128000:
	case 96000:
	case 64000:
	case 48000:
	case 32000:
	case 24000:
	case 16000:
	case 12000:
	case 8000:
		clk_rate = 24576000;
		break;

	default:
		dev_err(dai->dev, "Unsupported sample rate: %u\n", rate);
		return -EINVAL;
	}

	ret = clk_set_rate(i2s->mod_clk, clk_rate);
	if (ret)
		return ret;

	oversample_rate = i2s->mclk_freq / rate;
	if (!sun4i_i2s_oversample_is_valid(oversample_rate)) {
		dev_err(dai->dev, "Unsupported oversample rate: %d\n",
			oversample_rate);
		return -EINVAL;
	}

	bclk_parent_rate = i2s->variant->get_bclk_parent_rate(i2s);
	bclk_div = sun4i_i2s_get_bclk_div(i2s, bclk_parent_rate,
					  rate, slots, slot_width);
	if (bclk_div < 0) {
		dev_err(dai->dev, "Unsupported BCLK divider: %d\n", bclk_div);
		return -EINVAL;
	}

	mclk_div = sun4i_i2s_get_mclk_div(i2s, clk_rate, i2s->mclk_freq);
	if (mclk_div < 0) {
		dev_err(dai->dev, "Unsupported MCLK divider: %d\n", mclk_div);
		return -EINVAL;
	}

	regmap_write(i2s->regmap, SUN4I_I2S_CLK_DIV_REG,
		     SUN4I_I2S_CLK_DIV_BCLK(bclk_div) |
		     SUN4I_I2S_CLK_DIV_MCLK(mclk_div));

	regmap_field_write(i2s->field_clkdiv_mclk_en, 1);

	return 0;
}

static int sun4i_i2s_get_sr(unsigned int width)
{
	switch (width) {
	case 16:
		return 0;
	case 20:
		return 1;
	case 24:
		return 2;
	}

	return -EINVAL;
}

static int sun4i_i2s_get_wss(unsigned int width)
{
	switch (width) {
	case 16:
		return 0;
	case 20:
		return 1;
	case 24:
		return 2;
	case 32:
		return 3;
	}

	return -EINVAL;
}

static int sun8i_i2s_get_sr_wss(unsigned int width)
{
	switch (width) {
	case 8:
		return 1;
	case 12:
		return 2;
	case 16:
		return 3;
	case 20:
		return 4;
	case 24:
		return 5;
	case 28:
		return 6;
	case 32:
		return 7;
	}

	return -EINVAL;
}

static int sun4i_i2s_set_chan_cfg(const struct sun4i_i2s *i2s,
				  unsigned int channels, unsigned int slots,
				  unsigned int slot_width)
{
	/* Map the channels for playback and capture */
	regmap_write(i2s->regmap, SUN4I_I2S_TX_CHAN_MAP_REG, 0x76543210);
	regmap_write(i2s->regmap, SUN4I_I2S_RX_CHAN_MAP_REG, 0x00003210);

	/* Configure the channels */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_TX_CHAN_SEL_REG,
			   SUN4I_I2S_CHAN_SEL_MASK,
			   SUN4I_I2S_CHAN_SEL(channels));
	regmap_update_bits(i2s->regmap, SUN4I_I2S_RX_CHAN_SEL_REG,
			   SUN4I_I2S_CHAN_SEL_MASK,
			   SUN4I_I2S_CHAN_SEL(channels));

	return 0;
}

static int sun8i_i2s_set_chan_cfg(const struct sun4i_i2s *i2s,
				  unsigned int channels, unsigned int slots,
				  unsigned int slot_width)
{
	unsigned int lrck_period;

	/* Map the channels for playback and capture */
	regmap_write(i2s->regmap, SUN8I_I2S_TX_CHAN_MAP_REG, 0x76543210);
	regmap_write(i2s->regmap, SUN8I_I2S_RX_CHAN_MAP_REG, 0x76543210);

	/* Configure the channels */
	regmap_update_bits(i2s->regmap, SUN8I_I2S_TX_CHAN_SEL_REG,
			   SUN4I_I2S_CHAN_SEL_MASK,
			   SUN4I_I2S_CHAN_SEL(channels));
	regmap_update_bits(i2s->regmap, SUN8I_I2S_RX_CHAN_SEL_REG,
			   SUN4I_I2S_CHAN_SEL_MASK,
			   SUN4I_I2S_CHAN_SEL(channels));

	regmap_update_bits(i2s->regmap, SUN8I_I2S_CHAN_CFG_REG,
			   SUN8I_I2S_CHAN_CFG_TX_SLOT_NUM_MASK,
			   SUN8I_I2S_CHAN_CFG_TX_SLOT_NUM(channels));
	regmap_update_bits(i2s->regmap, SUN8I_I2S_CHAN_CFG_REG,
			   SUN8I_I2S_CHAN_CFG_RX_SLOT_NUM_MASK,
			   SUN8I_I2S_CHAN_CFG_RX_SLOT_NUM(channels));

	switch (i2s->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		lrck_period = slot_width * slots;
		break;

	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_I2S:
		lrck_period = slot_width;
		break;

	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN8I_I2S_FMT0_LRCK_PERIOD_MASK,
			   SUN8I_I2S_FMT0_LRCK_PERIOD(lrck_period));

	regmap_update_bits(i2s->regmap, SUN8I_I2S_TX_CHAN_SEL_REG,
			   SUN8I_I2S_TX_CHAN_EN_MASK,
			   SUN8I_I2S_TX_CHAN_EN(channels));

	return 0;
}

static int sun50i_h6_i2s_set_chan_cfg(const struct sun4i_i2s *i2s,
				      unsigned int channels, unsigned int slots,
				      unsigned int slot_width)
{
	unsigned int lrck_period;

	/* Map the channels for playback and capture */
	regmap_write(i2s->regmap, SUN50I_H6_I2S_TX_CHAN_MAP0_REG(0), 0xFEDCBA98);
	regmap_write(i2s->regmap, SUN50I_H6_I2S_TX_CHAN_MAP1_REG(0), 0x76543210);
	if (i2s->variant->num_din_pins > 1) {
		regmap_write(i2s->regmap, SUN50I_R329_I2S_RX_CHAN_MAP0_REG, 0x0F0E0D0C);
		regmap_write(i2s->regmap, SUN50I_R329_I2S_RX_CHAN_MAP1_REG, 0x0B0A0908);
		regmap_write(i2s->regmap, SUN50I_R329_I2S_RX_CHAN_MAP2_REG, 0x07060504);
		regmap_write(i2s->regmap, SUN50I_R329_I2S_RX_CHAN_MAP3_REG, 0x03020100);
	} else {
		regmap_write(i2s->regmap, SUN50I_H6_I2S_RX_CHAN_MAP0_REG, 0xFEDCBA98);
		regmap_write(i2s->regmap, SUN50I_H6_I2S_RX_CHAN_MAP1_REG, 0x76543210);
	}

	/* Configure the channels */
	regmap_update_bits(i2s->regmap, SUN50I_H6_I2S_TX_CHAN_SEL_REG(0),
			   SUN50I_H6_I2S_TX_CHAN_SEL_MASK,
			   SUN50I_H6_I2S_TX_CHAN_SEL(channels));
	regmap_update_bits(i2s->regmap, SUN50I_H6_I2S_RX_CHAN_SEL_REG,
			   SUN50I_H6_I2S_TX_CHAN_SEL_MASK,
			   SUN50I_H6_I2S_TX_CHAN_SEL(channels));

	regmap_update_bits(i2s->regmap, SUN8I_I2S_CHAN_CFG_REG,
			   SUN8I_I2S_CHAN_CFG_TX_SLOT_NUM_MASK,
			   SUN8I_I2S_CHAN_CFG_TX_SLOT_NUM(channels));
	regmap_update_bits(i2s->regmap, SUN8I_I2S_CHAN_CFG_REG,
			   SUN8I_I2S_CHAN_CFG_RX_SLOT_NUM_MASK,
			   SUN8I_I2S_CHAN_CFG_RX_SLOT_NUM(channels));

	switch (i2s->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		lrck_period = slot_width * slots;
		break;

	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_I2S:
		lrck_period = slot_width;
		break;

	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN8I_I2S_FMT0_LRCK_PERIOD_MASK,
			   SUN8I_I2S_FMT0_LRCK_PERIOD(lrck_period));

	regmap_update_bits(i2s->regmap, SUN50I_H6_I2S_TX_CHAN_SEL_REG(0),
			   SUN50I_H6_I2S_TX_CHAN_EN_MASK,
			   SUN50I_H6_I2S_TX_CHAN_EN(channels));

	return 0;
}

static int sun4i_i2s_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int word_size = params_width(params);
	unsigned int slot_width = params_physical_width(params);
	unsigned int channels = params_channels(params);

	unsigned int slots = channels;

	int ret, sr, wss;
	u32 width;

	if (i2s->slots)
		slots = i2s->slots;

	if (i2s->slot_width)
		slot_width = i2s->slot_width;

	ret = i2s->variant->set_chan_cfg(i2s, channels, slots, slot_width);
	if (ret < 0) {
		dev_err(dai->dev, "Invalid channel configuration\n");
		return ret;
	}

	/* Set significant bits in our FIFOs */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_FIFO_CTRL_REG,
			   SUN4I_I2S_FIFO_CTRL_TX_MODE_MASK |
			   SUN4I_I2S_FIFO_CTRL_RX_MODE_MASK,
			   SUN4I_I2S_FIFO_CTRL_TX_MODE(1) |
			   SUN4I_I2S_FIFO_CTRL_RX_MODE(1));

	switch (params_physical_width(params)) {
	case 16:
		width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case 32:
		width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	default:
		dev_err(dai->dev, "Unsupported physical sample width: %d\n",
			params_physical_width(params));
		return -EINVAL;
	}
	i2s->playback_dma_data.addr_width = width;

	sr = i2s->variant->get_sr(word_size);
	if (sr < 0)
		return -EINVAL;

	wss = i2s->variant->get_wss(slot_width);
	if (wss < 0)
		return -EINVAL;

	regmap_field_write(i2s->field_fmt_wss, wss);
	regmap_field_write(i2s->field_fmt_sr, sr);

	return sun4i_i2s_set_clk_rate(dai, params_rate(params),
				      slots, slot_width);
}

static int sun4i_i2s_set_soc_fmt(const struct sun4i_i2s *i2s,
				 unsigned int fmt)
{
	u32 val;

	/* DAI clock polarity */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		val = SUN4I_I2S_FMT0_BCLK_POLARITY_INVERTED |
		      SUN4I_I2S_FMT0_LRCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		val = SUN4I_I2S_FMT0_BCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		val = SUN4I_I2S_FMT0_LRCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN4I_I2S_FMT0_LRCLK_POLARITY_MASK |
			   SUN4I_I2S_FMT0_BCLK_POLARITY_MASK,
			   val);

	/* DAI Mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		val = SUN4I_I2S_FMT0_FMT_I2S;
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		val = SUN4I_I2S_FMT0_FMT_LEFT_J;
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
		val = SUN4I_I2S_FMT0_FMT_RIGHT_J;
		break;

	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN4I_I2S_FMT0_FMT_MASK, val);

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* BCLK and LRCLK master */
		val = SUN4I_I2S_CTRL_MODE_MASTER;
		break;

	case SND_SOC_DAIFMT_BC_FC:
		/* BCLK and LRCLK slave */
		val = SUN4I_I2S_CTRL_MODE_SLAVE;
		break;

	default:
		return -EINVAL;
	}
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_MODE_MASK, val);

	return 0;
}

static int sun8i_i2s_set_soc_fmt(const struct sun4i_i2s *i2s,
				 unsigned int fmt)
{
	u32 mode, lrclk_pol, bclk_pol, val;
	u8 offset;

	/* DAI Mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_HIGH;
		mode = SUN8I_I2S_CTRL_MODE_PCM;
		offset = 1;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_HIGH;
		mode = SUN8I_I2S_CTRL_MODE_PCM;
		offset = 0;
		break;

	case SND_SOC_DAIFMT_I2S:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_LOW;
		mode = SUN8I_I2S_CTRL_MODE_LEFT;
		offset = 1;
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_HIGH;
		mode = SUN8I_I2S_CTRL_MODE_LEFT;
		offset = 0;
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_HIGH;
		mode = SUN8I_I2S_CTRL_MODE_RIGHT;
		offset = 0;
		break;

	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN8I_I2S_CTRL_MODE_MASK, mode);
	regmap_update_bits(i2s->regmap, SUN8I_I2S_TX_CHAN_SEL_REG,
			   SUN8I_I2S_TX_CHAN_OFFSET_MASK,
			   SUN8I_I2S_TX_CHAN_OFFSET(offset));
	regmap_update_bits(i2s->regmap, SUN8I_I2S_RX_CHAN_SEL_REG,
			   SUN8I_I2S_TX_CHAN_OFFSET_MASK,
			   SUN8I_I2S_TX_CHAN_OFFSET(offset));

	/* DAI clock polarity */
	bclk_pol = SUN8I_I2S_FMT0_BCLK_POLARITY_NORMAL;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		lrclk_pol ^= SUN8I_I2S_FMT0_LRCLK_POLARITY_MASK;
		bclk_pol = SUN8I_I2S_FMT0_BCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		bclk_pol = SUN8I_I2S_FMT0_BCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		lrclk_pol ^= SUN8I_I2S_FMT0_LRCLK_POLARITY_MASK;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		/* No inversion */
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN8I_I2S_FMT0_LRCLK_POLARITY_MASK |
			   SUN8I_I2S_FMT0_BCLK_POLARITY_MASK,
			   lrclk_pol | bclk_pol);

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* BCLK and LRCLK master */
		val = SUN8I_I2S_CTRL_BCLK_OUT |	SUN8I_I2S_CTRL_LRCK_OUT;
		break;

	case SND_SOC_DAIFMT_BC_FC:
		/* BCLK and LRCLK slave */
		val = 0;
		break;

	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN8I_I2S_CTRL_BCLK_OUT | SUN8I_I2S_CTRL_LRCK_OUT,
			   val);

	/* Set sign extension to pad out LSB with 0 */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT1_REG,
			   SUN8I_I2S_FMT1_REG_SEXT_MASK,
			   SUN8I_I2S_FMT1_REG_SEXT(0));

	return 0;
}

static int sun50i_h6_i2s_set_soc_fmt(const struct sun4i_i2s *i2s,
				     unsigned int fmt)
{
	u32 mode, lrclk_pol, bclk_pol, val;
	u8 offset;

	/* DAI Mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_HIGH;
		mode = SUN8I_I2S_CTRL_MODE_PCM;
		offset = 1;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_HIGH;
		mode = SUN8I_I2S_CTRL_MODE_PCM;
		offset = 0;
		break;

	case SND_SOC_DAIFMT_I2S:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_LOW;
		mode = SUN8I_I2S_CTRL_MODE_LEFT;
		offset = 1;
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_HIGH;
		mode = SUN8I_I2S_CTRL_MODE_LEFT;
		offset = 0;
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
		lrclk_pol = SUN8I_I2S_FMT0_LRCLK_POLARITY_START_HIGH;
		mode = SUN8I_I2S_CTRL_MODE_RIGHT;
		offset = 0;
		break;

	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN8I_I2S_CTRL_MODE_MASK, mode);
	regmap_update_bits(i2s->regmap, SUN8I_I2S_TX_CHAN_SEL_REG,
			   SUN50I_H6_I2S_TX_CHAN_SEL_OFFSET_MASK,
			   SUN50I_H6_I2S_TX_CHAN_SEL_OFFSET(offset));
	regmap_update_bits(i2s->regmap, SUN50I_H6_I2S_RX_CHAN_SEL_REG,
			   SUN50I_H6_I2S_TX_CHAN_SEL_OFFSET_MASK,
			   SUN50I_H6_I2S_TX_CHAN_SEL_OFFSET(offset));

	/* DAI clock polarity */
	bclk_pol = SUN8I_I2S_FMT0_BCLK_POLARITY_NORMAL;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		lrclk_pol ^= SUN8I_I2S_FMT0_LRCLK_POLARITY_MASK;
		bclk_pol = SUN8I_I2S_FMT0_BCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		bclk_pol = SUN8I_I2S_FMT0_BCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		lrclk_pol ^= SUN8I_I2S_FMT0_LRCLK_POLARITY_MASK;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		/* No inversion */
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN8I_I2S_FMT0_LRCLK_POLARITY_MASK |
			   SUN8I_I2S_FMT0_BCLK_POLARITY_MASK,
			   lrclk_pol | bclk_pol);


	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* BCLK and LRCLK master */
		val = SUN8I_I2S_CTRL_BCLK_OUT |	SUN8I_I2S_CTRL_LRCK_OUT;
		break;

	case SND_SOC_DAIFMT_BC_FC:
		/* BCLK and LRCLK slave */
		val = 0;
		break;

	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN8I_I2S_CTRL_BCLK_OUT | SUN8I_I2S_CTRL_LRCK_OUT,
			   val);

	/* Set sign extension to pad out LSB with 0 */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT1_REG,
			   SUN8I_I2S_FMT1_REG_SEXT_MASK,
			   SUN8I_I2S_FMT1_REG_SEXT(0));

	return 0;
}

static int sun4i_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = i2s->variant->set_fmt(i2s, fmt);
	if (ret) {
		dev_err(dai->dev, "Unsupported format configuration\n");
		return ret;
	}

	i2s->format = fmt;

	return 0;
}

static void sun4i_i2s_start_capture(struct sun4i_i2s *i2s)
{
	/* Flush RX FIFO */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_FIFO_CTRL_REG,
			   SUN4I_I2S_FIFO_CTRL_FLUSH_RX,
			   SUN4I_I2S_FIFO_CTRL_FLUSH_RX);

	/* Clear RX counter */
	regmap_write(i2s->regmap, SUN4I_I2S_RX_CNT_REG, 0);

	/* Enable RX Block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_RX_EN,
			   SUN4I_I2S_CTRL_RX_EN);

	/* Enable RX DRQ */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_DMA_INT_CTRL_REG,
			   SUN4I_I2S_DMA_INT_CTRL_RX_DRQ_EN,
			   SUN4I_I2S_DMA_INT_CTRL_RX_DRQ_EN);
}

static void sun4i_i2s_start_playback(struct sun4i_i2s *i2s)
{
	/* Flush TX FIFO */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_FIFO_CTRL_REG,
			   SUN4I_I2S_FIFO_CTRL_FLUSH_TX,
			   SUN4I_I2S_FIFO_CTRL_FLUSH_TX);

	/* Clear TX counter */
	regmap_write(i2s->regmap, SUN4I_I2S_TX_CNT_REG, 0);

	/* Enable TX Block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_TX_EN,
			   SUN4I_I2S_CTRL_TX_EN);

	/* Enable TX DRQ */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_DMA_INT_CTRL_REG,
			   SUN4I_I2S_DMA_INT_CTRL_TX_DRQ_EN,
			   SUN4I_I2S_DMA_INT_CTRL_TX_DRQ_EN);
}

static void sun4i_i2s_stop_capture(struct sun4i_i2s *i2s)
{
	/* Disable RX Block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_RX_EN,
			   0);

	/* Disable RX DRQ */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_DMA_INT_CTRL_REG,
			   SUN4I_I2S_DMA_INT_CTRL_RX_DRQ_EN,
			   0);
}

static void sun4i_i2s_stop_playback(struct sun4i_i2s *i2s)
{
	/* Disable TX Block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_TX_EN,
			   0);

	/* Disable TX DRQ */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_DMA_INT_CTRL_REG,
			   SUN4I_I2S_DMA_INT_CTRL_TX_DRQ_EN,
			   0);
}

static int sun4i_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			     struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sun4i_i2s_start_playback(i2s);
		else
			sun4i_i2s_start_capture(i2s);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sun4i_i2s_stop_playback(i2s);
		else
			sun4i_i2s_stop_capture(i2s);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int sun4i_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				unsigned int freq, int dir)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	if (clk_id != 0)
		return -EINVAL;

	i2s->mclk_freq = freq;

	return 0;
}

static int sun4i_i2s_set_tdm_slot(struct snd_soc_dai *dai,
				  unsigned int tx_mask, unsigned int rx_mask,
				  int slots, int slot_width)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	if (slots > 8)
		return -EINVAL;

	i2s->slots = slots;
	i2s->slot_width = slot_width;

	return 0;
}

static int sun4i_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai,
				  &i2s->playback_dma_data,
				  &i2s->capture_dma_data);

	return 0;
}

static int sun4i_i2s_dai_startup(struct snd_pcm_substream *sub, struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_runtime *runtime = sub->runtime;

	return snd_pcm_hw_constraint_mask64(runtime, SNDRV_PCM_HW_PARAM_FORMAT,
					    i2s->variant->pcm_formats);
}

static const struct snd_soc_dai_ops sun4i_i2s_dai_ops = {
	.probe		= sun4i_i2s_dai_probe,
	.startup	= sun4i_i2s_dai_startup,
	.hw_params	= sun4i_i2s_hw_params,
	.set_fmt	= sun4i_i2s_set_fmt,
	.set_sysclk	= sun4i_i2s_set_sysclk,
	.set_tdm_slot	= sun4i_i2s_set_tdm_slot,
	.trigger	= sun4i_i2s_trigger,
};

#define SUN4I_FORMATS_ALL (SNDRV_PCM_FMTBIT_S16_LE | \
			   SNDRV_PCM_FMTBIT_S20_LE | \
			   SNDRV_PCM_FMTBIT_S24_LE | \
			   SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver sun4i_i2s_dai = {
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SUN4I_FORMATS_ALL,
	},
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SUN4I_FORMATS_ALL,
	},
	.ops = &sun4i_i2s_dai_ops,
	.symmetric_rate = 1,
};

static const struct snd_soc_component_driver sun4i_i2s_component = {
	.name			= "sun4i-dai",
	.legacy_dai_naming	= 1,
};

static bool sun4i_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN4I_I2S_FIFO_TX_REG:
		return false;

	default:
		return true;
	}
}

static bool sun4i_i2s_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN4I_I2S_FIFO_RX_REG:
	case SUN4I_I2S_FIFO_STA_REG:
		return false;

	default:
		return true;
	}
}

static bool sun4i_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN4I_I2S_FIFO_RX_REG:
	case SUN4I_I2S_INT_STA_REG:
	case SUN4I_I2S_RX_CNT_REG:
	case SUN4I_I2S_TX_CNT_REG:
		return true;

	default:
		return false;
	}
}

static bool sun8i_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN8I_I2S_FIFO_TX_REG:
		return false;

	default:
		return true;
	}
}

static bool sun8i_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN4I_I2S_FIFO_CTRL_REG:
	case SUN4I_I2S_FIFO_RX_REG:
	case SUN4I_I2S_FIFO_STA_REG:
	case SUN4I_I2S_RX_CNT_REG:
	case SUN4I_I2S_TX_CNT_REG:
	case SUN8I_I2S_FIFO_TX_REG:
	case SUN8I_I2S_INT_STA_REG:
		return true;

	default:
		return false;
	}
}

static const struct reg_default sun4i_i2s_reg_defaults[] = {
	{ SUN4I_I2S_CTRL_REG, 0x00000000 },
	{ SUN4I_I2S_FMT0_REG, 0x0000000c },
	{ SUN4I_I2S_FMT1_REG, 0x00004020 },
	{ SUN4I_I2S_FIFO_CTRL_REG, 0x000400f0 },
	{ SUN4I_I2S_DMA_INT_CTRL_REG, 0x00000000 },
	{ SUN4I_I2S_CLK_DIV_REG, 0x00000000 },
	{ SUN4I_I2S_TX_CHAN_SEL_REG, 0x00000001 },
	{ SUN4I_I2S_TX_CHAN_MAP_REG, 0x76543210 },
	{ SUN4I_I2S_RX_CHAN_SEL_REG, 0x00000001 },
	{ SUN4I_I2S_RX_CHAN_MAP_REG, 0x00003210 },
};

static const struct reg_default sun8i_i2s_reg_defaults[] = {
	{ SUN4I_I2S_CTRL_REG, 0x00060000 },
	{ SUN4I_I2S_FMT0_REG, 0x00000033 },
	{ SUN4I_I2S_FMT1_REG, 0x00000030 },
	{ SUN4I_I2S_FIFO_CTRL_REG, 0x000400f0 },
	{ SUN4I_I2S_DMA_INT_CTRL_REG, 0x00000000 },
	{ SUN4I_I2S_CLK_DIV_REG, 0x00000000 },
	{ SUN8I_I2S_CHAN_CFG_REG, 0x00000000 },
	{ SUN8I_I2S_TX_CHAN_SEL_REG, 0x00000000 },
	{ SUN8I_I2S_TX_CHAN_MAP_REG, 0x00000000 },
	{ SUN8I_I2S_RX_CHAN_SEL_REG, 0x00000000 },
	{ SUN8I_I2S_RX_CHAN_MAP_REG, 0x00000000 },
};

static const struct reg_default sun50i_h6_i2s_reg_defaults[] = {
	{ SUN4I_I2S_CTRL_REG, 0x00060000 },
	{ SUN4I_I2S_FMT0_REG, 0x00000033 },
	{ SUN4I_I2S_FMT1_REG, 0x00000030 },
	{ SUN4I_I2S_FIFO_CTRL_REG, 0x000400f0 },
	{ SUN4I_I2S_DMA_INT_CTRL_REG, 0x00000000 },
	{ SUN4I_I2S_CLK_DIV_REG, 0x00000000 },
	{ SUN8I_I2S_CHAN_CFG_REG, 0x00000000 },
	{ SUN50I_H6_I2S_TX_CHAN_SEL_REG(0), 0x00000000 },
	{ SUN50I_H6_I2S_TX_CHAN_MAP0_REG(0), 0x00000000 },
	{ SUN50I_H6_I2S_TX_CHAN_MAP1_REG(0), 0x00000000 },
	{ SUN50I_H6_I2S_RX_CHAN_SEL_REG, 0x00000000 },
	{ SUN50I_H6_I2S_RX_CHAN_MAP0_REG, 0x00000000 },
	{ SUN50I_H6_I2S_RX_CHAN_MAP1_REG, 0x00000000 },
};

static const struct regmap_config sun4i_i2s_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= SUN4I_I2S_RX_CHAN_MAP_REG,

	.cache_type	= REGCACHE_FLAT,
	.reg_defaults	= sun4i_i2s_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(sun4i_i2s_reg_defaults),
	.writeable_reg	= sun4i_i2s_wr_reg,
	.readable_reg	= sun4i_i2s_rd_reg,
	.volatile_reg	= sun4i_i2s_volatile_reg,
};

static const struct regmap_config sun8i_i2s_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= SUN8I_I2S_RX_CHAN_MAP_REG,
	.cache_type	= REGCACHE_FLAT,
	.reg_defaults	= sun8i_i2s_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(sun8i_i2s_reg_defaults),
	.writeable_reg	= sun4i_i2s_wr_reg,
	.readable_reg	= sun8i_i2s_rd_reg,
	.volatile_reg	= sun8i_i2s_volatile_reg,
};

static const struct regmap_config sun50i_h6_i2s_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= SUN50I_R329_I2S_RX_CHAN_MAP3_REG,
	.cache_type	= REGCACHE_FLAT,
	.reg_defaults	= sun50i_h6_i2s_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(sun50i_h6_i2s_reg_defaults),
	.writeable_reg	= sun4i_i2s_wr_reg,
	.readable_reg	= sun8i_i2s_rd_reg,
	.volatile_reg	= sun8i_i2s_volatile_reg,
};

static int sun4i_i2s_runtime_resume(struct device *dev)
{
	struct sun4i_i2s *i2s = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2s->bus_clk);
	if (ret) {
		dev_err(dev, "Failed to enable bus clock\n");
		return ret;
	}

	regcache_cache_only(i2s->regmap, false);
	regcache_mark_dirty(i2s->regmap);

	ret = regcache_sync(i2s->regmap);
	if (ret) {
		dev_err(dev, "Failed to sync regmap cache\n");
		goto err_disable_clk;
	}

	/* Enable the whole hardware block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_GL_EN, SUN4I_I2S_CTRL_GL_EN);

	/* Enable the first output line */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_SDO_EN_MASK,
			   SUN4I_I2S_CTRL_SDO_EN(0));

	ret = clk_prepare_enable(i2s->mod_clk);
	if (ret) {
		dev_err(dev, "Failed to enable module clock\n");
		goto err_disable_clk;
	}

	return 0;

err_disable_clk:
	clk_disable_unprepare(i2s->bus_clk);
	return ret;
}

static int sun4i_i2s_runtime_suspend(struct device *dev)
{
	struct sun4i_i2s *i2s = dev_get_drvdata(dev);

	clk_disable_unprepare(i2s->mod_clk);

	/* Disable our output lines */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_SDO_EN_MASK, 0);

	/* Disable the whole hardware block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_GL_EN, 0);

	regcache_cache_only(i2s->regmap, true);

	clk_disable_unprepare(i2s->bus_clk);

	return 0;
}

#define SUN4I_FORMATS_A10 (SUN4I_FORMATS_ALL & ~SNDRV_PCM_FMTBIT_S32_LE)
#define SUN4I_FORMATS_H3 SUN4I_FORMATS_ALL

static const struct sun4i_i2s_quirks sun4i_a10_i2s_quirks = {
	.has_reset		= false,
	.pcm_formats		= SUN4I_FORMATS_A10,
	.reg_offset_txdata	= SUN4I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun4i_i2s_regmap_config,
	.field_clkdiv_mclk_en	= REG_FIELD(SUN4I_I2S_CLK_DIV_REG, 7, 7),
	.field_fmt_wss		= REG_FIELD(SUN4I_I2S_FMT0_REG, 2, 3),
	.field_fmt_sr		= REG_FIELD(SUN4I_I2S_FMT0_REG, 4, 5),
	.bclk_dividers		= sun4i_i2s_bclk_div,
	.num_bclk_dividers	= ARRAY_SIZE(sun4i_i2s_bclk_div),
	.mclk_dividers		= sun4i_i2s_mclk_div,
	.num_mclk_dividers	= ARRAY_SIZE(sun4i_i2s_mclk_div),
	.get_bclk_parent_rate	= sun4i_i2s_get_bclk_parent_rate,
	.get_sr			= sun4i_i2s_get_sr,
	.get_wss		= sun4i_i2s_get_wss,
	.set_chan_cfg		= sun4i_i2s_set_chan_cfg,
	.set_fmt		= sun4i_i2s_set_soc_fmt,
};

static const struct sun4i_i2s_quirks sun6i_a31_i2s_quirks = {
	.has_reset		= true,
	.pcm_formats		= SUN4I_FORMATS_A10,
	.reg_offset_txdata	= SUN4I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun4i_i2s_regmap_config,
	.field_clkdiv_mclk_en	= REG_FIELD(SUN4I_I2S_CLK_DIV_REG, 7, 7),
	.field_fmt_wss		= REG_FIELD(SUN4I_I2S_FMT0_REG, 2, 3),
	.field_fmt_sr		= REG_FIELD(SUN4I_I2S_FMT0_REG, 4, 5),
	.bclk_dividers		= sun4i_i2s_bclk_div,
	.num_bclk_dividers	= ARRAY_SIZE(sun4i_i2s_bclk_div),
	.mclk_dividers		= sun4i_i2s_mclk_div,
	.num_mclk_dividers	= ARRAY_SIZE(sun4i_i2s_mclk_div),
	.get_bclk_parent_rate	= sun4i_i2s_get_bclk_parent_rate,
	.get_sr			= sun4i_i2s_get_sr,
	.get_wss		= sun4i_i2s_get_wss,
	.set_chan_cfg		= sun4i_i2s_set_chan_cfg,
	.set_fmt		= sun4i_i2s_set_soc_fmt,
};

/*
 * This doesn't describe the TDM controller documented in the A83t
 * datasheet, but the three undocumented I2S controller that use the
 * older design.
 */
static const struct sun4i_i2s_quirks sun8i_a83t_i2s_quirks = {
	.has_reset		= true,
	.pcm_formats		= SUN4I_FORMATS_A10,
	.reg_offset_txdata	= SUN8I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun4i_i2s_regmap_config,
	.field_clkdiv_mclk_en	= REG_FIELD(SUN4I_I2S_CLK_DIV_REG, 7, 7),
	.field_fmt_wss		= REG_FIELD(SUN4I_I2S_FMT0_REG, 2, 3),
	.field_fmt_sr		= REG_FIELD(SUN4I_I2S_FMT0_REG, 4, 5),
	.bclk_dividers		= sun4i_i2s_bclk_div,
	.num_bclk_dividers	= ARRAY_SIZE(sun4i_i2s_bclk_div),
	.mclk_dividers		= sun4i_i2s_mclk_div,
	.num_mclk_dividers	= ARRAY_SIZE(sun4i_i2s_mclk_div),
	.get_bclk_parent_rate	= sun4i_i2s_get_bclk_parent_rate,
	.get_sr			= sun4i_i2s_get_sr,
	.get_wss		= sun4i_i2s_get_wss,
	.set_chan_cfg		= sun4i_i2s_set_chan_cfg,
	.set_fmt		= sun4i_i2s_set_soc_fmt,
};

static const struct sun4i_i2s_quirks sun8i_h3_i2s_quirks = {
	.has_reset		= true,
	.pcm_formats		= SUN4I_FORMATS_H3,
	.reg_offset_txdata	= SUN8I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun8i_i2s_regmap_config,
	.field_clkdiv_mclk_en	= REG_FIELD(SUN4I_I2S_CLK_DIV_REG, 8, 8),
	.field_fmt_wss		= REG_FIELD(SUN4I_I2S_FMT0_REG, 0, 2),
	.field_fmt_sr		= REG_FIELD(SUN4I_I2S_FMT0_REG, 4, 6),
	.bclk_dividers		= sun8i_i2s_clk_div,
	.num_bclk_dividers	= ARRAY_SIZE(sun8i_i2s_clk_div),
	.mclk_dividers		= sun8i_i2s_clk_div,
	.num_mclk_dividers	= ARRAY_SIZE(sun8i_i2s_clk_div),
	.get_bclk_parent_rate	= sun8i_i2s_get_bclk_parent_rate,
	.get_sr			= sun8i_i2s_get_sr_wss,
	.get_wss		= sun8i_i2s_get_sr_wss,
	.set_chan_cfg		= sun8i_i2s_set_chan_cfg,
	.set_fmt		= sun8i_i2s_set_soc_fmt,
};

static const struct sun4i_i2s_quirks sun50i_a64_codec_i2s_quirks = {
	.has_reset		= true,
	.pcm_formats		= SUN4I_FORMATS_H3,
	.reg_offset_txdata	= SUN8I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun4i_i2s_regmap_config,
	.field_clkdiv_mclk_en	= REG_FIELD(SUN4I_I2S_CLK_DIV_REG, 7, 7),
	.field_fmt_wss		= REG_FIELD(SUN4I_I2S_FMT0_REG, 2, 3),
	.field_fmt_sr		= REG_FIELD(SUN4I_I2S_FMT0_REG, 4, 5),
	.bclk_dividers		= sun4i_i2s_bclk_div,
	.num_bclk_dividers	= ARRAY_SIZE(sun4i_i2s_bclk_div),
	.mclk_dividers		= sun4i_i2s_mclk_div,
	.num_mclk_dividers	= ARRAY_SIZE(sun4i_i2s_mclk_div),
	.get_bclk_parent_rate	= sun4i_i2s_get_bclk_parent_rate,
	.get_sr			= sun4i_i2s_get_sr,
	.get_wss		= sun4i_i2s_get_wss,
	.set_chan_cfg		= sun4i_i2s_set_chan_cfg,
	.set_fmt		= sun4i_i2s_set_soc_fmt,
};

static const struct sun4i_i2s_quirks sun50i_h6_i2s_quirks = {
	.has_reset		= true,
	.pcm_formats		= SUN4I_FORMATS_H3,
	.reg_offset_txdata	= SUN8I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun50i_h6_i2s_regmap_config,
	.field_clkdiv_mclk_en	= REG_FIELD(SUN4I_I2S_CLK_DIV_REG, 8, 8),
	.field_fmt_wss		= REG_FIELD(SUN4I_I2S_FMT0_REG, 0, 2),
	.field_fmt_sr		= REG_FIELD(SUN4I_I2S_FMT0_REG, 4, 6),
	.bclk_dividers		= sun8i_i2s_clk_div,
	.num_bclk_dividers	= ARRAY_SIZE(sun8i_i2s_clk_div),
	.mclk_dividers		= sun8i_i2s_clk_div,
	.num_mclk_dividers	= ARRAY_SIZE(sun8i_i2s_clk_div),
	.get_bclk_parent_rate	= sun8i_i2s_get_bclk_parent_rate,
	.get_sr			= sun8i_i2s_get_sr_wss,
	.get_wss		= sun8i_i2s_get_sr_wss,
	.set_chan_cfg		= sun50i_h6_i2s_set_chan_cfg,
	.set_fmt		= sun50i_h6_i2s_set_soc_fmt,
};

static const struct sun4i_i2s_quirks sun50i_r329_i2s_quirks = {
	.has_reset		= true,
	.pcm_formats		= SUN4I_FORMATS_H3,
	.reg_offset_txdata	= SUN8I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun50i_h6_i2s_regmap_config,
	.field_clkdiv_mclk_en	= REG_FIELD(SUN4I_I2S_CLK_DIV_REG, 8, 8),
	.field_fmt_wss		= REG_FIELD(SUN4I_I2S_FMT0_REG, 0, 2),
	.field_fmt_sr		= REG_FIELD(SUN4I_I2S_FMT0_REG, 4, 6),
	.num_din_pins		= 4,
	.num_dout_pins		= 4,
	.bclk_dividers		= sun8i_i2s_clk_div,
	.num_bclk_dividers	= ARRAY_SIZE(sun8i_i2s_clk_div),
	.mclk_dividers		= sun8i_i2s_clk_div,
	.num_mclk_dividers	= ARRAY_SIZE(sun8i_i2s_clk_div),
	.get_bclk_parent_rate	= sun8i_i2s_get_bclk_parent_rate,
	.get_sr			= sun8i_i2s_get_sr_wss,
	.get_wss		= sun8i_i2s_get_sr_wss,
	.set_chan_cfg		= sun50i_h6_i2s_set_chan_cfg,
	.set_fmt		= sun50i_h6_i2s_set_soc_fmt,
};

static int sun4i_i2s_init_regmap_fields(struct device *dev,
					struct sun4i_i2s *i2s)
{
	i2s->field_clkdiv_mclk_en =
		devm_regmap_field_alloc(dev, i2s->regmap,
					i2s->variant->field_clkdiv_mclk_en);
	if (IS_ERR(i2s->field_clkdiv_mclk_en))
		return PTR_ERR(i2s->field_clkdiv_mclk_en);

	i2s->field_fmt_wss =
			devm_regmap_field_alloc(dev, i2s->regmap,
						i2s->variant->field_fmt_wss);
	if (IS_ERR(i2s->field_fmt_wss))
		return PTR_ERR(i2s->field_fmt_wss);

	i2s->field_fmt_sr =
			devm_regmap_field_alloc(dev, i2s->regmap,
						i2s->variant->field_fmt_sr);
	if (IS_ERR(i2s->field_fmt_sr))
		return PTR_ERR(i2s->field_fmt_sr);

	return 0;
}

static int sun4i_i2s_probe(struct platform_device *pdev)
{
	struct sun4i_i2s *i2s;
	struct resource *res;
	void __iomem *regs;
	int irq, ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;
	platform_set_drvdata(pdev, i2s);

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	i2s->variant = of_device_get_match_data(&pdev->dev);
	if (!i2s->variant) {
		dev_err(&pdev->dev, "Failed to determine the quirks to use\n");
		return -ENODEV;
	}

	i2s->bus_clk = devm_clk_get(&pdev->dev, "apb");
	if (IS_ERR(i2s->bus_clk)) {
		dev_err(&pdev->dev, "Can't get our bus clock\n");
		return PTR_ERR(i2s->bus_clk);
	}

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    i2s->variant->sun4i_i2s_regmap);
	if (IS_ERR(i2s->regmap)) {
		dev_err(&pdev->dev, "Regmap initialisation failed\n");
		return PTR_ERR(i2s->regmap);
	}

	i2s->mod_clk = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(i2s->mod_clk)) {
		dev_err(&pdev->dev, "Can't get our mod clock\n");
		return PTR_ERR(i2s->mod_clk);
	}

	if (i2s->variant->has_reset) {
		i2s->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
		if (IS_ERR(i2s->rst)) {
			dev_err(&pdev->dev, "Failed to get reset control\n");
			return PTR_ERR(i2s->rst);
		}
	}

	if (!IS_ERR(i2s->rst)) {
		ret = reset_control_deassert(i2s->rst);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to deassert the reset control\n");
			return -EINVAL;
		}
	}

	i2s->playback_dma_data.addr = res->start +
					i2s->variant->reg_offset_txdata;
	i2s->playback_dma_data.maxburst = 8;

	i2s->capture_dma_data.addr = res->start + SUN4I_I2S_FIFO_RX_REG;
	i2s->capture_dma_data.maxburst = 8;

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = sun4i_i2s_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = sun4i_i2s_init_regmap_fields(&pdev->dev, i2s);
	if (ret) {
		dev_err(&pdev->dev, "Could not initialise regmap fields\n");
		goto err_suspend;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM\n");
		goto err_suspend;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &sun4i_i2s_component,
					      &sun4i_i2s_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI\n");
		goto err_suspend;
	}

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun4i_i2s_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	if (!IS_ERR(i2s->rst))
		reset_control_assert(i2s->rst);

	return ret;
}

static void sun4i_i2s_remove(struct platform_device *pdev)
{
	struct sun4i_i2s *i2s = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun4i_i2s_runtime_suspend(&pdev->dev);

	if (!IS_ERR(i2s->rst))
		reset_control_assert(i2s->rst);
}

static const struct of_device_id sun4i_i2s_match[] = {
	{
		.compatible = "allwinner,sun4i-a10-i2s",
		.data = &sun4i_a10_i2s_quirks,
	},
	{
		.compatible = "allwinner,sun6i-a31-i2s",
		.data = &sun6i_a31_i2s_quirks,
	},
	{
		.compatible = "allwinner,sun8i-a83t-i2s",
		.data = &sun8i_a83t_i2s_quirks,
	},
	{
		.compatible = "allwinner,sun8i-h3-i2s",
		.data = &sun8i_h3_i2s_quirks,
	},
	{
		.compatible = "allwinner,sun50i-a64-codec-i2s",
		.data = &sun50i_a64_codec_i2s_quirks,
	},
	{
		.compatible = "allwinner,sun50i-h6-i2s",
		.data = &sun50i_h6_i2s_quirks,
	},
	{
		.compatible = "allwinner,sun50i-r329-i2s",
		.data = &sun50i_r329_i2s_quirks,
	},
	{}
};
MODULE_DEVICE_TABLE(of, sun4i_i2s_match);

static const struct dev_pm_ops sun4i_i2s_pm_ops = {
	.runtime_resume		= sun4i_i2s_runtime_resume,
	.runtime_suspend	= sun4i_i2s_runtime_suspend,
};

static struct platform_driver sun4i_i2s_driver = {
	.probe	= sun4i_i2s_probe,
	.remove_new = sun4i_i2s_remove,
	.driver	= {
		.name		= "sun4i-i2s",
		.of_match_table	= sun4i_i2s_match,
		.pm		= &sun4i_i2s_pm_ops,
	},
};
module_platform_driver(sun4i_i2s_driver);

MODULE_AUTHOR("Andrea Venturi <be17068@iperbole.bo.it>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 I2S driver");
MODULE_LICENSE("GPL");
