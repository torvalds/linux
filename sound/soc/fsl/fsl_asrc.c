// SPDX-License-Identifier: GPL-2.0
//
// Freescale ASRC ALSA SoC Digital Audio Interface (DAI) driver
//
// Copyright (C) 2014 Freescale Semiconductor, Inc.
//
// Author: Nicolin Chen <nicoleotsuka@gmail.com>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/dma/imx-dma.h>
#include <linux/pm_runtime.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "fsl_asrc.h"

#define IDEAL_RATIO_DECIMAL_DEPTH 26
#define DIVIDER_NUM  64

#define pair_err(fmt, ...) \
	dev_err(&asrc->pdev->dev, "Pair %c: " fmt, 'A' + index, ##__VA_ARGS__)

#define pair_dbg(fmt, ...) \
	dev_dbg(&asrc->pdev->dev, "Pair %c: " fmt, 'A' + index, ##__VA_ARGS__)

/* Corresponding to process_option */
static unsigned int supported_asrc_rate[] = {
	5512, 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
	64000, 88200, 96000, 128000, 176400, 192000,
};

static struct snd_pcm_hw_constraint_list fsl_asrc_rate_constraints = {
	.count = ARRAY_SIZE(supported_asrc_rate),
	.list = supported_asrc_rate,
};

/*
 * The following tables map the relationship between asrc_inclk/asrc_outclk in
 * fsl_asrc.h and the registers of ASRCSR
 */
static unsigned char input_clk_map_imx35[ASRC_CLK_MAP_LEN] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};

static unsigned char output_clk_map_imx35[ASRC_CLK_MAP_LEN] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};

/* i.MX53 uses the same map for input and output */
static unsigned char input_clk_map_imx53[ASRC_CLK_MAP_LEN] = {
/*	0x0  0x1  0x2  0x3  0x4  0x5  0x6  0x7  0x8  0x9  0xa  0xb  0xc  0xd  0xe  0xf */
	0x0, 0x1, 0x2, 0x7, 0x4, 0x5, 0x6, 0x3, 0x8, 0x9, 0xa, 0xb, 0xc, 0xf, 0xe, 0xd,
	0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7,
	0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7,
};

static unsigned char output_clk_map_imx53[ASRC_CLK_MAP_LEN] = {
/*	0x0  0x1  0x2  0x3  0x4  0x5  0x6  0x7  0x8  0x9  0xa  0xb  0xc  0xd  0xe  0xf */
	0x8, 0x9, 0xa, 0x7, 0xc, 0x5, 0x6, 0xb, 0x0, 0x1, 0x2, 0x3, 0x4, 0xf, 0xe, 0xd,
	0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7,
	0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7,
};

/*
 * i.MX8QM/i.MX8QXP uses the same map for input and output.
 * clk_map_imx8qm[0] is for i.MX8QM asrc0
 * clk_map_imx8qm[1] is for i.MX8QM asrc1
 * clk_map_imx8qxp[0] is for i.MX8QXP asrc0
 * clk_map_imx8qxp[1] is for i.MX8QXP asrc1
 */
static unsigned char clk_map_imx8qm[2][ASRC_CLK_MAP_LEN] = {
	{
	0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0x0,
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
	0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
	},
	{
	0xf, 0xf, 0xf, 0xf, 0xf, 0x7, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0x0,
	0x0, 0x1, 0x2, 0x3, 0xb, 0xc, 0xf, 0xf, 0xd, 0xe, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
	0x4, 0x5, 0x6, 0xf, 0x8, 0x9, 0xa, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
	},
};

static unsigned char clk_map_imx8qxp[2][ASRC_CLK_MAP_LEN] = {
	{
	0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0x0,
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0xf, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xf, 0xf,
	0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
	},
	{
	0xf, 0xf, 0xf, 0xf, 0xf, 0x7, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0x0,
	0x0, 0x1, 0x2, 0x3, 0x7, 0x8, 0xf, 0xf, 0x9, 0xa, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
	0xf, 0xf, 0x6, 0xf, 0xf, 0xf, 0xa, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
	},
};

/*
 * According to RM, the divider range is 1 ~ 8,
 * prescaler is power of 2 from 1 ~ 128.
 */
static int asrc_clk_divider[DIVIDER_NUM] = {
	1,  2,  4,  8,  16,  32,  64,  128,  /* divider = 1 */
	2,  4,  8, 16,  32,  64, 128,  256,  /* divider = 2 */
	3,  6, 12, 24,  48,  96, 192,  384,  /* divider = 3 */
	4,  8, 16, 32,  64, 128, 256,  512,  /* divider = 4 */
	5, 10, 20, 40,  80, 160, 320,  640,  /* divider = 5 */
	6, 12, 24, 48,  96, 192, 384,  768,  /* divider = 6 */
	7, 14, 28, 56, 112, 224, 448,  896,  /* divider = 7 */
	8, 16, 32, 64, 128, 256, 512, 1024,  /* divider = 8 */
};

/*
 * Check if the divider is available for internal ratio mode
 */
static bool fsl_asrc_divider_avail(int clk_rate, int rate, int *div)
{
	u32 rem, i;
	u64 n;

	if (div)
		*div = 0;

	if (clk_rate == 0 || rate == 0)
		return false;

	n = clk_rate;
	rem = do_div(n, rate);

	if (div)
		*div = n;

	if (rem != 0)
		return false;

	for (i = 0; i < DIVIDER_NUM; i++) {
		if (n == asrc_clk_divider[i])
			break;
	}

	if (i == DIVIDER_NUM)
		return false;

	return true;
}

/**
 * fsl_asrc_sel_proc - Select the pre-processing and post-processing options
 * @inrate: input sample rate
 * @outrate: output sample rate
 * @pre_proc: return value for pre-processing option
 * @post_proc: return value for post-processing option
 *
 * Make sure to exclude following unsupported cases before
 * calling this function:
 * 1) inrate > 8.125 * outrate
 * 2) inrate > 16.125 * outrate
 *
 */
static void fsl_asrc_sel_proc(int inrate, int outrate,
			     int *pre_proc, int *post_proc)
{
	bool post_proc_cond2;
	bool post_proc_cond0;

	/* select pre_proc between [0, 2] */
	if (inrate * 8 > 33 * outrate)
		*pre_proc = 2;
	else if (inrate * 8 > 15 * outrate) {
		if (inrate > 152000)
			*pre_proc = 2;
		else
			*pre_proc = 1;
	} else if (inrate < 76000)
		*pre_proc = 0;
	else if (inrate > 152000)
		*pre_proc = 2;
	else
		*pre_proc = 1;

	/* Condition for selection of post-processing */
	post_proc_cond2 = (inrate * 15 > outrate * 16 && outrate < 56000) ||
			  (inrate > 56000 && outrate < 56000);
	post_proc_cond0 = inrate * 23 < outrate * 8;

	if (post_proc_cond2)
		*post_proc = 2;
	else if (post_proc_cond0)
		*post_proc = 0;
	else
		*post_proc = 1;
}

/**
 * fsl_asrc_request_pair - Request ASRC pair
 * @channels: number of channels
 * @pair: pointer to pair
 *
 * It assigns pair by the order of A->C->B because allocation of pair B,
 * within range [ANCA, ANCA+ANCB-1], depends on the channels of pair A
 * while pair A and pair C are comparatively independent.
 */
static int fsl_asrc_request_pair(int channels, struct fsl_asrc_pair *pair)
{
	enum asrc_pair_index index = ASRC_INVALID_PAIR;
	struct fsl_asrc *asrc = pair->asrc;
	struct device *dev = &asrc->pdev->dev;
	unsigned long lock_flags;
	int i, ret = 0;

	spin_lock_irqsave(&asrc->lock, lock_flags);

	for (i = ASRC_PAIR_A; i < ASRC_PAIR_MAX_NUM; i++) {
		if (asrc->pair[i] != NULL)
			continue;

		index = i;

		if (i != ASRC_PAIR_B)
			break;
	}

	if (index == ASRC_INVALID_PAIR) {
		dev_err(dev, "all pairs are busy now\n");
		ret = -EBUSY;
	} else if (asrc->channel_avail < channels) {
		dev_err(dev, "can't afford required channels: %d\n", channels);
		ret = -EINVAL;
	} else {
		asrc->channel_avail -= channels;
		asrc->pair[index] = pair;
		pair->channels = channels;
		pair->index = index;
	}

	spin_unlock_irqrestore(&asrc->lock, lock_flags);

	return ret;
}

/**
 * fsl_asrc_release_pair - Release ASRC pair
 * @pair: pair to release
 *
 * It clears the resource from asrc and releases the occupied channels.
 */
static void fsl_asrc_release_pair(struct fsl_asrc_pair *pair)
{
	struct fsl_asrc *asrc = pair->asrc;
	enum asrc_pair_index index = pair->index;
	unsigned long lock_flags;

	/* Make sure the pair is disabled */
	regmap_update_bits(asrc->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_MASK(index), 0);

	spin_lock_irqsave(&asrc->lock, lock_flags);

	asrc->channel_avail += pair->channels;
	asrc->pair[index] = NULL;
	pair->error = 0;

	spin_unlock_irqrestore(&asrc->lock, lock_flags);
}

/**
 * fsl_asrc_set_watermarks- configure input and output thresholds
 * @pair: pointer to pair
 * @in: input threshold
 * @out: output threshold
 */
static void fsl_asrc_set_watermarks(struct fsl_asrc_pair *pair, u32 in, u32 out)
{
	struct fsl_asrc *asrc = pair->asrc;
	enum asrc_pair_index index = pair->index;

	regmap_update_bits(asrc->regmap, REG_ASRMCR(index),
			   ASRMCRi_EXTTHRSHi_MASK |
			   ASRMCRi_INFIFO_THRESHOLD_MASK |
			   ASRMCRi_OUTFIFO_THRESHOLD_MASK,
			   ASRMCRi_EXTTHRSHi |
			   ASRMCRi_INFIFO_THRESHOLD(in) |
			   ASRMCRi_OUTFIFO_THRESHOLD(out));
}

/**
 * fsl_asrc_cal_asrck_divisor - Calculate the total divisor between asrck clock rate and sample rate
 * @pair: pointer to pair
 * @div: divider
 *
 * It follows the formula clk_rate = samplerate * (2 ^ prescaler) * divider
 */
static u32 fsl_asrc_cal_asrck_divisor(struct fsl_asrc_pair *pair, u32 div)
{
	u32 ps;

	/* Calculate the divisors: prescaler [2^0, 2^7], divder [1, 8] */
	for (ps = 0; div > 8; ps++)
		div >>= 1;

	return ((div - 1) << ASRCDRi_AxCPi_WIDTH) | ps;
}

/**
 * fsl_asrc_set_ideal_ratio - Calculate and set the ratio for Ideal Ratio mode only
 * @pair: pointer to pair
 * @inrate: input rate
 * @outrate: output rate
 *
 * The ratio is a 32-bit fixed point value with 26 fractional bits.
 */
static int fsl_asrc_set_ideal_ratio(struct fsl_asrc_pair *pair,
				    int inrate, int outrate)
{
	struct fsl_asrc *asrc = pair->asrc;
	enum asrc_pair_index index = pair->index;
	unsigned long ratio;
	int i;

	if (!outrate) {
		pair_err("output rate should not be zero\n");
		return -EINVAL;
	}

	/* Calculate the intergal part of the ratio */
	ratio = (inrate / outrate) << IDEAL_RATIO_DECIMAL_DEPTH;

	/* ... and then the 26 depth decimal part */
	inrate %= outrate;

	for (i = 1; i <= IDEAL_RATIO_DECIMAL_DEPTH; i++) {
		inrate <<= 1;

		if (inrate < outrate)
			continue;

		ratio |= 1 << (IDEAL_RATIO_DECIMAL_DEPTH - i);
		inrate -= outrate;

		if (!inrate)
			break;
	}

	regmap_write(asrc->regmap, REG_ASRIDRL(index), ratio);
	regmap_write(asrc->regmap, REG_ASRIDRH(index), ratio >> 24);

	return 0;
}

/**
 * fsl_asrc_config_pair - Configure the assigned ASRC pair
 * @pair: pointer to pair
 * @use_ideal_rate: boolean configuration
 *
 * It configures those ASRC registers according to a configuration instance
 * of struct asrc_config which includes in/output sample rate, width, channel
 * and clock settings.
 *
 * Note:
 * The ideal ratio configuration can work with a flexible clock rate setting.
 * Using IDEAL_RATIO_RATE gives a faster converting speed but overloads ASRC.
 * For a regular audio playback, the clock rate should not be slower than an
 * clock rate aligning with the output sample rate; For a use case requiring
 * faster conversion, set use_ideal_rate to have the faster speed.
 */
static int fsl_asrc_config_pair(struct fsl_asrc_pair *pair, bool use_ideal_rate)
{
	struct fsl_asrc_pair_priv *pair_priv = pair->private;
	struct asrc_config *config = pair_priv->config;
	struct fsl_asrc *asrc = pair->asrc;
	struct fsl_asrc_priv *asrc_priv = asrc->private;
	enum asrc_pair_index index = pair->index;
	enum asrc_word_width input_word_width;
	enum asrc_word_width output_word_width;
	u32 inrate, outrate, indiv, outdiv;
	u32 clk_index[2], div[2];
	u64 clk_rate;
	int in, out, channels;
	int pre_proc, post_proc;
	struct clk *clk;
	bool ideal, div_avail;

	if (!config) {
		pair_err("invalid pair config\n");
		return -EINVAL;
	}

	/* Validate channels */
	if (config->channel_num < 1 || config->channel_num > 10) {
		pair_err("does not support %d channels\n", config->channel_num);
		return -EINVAL;
	}

	switch (snd_pcm_format_width(config->input_format)) {
	case 8:
		input_word_width = ASRC_WIDTH_8_BIT;
		break;
	case 16:
		input_word_width = ASRC_WIDTH_16_BIT;
		break;
	case 24:
		input_word_width = ASRC_WIDTH_24_BIT;
		break;
	default:
		pair_err("does not support this input format, %d\n",
			 config->input_format);
		return -EINVAL;
	}

	switch (snd_pcm_format_width(config->output_format)) {
	case 16:
		output_word_width = ASRC_WIDTH_16_BIT;
		break;
	case 24:
		output_word_width = ASRC_WIDTH_24_BIT;
		break;
	default:
		pair_err("does not support this output format, %d\n",
			 config->output_format);
		return -EINVAL;
	}

	inrate = config->input_sample_rate;
	outrate = config->output_sample_rate;
	ideal = config->inclk == INCLK_NONE;

	/* Validate input and output sample rates */
	for (in = 0; in < ARRAY_SIZE(supported_asrc_rate); in++)
		if (inrate == supported_asrc_rate[in])
			break;

	if (in == ARRAY_SIZE(supported_asrc_rate)) {
		pair_err("unsupported input sample rate: %dHz\n", inrate);
		return -EINVAL;
	}

	for (out = 0; out < ARRAY_SIZE(supported_asrc_rate); out++)
		if (outrate == supported_asrc_rate[out])
			break;

	if (out == ARRAY_SIZE(supported_asrc_rate)) {
		pair_err("unsupported output sample rate: %dHz\n", outrate);
		return -EINVAL;
	}

	if ((outrate >= 5512 && outrate <= 30000) &&
	    (outrate > 24 * inrate || inrate > 8 * outrate)) {
		pair_err("exceed supported ratio range [1/24, 8] for \
				inrate/outrate: %d/%d\n", inrate, outrate);
		return -EINVAL;
	}

	/* Validate input and output clock sources */
	clk_index[IN] = asrc_priv->clk_map[IN][config->inclk];
	clk_index[OUT] = asrc_priv->clk_map[OUT][config->outclk];

	/* We only have output clock for ideal ratio mode */
	clk = asrc_priv->asrck_clk[clk_index[ideal ? OUT : IN]];

	clk_rate = clk_get_rate(clk);
	div_avail = fsl_asrc_divider_avail(clk_rate, inrate, &div[IN]);

	/*
	 * The divider range is [1, 1024], defined by the hardware. For non-
	 * ideal ratio configuration, clock rate has to be strictly aligned
	 * with the sample rate. For ideal ratio configuration, clock rates
	 * only result in different converting speeds. So remainder does not
	 * matter, as long as we keep the divider within its valid range.
	 */
	if (div[IN] == 0 || (!ideal && !div_avail)) {
		pair_err("failed to support input sample rate %dHz by asrck_%x\n",
				inrate, clk_index[ideal ? OUT : IN]);
		return -EINVAL;
	}

	div[IN] = min_t(u32, 1024, div[IN]);

	clk = asrc_priv->asrck_clk[clk_index[OUT]];
	clk_rate = clk_get_rate(clk);
	if (ideal && use_ideal_rate)
		div_avail = fsl_asrc_divider_avail(clk_rate, IDEAL_RATIO_RATE, &div[OUT]);
	else
		div_avail = fsl_asrc_divider_avail(clk_rate, outrate, &div[OUT]);

	/* Output divider has the same limitation as the input one */
	if (div[OUT] == 0 || (!ideal && !div_avail)) {
		pair_err("failed to support output sample rate %dHz by asrck_%x\n",
				outrate, clk_index[OUT]);
		return -EINVAL;
	}

	div[OUT] = min_t(u32, 1024, div[OUT]);

	/* Set the channel number */
	channels = config->channel_num;

	if (asrc_priv->soc->channel_bits < 4)
		channels /= 2;

	/* Update channels for current pair */
	regmap_update_bits(asrc->regmap, REG_ASRCNCR,
			   ASRCNCR_ANCi_MASK(index, asrc_priv->soc->channel_bits),
			   ASRCNCR_ANCi(index, channels, asrc_priv->soc->channel_bits));

	/* Default setting: Automatic selection for processing mode */
	regmap_update_bits(asrc->regmap, REG_ASRCTR,
			   ASRCTR_ATSi_MASK(index), ASRCTR_ATS(index));
	regmap_update_bits(asrc->regmap, REG_ASRCTR,
			   ASRCTR_USRi_MASK(index), 0);

	/* Set the input and output clock sources */
	regmap_update_bits(asrc->regmap, REG_ASRCSR,
			   ASRCSR_AICSi_MASK(index) | ASRCSR_AOCSi_MASK(index),
			   ASRCSR_AICS(index, clk_index[IN]) |
			   ASRCSR_AOCS(index, clk_index[OUT]));

	/* Calculate the input clock divisors */
	indiv = fsl_asrc_cal_asrck_divisor(pair, div[IN]);
	outdiv = fsl_asrc_cal_asrck_divisor(pair, div[OUT]);

	/* Suppose indiv and outdiv includes prescaler, so add its MASK too */
	regmap_update_bits(asrc->regmap, REG_ASRCDR(index),
			   ASRCDRi_AOCPi_MASK(index) | ASRCDRi_AICPi_MASK(index) |
			   ASRCDRi_AOCDi_MASK(index) | ASRCDRi_AICDi_MASK(index),
			   ASRCDRi_AOCP(index, outdiv) | ASRCDRi_AICP(index, indiv));

	/* Implement word_width configurations */
	regmap_update_bits(asrc->regmap, REG_ASRMCR1(index),
			   ASRMCR1i_OW16_MASK | ASRMCR1i_IWD_MASK,
			   ASRMCR1i_OW16(output_word_width) |
			   ASRMCR1i_IWD(input_word_width));

	/* Enable BUFFER STALL */
	regmap_update_bits(asrc->regmap, REG_ASRMCR(index),
			   ASRMCRi_BUFSTALLi_MASK, ASRMCRi_BUFSTALLi);

	/* Set default thresholds for input and output FIFO */
	fsl_asrc_set_watermarks(pair, ASRC_INPUTFIFO_THRESHOLD,
				ASRC_INPUTFIFO_THRESHOLD);

	/* Configure the following only for Ideal Ratio mode */
	if (!ideal)
		return 0;

	/* Clear ASTSx bit to use Ideal Ratio mode */
	regmap_update_bits(asrc->regmap, REG_ASRCTR,
			   ASRCTR_ATSi_MASK(index), 0);

	/* Enable Ideal Ratio mode */
	regmap_update_bits(asrc->regmap, REG_ASRCTR,
			   ASRCTR_IDRi_MASK(index) | ASRCTR_USRi_MASK(index),
			   ASRCTR_IDR(index) | ASRCTR_USR(index));

	fsl_asrc_sel_proc(inrate, outrate, &pre_proc, &post_proc);

	/* Apply configurations for pre- and post-processing */
	regmap_update_bits(asrc->regmap, REG_ASRCFG,
			   ASRCFG_PREMODi_MASK(index) |	ASRCFG_POSTMODi_MASK(index),
			   ASRCFG_PREMOD(index, pre_proc) |
			   ASRCFG_POSTMOD(index, post_proc));

	return fsl_asrc_set_ideal_ratio(pair, inrate, outrate);
}

/**
 * fsl_asrc_start_pair - Start the assigned ASRC pair
 * @pair: pointer to pair
 *
 * It enables the assigned pair and makes it stopped at the stall level.
 */
static void fsl_asrc_start_pair(struct fsl_asrc_pair *pair)
{
	struct fsl_asrc *asrc = pair->asrc;
	enum asrc_pair_index index = pair->index;
	int reg, retry = 10, i;

	/* Enable the current pair */
	regmap_update_bits(asrc->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_MASK(index), ASRCTR_ASRCE(index));

	/* Wait for status of initialization */
	do {
		udelay(5);
		regmap_read(asrc->regmap, REG_ASRCFG, &reg);
		reg &= ASRCFG_INIRQi_MASK(index);
	} while (!reg && --retry);

	/* Make the input fifo to ASRC STALL level */
	regmap_read(asrc->regmap, REG_ASRCNCR, &reg);
	for (i = 0; i < pair->channels * 4; i++)
		regmap_write(asrc->regmap, REG_ASRDI(index), 0);

	/* Enable overload interrupt */
	regmap_write(asrc->regmap, REG_ASRIER, ASRIER_AOLIE);
}

/**
 * fsl_asrc_stop_pair - Stop the assigned ASRC pair
 * @pair: pointer to pair
 */
static void fsl_asrc_stop_pair(struct fsl_asrc_pair *pair)
{
	struct fsl_asrc *asrc = pair->asrc;
	enum asrc_pair_index index = pair->index;

	/* Stop the current pair */
	regmap_update_bits(asrc->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_MASK(index), 0);
}

/**
 * fsl_asrc_get_dma_channel- Get DMA channel according to the pair and direction.
 * @pair: pointer to pair
 * @dir: DMA direction
 */
static struct dma_chan *fsl_asrc_get_dma_channel(struct fsl_asrc_pair *pair,
						 bool dir)
{
	struct fsl_asrc *asrc = pair->asrc;
	enum asrc_pair_index index = pair->index;
	char name[4];

	sprintf(name, "%cx%c", dir == IN ? 'r' : 't', index + 'a');

	return dma_request_slave_channel(&asrc->pdev->dev, name);
}

static int fsl_asrc_dai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct fsl_asrc *asrc = snd_soc_dai_get_drvdata(dai);
	struct fsl_asrc_priv *asrc_priv = asrc->private;

	/* Odd channel number is not valid for older ASRC (channel_bits==3) */
	if (asrc_priv->soc->channel_bits == 3)
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_CHANNELS, 2);


	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &fsl_asrc_rate_constraints);
}

/* Select proper clock source for internal ratio mode */
static void fsl_asrc_select_clk(struct fsl_asrc_priv *asrc_priv,
				struct fsl_asrc_pair *pair,
				int in_rate,
				int out_rate)
{
	struct fsl_asrc_pair_priv *pair_priv = pair->private;
	struct asrc_config *config = pair_priv->config;
	int rate[2], select_clk[2]; /* Array size 2 means IN and OUT */
	int clk_rate, clk_index;
	int i, j;

	rate[IN] = in_rate;
	rate[OUT] = out_rate;

	/* Select proper clock source for internal ratio mode */
	for (j = 0; j < 2; j++) {
		for (i = 0; i < ASRC_CLK_MAP_LEN; i++) {
			clk_index = asrc_priv->clk_map[j][i];
			clk_rate = clk_get_rate(asrc_priv->asrck_clk[clk_index]);
			/* Only match a perfect clock source with no remainder */
			if (fsl_asrc_divider_avail(clk_rate, rate[j], NULL))
				break;
		}

		select_clk[j] = i;
	}

	/* Switch to ideal ratio mode if there is no proper clock source */
	if (select_clk[IN] == ASRC_CLK_MAP_LEN || select_clk[OUT] == ASRC_CLK_MAP_LEN) {
		select_clk[IN] = INCLK_NONE;
		select_clk[OUT] = OUTCLK_ASRCK1_CLK;
	}

	config->inclk = select_clk[IN];
	config->outclk = select_clk[OUT];
}

static int fsl_asrc_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct fsl_asrc *asrc = snd_soc_dai_get_drvdata(dai);
	struct fsl_asrc_priv *asrc_priv = asrc->private;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;
	struct fsl_asrc_pair_priv *pair_priv = pair->private;
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	struct asrc_config config;
	int ret;

	ret = fsl_asrc_request_pair(channels, pair);
	if (ret) {
		dev_err(dai->dev, "fail to request asrc pair\n");
		return ret;
	}

	pair_priv->config = &config;

	config.pair = pair->index;
	config.channel_num = channels;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		config.input_format   = params_format(params);
		config.output_format  = asrc->asrc_format;
		config.input_sample_rate  = rate;
		config.output_sample_rate = asrc->asrc_rate;
	} else {
		config.input_format   = asrc->asrc_format;
		config.output_format  = params_format(params);
		config.input_sample_rate  = asrc->asrc_rate;
		config.output_sample_rate = rate;
	}

	fsl_asrc_select_clk(asrc_priv, pair,
			    config.input_sample_rate,
			    config.output_sample_rate);

	ret = fsl_asrc_config_pair(pair, false);
	if (ret) {
		dev_err(dai->dev, "fail to config asrc pair\n");
		return ret;
	}

	return 0;
}

static int fsl_asrc_dai_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;

	if (pair)
		fsl_asrc_release_pair(pair);

	return 0;
}

static int fsl_asrc_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		fsl_asrc_start_pair(pair);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		fsl_asrc_stop_pair(pair);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops fsl_asrc_dai_ops = {
	.startup      = fsl_asrc_dai_startup,
	.hw_params    = fsl_asrc_dai_hw_params,
	.hw_free      = fsl_asrc_dai_hw_free,
	.trigger      = fsl_asrc_dai_trigger,
};

static int fsl_asrc_dai_probe(struct snd_soc_dai *dai)
{
	struct fsl_asrc *asrc = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &asrc->dma_params_tx,
				  &asrc->dma_params_rx);

	return 0;
}

#define FSL_ASRC_FORMATS	(SNDRV_PCM_FMTBIT_S24_LE | \
				 SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_S24_3LE)

static struct snd_soc_dai_driver fsl_asrc_dai = {
	.probe = fsl_asrc_dai_probe,
	.playback = {
		.stream_name = "ASRC-Playback",
		.channels_min = 1,
		.channels_max = 10,
		.rate_min = 5512,
		.rate_max = 192000,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = FSL_ASRC_FORMATS |
			   SNDRV_PCM_FMTBIT_S8,
	},
	.capture = {
		.stream_name = "ASRC-Capture",
		.channels_min = 1,
		.channels_max = 10,
		.rate_min = 5512,
		.rate_max = 192000,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = FSL_ASRC_FORMATS,
	},
	.ops = &fsl_asrc_dai_ops,
};

static bool fsl_asrc_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_ASRCTR:
	case REG_ASRIER:
	case REG_ASRCNCR:
	case REG_ASRCFG:
	case REG_ASRCSR:
	case REG_ASRCDR1:
	case REG_ASRCDR2:
	case REG_ASRSTR:
	case REG_ASRPM1:
	case REG_ASRPM2:
	case REG_ASRPM3:
	case REG_ASRPM4:
	case REG_ASRPM5:
	case REG_ASRTFR1:
	case REG_ASRCCR:
	case REG_ASRDOA:
	case REG_ASRDOB:
	case REG_ASRDOC:
	case REG_ASRIDRHA:
	case REG_ASRIDRLA:
	case REG_ASRIDRHB:
	case REG_ASRIDRLB:
	case REG_ASRIDRHC:
	case REG_ASRIDRLC:
	case REG_ASR76K:
	case REG_ASR56K:
	case REG_ASRMCRA:
	case REG_ASRFSTA:
	case REG_ASRMCRB:
	case REG_ASRFSTB:
	case REG_ASRMCRC:
	case REG_ASRFSTC:
	case REG_ASRMCR1A:
	case REG_ASRMCR1B:
	case REG_ASRMCR1C:
		return true;
	default:
		return false;
	}
}

static bool fsl_asrc_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_ASRSTR:
	case REG_ASRDIA:
	case REG_ASRDIB:
	case REG_ASRDIC:
	case REG_ASRDOA:
	case REG_ASRDOB:
	case REG_ASRDOC:
	case REG_ASRFSTA:
	case REG_ASRFSTB:
	case REG_ASRFSTC:
	case REG_ASRCFG:
		return true;
	default:
		return false;
	}
}

static bool fsl_asrc_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_ASRCTR:
	case REG_ASRIER:
	case REG_ASRCNCR:
	case REG_ASRCFG:
	case REG_ASRCSR:
	case REG_ASRCDR1:
	case REG_ASRCDR2:
	case REG_ASRSTR:
	case REG_ASRPM1:
	case REG_ASRPM2:
	case REG_ASRPM3:
	case REG_ASRPM4:
	case REG_ASRPM5:
	case REG_ASRTFR1:
	case REG_ASRCCR:
	case REG_ASRDIA:
	case REG_ASRDIB:
	case REG_ASRDIC:
	case REG_ASRIDRHA:
	case REG_ASRIDRLA:
	case REG_ASRIDRHB:
	case REG_ASRIDRLB:
	case REG_ASRIDRHC:
	case REG_ASRIDRLC:
	case REG_ASR76K:
	case REG_ASR56K:
	case REG_ASRMCRA:
	case REG_ASRMCRB:
	case REG_ASRMCRC:
	case REG_ASRMCR1A:
	case REG_ASRMCR1B:
	case REG_ASRMCR1C:
		return true;
	default:
		return false;
	}
}

static struct reg_default fsl_asrc_reg[] = {
	{ REG_ASRCTR, 0x0000 }, { REG_ASRIER, 0x0000 },
	{ REG_ASRCNCR, 0x0000 }, { REG_ASRCFG, 0x0000 },
	{ REG_ASRCSR, 0x0000 }, { REG_ASRCDR1, 0x0000 },
	{ REG_ASRCDR2, 0x0000 }, { REG_ASRSTR, 0x0000 },
	{ REG_ASRRA, 0x0000 }, { REG_ASRRB, 0x0000 },
	{ REG_ASRRC, 0x0000 }, { REG_ASRPM1, 0x0000 },
	{ REG_ASRPM2, 0x0000 }, { REG_ASRPM3, 0x0000 },
	{ REG_ASRPM4, 0x0000 }, { REG_ASRPM5, 0x0000 },
	{ REG_ASRTFR1, 0x0000 }, { REG_ASRCCR, 0x0000 },
	{ REG_ASRDIA, 0x0000 }, { REG_ASRDOA, 0x0000 },
	{ REG_ASRDIB, 0x0000 }, { REG_ASRDOB, 0x0000 },
	{ REG_ASRDIC, 0x0000 }, { REG_ASRDOC, 0x0000 },
	{ REG_ASRIDRHA, 0x0000 }, { REG_ASRIDRLA, 0x0000 },
	{ REG_ASRIDRHB, 0x0000 }, { REG_ASRIDRLB, 0x0000 },
	{ REG_ASRIDRHC, 0x0000 }, { REG_ASRIDRLC, 0x0000 },
	{ REG_ASR76K, 0x0A47 }, { REG_ASR56K, 0x0DF3 },
	{ REG_ASRMCRA, 0x0000 }, { REG_ASRFSTA, 0x0000 },
	{ REG_ASRMCRB, 0x0000 }, { REG_ASRFSTB, 0x0000 },
	{ REG_ASRMCRC, 0x0000 }, { REG_ASRFSTC, 0x0000 },
	{ REG_ASRMCR1A, 0x0000 }, { REG_ASRMCR1B, 0x0000 },
	{ REG_ASRMCR1C, 0x0000 },
};

static const struct regmap_config fsl_asrc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,

	.max_register = REG_ASRMCR1C,
	.reg_defaults = fsl_asrc_reg,
	.num_reg_defaults = ARRAY_SIZE(fsl_asrc_reg),
	.readable_reg = fsl_asrc_readable_reg,
	.volatile_reg = fsl_asrc_volatile_reg,
	.writeable_reg = fsl_asrc_writeable_reg,
	.cache_type = REGCACHE_FLAT,
};

/**
 * fsl_asrc_init - Initialize ASRC registers with a default configuration
 * @asrc: ASRC context
 */
static int fsl_asrc_init(struct fsl_asrc *asrc)
{
	unsigned long ipg_rate;

	/* Halt ASRC internal FP when input FIFO needs data for pair A, B, C */
	regmap_write(asrc->regmap, REG_ASRCTR, ASRCTR_ASRCEN);

	/* Disable interrupt by default */
	regmap_write(asrc->regmap, REG_ASRIER, 0x0);

	/* Apply recommended settings for parameters from Reference Manual */
	regmap_write(asrc->regmap, REG_ASRPM1, 0x7fffff);
	regmap_write(asrc->regmap, REG_ASRPM2, 0x255555);
	regmap_write(asrc->regmap, REG_ASRPM3, 0xff7280);
	regmap_write(asrc->regmap, REG_ASRPM4, 0xff7280);
	regmap_write(asrc->regmap, REG_ASRPM5, 0xff7280);

	/* Base address for task queue FIFO. Set to 0x7C */
	regmap_update_bits(asrc->regmap, REG_ASRTFR1,
			   ASRTFR1_TF_BASE_MASK, ASRTFR1_TF_BASE(0xfc));

	/*
	 * Set the period of the 76KHz and 56KHz sampling clocks based on
	 * the ASRC processing clock.
	 * On iMX6, ipg_clk = 133MHz, REG_ASR76K = 0x06D6, REG_ASR56K = 0x0947
	 */
	ipg_rate = clk_get_rate(asrc->ipg_clk);
	regmap_write(asrc->regmap, REG_ASR76K, ipg_rate / 76000);
	return regmap_write(asrc->regmap, REG_ASR56K, ipg_rate / 56000);
}

/**
 * fsl_asrc_isr- Interrupt handler for ASRC
 * @irq: irq number
 * @dev_id: ASRC context
 */
static irqreturn_t fsl_asrc_isr(int irq, void *dev_id)
{
	struct fsl_asrc *asrc = (struct fsl_asrc *)dev_id;
	struct device *dev = &asrc->pdev->dev;
	enum asrc_pair_index index;
	u32 status;

	regmap_read(asrc->regmap, REG_ASRSTR, &status);

	/* Clean overload error */
	regmap_write(asrc->regmap, REG_ASRSTR, ASRSTR_AOLE);

	/*
	 * We here use dev_dbg() for all exceptions because ASRC itself does
	 * not care if FIFO overflowed or underrun while a warning in the
	 * interrupt would result a ridged conversion.
	 */
	for (index = ASRC_PAIR_A; index < ASRC_PAIR_MAX_NUM; index++) {
		if (!asrc->pair[index])
			continue;

		if (status & ASRSTR_ATQOL) {
			asrc->pair[index]->error |= ASRC_TASK_Q_OVERLOAD;
			dev_dbg(dev, "ASRC Task Queue FIFO overload\n");
		}

		if (status & ASRSTR_AOOL(index)) {
			asrc->pair[index]->error |= ASRC_OUTPUT_TASK_OVERLOAD;
			pair_dbg("Output Task Overload\n");
		}

		if (status & ASRSTR_AIOL(index)) {
			asrc->pair[index]->error |= ASRC_INPUT_TASK_OVERLOAD;
			pair_dbg("Input Task Overload\n");
		}

		if (status & ASRSTR_AODO(index)) {
			asrc->pair[index]->error |= ASRC_OUTPUT_BUFFER_OVERFLOW;
			pair_dbg("Output Data Buffer has overflowed\n");
		}

		if (status & ASRSTR_AIDU(index)) {
			asrc->pair[index]->error |= ASRC_INPUT_BUFFER_UNDERRUN;
			pair_dbg("Input Data Buffer has underflowed\n");
		}
	}

	return IRQ_HANDLED;
}

static int fsl_asrc_get_fifo_addr(u8 dir, enum asrc_pair_index index)
{
	return REG_ASRDx(dir, index);
}

static int fsl_asrc_runtime_resume(struct device *dev);
static int fsl_asrc_runtime_suspend(struct device *dev);

static int fsl_asrc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_asrc_priv *asrc_priv;
	struct fsl_asrc *asrc;
	struct resource *res;
	void __iomem *regs;
	int irq, ret, i;
	u32 map_idx;
	char tmp[16];
	u32 width;

	asrc = devm_kzalloc(&pdev->dev, sizeof(*asrc), GFP_KERNEL);
	if (!asrc)
		return -ENOMEM;

	asrc_priv = devm_kzalloc(&pdev->dev, sizeof(*asrc_priv), GFP_KERNEL);
	if (!asrc_priv)
		return -ENOMEM;

	asrc->pdev = pdev;
	asrc->private = asrc_priv;

	/* Get the addresses and IRQ */
	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	asrc->paddr = res->start;

	asrc->regmap = devm_regmap_init_mmio(&pdev->dev, regs, &fsl_asrc_regmap_config);
	if (IS_ERR(asrc->regmap)) {
		dev_err(&pdev->dev, "failed to init regmap\n");
		return PTR_ERR(asrc->regmap);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, fsl_asrc_isr, 0,
			       dev_name(&pdev->dev), asrc);
	if (ret) {
		dev_err(&pdev->dev, "failed to claim irq %u: %d\n", irq, ret);
		return ret;
	}

	asrc->mem_clk = devm_clk_get(&pdev->dev, "mem");
	if (IS_ERR(asrc->mem_clk)) {
		dev_err(&pdev->dev, "failed to get mem clock\n");
		return PTR_ERR(asrc->mem_clk);
	}

	asrc->ipg_clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(asrc->ipg_clk)) {
		dev_err(&pdev->dev, "failed to get ipg clock\n");
		return PTR_ERR(asrc->ipg_clk);
	}

	asrc->spba_clk = devm_clk_get(&pdev->dev, "spba");
	if (IS_ERR(asrc->spba_clk))
		dev_warn(&pdev->dev, "failed to get spba clock\n");

	for (i = 0; i < ASRC_CLK_MAX_NUM; i++) {
		sprintf(tmp, "asrck_%x", i);
		asrc_priv->asrck_clk[i] = devm_clk_get(&pdev->dev, tmp);
		if (IS_ERR(asrc_priv->asrck_clk[i])) {
			dev_err(&pdev->dev, "failed to get %s clock\n", tmp);
			return PTR_ERR(asrc_priv->asrck_clk[i]);
		}
	}

	asrc_priv->soc = of_device_get_match_data(&pdev->dev);
	asrc->use_edma = asrc_priv->soc->use_edma;
	asrc->get_dma_channel = fsl_asrc_get_dma_channel;
	asrc->request_pair = fsl_asrc_request_pair;
	asrc->release_pair = fsl_asrc_release_pair;
	asrc->get_fifo_addr = fsl_asrc_get_fifo_addr;
	asrc->pair_priv_size = sizeof(struct fsl_asrc_pair_priv);

	if (of_device_is_compatible(np, "fsl,imx35-asrc")) {
		asrc_priv->clk_map[IN] = input_clk_map_imx35;
		asrc_priv->clk_map[OUT] = output_clk_map_imx35;
	} else if (of_device_is_compatible(np, "fsl,imx53-asrc")) {
		asrc_priv->clk_map[IN] = input_clk_map_imx53;
		asrc_priv->clk_map[OUT] = output_clk_map_imx53;
	} else if (of_device_is_compatible(np, "fsl,imx8qm-asrc") ||
		   of_device_is_compatible(np, "fsl,imx8qxp-asrc")) {
		ret = of_property_read_u32(np, "fsl,asrc-clk-map", &map_idx);
		if (ret) {
			dev_err(&pdev->dev, "failed to get clk map index\n");
			return ret;
		}

		if (map_idx > 1) {
			dev_err(&pdev->dev, "unsupported clk map index\n");
			return -EINVAL;
		}
		if (of_device_is_compatible(np, "fsl,imx8qm-asrc")) {
			asrc_priv->clk_map[IN] = clk_map_imx8qm[map_idx];
			asrc_priv->clk_map[OUT] = clk_map_imx8qm[map_idx];
		} else {
			asrc_priv->clk_map[IN] = clk_map_imx8qxp[map_idx];
			asrc_priv->clk_map[OUT] = clk_map_imx8qxp[map_idx];
		}
	}

	asrc->channel_avail = 10;

	ret = of_property_read_u32(np, "fsl,asrc-rate",
				   &asrc->asrc_rate);
	if (ret) {
		dev_err(&pdev->dev, "failed to get output rate\n");
		return ret;
	}

	ret = of_property_read_u32(np, "fsl,asrc-format", &asrc->asrc_format);
	if (ret) {
		ret = of_property_read_u32(np, "fsl,asrc-width", &width);
		if (ret) {
			dev_err(&pdev->dev, "failed to decide output format\n");
			return ret;
		}

		switch (width) {
		case 16:
			asrc->asrc_format = SNDRV_PCM_FORMAT_S16_LE;
			break;
		case 24:
			asrc->asrc_format = SNDRV_PCM_FORMAT_S24_LE;
			break;
		default:
			dev_warn(&pdev->dev,
				 "unsupported width, use default S24_LE\n");
			asrc->asrc_format = SNDRV_PCM_FORMAT_S24_LE;
			break;
		}
	}

	if (!(FSL_ASRC_FORMATS & (1ULL << asrc->asrc_format))) {
		dev_warn(&pdev->dev, "unsupported width, use default S24_LE\n");
		asrc->asrc_format = SNDRV_PCM_FORMAT_S24_LE;
	}

	platform_set_drvdata(pdev, asrc);
	spin_lock_init(&asrc->lock);
	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = fsl_asrc_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		goto err_pm_get_sync;

	ret = fsl_asrc_init(asrc);
	if (ret) {
		dev_err(&pdev->dev, "failed to init asrc %d\n", ret);
		goto err_pm_get_sync;
	}

	ret = pm_runtime_put_sync(&pdev->dev);
	if (ret < 0)
		goto err_pm_get_sync;

	ret = devm_snd_soc_register_component(&pdev->dev, &fsl_asrc_component,
					      &fsl_asrc_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "failed to register ASoC DAI\n");
		goto err_pm_get_sync;
	}

	return 0;

err_pm_get_sync:
	if (!pm_runtime_status_suspended(&pdev->dev))
		fsl_asrc_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int fsl_asrc_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		fsl_asrc_runtime_suspend(&pdev->dev);

	return 0;
}

static int fsl_asrc_runtime_resume(struct device *dev)
{
	struct fsl_asrc *asrc = dev_get_drvdata(dev);
	struct fsl_asrc_priv *asrc_priv = asrc->private;
	int i, ret;
	u32 asrctr;

	ret = clk_prepare_enable(asrc->mem_clk);
	if (ret)
		return ret;
	ret = clk_prepare_enable(asrc->ipg_clk);
	if (ret)
		goto disable_mem_clk;
	if (!IS_ERR(asrc->spba_clk)) {
		ret = clk_prepare_enable(asrc->spba_clk);
		if (ret)
			goto disable_ipg_clk;
	}
	for (i = 0; i < ASRC_CLK_MAX_NUM; i++) {
		ret = clk_prepare_enable(asrc_priv->asrck_clk[i]);
		if (ret)
			goto disable_asrck_clk;
	}

	/* Stop all pairs provisionally */
	regmap_read(asrc->regmap, REG_ASRCTR, &asrctr);
	regmap_update_bits(asrc->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_ALL_MASK, 0);

	/* Restore all registers */
	regcache_cache_only(asrc->regmap, false);
	regcache_mark_dirty(asrc->regmap);
	regcache_sync(asrc->regmap);

	regmap_update_bits(asrc->regmap, REG_ASRCFG,
			   ASRCFG_NDPRi_ALL_MASK | ASRCFG_POSTMODi_ALL_MASK |
			   ASRCFG_PREMODi_ALL_MASK, asrc_priv->regcache_cfg);

	/* Restart enabled pairs */
	regmap_update_bits(asrc->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_ALL_MASK, asrctr);

	return 0;

disable_asrck_clk:
	for (i--; i >= 0; i--)
		clk_disable_unprepare(asrc_priv->asrck_clk[i]);
	if (!IS_ERR(asrc->spba_clk))
		clk_disable_unprepare(asrc->spba_clk);
disable_ipg_clk:
	clk_disable_unprepare(asrc->ipg_clk);
disable_mem_clk:
	clk_disable_unprepare(asrc->mem_clk);
	return ret;
}

static int fsl_asrc_runtime_suspend(struct device *dev)
{
	struct fsl_asrc *asrc = dev_get_drvdata(dev);
	struct fsl_asrc_priv *asrc_priv = asrc->private;
	int i;

	regmap_read(asrc->regmap, REG_ASRCFG,
		    &asrc_priv->regcache_cfg);

	regcache_cache_only(asrc->regmap, true);

	for (i = 0; i < ASRC_CLK_MAX_NUM; i++)
		clk_disable_unprepare(asrc_priv->asrck_clk[i]);
	if (!IS_ERR(asrc->spba_clk))
		clk_disable_unprepare(asrc->spba_clk);
	clk_disable_unprepare(asrc->ipg_clk);
	clk_disable_unprepare(asrc->mem_clk);

	return 0;
}

static const struct dev_pm_ops fsl_asrc_pm = {
	SET_RUNTIME_PM_OPS(fsl_asrc_runtime_suspend, fsl_asrc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct fsl_asrc_soc_data fsl_asrc_imx35_data = {
	.use_edma = false,
	.channel_bits = 3,
};

static const struct fsl_asrc_soc_data fsl_asrc_imx53_data = {
	.use_edma = false,
	.channel_bits = 4,
};

static const struct fsl_asrc_soc_data fsl_asrc_imx8qm_data = {
	.use_edma = true,
	.channel_bits = 4,
};

static const struct fsl_asrc_soc_data fsl_asrc_imx8qxp_data = {
	.use_edma = true,
	.channel_bits = 4,
};

static const struct of_device_id fsl_asrc_ids[] = {
	{ .compatible = "fsl,imx35-asrc", .data = &fsl_asrc_imx35_data },
	{ .compatible = "fsl,imx53-asrc", .data = &fsl_asrc_imx53_data },
	{ .compatible = "fsl,imx8qm-asrc", .data = &fsl_asrc_imx8qm_data },
	{ .compatible = "fsl,imx8qxp-asrc", .data = &fsl_asrc_imx8qxp_data },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_asrc_ids);

static struct platform_driver fsl_asrc_driver = {
	.probe = fsl_asrc_probe,
	.remove = fsl_asrc_remove,
	.driver = {
		.name = "fsl-asrc",
		.of_match_table = fsl_asrc_ids,
		.pm = &fsl_asrc_pm,
	},
};
module_platform_driver(fsl_asrc_driver);

MODULE_DESCRIPTION("Freescale ASRC ASoC driver");
MODULE_AUTHOR("Nicolin Chen <nicoleotsuka@gmail.com>");
MODULE_ALIAS("platform:fsl-asrc");
MODULE_LICENSE("GPL v2");
