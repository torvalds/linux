/*
 * sgtl5000.c  --  SGTL5000 ALSA SoC Audio driver
 *
 * Copyright 2010-2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/tlv.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "sgtl5000.h"

#define SGTL5000_DAP_REG_OFFSET	0x0100
#define SGTL5000_MAX_REG_OFFSET	0x013A

/* default value of sgtl5000 registers */
static const u16 sgtl5000_regs[SGTL5000_MAX_REG_OFFSET] =  {
	[SGTL5000_CHIP_CLK_CTRL] = 0x0008,
	[SGTL5000_CHIP_I2S_CTRL] = 0x0010,
	[SGTL5000_CHIP_SSS_CTRL] = 0x0008,
	[SGTL5000_CHIP_DAC_VOL] = 0x3c3c,
	[SGTL5000_CHIP_PAD_STRENGTH] = 0x015f,
	[SGTL5000_CHIP_ANA_HP_CTRL] = 0x1818,
	[SGTL5000_CHIP_ANA_CTRL] = 0x0111,
	[SGTL5000_CHIP_LINE_OUT_VOL] = 0x0404,
	[SGTL5000_CHIP_ANA_POWER] = 0x7060,
	[SGTL5000_CHIP_PLL_CTRL] = 0x5000,
	[SGTL5000_DAP_BASS_ENHANCE] = 0x0040,
	[SGTL5000_DAP_BASS_ENHANCE_CTRL] = 0x051f,
	[SGTL5000_DAP_SURROUND] = 0x0040,
	[SGTL5000_DAP_EQ_BASS_BAND0] = 0x002f,
	[SGTL5000_DAP_EQ_BASS_BAND1] = 0x002f,
	[SGTL5000_DAP_EQ_BASS_BAND2] = 0x002f,
	[SGTL5000_DAP_EQ_BASS_BAND3] = 0x002f,
	[SGTL5000_DAP_EQ_BASS_BAND4] = 0x002f,
	[SGTL5000_DAP_MAIN_CHAN] = 0x8000,
	[SGTL5000_DAP_AVC_CTRL] = 0x0510,
	[SGTL5000_DAP_AVC_THRESHOLD] = 0x1473,
	[SGTL5000_DAP_AVC_ATTACK] = 0x0028,
	[SGTL5000_DAP_AVC_DECAY] = 0x0050,
};

/* regulator supplies for sgtl5000, VDDD is an optional external supply */
enum sgtl5000_regulator_supplies {
	VDDA,
	VDDIO,
	VDDD,
	SGTL5000_SUPPLY_NUM
};

/* vddd is optional supply */
static const char *supply_names[SGTL5000_SUPPLY_NUM] = {
	"VDDA",
	"VDDIO",
	"VDDD"
};

#define LDO_CONSUMER_NAME	"VDDD_LDO"
#define LDO_VOLTAGE		1200000

static struct regulator_consumer_supply ldo_consumer[] = {
	REGULATOR_SUPPLY(LDO_CONSUMER_NAME, NULL),
};

static struct regulator_init_data ldo_init_data = {
	.constraints = {
		.min_uV                 = 850000,
		.max_uV                 = 1600000,
		.valid_modes_mask       = REGULATOR_MODE_NORMAL,
		.valid_ops_mask         = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &ldo_consumer[0],
};

/*
 * sgtl5000 internal ldo regulator,
 * enabled when VDDD not provided
 */
struct ldo_regulator {
	struct regulator_desc desc;
	struct regulator_dev *dev;
	int voltage;
	void *codec_data;
	bool enabled;
};

/* sgtl5000 private structure in codec */
struct sgtl5000_priv {
	int sysclk;	/* sysclk rate */
	int master;	/* i2s master or not */
	int fmt;	/* i2s data format */
	struct regulator_bulk_data supplies[SGTL5000_SUPPLY_NUM];
	struct ldo_regulator *ldo;
};

/*
 * mic_bias power on/off share the same register bits with
 * output impedance of mic bias, when power on mic bias, we
 * need reclaim it to impedance value.
 * 0x0 = Powered off
 * 0x1 = 2Kohm
 * 0x2 = 4Kohm
 * 0x3 = 8Kohm
 */
static int mic_bias_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* change mic bias resistor to 4Kohm */
		snd_soc_update_bits(w->codec, SGTL5000_CHIP_MIC_CTRL,
				SGTL5000_BIAS_R_MASK,
				SGTL5000_BIAS_R_4k << SGTL5000_BIAS_R_SHIFT);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(w->codec, SGTL5000_CHIP_MIC_CTRL,
				SGTL5000_BIAS_R_MASK, 0);
		break;
	}
	return 0;
}

/*
 * using codec assist to small pop, hp_powerup or lineout_powerup
 * should stay setting until vag_powerup is fully ramped down,
 * vag fully ramped down require 400ms.
 */
static int small_pop_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(w->codec, SGTL5000_CHIP_ANA_POWER,
			SGTL5000_VAG_POWERUP, SGTL5000_VAG_POWERUP);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(w->codec, SGTL5000_CHIP_ANA_POWER,
			SGTL5000_VAG_POWERUP, 0);
		msleep(400);
		break;
	default:
		break;
	}

	return 0;
}

/* input sources for ADC */
static const char *adc_mux_text[] = {
	"MIC_IN", "LINE_IN"
};

static const struct soc_enum adc_enum =
SOC_ENUM_SINGLE(SGTL5000_CHIP_ANA_CTRL, 2, 2, adc_mux_text);

static const struct snd_kcontrol_new adc_mux =
SOC_DAPM_ENUM("Capture Mux", adc_enum);

/* input sources for DAC */
static const char *dac_mux_text[] = {
	"DAC", "LINE_IN"
};

static const struct soc_enum dac_enum =
SOC_ENUM_SINGLE(SGTL5000_CHIP_ANA_CTRL, 6, 2, dac_mux_text);

static const struct snd_kcontrol_new dac_mux =
SOC_DAPM_ENUM("Headphone Mux", dac_enum);

static const struct snd_soc_dapm_widget sgtl5000_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("LINE_IN"),
	SND_SOC_DAPM_INPUT("MIC_IN"),

	SND_SOC_DAPM_OUTPUT("HP_OUT"),
	SND_SOC_DAPM_OUTPUT("LINE_OUT"),

	SND_SOC_DAPM_MICBIAS_E("Mic Bias", SGTL5000_CHIP_MIC_CTRL, 8, 0,
				mic_bias_event,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_PGA_E("HP", SGTL5000_CHIP_ANA_POWER, 4, 0, NULL, 0,
			small_pop_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("LO", SGTL5000_CHIP_ANA_POWER, 0, 0, NULL, 0,
			small_pop_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MUX("Capture Mux", SND_SOC_NOPM, 0, 0, &adc_mux),
	SND_SOC_DAPM_MUX("Headphone Mux", SND_SOC_NOPM, 0, 0, &dac_mux),

	/* aif for i2s input */
	SND_SOC_DAPM_AIF_IN("AIFIN", "Playback",
				0, SGTL5000_CHIP_DIG_POWER,
				0, 0),

	/* aif for i2s output */
	SND_SOC_DAPM_AIF_OUT("AIFOUT", "Capture",
				0, SGTL5000_CHIP_DIG_POWER,
				1, 0),

	SND_SOC_DAPM_ADC("ADC", "Capture", SGTL5000_CHIP_ANA_POWER, 1, 0),

	SND_SOC_DAPM_DAC("DAC", "Playback", SGTL5000_CHIP_ANA_POWER, 3, 0),
};

/* routes for sgtl5000 */
static const struct snd_soc_dapm_route audio_map[] = {
	{"Capture Mux", "LINE_IN", "LINE_IN"},	/* line_in --> adc_mux */
	{"Capture Mux", "MIC_IN", "MIC_IN"},	/* mic_in --> adc_mux */

	{"ADC", NULL, "Capture Mux"},		/* adc_mux --> adc */
	{"AIFOUT", NULL, "ADC"},		/* adc --> i2s_out */

	{"DAC", NULL, "AIFIN"},			/* i2s-->dac,skip audio mux */
	{"Headphone Mux", "DAC", "DAC"},	/* dac --> hp_mux */
	{"LO", NULL, "DAC"},			/* dac --> line_out */

	{"Headphone Mux", "LINE_IN", "LINE_IN"},/* line_in --> hp_mux */
	{"HP", NULL, "Headphone Mux"},		/* hp_mux --> hp */

	{"LINE_OUT", NULL, "LO"},
	{"HP_OUT", NULL, "HP"},
};

/* custom function to fetch info of PCM playback volume */
static int dac_info_volsw(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xfc - 0x3c;
	return 0;
}

/*
 * custom function to get of PCM playback volume
 *
 * dac volume register
 * 15-------------8-7--------------0
 * | R channel vol | L channel vol |
 *  -------------------------------
 *
 * PCM volume with 0.5017 dB steps from 0 to -90 dB
 *
 * register values map to dB
 * 0x3B and less = Reserved
 * 0x3C = 0 dB
 * 0x3D = -0.5 dB
 * 0xF0 = -90 dB
 * 0xFC and greater = Muted
 *
 * register value map to userspace value
 *
 * register value	0x3c(0dB)	  0xf0(-90dB)0xfc
 *			------------------------------
 * userspace value	0xc0			     0
 */
static int dac_get_volsw(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int reg;
	int l;
	int r;

	reg = snd_soc_read(codec, SGTL5000_CHIP_DAC_VOL);

	/* get left channel volume */
	l = (reg & SGTL5000_DAC_VOL_LEFT_MASK) >> SGTL5000_DAC_VOL_LEFT_SHIFT;

	/* get right channel volume */
	r = (reg & SGTL5000_DAC_VOL_RIGHT_MASK) >> SGTL5000_DAC_VOL_RIGHT_SHIFT;

	/* make sure value fall in (0x3c,0xfc) */
	l = clamp(l, 0x3c, 0xfc);
	r = clamp(r, 0x3c, 0xfc);

	/* invert it and map to userspace value */
	l = 0xfc - l;
	r = 0xfc - r;

	ucontrol->value.integer.value[0] = l;
	ucontrol->value.integer.value[1] = r;

	return 0;
}

/*
 * custom function to put of PCM playback volume
 *
 * dac volume register
 * 15-------------8-7--------------0
 * | R channel vol | L channel vol |
 *  -------------------------------
 *
 * PCM volume with 0.5017 dB steps from 0 to -90 dB
 *
 * register values map to dB
 * 0x3B and less = Reserved
 * 0x3C = 0 dB
 * 0x3D = -0.5 dB
 * 0xF0 = -90 dB
 * 0xFC and greater = Muted
 *
 * userspace value map to register value
 *
 * userspace value	0xc0			     0
 *			------------------------------
 * register value	0x3c(0dB)	0xf0(-90dB)0xfc
 */
static int dac_put_volsw(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int reg;
	int l;
	int r;

	l = ucontrol->value.integer.value[0];
	r = ucontrol->value.integer.value[1];

	/* make sure userspace volume fall in (0, 0xfc-0x3c) */
	l = clamp(l, 0, 0xfc - 0x3c);
	r = clamp(r, 0, 0xfc - 0x3c);

	/* invert it, get the value can be set to register */
	l = 0xfc - l;
	r = 0xfc - r;

	/* shift to get the register value */
	reg = l << SGTL5000_DAC_VOL_LEFT_SHIFT |
		r << SGTL5000_DAC_VOL_RIGHT_SHIFT;

	snd_soc_write(codec, SGTL5000_CHIP_DAC_VOL, reg);

	return 0;
}

static const DECLARE_TLV_DB_SCALE(capture_6db_attenuate, -600, 600, 0);

/* tlv for mic gain, 0db 20db 30db 40db */
static const unsigned int mic_gain_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 3, TLV_DB_SCALE_ITEM(2000, 1000, 0),
};

/* tlv for hp volume, -51.5db to 12.0db, step .5db */
static const DECLARE_TLV_DB_SCALE(headphone_volume, -5150, 50, 0);

static const struct snd_kcontrol_new sgtl5000_snd_controls[] = {
	/* SOC_DOUBLE_S8_TLV with invert */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Volume",
		.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dac_info_volsw,
		.get = dac_get_volsw,
		.put = dac_put_volsw,
	},

	SOC_DOUBLE("Capture Volume", SGTL5000_CHIP_ANA_ADC_CTRL, 0, 4, 0xf, 0),
	SOC_SINGLE_TLV("Capture Attenuate Switch (-6dB)",
			SGTL5000_CHIP_ANA_ADC_CTRL,
			8, 2, 0, capture_6db_attenuate),
	SOC_SINGLE("Capture ZC Switch", SGTL5000_CHIP_ANA_CTRL, 1, 1, 0),

	SOC_DOUBLE_TLV("Headphone Playback Volume",
			SGTL5000_CHIP_ANA_HP_CTRL,
			0, 8,
			0x7f, 1,
			headphone_volume),
	SOC_SINGLE("Headphone Playback ZC Switch", SGTL5000_CHIP_ANA_CTRL,
			5, 1, 0),

	SOC_SINGLE_TLV("Mic Volume", SGTL5000_CHIP_MIC_CTRL,
			0, 4, 0, mic_gain_tlv),
};

/* mute the codec used by alsa core */
static int sgtl5000_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 adcdac_ctrl = SGTL5000_DAC_MUTE_LEFT | SGTL5000_DAC_MUTE_RIGHT;

	snd_soc_update_bits(codec, SGTL5000_CHIP_ADCDAC_CTRL,
			adcdac_ctrl, mute ? adcdac_ctrl : 0);

	return 0;
}

/* set codec format */
static int sgtl5000_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);
	u16 i2sctl = 0;

	sgtl5000->master = 0;
	/*
	 * i2s clock and frame master setting.
	 * ONLY support:
	 *  - clock and frame slave,
	 *  - clock and frame master
	 */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		i2sctl |= SGTL5000_I2S_MASTER;
		sgtl5000->master = 1;
		break;
	default:
		return -EINVAL;
	}

	/* setting i2s data format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		i2sctl |= SGTL5000_I2S_MODE_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		i2sctl |= SGTL5000_I2S_MODE_PCM;
		i2sctl |= SGTL5000_I2S_LRALIGN;
		break;
	case SND_SOC_DAIFMT_I2S:
		i2sctl |= SGTL5000_I2S_MODE_I2S_LJ;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		i2sctl |= SGTL5000_I2S_MODE_RJ;
		i2sctl |= SGTL5000_I2S_LRPOL;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		i2sctl |= SGTL5000_I2S_MODE_I2S_LJ;
		i2sctl |= SGTL5000_I2S_LRALIGN;
		break;
	default:
		return -EINVAL;
	}

	sgtl5000->fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		i2sctl |= SGTL5000_I2S_SCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, SGTL5000_CHIP_I2S_CTRL, i2sctl);

	return 0;
}

/* set codec sysclk */
static int sgtl5000_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				   int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);

	switch (clk_id) {
	case SGTL5000_SYSCLK:
		sgtl5000->sysclk = freq;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * set clock according to i2s frame clock,
 * sgtl5000 provide 2 clock sources.
 * 1. sys_mclk. sample freq can only configure to
 *	1/256, 1/384, 1/512 of sys_mclk.
 * 2. pll. can derive any audio clocks.
 *
 * clock setting rules:
 * 1. in slave mode, only sys_mclk can use.
 * 2. as constraint by sys_mclk, sample freq should
 *	set to 32k, 44.1k and above.
 * 3. using sys_mclk prefer to pll to save power.
 */
static int sgtl5000_set_clock(struct snd_soc_codec *codec, int frame_rate)
{
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);
	int clk_ctl = 0;
	int sys_fs;	/* sample freq */

	/*
	 * sample freq should be divided by frame clock,
	 * if frame clock lower than 44.1khz, sample feq should set to
	 * 32khz or 44.1khz.
	 */
	switch (frame_rate) {
	case 8000:
	case 16000:
		sys_fs = 32000;
		break;
	case 11025:
	case 22050:
		sys_fs = 44100;
		break;
	default:
		sys_fs = frame_rate;
		break;
	}

	/* set divided factor of frame clock */
	switch (sys_fs / frame_rate) {
	case 4:
		clk_ctl |= SGTL5000_RATE_MODE_DIV_4 << SGTL5000_RATE_MODE_SHIFT;
		break;
	case 2:
		clk_ctl |= SGTL5000_RATE_MODE_DIV_2 << SGTL5000_RATE_MODE_SHIFT;
		break;
	case 1:
		clk_ctl |= SGTL5000_RATE_MODE_DIV_1 << SGTL5000_RATE_MODE_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	/* set the sys_fs according to frame rate */
	switch (sys_fs) {
	case 32000:
		clk_ctl |= SGTL5000_SYS_FS_32k << SGTL5000_SYS_FS_SHIFT;
		break;
	case 44100:
		clk_ctl |= SGTL5000_SYS_FS_44_1k << SGTL5000_SYS_FS_SHIFT;
		break;
	case 48000:
		clk_ctl |= SGTL5000_SYS_FS_48k << SGTL5000_SYS_FS_SHIFT;
		break;
	case 96000:
		clk_ctl |= SGTL5000_SYS_FS_96k << SGTL5000_SYS_FS_SHIFT;
		break;
	default:
		dev_err(codec->dev, "frame rate %d not supported\n",
			frame_rate);
		return -EINVAL;
	}

	/*
	 * calculate the divider of mclk/sample_freq,
	 * factor of freq =96k can only be 256, since mclk in range (12m,27m)
	 */
	switch (sgtl5000->sysclk / sys_fs) {
	case 256:
		clk_ctl |= SGTL5000_MCLK_FREQ_256FS <<
			SGTL5000_MCLK_FREQ_SHIFT;
		break;
	case 384:
		clk_ctl |= SGTL5000_MCLK_FREQ_384FS <<
			SGTL5000_MCLK_FREQ_SHIFT;
		break;
	case 512:
		clk_ctl |= SGTL5000_MCLK_FREQ_512FS <<
			SGTL5000_MCLK_FREQ_SHIFT;
		break;
	default:
		/* if mclk not satisify the divider, use pll */
		if (sgtl5000->master) {
			clk_ctl |= SGTL5000_MCLK_FREQ_PLL <<
				SGTL5000_MCLK_FREQ_SHIFT;
		} else {
			dev_err(codec->dev,
				"PLL not supported in slave mode\n");
			return -EINVAL;
		}
	}

	/* if using pll, please check manual 6.4.2 for detail */
	if ((clk_ctl & SGTL5000_MCLK_FREQ_MASK) == SGTL5000_MCLK_FREQ_PLL) {
		u64 out, t;
		int div2;
		int pll_ctl;
		unsigned int in, int_div, frac_div;

		if (sgtl5000->sysclk > 17000000) {
			div2 = 1;
			in = sgtl5000->sysclk / 2;
		} else {
			div2 = 0;
			in = sgtl5000->sysclk;
		}
		if (sys_fs == 44100)
			out = 180633600;
		else
			out = 196608000;
		t = do_div(out, in);
		int_div = out;
		t *= 2048;
		do_div(t, in);
		frac_div = t;
		pll_ctl = int_div << SGTL5000_PLL_INT_DIV_SHIFT |
		    frac_div << SGTL5000_PLL_FRAC_DIV_SHIFT;

		snd_soc_write(codec, SGTL5000_CHIP_PLL_CTRL, pll_ctl);
		if (div2)
			snd_soc_update_bits(codec,
				SGTL5000_CHIP_CLK_TOP_CTRL,
				SGTL5000_INPUT_FREQ_DIV2,
				SGTL5000_INPUT_FREQ_DIV2);
		else
			snd_soc_update_bits(codec,
				SGTL5000_CHIP_CLK_TOP_CTRL,
				SGTL5000_INPUT_FREQ_DIV2,
				0);

		/* power up pll */
		snd_soc_update_bits(codec, SGTL5000_CHIP_ANA_POWER,
			SGTL5000_PLL_POWERUP | SGTL5000_VCOAMP_POWERUP,
			SGTL5000_PLL_POWERUP | SGTL5000_VCOAMP_POWERUP);
	} else {
		/* power down pll */
		snd_soc_update_bits(codec, SGTL5000_CHIP_ANA_POWER,
			SGTL5000_PLL_POWERUP | SGTL5000_VCOAMP_POWERUP,
			0);
	}

	/* if using pll, clk_ctrl must be set after pll power up */
	snd_soc_write(codec, SGTL5000_CHIP_CLK_CTRL, clk_ctl);

	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 * input: params_rate, params_fmt
 */
static int sgtl5000_pcm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);
	int channels = params_channels(params);
	int i2s_ctl = 0;
	int stereo;
	int ret;

	/* sysclk should already set */
	if (!sgtl5000->sysclk) {
		dev_err(codec->dev, "%s: set sysclk first!\n", __func__);
		return -EFAULT;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		stereo = SGTL5000_DAC_STEREO;
	else
		stereo = SGTL5000_ADC_STEREO;

	/* set mono to save power */
	snd_soc_update_bits(codec, SGTL5000_CHIP_ANA_POWER, stereo,
			channels == 1 ? 0 : stereo);

	/* set codec clock base on lrclk */
	ret = sgtl5000_set_clock(codec, params_rate(params));
	if (ret)
		return ret;

	/* set i2s data format */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		if (sgtl5000->fmt == SND_SOC_DAIFMT_RIGHT_J)
			return -EINVAL;
		i2s_ctl |= SGTL5000_I2S_DLEN_16 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_32FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		i2s_ctl |= SGTL5000_I2S_DLEN_20 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_64FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		i2s_ctl |= SGTL5000_I2S_DLEN_24 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_64FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		if (sgtl5000->fmt == SND_SOC_DAIFMT_RIGHT_J)
			return -EINVAL;
		i2s_ctl |= SGTL5000_I2S_DLEN_32 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_64FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, SGTL5000_CHIP_I2S_CTRL,
			    SGTL5000_I2S_DLEN_MASK | SGTL5000_I2S_SCLKFREQ_MASK,
			    i2s_ctl);

	return 0;
}

#ifdef CONFIG_REGULATOR
static int ldo_regulator_is_enabled(struct regulator_dev *dev)
{
	struct ldo_regulator *ldo = rdev_get_drvdata(dev);

	return ldo->enabled;
}

static int ldo_regulator_enable(struct regulator_dev *dev)
{
	struct ldo_regulator *ldo = rdev_get_drvdata(dev);
	struct snd_soc_codec *codec = (struct snd_soc_codec *)ldo->codec_data;
	int reg;

	if (ldo_regulator_is_enabled(dev))
		return 0;

	/* set regulator value firstly */
	reg = (1600 - ldo->voltage / 1000) / 50;
	reg = clamp(reg, 0x0, 0xf);

	/* amend the voltage value, unit: uV */
	ldo->voltage = (1600 - reg * 50) * 1000;

	/* set voltage to register */
	snd_soc_update_bits(codec, SGTL5000_CHIP_LINREG_CTRL,
				SGTL5000_LINREG_VDDD_MASK, reg);

	snd_soc_update_bits(codec, SGTL5000_CHIP_ANA_POWER,
				SGTL5000_LINEREG_D_POWERUP,
				SGTL5000_LINEREG_D_POWERUP);

	/* when internal ldo enabled, simple digital power can be disabled */
	snd_soc_update_bits(codec, SGTL5000_CHIP_ANA_POWER,
				SGTL5000_LINREG_SIMPLE_POWERUP,
				0);

	ldo->enabled = 1;
	return 0;
}

static int ldo_regulator_disable(struct regulator_dev *dev)
{
	struct ldo_regulator *ldo = rdev_get_drvdata(dev);
	struct snd_soc_codec *codec = (struct snd_soc_codec *)ldo->codec_data;

	snd_soc_update_bits(codec, SGTL5000_CHIP_ANA_POWER,
				SGTL5000_LINEREG_D_POWERUP,
				0);

	/* clear voltage info */
	snd_soc_update_bits(codec, SGTL5000_CHIP_LINREG_CTRL,
				SGTL5000_LINREG_VDDD_MASK, 0);

	ldo->enabled = 0;

	return 0;
}

static int ldo_regulator_get_voltage(struct regulator_dev *dev)
{
	struct ldo_regulator *ldo = rdev_get_drvdata(dev);

	return ldo->voltage;
}

static struct regulator_ops ldo_regulator_ops = {
	.is_enabled = ldo_regulator_is_enabled,
	.enable = ldo_regulator_enable,
	.disable = ldo_regulator_disable,
	.get_voltage = ldo_regulator_get_voltage,
};

static int ldo_regulator_register(struct snd_soc_codec *codec,
				struct regulator_init_data *init_data,
				int voltage)
{
	struct ldo_regulator *ldo;
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);

	ldo = kzalloc(sizeof(struct ldo_regulator), GFP_KERNEL);

	if (!ldo) {
		dev_err(codec->dev, "failed to allocate ldo_regulator\n");
		return -ENOMEM;
	}

	ldo->desc.name = kstrdup(dev_name(codec->dev), GFP_KERNEL);
	if (!ldo->desc.name) {
		kfree(ldo);
		dev_err(codec->dev, "failed to allocate decs name memory\n");
		return -ENOMEM;
	}

	ldo->desc.type  = REGULATOR_VOLTAGE;
	ldo->desc.owner = THIS_MODULE;
	ldo->desc.ops   = &ldo_regulator_ops;
	ldo->desc.n_voltages = 1;

	ldo->codec_data = codec;
	ldo->voltage = voltage;

	ldo->dev = regulator_register(&ldo->desc, codec->dev,
					  init_data, ldo, NULL);
	if (IS_ERR(ldo->dev)) {
		int ret = PTR_ERR(ldo->dev);

		dev_err(codec->dev, "failed to register regulator\n");
		kfree(ldo->desc.name);
		kfree(ldo);

		return ret;
	}
	sgtl5000->ldo = ldo;

	return 0;
}

static int ldo_regulator_remove(struct snd_soc_codec *codec)
{
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);
	struct ldo_regulator *ldo = sgtl5000->ldo;

	if (!ldo)
		return 0;

	regulator_unregister(ldo->dev);
	kfree(ldo->desc.name);
	kfree(ldo);

	return 0;
}
#else
static int ldo_regulator_register(struct snd_soc_codec *codec,
				struct regulator_init_data *init_data,
				int voltage)
{
	dev_err(codec->dev, "this setup needs regulator support in the kernel\n");
	return -EINVAL;
}

static int ldo_regulator_remove(struct snd_soc_codec *codec)
{
	return 0;
}
#endif

/*
 * set dac bias
 * common state changes:
 * startup:
 * off --> standby --> prepare --> on
 * standby --> prepare --> on
 *
 * stop:
 * on --> prepare --> standby
 */
static int sgtl5000_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level)
{
	int ret;
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(
						ARRAY_SIZE(sgtl5000->supplies),
						sgtl5000->supplies);
			if (ret)
				return ret;
			udelay(10);
		}

		break;
	case SND_SOC_BIAS_OFF:
		regulator_bulk_disable(ARRAY_SIZE(sgtl5000->supplies),
					sgtl5000->supplies);
		break;
	}

	codec->dapm.bias_level = level;
	return 0;
}

#define SGTL5000_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops sgtl5000_ops = {
	.hw_params = sgtl5000_pcm_hw_params,
	.digital_mute = sgtl5000_digital_mute,
	.set_fmt = sgtl5000_set_dai_fmt,
	.set_sysclk = sgtl5000_set_dai_sysclk,
};

static struct snd_soc_dai_driver sgtl5000_dai = {
	.name = "sgtl5000",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		/*
		 * only support 8~48K + 96K,
		 * TODO modify hw_param to support more
		 */
		.rates = SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_96000,
		.formats = SGTL5000_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_96000,
		.formats = SGTL5000_FORMATS,
	},
	.ops = &sgtl5000_ops,
	.symmetric_rates = 1,
};

static int sgtl5000_volatile_register(struct snd_soc_codec *codec,
					unsigned int reg)
{
	switch (reg) {
	case SGTL5000_CHIP_ID:
	case SGTL5000_CHIP_ADCDAC_CTRL:
	case SGTL5000_CHIP_ANA_STATUS:
		return 1;
	}

	return 0;
}

#ifdef CONFIG_SUSPEND
static int sgtl5000_suspend(struct snd_soc_codec *codec)
{
	sgtl5000_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

/*
 * restore all sgtl5000 registers,
 * since a big hole between dap and regular registers,
 * we will restore them respectively.
 */
static int sgtl5000_restore_regs(struct snd_soc_codec *codec)
{
	u16 *cache = codec->reg_cache;
	u16 reg;

	/* restore regular registers */
	for (reg = 0; reg <= SGTL5000_CHIP_SHORT_CTRL; reg += 2) {

		/* this regs depends on the others */
		if (reg == SGTL5000_CHIP_ANA_POWER ||
			reg == SGTL5000_CHIP_CLK_CTRL ||
			reg == SGTL5000_CHIP_LINREG_CTRL ||
			reg == SGTL5000_CHIP_LINE_OUT_CTRL ||
			reg == SGTL5000_CHIP_CLK_CTRL)
			continue;

		snd_soc_write(codec, reg, cache[reg]);
	}

	/* restore dap registers */
	for (reg = SGTL5000_DAP_REG_OFFSET; reg < SGTL5000_MAX_REG_OFFSET; reg += 2)
		snd_soc_write(codec, reg, cache[reg]);

	/*
	 * restore power and other regs according
	 * to set_power() and set_clock()
	 */
	snd_soc_write(codec, SGTL5000_CHIP_LINREG_CTRL,
			cache[SGTL5000_CHIP_LINREG_CTRL]);

	snd_soc_write(codec, SGTL5000_CHIP_ANA_POWER,
			cache[SGTL5000_CHIP_ANA_POWER]);

	snd_soc_write(codec, SGTL5000_CHIP_CLK_CTRL,
			cache[SGTL5000_CHIP_CLK_CTRL]);

	snd_soc_write(codec, SGTL5000_CHIP_REF_CTRL,
			cache[SGTL5000_CHIP_REF_CTRL]);

	snd_soc_write(codec, SGTL5000_CHIP_LINE_OUT_CTRL,
			cache[SGTL5000_CHIP_LINE_OUT_CTRL]);
	return 0;
}

static int sgtl5000_resume(struct snd_soc_codec *codec)
{
	/* Bring the codec back up to standby to enable regulators */
	sgtl5000_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Restore registers by cached in memory */
	sgtl5000_restore_regs(codec);
	return 0;
}
#else
#define sgtl5000_suspend NULL
#define sgtl5000_resume  NULL
#endif	/* CONFIG_SUSPEND */

/*
 * sgtl5000 has 3 internal power supplies:
 * 1. VAG, normally set to vdda/2
 * 2. chargepump, set to different value
 *	according to voltage of vdda and vddio
 * 3. line out VAG, normally set to vddio/2
 *
 * and should be set according to:
 * 1. vddd provided by external or not
 * 2. vdda and vddio voltage value. > 3.1v or not
 * 3. chip revision >=0x11 or not. If >=0x11, not use external vddd.
 */
static int sgtl5000_set_power_regs(struct snd_soc_codec *codec)
{
	int vddd;
	int vdda;
	int vddio;
	u16 ana_pwr;
	u16 lreg_ctrl;
	int vag;
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);

	vdda  = regulator_get_voltage(sgtl5000->supplies[VDDA].consumer);
	vddio = regulator_get_voltage(sgtl5000->supplies[VDDIO].consumer);
	vddd  = regulator_get_voltage(sgtl5000->supplies[VDDD].consumer);

	vdda  = vdda / 1000;
	vddio = vddio / 1000;
	vddd  = vddd / 1000;

	if (vdda <= 0 || vddio <= 0 || vddd < 0) {
		dev_err(codec->dev, "regulator voltage not set correctly\n");

		return -EINVAL;
	}

	/* according to datasheet, maximum voltage of supplies */
	if (vdda > 3600 || vddio > 3600 || vddd > 1980) {
		dev_err(codec->dev,
			"exceed max voltage vdda %dmV vddio %dmV vddd %dmV\n",
			vdda, vddio, vddd);

		return -EINVAL;
	}

	/* reset value */
	ana_pwr = snd_soc_read(codec, SGTL5000_CHIP_ANA_POWER);
	ana_pwr |= SGTL5000_DAC_STEREO |
			SGTL5000_ADC_STEREO |
			SGTL5000_REFTOP_POWERUP;
	lreg_ctrl = snd_soc_read(codec, SGTL5000_CHIP_LINREG_CTRL);

	if (vddio < 3100 && vdda < 3100) {
		/* enable internal oscillator used for charge pump */
		snd_soc_update_bits(codec, SGTL5000_CHIP_CLK_TOP_CTRL,
					SGTL5000_INT_OSC_EN,
					SGTL5000_INT_OSC_EN);
		/* Enable VDDC charge pump */
		ana_pwr |= SGTL5000_VDDC_CHRGPMP_POWERUP;
	} else if (vddio >= 3100 && vdda >= 3100) {
		/*
		 * if vddio and vddd > 3.1v,
		 * charge pump should be clean before set ana_pwr
		 */
		snd_soc_update_bits(codec, SGTL5000_CHIP_ANA_POWER,
				SGTL5000_VDDC_CHRGPMP_POWERUP, 0);

		/* VDDC use VDDIO rail */
		lreg_ctrl |= SGTL5000_VDDC_ASSN_OVRD;
		lreg_ctrl |= SGTL5000_VDDC_MAN_ASSN_VDDIO <<
			    SGTL5000_VDDC_MAN_ASSN_SHIFT;
	}

	snd_soc_write(codec, SGTL5000_CHIP_LINREG_CTRL, lreg_ctrl);

	snd_soc_write(codec, SGTL5000_CHIP_ANA_POWER, ana_pwr);

	/* set voltage to register */
	snd_soc_update_bits(codec, SGTL5000_CHIP_LINREG_CTRL,
				SGTL5000_LINREG_VDDD_MASK, 0x8);

	/*
	 * if vddd linear reg has been enabled,
	 * simple digital supply should be clear to get
	 * proper VDDD voltage.
	 */
	if (ana_pwr & SGTL5000_LINEREG_D_POWERUP)
		snd_soc_update_bits(codec, SGTL5000_CHIP_ANA_POWER,
				SGTL5000_LINREG_SIMPLE_POWERUP,
				0);
	else
		snd_soc_update_bits(codec, SGTL5000_CHIP_ANA_POWER,
				SGTL5000_LINREG_SIMPLE_POWERUP |
				SGTL5000_STARTUP_POWERUP,
				0);

	/*
	 * set ADC/DAC VAG to vdda / 2,
	 * should stay in range (0.8v, 1.575v)
	 */
	vag = vdda / 2;
	if (vag <= SGTL5000_ANA_GND_BASE)
		vag = 0;
	else if (vag >= SGTL5000_ANA_GND_BASE + SGTL5000_ANA_GND_STP *
		 (SGTL5000_ANA_GND_MASK >> SGTL5000_ANA_GND_SHIFT))
		vag = SGTL5000_ANA_GND_MASK >> SGTL5000_ANA_GND_SHIFT;
	else
		vag = (vag - SGTL5000_ANA_GND_BASE) / SGTL5000_ANA_GND_STP;

	snd_soc_update_bits(codec, SGTL5000_CHIP_REF_CTRL,
			SGTL5000_ANA_GND_MASK, vag << SGTL5000_ANA_GND_SHIFT);

	/* set line out VAG to vddio / 2, in range (0.8v, 1.675v) */
	vag = vddio / 2;
	if (vag <= SGTL5000_LINE_OUT_GND_BASE)
		vag = 0;
	else if (vag >= SGTL5000_LINE_OUT_GND_BASE +
		SGTL5000_LINE_OUT_GND_STP * SGTL5000_LINE_OUT_GND_MAX)
		vag = SGTL5000_LINE_OUT_GND_MAX;
	else
		vag = (vag - SGTL5000_LINE_OUT_GND_BASE) /
		    SGTL5000_LINE_OUT_GND_STP;

	snd_soc_update_bits(codec, SGTL5000_CHIP_LINE_OUT_CTRL,
			SGTL5000_LINE_OUT_CURRENT_MASK |
			SGTL5000_LINE_OUT_GND_MASK,
			vag << SGTL5000_LINE_OUT_GND_SHIFT |
			SGTL5000_LINE_OUT_CURRENT_360u <<
				SGTL5000_LINE_OUT_CURRENT_SHIFT);

	return 0;
}

static int sgtl5000_replace_vddd_with_ldo(struct snd_soc_codec *codec)
{
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);
	int ret;

	/* set internal ldo to 1.2v */
	ret = ldo_regulator_register(codec, &ldo_init_data, LDO_VOLTAGE);
	if (ret) {
		dev_err(codec->dev,
			"Failed to register vddd internal supplies: %d\n", ret);
		return ret;
	}

	sgtl5000->supplies[VDDD].supply = LDO_CONSUMER_NAME;

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(sgtl5000->supplies),
			sgtl5000->supplies);

	if (ret) {
		ldo_regulator_remove(codec);
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	dev_info(codec->dev, "Using internal LDO instead of VDDD\n");
	return 0;
}

static int sgtl5000_enable_regulators(struct snd_soc_codec *codec)
{
	u16 reg;
	int ret;
	int rev;
	int i;
	int external_vddd = 0;
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);

	for (i = 0; i < ARRAY_SIZE(sgtl5000->supplies); i++)
		sgtl5000->supplies[i].supply = supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(sgtl5000->supplies),
				sgtl5000->supplies);
	if (!ret)
		external_vddd = 1;
	else {
		ret = sgtl5000_replace_vddd_with_ldo(codec);
		if (ret)
			return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(sgtl5000->supplies),
					sgtl5000->supplies);
	if (ret)
		goto err_regulator_free;

	/* wait for all power rails bring up */
	udelay(10);

	/* read chip information */
	reg = snd_soc_read(codec, SGTL5000_CHIP_ID);
	if (((reg & SGTL5000_PARTID_MASK) >> SGTL5000_PARTID_SHIFT) !=
	    SGTL5000_PARTID_PART_ID) {
		dev_err(codec->dev,
			"Device with ID register %x is not a sgtl5000\n", reg);
		ret = -ENODEV;
		goto err_regulator_disable;
	}

	rev = (reg & SGTL5000_REVID_MASK) >> SGTL5000_REVID_SHIFT;
	dev_info(codec->dev, "sgtl5000 revision %d\n", rev);

	/*
	 * workaround for revision 0x11 and later,
	 * roll back to use internal LDO
	 */
	if (external_vddd && rev >= 0x11) {
		/* disable all regulator first */
		regulator_bulk_disable(ARRAY_SIZE(sgtl5000->supplies),
					sgtl5000->supplies);
		/* free VDDD regulator */
		regulator_bulk_free(ARRAY_SIZE(sgtl5000->supplies),
					sgtl5000->supplies);

		ret = sgtl5000_replace_vddd_with_ldo(codec);
		if (ret)
			return ret;

		ret = regulator_bulk_enable(ARRAY_SIZE(sgtl5000->supplies),
						sgtl5000->supplies);
		if (ret)
			goto err_regulator_free;

		/* wait for all power rails bring up */
		udelay(10);
	}

	return 0;

err_regulator_disable:
	regulator_bulk_disable(ARRAY_SIZE(sgtl5000->supplies),
				sgtl5000->supplies);
err_regulator_free:
	regulator_bulk_free(ARRAY_SIZE(sgtl5000->supplies),
				sgtl5000->supplies);
	if (external_vddd)
		ldo_regulator_remove(codec);
	return ret;

}

static int sgtl5000_probe(struct snd_soc_codec *codec)
{
	int ret;
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);

	/* setup i2c data ops */
	ret = snd_soc_codec_set_cache_io(codec, 16, 16, SND_SOC_I2C);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	ret = sgtl5000_enable_regulators(codec);
	if (ret)
		return ret;

	/* power up sgtl5000 */
	ret = sgtl5000_set_power_regs(codec);
	if (ret)
		goto err;

	/* enable small pop, introduce 400ms delay in turning off */
	snd_soc_update_bits(codec, SGTL5000_CHIP_REF_CTRL,
				SGTL5000_SMALL_POP,
				SGTL5000_SMALL_POP);

	/* disable short cut detector */
	snd_soc_write(codec, SGTL5000_CHIP_SHORT_CTRL, 0);

	/*
	 * set i2s as default input of sound switch
	 * TODO: add sound switch to control and dapm widge.
	 */
	snd_soc_write(codec, SGTL5000_CHIP_SSS_CTRL,
			SGTL5000_DAC_SEL_I2S_IN << SGTL5000_DAC_SEL_SHIFT);
	snd_soc_write(codec, SGTL5000_CHIP_DIG_POWER,
			SGTL5000_ADC_EN | SGTL5000_DAC_EN);

	/* enable dac volume ramp by default */
	snd_soc_write(codec, SGTL5000_CHIP_ADCDAC_CTRL,
			SGTL5000_DAC_VOL_RAMP_EN |
			SGTL5000_DAC_MUTE_RIGHT |
			SGTL5000_DAC_MUTE_LEFT);

	snd_soc_write(codec, SGTL5000_CHIP_PAD_STRENGTH, 0x015f);

	snd_soc_write(codec, SGTL5000_CHIP_ANA_CTRL,
			SGTL5000_HP_ZCD_EN |
			SGTL5000_ADC_ZCD_EN);

	snd_soc_write(codec, SGTL5000_CHIP_MIC_CTRL, 0);

	/*
	 * disable DAP
	 * TODO:
	 * Enable DAP in kcontrol and dapm.
	 */
	snd_soc_write(codec, SGTL5000_DAP_CTRL, 0);

	/* leading to standby state */
	ret = sgtl5000_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	if (ret)
		goto err;

	snd_soc_add_controls(codec, sgtl5000_snd_controls,
			     ARRAY_SIZE(sgtl5000_snd_controls));

	snd_soc_dapm_new_controls(&codec->dapm, sgtl5000_dapm_widgets,
				  ARRAY_SIZE(sgtl5000_dapm_widgets));

	snd_soc_dapm_add_routes(&codec->dapm, audio_map,
				ARRAY_SIZE(audio_map));

	snd_soc_dapm_new_widgets(&codec->dapm);

	return 0;

err:
	regulator_bulk_disable(ARRAY_SIZE(sgtl5000->supplies),
						sgtl5000->supplies);
	regulator_bulk_free(ARRAY_SIZE(sgtl5000->supplies),
				sgtl5000->supplies);
	ldo_regulator_remove(codec);

	return ret;
}

static int sgtl5000_remove(struct snd_soc_codec *codec)
{
	struct sgtl5000_priv *sgtl5000 = snd_soc_codec_get_drvdata(codec);

	sgtl5000_set_bias_level(codec, SND_SOC_BIAS_OFF);

	regulator_bulk_disable(ARRAY_SIZE(sgtl5000->supplies),
						sgtl5000->supplies);
	regulator_bulk_free(ARRAY_SIZE(sgtl5000->supplies),
				sgtl5000->supplies);
	ldo_regulator_remove(codec);

	return 0;
}

static struct snd_soc_codec_driver sgtl5000_driver = {
	.probe = sgtl5000_probe,
	.remove = sgtl5000_remove,
	.suspend = sgtl5000_suspend,
	.resume = sgtl5000_resume,
	.set_bias_level = sgtl5000_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(sgtl5000_regs),
	.reg_word_size = sizeof(u16),
	.reg_cache_step = 2,
	.reg_cache_default = sgtl5000_regs,
	.volatile_register = sgtl5000_volatile_register,
};

static __devinit int sgtl5000_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct sgtl5000_priv *sgtl5000;
	int ret;

	sgtl5000 = devm_kzalloc(&client->dev, sizeof(struct sgtl5000_priv),
								GFP_KERNEL);
	if (!sgtl5000)
		return -ENOMEM;

	i2c_set_clientdata(client, sgtl5000);

	ret = snd_soc_register_codec(&client->dev,
			&sgtl5000_driver, &sgtl5000_dai, 1);
	return ret;
}

static __devexit int sgtl5000_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);

	return 0;
}

static const struct i2c_device_id sgtl5000_id[] = {
	{"sgtl5000", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, sgtl5000_id);

static const struct of_device_id sgtl5000_dt_ids[] = {
	{ .compatible = "fsl,sgtl5000", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sgtl5000_dt_ids);

static struct i2c_driver sgtl5000_i2c_driver = {
	.driver = {
		   .name = "sgtl5000",
		   .owner = THIS_MODULE,
		   .of_match_table = sgtl5000_dt_ids,
		   },
	.probe = sgtl5000_i2c_probe,
	.remove = __devexit_p(sgtl5000_i2c_remove),
	.id_table = sgtl5000_id,
};

static int __init sgtl5000_modinit(void)
{
	return i2c_add_driver(&sgtl5000_i2c_driver);
}
module_init(sgtl5000_modinit);

static void __exit sgtl5000_exit(void)
{
	i2c_del_driver(&sgtl5000_i2c_driver);
}
module_exit(sgtl5000_exit);

MODULE_DESCRIPTION("Freescale SGTL5000 ALSA SoC Codec Driver");
MODULE_AUTHOR("Zeng Zhaoming <zengzm.kernel@gmail.com>");
MODULE_LICENSE("GPL");
