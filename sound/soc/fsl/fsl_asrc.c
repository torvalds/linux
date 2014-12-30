/*
 * Freescale ASRC ALSA SoC Digital Audio Interface (DAI) driver
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * Author: Nicolin Chen <nicoleotsuka@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_data/dma-imx.h>
#include <linux/pm_runtime.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "fsl_asrc.h"

#define IDEAL_RATIO_DECIMAL_DEPTH 26

#define pair_err(fmt, ...) \
	dev_err(&asrc_priv->pdev->dev, "Pair %c: " fmt, 'A' + index, ##__VA_ARGS__)

#define pair_dbg(fmt, ...) \
	dev_dbg(&asrc_priv->pdev->dev, "Pair %c: " fmt, 'A' + index, ##__VA_ARGS__)

/* Sample rates are aligned with that defined in pcm.h file */
static const u8 process_option[][8][2] = {
	/* 32kHz 44.1kHz 48kHz   64kHz   88.2kHz 96kHz   176kHz  192kHz */
	{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},},	/* 5512Hz */
	{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},},	/* 8kHz */
	{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},},	/* 11025Hz */
	{{0, 1}, {0, 1}, {0, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},},	/* 16kHz */
	{{0, 1}, {0, 1}, {0, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},},	/* 22050Hz */
	{{0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 0}, {0, 0}, {0, 0},},	/* 32kHz */
	{{0, 2}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 0}, {0, 0},},	/* 44.1kHz */
	{{0, 2}, {0, 2}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 0}, {0, 0},},	/* 48kHz */
	{{1, 2}, {0, 2}, {0, 2}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 0},},	/* 64kHz */
	{{1, 2}, {1, 2}, {1, 2}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1},},	/* 88.2kHz */
	{{1, 2}, {1, 2}, {1, 2}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1},},	/* 96kHz */
	{{2, 2}, {2, 2}, {2, 2}, {2, 1}, {2, 1}, {2, 1}, {2, 1}, {2, 1},},	/* 176kHz */
	{{2, 2}, {2, 2}, {2, 2}, {2, 1}, {2, 1}, {2, 1}, {2, 1}, {2, 1},},	/* 192kHz */
};

/* Corresponding to process_option */
static int supported_input_rate[] = {
	5512, 8000, 11025, 16000, 22050, 32000, 44100, 48000, 64000, 88200,
	96000, 176400, 192000,
};

static int supported_asrc_rate[] = {
	32000, 44100, 48000, 64000, 88200, 96000, 176400, 192000,
};

/**
 * The following tables map the relationship between asrc_inclk/asrc_outclk in
 * fsl_asrc.h and the registers of ASRCSR
 */
static unsigned char input_clk_map_imx35[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
};

static unsigned char output_clk_map_imx35[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
};

/* i.MX53 uses the same map for input and output */
static unsigned char input_clk_map_imx53[] = {
/*	0x0  0x1  0x2  0x3  0x4  0x5  0x6  0x7  0x8  0x9  0xa  0xb  0xc  0xd  0xe  0xf */
	0x0, 0x1, 0x2, 0x7, 0x4, 0x5, 0x6, 0x3, 0x8, 0x9, 0xa, 0xb, 0xc, 0xf, 0xe, 0xd,
};

static unsigned char output_clk_map_imx53[] = {
/*	0x0  0x1  0x2  0x3  0x4  0x5  0x6  0x7  0x8  0x9  0xa  0xb  0xc  0xd  0xe  0xf */
	0x8, 0x9, 0xa, 0x7, 0xc, 0x5, 0x6, 0xb, 0x0, 0x1, 0x2, 0x3, 0x4, 0xf, 0xe, 0xd,
};

static unsigned char *clk_map[2];

/**
 * Request ASRC pair
 *
 * It assigns pair by the order of A->C->B because allocation of pair B,
 * within range [ANCA, ANCA+ANCB-1], depends on the channels of pair A
 * while pair A and pair C are comparatively independent.
 */
static int fsl_asrc_request_pair(int channels, struct fsl_asrc_pair *pair)
{
	enum asrc_pair_index index = ASRC_INVALID_PAIR;
	struct fsl_asrc *asrc_priv = pair->asrc_priv;
	struct device *dev = &asrc_priv->pdev->dev;
	unsigned long lock_flags;
	int i, ret = 0;

	spin_lock_irqsave(&asrc_priv->lock, lock_flags);

	for (i = ASRC_PAIR_A; i < ASRC_PAIR_MAX_NUM; i++) {
		if (asrc_priv->pair[i] != NULL)
			continue;

		index = i;

		if (i != ASRC_PAIR_B)
			break;
	}

	if (index == ASRC_INVALID_PAIR) {
		dev_err(dev, "all pairs are busy now\n");
		ret = -EBUSY;
	} else if (asrc_priv->channel_avail < channels) {
		dev_err(dev, "can't afford required channels: %d\n", channels);
		ret = -EINVAL;
	} else {
		asrc_priv->channel_avail -= channels;
		asrc_priv->pair[index] = pair;
		pair->channels = channels;
		pair->index = index;
	}

	spin_unlock_irqrestore(&asrc_priv->lock, lock_flags);

	return ret;
}

/**
 * Release ASRC pair
 *
 * It clears the resource from asrc_priv and releases the occupied channels.
 */
static void fsl_asrc_release_pair(struct fsl_asrc_pair *pair)
{
	struct fsl_asrc *asrc_priv = pair->asrc_priv;
	enum asrc_pair_index index = pair->index;
	unsigned long lock_flags;

	/* Make sure the pair is disabled */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_MASK(index), 0);

	spin_lock_irqsave(&asrc_priv->lock, lock_flags);

	asrc_priv->channel_avail += pair->channels;
	asrc_priv->pair[index] = NULL;
	pair->error = 0;

	spin_unlock_irqrestore(&asrc_priv->lock, lock_flags);
}

/**
 * Configure input and output thresholds
 */
static void fsl_asrc_set_watermarks(struct fsl_asrc_pair *pair, u32 in, u32 out)
{
	struct fsl_asrc *asrc_priv = pair->asrc_priv;
	enum asrc_pair_index index = pair->index;

	regmap_update_bits(asrc_priv->regmap, REG_ASRMCR(index),
			   ASRMCRi_EXTTHRSHi_MASK |
			   ASRMCRi_INFIFO_THRESHOLD_MASK |
			   ASRMCRi_OUTFIFO_THRESHOLD_MASK,
			   ASRMCRi_EXTTHRSHi |
			   ASRMCRi_INFIFO_THRESHOLD(in) |
			   ASRMCRi_OUTFIFO_THRESHOLD(out));
}

/**
 * Calculate the total divisor between asrck clock rate and sample rate
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
 * Calculate and set the ratio for Ideal Ratio mode only
 *
 * The ratio is a 32-bit fixed point value with 26 fractional bits.
 */
static int fsl_asrc_set_ideal_ratio(struct fsl_asrc_pair *pair,
				    int inrate, int outrate)
{
	struct fsl_asrc *asrc_priv = pair->asrc_priv;
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

	regmap_write(asrc_priv->regmap, REG_ASRIDRL(index), ratio);
	regmap_write(asrc_priv->regmap, REG_ASRIDRH(index), ratio >> 24);

	return 0;
}

/**
 * Configure the assigned ASRC pair
 *
 * It configures those ASRC registers according to a configuration instance
 * of struct asrc_config which includes in/output sample rate, width, channel
 * and clock settings.
 */
static int fsl_asrc_config_pair(struct fsl_asrc_pair *pair)
{
	struct asrc_config *config = pair->config;
	struct fsl_asrc *asrc_priv = pair->asrc_priv;
	enum asrc_pair_index index = pair->index;
	u32 inrate, outrate, indiv, outdiv;
	u32 clk_index[2], div[2];
	int in, out, channels;
	struct clk *clk;
	bool ideal;

	if (!config) {
		pair_err("invalid pair config\n");
		return -EINVAL;
	}

	/* Validate channels */
	if (config->channel_num < 1 || config->channel_num > 10) {
		pair_err("does not support %d channels\n", config->channel_num);
		return -EINVAL;
	}

	/* Validate output width */
	if (config->output_word_width == ASRC_WIDTH_8_BIT) {
		pair_err("does not support 8bit width output\n");
		return -EINVAL;
	}

	inrate = config->input_sample_rate;
	outrate = config->output_sample_rate;
	ideal = config->inclk == INCLK_NONE;

	/* Validate input and output sample rates */
	for (in = 0; in < ARRAY_SIZE(supported_input_rate); in++)
		if (inrate == supported_input_rate[in])
			break;

	if (in == ARRAY_SIZE(supported_input_rate)) {
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

	/* Validate input and output clock sources */
	clk_index[IN] = clk_map[IN][config->inclk];
	clk_index[OUT] = clk_map[OUT][config->outclk];

	/* We only have output clock for ideal ratio mode */
	clk = asrc_priv->asrck_clk[clk_index[ideal ? OUT : IN]];

	div[IN] = clk_get_rate(clk) / inrate;
	if (div[IN] == 0) {
		pair_err("failed to support input sample rate %dHz by asrck_%x\n",
				inrate, clk_index[ideal ? OUT : IN]);
		return -EINVAL;
	}

	clk = asrc_priv->asrck_clk[clk_index[OUT]];

	/* Use fixed output rate for Ideal Ratio mode (INCLK_NONE) */
	if (ideal)
		div[OUT] = clk_get_rate(clk) / IDEAL_RATIO_RATE;
	else
		div[OUT] = clk_get_rate(clk) / outrate;

	if (div[OUT] == 0) {
		pair_err("failed to support output sample rate %dHz by asrck_%x\n",
				outrate, clk_index[OUT]);
		return -EINVAL;
	}

	/* Set the channel number */
	channels = config->channel_num;

	if (asrc_priv->channel_bits < 4)
		channels /= 2;

	/* Update channels for current pair */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCNCR,
			   ASRCNCR_ANCi_MASK(index, asrc_priv->channel_bits),
			   ASRCNCR_ANCi(index, channels, asrc_priv->channel_bits));

	/* Default setting: Automatic selection for processing mode */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCTR,
			   ASRCTR_ATSi_MASK(index), ASRCTR_ATS(index));
	regmap_update_bits(asrc_priv->regmap, REG_ASRCTR,
			   ASRCTR_USRi_MASK(index), 0);

	/* Set the input and output clock sources */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCSR,
			   ASRCSR_AICSi_MASK(index) | ASRCSR_AOCSi_MASK(index),
			   ASRCSR_AICS(index, clk_index[IN]) |
			   ASRCSR_AOCS(index, clk_index[OUT]));

	/* Calculate the input clock divisors */
	indiv = fsl_asrc_cal_asrck_divisor(pair, div[IN]);
	outdiv = fsl_asrc_cal_asrck_divisor(pair, div[OUT]);

	/* Suppose indiv and outdiv includes prescaler, so add its MASK too */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCDR(index),
			   ASRCDRi_AOCPi_MASK(index) | ASRCDRi_AICPi_MASK(index) |
			   ASRCDRi_AOCDi_MASK(index) | ASRCDRi_AICDi_MASK(index),
			   ASRCDRi_AOCP(index, outdiv) | ASRCDRi_AICP(index, indiv));

	/* Implement word_width configurations */
	regmap_update_bits(asrc_priv->regmap, REG_ASRMCR1(index),
			   ASRMCR1i_OW16_MASK | ASRMCR1i_IWD_MASK,
			   ASRMCR1i_OW16(config->output_word_width) |
			   ASRMCR1i_IWD(config->input_word_width));

	/* Enable BUFFER STALL */
	regmap_update_bits(asrc_priv->regmap, REG_ASRMCR(index),
			   ASRMCRi_BUFSTALLi_MASK, ASRMCRi_BUFSTALLi);

	/* Set default thresholds for input and output FIFO */
	fsl_asrc_set_watermarks(pair, ASRC_INPUTFIFO_THRESHOLD,
				ASRC_INPUTFIFO_THRESHOLD);

	/* Configure the followings only for Ideal Ratio mode */
	if (!ideal)
		return 0;

	/* Clear ASTSx bit to use Ideal Ratio mode */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCTR,
			   ASRCTR_ATSi_MASK(index), 0);

	/* Enable Ideal Ratio mode */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCTR,
			   ASRCTR_IDRi_MASK(index) | ASRCTR_USRi_MASK(index),
			   ASRCTR_IDR(index) | ASRCTR_USR(index));

	/* Apply configurations for pre- and post-processing */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCFG,
			   ASRCFG_PREMODi_MASK(index) |	ASRCFG_POSTMODi_MASK(index),
			   ASRCFG_PREMOD(index, process_option[in][out][0]) |
			   ASRCFG_POSTMOD(index, process_option[in][out][1]));

	return fsl_asrc_set_ideal_ratio(pair, inrate, outrate);
}

/**
 * Start the assigned ASRC pair
 *
 * It enables the assigned pair and makes it stopped at the stall level.
 */
static void fsl_asrc_start_pair(struct fsl_asrc_pair *pair)
{
	struct fsl_asrc *asrc_priv = pair->asrc_priv;
	enum asrc_pair_index index = pair->index;
	int reg, retry = 10, i;

	/* Enable the current pair */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_MASK(index), ASRCTR_ASRCE(index));

	/* Wait for status of initialization */
	do {
		udelay(5);
		regmap_read(asrc_priv->regmap, REG_ASRCFG, &reg);
		reg &= ASRCFG_INIRQi_MASK(index);
	} while (!reg && --retry);

	/* Make the input fifo to ASRC STALL level */
	regmap_read(asrc_priv->regmap, REG_ASRCNCR, &reg);
	for (i = 0; i < pair->channels * 4; i++)
		regmap_write(asrc_priv->regmap, REG_ASRDI(index), 0);

	/* Enable overload interrupt */
	regmap_write(asrc_priv->regmap, REG_ASRIER, ASRIER_AOLIE);
}

/**
 * Stop the assigned ASRC pair
 */
static void fsl_asrc_stop_pair(struct fsl_asrc_pair *pair)
{
	struct fsl_asrc *asrc_priv = pair->asrc_priv;
	enum asrc_pair_index index = pair->index;

	/* Stop the current pair */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_MASK(index), 0);
}

/**
 * Get DMA channel according to the pair and direction.
 */
struct dma_chan *fsl_asrc_get_dma_channel(struct fsl_asrc_pair *pair, bool dir)
{
	struct fsl_asrc *asrc_priv = pair->asrc_priv;
	enum asrc_pair_index index = pair->index;
	char name[4];

	sprintf(name, "%cx%c", dir == IN ? 'r' : 't', index + 'a');

	return dma_request_slave_channel(&asrc_priv->pdev->dev, name);
}
EXPORT_SYMBOL_GPL(fsl_asrc_get_dma_channel);

static int fsl_asrc_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct fsl_asrc *asrc_priv = snd_soc_dai_get_drvdata(dai);
	int width = snd_pcm_format_width(params_format(params));
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *pair = runtime->private_data;
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	struct asrc_config config;
	int word_width, ret;

	ret = fsl_asrc_request_pair(channels, pair);
	if (ret) {
		dev_err(dai->dev, "fail to request asrc pair\n");
		return ret;
	}

	pair->config = &config;

	if (width == 16)
		width = ASRC_WIDTH_16_BIT;
	else
		width = ASRC_WIDTH_24_BIT;

	if (asrc_priv->asrc_width == 16)
		word_width = ASRC_WIDTH_16_BIT;
	else
		word_width = ASRC_WIDTH_24_BIT;

	config.pair = pair->index;
	config.channel_num = channels;
	config.inclk = INCLK_NONE;
	config.outclk = OUTCLK_ASRCK1_CLK;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		config.input_word_width   = width;
		config.output_word_width  = word_width;
		config.input_sample_rate  = rate;
		config.output_sample_rate = asrc_priv->asrc_rate;
	} else {
		config.input_word_width   = word_width;
		config.output_word_width  = width;
		config.input_sample_rate  = asrc_priv->asrc_rate;
		config.output_sample_rate = rate;
	}

	ret = fsl_asrc_config_pair(pair);
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

static struct snd_soc_dai_ops fsl_asrc_dai_ops = {
	.hw_params    = fsl_asrc_dai_hw_params,
	.hw_free      = fsl_asrc_dai_hw_free,
	.trigger      = fsl_asrc_dai_trigger,
};

static int fsl_asrc_dai_probe(struct snd_soc_dai *dai)
{
	struct fsl_asrc *asrc_priv = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &asrc_priv->dma_params_tx,
				  &asrc_priv->dma_params_rx);

	return 0;
}

#define FSL_ASRC_RATES		 SNDRV_PCM_RATE_8000_192000
#define FSL_ASRC_FORMATS	(SNDRV_PCM_FMTBIT_S24_LE | \
				 SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_S20_3LE)

static struct snd_soc_dai_driver fsl_asrc_dai = {
	.probe = fsl_asrc_dai_probe,
	.playback = {
		.stream_name = "ASRC-Playback",
		.channels_min = 1,
		.channels_max = 10,
		.rates = FSL_ASRC_RATES,
		.formats = FSL_ASRC_FORMATS,
	},
	.capture = {
		.stream_name = "ASRC-Capture",
		.channels_min = 1,
		.channels_max = 10,
		.rates = FSL_ASRC_RATES,
		.formats = FSL_ASRC_FORMATS,
	},
	.ops = &fsl_asrc_dai_ops,
};

static const struct snd_soc_component_driver fsl_asrc_component = {
	.name = "fsl-asrc-dai",
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
	.cache_type = REGCACHE_RBTREE,
};

/**
 * Initialize ASRC registers with a default configurations
 */
static int fsl_asrc_init(struct fsl_asrc *asrc_priv)
{
	/* Halt ASRC internal FP when input FIFO needs data for pair A, B, C */
	regmap_write(asrc_priv->regmap, REG_ASRCTR, ASRCTR_ASRCEN);

	/* Disable interrupt by default */
	regmap_write(asrc_priv->regmap, REG_ASRIER, 0x0);

	/* Apply recommended settings for parameters from Reference Manual */
	regmap_write(asrc_priv->regmap, REG_ASRPM1, 0x7fffff);
	regmap_write(asrc_priv->regmap, REG_ASRPM2, 0x255555);
	regmap_write(asrc_priv->regmap, REG_ASRPM3, 0xff7280);
	regmap_write(asrc_priv->regmap, REG_ASRPM4, 0xff7280);
	regmap_write(asrc_priv->regmap, REG_ASRPM5, 0xff7280);

	/* Base address for task queue FIFO. Set to 0x7C */
	regmap_update_bits(asrc_priv->regmap, REG_ASRTFR1,
			   ASRTFR1_TF_BASE_MASK, ASRTFR1_TF_BASE(0xfc));

	/* Set the processing clock for 76KHz to 133M */
	regmap_write(asrc_priv->regmap, REG_ASR76K, 0x06D6);

	/* Set the processing clock for 56KHz to 133M */
	return regmap_write(asrc_priv->regmap, REG_ASR56K, 0x0947);
}

/**
 * Interrupt handler for ASRC
 */
static irqreturn_t fsl_asrc_isr(int irq, void *dev_id)
{
	struct fsl_asrc *asrc_priv = (struct fsl_asrc *)dev_id;
	struct device *dev = &asrc_priv->pdev->dev;
	enum asrc_pair_index index;
	u32 status;

	regmap_read(asrc_priv->regmap, REG_ASRSTR, &status);

	/* Clean overload error */
	regmap_write(asrc_priv->regmap, REG_ASRSTR, ASRSTR_AOLE);

	/*
	 * We here use dev_dbg() for all exceptions because ASRC itself does
	 * not care if FIFO overflowed or underrun while a warning in the
	 * interrupt would result a ridged conversion.
	 */
	for (index = ASRC_PAIR_A; index < ASRC_PAIR_MAX_NUM; index++) {
		if (!asrc_priv->pair[index])
			continue;

		if (status & ASRSTR_ATQOL) {
			asrc_priv->pair[index]->error |= ASRC_TASK_Q_OVERLOAD;
			dev_dbg(dev, "ASRC Task Queue FIFO overload\n");
		}

		if (status & ASRSTR_AOOL(index)) {
			asrc_priv->pair[index]->error |= ASRC_OUTPUT_TASK_OVERLOAD;
			pair_dbg("Output Task Overload\n");
		}

		if (status & ASRSTR_AIOL(index)) {
			asrc_priv->pair[index]->error |= ASRC_INPUT_TASK_OVERLOAD;
			pair_dbg("Input Task Overload\n");
		}

		if (status & ASRSTR_AODO(index)) {
			asrc_priv->pair[index]->error |= ASRC_OUTPUT_BUFFER_OVERFLOW;
			pair_dbg("Output Data Buffer has overflowed\n");
		}

		if (status & ASRSTR_AIDU(index)) {
			asrc_priv->pair[index]->error |= ASRC_INPUT_BUFFER_UNDERRUN;
			pair_dbg("Input Data Buffer has underflowed\n");
		}
	}

	return IRQ_HANDLED;
}

static int fsl_asrc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_asrc *asrc_priv;
	struct resource *res;
	void __iomem *regs;
	int irq, ret, i;
	char tmp[16];

	asrc_priv = devm_kzalloc(&pdev->dev, sizeof(*asrc_priv), GFP_KERNEL);
	if (!asrc_priv)
		return -ENOMEM;

	asrc_priv->pdev = pdev;

	/* Get the addresses and IRQ */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	asrc_priv->paddr = res->start;

	asrc_priv->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "mem", regs,
						      &fsl_asrc_regmap_config);
	if (IS_ERR(asrc_priv->regmap)) {
		dev_err(&pdev->dev, "failed to init regmap\n");
		return PTR_ERR(asrc_priv->regmap);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq for node %s\n", np->full_name);
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, fsl_asrc_isr, 0,
			       dev_name(&pdev->dev), asrc_priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to claim irq %u: %d\n", irq, ret);
		return ret;
	}

	asrc_priv->mem_clk = devm_clk_get(&pdev->dev, "mem");
	if (IS_ERR(asrc_priv->mem_clk)) {
		dev_err(&pdev->dev, "failed to get mem clock\n");
		return PTR_ERR(asrc_priv->mem_clk);
	}

	asrc_priv->ipg_clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(asrc_priv->ipg_clk)) {
		dev_err(&pdev->dev, "failed to get ipg clock\n");
		return PTR_ERR(asrc_priv->ipg_clk);
	}

	for (i = 0; i < ASRC_CLK_MAX_NUM; i++) {
		sprintf(tmp, "asrck_%x", i);
		asrc_priv->asrck_clk[i] = devm_clk_get(&pdev->dev, tmp);
		if (IS_ERR(asrc_priv->asrck_clk[i])) {
			dev_err(&pdev->dev, "failed to get %s clock\n", tmp);
			return PTR_ERR(asrc_priv->asrck_clk[i]);
		}
	}

	if (of_device_is_compatible(pdev->dev.of_node, "fsl,imx35-asrc")) {
		asrc_priv->channel_bits = 3;
		clk_map[IN] = input_clk_map_imx35;
		clk_map[OUT] = output_clk_map_imx35;
	} else {
		asrc_priv->channel_bits = 4;
		clk_map[IN] = input_clk_map_imx53;
		clk_map[OUT] = output_clk_map_imx53;
	}

	ret = fsl_asrc_init(asrc_priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to init asrc %d\n", ret);
		return -EINVAL;
	}

	asrc_priv->channel_avail = 10;

	ret = of_property_read_u32(np, "fsl,asrc-rate",
				   &asrc_priv->asrc_rate);
	if (ret) {
		dev_err(&pdev->dev, "failed to get output rate\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "fsl,asrc-width",
				   &asrc_priv->asrc_width);
	if (ret) {
		dev_err(&pdev->dev, "failed to get output width\n");
		return -EINVAL;
	}

	if (asrc_priv->asrc_width != 16 && asrc_priv->asrc_width != 24) {
		dev_warn(&pdev->dev, "unsupported width, switching to 24bit\n");
		asrc_priv->asrc_width = 24;
	}

	platform_set_drvdata(pdev, asrc_priv);
	pm_runtime_enable(&pdev->dev);
	spin_lock_init(&asrc_priv->lock);

	ret = devm_snd_soc_register_component(&pdev->dev, &fsl_asrc_component,
					      &fsl_asrc_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "failed to register ASoC DAI\n");
		return ret;
	}

	ret = devm_snd_soc_register_platform(&pdev->dev, &fsl_asrc_platform);
	if (ret) {
		dev_err(&pdev->dev, "failed to register ASoC platform\n");
		return ret;
	}

	dev_info(&pdev->dev, "driver registered\n");

	return 0;
}

#ifdef CONFIG_PM
static int fsl_asrc_runtime_resume(struct device *dev)
{
	struct fsl_asrc *asrc_priv = dev_get_drvdata(dev);
	int i;

	clk_prepare_enable(asrc_priv->mem_clk);
	clk_prepare_enable(asrc_priv->ipg_clk);
	for (i = 0; i < ASRC_CLK_MAX_NUM; i++)
		clk_prepare_enable(asrc_priv->asrck_clk[i]);

	return 0;
}

static int fsl_asrc_runtime_suspend(struct device *dev)
{
	struct fsl_asrc *asrc_priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < ASRC_CLK_MAX_NUM; i++)
		clk_disable_unprepare(asrc_priv->asrck_clk[i]);
	clk_disable_unprepare(asrc_priv->ipg_clk);
	clk_disable_unprepare(asrc_priv->mem_clk);

	return 0;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
static int fsl_asrc_suspend(struct device *dev)
{
	struct fsl_asrc *asrc_priv = dev_get_drvdata(dev);

	regcache_cache_only(asrc_priv->regmap, true);
	regcache_mark_dirty(asrc_priv->regmap);

	return 0;
}

static int fsl_asrc_resume(struct device *dev)
{
	struct fsl_asrc *asrc_priv = dev_get_drvdata(dev);
	u32 asrctr;

	/* Stop all pairs provisionally */
	regmap_read(asrc_priv->regmap, REG_ASRCTR, &asrctr);
	regmap_update_bits(asrc_priv->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_ALL_MASK, 0);

	/* Restore all registers */
	regcache_cache_only(asrc_priv->regmap, false);
	regcache_sync(asrc_priv->regmap);

	/* Restart enabled pairs */
	regmap_update_bits(asrc_priv->regmap, REG_ASRCTR,
			   ASRCTR_ASRCEi_ALL_MASK, asrctr);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops fsl_asrc_pm = {
	SET_RUNTIME_PM_OPS(fsl_asrc_runtime_suspend, fsl_asrc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(fsl_asrc_suspend, fsl_asrc_resume)
};

static const struct of_device_id fsl_asrc_ids[] = {
	{ .compatible = "fsl,imx35-asrc", },
	{ .compatible = "fsl,imx53-asrc", },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_asrc_ids);

static struct platform_driver fsl_asrc_driver = {
	.probe = fsl_asrc_probe,
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
