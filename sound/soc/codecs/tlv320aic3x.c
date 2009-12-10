/*
 * ALSA SoC TLV320AIC3X codec driver
 *
 * Author:      Vladimir Barinov, <vbarinov@embeddedalley.com>
 * Copyright:   (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 *
 * Based on sound/soc/codecs/wm8753.c by Liam Girdwood
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Notes:
 *  The AIC3X is a driver for a low power stereo audio
 *  codecs aic31, aic32, aic33.
 *
 *  It supports full aic33 codec functionality.
 *  The compatibility with aic32, aic31 is as follows:
 *        aic32        |        aic31
 *  ---------------------------------------
 *   MONO_LOUT -> N/A  |  MONO_LOUT -> N/A
 *                     |  IN1L -> LINE1L
 *                     |  IN1R -> LINE1R
 *                     |  IN2L -> LINE2L
 *                     |  IN2R -> LINE2R
 *                     |  MIC3L/R -> N/A
 *   truncated internal functionality in
 *   accordance with documentation
 *  ---------------------------------------
 *
 *  Hence the machine layer should disable unsupported inputs/outputs by
 *  snd_soc_dapm_disable_pin(codec, "MONO_LOUT"), etc.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "tlv320aic3x.h"

#define AIC3X_VERSION "0.2"

/* codec private data */
struct aic3x_priv {
	struct snd_soc_codec codec;
	unsigned int sysclk;
	int master;
};

/*
 * AIC3X register cache
 * We can't read the AIC3X register space when we are
 * using 2 wire for device control, so we cache them instead.
 * There is no point in caching the reset register
 */
static const u8 aic3x_reg[AIC3X_CACHEREGNUM] = {
	0x00, 0x00, 0x00, 0x10,	/* 0 */
	0x04, 0x00, 0x00, 0x00,	/* 4 */
	0x00, 0x00, 0x00, 0x01,	/* 8 */
	0x00, 0x00, 0x00, 0x80,	/* 12 */
	0x80, 0xff, 0xff, 0x78,	/* 16 */
	0x78, 0x78, 0x78, 0x78,	/* 20 */
	0x78, 0x00, 0x00, 0xfe,	/* 24 */
	0x00, 0x00, 0xfe, 0x00,	/* 28 */
	0x18, 0x18, 0x00, 0x00,	/* 32 */
	0x00, 0x00, 0x00, 0x00,	/* 36 */
	0x00, 0x00, 0x00, 0x80,	/* 40 */
	0x80, 0x00, 0x00, 0x00,	/* 44 */
	0x00, 0x00, 0x00, 0x04,	/* 48 */
	0x00, 0x00, 0x00, 0x00,	/* 52 */
	0x00, 0x00, 0x04, 0x00,	/* 56 */
	0x00, 0x00, 0x00, 0x00,	/* 60 */
	0x00, 0x04, 0x00, 0x00,	/* 64 */
	0x00, 0x00, 0x00, 0x00,	/* 68 */
	0x04, 0x00, 0x00, 0x00,	/* 72 */
	0x00, 0x00, 0x00, 0x00,	/* 76 */
	0x00, 0x00, 0x00, 0x00,	/* 80 */
	0x00, 0x00, 0x00, 0x00,	/* 84 */
	0x00, 0x00, 0x00, 0x00,	/* 88 */
	0x00, 0x00, 0x00, 0x00,	/* 92 */
	0x00, 0x00, 0x00, 0x00,	/* 96 */
	0x00, 0x00, 0x02,	/* 100 */
};

/*
 * read aic3x register cache
 */
static inline unsigned int aic3x_read_reg_cache(struct snd_soc_codec *codec,
						unsigned int reg)
{
	u8 *cache = codec->reg_cache;
	if (reg >= AIC3X_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write aic3x register cache
 */
static inline void aic3x_write_reg_cache(struct snd_soc_codec *codec,
					 u8 reg, u8 value)
{
	u8 *cache = codec->reg_cache;
	if (reg >= AIC3X_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the aic3x register space
 */
static int aic3x_write(struct snd_soc_codec *codec, unsigned int reg,
		       unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D8 aic3x register offset
	 *   D7...D0 register data
	 */
	data[0] = reg & 0xff;
	data[1] = value & 0xff;

	aic3x_write_reg_cache(codec, data[0], data[1]);
	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

/*
 * read from the aic3x register space
 */
static int aic3x_read(struct snd_soc_codec *codec, unsigned int reg,
		      u8 *value)
{
	*value = reg & 0xff;

	value[0] = i2c_smbus_read_byte_data(codec->control_data, value[0]);

	aic3x_write_reg_cache(codec, reg, *value);
	return 0;
}

#define SOC_DAPM_SINGLE_AIC3X(xname, reg, shift, mask, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw_aic3x, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, mask, invert) }

/*
 * All input lines are connected when !0xf and disconnected with 0xf bit field,
 * so we have to use specific dapm_put call for input mixer
 */
static int snd_soc_dapm_put_volsw_aic3x(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned short val, val_mask;
	int ret;
	struct snd_soc_dapm_path *path;
	int found = 0;

	val = (ucontrol->value.integer.value[0] & mask);

	mask = 0xf;
	if (val)
		val = mask;

	if (invert)
		val = mask - val;
	val_mask = mask << shift;
	val = val << shift;

	mutex_lock(&widget->codec->mutex);

	if (snd_soc_test_bits(widget->codec, reg, val_mask, val)) {
		/* find dapm widget path assoc with kcontrol */
		list_for_each_entry(path, &widget->codec->dapm_paths, list) {
			if (path->kcontrol != kcontrol)
				continue;

			/* found, now check type */
			found = 1;
			if (val)
				/* new connection */
				path->connect = invert ? 0 : 1;
			else
				/* old connection must be powered down */
				path->connect = invert ? 1 : 0;
			break;
		}

		if (found)
			snd_soc_dapm_sync(widget->codec);
	}

	ret = snd_soc_update_bits(widget->codec, reg, val_mask, val);

	mutex_unlock(&widget->codec->mutex);
	return ret;
}

static const char *aic3x_left_dac_mux[] = { "DAC_L1", "DAC_L3", "DAC_L2" };
static const char *aic3x_right_dac_mux[] = { "DAC_R1", "DAC_R3", "DAC_R2" };
static const char *aic3x_left_hpcom_mux[] =
    { "differential of HPLOUT", "constant VCM", "single-ended" };
static const char *aic3x_right_hpcom_mux[] =
    { "differential of HPROUT", "constant VCM", "single-ended",
      "differential of HPLCOM", "external feedback" };
static const char *aic3x_linein_mode_mux[] = { "single-ended", "differential" };
static const char *aic3x_adc_hpf[] =
    { "Disabled", "0.0045xFs", "0.0125xFs", "0.025xFs" };

#define LDAC_ENUM	0
#define RDAC_ENUM	1
#define LHPCOM_ENUM	2
#define RHPCOM_ENUM	3
#define LINE1L_ENUM	4
#define LINE1R_ENUM	5
#define LINE2L_ENUM	6
#define LINE2R_ENUM	7
#define ADC_HPF_ENUM	8

static const struct soc_enum aic3x_enum[] = {
	SOC_ENUM_SINGLE(DAC_LINE_MUX, 6, 3, aic3x_left_dac_mux),
	SOC_ENUM_SINGLE(DAC_LINE_MUX, 4, 3, aic3x_right_dac_mux),
	SOC_ENUM_SINGLE(HPLCOM_CFG, 4, 3, aic3x_left_hpcom_mux),
	SOC_ENUM_SINGLE(HPRCOM_CFG, 3, 5, aic3x_right_hpcom_mux),
	SOC_ENUM_SINGLE(LINE1L_2_LADC_CTRL, 7, 2, aic3x_linein_mode_mux),
	SOC_ENUM_SINGLE(LINE1R_2_RADC_CTRL, 7, 2, aic3x_linein_mode_mux),
	SOC_ENUM_SINGLE(LINE2L_2_LADC_CTRL, 7, 2, aic3x_linein_mode_mux),
	SOC_ENUM_SINGLE(LINE2R_2_RADC_CTRL, 7, 2, aic3x_linein_mode_mux),
	SOC_ENUM_DOUBLE(AIC3X_CODEC_DFILT_CTRL, 6, 4, 4, aic3x_adc_hpf),
};

/*
 * DAC digital volumes. From -63.5 to 0 dB in 0.5 dB steps
 */
static DECLARE_TLV_DB_SCALE(dac_tlv, -6350, 50, 0);
/* ADC PGA gain volumes. From 0 to 59.5 dB in 0.5 dB steps */
static DECLARE_TLV_DB_SCALE(adc_tlv, 0, 50, 0);
/*
 * Output stage volumes. From -78.3 to 0 dB. Muted below -78.3 dB.
 * Step size is approximately 0.5 dB over most of the scale but increasing
 * near the very low levels.
 * Define dB scale so that it is mostly correct for range about -55 to 0 dB
 * but having increasing dB difference below that (and where it doesn't count
 * so much). This setting shows -50 dB (actual is -50.3 dB) for register
 * value 100 and -58.5 dB (actual is -78.3 dB) for register value 117.
 */
static DECLARE_TLV_DB_SCALE(output_stage_tlv, -5900, 50, 1);

static const struct snd_kcontrol_new aic3x_snd_controls[] = {
	/* Output */
	SOC_DOUBLE_R_TLV("PCM Playback Volume",
			 LDAC_VOL, RDAC_VOL, 0, 0x7f, 1, dac_tlv),

	SOC_DOUBLE_R_TLV("Line DAC Playback Volume",
			 DACL1_2_LLOPM_VOL, DACR1_2_RLOPM_VOL,
			 0, 118, 1, output_stage_tlv),
	SOC_SINGLE("LineL Playback Switch", LLOPM_CTRL, 3, 0x01, 0),
	SOC_SINGLE("LineR Playback Switch", RLOPM_CTRL, 3, 0x01, 0),
	SOC_DOUBLE_R_TLV("LineL DAC Playback Volume",
			 DACL1_2_LLOPM_VOL, DACR1_2_LLOPM_VOL,
			 0, 118, 1, output_stage_tlv),
	SOC_SINGLE_TLV("LineL Left PGA Bypass Playback Volume",
		       PGAL_2_LLOPM_VOL, 0, 118, 1, output_stage_tlv),
	SOC_SINGLE_TLV("LineR Right PGA Bypass Playback Volume",
		       PGAR_2_RLOPM_VOL, 0, 118, 1, output_stage_tlv),
	SOC_DOUBLE_R_TLV("LineL Line2 Bypass Playback Volume",
			 LINE2L_2_LLOPM_VOL, LINE2R_2_LLOPM_VOL,
			 0, 118, 1, output_stage_tlv),
	SOC_DOUBLE_R_TLV("LineR Line2 Bypass Playback Volume",
			 LINE2L_2_RLOPM_VOL, LINE2R_2_RLOPM_VOL,
			 0, 118, 1, output_stage_tlv),

	SOC_DOUBLE_R_TLV("Mono DAC Playback Volume",
			 DACL1_2_MONOLOPM_VOL, DACR1_2_MONOLOPM_VOL,
			 0, 118, 1, output_stage_tlv),
	SOC_SINGLE("Mono DAC Playback Switch", MONOLOPM_CTRL, 3, 0x01, 0),
	SOC_DOUBLE_R_TLV("Mono PGA Bypass Playback Volume",
			 PGAL_2_MONOLOPM_VOL, PGAR_2_MONOLOPM_VOL,
			 0, 118, 1, output_stage_tlv),
	SOC_DOUBLE_R_TLV("Mono Line2 Bypass Playback Volume",
			 LINE2L_2_MONOLOPM_VOL, LINE2R_2_MONOLOPM_VOL,
			 0, 118, 1, output_stage_tlv),

	SOC_DOUBLE_R_TLV("HP DAC Playback Volume",
			 DACL1_2_HPLOUT_VOL, DACR1_2_HPROUT_VOL,
			 0, 118, 1, output_stage_tlv),
	SOC_DOUBLE_R("HP DAC Playback Switch", HPLOUT_CTRL, HPROUT_CTRL, 3,
		     0x01, 0),
	SOC_DOUBLE_R_TLV("HP Right PGA Bypass Playback Volume",
			 PGAR_2_HPLOUT_VOL, PGAR_2_HPROUT_VOL,
			 0, 118, 1, output_stage_tlv),
	SOC_SINGLE_TLV("HPL PGA Bypass Playback Volume",
		       PGAL_2_HPLOUT_VOL, 0, 118, 1, output_stage_tlv),
	SOC_SINGLE_TLV("HPR PGA Bypass Playback Volume",
		       PGAL_2_HPROUT_VOL, 0, 118, 1, output_stage_tlv),
	SOC_DOUBLE_R_TLV("HP Line2 Bypass Playback Volume",
			 LINE2L_2_HPLOUT_VOL, LINE2R_2_HPROUT_VOL,
			 0, 118, 1, output_stage_tlv),

	SOC_DOUBLE_R_TLV("HPCOM DAC Playback Volume",
			 DACL1_2_HPLCOM_VOL, DACR1_2_HPRCOM_VOL,
			 0, 118, 1, output_stage_tlv),
	SOC_DOUBLE_R("HPCOM DAC Playback Switch", HPLCOM_CTRL, HPRCOM_CTRL, 3,
		     0x01, 0),
	SOC_SINGLE_TLV("HPLCOM PGA Bypass Playback Volume",
		       PGAL_2_HPLCOM_VOL, 0, 118, 1, output_stage_tlv),
	SOC_SINGLE_TLV("HPRCOM PGA Bypass Playback Volume",
		       PGAL_2_HPRCOM_VOL, 0, 118, 1, output_stage_tlv),
	SOC_DOUBLE_R_TLV("HPCOM Line2 Bypass Playback Volume",
			 LINE2L_2_HPLCOM_VOL, LINE2R_2_HPRCOM_VOL,
			 0, 118, 1, output_stage_tlv),

	/*
	 * Note: enable Automatic input Gain Controller with care. It can
	 * adjust PGA to max value when ADC is on and will never go back.
	*/
	SOC_DOUBLE_R("AGC Switch", LAGC_CTRL_A, RAGC_CTRL_A, 7, 0x01, 0),

	/* Input */
	SOC_DOUBLE_R_TLV("PGA Capture Volume", LADC_VOL, RADC_VOL,
			 0, 119, 0, adc_tlv),
	SOC_DOUBLE_R("PGA Capture Switch", LADC_VOL, RADC_VOL, 7, 0x01, 1),

	SOC_ENUM("ADC HPF Cut-off", aic3x_enum[ADC_HPF_ENUM]),
};

/* Left DAC Mux */
static const struct snd_kcontrol_new aic3x_left_dac_mux_controls =
SOC_DAPM_ENUM("Route", aic3x_enum[LDAC_ENUM]);

/* Right DAC Mux */
static const struct snd_kcontrol_new aic3x_right_dac_mux_controls =
SOC_DAPM_ENUM("Route", aic3x_enum[RDAC_ENUM]);

/* Left HPCOM Mux */
static const struct snd_kcontrol_new aic3x_left_hpcom_mux_controls =
SOC_DAPM_ENUM("Route", aic3x_enum[LHPCOM_ENUM]);

/* Right HPCOM Mux */
static const struct snd_kcontrol_new aic3x_right_hpcom_mux_controls =
SOC_DAPM_ENUM("Route", aic3x_enum[RHPCOM_ENUM]);

/* Left DAC_L1 Mixer */
static const struct snd_kcontrol_new aic3x_left_dac_mixer_controls[] = {
	SOC_DAPM_SINGLE("LineL Switch", DACL1_2_LLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("LineR Switch", DACL1_2_RLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("Mono Switch", DACL1_2_MONOLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HP Switch", DACL1_2_HPLOUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPCOM Switch", DACL1_2_HPLCOM_VOL, 7, 1, 0),
};

/* Right DAC_R1 Mixer */
static const struct snd_kcontrol_new aic3x_right_dac_mixer_controls[] = {
	SOC_DAPM_SINGLE("LineL Switch", DACR1_2_LLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("LineR Switch", DACR1_2_RLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("Mono Switch", DACR1_2_MONOLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HP Switch", DACR1_2_HPROUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPCOM Switch", DACR1_2_HPRCOM_VOL, 7, 1, 0),
};

/* Left PGA Mixer */
static const struct snd_kcontrol_new aic3x_left_pga_mixer_controls[] = {
	SOC_DAPM_SINGLE_AIC3X("Line1L Switch", LINE1L_2_LADC_CTRL, 3, 1, 1),
	SOC_DAPM_SINGLE_AIC3X("Line1R Switch", LINE1R_2_LADC_CTRL, 3, 1, 1),
	SOC_DAPM_SINGLE_AIC3X("Line2L Switch", LINE2L_2_LADC_CTRL, 3, 1, 1),
	SOC_DAPM_SINGLE_AIC3X("Mic3L Switch", MIC3LR_2_LADC_CTRL, 4, 1, 1),
	SOC_DAPM_SINGLE_AIC3X("Mic3R Switch", MIC3LR_2_LADC_CTRL, 0, 1, 1),
};

/* Right PGA Mixer */
static const struct snd_kcontrol_new aic3x_right_pga_mixer_controls[] = {
	SOC_DAPM_SINGLE_AIC3X("Line1R Switch", LINE1R_2_RADC_CTRL, 3, 1, 1),
	SOC_DAPM_SINGLE_AIC3X("Line1L Switch", LINE1L_2_RADC_CTRL, 3, 1, 1),
	SOC_DAPM_SINGLE_AIC3X("Line2R Switch", LINE2R_2_RADC_CTRL, 3, 1, 1),
	SOC_DAPM_SINGLE_AIC3X("Mic3L Switch", MIC3LR_2_RADC_CTRL, 4, 1, 1),
	SOC_DAPM_SINGLE_AIC3X("Mic3R Switch", MIC3LR_2_RADC_CTRL, 0, 1, 1),
};

/* Left Line1 Mux */
static const struct snd_kcontrol_new aic3x_left_line1_mux_controls =
SOC_DAPM_ENUM("Route", aic3x_enum[LINE1L_ENUM]);

/* Right Line1 Mux */
static const struct snd_kcontrol_new aic3x_right_line1_mux_controls =
SOC_DAPM_ENUM("Route", aic3x_enum[LINE1R_ENUM]);

/* Left Line2 Mux */
static const struct snd_kcontrol_new aic3x_left_line2_mux_controls =
SOC_DAPM_ENUM("Route", aic3x_enum[LINE2L_ENUM]);

/* Right Line2 Mux */
static const struct snd_kcontrol_new aic3x_right_line2_mux_controls =
SOC_DAPM_ENUM("Route", aic3x_enum[LINE2R_ENUM]);

/* Left PGA Bypass Mixer */
static const struct snd_kcontrol_new aic3x_left_pga_bp_mixer_controls[] = {
	SOC_DAPM_SINGLE("LineL Switch", PGAL_2_LLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("LineR Switch", PGAL_2_RLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("Mono Switch", PGAL_2_MONOLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPL Switch", PGAL_2_HPLOUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPR Switch", PGAL_2_HPROUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPLCOM Switch", PGAL_2_HPLCOM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPRCOM Switch", PGAL_2_HPRCOM_VOL, 7, 1, 0),
};

/* Right PGA Bypass Mixer */
static const struct snd_kcontrol_new aic3x_right_pga_bp_mixer_controls[] = {
	SOC_DAPM_SINGLE("LineL Switch", PGAR_2_LLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("LineR Switch", PGAR_2_RLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("Mono Switch", PGAR_2_MONOLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPL Switch", PGAR_2_HPLOUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPR Switch", PGAR_2_HPROUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPLCOM Switch", PGAR_2_HPLCOM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPRCOM Switch", PGAR_2_HPRCOM_VOL, 7, 1, 0),
};

/* Left Line2 Bypass Mixer */
static const struct snd_kcontrol_new aic3x_left_line2_bp_mixer_controls[] = {
	SOC_DAPM_SINGLE("LineL Switch", LINE2L_2_LLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("LineR Switch", LINE2L_2_RLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("Mono Switch", LINE2L_2_MONOLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HP Switch", LINE2L_2_HPLOUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPLCOM Switch", LINE2L_2_HPLCOM_VOL, 7, 1, 0),
};

/* Right Line2 Bypass Mixer */
static const struct snd_kcontrol_new aic3x_right_line2_bp_mixer_controls[] = {
	SOC_DAPM_SINGLE("LineL Switch", LINE2R_2_LLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("LineR Switch", LINE2R_2_RLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("Mono Switch", LINE2R_2_MONOLOPM_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HP Switch", LINE2R_2_HPROUT_VOL, 7, 1, 0),
	SOC_DAPM_SINGLE("HPRCOM Switch", LINE2R_2_HPRCOM_VOL, 7, 1, 0),
};

static const struct snd_soc_dapm_widget aic3x_dapm_widgets[] = {
	/* Left DAC to Left Outputs */
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", DAC_PWR, 7, 0),
	SND_SOC_DAPM_MUX("Left DAC Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_left_dac_mux_controls),
	SND_SOC_DAPM_MIXER("Left DAC_L1 Mixer", SND_SOC_NOPM, 0, 0,
			   &aic3x_left_dac_mixer_controls[0],
			   ARRAY_SIZE(aic3x_left_dac_mixer_controls)),
	SND_SOC_DAPM_MUX("Left HPCOM Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_left_hpcom_mux_controls),
	SND_SOC_DAPM_PGA("Left Line Out", LLOPM_CTRL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left HP Out", HPLOUT_CTRL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left HP Com", HPLCOM_CTRL, 0, 0, NULL, 0),

	/* Right DAC to Right Outputs */
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", DAC_PWR, 6, 0),
	SND_SOC_DAPM_MUX("Right DAC Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_right_dac_mux_controls),
	SND_SOC_DAPM_MIXER("Right DAC_R1 Mixer", SND_SOC_NOPM, 0, 0,
			   &aic3x_right_dac_mixer_controls[0],
			   ARRAY_SIZE(aic3x_right_dac_mixer_controls)),
	SND_SOC_DAPM_MUX("Right HPCOM Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_right_hpcom_mux_controls),
	SND_SOC_DAPM_PGA("Right Line Out", RLOPM_CTRL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right HP Out", HPROUT_CTRL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right HP Com", HPRCOM_CTRL, 0, 0, NULL, 0),

	/* Mono Output */
	SND_SOC_DAPM_PGA("Mono Out", MONOLOPM_CTRL, 0, 0, NULL, 0),

	/* Inputs to Left ADC */
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", LINE1L_2_LADC_CTRL, 2, 0),
	SND_SOC_DAPM_MIXER("Left PGA Mixer", SND_SOC_NOPM, 0, 0,
			   &aic3x_left_pga_mixer_controls[0],
			   ARRAY_SIZE(aic3x_left_pga_mixer_controls)),
	SND_SOC_DAPM_MUX("Left Line1L Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_left_line1_mux_controls),
	SND_SOC_DAPM_MUX("Left Line1R Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_left_line1_mux_controls),
	SND_SOC_DAPM_MUX("Left Line2L Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_left_line2_mux_controls),

	/* Inputs to Right ADC */
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture",
			 LINE1R_2_RADC_CTRL, 2, 0),
	SND_SOC_DAPM_MIXER("Right PGA Mixer", SND_SOC_NOPM, 0, 0,
			   &aic3x_right_pga_mixer_controls[0],
			   ARRAY_SIZE(aic3x_right_pga_mixer_controls)),
	SND_SOC_DAPM_MUX("Right Line1L Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_right_line1_mux_controls),
	SND_SOC_DAPM_MUX("Right Line1R Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_right_line1_mux_controls),
	SND_SOC_DAPM_MUX("Right Line2R Mux", SND_SOC_NOPM, 0, 0,
			 &aic3x_right_line2_mux_controls),

	/*
	 * Not a real mic bias widget but similar function. This is for dynamic
	 * control of GPIO1 digital mic modulator clock output function when
	 * using digital mic.
	 */
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "GPIO1 dmic modclk",
			 AIC3X_GPIO1_REG, 4, 0xf,
			 AIC3X_GPIO1_FUNC_DIGITAL_MIC_MODCLK,
			 AIC3X_GPIO1_FUNC_DISABLED),

	/*
	 * Also similar function like mic bias. Selects digital mic with
	 * configurable oversampling rate instead of ADC converter.
	 */
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "DMic Rate 128",
			 AIC3X_ASD_INTF_CTRLA, 0, 3, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "DMic Rate 64",
			 AIC3X_ASD_INTF_CTRLA, 0, 3, 2, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "DMic Rate 32",
			 AIC3X_ASD_INTF_CTRLA, 0, 3, 3, 0),

	/* Mic Bias */
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "Mic Bias 2V",
			 MICBIAS_CTRL, 6, 3, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "Mic Bias 2.5V",
			 MICBIAS_CTRL, 6, 3, 2, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_micbias, "Mic Bias AVDD",
			 MICBIAS_CTRL, 6, 3, 3, 0),

	/* Left PGA to Left Output bypass */
	SND_SOC_DAPM_MIXER("Left PGA Bypass Mixer", SND_SOC_NOPM, 0, 0,
			   &aic3x_left_pga_bp_mixer_controls[0],
			   ARRAY_SIZE(aic3x_left_pga_bp_mixer_controls)),

	/* Right PGA to Right Output bypass */
	SND_SOC_DAPM_MIXER("Right PGA Bypass Mixer", SND_SOC_NOPM, 0, 0,
			   &aic3x_right_pga_bp_mixer_controls[0],
			   ARRAY_SIZE(aic3x_right_pga_bp_mixer_controls)),

	/* Left Line2 to Left Output bypass */
	SND_SOC_DAPM_MIXER("Left Line2 Bypass Mixer", SND_SOC_NOPM, 0, 0,
			   &aic3x_left_line2_bp_mixer_controls[0],
			   ARRAY_SIZE(aic3x_left_line2_bp_mixer_controls)),

	/* Right Line2 to Right Output bypass */
	SND_SOC_DAPM_MIXER("Right Line2 Bypass Mixer", SND_SOC_NOPM, 0, 0,
			   &aic3x_right_line2_bp_mixer_controls[0],
			   ARRAY_SIZE(aic3x_right_line2_bp_mixer_controls)),

	SND_SOC_DAPM_OUTPUT("LLOUT"),
	SND_SOC_DAPM_OUTPUT("RLOUT"),
	SND_SOC_DAPM_OUTPUT("MONO_LOUT"),
	SND_SOC_DAPM_OUTPUT("HPLOUT"),
	SND_SOC_DAPM_OUTPUT("HPROUT"),
	SND_SOC_DAPM_OUTPUT("HPLCOM"),
	SND_SOC_DAPM_OUTPUT("HPRCOM"),

	SND_SOC_DAPM_INPUT("MIC3L"),
	SND_SOC_DAPM_INPUT("MIC3R"),
	SND_SOC_DAPM_INPUT("LINE1L"),
	SND_SOC_DAPM_INPUT("LINE1R"),
	SND_SOC_DAPM_INPUT("LINE2L"),
	SND_SOC_DAPM_INPUT("LINE2R"),
};

static const struct snd_soc_dapm_route intercon[] = {
	/* Left Output */
	{"Left DAC Mux", "DAC_L1", "Left DAC"},
	{"Left DAC Mux", "DAC_L2", "Left DAC"},
	{"Left DAC Mux", "DAC_L3", "Left DAC"},

	{"Left DAC_L1 Mixer", "LineL Switch", "Left DAC Mux"},
	{"Left DAC_L1 Mixer", "LineR Switch", "Left DAC Mux"},
	{"Left DAC_L1 Mixer", "Mono Switch", "Left DAC Mux"},
	{"Left DAC_L1 Mixer", "HP Switch", "Left DAC Mux"},
	{"Left DAC_L1 Mixer", "HPCOM Switch", "Left DAC Mux"},
	{"Left Line Out", NULL, "Left DAC Mux"},
	{"Left HP Out", NULL, "Left DAC Mux"},

	{"Left HPCOM Mux", "differential of HPLOUT", "Left DAC_L1 Mixer"},
	{"Left HPCOM Mux", "constant VCM", "Left DAC_L1 Mixer"},
	{"Left HPCOM Mux", "single-ended", "Left DAC_L1 Mixer"},

	{"Left Line Out", NULL, "Left DAC_L1 Mixer"},
	{"Mono Out", NULL, "Left DAC_L1 Mixer"},
	{"Left HP Out", NULL, "Left DAC_L1 Mixer"},
	{"Left HP Com", NULL, "Left HPCOM Mux"},

	{"LLOUT", NULL, "Left Line Out"},
	{"LLOUT", NULL, "Left Line Out"},
	{"HPLOUT", NULL, "Left HP Out"},
	{"HPLCOM", NULL, "Left HP Com"},

	/* Right Output */
	{"Right DAC Mux", "DAC_R1", "Right DAC"},
	{"Right DAC Mux", "DAC_R2", "Right DAC"},
	{"Right DAC Mux", "DAC_R3", "Right DAC"},

	{"Right DAC_R1 Mixer", "LineL Switch", "Right DAC Mux"},
	{"Right DAC_R1 Mixer", "LineR Switch", "Right DAC Mux"},
	{"Right DAC_R1 Mixer", "Mono Switch", "Right DAC Mux"},
	{"Right DAC_R1 Mixer", "HP Switch", "Right DAC Mux"},
	{"Right DAC_R1 Mixer", "HPCOM Switch", "Right DAC Mux"},
	{"Right Line Out", NULL, "Right DAC Mux"},
	{"Right HP Out", NULL, "Right DAC Mux"},

	{"Right HPCOM Mux", "differential of HPROUT", "Right DAC_R1 Mixer"},
	{"Right HPCOM Mux", "constant VCM", "Right DAC_R1 Mixer"},
	{"Right HPCOM Mux", "single-ended", "Right DAC_R1 Mixer"},
	{"Right HPCOM Mux", "differential of HPLCOM", "Right DAC_R1 Mixer"},
	{"Right HPCOM Mux", "external feedback", "Right DAC_R1 Mixer"},

	{"Right Line Out", NULL, "Right DAC_R1 Mixer"},
	{"Mono Out", NULL, "Right DAC_R1 Mixer"},
	{"Right HP Out", NULL, "Right DAC_R1 Mixer"},
	{"Right HP Com", NULL, "Right HPCOM Mux"},

	{"RLOUT", NULL, "Right Line Out"},
	{"RLOUT", NULL, "Right Line Out"},
	{"HPROUT", NULL, "Right HP Out"},
	{"HPRCOM", NULL, "Right HP Com"},

	/* Mono Output */
	{"MONO_LOUT", NULL, "Mono Out"},
	{"MONO_LOUT", NULL, "Mono Out"},

	/* Left Input */
	{"Left Line1L Mux", "single-ended", "LINE1L"},
	{"Left Line1L Mux", "differential", "LINE1L"},

	{"Left Line2L Mux", "single-ended", "LINE2L"},
	{"Left Line2L Mux", "differential", "LINE2L"},

	{"Left PGA Mixer", "Line1L Switch", "Left Line1L Mux"},
	{"Left PGA Mixer", "Line1R Switch", "Left Line1R Mux"},
	{"Left PGA Mixer", "Line2L Switch", "Left Line2L Mux"},
	{"Left PGA Mixer", "Mic3L Switch", "MIC3L"},
	{"Left PGA Mixer", "Mic3R Switch", "MIC3R"},

	{"Left ADC", NULL, "Left PGA Mixer"},
	{"Left ADC", NULL, "GPIO1 dmic modclk"},

	/* Right Input */
	{"Right Line1R Mux", "single-ended", "LINE1R"},
	{"Right Line1R Mux", "differential", "LINE1R"},

	{"Right Line2R Mux", "single-ended", "LINE2R"},
	{"Right Line2R Mux", "differential", "LINE2R"},

	{"Right PGA Mixer", "Line1L Switch", "Right Line1L Mux"},
	{"Right PGA Mixer", "Line1R Switch", "Right Line1R Mux"},
	{"Right PGA Mixer", "Line2R Switch", "Right Line2R Mux"},
	{"Right PGA Mixer", "Mic3L Switch", "MIC3L"},
	{"Right PGA Mixer", "Mic3R Switch", "MIC3R"},

	{"Right ADC", NULL, "Right PGA Mixer"},
	{"Right ADC", NULL, "GPIO1 dmic modclk"},

	/* Left PGA Bypass */
	{"Left PGA Bypass Mixer", "LineL Switch", "Left PGA Mixer"},
	{"Left PGA Bypass Mixer", "LineR Switch", "Left PGA Mixer"},
	{"Left PGA Bypass Mixer", "Mono Switch", "Left PGA Mixer"},
	{"Left PGA Bypass Mixer", "HPL Switch", "Left PGA Mixer"},
	{"Left PGA Bypass Mixer", "HPR Switch", "Left PGA Mixer"},
	{"Left PGA Bypass Mixer", "HPLCOM Switch", "Left PGA Mixer"},
	{"Left PGA Bypass Mixer", "HPRCOM Switch", "Left PGA Mixer"},

	{"Left HPCOM Mux", "differential of HPLOUT", "Left PGA Bypass Mixer"},
	{"Left HPCOM Mux", "constant VCM", "Left PGA Bypass Mixer"},
	{"Left HPCOM Mux", "single-ended", "Left PGA Bypass Mixer"},

	{"Left Line Out", NULL, "Left PGA Bypass Mixer"},
	{"Mono Out", NULL, "Left PGA Bypass Mixer"},
	{"Left HP Out", NULL, "Left PGA Bypass Mixer"},

	/* Right PGA Bypass */
	{"Right PGA Bypass Mixer", "LineL Switch", "Right PGA Mixer"},
	{"Right PGA Bypass Mixer", "LineR Switch", "Right PGA Mixer"},
	{"Right PGA Bypass Mixer", "Mono Switch", "Right PGA Mixer"},
	{"Right PGA Bypass Mixer", "HPL Switch", "Right PGA Mixer"},
	{"Right PGA Bypass Mixer", "HPR Switch", "Right PGA Mixer"},
	{"Right PGA Bypass Mixer", "HPLCOM Switch", "Right PGA Mixer"},
	{"Right PGA Bypass Mixer", "HPRCOM Switch", "Right PGA Mixer"},

	{"Right HPCOM Mux", "differential of HPROUT", "Right PGA Bypass Mixer"},
	{"Right HPCOM Mux", "constant VCM", "Right PGA Bypass Mixer"},
	{"Right HPCOM Mux", "single-ended", "Right PGA Bypass Mixer"},
	{"Right HPCOM Mux", "differential of HPLCOM", "Right PGA Bypass Mixer"},
	{"Right HPCOM Mux", "external feedback", "Right PGA Bypass Mixer"},

	{"Right Line Out", NULL, "Right PGA Bypass Mixer"},
	{"Mono Out", NULL, "Right PGA Bypass Mixer"},
	{"Right HP Out", NULL, "Right PGA Bypass Mixer"},

	/* Left Line2 Bypass */
	{"Left Line2 Bypass Mixer", "LineL Switch", "Left Line2L Mux"},
	{"Left Line2 Bypass Mixer", "LineR Switch", "Left Line2L Mux"},
	{"Left Line2 Bypass Mixer", "Mono Switch", "Left Line2L Mux"},
	{"Left Line2 Bypass Mixer", "HP Switch", "Left Line2L Mux"},
	{"Left Line2 Bypass Mixer", "HPLCOM Switch", "Left Line2L Mux"},

	{"Left HPCOM Mux", "differential of HPLOUT", "Left Line2 Bypass Mixer"},
	{"Left HPCOM Mux", "constant VCM", "Left Line2 Bypass Mixer"},
	{"Left HPCOM Mux", "single-ended", "Left Line2 Bypass Mixer"},

	{"Left Line Out", NULL, "Left Line2 Bypass Mixer"},
	{"Mono Out", NULL, "Left Line2 Bypass Mixer"},
	{"Left HP Out", NULL, "Left Line2 Bypass Mixer"},

	/* Right Line2 Bypass */
	{"Right Line2 Bypass Mixer", "LineL Switch", "Right Line2R Mux"},
	{"Right Line2 Bypass Mixer", "LineR Switch", "Right Line2R Mux"},
	{"Right Line2 Bypass Mixer", "Mono Switch", "Right Line2R Mux"},
	{"Right Line2 Bypass Mixer", "HP Switch", "Right Line2R Mux"},
	{"Right Line2 Bypass Mixer", "HPRCOM Switch", "Right Line2R Mux"},

	{"Right HPCOM Mux", "differential of HPROUT", "Right Line2 Bypass Mixer"},
	{"Right HPCOM Mux", "constant VCM", "Right Line2 Bypass Mixer"},
	{"Right HPCOM Mux", "single-ended", "Right Line2 Bypass Mixer"},
	{"Right HPCOM Mux", "differential of HPLCOM", "Right Line2 Bypass Mixer"},
	{"Right HPCOM Mux", "external feedback", "Right Line2 Bypass Mixer"},

	{"Right Line Out", NULL, "Right Line2 Bypass Mixer"},
	{"Mono Out", NULL, "Right Line2 Bypass Mixer"},
	{"Right HP Out", NULL, "Right Line2 Bypass Mixer"},

	/*
	 * Logical path between digital mic enable and GPIO1 modulator clock
	 * output function
	 */
	{"GPIO1 dmic modclk", NULL, "DMic Rate 128"},
	{"GPIO1 dmic modclk", NULL, "DMic Rate 64"},
	{"GPIO1 dmic modclk", NULL, "DMic Rate 32"},
};

static int aic3x_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, aic3x_dapm_widgets,
				  ARRAY_SIZE(aic3x_dapm_widgets));

	/* set up audio path interconnects */
	snd_soc_dapm_add_routes(codec, intercon, ARRAY_SIZE(intercon));

	return 0;
}

static int aic3x_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct aic3x_priv *aic3x = codec->private_data;
	int codec_clk = 0, bypass_pll = 0, fsref, last_clk = 0;
	u8 data, r, p, pll_q, pll_p = 1, pll_r = 1, pll_j = 1;
	u16 pll_d = 1;
	u8 reg;

	/* select data word length */
	data =
	    aic3x_read_reg_cache(codec, AIC3X_ASD_INTF_CTRLB) & (~(0x3 << 4));
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		data |= (0x01 << 4);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data |= (0x02 << 4);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		data |= (0x03 << 4);
		break;
	}
	aic3x_write(codec, AIC3X_ASD_INTF_CTRLB, data);

	/* Fsref can be 44100 or 48000 */
	fsref = (params_rate(params) % 11025 == 0) ? 44100 : 48000;

	/* Try to find a value for Q which allows us to bypass the PLL and
	 * generate CODEC_CLK directly. */
	for (pll_q = 2; pll_q < 18; pll_q++)
		if (aic3x->sysclk / (128 * pll_q) == fsref) {
			bypass_pll = 1;
			break;
		}

	if (bypass_pll) {
		pll_q &= 0xf;
		aic3x_write(codec, AIC3X_PLL_PROGA_REG, pll_q << PLLQ_SHIFT);
		aic3x_write(codec, AIC3X_GPIOB_REG, CODEC_CLKIN_CLKDIV);
		/* disable PLL if it is bypassed */
		reg = aic3x_read_reg_cache(codec, AIC3X_PLL_PROGA_REG);
		aic3x_write(codec, AIC3X_PLL_PROGA_REG, reg & ~PLL_ENABLE);

	} else {
		aic3x_write(codec, AIC3X_GPIOB_REG, CODEC_CLKIN_PLLDIV);
		/* enable PLL when it is used */
		reg = aic3x_read_reg_cache(codec, AIC3X_PLL_PROGA_REG);
		aic3x_write(codec, AIC3X_PLL_PROGA_REG, reg | PLL_ENABLE);
	}

	/* Route Left DAC to left channel input and
	 * right DAC to right channel input */
	data = (LDAC2LCH | RDAC2RCH);
	data |= (fsref == 44100) ? FSREF_44100 : FSREF_48000;
	if (params_rate(params) >= 64000)
		data |= DUAL_RATE_MODE;
	aic3x_write(codec, AIC3X_CODEC_DATAPATH_REG, data);

	/* codec sample rate select */
	data = (fsref * 20) / params_rate(params);
	if (params_rate(params) < 64000)
		data /= 2;
	data /= 5;
	data -= 2;
	data |= (data << 4);
	aic3x_write(codec, AIC3X_SAMPLE_RATE_SEL_REG, data);

	if (bypass_pll)
		return 0;

	/* Use PLL
	 * find an apropriate setup for j, d, r and p by iterating over
	 * p and r - j and d are calculated for each fraction.
	 * Up to 128 values are probed, the closest one wins the game.
	 * The sysclk is divided by 1000 to prevent integer overflows.
	 */
	codec_clk = (2048 * fsref) / (aic3x->sysclk / 1000);

	for (r = 1; r <= 16; r++)
		for (p = 1; p <= 8; p++) {
			int clk, tmp = (codec_clk * pll_r * 10) / pll_p;
			u8 j = tmp / 10000;
			u16 d = tmp % 10000;

			if (j > 63)
				continue;

			if (d != 0 && aic3x->sysclk < 10000000)
				continue;

			/* This is actually 1000 * ((j + (d/10000)) * r) / p
			 * The term had to be converted to get rid of the
			 * division by 10000 */
			clk = ((10000 * j * r) + (d * r)) / (10 * p);

			/* check whether this values get closer than the best
			 * ones we had before */
			if (abs(codec_clk - clk) < abs(codec_clk - last_clk)) {
				pll_j = j; pll_d = d; pll_r = r; pll_p = p;
				last_clk = clk;
			}

			/* Early exit for exact matches */
			if (clk == codec_clk)
				break;
		}

	if (last_clk == 0) {
		printk(KERN_ERR "%s(): unable to setup PLL\n", __func__);
		return -EINVAL;
	}

	data = aic3x_read_reg_cache(codec, AIC3X_PLL_PROGA_REG);
	aic3x_write(codec, AIC3X_PLL_PROGA_REG, data | (pll_p << PLLP_SHIFT));
	aic3x_write(codec, AIC3X_OVRF_STATUS_AND_PLLR_REG, pll_r << PLLR_SHIFT);
	aic3x_write(codec, AIC3X_PLL_PROGB_REG, pll_j << PLLJ_SHIFT);
	aic3x_write(codec, AIC3X_PLL_PROGC_REG, (pll_d >> 6) << PLLD_MSB_SHIFT);
	aic3x_write(codec, AIC3X_PLL_PROGD_REG,
		    (pll_d & 0x3F) << PLLD_LSB_SHIFT);

	return 0;
}

static int aic3x_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 ldac_reg = aic3x_read_reg_cache(codec, LDAC_VOL) & ~MUTE_ON;
	u8 rdac_reg = aic3x_read_reg_cache(codec, RDAC_VOL) & ~MUTE_ON;

	if (mute) {
		aic3x_write(codec, LDAC_VOL, ldac_reg | MUTE_ON);
		aic3x_write(codec, RDAC_VOL, rdac_reg | MUTE_ON);
	} else {
		aic3x_write(codec, LDAC_VOL, ldac_reg);
		aic3x_write(codec, RDAC_VOL, rdac_reg);
	}

	return 0;
}

static int aic3x_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct aic3x_priv *aic3x = codec->private_data;

	aic3x->sysclk = freq;
	return 0;
}

static int aic3x_set_dai_fmt(struct snd_soc_dai *codec_dai,
			     unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct aic3x_priv *aic3x = codec->private_data;
	u8 iface_areg, iface_breg;
	int delay = 0;

	iface_areg = aic3x_read_reg_cache(codec, AIC3X_ASD_INTF_CTRLA) & 0x3f;
	iface_breg = aic3x_read_reg_cache(codec, AIC3X_ASD_INTF_CTRLB) & 0x3f;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		aic3x->master = 1;
		iface_areg |= BIT_CLK_MASTER | WORD_CLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		aic3x->master = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * match both interface format and signal polarities since they
	 * are fixed
	 */
	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK |
		       SND_SOC_DAIFMT_INV_MASK)) {
	case (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF):
		break;
	case (SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF):
		delay = 1;
	case (SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_IB_NF):
		iface_breg |= (0x01 << 6);
		break;
	case (SND_SOC_DAIFMT_RIGHT_J | SND_SOC_DAIFMT_NB_NF):
		iface_breg |= (0x02 << 6);
		break;
	case (SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF):
		iface_breg |= (0x03 << 6);
		break;
	default:
		return -EINVAL;
	}

	/* set iface */
	aic3x_write(codec, AIC3X_ASD_INTF_CTRLA, iface_areg);
	aic3x_write(codec, AIC3X_ASD_INTF_CTRLB, iface_breg);
	aic3x_write(codec, AIC3X_ASD_INTF_CTRLC, delay);

	return 0;
}

static int aic3x_set_bias_level(struct snd_soc_codec *codec,
				enum snd_soc_bias_level level)
{
	struct aic3x_priv *aic3x = codec->private_data;
	u8 reg;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* all power is driven by DAPM system */
		if (aic3x->master) {
			/* enable pll */
			reg = aic3x_read_reg_cache(codec, AIC3X_PLL_PROGA_REG);
			aic3x_write(codec, AIC3X_PLL_PROGA_REG,
				    reg | PLL_ENABLE);
		}
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/*
		 * all power is driven by DAPM system,
		 * so output power is safe if bypass was set
		 */
		if (aic3x->master) {
			/* disable pll */
			reg = aic3x_read_reg_cache(codec, AIC3X_PLL_PROGA_REG);
			aic3x_write(codec, AIC3X_PLL_PROGA_REG,
				    reg & ~PLL_ENABLE);
		}
		break;
	case SND_SOC_BIAS_OFF:
		/* force all power off */
		reg = aic3x_read_reg_cache(codec, LINE1L_2_LADC_CTRL);
		aic3x_write(codec, LINE1L_2_LADC_CTRL, reg & ~LADC_PWR_ON);
		reg = aic3x_read_reg_cache(codec, LINE1R_2_RADC_CTRL);
		aic3x_write(codec, LINE1R_2_RADC_CTRL, reg & ~RADC_PWR_ON);

		reg = aic3x_read_reg_cache(codec, DAC_PWR);
		aic3x_write(codec, DAC_PWR, reg & ~(LDAC_PWR_ON | RDAC_PWR_ON));

		reg = aic3x_read_reg_cache(codec, HPLOUT_CTRL);
		aic3x_write(codec, HPLOUT_CTRL, reg & ~HPLOUT_PWR_ON);
		reg = aic3x_read_reg_cache(codec, HPROUT_CTRL);
		aic3x_write(codec, HPROUT_CTRL, reg & ~HPROUT_PWR_ON);

		reg = aic3x_read_reg_cache(codec, HPLCOM_CTRL);
		aic3x_write(codec, HPLCOM_CTRL, reg & ~HPLCOM_PWR_ON);
		reg = aic3x_read_reg_cache(codec, HPRCOM_CTRL);
		aic3x_write(codec, HPRCOM_CTRL, reg & ~HPRCOM_PWR_ON);

		reg = aic3x_read_reg_cache(codec, MONOLOPM_CTRL);
		aic3x_write(codec, MONOLOPM_CTRL, reg & ~MONOLOPM_PWR_ON);

		reg = aic3x_read_reg_cache(codec, LLOPM_CTRL);
		aic3x_write(codec, LLOPM_CTRL, reg & ~LLOPM_PWR_ON);
		reg = aic3x_read_reg_cache(codec, RLOPM_CTRL);
		aic3x_write(codec, RLOPM_CTRL, reg & ~RLOPM_PWR_ON);

		if (aic3x->master) {
			/* disable pll */
			reg = aic3x_read_reg_cache(codec, AIC3X_PLL_PROGA_REG);
			aic3x_write(codec, AIC3X_PLL_PROGA_REG,
				    reg & ~PLL_ENABLE);
		}
		break;
	}
	codec->bias_level = level;

	return 0;
}

void aic3x_set_gpio(struct snd_soc_codec *codec, int gpio, int state)
{
	u8 reg = gpio ? AIC3X_GPIO2_REG : AIC3X_GPIO1_REG;
	u8 bit = gpio ? 3: 0;
	u8 val = aic3x_read_reg_cache(codec, reg) & ~(1 << bit);
	aic3x_write(codec, reg, val | (!!state << bit));
}
EXPORT_SYMBOL_GPL(aic3x_set_gpio);

int aic3x_get_gpio(struct snd_soc_codec *codec, int gpio)
{
	u8 reg = gpio ? AIC3X_GPIO2_REG : AIC3X_GPIO1_REG;
	u8 val, bit = gpio ? 2: 1;

	aic3x_read(codec, reg, &val);
	return (val >> bit) & 1;
}
EXPORT_SYMBOL_GPL(aic3x_get_gpio);

void aic3x_set_headset_detection(struct snd_soc_codec *codec, int detect,
				 int headset_debounce, int button_debounce)
{
	u8 val;

	val = ((detect & AIC3X_HEADSET_DETECT_MASK)
		<< AIC3X_HEADSET_DETECT_SHIFT) |
	      ((headset_debounce & AIC3X_HEADSET_DEBOUNCE_MASK)
		<< AIC3X_HEADSET_DEBOUNCE_SHIFT) |
	      ((button_debounce & AIC3X_BUTTON_DEBOUNCE_MASK)
		<< AIC3X_BUTTON_DEBOUNCE_SHIFT);

	if (detect & AIC3X_HEADSET_DETECT_MASK)
		val |= AIC3X_HEADSET_DETECT_ENABLED;

	aic3x_write(codec, AIC3X_HEADSET_DETECT_CTRL_A, val);
}
EXPORT_SYMBOL_GPL(aic3x_set_headset_detection);

int aic3x_headset_detected(struct snd_soc_codec *codec)
{
	u8 val;
	aic3x_read(codec, AIC3X_HEADSET_DETECT_CTRL_B, &val);
	return (val >> 4) & 1;
}
EXPORT_SYMBOL_GPL(aic3x_headset_detected);

int aic3x_button_pressed(struct snd_soc_codec *codec)
{
	u8 val;
	aic3x_read(codec, AIC3X_HEADSET_DETECT_CTRL_B, &val);
	return (val >> 5) & 1;
}
EXPORT_SYMBOL_GPL(aic3x_button_pressed);

#define AIC3X_RATES	SNDRV_PCM_RATE_8000_96000
#define AIC3X_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops aic3x_dai_ops = {
	.hw_params	= aic3x_hw_params,
	.digital_mute	= aic3x_mute,
	.set_sysclk	= aic3x_set_dai_sysclk,
	.set_fmt	= aic3x_set_dai_fmt,
};

struct snd_soc_dai aic3x_dai = {
	.name = "tlv320aic3x",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AIC3X_RATES,
		.formats = AIC3X_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AIC3X_RATES,
		.formats = AIC3X_FORMATS,},
	.ops = &aic3x_dai_ops,
};
EXPORT_SYMBOL_GPL(aic3x_dai);

static int aic3x_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	aic3x_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int aic3x_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	int i;
	u8 data[2];
	u8 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(aic3x_reg); i++) {
		data[0] = i;
		data[1] = cache[i];
		codec->hw_write(codec->control_data, data, 2);
	}

	aic3x_set_bias_level(codec, codec->suspend_bias_level);

	return 0;
}

/*
 * initialise the AIC3X driver
 * register the mixer and dsp interfaces with the kernel
 */
static int aic3x_init(struct snd_soc_codec *codec)
{
	int reg;

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->name = "tlv320aic3x";
	codec->owner = THIS_MODULE;
	codec->read = aic3x_read_reg_cache;
	codec->write = aic3x_write;
	codec->set_bias_level = aic3x_set_bias_level;
	codec->dai = &aic3x_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = ARRAY_SIZE(aic3x_reg);
	codec->reg_cache = kmemdup(aic3x_reg, sizeof(aic3x_reg), GFP_KERNEL);
	if (codec->reg_cache == NULL)
		return -ENOMEM;

	aic3x_write(codec, AIC3X_PAGE_SELECT, PAGE0_SELECT);
	aic3x_write(codec, AIC3X_RESET, SOFT_RESET);

	/* DAC default volume and mute */
	aic3x_write(codec, LDAC_VOL, DEFAULT_VOL | MUTE_ON);
	aic3x_write(codec, RDAC_VOL, DEFAULT_VOL | MUTE_ON);

	/* DAC to HP default volume and route to Output mixer */
	aic3x_write(codec, DACL1_2_HPLOUT_VOL, DEFAULT_VOL | ROUTE_ON);
	aic3x_write(codec, DACR1_2_HPROUT_VOL, DEFAULT_VOL | ROUTE_ON);
	aic3x_write(codec, DACL1_2_HPLCOM_VOL, DEFAULT_VOL | ROUTE_ON);
	aic3x_write(codec, DACR1_2_HPRCOM_VOL, DEFAULT_VOL | ROUTE_ON);
	/* DAC to Line Out default volume and route to Output mixer */
	aic3x_write(codec, DACL1_2_LLOPM_VOL, DEFAULT_VOL | ROUTE_ON);
	aic3x_write(codec, DACR1_2_RLOPM_VOL, DEFAULT_VOL | ROUTE_ON);
	/* DAC to Mono Line Out default volume and route to Output mixer */
	aic3x_write(codec, DACL1_2_MONOLOPM_VOL, DEFAULT_VOL | ROUTE_ON);
	aic3x_write(codec, DACR1_2_MONOLOPM_VOL, DEFAULT_VOL | ROUTE_ON);

	/* unmute all outputs */
	reg = aic3x_read_reg_cache(codec, LLOPM_CTRL);
	aic3x_write(codec, LLOPM_CTRL, reg | UNMUTE);
	reg = aic3x_read_reg_cache(codec, RLOPM_CTRL);
	aic3x_write(codec, RLOPM_CTRL, reg | UNMUTE);
	reg = aic3x_read_reg_cache(codec, MONOLOPM_CTRL);
	aic3x_write(codec, MONOLOPM_CTRL, reg | UNMUTE);
	reg = aic3x_read_reg_cache(codec, HPLOUT_CTRL);
	aic3x_write(codec, HPLOUT_CTRL, reg | UNMUTE);
	reg = aic3x_read_reg_cache(codec, HPROUT_CTRL);
	aic3x_write(codec, HPROUT_CTRL, reg | UNMUTE);
	reg = aic3x_read_reg_cache(codec, HPLCOM_CTRL);
	aic3x_write(codec, HPLCOM_CTRL, reg | UNMUTE);
	reg = aic3x_read_reg_cache(codec, HPRCOM_CTRL);
	aic3x_write(codec, HPRCOM_CTRL, reg | UNMUTE);

	/* ADC default volume and unmute */
	aic3x_write(codec, LADC_VOL, DEFAULT_GAIN);
	aic3x_write(codec, RADC_VOL, DEFAULT_GAIN);
	/* By default route Line1 to ADC PGA mixer */
	aic3x_write(codec, LINE1L_2_LADC_CTRL, 0x0);
	aic3x_write(codec, LINE1R_2_RADC_CTRL, 0x0);

	/* PGA to HP Bypass default volume, disconnect from Output Mixer */
	aic3x_write(codec, PGAL_2_HPLOUT_VOL, DEFAULT_VOL);
	aic3x_write(codec, PGAR_2_HPROUT_VOL, DEFAULT_VOL);
	aic3x_write(codec, PGAL_2_HPLCOM_VOL, DEFAULT_VOL);
	aic3x_write(codec, PGAR_2_HPRCOM_VOL, DEFAULT_VOL);
	/* PGA to Line Out default volume, disconnect from Output Mixer */
	aic3x_write(codec, PGAL_2_LLOPM_VOL, DEFAULT_VOL);
	aic3x_write(codec, PGAR_2_RLOPM_VOL, DEFAULT_VOL);
	/* PGA to Mono Line Out default volume, disconnect from Output Mixer */
	aic3x_write(codec, PGAL_2_MONOLOPM_VOL, DEFAULT_VOL);
	aic3x_write(codec, PGAR_2_MONOLOPM_VOL, DEFAULT_VOL);

	/* Line2 to HP Bypass default volume, disconnect from Output Mixer */
	aic3x_write(codec, LINE2L_2_HPLOUT_VOL, DEFAULT_VOL);
	aic3x_write(codec, LINE2R_2_HPROUT_VOL, DEFAULT_VOL);
	aic3x_write(codec, LINE2L_2_HPLCOM_VOL, DEFAULT_VOL);
	aic3x_write(codec, LINE2R_2_HPRCOM_VOL, DEFAULT_VOL);
	/* Line2 Line Out default volume, disconnect from Output Mixer */
	aic3x_write(codec, LINE2L_2_LLOPM_VOL, DEFAULT_VOL);
	aic3x_write(codec, LINE2R_2_RLOPM_VOL, DEFAULT_VOL);
	/* Line2 to Mono Out default volume, disconnect from Output Mixer */
	aic3x_write(codec, LINE2L_2_MONOLOPM_VOL, DEFAULT_VOL);
	aic3x_write(codec, LINE2R_2_MONOLOPM_VOL, DEFAULT_VOL);

	/* off, with power on */
	aic3x_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

static struct snd_soc_codec *aic3x_codec;

static int aic3x_register(struct snd_soc_codec *codec)
{
	int ret;

	ret = aic3x_init(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to initialise device\n");
		return ret;
	}

	aic3x_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret) {
		dev_err(codec->dev, "Failed to register codec\n");
		return ret;
	}

	ret = snd_soc_register_dai(&aic3x_dai);
	if (ret) {
		dev_err(codec->dev, "Failed to register dai\n");
		snd_soc_unregister_codec(codec);
		return ret;
	}

	return 0;
}

static int aic3x_unregister(struct aic3x_priv *aic3x)
{
	aic3x_set_bias_level(&aic3x->codec, SND_SOC_BIAS_OFF);

	snd_soc_unregister_dai(&aic3x_dai);
	snd_soc_unregister_codec(&aic3x->codec);

	kfree(aic3x);
	aic3x_codec = NULL;

	return 0;
}

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
/*
 * AIC3X 2 wire address can be up to 4 devices with device addresses
 * 0x18, 0x19, 0x1A, 0x1B
 */

/*
 * If the i2c layer weren't so broken, we could pass this kind of data
 * around
 */
static int aic3x_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct snd_soc_codec *codec;
	struct aic3x_priv *aic3x;

	aic3x = kzalloc(sizeof(struct aic3x_priv), GFP_KERNEL);
	if (aic3x == NULL) {
		dev_err(&i2c->dev, "failed to create private data\n");
		return -ENOMEM;
	}

	codec = &aic3x->codec;
	codec->dev = &i2c->dev;
	codec->private_data = aic3x;
	codec->control_data = i2c;
	codec->hw_write = (hw_write_t) i2c_master_send;

	i2c_set_clientdata(i2c, aic3x);

	return aic3x_register(codec);
}

static int aic3x_i2c_remove(struct i2c_client *client)
{
	struct aic3x_priv *aic3x = i2c_get_clientdata(client);

	return aic3x_unregister(aic3x);
}

static const struct i2c_device_id aic3x_i2c_id[] = {
	{ "tlv320aic3x", 0 },
	{ "tlv320aic33", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aic3x_i2c_id);

/* machine i2c codec control layer */
static struct i2c_driver aic3x_i2c_driver = {
	.driver = {
		.name = "aic3x I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe	= aic3x_i2c_probe,
	.remove = aic3x_i2c_remove,
	.id_table = aic3x_i2c_id,
};

static inline void aic3x_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&aic3x_i2c_driver);
	if (ret)
		printk(KERN_ERR "%s: error regsitering i2c driver, %d\n",
		       __func__, ret);
}

static inline void aic3x_i2c_exit(void)
{
	i2c_del_driver(&aic3x_i2c_driver);
}
#else
static inline void aic3x_i2c_init(void) { }
static inline void aic3x_i2c_exit(void) { }
#endif

static int aic3x_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct aic3x_setup_data *setup;
	struct snd_soc_codec *codec;
	int ret = 0;

	codec = aic3x_codec;
	if (!codec) {
		dev_err(&pdev->dev, "Codec not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = codec;
	setup = socdev->codec_data;

	if (setup) {
		/* setup GPIO functions */
		aic3x_write(codec, AIC3X_GPIO1_REG,
			    (setup->gpio_func[0] & 0xf) << 4);
		aic3x_write(codec, AIC3X_GPIO2_REG,
			    (setup->gpio_func[1] & 0xf) << 4);
	}

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "aic3x: failed to create pcms\n");
		goto pcm_err;
	}

	snd_soc_add_controls(codec, aic3x_snd_controls,
			     ARRAY_SIZE(aic3x_snd_controls));

	aic3x_add_widgets(codec);

	return ret;

pcm_err:
	kfree(codec->reg_cache);
	return ret;
}

static int aic3x_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	/* power down chip */
	if (codec->control_data)
		aic3x_set_bias_level(codec, SND_SOC_BIAS_OFF);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	kfree(codec->reg_cache);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_aic3x = {
	.probe = aic3x_probe,
	.remove = aic3x_remove,
	.suspend = aic3x_suspend,
	.resume = aic3x_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_aic3x);

static int __init aic3x_modinit(void)
{
	aic3x_i2c_init();

	return 0;
}
module_init(aic3x_modinit);

static void __exit aic3x_exit(void)
{
	aic3x_i2c_exit();
}
module_exit(aic3x_exit);

MODULE_DESCRIPTION("ASoC TLV320AIC3X codec driver");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
