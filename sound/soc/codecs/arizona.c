/*
 * arizona.c - Wolfson Arizona class device shared support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/gcd.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/registers.h>

#include "arizona.h"

#define ARIZONA_AIF_BCLK_CTRL                   0x00
#define ARIZONA_AIF_TX_PIN_CTRL                 0x01
#define ARIZONA_AIF_RX_PIN_CTRL                 0x02
#define ARIZONA_AIF_RATE_CTRL                   0x03
#define ARIZONA_AIF_FORMAT                      0x04
#define ARIZONA_AIF_TX_BCLK_RATE                0x05
#define ARIZONA_AIF_RX_BCLK_RATE                0x06
#define ARIZONA_AIF_FRAME_CTRL_1                0x07
#define ARIZONA_AIF_FRAME_CTRL_2                0x08
#define ARIZONA_AIF_FRAME_CTRL_3                0x09
#define ARIZONA_AIF_FRAME_CTRL_4                0x0A
#define ARIZONA_AIF_FRAME_CTRL_5                0x0B
#define ARIZONA_AIF_FRAME_CTRL_6                0x0C
#define ARIZONA_AIF_FRAME_CTRL_7                0x0D
#define ARIZONA_AIF_FRAME_CTRL_8                0x0E
#define ARIZONA_AIF_FRAME_CTRL_9                0x0F
#define ARIZONA_AIF_FRAME_CTRL_10               0x10
#define ARIZONA_AIF_FRAME_CTRL_11               0x11
#define ARIZONA_AIF_FRAME_CTRL_12               0x12
#define ARIZONA_AIF_FRAME_CTRL_13               0x13
#define ARIZONA_AIF_FRAME_CTRL_14               0x14
#define ARIZONA_AIF_FRAME_CTRL_15               0x15
#define ARIZONA_AIF_FRAME_CTRL_16               0x16
#define ARIZONA_AIF_FRAME_CTRL_17               0x17
#define ARIZONA_AIF_FRAME_CTRL_18               0x18
#define ARIZONA_AIF_TX_ENABLES                  0x19
#define ARIZONA_AIF_RX_ENABLES                  0x1A
#define ARIZONA_AIF_FORCE_WRITE                 0x1B

#define arizona_fll_err(_fll, fmt, ...) \
	dev_err(_fll->arizona->dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)
#define arizona_fll_warn(_fll, fmt, ...) \
	dev_warn(_fll->arizona->dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)
#define arizona_fll_dbg(_fll, fmt, ...) \
	dev_err(_fll->arizona->dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)

#define arizona_aif_err(_dai, fmt, ...) \
	dev_err(_dai->dev, "AIF%d: " fmt, _dai->id, ##__VA_ARGS__)
#define arizona_aif_warn(_dai, fmt, ...) \
	dev_warn(_dai->dev, "AIF%d: " fmt, _dai->id, ##__VA_ARGS__)
#define arizona_aif_dbg(_dai, fmt, ...) \
	dev_err(_dai->dev, "AIF%d: " fmt, _dai->id, ##__VA_ARGS__)

const char *arizona_mixer_texts[ARIZONA_NUM_MIXER_INPUTS] = {
	"None",
	"Tone Generator 1",
	"Tone Generator 2",
	"Haptics",
	"AEC",
	"Mic Mute Mixer",
	"Noise Generator",
	"IN1L",
	"IN1R",
	"IN2L",
	"IN2R",
	"IN3L",
	"IN3R",
	"AIF1RX1",
	"AIF1RX2",
	"AIF1RX3",
	"AIF1RX4",
	"AIF1RX5",
	"AIF1RX6",
	"AIF1RX7",
	"AIF1RX8",
	"AIF2RX1",
	"AIF2RX2",
	"AIF3RX1",
	"AIF3RX2",
	"SLIMRX1",
	"SLIMRX2",
	"SLIMRX3",
	"SLIMRX4",
	"SLIMRX5",
	"SLIMRX6",
	"SLIMRX7",
	"SLIMRX8",
	"EQ1",
	"EQ2",
	"EQ3",
	"EQ4",
	"DRC1L",
	"DRC1R",
	"DRC2L",
	"DRC2R",
	"LHPF1",
	"LHPF2",
	"LHPF3",
	"LHPF4",
	"DSP1.1",
	"DSP1.2",
	"DSP1.3",
	"DSP1.4",
	"DSP1.5",
	"DSP1.6",
	"ASRC1L",
	"ASRC1R",
	"ASRC2L",
	"ASRC2R",
};
EXPORT_SYMBOL_GPL(arizona_mixer_texts);

int arizona_mixer_values[ARIZONA_NUM_MIXER_INPUTS] = {
	0x00,  /* None */
	0x04,  /* Tone */
	0x05,
	0x06,  /* Haptics */
	0x08,  /* AEC */
	0x0c,  /* Noise mixer */
	0x0d,  /* Comfort noise */
	0x10,  /* IN1L */
	0x11,
	0x12,
	0x13,
	0x14,
	0x15,
	0x20,  /* AIF1RX1 */
	0x21,
	0x22,
	0x23,
	0x24,
	0x25,
	0x26,
	0x27,
	0x28,  /* AIF2RX1 */
	0x29,
	0x30,  /* AIF3RX1 */
	0x31,
	0x38,  /* SLIMRX1 */
	0x39,
	0x3a,
	0x3b,
	0x3c,
	0x3d,
	0x3e,
	0x3f,
	0x50,  /* EQ1 */
	0x51,
	0x52,
	0x53,
	0x58,  /* DRC1L */
	0x59,
	0x5a,
	0x5b,
	0x60,  /* LHPF1 */
	0x61,
	0x62,
	0x63,
	0x68,  /* DSP1.1 */
	0x69,
	0x6a,
	0x6b,
	0x6c,
	0x6d,
	0x90,  /* ASRC1L */
	0x91,
	0x92,
	0x93,
};
EXPORT_SYMBOL_GPL(arizona_mixer_values);

const DECLARE_TLV_DB_SCALE(arizona_mixer_tlv, -3200, 100, 0);
EXPORT_SYMBOL_GPL(arizona_mixer_tlv);

static const char *arizona_lhpf_mode_text[] = {
	"Low-pass", "High-pass"
};

const struct soc_enum arizona_lhpf1_mode =
	SOC_ENUM_SINGLE(ARIZONA_HPLPF1_1, ARIZONA_LHPF1_MODE_SHIFT, 2,
			arizona_lhpf_mode_text);
EXPORT_SYMBOL_GPL(arizona_lhpf1_mode);

const struct soc_enum arizona_lhpf2_mode =
	SOC_ENUM_SINGLE(ARIZONA_HPLPF2_1, ARIZONA_LHPF2_MODE_SHIFT, 2,
			arizona_lhpf_mode_text);
EXPORT_SYMBOL_GPL(arizona_lhpf2_mode);

const struct soc_enum arizona_lhpf3_mode =
	SOC_ENUM_SINGLE(ARIZONA_HPLPF3_1, ARIZONA_LHPF3_MODE_SHIFT, 2,
			arizona_lhpf_mode_text);
EXPORT_SYMBOL_GPL(arizona_lhpf3_mode);

const struct soc_enum arizona_lhpf4_mode =
	SOC_ENUM_SINGLE(ARIZONA_HPLPF4_1, ARIZONA_LHPF4_MODE_SHIFT, 2,
			arizona_lhpf_mode_text);
EXPORT_SYMBOL_GPL(arizona_lhpf4_mode);

int arizona_in_ev(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
		  int event)
{
	return 0;
}
EXPORT_SYMBOL_GPL(arizona_in_ev);

int arizona_out_ev(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol,
		   int event)
{
	return 0;
}
EXPORT_SYMBOL_GPL(arizona_out_ev);

int arizona_set_sysclk(struct snd_soc_codec *codec, int clk_id,
		       int source, unsigned int freq, int dir)
{
	struct arizona_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct arizona *arizona = priv->arizona;
	char *name;
	unsigned int reg;
	unsigned int mask = ARIZONA_SYSCLK_FREQ_MASK | ARIZONA_SYSCLK_SRC_MASK;
	unsigned int val = source << ARIZONA_SYSCLK_SRC_SHIFT;
	unsigned int *clk;

	switch (clk_id) {
	case ARIZONA_CLK_SYSCLK:
		name = "SYSCLK";
		reg = ARIZONA_SYSTEM_CLOCK_1;
		clk = &priv->sysclk;
		mask |= ARIZONA_SYSCLK_FRAC;
		break;
	case ARIZONA_CLK_ASYNCCLK:
		name = "ASYNCCLK";
		reg = ARIZONA_ASYNC_CLOCK_1;
		clk = &priv->asyncclk;
		break;
	default:
		return -EINVAL;
	}

	switch (freq) {
	case  5644800:
	case  6144000:
		break;
	case 11289600:
	case 12288000:
		val |= 1 << ARIZONA_SYSCLK_FREQ_SHIFT;
		break;
	case 22579200:
	case 24576000:
		val |= 2 << ARIZONA_SYSCLK_FREQ_SHIFT;
		break;
	case 45158400:
	case 49152000:
		val |= 3 << ARIZONA_SYSCLK_FREQ_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	*clk = freq;

	if (freq % 6144000)
		val |= ARIZONA_SYSCLK_FRAC;

	dev_dbg(arizona->dev, "%s set to %uHz", name, freq);

	return regmap_update_bits(arizona->regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(arizona_set_sysclk);

static int arizona_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int lrclk, bclk, mode, base;

	base = dai->driver->base;

	lrclk = 0;
	bclk = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		mode = 0;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		mode = 1;
		break;
	case SND_SOC_DAIFMT_I2S:
		mode = 2;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		mode = 3;
		break;
	default:
		arizona_aif_err(dai, "Unsupported DAI format %d\n",
				fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		lrclk |= ARIZONA_AIF1TX_LRCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		bclk |= ARIZONA_AIF1_BCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		bclk |= ARIZONA_AIF1_BCLK_MSTR;
		lrclk |= ARIZONA_AIF1TX_LRCLK_MSTR;
		break;
	default:
		arizona_aif_err(dai, "Unsupported master mode %d\n",
				fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bclk |= ARIZONA_AIF1_BCLK_INV;
		lrclk |= ARIZONA_AIF1TX_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bclk |= ARIZONA_AIF1_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrclk |= ARIZONA_AIF1TX_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, base + ARIZONA_AIF_BCLK_CTRL,
			    ARIZONA_AIF1_BCLK_INV | ARIZONA_AIF1_BCLK_MSTR,
			    bclk);
	snd_soc_update_bits(codec, base + ARIZONA_AIF_TX_PIN_CTRL,
			    ARIZONA_AIF1TX_LRCLK_INV |
			    ARIZONA_AIF1TX_LRCLK_MSTR, lrclk);
	snd_soc_update_bits(codec, base + ARIZONA_AIF_RX_PIN_CTRL,
			    ARIZONA_AIF1RX_LRCLK_INV |
			    ARIZONA_AIF1RX_LRCLK_MSTR, lrclk);
	snd_soc_update_bits(codec, base + ARIZONA_AIF_FORMAT,
			    ARIZONA_AIF1_FMT_MASK, mode);

	return 0;
}

static const int arizona_48k_bclk_rates[] = {
	-1,
	48000,
	64000,
	96000,
	128000,
	192000,
	256000,
	384000,
	512000,
	768000,
	1024000,
	1536000,
	2048000,
	3072000,
	4096000,
	6144000,
	8192000,
	12288000,
	24576000,
};

static const unsigned int arizona_48k_rates[] = {
	12000,
	24000,
	48000,
	96000,
	192000,
	384000,
	768000,
	4000,
	8000,
	16000,
	32000,
	64000,
	128000,
	256000,
	512000,
};

static const struct snd_pcm_hw_constraint_list arizona_48k_constraint = {
	.count	= ARRAY_SIZE(arizona_48k_rates),
	.list	= arizona_48k_rates,
};

static const int arizona_44k1_bclk_rates[] = {
	-1,
	44100,
	58800,
	88200,
	117600,
	177640,
	235200,
	352800,
	470400,
	705600,
	940800,
	1411200,
	1881600,
	2882400,
	3763200,
	5644800,
	7526400,
	11289600,
	22579200,
};

static const unsigned int arizona_44k1_rates[] = {
	11025,
	22050,
	44100,
	88200,
	176400,
	352800,
	705600,
};

static const struct snd_pcm_hw_constraint_list arizona_44k1_constraint = {
	.count	= ARRAY_SIZE(arizona_44k1_rates),
	.list	= arizona_44k1_rates,
};

static int arizona_sr_vals[] = {
	0,
	12000,
	24000,
	48000,
	96000,
	192000,
	384000,
	768000,
	0,
	11025,
	22050,
	44100,
	88200,
	176400,
	352800,
	705600,
	4000,
	8000,
	16000,
	32000,
	64000,
	128000,
	256000,
	512000,
};

static int arizona_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct arizona_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct arizona_dai_priv *dai_priv = &priv->dai[dai->id - 1];
	const struct snd_pcm_hw_constraint_list *constraint;
	unsigned int base_rate;

	switch (dai_priv->clk) {
	case ARIZONA_CLK_SYSCLK:
		base_rate = priv->sysclk;
		break;
	case ARIZONA_CLK_ASYNCCLK:
		base_rate = priv->asyncclk;
		break;
	default:
		return 0;
	}

	if (base_rate % 8000)
		constraint = &arizona_44k1_constraint;
	else
		constraint = &arizona_48k_constraint;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  constraint);
}

static int arizona_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct arizona_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct arizona_dai_priv *dai_priv = &priv->dai[dai->id - 1];
	int base = dai->driver->base;
	const int *rates;
	int i;
	int bclk, lrclk, wl, frame, sr_val;

	if (params_rate(params) % 8000)
		rates = &arizona_44k1_bclk_rates[0];
	else
		rates = &arizona_48k_bclk_rates[0];

	for (i = 0; i < ARRAY_SIZE(arizona_44k1_bclk_rates); i++) {
		if (rates[i] >= snd_soc_params_to_bclk(params) &&
		    rates[i] % params_rate(params) == 0) {
			bclk = i;
			break;
		}
	}
	if (i == ARRAY_SIZE(arizona_44k1_bclk_rates)) {
		arizona_aif_err(dai, "Unsupported sample rate %dHz\n",
				params_rate(params));
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(arizona_sr_vals); i++)
		if (arizona_sr_vals[i] == params_rate(params))
			break;
	if (i == ARRAY_SIZE(arizona_sr_vals)) {
		arizona_aif_err(dai, "Unsupported sample rate %dHz\n",
				params_rate(params));
		return -EINVAL;
	}
	sr_val = i;

	lrclk = snd_soc_params_to_bclk(params) / params_rate(params);

	arizona_aif_dbg(dai, "BCLK %dHz LRCLK %dHz\n",
			rates[bclk], rates[bclk] / lrclk);

	wl = snd_pcm_format_width(params_format(params));
	frame = wl << ARIZONA_AIF1TX_WL_SHIFT | wl;

	/*
	 * We will need to be more flexible than this in future,
	 * currently we use a single sample rate for SYSCLK.
	 */
	switch (dai_priv->clk) {
	case ARIZONA_CLK_SYSCLK:
		snd_soc_update_bits(codec, ARIZONA_SAMPLE_RATE_1,
				    ARIZONA_SAMPLE_RATE_1_MASK, sr_val);
		snd_soc_update_bits(codec, base + ARIZONA_AIF_RATE_CTRL,
				    ARIZONA_AIF1_RATE_MASK, 0);
		break;
	case ARIZONA_CLK_ASYNCCLK:
		snd_soc_update_bits(codec, ARIZONA_ASYNC_SAMPLE_RATE_1,
				    ARIZONA_ASYNC_SAMPLE_RATE_MASK, sr_val);
		snd_soc_update_bits(codec, base + ARIZONA_AIF_RATE_CTRL,
				    ARIZONA_AIF1_RATE_MASK, 8);
		break;
	default:
		arizona_aif_err(dai, "Invalid clock %d\n", dai_priv->clk);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, base + ARIZONA_AIF_BCLK_CTRL,
			    ARIZONA_AIF1_BCLK_FREQ_MASK, bclk);
	snd_soc_update_bits(codec, base + ARIZONA_AIF_TX_BCLK_RATE,
			    ARIZONA_AIF1TX_BCPF_MASK, lrclk);
	snd_soc_update_bits(codec, base + ARIZONA_AIF_RX_BCLK_RATE,
			    ARIZONA_AIF1RX_BCPF_MASK, lrclk);
	snd_soc_update_bits(codec, base + ARIZONA_AIF_FRAME_CTRL_1,
			    ARIZONA_AIF1TX_WL_MASK |
			    ARIZONA_AIF1TX_SLOT_LEN_MASK, frame);
	snd_soc_update_bits(codec, base + ARIZONA_AIF_FRAME_CTRL_2,
			    ARIZONA_AIF1RX_WL_MASK |
			    ARIZONA_AIF1RX_SLOT_LEN_MASK, frame);

	return 0;
}

static const char *arizona_dai_clk_str(int clk_id)
{
	switch (clk_id) {
	case ARIZONA_CLK_SYSCLK:
		return "SYSCLK";
	case ARIZONA_CLK_ASYNCCLK:
		return "ASYNCCLK";
	default:
		return "Unknown clock";
	}
}

static int arizona_dai_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct arizona_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct arizona_dai_priv *dai_priv = &priv->dai[dai->id - 1];
	struct snd_soc_dapm_route routes[2];

	switch (clk_id) {
	case ARIZONA_CLK_SYSCLK:
	case ARIZONA_CLK_ASYNCCLK:
		break;
	default:
		return -EINVAL;
	}

	if (clk_id == dai_priv->clk)
		return 0;

	if (dai->active) {
		dev_err(codec->dev, "Can't change clock on active DAI %d\n",
			dai->id);
		return -EBUSY;
	}

	memset(&routes, 0, sizeof(routes));
	routes[0].sink = dai->driver->capture.stream_name;
	routes[1].sink = dai->driver->playback.stream_name;

	routes[0].source = arizona_dai_clk_str(dai_priv->clk);
	routes[1].source = arizona_dai_clk_str(dai_priv->clk);
	snd_soc_dapm_del_routes(&codec->dapm, routes, ARRAY_SIZE(routes));

	routes[0].source = arizona_dai_clk_str(clk_id);
	routes[1].source = arizona_dai_clk_str(clk_id);
	snd_soc_dapm_add_routes(&codec->dapm, routes, ARRAY_SIZE(routes));

	return snd_soc_dapm_sync(&codec->dapm);
}

const struct snd_soc_dai_ops arizona_dai_ops = {
	.startup = arizona_startup,
	.set_fmt = arizona_set_fmt,
	.hw_params = arizona_hw_params,
	.set_sysclk = arizona_dai_set_sysclk,
};

int arizona_init_dai(struct arizona_priv *priv, int id)
{
	struct arizona_dai_priv *dai_priv = &priv->dai[id];

	dai_priv->clk = ARIZONA_CLK_SYSCLK;

	return 0;
}
EXPORT_SYMBOL_GPL(arizona_init_dai);

static irqreturn_t arizona_fll_lock(int irq, void *data)
{
	struct arizona_fll *fll = data;

	arizona_fll_dbg(fll, "Locked\n");

	complete(&fll->lock);

	return IRQ_HANDLED;
}

static irqreturn_t arizona_fll_clock_ok(int irq, void *data)
{
	struct arizona_fll *fll = data;

	arizona_fll_dbg(fll, "clock OK\n");

	complete(&fll->ok);

	return IRQ_HANDLED;
}

static struct {
	unsigned int min;
	unsigned int max;
	u16 fratio;
	int ratio;
} fll_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

struct arizona_fll_cfg {
	int n;
	int theta;
	int lambda;
	int refdiv;
	int outdiv;
	int fratio;
};

static int arizona_calc_fll(struct arizona_fll *fll,
			    struct arizona_fll_cfg *cfg,
			    unsigned int Fref,
			    unsigned int Fout)
{
	unsigned int target, div, gcd_fll;
	int i, ratio;

	arizona_fll_dbg(fll, "Fref=%u Fout=%u\n", Fref, Fout);

	/* Fref must be <=13.5MHz */
	div = 1;
	cfg->refdiv = 0;
	while ((Fref / div) > 13500000) {
		div *= 2;
		cfg->refdiv++;

		if (div > 8) {
			arizona_fll_err(fll,
					"Can't scale %dMHz in to <=13.5MHz\n",
					Fref);
			return -EINVAL;
		}
	}

	/* Apply the division for our remaining calculations */
	Fref /= div;

	/* Fvco should be 90-100MHz; don't check the upper bound */
	div = 1;
	while (Fout * div < 90000000) {
		div++;
		if (div > 7) {
			arizona_fll_err(fll, "No FLL_OUTDIV for Fout=%uHz\n",
					Fout);
			return -EINVAL;
		}
	}
	target = Fout * div;
	cfg->outdiv = div;

	arizona_fll_dbg(fll, "Fvco=%dHz\n", target);

	/* Find an appropraite FLL_FRATIO and factor it out of the target */
	for (i = 0; i < ARRAY_SIZE(fll_fratios); i++) {
		if (fll_fratios[i].min <= Fref && Fref <= fll_fratios[i].max) {
			cfg->fratio = fll_fratios[i].fratio;
			ratio = fll_fratios[i].ratio;
			break;
		}
	}
	if (i == ARRAY_SIZE(fll_fratios)) {
		arizona_fll_err(fll, "Unable to find FRATIO for Fref=%uHz\n",
				Fref);
		return -EINVAL;
	}

	cfg->n = target / (ratio * Fref);

	if (target % Fref) {
		gcd_fll = gcd(target, ratio * Fref);
		arizona_fll_dbg(fll, "GCD=%u\n", gcd_fll);

		cfg->theta = (target - (cfg->n * ratio * Fref))
			/ gcd_fll;
		cfg->lambda = (ratio * Fref) / gcd_fll;
	} else {
		cfg->theta = 0;
		cfg->lambda = 0;
	}

	arizona_fll_dbg(fll, "N=%x THETA=%x LAMBDA=%x\n",
			cfg->n, cfg->theta, cfg->lambda);
	arizona_fll_dbg(fll, "FRATIO=%x(%d) OUTDIV=%x REFCLK_DIV=%x\n",
			cfg->fratio, cfg->fratio, cfg->outdiv, cfg->refdiv);

	return 0;

}

static void arizona_apply_fll(struct arizona *arizona, unsigned int base,
			      struct arizona_fll_cfg *cfg, int source)
{
	regmap_update_bits(arizona->regmap, base + 3,
			   ARIZONA_FLL1_THETA_MASK, cfg->theta);
	regmap_update_bits(arizona->regmap, base + 4,
			   ARIZONA_FLL1_LAMBDA_MASK, cfg->lambda);
	regmap_update_bits(arizona->regmap, base + 5,
			   ARIZONA_FLL1_FRATIO_MASK,
			   cfg->fratio << ARIZONA_FLL1_FRATIO_SHIFT);
	regmap_update_bits(arizona->regmap, base + 6,
			   ARIZONA_FLL1_CLK_REF_DIV_MASK |
			   ARIZONA_FLL1_CLK_REF_SRC_MASK,
			   cfg->refdiv << ARIZONA_FLL1_CLK_REF_DIV_SHIFT |
			   source << ARIZONA_FLL1_CLK_REF_SRC_SHIFT);

	regmap_update_bits(arizona->regmap, base + 2,
			   ARIZONA_FLL1_CTRL_UPD | ARIZONA_FLL1_N_MASK,
			   ARIZONA_FLL1_CTRL_UPD | cfg->n);
}

int arizona_set_fll(struct arizona_fll *fll, int source,
		    unsigned int Fref, unsigned int Fout)
{
	struct arizona *arizona = fll->arizona;
	struct arizona_fll_cfg cfg, sync;
	unsigned int reg, val;
	int syncsrc;
	bool ena;
	int ret;

	ret = regmap_read(arizona->regmap, fll->base + 1, &reg);
	if (ret != 0) {
		arizona_fll_err(fll, "Failed to read current state: %d\n",
				ret);
		return ret;
	}
	ena = reg & ARIZONA_FLL1_ENA;

	if (Fout) {
		/* Do we have a 32kHz reference? */
		regmap_read(arizona->regmap, ARIZONA_CLOCK_32K_1, &val);
		switch (val & ARIZONA_CLK_32K_SRC_MASK) {
		case ARIZONA_CLK_SRC_MCLK1:
		case ARIZONA_CLK_SRC_MCLK2:
			syncsrc = val & ARIZONA_CLK_32K_SRC_MASK;
			break;
		default:
			syncsrc = -1;
		}

		if (source == syncsrc)
			syncsrc = -1;

		if (syncsrc >= 0) {
			ret = arizona_calc_fll(fll, &sync, Fref, Fout);
			if (ret != 0)
				return ret;

			ret = arizona_calc_fll(fll, &cfg, 32768, Fout);
			if (ret != 0)
				return ret;
		} else {
			ret = arizona_calc_fll(fll, &cfg, Fref, Fout);
			if (ret != 0)
				return ret;
		}
	} else {
		regmap_update_bits(arizona->regmap, fll->base + 1,
				   ARIZONA_FLL1_ENA, 0);
		regmap_update_bits(arizona->regmap, fll->base + 0x11,
				   ARIZONA_FLL1_SYNC_ENA, 0);

		if (ena)
			pm_runtime_put_autosuspend(arizona->dev);

		return 0;
	}

	regmap_update_bits(arizona->regmap, fll->base + 5,
			   ARIZONA_FLL1_OUTDIV_MASK,
			   cfg.outdiv << ARIZONA_FLL1_OUTDIV_SHIFT);

	if (syncsrc >= 0) {
		arizona_apply_fll(arizona, fll->base, &cfg, syncsrc);
		arizona_apply_fll(arizona, fll->base + 0x10, &sync, source);
	} else {
		arizona_apply_fll(arizona, fll->base, &cfg, source);
	}

	if (!ena)
		pm_runtime_get(arizona->dev);

	/* Clear any pending completions */
	try_wait_for_completion(&fll->ok);

	regmap_update_bits(arizona->regmap, fll->base + 1,
			   ARIZONA_FLL1_ENA, ARIZONA_FLL1_ENA);
	if (syncsrc >= 0)
		regmap_update_bits(arizona->regmap, fll->base + 0x11,
				   ARIZONA_FLL1_SYNC_ENA,
				   ARIZONA_FLL1_SYNC_ENA);

	ret = wait_for_completion_timeout(&fll->ok,
					  msecs_to_jiffies(25));
	if (ret == 0)
		arizona_fll_warn(fll, "Timed out waiting for lock\n");

	return 0;
}
EXPORT_SYMBOL_GPL(arizona_set_fll);

int arizona_init_fll(struct arizona *arizona, int id, int base, int lock_irq,
		     int ok_irq, struct arizona_fll *fll)
{
	int ret;

	init_completion(&fll->lock);
	init_completion(&fll->ok);

	fll->id = id;
	fll->base = base;
	fll->arizona = arizona;

	snprintf(fll->lock_name, sizeof(fll->lock_name), "FLL%d lock", id);
	snprintf(fll->clock_ok_name, sizeof(fll->clock_ok_name),
		 "FLL%d clock OK", id);

	ret = arizona_request_irq(arizona, lock_irq, fll->lock_name,
				  arizona_fll_lock, fll);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to get FLL%d lock IRQ: %d\n",
			id, ret);
	}

	ret = arizona_request_irq(arizona, ok_irq, fll->clock_ok_name,
				  arizona_fll_clock_ok, fll);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to get FLL%d clock OK IRQ: %d\n",
			id, ret);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(arizona_init_fll);

MODULE_DESCRIPTION("ASoC Wolfson Arizona class device support");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
