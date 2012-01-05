/*
 * uda1380.c - Philips UDA1380 ALSA SoC audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (c) 2007-2009 Philipp Zabel <philipp.zabel@gmail.com>
 *
 * Modified by Richard Purdie <richard@openedhand.com> to fit into SoC
 * codec model.
 *
 * Copyright (c) 2005 Giorgio Padrin <giorgio@mandarinlogiq.org>
 * Copyright 2005 Openedhand Ltd.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/uda1380.h>

#include "uda1380.h"

/* codec private data */
struct uda1380_priv {
	struct snd_soc_codec *codec;
	unsigned int dac_clk;
	struct work_struct work;
	void *control_data;
};

/*
 * uda1380 register cache
 */
static const u16 uda1380_reg[UDA1380_CACHEREGNUM] = {
	0x0502, 0x0000, 0x0000, 0x3f3f,
	0x0202, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0xff00, 0x0000, 0x4800,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x8000, 0x0002, 0x0000,
};

static unsigned long uda1380_cache_dirty;

/*
 * read uda1380 register cache
 */
static inline unsigned int uda1380_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg == UDA1380_RESET)
		return 0;
	if (reg >= UDA1380_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write uda1380 register cache
 */
static inline void uda1380_write_reg_cache(struct snd_soc_codec *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;

	if (reg >= UDA1380_CACHEREGNUM)
		return;
	if ((reg >= 0x10) && (cache[reg] != value))
		set_bit(reg - 0x10, &uda1380_cache_dirty);
	cache[reg] = value;
}

/*
 * write to the UDA1380 register space
 */
static int uda1380_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[3];

	/* data is
	 *   data[0] is register offset
	 *   data[1] is MS byte
	 *   data[2] is LS byte
	 */
	data[0] = reg;
	data[1] = (value & 0xff00) >> 8;
	data[2] = value & 0x00ff;

	uda1380_write_reg_cache(codec, reg, value);

	/* the interpolator & decimator regs must only be written when the
	 * codec DAI is active.
	 */
	if (!codec->active && (reg >= UDA1380_MVOL))
		return 0;
	pr_debug("uda1380: hw write %x val %x\n", reg, value);
	if (codec->hw_write(codec->control_data, data, 3) == 3) {
		unsigned int val;
		i2c_master_send(codec->control_data, data, 1);
		i2c_master_recv(codec->control_data, data, 2);
		val = (data[0]<<8) | data[1];
		if (val != value) {
			pr_debug("uda1380: READ BACK VAL %x\n",
					(data[0]<<8) | data[1]);
			return -EIO;
		}
		if (reg >= 0x10)
			clear_bit(reg - 0x10, &uda1380_cache_dirty);
		return 0;
	} else
		return -EIO;
}

static void uda1380_sync_cache(struct snd_soc_codec *codec)
{
	int reg;
	u8 data[3];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (reg = 0; reg < UDA1380_MVOL; reg++) {
		data[0] = reg;
		data[1] = (cache[reg] & 0xff00) >> 8;
		data[2] = cache[reg] & 0x00ff;
		if (codec->hw_write(codec->control_data, data, 3) != 3)
			dev_err(codec->dev, "%s: write to reg 0x%x failed\n",
				__func__, reg);
	}
}

static int uda1380_reset(struct snd_soc_codec *codec)
{
	struct uda1380_platform_data *pdata = codec->dev->platform_data;

	if (gpio_is_valid(pdata->gpio_reset)) {
		gpio_set_value(pdata->gpio_reset, 1);
		mdelay(1);
		gpio_set_value(pdata->gpio_reset, 0);
	} else {
		u8 data[3];

		data[0] = UDA1380_RESET;
		data[1] = 0;
		data[2] = 0;

		if (codec->hw_write(codec->control_data, data, 3) != 3) {
			dev_err(codec->dev, "%s: failed\n", __func__);
			return -EIO;
		}
	}

	return 0;
}

static void uda1380_flush_work(struct work_struct *work)
{
	struct uda1380_priv *uda1380 = container_of(work, struct uda1380_priv, work);
	struct snd_soc_codec *uda1380_codec = uda1380->codec;
	int bit, reg;

	for_each_set_bit(bit, &uda1380_cache_dirty, UDA1380_CACHEREGNUM - 0x10) {
		reg = 0x10 + bit;
		pr_debug("uda1380: flush reg %x val %x:\n", reg,
				uda1380_read_reg_cache(uda1380_codec, reg));
		uda1380_write(uda1380_codec, reg,
				uda1380_read_reg_cache(uda1380_codec, reg));
		clear_bit(bit, &uda1380_cache_dirty);
	}

}

/* declarations of ALSA reg_elem_REAL controls */
static const char *uda1380_deemp[] = {
	"None",
	"32kHz",
	"44.1kHz",
	"48kHz",
	"96kHz",
};
static const char *uda1380_input_sel[] = {
	"Line",
	"Mic + Line R",
	"Line L",
	"Mic",
};
static const char *uda1380_output_sel[] = {
	"DAC",
	"Analog Mixer",
};
static const char *uda1380_spf_mode[] = {
	"Flat",
	"Minimum1",
	"Minimum2",
	"Maximum"
};
static const char *uda1380_capture_sel[] = {
	"ADC",
	"Digital Mixer"
};
static const char *uda1380_sel_ns[] = {
	"3rd-order",
	"5th-order"
};
static const char *uda1380_mix_control[] = {
	"off",
	"PCM only",
	"before sound processing",
	"after sound processing"
};
static const char *uda1380_sdet_setting[] = {
	"3200",
	"4800",
	"9600",
	"19200"
};
static const char *uda1380_os_setting[] = {
	"single-speed",
	"double-speed (no mixing)",
	"quad-speed (no mixing)"
};

static const struct soc_enum uda1380_deemp_enum[] = {
	SOC_ENUM_SINGLE(UDA1380_DEEMP, 8, 5, uda1380_deemp),
	SOC_ENUM_SINGLE(UDA1380_DEEMP, 0, 5, uda1380_deemp),
};
static const struct soc_enum uda1380_input_sel_enum =
	SOC_ENUM_SINGLE(UDA1380_ADC, 2, 4, uda1380_input_sel);		/* SEL_MIC, SEL_LNA */
static const struct soc_enum uda1380_output_sel_enum =
	SOC_ENUM_SINGLE(UDA1380_PM, 7, 2, uda1380_output_sel);		/* R02_EN_AVC */
static const struct soc_enum uda1380_spf_enum =
	SOC_ENUM_SINGLE(UDA1380_MODE, 14, 4, uda1380_spf_mode);		/* M */
static const struct soc_enum uda1380_capture_sel_enum =
	SOC_ENUM_SINGLE(UDA1380_IFACE, 6, 2, uda1380_capture_sel);	/* SEL_SOURCE */
static const struct soc_enum uda1380_sel_ns_enum =
	SOC_ENUM_SINGLE(UDA1380_MIXER, 14, 2, uda1380_sel_ns);		/* SEL_NS */
static const struct soc_enum uda1380_mix_enum =
	SOC_ENUM_SINGLE(UDA1380_MIXER, 12, 4, uda1380_mix_control);	/* MIX, MIX_POS */
static const struct soc_enum uda1380_sdet_enum =
	SOC_ENUM_SINGLE(UDA1380_MIXER, 4, 4, uda1380_sdet_setting);	/* SD_VALUE */
static const struct soc_enum uda1380_os_enum =
	SOC_ENUM_SINGLE(UDA1380_MIXER, 0, 3, uda1380_os_setting);	/* OS */

/*
 * from -48 dB in 1.5 dB steps (mute instead of -49.5 dB)
 */
static DECLARE_TLV_DB_SCALE(amix_tlv, -4950, 150, 1);

/*
 * from -78 dB in 1 dB steps (3 dB steps, really. LSB are ignored),
 * from -66 dB in 0.5 dB steps (2 dB steps, really) and
 * from -52 dB in 0.25 dB steps
 */
static const unsigned int mvol_tlv[] = {
	TLV_DB_RANGE_HEAD(3),
	0, 15, TLV_DB_SCALE_ITEM(-8200, 100, 1),
	16, 43, TLV_DB_SCALE_ITEM(-6600, 50, 0),
	44, 252, TLV_DB_SCALE_ITEM(-5200, 25, 0),
};

/*
 * from -72 dB in 1.5 dB steps (6 dB steps really),
 * from -66 dB in 0.75 dB steps (3 dB steps really),
 * from -60 dB in 0.5 dB steps (2 dB steps really) and
 * from -46 dB in 0.25 dB steps
 */
static const unsigned int vc_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 7, TLV_DB_SCALE_ITEM(-7800, 150, 1),
	8, 15, TLV_DB_SCALE_ITEM(-6600, 75, 0),
	16, 43, TLV_DB_SCALE_ITEM(-6000, 50, 0),
	44, 228, TLV_DB_SCALE_ITEM(-4600, 25, 0),
};

/* from 0 to 6 dB in 2 dB steps if SPF mode != flat */
static DECLARE_TLV_DB_SCALE(tr_tlv, 0, 200, 0);

/* from 0 to 24 dB in 2 dB steps, if SPF mode == maximum, otherwise cuts
 * off at 18 dB max) */
static DECLARE_TLV_DB_SCALE(bb_tlv, 0, 200, 0);

/* from -63 to 24 dB in 0.5 dB steps (-128...48) */
static DECLARE_TLV_DB_SCALE(dec_tlv, -6400, 50, 1);

/* from 0 to 24 dB in 3 dB steps */
static DECLARE_TLV_DB_SCALE(pga_tlv, 0, 300, 0);

/* from 0 to 30 dB in 2 dB steps */
static DECLARE_TLV_DB_SCALE(vga_tlv, 0, 200, 0);

static const struct snd_kcontrol_new uda1380_snd_controls[] = {
	SOC_DOUBLE_TLV("Analog Mixer Volume", UDA1380_AMIX, 0, 8, 44, 1, amix_tlv),	/* AVCR, AVCL */
	SOC_DOUBLE_TLV("Master Playback Volume", UDA1380_MVOL, 0, 8, 252, 1, mvol_tlv),	/* MVCL, MVCR */
	SOC_SINGLE_TLV("ADC Playback Volume", UDA1380_MIXVOL, 8, 228, 1, vc_tlv),	/* VC2 */
	SOC_SINGLE_TLV("PCM Playback Volume", UDA1380_MIXVOL, 0, 228, 1, vc_tlv),	/* VC1 */
	SOC_ENUM("Sound Processing Filter", uda1380_spf_enum),				/* M */
	SOC_DOUBLE_TLV("Tone Control - Treble", UDA1380_MODE, 4, 12, 3, 0, tr_tlv), 	/* TRL, TRR */
	SOC_DOUBLE_TLV("Tone Control - Bass", UDA1380_MODE, 0, 8, 15, 0, bb_tlv),	/* BBL, BBR */
/**/	SOC_SINGLE("Master Playback Switch", UDA1380_DEEMP, 14, 1, 1),		/* MTM */
	SOC_SINGLE("ADC Playback Switch", UDA1380_DEEMP, 11, 1, 1),		/* MT2 from decimation filter */
	SOC_ENUM("ADC Playback De-emphasis", uda1380_deemp_enum[0]),		/* DE2 */
	SOC_SINGLE("PCM Playback Switch", UDA1380_DEEMP, 3, 1, 1),		/* MT1, from digital data input */
	SOC_ENUM("PCM Playback De-emphasis", uda1380_deemp_enum[1]),		/* DE1 */
	SOC_SINGLE("DAC Polarity inverting Switch", UDA1380_MIXER, 15, 1, 0),	/* DA_POL_INV */
	SOC_ENUM("Noise Shaper", uda1380_sel_ns_enum),				/* SEL_NS */
	SOC_ENUM("Digital Mixer Signal Control", uda1380_mix_enum),		/* MIX_POS, MIX */
	SOC_SINGLE("Silence Detector Switch", UDA1380_MIXER, 6, 1, 0),		/* SDET_ON */
	SOC_ENUM("Silence Detector Setting", uda1380_sdet_enum),		/* SD_VALUE */
	SOC_ENUM("Oversampling Input", uda1380_os_enum),			/* OS */
	SOC_DOUBLE_S8_TLV("ADC Capture Volume", UDA1380_DEC, -128, 48, dec_tlv),	/* ML_DEC, MR_DEC */
/**/	SOC_SINGLE("ADC Capture Switch", UDA1380_PGA, 15, 1, 1),		/* MT_ADC */
	SOC_DOUBLE_TLV("Line Capture Volume", UDA1380_PGA, 0, 8, 8, 0, pga_tlv), /* PGA_GAINCTRLL, PGA_GAINCTRLR */
	SOC_SINGLE("ADC Polarity inverting Switch", UDA1380_ADC, 12, 1, 0),	/* ADCPOL_INV */
	SOC_SINGLE_TLV("Mic Capture Volume", UDA1380_ADC, 8, 15, 0, vga_tlv),	/* VGA_CTRL */
	SOC_SINGLE("DC Filter Bypass Switch", UDA1380_ADC, 1, 1, 0),		/* SKIP_DCFIL (before decimator) */
	SOC_SINGLE("DC Filter Enable Switch", UDA1380_ADC, 0, 1, 0),		/* EN_DCFIL (at output of decimator) */
	SOC_SINGLE("AGC Timing", UDA1380_AGC, 8, 7, 0),			/* TODO: enum, see table 62 */
	SOC_SINGLE("AGC Target level", UDA1380_AGC, 2, 3, 1),			/* AGC_LEVEL */
	/* -5.5, -8, -11.5, -14 dBFS */
	SOC_SINGLE("AGC Switch", UDA1380_AGC, 0, 1, 0),
};

/* Input mux */
static const struct snd_kcontrol_new uda1380_input_mux_control =
	SOC_DAPM_ENUM("Route", uda1380_input_sel_enum);

/* Output mux */
static const struct snd_kcontrol_new uda1380_output_mux_control =
	SOC_DAPM_ENUM("Route", uda1380_output_sel_enum);

/* Capture mux */
static const struct snd_kcontrol_new uda1380_capture_mux_control =
	SOC_DAPM_ENUM("Route", uda1380_capture_sel_enum);


static const struct snd_soc_dapm_widget uda1380_dapm_widgets[] = {
	SND_SOC_DAPM_MUX("Input Mux", SND_SOC_NOPM, 0, 0,
		&uda1380_input_mux_control),
	SND_SOC_DAPM_MUX("Output Mux", SND_SOC_NOPM, 0, 0,
		&uda1380_output_mux_control),
	SND_SOC_DAPM_MUX("Capture Mux", SND_SOC_NOPM, 0, 0,
		&uda1380_capture_mux_control),
	SND_SOC_DAPM_PGA("Left PGA", UDA1380_PM, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right PGA", UDA1380_PM, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mic LNA", UDA1380_PM, 4, 0, NULL, 0),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", UDA1380_PM, 2, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", UDA1380_PM, 0, 0),
	SND_SOC_DAPM_INPUT("VINM"),
	SND_SOC_DAPM_INPUT("VINL"),
	SND_SOC_DAPM_INPUT("VINR"),
	SND_SOC_DAPM_MIXER("Analog Mixer", UDA1380_PM, 6, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("VOUTLHP"),
	SND_SOC_DAPM_OUTPUT("VOUTRHP"),
	SND_SOC_DAPM_OUTPUT("VOUTL"),
	SND_SOC_DAPM_OUTPUT("VOUTR"),
	SND_SOC_DAPM_DAC("DAC", "Playback", UDA1380_PM, 10, 0),
	SND_SOC_DAPM_PGA("HeadPhone Driver", UDA1380_PM, 13, 0, NULL, 0),
};

static const struct snd_soc_dapm_route audio_map[] = {

	/* output mux */
	{"HeadPhone Driver", NULL, "Output Mux"},
	{"VOUTR", NULL, "Output Mux"},
	{"VOUTL", NULL, "Output Mux"},

	{"Analog Mixer", NULL, "VINR"},
	{"Analog Mixer", NULL, "VINL"},
	{"Analog Mixer", NULL, "DAC"},

	{"Output Mux", "DAC", "DAC"},
	{"Output Mux", "Analog Mixer", "Analog Mixer"},

	/* {"DAC", "Digital Mixer", "I2S" } */

	/* headphone driver */
	{"VOUTLHP", NULL, "HeadPhone Driver"},
	{"VOUTRHP", NULL, "HeadPhone Driver"},

	/* input mux */
	{"Left ADC", NULL, "Input Mux"},
	{"Input Mux", "Mic", "Mic LNA"},
	{"Input Mux", "Mic + Line R", "Mic LNA"},
	{"Input Mux", "Line L", "Left PGA"},
	{"Input Mux", "Line", "Left PGA"},

	/* right input */
	{"Right ADC", "Mic + Line R", "Right PGA"},
	{"Right ADC", "Line", "Right PGA"},

	/* inputs */
	{"Mic LNA", NULL, "VINM"},
	{"Left PGA", NULL, "VINL"},
	{"Right PGA", NULL, "VINR"},
};

static int uda1380_add_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, uda1380_dapm_widgets,
				  ARRAY_SIZE(uda1380_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	return 0;
}

static int uda1380_set_dai_fmt_both(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int iface;

	/* set up DAI based upon fmt */
	iface = uda1380_read_reg_cache(codec, UDA1380_IFACE);
	iface &= ~(R01_SFORI_MASK | R01_SIM | R01_SFORO_MASK);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= R01_SFORI_I2S | R01_SFORO_I2S;
		break;
	case SND_SOC_DAIFMT_LSB:
		iface |= R01_SFORI_LSB16 | R01_SFORO_LSB16;
		break;
	case SND_SOC_DAIFMT_MSB:
		iface |= R01_SFORI_MSB | R01_SFORO_MSB;
	}

	/* DATAI is slave only, so in single-link mode, this has to be slave */
	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS)
		return -EINVAL;

	uda1380_write(codec, UDA1380_IFACE, iface);

	return 0;
}

static int uda1380_set_dai_fmt_playback(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int iface;

	/* set up DAI based upon fmt */
	iface = uda1380_read_reg_cache(codec, UDA1380_IFACE);
	iface &= ~R01_SFORI_MASK;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= R01_SFORI_I2S;
		break;
	case SND_SOC_DAIFMT_LSB:
		iface |= R01_SFORI_LSB16;
		break;
	case SND_SOC_DAIFMT_MSB:
		iface |= R01_SFORI_MSB;
	}

	/* DATAI is slave only, so this has to be slave */
	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS)
		return -EINVAL;

	uda1380_write(codec, UDA1380_IFACE, iface);

	return 0;
}

static int uda1380_set_dai_fmt_capture(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int iface;

	/* set up DAI based upon fmt */
	iface = uda1380_read_reg_cache(codec, UDA1380_IFACE);
	iface &= ~(R01_SIM | R01_SFORO_MASK);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= R01_SFORO_I2S;
		break;
	case SND_SOC_DAIFMT_LSB:
		iface |= R01_SFORO_LSB16;
		break;
	case SND_SOC_DAIFMT_MSB:
		iface |= R01_SFORO_MSB;
	}

	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBM_CFM)
		iface |= R01_SIM;

	uda1380_write(codec, UDA1380_IFACE, iface);

	return 0;
}

static int uda1380_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct uda1380_priv *uda1380 = snd_soc_codec_get_drvdata(codec);
	int mixer = uda1380_read_reg_cache(codec, UDA1380_MIXER);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		uda1380_write_reg_cache(codec, UDA1380_MIXER,
					mixer & ~R14_SILENCE);
		schedule_work(&uda1380->work);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		uda1380_write_reg_cache(codec, UDA1380_MIXER,
					mixer | R14_SILENCE);
		schedule_work(&uda1380->work);
		break;
	}
	return 0;
}

static int uda1380_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	u16 clk = uda1380_read_reg_cache(codec, UDA1380_CLK);

	/* set WSPLL power and divider if running from this clock */
	if (clk & R00_DAC_CLK) {
		int rate = params_rate(params);
		u16 pm = uda1380_read_reg_cache(codec, UDA1380_PM);
		clk &= ~0x3; /* clear SEL_LOOP_DIV */
		switch (rate) {
		case 6250 ... 12500:
			clk |= 0x0;
			break;
		case 12501 ... 25000:
			clk |= 0x1;
			break;
		case 25001 ... 50000:
			clk |= 0x2;
			break;
		case 50001 ... 100000:
			clk |= 0x3;
			break;
		}
		uda1380_write(codec, UDA1380_PM, R02_PON_PLL | pm);
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		clk |= R00_EN_DAC | R00_EN_INT;
	else
		clk |= R00_EN_ADC | R00_EN_DEC;

	uda1380_write(codec, UDA1380_CLK, clk);
	return 0;
}

static void uda1380_pcm_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	u16 clk = uda1380_read_reg_cache(codec, UDA1380_CLK);

	/* shut down WSPLL power if running from this clock */
	if (clk & R00_DAC_CLK) {
		u16 pm = uda1380_read_reg_cache(codec, UDA1380_PM);
		uda1380_write(codec, UDA1380_PM, ~R02_PON_PLL & pm);
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		clk &= ~(R00_EN_DAC | R00_EN_INT);
	else
		clk &= ~(R00_EN_ADC | R00_EN_DEC);

	uda1380_write(codec, UDA1380_CLK, clk);
}

static int uda1380_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	int pm = uda1380_read_reg_cache(codec, UDA1380_PM);
	int reg;
	struct uda1380_platform_data *pdata = codec->dev->platform_data;

	if (codec->dapm.bias_level == level)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		/* ADC, DAC on */
		uda1380_write(codec, UDA1380_PM, R02_PON_BIAS | pm);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			if (gpio_is_valid(pdata->gpio_power)) {
				gpio_set_value(pdata->gpio_power, 1);
				mdelay(1);
				uda1380_reset(codec);
			}

			uda1380_sync_cache(codec);
		}
		uda1380_write(codec, UDA1380_PM, 0x0);
		break;
	case SND_SOC_BIAS_OFF:
		if (!gpio_is_valid(pdata->gpio_power))
			break;

		gpio_set_value(pdata->gpio_power, 0);

		/* Mark mixer regs cache dirty to sync them with
		 * codec regs on power on.
		 */
		for (reg = UDA1380_MVOL; reg < UDA1380_CACHEREGNUM; reg++)
			set_bit(reg - 0x10, &uda1380_cache_dirty);
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define UDA1380_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		       SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
		       SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

static struct snd_soc_dai_ops uda1380_dai_ops = {
	.hw_params	= uda1380_pcm_hw_params,
	.shutdown	= uda1380_pcm_shutdown,
	.trigger	= uda1380_trigger,
	.set_fmt	= uda1380_set_dai_fmt_both,
};

static struct snd_soc_dai_ops uda1380_dai_ops_playback = {
	.hw_params	= uda1380_pcm_hw_params,
	.shutdown	= uda1380_pcm_shutdown,
	.trigger	= uda1380_trigger,
	.set_fmt	= uda1380_set_dai_fmt_playback,
};

static struct snd_soc_dai_ops uda1380_dai_ops_capture = {
	.hw_params	= uda1380_pcm_hw_params,
	.shutdown	= uda1380_pcm_shutdown,
	.trigger	= uda1380_trigger,
	.set_fmt	= uda1380_set_dai_fmt_capture,
};

static struct snd_soc_dai_driver uda1380_dai[] = {
{
	.name = "uda1380-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA1380_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA1380_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = &uda1380_dai_ops,
},
{ /* playback only - dual interface */
	.name = "uda1380-hifi-playback",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA1380_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &uda1380_dai_ops_playback,
},
{ /* capture only - dual interface*/
	.name = "uda1380-hifi-capture",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA1380_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &uda1380_dai_ops_capture,
},
};

static int uda1380_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	uda1380_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int uda1380_resume(struct snd_soc_codec *codec)
{
	uda1380_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}

static int uda1380_probe(struct snd_soc_codec *codec)
{
	struct uda1380_platform_data *pdata =codec->dev->platform_data;
	struct uda1380_priv *uda1380 = snd_soc_codec_get_drvdata(codec);
	int ret;

	uda1380->codec = codec;

	codec->hw_write = (hw_write_t)i2c_master_send;
	codec->control_data = uda1380->control_data;

	if (!pdata)
		return -EINVAL;

	if (gpio_is_valid(pdata->gpio_reset)) {
		ret = gpio_request(pdata->gpio_reset, "uda1380 reset");
		if (ret)
			goto err_out;
		ret = gpio_direction_output(pdata->gpio_reset, 0);
		if (ret)
			goto err_gpio_reset_conf;
	}

	if (gpio_is_valid(pdata->gpio_power)) {
		ret = gpio_request(pdata->gpio_power, "uda1380 power");
		if (ret)
			goto err_gpio;
		ret = gpio_direction_output(pdata->gpio_power, 0);
		if (ret)
			goto err_gpio_power_conf;
	} else {
		ret = uda1380_reset(codec);
		if (ret) {
			dev_err(codec->dev, "Failed to issue reset\n");
			goto err_reset;
		}
	}

	INIT_WORK(&uda1380->work, uda1380_flush_work);

	/* power on device */
	uda1380_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	/* set clock input */
	switch (pdata->dac_clk) {
	case UDA1380_DAC_CLK_SYSCLK:
		uda1380_write_reg_cache(codec, UDA1380_CLK, 0);
		break;
	case UDA1380_DAC_CLK_WSPLL:
		uda1380_write_reg_cache(codec, UDA1380_CLK,
			R00_DAC_CLK);
		break;
	}

	snd_soc_add_controls(codec, uda1380_snd_controls,
				ARRAY_SIZE(uda1380_snd_controls));
	uda1380_add_widgets(codec);

	return 0;

err_reset:
err_gpio_power_conf:
	if (gpio_is_valid(pdata->gpio_power))
		gpio_free(pdata->gpio_power);

err_gpio_reset_conf:
err_gpio:
	if (gpio_is_valid(pdata->gpio_reset))
		gpio_free(pdata->gpio_reset);
err_out:
	return ret;
}

/* power down chip */
static int uda1380_remove(struct snd_soc_codec *codec)
{
	struct uda1380_platform_data *pdata =codec->dev->platform_data;

	uda1380_set_bias_level(codec, SND_SOC_BIAS_OFF);

	gpio_free(pdata->gpio_reset);
	gpio_free(pdata->gpio_power);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_uda1380 = {
	.probe =	uda1380_probe,
	.remove =	uda1380_remove,
	.suspend =	uda1380_suspend,
	.resume =	uda1380_resume,
	.read =		uda1380_read_reg_cache,
	.write =	uda1380_write,
	.set_bias_level = uda1380_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(uda1380_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = uda1380_reg,
	.reg_cache_step = 1,
};

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int uda1380_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct uda1380_priv *uda1380;
	int ret;

	uda1380 = kzalloc(sizeof(struct uda1380_priv), GFP_KERNEL);
	if (uda1380 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, uda1380);
	uda1380->control_data = i2c;

	ret =  snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_uda1380, uda1380_dai, ARRAY_SIZE(uda1380_dai));
	if (ret < 0)
		kfree(uda1380);
	return ret;
}

static int __devexit uda1380_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);
	kfree(i2c_get_clientdata(i2c));
	return 0;
}

static const struct i2c_device_id uda1380_i2c_id[] = {
	{ "uda1380", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, uda1380_i2c_id);

static struct i2c_driver uda1380_i2c_driver = {
	.driver = {
		.name =  "uda1380-codec",
		.owner = THIS_MODULE,
	},
	.probe =    uda1380_i2c_probe,
	.remove =   __devexit_p(uda1380_i2c_remove),
	.id_table = uda1380_i2c_id,
};
#endif

static int __init uda1380_modinit(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&uda1380_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register UDA1380 I2C driver: %d\n", ret);
#endif
	return ret;
}
module_init(uda1380_modinit);

static void __exit uda1380_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&uda1380_i2c_driver);
#endif
}
module_exit(uda1380_exit);

MODULE_AUTHOR("Giorgio Padrin");
MODULE_DESCRIPTION("Audio support for codec Philips UDA1380");
MODULE_LICENSE("GPL");
