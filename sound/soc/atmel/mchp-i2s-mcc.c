// SPDX-License-Identifier: GPL-2.0
//
// Driver for Microchip I2S Multi-channel controller
//
// Copyright (C) 2018 Microchip Technology Inc. and its subsidiaries
//
// Author: Codrin Ciubotariu <codrin.ciubotariu@microchip.com>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/lcm.h>
#include <linux/of.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

/*
 * ---- I2S Controller Register map ----
 */
#define MCHP_I2SMCC_CR		0x0000	/* Control Register */
#define MCHP_I2SMCC_MRA		0x0004	/* Mode Register A */
#define MCHP_I2SMCC_MRB		0x0008	/* Mode Register B */
#define MCHP_I2SMCC_SR		0x000C	/* Status Register */
#define MCHP_I2SMCC_IERA	0x0010	/* Interrupt Enable Register A */
#define MCHP_I2SMCC_IDRA	0x0014	/* Interrupt Disable Register A */
#define MCHP_I2SMCC_IMRA	0x0018	/* Interrupt Mask Register A */
#define MCHP_I2SMCC_ISRA	0X001C	/* Interrupt Status Register A */

#define MCHP_I2SMCC_IERB	0x0020	/* Interrupt Enable Register B */
#define MCHP_I2SMCC_IDRB	0x0024	/* Interrupt Disable Register B */
#define MCHP_I2SMCC_IMRB	0x0028	/* Interrupt Mask Register B */
#define MCHP_I2SMCC_ISRB	0X002C	/* Interrupt Status Register B */

#define MCHP_I2SMCC_RHR		0x0030	/* Receiver Holding Register */
#define MCHP_I2SMCC_THR		0x0034	/* Transmitter Holding Register */

#define MCHP_I2SMCC_RHL0R	0x0040	/* Receiver Holding Left 0 Register */
#define MCHP_I2SMCC_RHR0R	0x0044	/* Receiver Holding Right 0 Register */

#define MCHP_I2SMCC_RHL1R	0x0048	/* Receiver Holding Left 1 Register */
#define MCHP_I2SMCC_RHR1R	0x004C	/* Receiver Holding Right 1 Register */

#define MCHP_I2SMCC_RHL2R	0x0050	/* Receiver Holding Left 2 Register */
#define MCHP_I2SMCC_RHR2R	0x0054	/* Receiver Holding Right 2 Register */

#define MCHP_I2SMCC_RHL3R	0x0058	/* Receiver Holding Left 3 Register */
#define MCHP_I2SMCC_RHR3R	0x005C	/* Receiver Holding Right 3 Register */

#define MCHP_I2SMCC_THL0R	0x0060	/* Transmitter Holding Left 0 Register */
#define MCHP_I2SMCC_THR0R	0x0064	/* Transmitter Holding Right 0 Register */

#define MCHP_I2SMCC_THL1R	0x0068	/* Transmitter Holding Left 1 Register */
#define MCHP_I2SMCC_THR1R	0x006C	/* Transmitter Holding Right 1 Register */

#define MCHP_I2SMCC_THL2R	0x0070	/* Transmitter Holding Left 2 Register */
#define MCHP_I2SMCC_THR2R	0x0074	/* Transmitter Holding Right 2 Register */

#define MCHP_I2SMCC_THL3R	0x0078	/* Transmitter Holding Left 3 Register */
#define MCHP_I2SMCC_THR3R	0x007C	/* Transmitter Holding Right 3 Register */

#define MCHP_I2SMCC_VERSION	0x00FC	/* Version Register */

/*
 * ---- Control Register (Write-only) ----
 */
#define MCHP_I2SMCC_CR_RXEN		BIT(0)	/* Receiver Enable */
#define MCHP_I2SMCC_CR_RXDIS		BIT(1)	/* Receiver Disable */
#define MCHP_I2SMCC_CR_CKEN		BIT(2)	/* Clock Enable */
#define MCHP_I2SMCC_CR_CKDIS		BIT(3)	/* Clock Disable */
#define MCHP_I2SMCC_CR_TXEN		BIT(4)	/* Transmitter Enable */
#define MCHP_I2SMCC_CR_TXDIS		BIT(5)	/* Transmitter Disable */
#define MCHP_I2SMCC_CR_SWRST		BIT(7)	/* Software Reset */

/*
 * ---- Mode Register A (Read/Write) ----
 */
#define MCHP_I2SMCC_MRA_MODE_MASK		GENMASK(0, 0)
#define MCHP_I2SMCC_MRA_MODE_SLAVE		(0 << 0)
#define MCHP_I2SMCC_MRA_MODE_MASTER		(1 << 0)

#define MCHP_I2SMCC_MRA_DATALENGTH_MASK			GENMASK(3, 1)
#define MCHP_I2SMCC_MRA_DATALENGTH_32_BITS		(0 << 1)
#define MCHP_I2SMCC_MRA_DATALENGTH_24_BITS		(1 << 1)
#define MCHP_I2SMCC_MRA_DATALENGTH_20_BITS		(2 << 1)
#define MCHP_I2SMCC_MRA_DATALENGTH_18_BITS		(3 << 1)
#define MCHP_I2SMCC_MRA_DATALENGTH_16_BITS		(4 << 1)
#define MCHP_I2SMCC_MRA_DATALENGTH_16_BITS_COMPACT	(5 << 1)
#define MCHP_I2SMCC_MRA_DATALENGTH_8_BITS		(6 << 1)
#define MCHP_I2SMCC_MRA_DATALENGTH_8_BITS_COMPACT	(7 << 1)

#define MCHP_I2SMCC_MRA_WIRECFG_MASK		GENMASK(5, 4)
#define MCHP_I2SMCC_MRA_WIRECFG_TDM(pin)	(((pin) << 4) & \
						 MCHP_I2SMCC_MRA_WIRECFG_MASK)
#define MCHP_I2SMCC_MRA_WIRECFG_I2S_1_TDM_0	(0 << 4)
#define MCHP_I2SMCC_MRA_WIRECFG_I2S_2_TDM_1	(1 << 4)
#define MCHP_I2SMCC_MRA_WIRECFG_I2S_4_TDM_2	(2 << 4)
#define MCHP_I2SMCC_MRA_WIRECFG_TDM_3		(3 << 4)

#define MCHP_I2SMCC_MRA_FORMAT_MASK		GENMASK(7, 6)
#define MCHP_I2SMCC_MRA_FORMAT_I2S		(0 << 6)
#define MCHP_I2SMCC_MRA_FORMAT_LJ		(1 << 6) /* Left Justified */
#define MCHP_I2SMCC_MRA_FORMAT_TDM		(2 << 6)
#define MCHP_I2SMCC_MRA_FORMAT_TDMLJ		(3 << 6)

/* Transmitter uses one DMA channel ... */
/* Left audio samples duplicated to right audio channel */
#define MCHP_I2SMCC_MRA_RXMONO			BIT(8)

/* I2SDO output of I2SC is internally connected to I2SDI input */
#define MCHP_I2SMCC_MRA_RXLOOP			BIT(9)

/* Receiver uses one DMA channel ... */
/* Left audio samples duplicated to right audio channel */
#define MCHP_I2SMCC_MRA_TXMONO			BIT(10)

/* x sample transmitted when underrun */
#define MCHP_I2SMCC_MRA_TXSAME_ZERO		(0 << 11) /* Zero sample */
#define MCHP_I2SMCC_MRA_TXSAME_PREVIOUS		(1 << 11) /* Previous sample */

/* select between peripheral clock and generated clock */
#define MCHP_I2SMCC_MRA_SRCCLK_PCLK		(0 << 12)
#define MCHP_I2SMCC_MRA_SRCCLK_GCLK		(1 << 12)

/* Number of TDM Channels - 1 */
#define MCHP_I2SMCC_MRA_NBCHAN_MASK		GENMASK(15, 13)
#define MCHP_I2SMCC_MRA_NBCHAN(ch) \
	((((ch) - 1) << 13) & MCHP_I2SMCC_MRA_NBCHAN_MASK)

/* Selected Clock to I2SMCC Master Clock ratio */
#define MCHP_I2SMCC_MRA_IMCKDIV_MASK		GENMASK(21, 16)
#define MCHP_I2SMCC_MRA_IMCKDIV(div) \
	(((div) << 16) & MCHP_I2SMCC_MRA_IMCKDIV_MASK)

/* TDM Frame Synchronization */
#define MCHP_I2SMCC_MRA_TDMFS_MASK		GENMASK(23, 22)
#define MCHP_I2SMCC_MRA_TDMFS_SLOT		(0 << 22)
#define MCHP_I2SMCC_MRA_TDMFS_HALF		(1 << 22)
#define MCHP_I2SMCC_MRA_TDMFS_BIT		(2 << 22)

/* Selected Clock to I2SMC Serial Clock ratio */
#define MCHP_I2SMCC_MRA_ISCKDIV_MASK		GENMASK(29, 24)
#define MCHP_I2SMCC_MRA_ISCKDIV(div) \
	(((div) << 24) & MCHP_I2SMCC_MRA_ISCKDIV_MASK)

/* Master Clock mode */
#define MCHP_I2SMCC_MRA_IMCKMODE_MASK		GENMASK(30, 30)
/* 0: No master clock generated*/
#define MCHP_I2SMCC_MRA_IMCKMODE_NONE		(0 << 30)
/* 1: master clock generated (internally generated clock drives I2SMCK pin) */
#define MCHP_I2SMCC_MRA_IMCKMODE_GEN		(1 << 30)

/* Slot Width */
/* 0: slot is 32 bits wide for DATALENGTH = 18/20/24 bits. */
/* 1: slot is 24 bits wide for DATALENGTH = 18/20/24 bits. */
#define MCHP_I2SMCC_MRA_IWS			BIT(31)

/*
 * ---- Mode Register B (Read/Write) ----
 */
/* all enabled I2S left channels are filled first, then I2S right channels */
#define MCHP_I2SMCC_MRB_CRAMODE_LEFT_FIRST	(0 << 0)
/*
 * an enabled I2S left channel is filled, then the corresponding right
 * channel, until all channels are filled
 */
#define MCHP_I2SMCC_MRB_CRAMODE_REGULAR		(1 << 0)

#define MCHP_I2SMCC_MRB_FIFOEN			BIT(4)

#define MCHP_I2SMCC_MRB_DMACHUNK_MASK		GENMASK(9, 8)
#define MCHP_I2SMCC_MRB_DMACHUNK(no_words) \
	(((fls(no_words) - 1) << 8) & MCHP_I2SMCC_MRB_DMACHUNK_MASK)

#define MCHP_I2SMCC_MRB_CLKSEL_MASK		GENMASK(16, 16)
#define MCHP_I2SMCC_MRB_CLKSEL_EXT		(0 << 16)
#define MCHP_I2SMCC_MRB_CLKSEL_INT		(1 << 16)

/*
 * ---- Status Registers (Read-only) ----
 */
#define MCHP_I2SMCC_SR_RXEN		BIT(0)	/* Receiver Enabled */
#define MCHP_I2SMCC_SR_TXEN		BIT(4)	/* Transmitter Enabled */

/*
 * ---- Interrupt Enable/Disable/Mask/Status Registers A ----
 */
#define MCHP_I2SMCC_INT_TXRDY_MASK(ch)		GENMASK((ch) - 1, 0)
#define MCHP_I2SMCC_INT_TXRDYCH(ch)		BIT(ch)
#define MCHP_I2SMCC_INT_TXUNF_MASK(ch)		GENMASK((ch) + 7, 8)
#define MCHP_I2SMCC_INT_TXUNFCH(ch)		BIT((ch) + 8)
#define MCHP_I2SMCC_INT_RXRDY_MASK(ch)		GENMASK((ch) + 15, 16)
#define MCHP_I2SMCC_INT_RXRDYCH(ch)		BIT((ch) + 16)
#define MCHP_I2SMCC_INT_RXOVF_MASK(ch)		GENMASK((ch) + 23, 24)
#define MCHP_I2SMCC_INT_RXOVFCH(ch)		BIT((ch) + 24)

/*
 * ---- Interrupt Enable/Disable/Mask/Status Registers B ----
 */
#define MCHP_I2SMCC_INT_WERR			BIT(0)
#define MCHP_I2SMCC_INT_TXFFRDY			BIT(8)
#define MCHP_I2SMCC_INT_TXFFEMP			BIT(9)
#define MCHP_I2SMCC_INT_RXFFRDY			BIT(12)
#define MCHP_I2SMCC_INT_RXFFFUL			BIT(13)

/*
 * ---- Version Register (Read-only) ----
 */
#define MCHP_I2SMCC_VERSION_MASK		GENMASK(11, 0)

#define MCHP_I2SMCC_MAX_CHANNELS		8
#define MCHP_I2MCC_TDM_SLOT_WIDTH		32

/*
 * ---- DMA chunk size allowed ----
 */
#define MCHP_I2SMCC_DMA_8_WORD_CHUNK			8
#define MCHP_I2SMCC_DMA_4_WORD_CHUNK			4
#define MCHP_I2SMCC_DMA_2_WORD_CHUNK			2
#define MCHP_I2SMCC_DMA_1_WORD_CHUNK			1
#define DMA_BURST_ALIGNED(_p, _s, _w)		!(_p % (_s * _w))

static const struct regmap_config mchp_i2s_mcc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = MCHP_I2SMCC_VERSION,
};

struct mchp_i2s_mcc_soc_data {
	unsigned int	data_pin_pair_num;
	bool		has_fifo;
};

struct mchp_i2s_mcc_dev {
	struct wait_queue_head			wq_txrdy;
	struct wait_queue_head			wq_rxrdy;
	struct device				*dev;
	struct regmap				*regmap;
	struct clk				*pclk;
	struct clk				*gclk;
	const struct mchp_i2s_mcc_soc_data	*soc;
	struct snd_dmaengine_dai_dma_data	playback;
	struct snd_dmaengine_dai_dma_data	capture;
	unsigned int				fmt;
	unsigned int				sysclk;
	unsigned int				frame_length;
	int					tdm_slots;
	int					channels;
	u8					tdm_data_pair;
	unsigned int				gclk_use:1;
	unsigned int				gclk_running:1;
	unsigned int				tx_rdy:1;
	unsigned int				rx_rdy:1;
};

static irqreturn_t mchp_i2s_mcc_interrupt(int irq, void *dev_id)
{
	struct mchp_i2s_mcc_dev *dev = dev_id;
	u32 sra, imra, srb, imrb, pendinga, pendingb, idra = 0, idrb = 0;
	irqreturn_t ret = IRQ_NONE;

	regmap_read(dev->regmap, MCHP_I2SMCC_IMRA, &imra);
	regmap_read(dev->regmap, MCHP_I2SMCC_ISRA, &sra);
	pendinga = imra & sra;

	regmap_read(dev->regmap, MCHP_I2SMCC_IMRB, &imrb);
	regmap_read(dev->regmap, MCHP_I2SMCC_ISRB, &srb);
	pendingb = imrb & srb;

	if (!pendinga && !pendingb)
		return IRQ_NONE;

	/*
	 * Tx/Rx ready interrupts are enabled when stopping only, to assure
	 * availability and to disable clocks if necessary
	 */
	if (dev->soc->has_fifo) {
		idrb |= pendingb & (MCHP_I2SMCC_INT_TXFFRDY |
				    MCHP_I2SMCC_INT_RXFFRDY);
	} else {
		idra |= pendinga & (MCHP_I2SMCC_INT_TXRDY_MASK(dev->channels) |
				    MCHP_I2SMCC_INT_RXRDY_MASK(dev->channels));
	}
	if (idra || idrb)
		ret = IRQ_HANDLED;

	if ((!dev->soc->has_fifo &&
	     (imra & MCHP_I2SMCC_INT_TXRDY_MASK(dev->channels)) &&
	     (imra & MCHP_I2SMCC_INT_TXRDY_MASK(dev->channels)) ==
	     (idra & MCHP_I2SMCC_INT_TXRDY_MASK(dev->channels))) ||
	    (dev->soc->has_fifo && imrb & MCHP_I2SMCC_INT_TXFFRDY)) {
		dev->tx_rdy = 1;
		wake_up_interruptible(&dev->wq_txrdy);
	}
	if ((!dev->soc->has_fifo &&
	     (imra & MCHP_I2SMCC_INT_RXRDY_MASK(dev->channels)) &&
	     (imra & MCHP_I2SMCC_INT_RXRDY_MASK(dev->channels)) ==
	     (idra & MCHP_I2SMCC_INT_RXRDY_MASK(dev->channels))) ||
	    (dev->soc->has_fifo && imrb & MCHP_I2SMCC_INT_RXFFRDY)) {
		dev->rx_rdy = 1;
		wake_up_interruptible(&dev->wq_rxrdy);
	}
	if (dev->soc->has_fifo)
		regmap_write(dev->regmap, MCHP_I2SMCC_IDRB, idrb);
	else
		regmap_write(dev->regmap, MCHP_I2SMCC_IDRA, idra);

	return ret;
}

static int mchp_i2s_mcc_set_sysclk(struct snd_soc_dai *dai,
				   int clk_id, unsigned int freq, int dir)
{
	struct mchp_i2s_mcc_dev *dev = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dev->dev, "%s() clk_id=%d freq=%u dir=%d\n",
		__func__, clk_id, freq, dir);

	/* We do not need SYSCLK */
	if (dir == SND_SOC_CLOCK_IN)
		return 0;

	dev->sysclk = freq;

	return 0;
}

static int mchp_i2s_mcc_set_bclk_ratio(struct snd_soc_dai *dai,
				       unsigned int ratio)
{
	struct mchp_i2s_mcc_dev *dev = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dev->dev, "%s() ratio=%u\n", __func__, ratio);

	dev->frame_length = ratio;

	return 0;
}

static int mchp_i2s_mcc_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mchp_i2s_mcc_dev *dev = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dev->dev, "%s() fmt=%#x\n", __func__, fmt);

	/* We don't support any kind of clock inversion */
	if ((fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF)
		return -EINVAL;

	/* We can't generate only FSYNC */
	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) == SND_SOC_DAIFMT_BC_FP)
		return -EINVAL;

	/* We can only reconfigure the IP when it's stopped */
	if (fmt & SND_SOC_DAIFMT_CONT)
		return -EINVAL;

	dev->fmt = fmt;

	return 0;
}

static int mchp_i2s_mcc_set_dai_tdm_slot(struct snd_soc_dai *dai,
					 unsigned int tx_mask,
					 unsigned int rx_mask,
					 int slots, int slot_width)
{
	struct mchp_i2s_mcc_dev *dev = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dev->dev,
		"%s() tx_mask=0x%08x rx_mask=0x%08x slots=%d width=%d\n",
		__func__, tx_mask, rx_mask, slots, slot_width);

	if (slots < 0 || slots > MCHP_I2SMCC_MAX_CHANNELS ||
	    slot_width != MCHP_I2MCC_TDM_SLOT_WIDTH)
		return -EINVAL;

	if (slots) {
		/* We do not support daisy chain */
		if (rx_mask != GENMASK(slots - 1, 0) ||
		    rx_mask != tx_mask)
			return -EINVAL;
	}

	dev->tdm_slots = slots;
	dev->frame_length = slots * MCHP_I2MCC_TDM_SLOT_WIDTH;

	return 0;
}

static int mchp_i2s_mcc_clk_get_rate_diff(struct clk *clk,
					  unsigned long rate,
					  struct clk **best_clk,
					  unsigned long *best_rate,
					  unsigned long *best_diff_rate)
{
	long round_rate;
	unsigned int diff_rate;

	round_rate = clk_round_rate(clk, rate);
	if (round_rate < 0)
		return (int)round_rate;

	diff_rate = abs(rate - round_rate);
	if (diff_rate < *best_diff_rate) {
		*best_clk = clk;
		*best_diff_rate = diff_rate;
		*best_rate = rate;
	}

	return 0;
}

static int mchp_i2s_mcc_config_divs(struct mchp_i2s_mcc_dev *dev,
				    unsigned int bclk, unsigned int *mra,
				    unsigned long *best_rate)
{
	unsigned long clk_rate;
	unsigned long lcm_rate;
	unsigned long best_diff_rate = ~0;
	unsigned int sysclk;
	struct clk *best_clk = NULL;
	int ret;

	/* For code simplification */
	if (!dev->sysclk)
		sysclk = bclk;
	else
		sysclk = dev->sysclk;

	/*
	 * MCLK is Selected CLK / (2 * IMCKDIV),
	 * BCLK is Selected CLK / (2 * ISCKDIV);
	 * if IMCKDIV or ISCKDIV are 0, MCLK or BCLK = Selected CLK
	 */
	lcm_rate = lcm(sysclk, bclk);
	if ((lcm_rate / sysclk % 2 == 1 && lcm_rate / sysclk > 2) ||
	    (lcm_rate / bclk % 2 == 1 && lcm_rate / bclk > 2))
		lcm_rate *= 2;

	for (clk_rate = lcm_rate;
	     (clk_rate == sysclk || clk_rate / (sysclk * 2) <= GENMASK(5, 0)) &&
	     (clk_rate == bclk || clk_rate / (bclk * 2) <= GENMASK(5, 0));
	     clk_rate += lcm_rate) {
		ret = mchp_i2s_mcc_clk_get_rate_diff(dev->gclk, clk_rate,
						     &best_clk, best_rate,
						     &best_diff_rate);
		if (ret) {
			dev_err(dev->dev, "gclk error for rate %lu: %d",
				clk_rate, ret);
		} else {
			if (!best_diff_rate) {
				dev_dbg(dev->dev, "found perfect rate on gclk: %lu\n",
					clk_rate);
				break;
			}
		}

		ret = mchp_i2s_mcc_clk_get_rate_diff(dev->pclk, clk_rate,
						     &best_clk, best_rate,
						     &best_diff_rate);
		if (ret) {
			dev_err(dev->dev, "pclk error for rate %lu: %d",
				clk_rate, ret);
		} else {
			if (!best_diff_rate) {
				dev_dbg(dev->dev, "found perfect rate on pclk: %lu\n",
					clk_rate);
				break;
			}
		}
	}

	/* check if clocks returned only errors */
	if (!best_clk) {
		dev_err(dev->dev, "unable to change rate to clocks\n");
		return -EINVAL;
	}

	dev_dbg(dev->dev, "source CLK is %s with rate %lu, diff %lu\n",
		best_clk == dev->pclk ? "pclk" : "gclk",
		*best_rate, best_diff_rate);

	/* Configure divisors */
	if (dev->sysclk)
		*mra |= MCHP_I2SMCC_MRA_IMCKDIV(*best_rate / (2 * sysclk));
	*mra |= MCHP_I2SMCC_MRA_ISCKDIV(*best_rate / (2 * bclk));

	if (best_clk == dev->gclk)
		*mra |= MCHP_I2SMCC_MRA_SRCCLK_GCLK;
	else
		*mra |= MCHP_I2SMCC_MRA_SRCCLK_PCLK;

	return 0;
}

static int mchp_i2s_mcc_is_running(struct mchp_i2s_mcc_dev *dev)
{
	u32 sr;

	regmap_read(dev->regmap, MCHP_I2SMCC_SR, &sr);
	return !!(sr & (MCHP_I2SMCC_SR_TXEN | MCHP_I2SMCC_SR_RXEN));
}

static inline int mchp_i2s_mcc_period_to_maxburst(int period_size, int sample_size)
{
	int p_size = period_size;
	int s_size = sample_size;

	if (DMA_BURST_ALIGNED(p_size, s_size, MCHP_I2SMCC_DMA_8_WORD_CHUNK))
		return MCHP_I2SMCC_DMA_8_WORD_CHUNK;
	if (DMA_BURST_ALIGNED(p_size, s_size, MCHP_I2SMCC_DMA_4_WORD_CHUNK))
		return MCHP_I2SMCC_DMA_4_WORD_CHUNK;
	if (DMA_BURST_ALIGNED(p_size, s_size, MCHP_I2SMCC_DMA_2_WORD_CHUNK))
		return MCHP_I2SMCC_DMA_2_WORD_CHUNK;
	return MCHP_I2SMCC_DMA_1_WORD_CHUNK;
}

static int mchp_i2s_mcc_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	unsigned long rate = 0;
	struct mchp_i2s_mcc_dev *dev = snd_soc_dai_get_drvdata(dai);
	int sample_bytes = params_physical_width(params) / 8;
	int period_bytes = params_period_size(params) *
		params_channels(params) * sample_bytes;
	int maxburst;
	u32 mra = 0;
	u32 mrb = 0;
	unsigned int channels = params_channels(params);
	unsigned int frame_length = dev->frame_length;
	unsigned int bclk_rate;
	int set_divs = 0;
	int ret;
	bool is_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	dev_dbg(dev->dev, "%s() rate=%u format=%#x width=%u channels=%u period_bytes=%d\n",
		__func__, params_rate(params), params_format(params),
		params_width(params), params_channels(params), period_bytes);

	switch (dev->fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		if (dev->tdm_slots) {
			dev_err(dev->dev, "I2S with TDM is not supported\n");
			return -EINVAL;
		}
		mra |= MCHP_I2SMCC_MRA_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		if (dev->tdm_slots) {
			dev_err(dev->dev, "Left-Justified with TDM is not supported\n");
			return -EINVAL;
		}
		mra |= MCHP_I2SMCC_MRA_FORMAT_LJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		mra |= MCHP_I2SMCC_MRA_FORMAT_TDM;
		break;
	default:
		dev_err(dev->dev, "unsupported bus format\n");
		return -EINVAL;
	}

	switch (dev->fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* cpu is BCLK and LRC master */
		mra |= MCHP_I2SMCC_MRA_MODE_MASTER;
		if (dev->sysclk)
			mra |= MCHP_I2SMCC_MRA_IMCKMODE_GEN;
		set_divs = 1;
		break;
	case SND_SOC_DAIFMT_BP_FC:
		/* cpu is BCLK master */
		mrb |= MCHP_I2SMCC_MRB_CLKSEL_INT;
		set_divs = 1;
		fallthrough;
	case SND_SOC_DAIFMT_BC_FC:
		/* cpu is slave */
		mra |= MCHP_I2SMCC_MRA_MODE_SLAVE;
		if (dev->sysclk)
			dev_warn(dev->dev, "Unable to generate MCLK in Slave mode\n");
		break;
	default:
		dev_err(dev->dev, "unsupported master/slave mode\n");
		return -EINVAL;
	}

	if (dev->fmt & (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_LEFT_J)) {
		/* for I2S and LEFT_J one pin is needed for every 2 channels */
		if (channels > dev->soc->data_pin_pair_num * 2) {
			dev_err(dev->dev,
				"unsupported number of audio channels: %d\n",
				channels);
			return -EINVAL;
		}

		/* enable for interleaved format */
		mrb |= MCHP_I2SMCC_MRB_CRAMODE_REGULAR;

		switch (channels) {
		case 1:
			if (is_playback)
				mra |= MCHP_I2SMCC_MRA_TXMONO;
			else
				mra |= MCHP_I2SMCC_MRA_RXMONO;
			break;
		case 2:
			break;
		case 4:
			mra |= MCHP_I2SMCC_MRA_WIRECFG_I2S_2_TDM_1;
			break;
		case 8:
			mra |= MCHP_I2SMCC_MRA_WIRECFG_I2S_4_TDM_2;
			break;
		default:
			dev_err(dev->dev, "unsupported number of audio channels\n");
			return -EINVAL;
		}

		if (!frame_length)
			frame_length = 2 * params_physical_width(params);
	} else if (dev->fmt & SND_SOC_DAIFMT_DSP_A) {
		mra |= MCHP_I2SMCC_MRA_WIRECFG_TDM(dev->tdm_data_pair);

		if (dev->tdm_slots) {
			if (channels % 2 && channels * 2 <= dev->tdm_slots) {
				/*
				 * Duplicate data for even-numbered channels
				 * to odd-numbered channels
				 */
				if (is_playback)
					mra |= MCHP_I2SMCC_MRA_TXMONO;
				else
					mra |= MCHP_I2SMCC_MRA_RXMONO;
			}
			channels = dev->tdm_slots;
		}

		mra |= MCHP_I2SMCC_MRA_NBCHAN(channels);
		if (!frame_length)
			frame_length = channels * MCHP_I2MCC_TDM_SLOT_WIDTH;
	}

	/*
	 * We must have the same burst size configured
	 * in the DMA transfer and in out IP
	 */
	maxburst = mchp_i2s_mcc_period_to_maxburst(period_bytes, sample_bytes);
	mrb |= MCHP_I2SMCC_MRB_DMACHUNK(maxburst);
	if (is_playback)
		dev->playback.maxburst = maxburst;
	else
		dev->capture.maxburst = maxburst;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		mra |= MCHP_I2SMCC_MRA_DATALENGTH_8_BITS;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		mra |= MCHP_I2SMCC_MRA_DATALENGTH_16_BITS;
		break;
	case SNDRV_PCM_FORMAT_S18_3LE:
		mra |= MCHP_I2SMCC_MRA_DATALENGTH_18_BITS |
		       MCHP_I2SMCC_MRA_IWS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		mra |= MCHP_I2SMCC_MRA_DATALENGTH_20_BITS |
		       MCHP_I2SMCC_MRA_IWS;
		break;
	case SNDRV_PCM_FORMAT_S24_3LE:
		mra |= MCHP_I2SMCC_MRA_DATALENGTH_24_BITS |
		       MCHP_I2SMCC_MRA_IWS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		mra |= MCHP_I2SMCC_MRA_DATALENGTH_24_BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		mra |= MCHP_I2SMCC_MRA_DATALENGTH_32_BITS;
		break;
	default:
		dev_err(dev->dev, "unsupported size/endianness for audio samples\n");
		return -EINVAL;
	}

	if (set_divs) {
		bclk_rate = frame_length * params_rate(params);
		ret = mchp_i2s_mcc_config_divs(dev, bclk_rate, &mra,
					       &rate);
		if (ret) {
			dev_err(dev->dev,
				"unable to configure the divisors: %d\n", ret);
			return ret;
		}
	}

	/* enable FIFO if available */
	if (dev->soc->has_fifo)
		mrb |= MCHP_I2SMCC_MRB_FIFOEN;

	/*
	 * If we are already running, the wanted setup must be
	 * the same with the one that's currently ongoing
	 */
	if (mchp_i2s_mcc_is_running(dev)) {
		u32 mra_cur;
		u32 mrb_cur;

		regmap_read(dev->regmap, MCHP_I2SMCC_MRA, &mra_cur);
		regmap_read(dev->regmap, MCHP_I2SMCC_MRB, &mrb_cur);
		if (mra != mra_cur || mrb != mrb_cur)
			return -EINVAL;

		return 0;
	}

	if (mra & MCHP_I2SMCC_MRA_SRCCLK_GCLK && !dev->gclk_use) {
		/* set the rate */
		ret = clk_set_rate(dev->gclk, rate);
		if (ret) {
			dev_err(dev->dev,
				"unable to set rate %lu to GCLK: %d\n",
				rate, ret);
			return ret;
		}

		ret = clk_prepare(dev->gclk);
		if (ret < 0) {
			dev_err(dev->dev, "unable to prepare GCLK: %d\n", ret);
			return ret;
		}
		dev->gclk_use = 1;
	}

	/* Save the number of channels to know what interrupts to enable */
	dev->channels = channels;

	ret = regmap_write(dev->regmap, MCHP_I2SMCC_MRA, mra);
	if (ret < 0) {
		if (dev->gclk_use) {
			clk_unprepare(dev->gclk);
			dev->gclk_use = 0;
		}
		return ret;
	}
	return regmap_write(dev->regmap, MCHP_I2SMCC_MRB, mrb);
}

static int mchp_i2s_mcc_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct mchp_i2s_mcc_dev *dev = snd_soc_dai_get_drvdata(dai);
	bool is_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	long err;

	if (is_playback) {
		err = wait_event_interruptible_timeout(dev->wq_txrdy,
						       dev->tx_rdy,
						       msecs_to_jiffies(500));
		if (err == 0) {
			dev_warn_once(dev->dev,
				      "Timeout waiting for Tx ready\n");
			if (dev->soc->has_fifo)
				regmap_write(dev->regmap, MCHP_I2SMCC_IDRB,
					     MCHP_I2SMCC_INT_TXFFRDY);
			else
				regmap_write(dev->regmap, MCHP_I2SMCC_IDRA,
					     MCHP_I2SMCC_INT_TXRDY_MASK(dev->channels));

			dev->tx_rdy = 1;
		}
	} else {
		err = wait_event_interruptible_timeout(dev->wq_rxrdy,
						       dev->rx_rdy,
						       msecs_to_jiffies(500));
		if (err == 0) {
			dev_warn_once(dev->dev,
				      "Timeout waiting for Rx ready\n");
			if (dev->soc->has_fifo)
				regmap_write(dev->regmap, MCHP_I2SMCC_IDRB,
					     MCHP_I2SMCC_INT_RXFFRDY);
			else
				regmap_write(dev->regmap, MCHP_I2SMCC_IDRA,
					     MCHP_I2SMCC_INT_RXRDY_MASK(dev->channels));
			dev->rx_rdy = 1;
		}
	}

	if (!mchp_i2s_mcc_is_running(dev)) {
		regmap_write(dev->regmap, MCHP_I2SMCC_CR, MCHP_I2SMCC_CR_CKDIS);

		if (dev->gclk_running) {
			clk_disable(dev->gclk);
			dev->gclk_running = 0;
		}
		if (dev->gclk_use) {
			clk_unprepare(dev->gclk);
			dev->gclk_use = 0;
		}
	}

	return 0;
}

static int mchp_i2s_mcc_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct mchp_i2s_mcc_dev *dev = snd_soc_dai_get_drvdata(dai);
	bool is_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	u32 cr = 0;
	u32 iera = 0, ierb = 0;
	u32 sr;
	int err;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (is_playback)
			cr = MCHP_I2SMCC_CR_TXEN | MCHP_I2SMCC_CR_CKEN;
		else
			cr = MCHP_I2SMCC_CR_RXEN | MCHP_I2SMCC_CR_CKEN;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		regmap_read(dev->regmap, MCHP_I2SMCC_SR, &sr);
		if (is_playback && (sr & MCHP_I2SMCC_SR_TXEN)) {
			cr = MCHP_I2SMCC_CR_TXDIS;
			dev->tx_rdy = 0;
			/*
			 * Enable Tx Ready interrupts on all channels
			 * to assure all data is sent
			 */
			if (dev->soc->has_fifo)
				ierb = MCHP_I2SMCC_INT_TXFFRDY;
			else
				iera = MCHP_I2SMCC_INT_TXRDY_MASK(dev->channels);
		} else if (!is_playback && (sr & MCHP_I2SMCC_SR_RXEN)) {
			cr = MCHP_I2SMCC_CR_RXDIS;
			dev->rx_rdy = 0;
			/*
			 * Enable Rx Ready interrupts on all channels
			 * to assure all data is received
			 */
			if (dev->soc->has_fifo)
				ierb = MCHP_I2SMCC_INT_RXFFRDY;
			else
				iera = MCHP_I2SMCC_INT_RXRDY_MASK(dev->channels);
		}
		break;
	default:
		return -EINVAL;
	}

	if ((cr & MCHP_I2SMCC_CR_CKEN) && dev->gclk_use &&
	    !dev->gclk_running) {
		err = clk_enable(dev->gclk);
		if (err) {
			dev_err_once(dev->dev, "failed to enable GCLK: %d\n",
				     err);
		} else {
			dev->gclk_running = 1;
		}
	}

	if (dev->soc->has_fifo)
		regmap_write(dev->regmap, MCHP_I2SMCC_IERB, ierb);
	else
		regmap_write(dev->regmap, MCHP_I2SMCC_IERA, iera);
	regmap_write(dev->regmap, MCHP_I2SMCC_CR, cr);

	return 0;
}

static int mchp_i2s_mcc_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct mchp_i2s_mcc_dev *dev = snd_soc_dai_get_drvdata(dai);

	/* Software reset the IP if it's not running */
	if (!mchp_i2s_mcc_is_running(dev)) {
		return regmap_write(dev->regmap, MCHP_I2SMCC_CR,
				    MCHP_I2SMCC_CR_SWRST);
	}

	return 0;
}

static int mchp_i2s_mcc_dai_probe(struct snd_soc_dai *dai)
{
	struct mchp_i2s_mcc_dev *dev = snd_soc_dai_get_drvdata(dai);

	init_waitqueue_head(&dev->wq_txrdy);
	init_waitqueue_head(&dev->wq_rxrdy);
	dev->tx_rdy = 1;
	dev->rx_rdy = 1;

	snd_soc_dai_init_dma_data(dai, &dev->playback, &dev->capture);

	return 0;
}

static const struct snd_soc_dai_ops mchp_i2s_mcc_dai_ops = {
	.probe		= mchp_i2s_mcc_dai_probe,
	.set_sysclk	= mchp_i2s_mcc_set_sysclk,
	.set_bclk_ratio	= mchp_i2s_mcc_set_bclk_ratio,
	.startup	= mchp_i2s_mcc_startup,
	.trigger	= mchp_i2s_mcc_trigger,
	.hw_params	= mchp_i2s_mcc_hw_params,
	.hw_free	= mchp_i2s_mcc_hw_free,
	.set_fmt	= mchp_i2s_mcc_set_dai_fmt,
	.set_tdm_slot	= mchp_i2s_mcc_set_dai_tdm_slot,
};

#define MCHP_I2SMCC_RATES              SNDRV_PCM_RATE_8000_192000

#define MCHP_I2SMCC_FORMATS	(SNDRV_PCM_FMTBIT_S8 |          \
				 SNDRV_PCM_FMTBIT_S16_LE |      \
				 SNDRV_PCM_FMTBIT_S18_3LE |     \
				 SNDRV_PCM_FMTBIT_S20_3LE |     \
				 SNDRV_PCM_FMTBIT_S24_3LE |     \
				 SNDRV_PCM_FMTBIT_S24_LE |      \
				 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mchp_i2s_mcc_dai = {
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = MCHP_I2SMCC_RATES,
		.formats = MCHP_I2SMCC_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = MCHP_I2SMCC_RATES,
		.formats = MCHP_I2SMCC_FORMATS,
	},
	.ops = &mchp_i2s_mcc_dai_ops,
	.symmetric_rate = 1,
	.symmetric_sample_bits = 1,
	.symmetric_channels = 1,
};

static const struct snd_soc_component_driver mchp_i2s_mcc_component = {
	.name			= "mchp-i2s-mcc",
	.legacy_dai_naming	= 1,
};

#ifdef CONFIG_OF
static struct mchp_i2s_mcc_soc_data mchp_i2s_mcc_sam9x60 = {
	.data_pin_pair_num = 1,
};

static struct mchp_i2s_mcc_soc_data mchp_i2s_mcc_sama7g5 = {
	.data_pin_pair_num = 4,
	.has_fifo = true,
};

static const struct of_device_id mchp_i2s_mcc_dt_ids[] = {
	{
		.compatible = "microchip,sam9x60-i2smcc",
		.data = &mchp_i2s_mcc_sam9x60,
	},
	{
		.compatible = "microchip,sama7g5-i2smcc",
		.data = &mchp_i2s_mcc_sama7g5,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mchp_i2s_mcc_dt_ids);
#endif

static int mchp_i2s_mcc_soc_data_parse(struct platform_device *pdev,
				       struct mchp_i2s_mcc_dev *dev)
{
	int err;

	if (!dev->soc) {
		dev_err(&pdev->dev, "failed to get soc data\n");
		return -ENODEV;
	}

	if (dev->soc->data_pin_pair_num == 1)
		return 0;

	err = of_property_read_u8(pdev->dev.of_node, "microchip,tdm-data-pair",
				  &dev->tdm_data_pair);
	if (err < 0 && err != -EINVAL) {
		dev_err(&pdev->dev,
			"bad property data for 'microchip,tdm-data-pair': %d",
			err);
		return err;
	}
	if (err == -EINVAL) {
		dev_info(&pdev->dev,
			 "'microchip,tdm-data-pair' not found; assuming DIN/DOUT 0 for TDM\n");
		dev->tdm_data_pair = 0;
	} else {
		if (dev->tdm_data_pair > dev->soc->data_pin_pair_num - 1) {
			dev_err(&pdev->dev,
				"invalid value for 'microchip,tdm-data-pair': %d\n",
				dev->tdm_data_pair);
			return -EINVAL;
		}
		dev_dbg(&pdev->dev, "TMD format on DIN/DOUT %d pins\n",
			dev->tdm_data_pair);
	}

	return 0;
}

static int mchp_i2s_mcc_probe(struct platform_device *pdev)
{
	struct mchp_i2s_mcc_dev *dev;
	struct resource *mem;
	struct regmap *regmap;
	void __iomem *base;
	u32 version;
	int irq;
	int err;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base,
				       &mchp_i2s_mcc_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_irq(&pdev->dev, irq, mchp_i2s_mcc_interrupt, 0,
			       dev_name(&pdev->dev), dev);
	if (err)
		return err;

	dev->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(dev->pclk)) {
		err = PTR_ERR(dev->pclk);
		dev_err(&pdev->dev,
			"failed to get the peripheral clock: %d\n", err);
		return err;
	}

	/* Get the optional generated clock */
	dev->gclk = devm_clk_get(&pdev->dev, "gclk");
	if (IS_ERR(dev->gclk)) {
		if (PTR_ERR(dev->gclk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_warn(&pdev->dev,
			 "generated clock not found: %d\n", err);
		dev->gclk = NULL;
	}

	dev->soc = of_device_get_match_data(&pdev->dev);
	err = mchp_i2s_mcc_soc_data_parse(pdev, dev);
	if (err < 0)
		return err;

	dev->dev = &pdev->dev;
	dev->regmap = regmap;
	platform_set_drvdata(pdev, dev);

	err = clk_prepare_enable(dev->pclk);
	if (err) {
		dev_err(&pdev->dev,
			"failed to enable the peripheral clock: %d\n", err);
		return err;
	}

	err = devm_snd_soc_register_component(&pdev->dev,
					      &mchp_i2s_mcc_component,
					      &mchp_i2s_mcc_dai, 1);
	if (err) {
		dev_err(&pdev->dev, "failed to register DAI: %d\n", err);
		clk_disable_unprepare(dev->pclk);
		return err;
	}

	dev->playback.addr	= (dma_addr_t)mem->start + MCHP_I2SMCC_THR;
	dev->capture.addr	= (dma_addr_t)mem->start + MCHP_I2SMCC_RHR;

	err = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (err) {
		dev_err(&pdev->dev, "failed to register PCM: %d\n", err);
		clk_disable_unprepare(dev->pclk);
		return err;
	}

	/* Get IP version. */
	regmap_read(dev->regmap, MCHP_I2SMCC_VERSION, &version);
	dev_info(&pdev->dev, "hw version: %#lx\n",
		 version & MCHP_I2SMCC_VERSION_MASK);

	return 0;
}

static void mchp_i2s_mcc_remove(struct platform_device *pdev)
{
	struct mchp_i2s_mcc_dev *dev = platform_get_drvdata(pdev);

	clk_disable_unprepare(dev->pclk);
}

static struct platform_driver mchp_i2s_mcc_driver = {
	.driver		= {
		.name	= "mchp_i2s_mcc",
		.of_match_table	= mchp_i2s_mcc_dt_ids,
	},
	.probe		= mchp_i2s_mcc_probe,
	.remove		= mchp_i2s_mcc_remove,
};
module_platform_driver(mchp_i2s_mcc_driver);

MODULE_DESCRIPTION("Microchip I2S Multi-Channel Controller driver");
MODULE_AUTHOR("Codrin Ciubotariu <codrin.ciubotariu@microchip.com>");
MODULE_LICENSE("GPL v2");
