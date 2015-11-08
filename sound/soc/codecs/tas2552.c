/*
 * tas2552.c - ALSA SoC Texas Instruments TAS2552 Mono Audio Amplifier
 *
 * Copyright (C) 2014 Texas Instruments Incorporated -  http://www.ti.com
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/tas2552-plat.h>
#include <dt-bindings/sound/tas2552.h>

#include "tas2552.h"

static const struct reg_default tas2552_reg_defs[] = {
	{TAS2552_CFG_1, 0x22},
	{TAS2552_CFG_3, 0x80},
	{TAS2552_DOUT, 0x00},
	{TAS2552_OUTPUT_DATA, 0xc0},
	{TAS2552_PDM_CFG, 0x01},
	{TAS2552_PGA_GAIN, 0x00},
	{TAS2552_BOOST_APT_CTRL, 0x0f},
	{TAS2552_RESERVED_0D, 0xbe},
	{TAS2552_LIMIT_RATE_HYS, 0x08},
	{TAS2552_CFG_2, 0xef},
	{TAS2552_SER_CTRL_1, 0x00},
	{TAS2552_SER_CTRL_2, 0x00},
	{TAS2552_PLL_CTRL_1, 0x10},
	{TAS2552_PLL_CTRL_2, 0x00},
	{TAS2552_PLL_CTRL_3, 0x00},
	{TAS2552_BTIP, 0x8f},
	{TAS2552_BTS_CTRL, 0x80},
	{TAS2552_LIMIT_RELEASE, 0x04},
	{TAS2552_LIMIT_INT_COUNT, 0x00},
	{TAS2552_EDGE_RATE_CTRL, 0x40},
	{TAS2552_VBAT_DATA, 0x00},
};

#define TAS2552_NUM_SUPPLIES	3
static const char *tas2552_supply_names[TAS2552_NUM_SUPPLIES] = {
	"vbat",		/* vbat voltage */
	"iovdd",	/* I/O Voltage */
	"avdd",		/* Analog DAC Voltage */
};

struct tas2552_data {
	struct snd_soc_codec *codec;
	struct regmap *regmap;
	struct i2c_client *tas2552_client;
	struct regulator_bulk_data supplies[TAS2552_NUM_SUPPLIES];
	struct gpio_desc *enable_gpio;
	unsigned char regs[TAS2552_VBAT_DATA];
	unsigned int pll_clkin;
	int pll_clk_id;
	unsigned int pdm_clk;
	int pdm_clk_id;

	unsigned int dai_fmt;
	unsigned int tdm_delay;
};

static int tas2552_post_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_write(codec, TAS2552_RESERVED_0D, 0xc0);
		snd_soc_update_bits(codec, TAS2552_LIMIT_RATE_HYS, (1 << 5),
				    (1 << 5));
		snd_soc_update_bits(codec, TAS2552_CFG_2, 1, 0);
		snd_soc_update_bits(codec, TAS2552_CFG_1, TAS2552_SWS, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, TAS2552_CFG_1, TAS2552_SWS,
				    TAS2552_SWS);
		snd_soc_update_bits(codec, TAS2552_CFG_2, 1, 1);
		snd_soc_update_bits(codec, TAS2552_LIMIT_RATE_HYS, (1 << 5), 0);
		snd_soc_write(codec, TAS2552_RESERVED_0D, 0xbe);
		break;
	}
	return 0;
}

/* Input mux controls */
static const char * const tas2552_input_texts[] = {
	"Digital", "Analog" };
static SOC_ENUM_SINGLE_DECL(tas2552_input_mux_enum, TAS2552_CFG_3, 7,
			    tas2552_input_texts);

static const struct snd_kcontrol_new tas2552_input_mux_control =
	SOC_DAPM_ENUM("Route", tas2552_input_mux_enum);

static const struct snd_soc_dapm_widget tas2552_dapm_widgets[] =
{
	SND_SOC_DAPM_INPUT("IN"),

	/* MUX Controls */
	SND_SOC_DAPM_MUX("Input selection", SND_SOC_NOPM, 0, 0,
			 &tas2552_input_mux_control),

	SND_SOC_DAPM_AIF_IN("DAC IN", "DAC Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUT_DRV("ClassD", TAS2552_CFG_2, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL", TAS2552_CFG_2, 3, 0, NULL, 0),
	SND_SOC_DAPM_POST("Post Event", tas2552_post_event),

	SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_dapm_route tas2552_audio_map[] = {
	{"DAC", NULL, "DAC IN"},
	{"Input selection", "Digital", "DAC"},
	{"Input selection", "Analog", "IN"},
	{"ClassD", NULL, "Input selection"},
	{"OUT", NULL, "ClassD"},
	{"ClassD", NULL, "PLL"},
};

#ifdef CONFIG_PM
static void tas2552_sw_shutdown(struct tas2552_data *tas2552, int sw_shutdown)
{
	u8 cfg1_reg = 0;

	if (!tas2552->codec)
		return;

	if (sw_shutdown)
		cfg1_reg = TAS2552_SWS;

	snd_soc_update_bits(tas2552->codec, TAS2552_CFG_1, TAS2552_SWS,
			    cfg1_reg);
}
#endif

static int tas2552_setup_pll(struct snd_soc_codec *codec,
			     struct snd_pcm_hw_params *params)
{
	struct tas2552_data *tas2552 = dev_get_drvdata(codec->dev);
	bool bypass_pll = false;
	unsigned int pll_clk = params_rate(params) * 512;
	unsigned int pll_clkin = tas2552->pll_clkin;
	u8 pll_enable;

	if (!pll_clkin) {
		if (tas2552->pll_clk_id != TAS2552_PLL_CLKIN_BCLK)
			return -EINVAL;

		pll_clkin = snd_soc_params_to_bclk(params);
		pll_clkin += tas2552->tdm_delay;
	}

	pll_enable = snd_soc_read(codec, TAS2552_CFG_2) & TAS2552_PLL_ENABLE;
	snd_soc_update_bits(codec, TAS2552_CFG_2, TAS2552_PLL_ENABLE, 0);

	if (pll_clkin == pll_clk)
		bypass_pll = true;

	if (bypass_pll) {
		/* By pass the PLL configuration */
		snd_soc_update_bits(codec, TAS2552_PLL_CTRL_2,
				    TAS2552_PLL_BYPASS, TAS2552_PLL_BYPASS);
	} else {
		/* Fill in the PLL control registers for J & D
		 * pll_clk = (.5 * pll_clkin * J.D) / 2^p
		 * Need to fill in J and D here based on incoming freq
		 */
		unsigned int d;
		u8 j;
		u8 pll_sel = (tas2552->pll_clk_id << 3) & TAS2552_PLL_SRC_MASK;
		u8 p = snd_soc_read(codec, TAS2552_PLL_CTRL_1);

		p = (p >> 7);

recalc:
		j = (pll_clk * 2 * (1 << p)) / pll_clkin;
		d = (pll_clk * 2 * (1 << p)) % pll_clkin;
		d /= (pll_clkin / 10000);

		if (d && (pll_clkin < 512000 || pll_clkin > 9200000)) {
			if (tas2552->pll_clk_id == TAS2552_PLL_CLKIN_BCLK) {
				pll_clkin = 1800000;
				pll_sel = (TAS2552_PLL_CLKIN_1_8_FIXED << 3) &
							TAS2552_PLL_SRC_MASK;
			} else {
				pll_clkin = snd_soc_params_to_bclk(params);
				pll_clkin += tas2552->tdm_delay;
				pll_sel = (TAS2552_PLL_CLKIN_BCLK << 3) &
							TAS2552_PLL_SRC_MASK;
			}
			goto recalc;
		}

		snd_soc_update_bits(codec, TAS2552_CFG_1, TAS2552_PLL_SRC_MASK,
				    pll_sel);

		snd_soc_update_bits(codec, TAS2552_PLL_CTRL_1,
				    TAS2552_PLL_J_MASK, j);
		/* Will clear the PLL_BYPASS bit */
		snd_soc_write(codec, TAS2552_PLL_CTRL_2,
			      TAS2552_PLL_D_UPPER(d));
		snd_soc_write(codec, TAS2552_PLL_CTRL_3,
			      TAS2552_PLL_D_LOWER(d));
	}

	/* Restore PLL status */
	snd_soc_update_bits(codec, TAS2552_CFG_2, TAS2552_PLL_ENABLE,
			    pll_enable);

	return 0;
}

static int tas2552_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2552_data *tas2552 = dev_get_drvdata(codec->dev);
	int cpf;
	u8 ser_ctrl1_reg, wclk_rate;

	switch (params_width(params)) {
	case 16:
		ser_ctrl1_reg = TAS2552_WORDLENGTH_16BIT;
		cpf = 32 + tas2552->tdm_delay;
		break;
	case 20:
		ser_ctrl1_reg = TAS2552_WORDLENGTH_20BIT;
		cpf = 64 + tas2552->tdm_delay;
		break;
	case 24:
		ser_ctrl1_reg = TAS2552_WORDLENGTH_24BIT;
		cpf = 64 + tas2552->tdm_delay;
		break;
	case 32:
		ser_ctrl1_reg = TAS2552_WORDLENGTH_32BIT;
		cpf = 64 + tas2552->tdm_delay;
		break;
	default:
		dev_err(codec->dev, "Not supported sample size: %d\n",
			params_width(params));
		return -EINVAL;
	}

	if (cpf <= 32)
		ser_ctrl1_reg |= TAS2552_CLKSPERFRAME_32;
	else if (cpf <= 64)
		ser_ctrl1_reg |= TAS2552_CLKSPERFRAME_64;
	else if (cpf <= 128)
		ser_ctrl1_reg |= TAS2552_CLKSPERFRAME_128;
	else
		ser_ctrl1_reg |= TAS2552_CLKSPERFRAME_256;

	snd_soc_update_bits(codec, TAS2552_SER_CTRL_1,
			    TAS2552_WORDLENGTH_MASK | TAS2552_CLKSPERFRAME_MASK,
			    ser_ctrl1_reg);

	switch (params_rate(params)) {
	case 8000:
		wclk_rate = TAS2552_WCLK_FREQ_8KHZ;
		break;
	case 11025:
	case 12000:
		wclk_rate = TAS2552_WCLK_FREQ_11_12KHZ;
		break;
	case 16000:
		wclk_rate = TAS2552_WCLK_FREQ_16KHZ;
		break;
	case 22050:
	case 24000:
		wclk_rate = TAS2552_WCLK_FREQ_22_24KHZ;
		break;
	case 32000:
		wclk_rate = TAS2552_WCLK_FREQ_32KHZ;
		break;
	case 44100:
	case 48000:
		wclk_rate = TAS2552_WCLK_FREQ_44_48KHZ;
		break;
	case 88200:
	case 96000:
		wclk_rate = TAS2552_WCLK_FREQ_88_96KHZ;
		break;
	case 176400:
	case 192000:
		wclk_rate = TAS2552_WCLK_FREQ_176_192KHZ;
		break;
	default:
		dev_err(codec->dev, "Not supported sample rate: %d\n",
			params_rate(params));
		return -EINVAL;
	}

	snd_soc_update_bits(codec, TAS2552_CFG_3, TAS2552_WCLK_FREQ_MASK,
			    wclk_rate);

	return tas2552_setup_pll(codec, params);
}

#define TAS2552_DAI_FMT_MASK	(TAS2552_BCLKDIR | \
				 TAS2552_WCLKDIR | \
				 TAS2552_DATAFORMAT_MASK)
static int tas2552_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2552_data *tas2552 = snd_soc_codec_get_drvdata(codec);
	int delay = 0;

	/* TDM slot selection only valid in DSP_A/_B mode */
	if (tas2552->dai_fmt == SND_SOC_DAIFMT_DSP_A)
		delay += (tas2552->tdm_delay + 1);
	else if (tas2552->dai_fmt == SND_SOC_DAIFMT_DSP_B)
		delay += tas2552->tdm_delay;

	/* Configure data delay */
	snd_soc_write(codec, TAS2552_SER_CTRL_2, delay);

	return 0;
}

static int tas2552_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2552_data *tas2552 = dev_get_drvdata(codec->dev);
	u8 serial_format;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		serial_format = 0x00;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		serial_format = TAS2552_WCLKDIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		serial_format = TAS2552_BCLKDIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		serial_format = (TAS2552_BCLKDIR | TAS2552_WCLKDIR);
		break;
	default:
		dev_vdbg(codec->dev, "DAI Format master is not found\n");
		return -EINVAL;
	}

	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK |
		       SND_SOC_DAIFMT_INV_MASK)) {
	case (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF):
		break;
	case (SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF):
	case (SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_IB_NF):
		serial_format |= TAS2552_DATAFORMAT_DSP;
		break;
	case (SND_SOC_DAIFMT_RIGHT_J | SND_SOC_DAIFMT_NB_NF):
		serial_format |= TAS2552_DATAFORMAT_RIGHT_J;
		break;
	case (SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF):
		serial_format |= TAS2552_DATAFORMAT_LEFT_J;
		break;
	default:
		dev_vdbg(codec->dev, "DAI Format is not found\n");
		return -EINVAL;
	}
	tas2552->dai_fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	snd_soc_update_bits(codec, TAS2552_SER_CTRL_1, TAS2552_DAI_FMT_MASK,
			    serial_format);
	return 0;
}

static int tas2552_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				  unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2552_data *tas2552 = dev_get_drvdata(codec->dev);
	u8 reg, mask, val;

	switch (clk_id) {
	case TAS2552_PLL_CLKIN_MCLK:
	case TAS2552_PLL_CLKIN_IVCLKIN:
		if (freq < 512000 || freq > 24576000) {
			/* out of range PLL_CLKIN, fall back to use BCLK */
			dev_warn(codec->dev, "Out of range PLL_CLKIN: %u\n",
				 freq);
			clk_id = TAS2552_PLL_CLKIN_BCLK;
			freq = 0;
		}
		/* fall through */
	case TAS2552_PLL_CLKIN_BCLK:
	case TAS2552_PLL_CLKIN_1_8_FIXED:
		mask = TAS2552_PLL_SRC_MASK;
		val = (clk_id << 3) & mask; /* bit 4:5 in the register */
		reg = TAS2552_CFG_1;
		tas2552->pll_clk_id = clk_id;
		tas2552->pll_clkin = freq;
		break;
	case TAS2552_PDM_CLK_PLL:
	case TAS2552_PDM_CLK_IVCLKIN:
	case TAS2552_PDM_CLK_BCLK:
	case TAS2552_PDM_CLK_MCLK:
		mask = TAS2552_PDM_CLK_SEL_MASK;
		val = (clk_id >> 1) & mask; /* bit 0:1 in the register */
		reg = TAS2552_PDM_CFG;
		tas2552->pdm_clk_id = clk_id;
		tas2552->pdm_clk = freq;
		break;
	default:
		dev_err(codec->dev, "Invalid clk id: %d\n", clk_id);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, reg, mask, val);

	return 0;
}

static int tas2552_set_dai_tdm_slot(struct snd_soc_dai *dai,
				    unsigned int tx_mask, unsigned int rx_mask,
				    int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2552_data *tas2552 = snd_soc_codec_get_drvdata(codec);
	unsigned int lsb;

	if (unlikely(!tx_mask)) {
		dev_err(codec->dev, "tx masks need to be non 0\n");
		return -EINVAL;
	}

	/* TDM based on DSP mode requires slots to be adjacent */
	lsb = __ffs(tx_mask);
	if ((lsb + 1) != __fls(tx_mask)) {
		dev_err(codec->dev, "Invalid mask, slots must be adjacent\n");
		return -EINVAL;
	}

	tas2552->tdm_delay = lsb * slot_width;

	/* DOUT in high-impedance on inactive bit clocks */
	snd_soc_update_bits(codec, TAS2552_DOUT,
			    TAS2552_SDOUT_TRISTATE, TAS2552_SDOUT_TRISTATE);

	return 0;
}

static int tas2552_mute(struct snd_soc_dai *dai, int mute)
{
	u8 cfg1_reg = 0;
	struct snd_soc_codec *codec = dai->codec;

	if (mute)
		cfg1_reg |= TAS2552_MUTE;

	snd_soc_update_bits(codec, TAS2552_CFG_1, TAS2552_MUTE, cfg1_reg);

	return 0;
}

#ifdef CONFIG_PM
static int tas2552_runtime_suspend(struct device *dev)
{
	struct tas2552_data *tas2552 = dev_get_drvdata(dev);

	tas2552_sw_shutdown(tas2552, 1);

	regcache_cache_only(tas2552->regmap, true);
	regcache_mark_dirty(tas2552->regmap);

	gpiod_set_value(tas2552->enable_gpio, 0);

	return 0;
}

static int tas2552_runtime_resume(struct device *dev)
{
	struct tas2552_data *tas2552 = dev_get_drvdata(dev);

	gpiod_set_value(tas2552->enable_gpio, 1);

	tas2552_sw_shutdown(tas2552, 0);

	regcache_cache_only(tas2552->regmap, false);
	regcache_sync(tas2552->regmap);

	return 0;
}
#endif

static const struct dev_pm_ops tas2552_pm = {
	SET_RUNTIME_PM_OPS(tas2552_runtime_suspend, tas2552_runtime_resume,
			   NULL)
};

static const struct snd_soc_dai_ops tas2552_speaker_dai_ops = {
	.hw_params	= tas2552_hw_params,
	.prepare	= tas2552_prepare,
	.set_sysclk	= tas2552_set_dai_sysclk,
	.set_fmt	= tas2552_set_dai_fmt,
	.set_tdm_slot	= tas2552_set_dai_tdm_slot,
	.digital_mute = tas2552_mute,
};

/* Formats supported by TAS2552 driver. */
#define TAS2552_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

/* TAS2552 dai structure. */
static struct snd_soc_dai_driver tas2552_dai[] = {
	{
		.name = "tas2552-amplifier",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = TAS2552_FORMATS,
		},
		.ops = &tas2552_speaker_dai_ops,
	},
};

/*
 * DAC digital volumes. From -7 to 24 dB in 1 dB steps
 */
static DECLARE_TLV_DB_SCALE(dac_tlv, -700, 100, 0);

static const char * const tas2552_din_source_select[] = {
	"Muted",
	"Left",
	"Right",
	"Left + Right average",
};
static SOC_ENUM_SINGLE_DECL(tas2552_din_source_enum,
			    TAS2552_CFG_3, 3,
			    tas2552_din_source_select);

static const struct snd_kcontrol_new tas2552_snd_controls[] = {
	SOC_SINGLE_TLV("Speaker Driver Playback Volume",
			 TAS2552_PGA_GAIN, 0, 0x1f, 0, dac_tlv),
	SOC_ENUM("DIN source", tas2552_din_source_enum),
};

static int tas2552_codec_probe(struct snd_soc_codec *codec)
{
	struct tas2552_data *tas2552 = snd_soc_codec_get_drvdata(codec);
	int ret;

	tas2552->codec = codec;

	ret = regulator_bulk_enable(ARRAY_SIZE(tas2552->supplies),
				    tas2552->supplies);

	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n",
			ret);
		return ret;
	}

	gpiod_set_value(tas2552->enable_gpio, 1);

	ret = pm_runtime_get_sync(codec->dev);
	if (ret < 0) {
		dev_err(codec->dev, "Enabling device failed: %d\n",
			ret);
		goto probe_fail;
	}

	snd_soc_update_bits(codec, TAS2552_CFG_1, TAS2552_MUTE, TAS2552_MUTE);
	snd_soc_write(codec, TAS2552_CFG_3, TAS2552_I2S_OUT_SEL |
					    TAS2552_DIN_SRC_SEL_AVG_L_R);
	snd_soc_write(codec, TAS2552_OUTPUT_DATA,
		      TAS2552_PDM_DATA_SEL_V_I |
		      TAS2552_R_DATA_OUT(TAS2552_DATA_OUT_V_DATA));
	snd_soc_write(codec, TAS2552_BOOST_APT_CTRL, TAS2552_APT_DELAY_200 |
						     TAS2552_APT_THRESH_20_17);

	snd_soc_write(codec, TAS2552_CFG_2, TAS2552_BOOST_EN | TAS2552_APT_EN |
					    TAS2552_LIM_EN);

	return 0;

probe_fail:
	gpiod_set_value(tas2552->enable_gpio, 0);

	regulator_bulk_disable(ARRAY_SIZE(tas2552->supplies),
					tas2552->supplies);
	return -EIO;
}

static int tas2552_codec_remove(struct snd_soc_codec *codec)
{
	struct tas2552_data *tas2552 = snd_soc_codec_get_drvdata(codec);

	pm_runtime_put(codec->dev);

	gpiod_set_value(tas2552->enable_gpio, 0);

	return 0;
};

#ifdef CONFIG_PM
static int tas2552_suspend(struct snd_soc_codec *codec)
{
	struct tas2552_data *tas2552 = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = regulator_bulk_disable(ARRAY_SIZE(tas2552->supplies),
					tas2552->supplies);

	if (ret != 0)
		dev_err(codec->dev, "Failed to disable supplies: %d\n",
			ret);
	return 0;
}

static int tas2552_resume(struct snd_soc_codec *codec)
{
	struct tas2552_data *tas2552 = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(tas2552->supplies),
				    tas2552->supplies);

	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n",
			ret);
	}

	return 0;
}
#else
#define tas2552_suspend NULL
#define tas2552_resume NULL
#endif

static struct snd_soc_codec_driver soc_codec_dev_tas2552 = {
	.probe = tas2552_codec_probe,
	.remove = tas2552_codec_remove,
	.suspend =	tas2552_suspend,
	.resume = tas2552_resume,
	.ignore_pmdown_time = true,

	.controls = tas2552_snd_controls,
	.num_controls = ARRAY_SIZE(tas2552_snd_controls),
	.dapm_widgets = tas2552_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tas2552_dapm_widgets),
	.dapm_routes = tas2552_audio_map,
	.num_dapm_routes = ARRAY_SIZE(tas2552_audio_map),
};

static const struct regmap_config tas2552_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = TAS2552_MAX_REG,
	.reg_defaults = tas2552_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(tas2552_reg_defs),
	.cache_type = REGCACHE_RBTREE,
};

static int tas2552_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct device *dev;
	struct tas2552_data *data;
	int ret;
	int i;

	dev = &client->dev;
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						    GPIOD_OUT_LOW);
	if (IS_ERR(data->enable_gpio))
		return PTR_ERR(data->enable_gpio);

	data->tas2552_client = client;
	data->regmap = devm_regmap_init_i2c(client, &tas2552_regmap_config);
	if (IS_ERR(data->regmap)) {
		ret = PTR_ERR(data->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(data->supplies); i++)
		data->supplies[i].supply = tas2552_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(data->supplies),
				      data->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	pm_runtime_set_active(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_mark_last_busy(&client->dev);
	pm_runtime_put_sync_autosuspend(&client->dev);

	dev_set_drvdata(&client->dev, data);

	ret = snd_soc_register_codec(&client->dev,
				      &soc_codec_dev_tas2552,
				      tas2552_dai, ARRAY_SIZE(tas2552_dai));
	if (ret < 0)
		dev_err(&client->dev, "Failed to register codec: %d\n", ret);

	return ret;
}

static int tas2552_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	pm_runtime_disable(&client->dev);
	return 0;
}

static const struct i2c_device_id tas2552_id[] = {
	{ "tas2552", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas2552_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tas2552_of_match[] = {
	{ .compatible = "ti,tas2552", },
	{},
};
MODULE_DEVICE_TABLE(of, tas2552_of_match);
#endif

static struct i2c_driver tas2552_i2c_driver = {
	.driver = {
		.name = "tas2552",
		.of_match_table = of_match_ptr(tas2552_of_match),
		.pm = &tas2552_pm,
	},
	.probe = tas2552_probe,
	.remove = tas2552_i2c_remove,
	.id_table = tas2552_id,
};

module_i2c_driver(tas2552_i2c_driver);

MODULE_AUTHOR("Dan Muprhy <dmurphy@ti.com>");
MODULE_DESCRIPTION("TAS2552 Audio amplifier driver");
MODULE_LICENSE("GPL");
