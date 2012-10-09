/*
 * wm8804.c  --  WM8804 S/PDIF transceiver driver
 *
 * Copyright 2010-11 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8804.h"

#define WM8804_NUM_SUPPLIES 2
static const char *wm8804_supply_names[WM8804_NUM_SUPPLIES] = {
	"PVDD",
	"DVDD"
};

static const struct reg_default wm8804_reg_defaults[] = {
	{ 3,  0x21 },     /* R3  - PLL1 */
	{ 4,  0xFD },     /* R4  - PLL2 */
	{ 5,  0x36 },     /* R5  - PLL3 */
	{ 6,  0x07 },     /* R6  - PLL4 */
	{ 7,  0x16 },     /* R7  - PLL5 */
	{ 8,  0x18 },     /* R8  - PLL6 */
	{ 9,  0xFF },     /* R9  - SPDMODE */
	{ 10, 0x00 },     /* R10 - INTMASK */
	{ 18, 0x00 },     /* R18 - SPDTX1 */
	{ 19, 0x00 },     /* R19 - SPDTX2 */
	{ 20, 0x00 },     /* R20 - SPDTX3 */
	{ 21, 0x71 },     /* R21 - SPDTX4 */
	{ 22, 0x0B },     /* R22 - SPDTX5 */
	{ 23, 0x70 },     /* R23 - GPO0 */
	{ 24, 0x57 },     /* R24 - GPO1 */
	{ 26, 0x42 },     /* R26 - GPO2 */
	{ 27, 0x06 },     /* R27 - AIFTX */
	{ 28, 0x06 },     /* R28 - AIFRX */
	{ 29, 0x80 },     /* R29 - SPDRX1 */
	{ 30, 0x07 },     /* R30 - PWRDN */
};

struct wm8804_priv {
	struct regmap *regmap;
	struct regulator_bulk_data supplies[WM8804_NUM_SUPPLIES];
	struct notifier_block disable_nb[WM8804_NUM_SUPPLIES];
};

static int txsrc_get(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol);

static int txsrc_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol);

/*
 * We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define WM8804_REGULATOR_EVENT(n) \
static int wm8804_regulator_event_##n(struct notifier_block *nb, \
				      unsigned long event, void *data)    \
{ \
	struct wm8804_priv *wm8804 = container_of(nb, struct wm8804_priv, \
						  disable_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		regcache_mark_dirty(wm8804->regmap);	\
	} \
	return 0; \
}

WM8804_REGULATOR_EVENT(0)
WM8804_REGULATOR_EVENT(1)

static const char *txsrc_text[] = { "S/PDIF RX", "AIF" };
static const SOC_ENUM_SINGLE_EXT_DECL(txsrc, txsrc_text);

static const struct snd_kcontrol_new wm8804_snd_controls[] = {
	SOC_ENUM_EXT("Input Source", txsrc, txsrc_get, txsrc_put),
	SOC_SINGLE("TX Playback Switch", WM8804_PWRDN, 2, 1, 1),
	SOC_SINGLE("AIF Playback Switch", WM8804_PWRDN, 4, 1, 1)
};

static int txsrc_get(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec;
	unsigned int src;

	codec = snd_kcontrol_chip(kcontrol);
	src = snd_soc_read(codec, WM8804_SPDTX4);
	if (src & 0x40)
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int txsrc_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec;
	unsigned int src, txpwr;

	codec = snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.integer.value[0] != 0
			&& ucontrol->value.integer.value[0] != 1)
		return -EINVAL;

	src = snd_soc_read(codec, WM8804_SPDTX4);
	switch ((src & 0x40) >> 6) {
	case 0:
		if (!ucontrol->value.integer.value[0])
			return 0;
		break;
	case 1:
		if (ucontrol->value.integer.value[1])
			return 0;
		break;
	}

	/* save the current power state of the transmitter */
	txpwr = snd_soc_read(codec, WM8804_PWRDN) & 0x4;
	/* power down the transmitter */
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x4, 0x4);
	/* set the tx source */
	snd_soc_update_bits(codec, WM8804_SPDTX4, 0x40,
			    ucontrol->value.integer.value[0] << 6);

	if (ucontrol->value.integer.value[0]) {
		/* power down the receiver */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x2, 0x2);
		/* power up the AIF */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x10, 0);
	} else {
		/* don't power down the AIF -- may be used as an output */
		/* power up the receiver */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x2, 0);
	}

	/* restore the transmitter's configuration */
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x4, txpwr);

	return 0;
}

static bool wm8804_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8804_RST_DEVID1:
	case WM8804_DEVID2:
	case WM8804_DEVREV:
	case WM8804_INTSTAT:
	case WM8804_SPDSTAT:
	case WM8804_RXCHAN1:
	case WM8804_RXCHAN2:
	case WM8804_RXCHAN3:
	case WM8804_RXCHAN4:
	case WM8804_RXCHAN5:
		return true;
	default:
		return false;
	}
}

static int wm8804_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8804_RST_DEVID1, 0x0);
}

static int wm8804_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec;
	u16 format, master, bcp, lrp;

	codec = dai->codec;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format = 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		format = 0x0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format = 0x1;
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		format = 0x3;
		break;
	default:
		dev_err(dai->dev, "Unknown dai format\n");
		return -EINVAL;
	}

	/* set data format */
	snd_soc_update_bits(codec, WM8804_AIFTX, 0x3, format);
	snd_soc_update_bits(codec, WM8804_AIFRX, 0x3, format);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		master = 0;
		break;
	default:
		dev_err(dai->dev, "Unknown master/slave configuration\n");
		return -EINVAL;
	}

	/* set master/slave mode */
	snd_soc_update_bits(codec, WM8804_AIFRX, 0x40, master << 6);

	bcp = lrp = 0;
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bcp = lrp = 1;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bcp = 1;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrp = 1;
		break;
	default:
		dev_err(dai->dev, "Unknown polarity configuration\n");
		return -EINVAL;
	}

	/* set frame inversion */
	snd_soc_update_bits(codec, WM8804_AIFTX, 0x10 | 0x20,
			    (bcp << 4) | (lrp << 5));
	snd_soc_update_bits(codec, WM8804_AIFRX, 0x10 | 0x20,
			    (bcp << 4) | (lrp << 5));
	return 0;
}

static int wm8804_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec;
	u16 blen;

	codec = dai->codec;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		blen = 0x0;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		blen = 0x1;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		blen = 0x2;
		break;
	default:
		dev_err(dai->dev, "Unsupported word length: %u\n",
			params_format(params));
		return -EINVAL;
	}

	/* set word length */
	snd_soc_update_bits(codec, WM8804_AIFTX, 0xc, blen << 2);
	snd_soc_update_bits(codec, WM8804_AIFRX, 0xc, blen << 2);

	return 0;
}

struct pll_div {
	u32 prescale:1;
	u32 mclkdiv:1;
	u32 freqmode:2;
	u32 n:4;
	u32 k:22;
};

/* PLL rate to output rate divisions */
static struct {
	unsigned int div;
	unsigned int freqmode;
	unsigned int mclkdiv;
} post_table[] = {
	{  2,  0, 0 },
	{  4,  0, 1 },
	{  4,  1, 0 },
	{  8,  1, 1 },
	{  8,  2, 0 },
	{ 16,  2, 1 },
	{ 12,  3, 0 },
	{ 24,  3, 1 }
};

#define FIXED_PLL_SIZE ((1ULL << 22) * 10)
static int pll_factors(struct pll_div *pll_div, unsigned int target,
		       unsigned int source)
{
	u64 Kpart;
	unsigned long int K, Ndiv, Nmod, tmp;
	int i;

	/*
	 * Scale the output frequency up; the PLL should run in the
	 * region of 90-100MHz.
	 */
	for (i = 0; i < ARRAY_SIZE(post_table); i++) {
		tmp = target * post_table[i].div;
		if (tmp >= 90000000 && tmp <= 100000000) {
			pll_div->freqmode = post_table[i].freqmode;
			pll_div->mclkdiv = post_table[i].mclkdiv;
			target *= post_table[i].div;
			break;
		}
	}

	if (i == ARRAY_SIZE(post_table)) {
		pr_err("%s: Unable to scale output frequency: %uHz\n",
		       __func__, target);
		return -EINVAL;
	}

	pll_div->prescale = 0;
	Ndiv = target / source;
	if (Ndiv < 5) {
		source >>= 1;
		pll_div->prescale = 1;
		Ndiv = target / source;
	}

	if (Ndiv < 5 || Ndiv > 13) {
		pr_err("%s: WM8804 N value is not within the recommended range: %lu\n",
		       __func__, Ndiv);
		return -EINVAL;
	}
	pll_div->n = Ndiv;

	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (u64)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xffffffff;
	if ((K % 10) >= 5)
		K += 5;
	K /= 10;
	pll_div->k = K;

	return 0;
}

static int wm8804_set_pll(struct snd_soc_dai *dai, int pll_id,
			  int source, unsigned int freq_in,
			  unsigned int freq_out)
{
	struct snd_soc_codec *codec;

	codec = dai->codec;
	if (!freq_in || !freq_out) {
		/* disable the PLL */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x1, 0x1);
		return 0;
	} else {
		int ret;
		struct pll_div pll_div;

		ret = pll_factors(&pll_div, freq_out, freq_in);
		if (ret)
			return ret;

		/* power down the PLL before reprogramming it */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x1, 0x1);

		if (!freq_in || !freq_out)
			return 0;

		/* set PLLN and PRESCALE */
		snd_soc_update_bits(codec, WM8804_PLL4, 0xf | 0x10,
				    pll_div.n | (pll_div.prescale << 4));
		/* set mclkdiv and freqmode */
		snd_soc_update_bits(codec, WM8804_PLL5, 0x3 | 0x8,
				    pll_div.freqmode | (pll_div.mclkdiv << 3));
		/* set PLLK */
		snd_soc_write(codec, WM8804_PLL1, pll_div.k & 0xff);
		snd_soc_write(codec, WM8804_PLL2, (pll_div.k >> 8) & 0xff);
		snd_soc_write(codec, WM8804_PLL3, pll_div.k >> 16);

		/* power up the PLL */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x1, 0);
	}

	return 0;
}

static int wm8804_set_sysclk(struct snd_soc_dai *dai,
			     int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec;

	codec = dai->codec;

	switch (clk_id) {
	case WM8804_TX_CLKSRC_MCLK:
		if ((freq >= 10000000 && freq <= 14400000)
				|| (freq >= 16280000 && freq <= 27000000))
			snd_soc_update_bits(codec, WM8804_PLL6, 0x80, 0x80);
		else {
			dev_err(dai->dev, "OSCCLOCK is not within the "
				"recommended range: %uHz\n", freq);
			return -EINVAL;
		}
		break;
	case WM8804_TX_CLKSRC_PLL:
		snd_soc_update_bits(codec, WM8804_PLL6, 0x80, 0);
		break;
	case WM8804_CLKOUT_SRC_CLK1:
		snd_soc_update_bits(codec, WM8804_PLL6, 0x8, 0);
		break;
	case WM8804_CLKOUT_SRC_OSCCLK:
		snd_soc_update_bits(codec, WM8804_PLL6, 0x8, 0x8);
		break;
	default:
		dev_err(dai->dev, "Unknown clock source: %d\n", clk_id);
		return -EINVAL;
	}

	return 0;
}

static int wm8804_set_clkdiv(struct snd_soc_dai *dai,
			     int div_id, int div)
{
	struct snd_soc_codec *codec;

	codec = dai->codec;
	switch (div_id) {
	case WM8804_CLKOUT_DIV:
		snd_soc_update_bits(codec, WM8804_PLL5, 0x30,
				    (div & 0x3) << 4);
		break;
	default:
		dev_err(dai->dev, "Unknown clock divider: %d\n", div_id);
		return -EINVAL;
	}
	return 0;
}

static int wm8804_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	int ret;
	struct wm8804_priv *wm8804;

	wm8804 = snd_soc_codec_get_drvdata(codec);
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		/* power up the OSC and the PLL */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x9, 0);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8804->supplies),
						    wm8804->supplies);
			if (ret) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}
			regcache_sync(wm8804->regmap);
		}
		/* power down the OSC and the PLL */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x9, 0x9);
		break;
	case SND_SOC_BIAS_OFF:
		/* power down the OSC and the PLL */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x9, 0x9);
		regulator_bulk_disable(ARRAY_SIZE(wm8804->supplies),
				       wm8804->supplies);
		break;
	}

	codec->dapm.bias_level = level;
	return 0;
}

#ifdef CONFIG_PM
static int wm8804_suspend(struct snd_soc_codec *codec)
{
	wm8804_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8804_resume(struct snd_soc_codec *codec)
{
	wm8804_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define wm8804_suspend NULL
#define wm8804_resume NULL
#endif

static int wm8804_remove(struct snd_soc_codec *codec)
{
	struct wm8804_priv *wm8804;
	int i;

	wm8804 = snd_soc_codec_get_drvdata(codec);
	wm8804_set_bias_level(codec, SND_SOC_BIAS_OFF);

	for (i = 0; i < ARRAY_SIZE(wm8804->supplies); ++i)
		regulator_unregister_notifier(wm8804->supplies[i].consumer,
					      &wm8804->disable_nb[i]);
	regulator_bulk_free(ARRAY_SIZE(wm8804->supplies), wm8804->supplies);
	return 0;
}

static int wm8804_probe(struct snd_soc_codec *codec)
{
	struct wm8804_priv *wm8804;
	int i, id1, id2, ret;

	wm8804 = snd_soc_codec_get_drvdata(codec);

	codec->control_data = wm8804->regmap;

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_REGMAP);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache i/o: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm8804->supplies); i++)
		wm8804->supplies[i].supply = wm8804_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8804->supplies),
				 wm8804->supplies);
	if (ret) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	wm8804->disable_nb[0].notifier_call = wm8804_regulator_event_0;
	wm8804->disable_nb[1].notifier_call = wm8804_regulator_event_1;

	/* This should really be moved into the regulator core */
	for (i = 0; i < ARRAY_SIZE(wm8804->supplies); i++) {
		ret = regulator_register_notifier(wm8804->supplies[i].consumer,
						  &wm8804->disable_nb[i]);
		if (ret != 0) {
			dev_err(codec->dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8804->supplies),
				    wm8804->supplies);
	if (ret) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		goto err_reg_get;
	}

	id1 = snd_soc_read(codec, WM8804_RST_DEVID1);
	if (id1 < 0) {
		dev_err(codec->dev, "Failed to read device ID: %d\n", id1);
		ret = id1;
		goto err_reg_enable;
	}

	id2 = snd_soc_read(codec, WM8804_DEVID2);
	if (id2 < 0) {
		dev_err(codec->dev, "Failed to read device ID: %d\n", id2);
		ret = id2;
		goto err_reg_enable;
	}

	id2 = (id2 << 8) | id1;

	if (id2 != 0x8805) {
		dev_err(codec->dev, "Invalid device ID: %#x\n", id2);
		ret = -EINVAL;
		goto err_reg_enable;
	}

	ret = snd_soc_read(codec, WM8804_DEVREV);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read device revision: %d\n",
			ret);
		goto err_reg_enable;
	}
	dev_info(codec->dev, "revision %c\n", ret + 'A');

	ret = wm8804_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset: %d\n", ret);
		goto err_reg_enable;
	}

	wm8804_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;

err_reg_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8804->supplies), wm8804->supplies);
err_reg_get:
	regulator_bulk_free(ARRAY_SIZE(wm8804->supplies), wm8804->supplies);
	return ret;
}

static const struct snd_soc_dai_ops wm8804_dai_ops = {
	.hw_params = wm8804_hw_params,
	.set_fmt = wm8804_set_fmt,
	.set_sysclk = wm8804_set_sysclk,
	.set_clkdiv = wm8804_set_clkdiv,
	.set_pll = wm8804_set_pll
};

#define WM8804_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

#define WM8804_RATES (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
		      SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_64000 | \
		      SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | \
		      SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

static struct snd_soc_dai_driver wm8804_dai = {
	.name = "wm8804-spdif",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8804_RATES,
		.formats = WM8804_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8804_RATES,
		.formats = WM8804_FORMATS,
	},
	.ops = &wm8804_dai_ops,
	.symmetric_rates = 1
};

static struct snd_soc_codec_driver soc_codec_dev_wm8804 = {
	.probe = wm8804_probe,
	.remove = wm8804_remove,
	.suspend = wm8804_suspend,
	.resume = wm8804_resume,
	.set_bias_level = wm8804_set_bias_level,
	.idle_bias_off = true,

	.controls = wm8804_snd_controls,
	.num_controls = ARRAY_SIZE(wm8804_snd_controls),
};

static const struct of_device_id wm8804_of_match[] = {
	{ .compatible = "wlf,wm8804", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8804_of_match);

static struct regmap_config wm8804_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = WM8804_MAX_REGISTER,
	.volatile_reg = wm8804_volatile,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm8804_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8804_reg_defaults),
};

#if defined(CONFIG_SPI_MASTER)
static int __devinit wm8804_spi_probe(struct spi_device *spi)
{
	struct wm8804_priv *wm8804;
	int ret;

	wm8804 = devm_kzalloc(&spi->dev, sizeof *wm8804, GFP_KERNEL);
	if (!wm8804)
		return -ENOMEM;

	wm8804->regmap = regmap_init_spi(spi, &wm8804_regmap_config);
	if (IS_ERR(wm8804->regmap)) {
		ret = PTR_ERR(wm8804->regmap);
		return ret;
	}

	spi_set_drvdata(spi, wm8804);

	ret = snd_soc_register_codec(&spi->dev,
				     &soc_codec_dev_wm8804, &wm8804_dai, 1);

	return ret;
}

static int __devexit wm8804_spi_remove(struct spi_device *spi)
{
	struct wm8804_priv *wm8804 = spi_get_drvdata(spi);
	snd_soc_unregister_codec(&spi->dev);
	regmap_exit(wm8804->regmap);
	return 0;
}

static struct spi_driver wm8804_spi_driver = {
	.driver = {
		.name = "wm8804",
		.owner = THIS_MODULE,
		.of_match_table = wm8804_of_match,
	},
	.probe = wm8804_spi_probe,
	.remove = __devexit_p(wm8804_spi_remove)
};
#endif

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8804_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8804_priv *wm8804;
	int ret;

	wm8804 = devm_kzalloc(&i2c->dev, sizeof *wm8804, GFP_KERNEL);
	if (!wm8804)
		return -ENOMEM;

	wm8804->regmap = regmap_init_i2c(i2c, &wm8804_regmap_config);
	if (IS_ERR(wm8804->regmap)) {
		ret = PTR_ERR(wm8804->regmap);
		return ret;
	}

	i2c_set_clientdata(i2c, wm8804);

	ret = snd_soc_register_codec(&i2c->dev,
				     &soc_codec_dev_wm8804, &wm8804_dai, 1);
	if (ret != 0)
		goto err;

	return 0;

err:
	regmap_exit(wm8804->regmap);
	return ret;
}

static __devexit int wm8804_i2c_remove(struct i2c_client *i2c)
{
	struct wm8804_priv *wm8804 = i2c_get_clientdata(i2c);

	snd_soc_unregister_codec(&i2c->dev);
	regmap_exit(wm8804->regmap);

	return 0;
}

static const struct i2c_device_id wm8804_i2c_id[] = {
	{ "wm8804", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8804_i2c_id);

static struct i2c_driver wm8804_i2c_driver = {
	.driver = {
		.name = "wm8804",
		.owner = THIS_MODULE,
		.of_match_table = wm8804_of_match,
	},
	.probe = wm8804_i2c_probe,
	.remove = __devexit_p(wm8804_i2c_remove),
	.id_table = wm8804_i2c_id
};
#endif

static int __init wm8804_modinit(void)
{
	int ret = 0;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8804_i2c_driver);
	if (ret) {
		printk(KERN_ERR "Failed to register wm8804 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8804_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8804 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8804_modinit);

static void __exit wm8804_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8804_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8804_spi_driver);
#endif
}
module_exit(wm8804_exit);

MODULE_DESCRIPTION("ASoC WM8804 driver");
MODULE_AUTHOR("Dimitris Papastamos <dp@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
