// SPDX-License-Identifier: GPL-2.0+
/*
 * rv1106_codec.c - Rockchip RV1106 SoC Codec Driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/rockchip/grf.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "rv1106_codec.h"

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#define CODEC_DRV_NAME			"rv1106-acodec"

#define PERI_GRF_PERI_CON1		0x004
#define ACODEC_AD2DA_LOOP_MSK		(1 << 23)
#define ACODEC_AD2DA_LOOP_EN		(1 << 7)
#define ACODEC_AD2DA_LOOP_DIS		(1 << 7)
/* Control the i2s sdo sdi interface that connect to internal acodec
 * 1: connect to internal acodec
 * 0: connect to external acodec
 */
#define ACODEC_MSK			(1 << 22)
#define ACODEC_EN			(1 << 6)
#define ACODEC_DIS			(0 << 6)

#define LR(b, x, v)			(((1 << b) & x) ? v : 0)
#define L(x, v)				LR(0, x, v)
#define R(x, v)				LR(1, x, v)

#define ADCL				(1 << 0)
#define ADCR				(1 << 1)

enum soc_id_e {
	SOC_RV1103 = 0x1103,
	SOC_RV1106 = 0x1106,
};

enum adc_mode_e {
	DIFF_ADCL = 0,		/* Differential ADCL, the ADCR is not used */
	SING_ADCL,		/* Single-end ADCL, the ADCR is not used */
	DIFF_ADCR,		/* Differential ADCR, the ADCL is not used */
	SING_ADCR,		/* Single-end ADCR, the ADCL is not used */
	SING_ADCLR,		/* Single-end ADCL and ADCR */
	DIFF_ADCLR,		/* Differential ADCL and ADCR (Not supported on rv1103 codec) */
	ADC_MODE_NUM,
};

struct rv1106_codec_priv {
	const struct device *plat_dev;
	struct device dev;
	struct reset_control *reset;
	struct regmap *regmap;
	struct regmap *grf;
	struct clk *pclk_acodec;
	struct clk *mclk_acodec;
	struct clk *mclk_cpu;
	struct gpio_desc *pa_ctl_gpio;
	struct snd_soc_component *component;

	enum adc_mode_e adc_mode;
	enum soc_id_e soc_id;

	u32 pa_ctl_delay_ms;
	u32 micbias_volt;

	/* AGC L/R Off/on */
	unsigned int agc_l;
	unsigned int agc_r;

	/* AGC L/R Approximate Sample Rate */
	unsigned int agc_asr_l;
	unsigned int agc_asr_r;

	/* ADC MIC Mute/Work */
	unsigned int mic_mute_l;
	unsigned int mic_mute_r;

	/* For the high pass filter */
	unsigned int hpf_cutoff;

	bool adc_enable;
	bool dac_enable;
	bool micbias_enable;
	bool micbias_used;

#if defined(CONFIG_DEBUG_FS)
	struct dentry *dbg_codec;
#endif
};

static const DECLARE_TLV_DB_SCALE(rv1106_codec_alc_agc_gain_tlv,
				  -1800, 150, 2850);
static const DECLARE_TLV_DB_SCALE(rv1106_codec_alc_agc_max_gain_tlv,
				  -1350, 600, 2850);
static const DECLARE_TLV_DB_SCALE(rv1106_codec_alc_agc_min_gain_tlv,
				  -1800, 600, 2400);
static const DECLARE_TLV_DB_SCALE(rv1106_codec_adc_alc_gain_tlv,
				  -900, 150, 3750);
static const DECLARE_TLV_DB_SCALE(rv1106_codec_adc_dig_gain_tlv,
				  -9750, 50, 3000);
static const DECLARE_TLV_DB_SCALE(rv1106_codec_dac_lineout_gain_tlv,
				  -3900, 150, 600);

static const DECLARE_TLV_DB_RANGE(rv1106_codec_adc_mic_gain_tlv,
	1, 1, TLV_DB_SCALE_ITEM(0, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2000, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(1200, 0, 0),
);

static const DECLARE_TLV_DB_RANGE(rv1106_codec_dac_hpmix_gain_tlv,
	1, 2, TLV_DB_SCALE_ITEM(0, 600, 0),
);

static int check_micbias(int volt);

static int rv1106_codec_adc_enable(struct rv1106_codec_priv *rv1106);
static int rv1106_codec_adc_disable(struct rv1106_codec_priv *rv1106);

static int rv1106_codec_micbias_enable(struct rv1106_codec_priv *rv1106,
				       int micbias_volt);
static int rv1106_codec_hpmix_gain_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_hpmix_gain_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_micbias_disable(struct rv1106_codec_priv *rv1106);
static int rv1106_codec_hpf_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_hpf_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_adc_mode_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_adc_mode_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_agc_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_agc_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_agc_asr_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_agc_asr_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_mic_mute_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_mic_mute_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_mic_gain_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_mic_gain_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_micbias_volts_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_micbias_volts_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_main_micbias_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol);
static int rv1106_codec_main_micbias_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol);

static const char *offon_text[2] = {
	[0] = "Off",
	[1] = "On",
};

static const char *mute_text[2] = {
	[0] = "Work",
	[1] = "Mute",
};

/* ADC MICBIAS Volt */
#define MICBIAS_VOLT_NUM		8

#define MICBIAS_VREFx0_8		0
#define MICBIAS_VREFx0_825		1
#define MICBIAS_VREFx0_85		2
#define MICBIAS_VREFx0_875		3
#define MICBIAS_VREFx0_9		4
#define MICBIAS_VREFx0_925		5
#define MICBIAS_VREFx0_95		6
#define MICBIAS_VREFx0_975		7

static const char *micbias_volts_enum_array[MICBIAS_VOLT_NUM] = {
	[MICBIAS_VREFx0_8] = "VREFx0_8",
	[MICBIAS_VREFx0_825] = "VREFx0_825",
	[MICBIAS_VREFx0_85] = "VREFx0_85",
	[MICBIAS_VREFx0_875] = "VREFx0_875",
	[MICBIAS_VREFx0_9] = "VREFx0_9",
	[MICBIAS_VREFx0_925] = "VREFx0_925",
	[MICBIAS_VREFx0_95] = "VREFx0_95",
	[MICBIAS_VREFx0_975] = "VREFx0_975",
};

static const char *adc_mode_enum_array[ADC_MODE_NUM] = {
	[DIFF_ADCL] = "DiffadcL",
	[SING_ADCL] = "SingadcL",
	[DIFF_ADCR] = "DiffadcR",
	[SING_ADCR] = "SingadcR",
	[SING_ADCLR] = "SingadcLR",
	[DIFF_ADCLR] = "DiffadcLR",
};

static const struct soc_enum rv1106_adc_mode_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(adc_mode_enum_array), adc_mode_enum_array),
};

static const struct soc_enum rv1106_micbias_volts_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(micbias_volts_enum_array), micbias_volts_enum_array),
};

/* ADC MICBIAS Main Switch */
static const struct soc_enum rv1106_main_micbias_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(offon_text), offon_text),
};

static const struct soc_enum rv1106_hpf_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(offon_text), offon_text),
};

/* ALC AGC Switch */
static const struct soc_enum rv1106_agc_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(offon_text), offon_text),
	SOC_ENUM_SINGLE(0, 1, ARRAY_SIZE(offon_text), offon_text),
};

/* ADC MIC Mute/Work Switch */
static const struct soc_enum rv1106_mic_mute_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(mute_text), mute_text),
	SOC_ENUM_SINGLE(0, 1, ARRAY_SIZE(mute_text), mute_text),
};

/* ALC AGC Approximate Sample Rate */
#define AGC_ASR_NUM				8

#define AGC_ASR_96KHZ				0
#define AGC_ASR_48KHZ				1
#define AGC_ASR_44_1KHZ				2
#define AGC_ASR_32KHZ				3
#define AGC_ASR_24KHZ				4
#define AGC_ASR_16KHZ				5
#define AGC_ASR_12KHZ				6
#define AGC_ASR_8KHZ				7

static const char *agc_asr_text[AGC_ASR_NUM] = {
	[AGC_ASR_96KHZ] = "96KHz",
	[AGC_ASR_48KHZ] = "48KHz",
	[AGC_ASR_44_1KHZ] = "44.1KHz",
	[AGC_ASR_32KHZ] = "32KHz",
	[AGC_ASR_24KHZ] = "24KHz",
	[AGC_ASR_16KHZ] = "16KHz",
	[AGC_ASR_12KHZ] = "12KHz",
	[AGC_ASR_8KHZ] = "8KHz",
};

static const struct soc_enum rv1106_agc_asr_enum_array[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(agc_asr_text), agc_asr_text),
	SOC_ENUM_SINGLE(0, 1, ARRAY_SIZE(agc_asr_text), agc_asr_text),
};

static const struct snd_kcontrol_new rv1106_codec_dapm_controls[] = {
	/* ADC MIC */
	SOC_SINGLE_EXT_TLV("ADC MIC Left Gain",
			   ACODEC_ADC_ANA_CTL2,
			   ACODEC_ADC_L_MIC_GAIN_SFT,
			   ACODEC_ADC_MIC_GAIN_MAX,
			   0,
			   rv1106_codec_mic_gain_get,
			   rv1106_codec_mic_gain_put,
			   rv1106_codec_adc_mic_gain_tlv),
	SOC_SINGLE_EXT_TLV("ADC MIC Right Gain",
			   ACODEC_ADC_ANA_CTL2,
			   ACODEC_ADC_R_MIC_GAIN_SFT,
			   ACODEC_ADC_MIC_GAIN_MAX,
			   0,
			   rv1106_codec_mic_gain_get,
			   rv1106_codec_mic_gain_put,
			   rv1106_codec_adc_mic_gain_tlv),

	/* ADC ALC */
	SOC_SINGLE_RANGE_TLV("ADC ALC Left Volume",
			     ACODEC_ADC_ANA_CTL4,
			     ACODEC_ADC_L_ALC_GAIN_SFT,
			     ACODEC_ADC_L_ALC_GAIN_MIN,
			     ACODEC_ADC_L_ALC_GAIN_MAX,
			     0, rv1106_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Right Volume",
			     ACODEC_ADC_ANA_CTL5,
			     ACODEC_ADC_R_ALC_GAIN_SFT,
			     ACODEC_ADC_R_ALC_GAIN_MIN,
			     ACODEC_ADC_R_ALC_GAIN_MAX,
			     0, rv1106_codec_adc_alc_gain_tlv),

	/* ADC Digital Volume */
	SOC_SINGLE_RANGE_TLV("ADC Digital Left Volume",
			     ACODEC_ADC_L_DIG_VOL,
			     ACODEC_ADC_L_DIG_VOL_SFT,
			     ACODEC_ADC_L_DIG_VOL_MIN,
			     ACODEC_ADC_L_DIG_VOL_MAX,
			     0, rv1106_codec_adc_dig_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC Digital Right Volume",
			     ACODEC_ADC_R_DIG_VOL,
			     ACODEC_ADC_R_DIG_VOL_SFT,
			     ACODEC_ADC_R_DIG_VOL_MIN,
			     ACODEC_ADC_R_DIG_VOL_MAX,
			     0, rv1106_codec_adc_dig_gain_tlv),

	/* ADC High Pass Filter */
	SOC_ENUM_EXT("ADC HPF Cut-off", rv1106_hpf_enum_array[0],
		     rv1106_codec_hpf_get, rv1106_codec_hpf_put),

	/* ALC AGC Group */
	SOC_SINGLE_RANGE_TLV("ALC AGC Left Volume",
			     ACODEC_ADC_PGA_AGC_L_CTL3,
			     ACODEC_AGC_PGA_GAIN_SFT,
			     ACODEC_AGC_PGA_GAIN_MIN,
			     ACODEC_AGC_PGA_GAIN_MAX,
			     0, rv1106_codec_alc_agc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Right Volume",
			     ACODEC_ADC_PGA_AGC_R_CTL3,
			     ACODEC_AGC_PGA_GAIN_SFT,
			     ACODEC_AGC_PGA_GAIN_MIN,
			     ACODEC_AGC_PGA_GAIN_MAX,
			     0, rv1106_codec_alc_agc_gain_tlv),

	/* ALC AGC MAX */
	SOC_SINGLE_RANGE_TLV("ALC AGC Left Max Volume",
			     ACODEC_ADC_PGA_AGC_L_CTL9,
			     ACODEC_AGC_MAX_GAIN_PGA_SFT,
			     ACODEC_AGC_MAX_GAIN_PGA_MIN,
			     ACODEC_AGC_MAX_GAIN_PGA_MAX,
			     0, rv1106_codec_alc_agc_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Right Max Volume",
			     ACODEC_ADC_PGA_AGC_R_CTL9,
			     ACODEC_AGC_MAX_GAIN_PGA_SFT,
			     ACODEC_AGC_MAX_GAIN_PGA_MIN,
			     ACODEC_AGC_MAX_GAIN_PGA_MAX,
			     0, rv1106_codec_alc_agc_max_gain_tlv),

	/* ALC AGC MIN */
	SOC_SINGLE_RANGE_TLV("ALC AGC Left Min Volume",
			     ACODEC_ADC_PGA_AGC_L_CTL9,
			     ACODEC_AGC_MIN_GAIN_PGA_SFT,
			     ACODEC_AGC_MIN_GAIN_PGA_MIN,
			     ACODEC_AGC_MIN_GAIN_PGA_MAX,
			     0, rv1106_codec_alc_agc_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Right Min Volume",
			     ACODEC_ADC_PGA_AGC_R_CTL9,
			     ACODEC_AGC_MIN_GAIN_PGA_SFT,
			     ACODEC_AGC_MIN_GAIN_PGA_MIN,
			     ACODEC_AGC_MIN_GAIN_PGA_MAX,
			     0, rv1106_codec_alc_agc_min_gain_tlv),

	/* ALC AGC Switch */
	SOC_ENUM_EXT("ALC AGC Left Switch", rv1106_agc_enum_array[0],
		     rv1106_codec_agc_get, rv1106_codec_agc_put),
	SOC_ENUM_EXT("ALC AGC Right Switch", rv1106_agc_enum_array[1],
		     rv1106_codec_agc_get, rv1106_codec_agc_put),

	/* ALC AGC Approximate Sample Rate */
	SOC_ENUM_EXT("AGC Left Approximate Sample Rate", rv1106_agc_asr_enum_array[0],
		     rv1106_codec_agc_asr_get, rv1106_codec_agc_asr_put),
	SOC_ENUM_EXT("AGC Right Approximate Sample Rate", rv1106_agc_asr_enum_array[1],
		     rv1106_codec_agc_asr_get, rv1106_codec_agc_asr_put),

	/* ADC Mode */
	SOC_ENUM_EXT("ADC Mode", rv1106_adc_mode_enum_array[0],
		     rv1106_codec_adc_mode_get, rv1106_codec_adc_mode_put),

	/* ADC MICBIAS Voltage */
	SOC_ENUM_EXT("ADC MICBIAS Voltage", rv1106_micbias_volts_enum_array[0],
		     rv1106_codec_micbias_volts_get, rv1106_codec_micbias_volts_put),

	/* ADC Main MICBIAS Switch */
	SOC_ENUM_EXT("ADC Main MICBIAS", rv1106_main_micbias_enum_array[0],
		     rv1106_codec_main_micbias_get, rv1106_codec_main_micbias_put),

	/* ADC MIC Mute/Work Switch */
	SOC_ENUM_EXT("ADC MIC Left Switch", rv1106_mic_mute_enum_array[0],
		     rv1106_codec_mic_mute_get, rv1106_codec_mic_mute_put),
	SOC_ENUM_EXT("ADC MIC Right Switch", rv1106_mic_mute_enum_array[1],
		     rv1106_codec_mic_mute_get, rv1106_codec_mic_mute_put),

	/* DAC LINEOUT */
	SOC_SINGLE_RANGE_TLV("DAC LINEOUT Volume",
			     ACODEC_DAC_ANA_CTL2,
			     ACODEC_DAC_LINEOUT_GAIN_SFT,
			     ACODEC_DAC_LINEOUT_GAIN_MIN,
			     ACODEC_DAC_LINEOUT_GAIN_MAX,
			     0, rv1106_codec_dac_lineout_gain_tlv),

	/* DAC HPMIX */
	SOC_SINGLE_EXT_TLV("DAC HPMIX Volume",
			   ACODEC_DAC_HPMIX_CTL,
			   ACODEC_DAC_HPMIX_GAIN_SFT,
			   ACODEC_DAC_HPMIX_GAIN_MAX,
			   0,
			   rv1106_codec_hpmix_gain_get,
			   rv1106_codec_hpmix_gain_put,
			   rv1106_codec_dac_hpmix_gain_tlv),
};

static unsigned int using_adc_lr(enum adc_mode_e adc_mode)
{
	if (adc_mode >= SING_ADCLR && adc_mode <= DIFF_ADCLR)
		return (ADCL | ADCR);
	else if (adc_mode >= DIFF_ADCR && adc_mode <= SING_ADCR)
		return ADCR;
	else
		return ADCL;
}

static bool using_adc_diff(enum adc_mode_e adc_mode)
{
	if (adc_mode == DIFF_ADCL ||
	    adc_mode == DIFF_ADCR ||
	    adc_mode == DIFF_ADCLR)
		return true;
	else
		return false;
}

static int check_adc_mode(struct rv1106_codec_priv *rv1106)
{
	if (rv1106->soc_id == SOC_RV1103 &&
	    (rv1106->adc_mode == DIFF_ADCLR ||
	     rv1106->adc_mode == DIFF_ADCR)) {
		dev_err(rv1106->plat_dev,
			"%s: Differential mode rv1103 only supports 'DiffadcL'\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int rv1106_codec_adc_mode_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rv1106->adc_mode;

	return 0;
}

static int rv1106_codec_adc_mode_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	unsigned int last_mode = rv1106->adc_mode;

	rv1106->adc_mode = ucontrol->value.integer.value[0];
	if (check_adc_mode(rv1106)) {
		dev_err(rv1106->plat_dev,
			"%s - something error checking ADC mode\n", __func__);
		rv1106->adc_mode = last_mode;
		return 0;
	}

	return 0;
}

static int rv1106_codec_agc_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	if (e->shift_l)
		ucontrol->value.integer.value[0] = rv1106->agc_r;
	else
		ucontrol->value.integer.value[0] = rv1106->agc_l;

	return 0;
}

static int rv1106_codec_agc_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value = ucontrol->value.integer.value[0];

	if (value) {
		/* ALC AGC On */
		if (e->shift_l) {
			/* ALC AGC Right On */
			regmap_update_bits(rv1106->regmap, ACODEC_ADC_PGA_AGC_R_CTL9,
					   ACODEC_AGC_FUNC_SEL_MSK,
					   ACODEC_AGC_FUNC_SEL_EN);
			regmap_update_bits(rv1106->regmap, ACODEC_ADC_HPF_PGA_CTL,
					   ACODEC_ADC_R_PGA_MSK,
					   ACODEC_ADC_PGA_ALCR_EN);

			rv1106->agc_r = 1;
		} else {
			/* ALC AGC Left On */
			regmap_update_bits(rv1106->regmap, ACODEC_ADC_PGA_AGC_L_CTL9,
					   ACODEC_AGC_FUNC_SEL_MSK,
					   ACODEC_AGC_FUNC_SEL_EN);
			regmap_update_bits(rv1106->regmap, ACODEC_ADC_HPF_PGA_CTL,
					   ACODEC_ADC_L_PGA_MSK,
					   ACODEC_ADC_PGA_ALCL_EN);

			rv1106->agc_l = 1;
		}
	} else {
		/* ALC AGC Off */
		if (e->shift_l) {
			/* ALC AGC Right Off */
			regmap_update_bits(rv1106->regmap, ACODEC_ADC_PGA_AGC_R_CTL9,
					   ACODEC_AGC_FUNC_SEL_MSK,
					   ACODEC_AGC_FUNC_SEL_DIS);
			regmap_update_bits(rv1106->regmap, ACODEC_ADC_HPF_PGA_CTL,
					   ACODEC_ADC_R_PGA_MSK,
					   ACODEC_ADC_PGA_ALCR_DIS);

			rv1106->agc_r = 0;
		} else {
			/* ALC AGC Left Off */
			regmap_update_bits(rv1106->regmap, ACODEC_ADC_PGA_AGC_L_CTL9,
					   ACODEC_AGC_FUNC_SEL_MSK,
					   ACODEC_AGC_FUNC_SEL_DIS);
			regmap_update_bits(rv1106->regmap, ACODEC_ADC_HPF_PGA_CTL,
					   ACODEC_ADC_L_PGA_MSK,
					   ACODEC_ADC_PGA_ALCL_DIS);

			rv1106->agc_l = 0;
		}
	}

	return 0;
}

static int rv1106_codec_agc_asr_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value;

	if (e->shift_l) {
		regmap_read(rv1106->regmap, ACODEC_ADC_PGA_AGC_R_CTL4, &value);
		rv1106->agc_asr_r = value >> ACODEC_AGC_APPROX_RATE_SFT;
		ucontrol->value.integer.value[0] = rv1106->agc_asr_r;
	} else {
		regmap_read(rv1106->regmap, ACODEC_ADC_PGA_AGC_L_CTL4, &value);
		rv1106->agc_asr_l = value >> ACODEC_AGC_APPROX_RATE_SFT;
		ucontrol->value.integer.value[0] = rv1106->agc_asr_l;
	}

	return 0;
}

static int rv1106_codec_agc_asr_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value;

	value = ucontrol->value.integer.value[0] << ACODEC_AGC_APPROX_RATE_SFT;

	if (e->shift_l) {
		/* ALC AGC Right Approximate Sample Rate */
		regmap_update_bits(rv1106->regmap, ACODEC_ADC_PGA_AGC_R_CTL4,
				   ACODEC_AGC_APPROX_RATE_MSK,
				   value);
		rv1106->agc_asr_r = ucontrol->value.integer.value[0];
	} else {
		/* ALC AGC Left Approximate Sample Rate */
		regmap_update_bits(rv1106->regmap, ACODEC_ADC_PGA_AGC_L_CTL4,
				   ACODEC_AGC_APPROX_RATE_MSK,
				   value);
		rv1106->agc_asr_l = ucontrol->value.integer.value[0];
	}

	return 0;
}

static int rv1106_codec_mic_mute_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value;

	if (e->shift_l) {
		/* ADC MIC Right Mute/Work Infos */
		regmap_read(rv1106->regmap, ACODEC_ADC_BIST_MODE_SEL, &value);
		rv1106->mic_mute_r = (value & ACODEC_ADC_R_BIST_SINE) >>
					     ACODEC_ADC_R_BIST_SFT;
		ucontrol->value.integer.value[0] = rv1106->mic_mute_r;
	} else {
		/* ADC MIC Left Mute/Work Infos */
		regmap_read(rv1106->regmap, ACODEC_ADC_BIST_MODE_SEL, &value);
		rv1106->mic_mute_l = (value & ACODEC_ADC_L_BIST_SINE) >>
					     ACODEC_ADC_L_BIST_SFT;
		ucontrol->value.integer.value[0] = rv1106->mic_mute_l;
	}

	return 0;
}

static int rv1106_codec_mic_mute_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int value;

	if (e->shift_l) {
		/* ADC MIC Right Mute/Work Configuration */
		value = ucontrol->value.integer.value[0] << ACODEC_ADC_R_BIST_SFT;
		regmap_update_bits(rv1106->regmap, ACODEC_ADC_BIST_MODE_SEL,
				   ACODEC_ADC_R_BIST_SINE,
				   value);
		rv1106->mic_mute_r = ucontrol->value.integer.value[0];
	} else {
		/* ADC MIC Left Mute/Work Configuration */
		value = ucontrol->value.integer.value[0] << ACODEC_ADC_L_BIST_SFT;
		regmap_update_bits(rv1106->regmap, ACODEC_ADC_BIST_MODE_SEL,
				   ACODEC_ADC_L_BIST_SINE,
				   value);
		rv1106->mic_mute_l = ucontrol->value.integer.value[0];
	}

	return 0;
}

static int rv1106_codec_micbias_volts_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rv1106->micbias_volt;

	return 0;
}

static int rv1106_codec_micbias_volts_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	unsigned int volt = ucontrol->value.integer.value[0];
	int ret;

	ret = check_micbias(volt);
	if (ret < 0) {
		dev_err(rv1106->plat_dev, "Invalid micbias volt: %d\n",
			volt);
		return ret;
	}

	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL0,
			   ACODEC_ADC_LEVEL_RANGE_MICBIAS_MSK,
			   volt);

	rv1106->micbias_volt = volt;

	return 0;
}

static int rv1106_codec_main_micbias_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rv1106->micbias_enable;

	return 0;
}

static int rv1106_codec_main_micbias_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	unsigned int on = ucontrol->value.integer.value[0];

	if (on) {
		if (!rv1106->micbias_enable)
			rv1106_codec_micbias_enable(rv1106, rv1106->micbias_volt);
	} else {
		if (rv1106->micbias_enable)
			rv1106_codec_micbias_disable(rv1106);
	}

	return 0;
}

static int rv1106_codec_mic_gain_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_get_volsw_range(kcontrol, ucontrol);
}

static int rv1106_codec_mic_gain_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	unsigned int index = ucontrol->value.integer.value[0];

	/*
	 * From the TRM, the gain of MIC Boost only supports:
	 * 0dB (index == 1)
	 * 20dB(index == 2)
	 * 12dB(index == 3)
	 */
	if ((index < ACODEC_ADC_MIC_GAIN_MIN) ||
	    (index > ACODEC_ADC_MIC_GAIN_MAX)) {
		dev_err(rv1106->plat_dev, "%s: invalid mic gain index: %d\n",
			__func__, index);
		return -EINVAL;
	}

	return snd_soc_put_volsw_range(kcontrol, ucontrol);
}

static int rv1106_codec_hpf_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	unsigned int value;

	regmap_read(rv1106->regmap, ACODEC_ADC_HPF_PGA_CTL, &value);
	if (value & ACODEC_ADC_HPF_MSK)
		rv1106->hpf_cutoff = 1;
	else
		rv1106->hpf_cutoff = 0;

	ucontrol->value.integer.value[0] = rv1106->hpf_cutoff;

	return 0;
}

static int rv1106_codec_hpf_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	unsigned int value = ucontrol->value.integer.value[0];

	if (value) {
		/* Enable high pass filter for ADCs */
		regmap_update_bits(rv1106->regmap, ACODEC_ADC_HPF_PGA_CTL,
				   ACODEC_ADC_HPF_MSK,
				   ACODEC_ADC_HPF_EN);
	} else {
		/* Disable high pass filter for ADCs. */
		regmap_update_bits(rv1106->regmap, ACODEC_ADC_HPF_PGA_CTL,
				   ACODEC_ADC_HPF_MSK,
				   ACODEC_ADC_HPF_DIS);
	}

	rv1106->hpf_cutoff = value;

	return 0;
}

static void rv1106_codec_pa_ctrl(struct rv1106_codec_priv *rv1106, bool on)
{
	if (!rv1106->pa_ctl_gpio)
		return;

	if (on) {
		gpiod_direction_output(rv1106->pa_ctl_gpio, on);
		msleep(rv1106->pa_ctl_delay_ms);
	} else {
		gpiod_direction_output(rv1106->pa_ctl_gpio, on);
	}
}

static int rv1106_codec_reset(struct snd_soc_component *component)
{
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	reset_control_assert(rv1106->reset);
	usleep_range(10000, 11000);     /* estimated value */
	reset_control_deassert(rv1106->reset);

	regmap_write(rv1106->regmap, ACODEC_GLB_CON, 0x00);
	usleep_range(10000, 11000);     /* estimated value */
	regmap_write(rv1106->regmap, ACODEC_GLB_CON,
		     ACODEC_CODEC_SYS_WORK |
		     ACODEC_CODEC_CORE_WORK);

	return 0;
}

static int rv1106_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		regcache_cache_only(rv1106->regmap, false);
		regcache_sync(rv1106->regmap);
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}

	return 0;
}

static int rv1106_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	unsigned int adc_aif1 = 0, adc_aif2 = 0, dac_aif1 = 0, dac_aif2 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		adc_aif2 |= ACODEC_ADC_IO_MODE_SLAVE;
		adc_aif2 |= ACODEC_ADC_MODE_SLAVE;
		dac_aif2 |= ACODEC_DAC_IO_MODE_SLAVE;
		dac_aif2 |= ACODEC_DAC_MODE_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		adc_aif2 |= ACODEC_ADC_IO_MODE_MASTER;
		adc_aif2 |= ACODEC_ADC_MODE_MASTER;
		dac_aif2 |= ACODEC_DAC_IO_MODE_MASTER;
		dac_aif2 |= ACODEC_DAC_MODE_MASTER;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		adc_aif1 |= ACODEC_ADC_I2S_MODE_PCM;
		dac_aif1 |= ACODEC_DAC_I2S_MODE_PCM;
		break;
	case SND_SOC_DAIFMT_I2S:
		adc_aif1 |= ACODEC_ADC_I2S_MODE_I2S;
		dac_aif1 |= ACODEC_DAC_I2S_MODE_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adc_aif1 |= ACODEC_ADC_I2S_MODE_RJ;
		dac_aif1 |= ACODEC_DAC_I2S_MODE_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adc_aif1 |= ACODEC_ADC_I2S_MODE_LJ;
		dac_aif1 |= ACODEC_DAC_I2S_MODE_LJ;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		adc_aif1 |= ACODEC_ADC_I2S_LRC_POL_NORMAL;
		adc_aif2 |= ACODEC_ADC_I2S_BIT_CLK_POL_NORMAL;
		dac_aif1 |= ACODEC_DAC_I2S_LRC_POL_NORMAL;
		dac_aif2 |= ACODEC_DAC_I2S_BIT_CLK_POL_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		adc_aif1 |= ACODEC_ADC_I2S_LRC_POL_REVERSAL;
		adc_aif2 |= ACODEC_ADC_I2S_BIT_CLK_POL_REVERSAL;
		dac_aif1 |= ACODEC_DAC_I2S_LRC_POL_REVERSAL;
		dac_aif2 |= ACODEC_DAC_I2S_BIT_CLK_POL_REVERSAL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		adc_aif1 |= ACODEC_ADC_I2S_LRC_POL_NORMAL;
		adc_aif2 |= ACODEC_ADC_I2S_BIT_CLK_POL_REVERSAL;
		dac_aif1 |= ACODEC_DAC_I2S_LRC_POL_NORMAL;
		dac_aif2 |= ACODEC_DAC_I2S_BIT_CLK_POL_REVERSAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		adc_aif1 |= ACODEC_ADC_I2S_LRC_POL_REVERSAL;
		adc_aif2 |= ACODEC_ADC_I2S_BIT_CLK_POL_NORMAL;
		dac_aif1 |= ACODEC_DAC_I2S_LRC_POL_REVERSAL;
		dac_aif2 |= ACODEC_DAC_I2S_BIT_CLK_POL_NORMAL;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(rv1106->regmap, ACODEC_ADC_I2S_CTL0,
			   ACODEC_ADC_I2S_LRC_POL_MSK |
			   ACODEC_ADC_I2S_MODE_MSK,
			   adc_aif1);
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_I2S_CTL1,
			   ACODEC_ADC_IO_MODE_MSK |
			   ACODEC_ADC_MODE_MSK |
			   ACODEC_ADC_I2S_BIT_CLK_POL_MSK,
			   adc_aif2);

	regmap_update_bits(rv1106->regmap, ACODEC_DAC_I2S_CTL0,
			   ACODEC_DAC_I2S_LRC_POL_MSK |
			   ACODEC_DAC_I2S_MODE_MSK,
			   dac_aif1);
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_I2S_CTL1,
			   ACODEC_DAC_IO_MODE_MSK |
			   ACODEC_DAC_MODE_MSK |
			   ACODEC_DAC_I2S_BIT_CLK_POL_MSK,
			   dac_aif2);

	return 0;
}

static int rv1106_codec_dac_dig_config(struct rv1106_codec_priv *rv1106,
				       struct snd_pcm_hw_params *params)
{
	unsigned int dac_aif1 = 0, dac_aif2 = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		dac_aif1 |= ACODEC_DAC_I2S_VALID_LEN_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		dac_aif1 |= ACODEC_DAC_I2S_VALID_LEN_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		dac_aif1 |= ACODEC_DAC_I2S_VALID_LEN_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dac_aif1 |= ACODEC_DAC_I2S_VALID_LEN_32BITS;
		break;
	default:
		return -EINVAL;
	}

	dac_aif1 |= ACODEC_DAC_I2S_LR_NORMAL;
	dac_aif2 |= ACODEC_DAC_I2S_WORK;

	regmap_update_bits(rv1106->regmap, ACODEC_DAC_I2S_CTL0,
			   ACODEC_DAC_I2S_VALID_LEN_MSK |
			   ACODEC_DAC_I2S_LR_MSK,
			   dac_aif1);
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_I2S_CTL1,
			   ACODEC_DAC_I2S_MSK,
			   dac_aif2);

	return 0;
}

static int rv1106_codec_adc_dig_config(struct rv1106_codec_priv *rv1106,
				       struct snd_pcm_hw_params *params)
{
	unsigned int adc_aif1 = 0, adc_aif2 = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adc_aif1 |= ACODEC_ADC_I2S_VALID_LEN_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adc_aif1 |= ACODEC_ADC_I2S_VALID_LEN_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		adc_aif1 |= ACODEC_ADC_I2S_VALID_LEN_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adc_aif1 |= ACODEC_ADC_I2S_VALID_LEN_32BITS;
		break;
	default:
		return -EINVAL;
	}

	adc_aif1 |= ACODEC_ADC_I2S_DATA_SEL_NORMAL;
	adc_aif2 |= ACODEC_ADC_I2S_WORK;

	regmap_update_bits(rv1106->regmap, ACODEC_ADC_I2S_CTL0,
			   ACODEC_ADC_I2S_VALID_LEN_MSK |
			   ACODEC_ADC_I2S_DATA_SEL_MSK,
			   adc_aif1);
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_I2S_CTL1,
			   ACODEC_ADC_I2S_MSK,
			   adc_aif2);

	return 0;
}

static int rv1106_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (mute) {
			/* Mute DAC HPMIX/LINEOUT */
			regmap_update_bits(rv1106->regmap,
					   ACODEC_DAC_ANA_CTL1,
					   ACODEC_DAC_L_LINEOUT_MUTE_MSK,
					   ACODEC_DAC_L_LINEOUT_MUTE);
			regmap_update_bits(rv1106->regmap,
					   ACODEC_DAC_HPMIX_CTL,
					   ACODEC_DAC_HPMIX_MUTE_MSK,
					   ACODEC_DAC_HPMIX_MUTE);
			rv1106_codec_pa_ctrl(rv1106, false);
		} else {
			/* Unmute DAC HPMIX/LINEOUT */
			regmap_update_bits(rv1106->regmap,
					   ACODEC_DAC_HPMIX_CTL,
					   ACODEC_DAC_HPMIX_MUTE_MSK,
					   ACODEC_DAC_HPMIX_WORK);
			regmap_update_bits(rv1106->regmap,
					   ACODEC_DAC_L_LINEOUT_MUTE_MSK,
					   ACODEC_DAC_MUTE_MSK,
					   ACODEC_DAC_L_LINEOUT_WORK);
			rv1106_codec_pa_ctrl(rv1106, true);
		}
	}

	return 0;
}

static int rv1106_codec_dac_enable(struct rv1106_codec_priv *rv1106)
{
	/* vendor step 1 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_IBIAS_MSK,
			   ACODEC_DAC_IBIAS_EN);

	udelay(20);

	/* vendor step 2 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_L_REF_VOL_BUF_MSK,
			   ACODEC_DAC_L_REF_VOL_BUF_EN);

	/* Waiting the stable reference voltage */
	mdelay(1);

	/* vendor step 7 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL1,
			   ACODEC_DAC_L_LINEOUT_MSK,
			   ACODEC_DAC_L_LINEOUT_EN);

	udelay(20);

	/* vendor step 8 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL1,
			   ACODEC_DAC_L_LINEOUT_SIGNAL_MSK,
			   ACODEC_DAC_L_LINEOUT_SIGNAL_WORK);

	udelay(20);

	/* vendor step 11 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_L_REF_VOL_MSK,
			   ACODEC_DAC_L_REF_VOL_EN);

	udelay(20);

	/* vendor step 12 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_L_CLK_MSK,
			   ACODEC_DAC_L_CLK_EN);

	udelay(20);

	/* vendor step 13 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_SRC_SIGNAL_MSK,
			   ACODEC_DAC_SRC_SIGNAL_EN);

	udelay(20);

	/* vendor step 14 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_L_SIGNAL_MSK,
			   ACODEC_DAC_L_SIGNAL_WORK);

	udelay(20);

	/* vendor step 15 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL1,
			   ACODEC_DAC_L_LINEOUT_MUTE_MSK,
			   ACODEC_DAC_L_LINEOUT_WORK);

	udelay(20);

	regmap_update_bits(rv1106->regmap, ACODEC_DAC_HPMIX_CTL,
			   ACODEC_DAC_HPMIX_MSK |
			   ACODEC_DAC_HPMIX_MDL_MSK |
			   ACODEC_DAC_HPMIX_MUTE_MSK |
			   ACODEC_DAC_HPMIX_SEL_MSK,
			   ACODEC_DAC_HPMIX_EN |
			   ACODEC_DAC_HPMIX_MDL_WORK |
			   ACODEC_DAC_HPMIX_WORK |
			   ACODEC_DAC_HPMIX_I2S);

	rv1106->dac_enable = true;

	return 0;
}

static int rv1106_codec_dac_disable(struct rv1106_codec_priv *rv1106)
{
	/* Step 02 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL1,
			   ACODEC_DAC_L_LINEOUT_MUTE_MSK,
			   ACODEC_DAC_L_LINEOUT_MUTE);

	/* Step 03 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL1,
			   ACODEC_DAC_L_LINEOUT_SIGNAL_MSK,
			   ACODEC_DAC_L_LINEOUT_SIGNAL_INIT);
	/* Step 04 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL1,
			   ACODEC_DAC_L_LINEOUT_MSK,
			   ACODEC_DAC_L_LINEOUT_DIS);
	/* Step 05 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_HPMIX_CTL,
			   ACODEC_DAC_HPMIX_MUTE_MSK,
			   ACODEC_DAC_HPMIX_MUTE);
	/* Step 06 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_HPMIX_CTL,
			   ACODEC_DAC_HPMIX_MDL_MSK,
			   ACODEC_DAC_HPMIX_MDL_INIT);
	/* Step 07 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_HPMIX_CTL,
			   ACODEC_DAC_HPMIX_MSK,
			   ACODEC_DAC_HPMIX_DIS);
	/* Step 08 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_SRC_SIGNAL_MSK,
			   ACODEC_DAC_SRC_SIGNAL_DIS);
	/* Step 09 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_L_CLK_MSK,
			   ACODEC_DAC_L_CLK_DIS);

	/* Step 10 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_L_REF_VOL_MSK,
			   ACODEC_DAC_L_REF_VOL_DIS);

	/* Step 11, note: skip handing POP Sound */

	/* Step 12 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_L_REF_VOL_BUF_MSK,
			   ACODEC_DAC_L_REF_VOL_BUF_DIS);

	/* Step 13 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_IBIAS_MSK,
			   ACODEC_DAC_IBIAS_DIS);

	/* Step 14 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_L_SIGNAL_MSK,
			   ACODEC_DAC_L_SIGNAL_INIT);


	rv1106->dac_enable = false;

	return 0;
}

static int rv1106_codec_power_on(struct rv1106_codec_priv *rv1106)
{
	/* vendor step 1 */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL0,
			   ACODEC_DAC_L_REF_POP_SOUND_MSK,
			   ACODEC_DAC_L_REF_POP_SOUND_WORK);
	/* vendor step 2. Charging */
	regmap_update_bits(rv1106->regmap, ACODEC_CURRENT_CHARGE_CTL,
			   ACODEC_ADC_CURRENT_CHARGE_MSK,
			   ACODEC_ADC_SEL_I(0xff));
	/* vendor step 3. Supply the power of the analog part. */
	/* vendor step 4 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL0,
			   ACODEC_ADC_REF_VOL_MSK, ACODEC_ADC_REF_VOL_EN);
	/* vendor step 5. Wait charging completed */
	msleep(20);
	/* vendor step 6 */
	regmap_update_bits(rv1106->regmap, ACODEC_CURRENT_CHARGE_CTL,
			   ACODEC_ADC_CURRENT_CHARGE_MSK,
			   ACODEC_ADC_SEL_I(0x02));
	return 0;
}

static int rv1106_codec_power_off(struct rv1106_codec_priv *rv1106)
{
	/*
	 * 0. Keep the power on and disable the DAC and ADC path.
	 */

	/* vendor step 1 */
	regmap_update_bits(rv1106->regmap, ACODEC_CURRENT_CHARGE_CTL,
			   ACODEC_ADC_CURRENT_CHARGE_MSK,
			   ACODEC_ADC_SEL_I(0xff));
	/* vendor step 3 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL0,
			   ACODEC_ADC_REF_VOL_MSK,
			   ACODEC_ADC_REF_VOL_DIS);
	/* vendor step 3. Wait until the voltage of VCM keep stable at AGND. */
	msleep(20);

	return 0;
}

static int rv1106_codec_adc_i2s_route(struct rv1106_codec_priv *rv1106)
{
	regmap_write(rv1106->grf, PERI_GRF_PERI_CON1,
		     ACODEC_MSK | ACODEC_EN);
	return 0;
}

static int check_micbias(int volt)
{
	switch (volt) {
	case ACODEC_ADC_MICBIAS_VOLT_0_975:
	case ACODEC_ADC_MICBIAS_VOLT_0_95:
	case ACODEC_ADC_MICBIAS_VOLT_0_925:
	case ACODEC_ADC_MICBIAS_VOLT_0_9:
	case ACODEC_ADC_MICBIAS_VOLT_0_875:
	case ACODEC_ADC_MICBIAS_VOLT_0_85:
	case ACODEC_ADC_MICBIAS_VOLT_0_825:
	case ACODEC_ADC_MICBIAS_VOLT_0_8:
		return 0;
	}

	return -EINVAL;
}

static int rv1106_codec_micbias_enable(struct rv1106_codec_priv *rv1106,
				       int volt)
{
	int ret;

	if (!rv1106->micbias_used)
		return 0;

	/* 0. Power up the ACODEC and keep the AVDDH stable */

	/* vendor step 1 */
	ret = check_micbias(volt);
	if (ret < 0) {
		dev_err(rv1106->plat_dev, "This is an invalid volt: %d\n",
			volt);
		return ret;
	}

	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL0,
			   ACODEC_ADC_LEVEL_RANGE_MICBIAS_MSK,
			   volt);

	/* vendor step 4 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL0,
			   ACODEC_MICBIAS_MSK,
			   ACODEC_MICBIAS_WORK);

	/* waiting micbias stabled*/
	mdelay(20);

	rv1106->micbias_enable = true;

	return 0;
}

static int rv1106_codec_micbias_disable(struct rv1106_codec_priv *rv1106)
{
	if (!rv1106->micbias_used)
		return 0;

	/* Step 0. Enable the MICBIAS and keep the Audio Codec stable */
	/* Do nothing */

	/* vendor step 1 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL0,
			   ACODEC_MICBIAS_MSK,
			   ACODEC_MICBIAS_RST);

	rv1106->micbias_enable = false;

	return 0;
}

static int rv1106_codec_hpmix_gain_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_get_volsw_range(kcontrol, ucontrol);
}

static int rv1106_codec_hpmix_gain_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	unsigned int index = ucontrol->value.integer.value[0];

	if ((index < ACODEC_DAC_HPMIX_GAIN_MIN) ||
	    (index > ACODEC_DAC_HPMIX_GAIN_MAX)) {
		dev_err(rv1106->plat_dev, "%s: invalid gain index: %d\n",
			__func__, index);
		return -EINVAL;
	}

	return snd_soc_put_volsw_range(kcontrol, ucontrol);
}

static int rv1106_codec_adc_enable(struct rv1106_codec_priv *rv1106)
{
	unsigned int lr = using_adc_lr(rv1106->adc_mode);
	bool is_diff = using_adc_diff(rv1106->adc_mode);
	unsigned int agc_func_en;
	int ret;

	dev_dbg(rv1106->plat_dev, "%s: soc_id: 0x%x lr: %d is_diff: %d\n",
		__func__, rv1106->soc_id, lr, is_diff);

	ret = check_adc_mode(rv1106);
	if (ret < 0) {
		dev_err(rv1106->plat_dev,
			"%s - something error checking ADC mode: %d\n",
			__func__, ret);
		return ret;
	}

	/* vendor step 00 */
	if (rv1106->soc_id == SOC_RV1103 && rv1106->adc_mode == DIFF_ADCL) {
		/* The ADCL is differential mode on rv1103 */
		regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL3,
				   ACODEC_ADC_L_MODE_SEL_MSK,
				   ACODEC_ADC_L_FULL_DIFFER2);
	} else if (rv1106->soc_id == SOC_RV1106 && is_diff) {
		/* The differential mode on rv1106 */
		regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL3,
				   L(lr, ACODEC_ADC_L_MODE_SEL_MSK) |
				   R(lr, ACODEC_ADC_R_MODE_SEL_MSK),
				   L(lr, ACODEC_ADC_L_FULL_DIFFER) |
				   R(lr, ACODEC_ADC_R_FULL_DIFFER));
	} else {
		/* The single-end mode */
		regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL3,
				   L(lr, ACODEC_ADC_L_MODE_SEL_MSK) |
				   R(lr, ACODEC_ADC_R_MODE_SEL_MSK),
				   L(lr, ACODEC_ADC_L_SINGLE_END) |
				   R(lr, ACODEC_ADC_R_SINGLE_END));
	}

	/* vendor step 01 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL1,
			   L(lr, ACODEC_ADC_L_MIC_MSK) |
			   R(lr, ACODEC_ADC_R_MIC_MSK),
			   L(lr, ACODEC_ADC_L_MIC_WORK) |
			   R(lr, ACODEC_ADC_R_MIC_WORK));

	/* vendor step 02 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL0,
			   ACODEC_ADC_IBIAS_MSK,
			   ACODEC_ADC_IBIAS_EN);

	/* vendor step 03 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL1,
			   L(lr, ACODEC_ADC_L_REF_VOL_BUF_MSK) |
			   R(lr, ACODEC_ADC_R_REF_VOL_BUF_MSK),
			   L(lr, ACODEC_ADC_L_REF_VOL_BUF_EN) |
			   R(lr, ACODEC_ADC_R_REF_VOL_BUF_EN));
	/* waiting VREF be stable */
	msleep(100);

	/* vendor step 04 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL3,
			   L(lr, ACODEC_MIC_L_MSK) |
			   R(lr, ACODEC_MIC_R_MSK),
			   L(lr, ACODEC_MIC_L_EN) |
			   R(lr, ACODEC_MIC_R_EN));

	/* vendor step 05 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL3,
			   L(lr, ACODEC_ADC_L_MSK) |
			   R(lr, ACODEC_ADC_R_MSK),
			   L(lr, ACODEC_ADC_L_EN) |
			   R(lr, ACODEC_ADC_R_EN));

	/* vendor step 06 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL6,
			   L(lr, ACODEC_ADC_L_CLK_MSK) |
			   R(lr, ACODEC_ADC_R_CLK_MSK),
			   L(lr, ACODEC_ADC_L_CLK_WORK) |
			   R(lr, ACODEC_ADC_R_CLK_WORK));

	/* vendor step 07 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL6,
			   L(lr, ACODEC_ADC_L_WORK) |
			   R(lr, ACODEC_ADC_R_WORK),
			   L(lr, ACODEC_ADC_L_WORK) |
			   R(lr, ACODEC_ADC_R_WORK));

	/* vendor step 08 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL6,
			   L(lr, ACODEC_ADC_L_SIGNAL_EN) |
			   R(lr, ACODEC_ADC_R_SIGNAL_EN),
			   L(lr, ACODEC_ADC_L_SIGNAL_EN) |
			   R(lr, ACODEC_ADC_R_SIGNAL_EN));

	/* vendor step 09 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL6,
			   L(lr, ACODEC_ADC_L_ALC_MSK) |
			   R(lr, ACODEC_ADC_R_ALC_MSK),
			   L(lr, ACODEC_ADC_L_ALC_WORK) |
			   R(lr, ACODEC_ADC_R_ALC_WORK));

	/* vendor step 10 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL1,
			   L(lr, ACODEC_ADC_L_MIC_SIGNAL_MSK) |
			   R(lr, ACODEC_ADC_R_MIC_SIGNAL_MSK),
			   L(lr, ACODEC_ADC_L_MIC_SIGNAL_WORK) |
			   R(lr, ACODEC_ADC_R_MIC_SIGNAL_WORK));

	/* vendor step 11, configure GAIN_MICL/R by user */

	/* vendor step 12, configure GAIN_ALCL/R by user */

	/* vendor step 13 */
	regmap_read(rv1106->regmap, ACODEC_ADC_ANA_CTL1, &agc_func_en);
	if (agc_func_en & ACODEC_AGC_FUNC_SEL_EN) {
		regmap_update_bits(rv1106->regmap,
				   ACODEC_ADC_ANA_CTL1,
				   L(lr, ACODEC_ADC_L_ZERO_CROSS_DET_MSK) |
				   R(lr, ACODEC_ADC_R_ZERO_CROSS_DET_MSK),
				   L(lr, ACODEC_ADC_L_ZERO_CROSS_DET_EN) |
				   R(lr, ACODEC_ADC_R_ZERO_CROSS_DET_EN));
	}

	rv1106->adc_enable = true;

	return 0;
}

static int rv1106_codec_adc_disable(struct rv1106_codec_priv *rv1106)
{
	/* vendor step 1 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL1,
			   ACODEC_ADC_L_ZERO_CROSS_DET_MSK |
			   ACODEC_ADC_R_ZERO_CROSS_DET_MSK,
			   ACODEC_ADC_L_ZERO_CROSS_DET_DIS |
			   ACODEC_ADC_R_ZERO_CROSS_DET_DIS);

	/* vendor step 2 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL6,
			   ACODEC_ADC_L_WORK |
			   ACODEC_ADC_R_WORK,
			   ACODEC_ADC_L_INIT |
			   ACODEC_ADC_R_INIT);

	/* vendor step 3 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL6,
			   ACODEC_ADC_L_CLK_WORK |
			   ACODEC_ADC_R_CLK_WORK,
			   ACODEC_ADC_L_CLK_RST |
			   ACODEC_ADC_R_CLK_RST);

	/* vendor step 4 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL3,
			   ACODEC_ADC_L_MSK |
			   ACODEC_ADC_R_MSK,
			   ACODEC_ADC_L_DIS |
			   ACODEC_ADC_R_DIS);

	/* vendor step 5 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL3,
			   ACODEC_MIC_L_MSK |
			   ACODEC_MIC_R_MSK,
			   ACODEC_MIC_L_DIS |
			   ACODEC_MIC_R_DIS);

	/* vendor step 6 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL1,
			   ACODEC_ADC_L_REF_VOL_BUF_MSK |
			   ACODEC_ADC_R_REF_VOL_BUF_MSK,
			   ACODEC_ADC_L_REF_VOL_BUF_DIS |
			   ACODEC_ADC_R_REF_VOL_BUF_DIS);

	/* vendor step 7 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL0,
			   ACODEC_ADC_IBIAS_MSK,
			   ACODEC_ADC_IBIAS_DIS);

	/* vendor step 8 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL6,
			   ACODEC_ADC_L_SIGNAL_EN |
			   ACODEC_ADC_R_SIGNAL_EN,
			   ACODEC_ADC_L_SIGNAL_DIS |
			   ACODEC_ADC_R_SIGNAL_DIS);

	/* vendor step 9 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL6,
			   ACODEC_ADC_L_ALC_MSK |
			   ACODEC_ADC_R_ALC_MSK,
			   ACODEC_ADC_L_ALC_INIT |
			   ACODEC_ADC_R_ALC_INIT);

	/* vendor step 10 */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL1,
			   ACODEC_ADC_L_MIC_SIGNAL_MSK |
			   ACODEC_ADC_R_MIC_SIGNAL_MSK,
			   ACODEC_ADC_L_MIC_SIGNAL_INIT |
			   ACODEC_ADC_R_MIC_SIGNAL_INIT);

	rv1106->adc_enable = false;

	return 0;
}

static int rv1106_codec_open_capture(struct rv1106_codec_priv *rv1106)
{
	rv1106_codec_adc_enable(rv1106);

	return 0;
}

static int rv1106_codec_close_capture(struct rv1106_codec_priv *rv1106)
{
	rv1106_codec_adc_disable(rv1106);
	return 0;
}

static int rv1106_codec_open_playback(struct rv1106_codec_priv *rv1106)
{
	rv1106_codec_dac_enable(rv1106);
	return 0;
}

static int rv1106_codec_close_playback(struct rv1106_codec_priv *rv1106)
{
	rv1106_codec_dac_disable(rv1106);
	return 0;
}

static int rv1106_codec_dlp_down(struct rv1106_codec_priv *rv1106)
{
	rv1106_codec_micbias_disable(rv1106);
	rv1106_codec_power_off(rv1106);
	return 0;
}

static int rv1106_codec_dlp_up(struct rv1106_codec_priv *rv1106)
{
	rv1106_codec_power_on(rv1106);
	rv1106_codec_micbias_enable(rv1106, rv1106->micbias_volt);
	return 0;
}

static int rv1106_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		rv1106_codec_open_playback(rv1106);
		rv1106_codec_dac_dig_config(rv1106, params);
	} else {
		rv1106_codec_open_capture(rv1106);
		rv1106_codec_adc_dig_config(rv1106, params);
	}

	return 0;
}

static void rv1106_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		rv1106_codec_close_playback(rv1106);
	else
		rv1106_codec_close_capture(rv1106);

	regcache_cache_only(rv1106->regmap, false);
	regcache_sync(rv1106->regmap);
}

static const struct snd_soc_dai_ops rv1106_dai_ops = {
	.hw_params = rv1106_hw_params,
	.set_fmt = rv1106_set_dai_fmt,
	.mute_stream = rv1106_mute_stream,
	.shutdown = rv1106_pcm_shutdown,
};

static struct snd_soc_dai_driver rv1106_dai[] = {
	{
		.name = "rv1106-hifi",
		.id = ACODEC_HIFI,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &rv1106_dai_ops,
	},
};

static int rv1106_suspend(struct snd_soc_component *component)
{
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	rv1106_codec_dlp_down(rv1106);
	clk_disable_unprepare(rv1106->mclk_acodec);
	clk_disable_unprepare(rv1106->pclk_acodec);
	rv1106_set_bias_level(component, SND_SOC_BIAS_OFF);
	return 0;
}

static int rv1106_resume(struct snd_soc_component *component)
{
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	ret = clk_prepare_enable(rv1106->pclk_acodec);
	if (ret < 0) {
		dev_err(rv1106->plat_dev,
			"Failed to enable acodec pclk_acodec: %d\n", ret);
		goto out;
	}

	ret = clk_prepare_enable(rv1106->mclk_acodec);
	if (ret < 0) {
		dev_err(rv1106->plat_dev,
			"Failed to enable acodec mclk_acodec: %d\n", ret);
		goto out;
	}

	rv1106_codec_dlp_up(rv1106);
out:
	rv1106_set_bias_level(component, SND_SOC_BIAS_STANDBY);
	return ret;
}

static int rv1106_codec_default_gains(struct rv1106_codec_priv *rv1106)
{
	/* Prepare ADC gains */
	/* vendor step 12, set MIC PGA default gains */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL2,
			   ACODEC_ADC_L_MIC_GAIN_MSK |
			   ACODEC_ADC_R_MIC_GAIN_MSK,
			   ACODEC_ADC_L_MIC_GAIN_20DB |
			   ACODEC_ADC_R_MIC_GAIN_20DB); // TODO: using 20dB

	/* vendor step 13, set ALC default gains */
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL4,
			   ACODEC_ADC_L_ALC_GAIN_MSK,
			   ACODEC_ADC_L_ALC_GAIN_0DB);
	regmap_update_bits(rv1106->regmap, ACODEC_ADC_ANA_CTL5,
			   ACODEC_ADC_R_ALC_GAIN_MSK,
			   ACODEC_ADC_R_ALC_GAIN_0DB);

	/* Prepare DAC gains */
	/* Step 19, set LINEOUT default gains */
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_GAIN_SEL,
			   ACODEC_DAC_DIG_GAIN_MSK,
			   ACODEC_DAC_DIG_GAIN(ACODEC_DAC_DIG_0DB));
	regmap_update_bits(rv1106->regmap, ACODEC_DAC_ANA_CTL2,
			   ACODEC_DAC_LINEOUT_GAIN_MSK,
			   ACODEC_DAC_LINEOUT_GAIN_0DB);

	return 0;
}

static int rv1106_codec_check_micbias(struct rv1106_codec_priv *rv1106,
				      struct device_node *np)
{
	/* Check internal of acodec micbias */
	rv1106->micbias_used =
		of_property_read_bool(np, "acodec,micbias");

	/* Using 0.9*AVDD by default */
	rv1106->micbias_volt = ACODEC_ADC_MICBIAS_VOLT_0_9;

	return 0;
}

static int rv1106_codec_dapm_controls_prepare(struct rv1106_codec_priv *rv1106)
{
	rv1106->adc_mode = DIFF_ADCL;
	rv1106->hpf_cutoff = 0;
	rv1106->agc_l = 0;
	rv1106->agc_r = 0;
	rv1106->agc_asr_l = AGC_ASR_96KHZ;
	rv1106->agc_asr_r = AGC_ASR_96KHZ;

	return 0;
}

static int rv1106_codec_prepare(struct rv1106_codec_priv *rv1106)
{
	/* Clear registers for ADC and DAC */
	rv1106_codec_close_playback(rv1106);
	rv1106_codec_close_capture(rv1106);
	rv1106_codec_default_gains(rv1106);
	rv1106_codec_dapm_controls_prepare(rv1106);

	return 0;
}

static int rv1106_probe(struct snd_soc_component *component)
{
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	rv1106->component = component;
	rv1106_codec_reset(component);
	rv1106_codec_dlp_up(rv1106);
	rv1106_codec_prepare(rv1106);

	regcache_cache_only(rv1106->regmap, false);
	regcache_sync(rv1106->regmap);

	return 0;
}

static void rv1106_remove(struct snd_soc_component *component)
{
	struct rv1106_codec_priv *rv1106 = snd_soc_component_get_drvdata(component);

	rv1106_codec_pa_ctrl(rv1106, false);
	rv1106_codec_micbias_disable(rv1106);
	rv1106_codec_power_off(rv1106);
	regcache_cache_only(rv1106->regmap, false);
	regcache_sync(rv1106->regmap);
}

static const struct snd_soc_component_driver soc_codec_dev_rv1106 = {
	.probe = rv1106_probe,
	.remove = rv1106_remove,
	.suspend = rv1106_suspend,
	.resume = rv1106_resume,
	.set_bias_level = rv1106_set_bias_level,
	.controls = rv1106_codec_dapm_controls,
	.num_controls = ARRAY_SIZE(rv1106_codec_dapm_controls),
};

/* Set the default value or reset value */
static const struct reg_default rv1106_codec_reg_defaults[] = {
	{ ACODEC_RESET_CTL, 0x03 },
};

static bool rv1106_codec_write_read_reg(struct device *dev, unsigned int reg)
{
	/* All registers can be read / write */
	return true;
}

static bool rv1106_codec_volatile_reg(struct device *dev, unsigned int reg)
{
	/* All registers can be read / write */
	return true;
}

static const struct regmap_config rv1106_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = ACODEC_REG_MAX,
	.writeable_reg = rv1106_codec_write_read_reg,
	.readable_reg = rv1106_codec_write_read_reg,
	.volatile_reg = rv1106_codec_volatile_reg,
	.reg_defaults = rv1106_codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rv1106_codec_reg_defaults),
	.cache_type = REGCACHE_FLAT,
};

static ssize_t adc_enable_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct rv1106_codec_priv *rv1106 =
		container_of(dev, struct rv1106_codec_priv, dev);

	return sprintf(buf, "%d\n", rv1106->adc_enable);
}

static ssize_t adc_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct rv1106_codec_priv *rv1106 =
		container_of(dev, struct rv1106_codec_priv, dev);
	unsigned long enable;
	int ret = kstrtoul(buf, 10, &enable);

	if (ret < 0) {
		dev_err(dev, "Invalid enable: %ld, ret: %d\n",
			enable, ret);
		return -EINVAL;
	}

	if (enable)
		rv1106_codec_open_capture(rv1106);
	else
		rv1106_codec_close_capture(rv1106);

	dev_info(dev, "ADC enable: %ld\n", enable);

	return count;
}

static ssize_t dac_enable_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct rv1106_codec_priv *rv1106 =
		container_of(dev, struct rv1106_codec_priv, dev);

	return sprintf(buf, "%d\n", rv1106->dac_enable);
}

static ssize_t dac_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct rv1106_codec_priv *rv1106 =
		container_of(dev, struct rv1106_codec_priv, dev);
	unsigned long enable;
	int ret = kstrtoul(buf, 10, &enable);

	if (ret < 0) {
		dev_err(dev, "Invalid enable: %ld, ret: %d\n",
			enable, ret);
		return -EINVAL;
	}

	if (enable)
		rv1106_codec_open_playback(rv1106);
	else
		rv1106_codec_close_playback(rv1106);

	dev_info(dev, "DAC enable: %ld\n", enable);

	return count;
}

static const struct device_attribute acodec_attrs[] = {
	__ATTR_RW(adc_enable),
	__ATTR_RW(dac_enable),
};

static void rv1106_codec_device_release(struct device *dev)
{
	/* Do nothing */
}

static int rv1106_codec_sysfs_init(struct platform_device *pdev,
				   struct rv1106_codec_priv *rv1106)
{
	struct device *dev = &rv1106->dev;
	int i;

	dev->release = rv1106_codec_device_release;
	dev->parent = &pdev->dev;
	set_dev_node(dev, dev_to_node(&pdev->dev));
	dev_set_name(dev, "acodec_attrs");

	if (device_register(dev)) {
		dev_err(&pdev->dev,
			"Register 'acodec_attrs' failed\n");
		dev->parent = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(acodec_attrs); i++) {
		if (device_create_file(dev, &acodec_attrs[i])) {
			dev_err(&pdev->dev,
				"Create 'acodec_attrs' failed\n");
			device_unregister(dev);
			return -ENOMEM;
		}
	}

	return 0;
}

static int rv1106_codec_sysfs_exit(struct rv1106_codec_priv *rv1106)
{
	struct device *dev = &rv1106->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(acodec_attrs); i++)
		device_remove_file(dev, &acodec_attrs[i]);

	device_unregister(dev);
	return 0;
}

#if defined(CONFIG_DEBUG_FS)
static int rv1106_codec_debugfs_reg_show(struct seq_file *s, void *v)
{
	struct rv1106_codec_priv *rv1106 = s->private;
	unsigned int i;
	unsigned int val;

	for (i = ACODEC_RESET_CTL; i <= ACODEC_ADC_PGA_AGC_R_CTL9; i += 4) {
		regmap_read(rv1106->regmap, i, &val);
		if (!(i % 16))
			seq_printf(s, "\nR:%04x: ", i);
		seq_printf(s, "%08x ", val);
	}

	seq_puts(s, "\n");

	return 0;
}

static ssize_t rv1106_codec_debugfs_reg_operate(struct file *file,
						const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct rv1106_codec_priv *rv1106 =
		((struct seq_file *)file->private_data)->private;
	unsigned int reg, val;
	char op;
	char kbuf[32];
	int ret;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	ret = sscanf(kbuf, "%c,%x,%x", &op, &reg, &val);
	if (ret != 3) {
		pr_err("sscanf failed: %d\n", ret);
		return -EFAULT;
	}

	if (op == 'w') {
		pr_info("Write reg: 0x%04x with val: 0x%08x\n", reg, val);
		regmap_write(rv1106->regmap, reg, val);
		regcache_cache_only(rv1106->regmap, false);
		regcache_sync(rv1106->regmap);
		pr_info("Read back reg: 0x%04x with val: 0x%08x\n", reg, val);
	} else if (op == 'r') {
		regmap_read(rv1106->regmap, reg, &val);
		pr_info("Read reg: 0x%04x with val: 0x%08x\n", reg, val);
	} else {
		pr_err("This is an invalid operation: %c\n", op);
	}

	return count;
}

static int rv1106_codec_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file,
			   rv1106_codec_debugfs_reg_show, inode->i_private);
}

static const struct file_operations rv1106_codec_reg_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = rv1106_codec_debugfs_open,
	.read = seq_read,
	.write = rv1106_codec_debugfs_reg_operate,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif /* CONFIG_DEBUG_FS */

static const struct of_device_id rv1106_codec_of_match[] = {
	{ .compatible = "rockchip,rv1103-codec", .data = (void *)SOC_RV1103},
	{ .compatible = "rockchip,rv1106-codec", .data = (void *)SOC_RV1106},
	{},
};
MODULE_DEVICE_TABLE(of, rv1106_codec_of_match);

static int rv1106_platform_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device_node *np = pdev->dev.of_node;
	struct rv1106_codec_priv *rv1106;
	struct resource *res;
	void __iomem *base;
	int ret;

	rv1106 = devm_kzalloc(&pdev->dev, sizeof(*rv1106), GFP_KERNEL);
	if (!rv1106)
		return -ENOMEM;

	of_id = of_match_device(rv1106_codec_of_match, &pdev->dev);
	if (of_id)
		rv1106->soc_id = (enum soc_id_e)of_id->data;
	dev_info(&pdev->dev, "current soc_id: rv%x\n", rv1106->soc_id);

	rv1106->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(rv1106->grf))
		return dev_err_probe(&pdev->dev, PTR_ERR(rv1106->grf),
				     "Missing 'rockchip,grf' property\n");

	rv1106->plat_dev = &pdev->dev;
	rv1106->reset = devm_reset_control_get(&pdev->dev, "acodec-reset");
	if (IS_ERR(rv1106->reset)) {
		ret = PTR_ERR(rv1106->reset);
		if (ret != -ENOENT)
			return ret;

		dev_dbg(&pdev->dev, "No reset control found\n");
		rv1106->reset = NULL;
	}

	rv1106->pa_ctl_gpio = devm_gpiod_get_optional(&pdev->dev, "pa-ctl",
						       GPIOD_OUT_LOW);
	if (IS_ERR(rv1106->pa_ctl_gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(rv1106->pa_ctl_gpio),
				     "Unable to claim gpio pa-ctl\n");

	if (rv1106->pa_ctl_gpio)
		of_property_read_u32(np, "pa-ctl-delay-ms",
				     &rv1106->pa_ctl_delay_ms);

	dev_info(&pdev->dev, "%s pa_ctl_gpio and pa_ctl_delay_ms: %d\n",
		rv1106->pa_ctl_gpio ? "Use" : "No use",
		rv1106->pa_ctl_delay_ms);

	/* Close external PA during startup. */
	rv1106_codec_pa_ctrl(rv1106, false);

	rv1106->pclk_acodec = devm_clk_get(&pdev->dev, "pclk_acodec");
	if (IS_ERR(rv1106->pclk_acodec))
		return dev_err_probe(&pdev->dev, PTR_ERR(rv1106->pclk_acodec),
				     "Can't get acodec pclk_acodec\n");

	rv1106->mclk_acodec = devm_clk_get(&pdev->dev, "mclk_acodec");
	if (IS_ERR(rv1106->mclk_acodec))
		return dev_err_probe(&pdev->dev, PTR_ERR(rv1106->mclk_acodec),
				    "Can't get acodec mclk_acodec\n");

	rv1106->mclk_cpu = devm_clk_get(&pdev->dev, "mclk_cpu");
	if (IS_ERR(rv1106->mclk_cpu))
		return dev_err_probe(&pdev->dev, PTR_ERR(rv1106->mclk_cpu),
				    "Can't get acodec mclk_cpu\n");

	ret = rv1106_codec_sysfs_init(pdev, rv1106);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Sysfs init failed\n");

#if defined(CONFIG_DEBUG_FS)
	rv1106->dbg_codec = debugfs_create_dir(CODEC_DRV_NAME, NULL);
	if (IS_ERR(rv1106->dbg_codec))
		dev_err(&pdev->dev,
			"Failed to create debugfs dir for rv1106!\n");
	else
		debugfs_create_file("reg", 0644, rv1106->dbg_codec,
				    rv1106, &rv1106_codec_reg_debugfs_fops);
#endif

	ret = clk_prepare_enable(rv1106->pclk_acodec);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable acodec pclk_acodec: %d\n", ret);
		goto failed_2;
	}

	ret = clk_prepare_enable(rv1106->mclk_acodec);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable acodec mclk_acodec: %d\n", ret);
		goto failed_1;
	}

	/**
	 * In PERICRU_PERICLKSEL_CON08, the mclk_acodec_t/rx_div are div 4
	 * by default, we need to calibrate once, make the div is 1 and keep
	 * the rate of mclk_acodec is the same with mclk_i2s.
	 *
	 * FIXME: need to handle div dynamically if the DSMAUDIO is enabled.
	 */
	clk_set_rate(rv1106->mclk_acodec, clk_get_rate(rv1106->mclk_cpu));

	rv1106_codec_check_micbias(rv1106, np);

	ret = rv1106_codec_adc_i2s_route(rv1106);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to route ADC to i2s: %d\n",
			ret);
		goto failed;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		dev_err(&pdev->dev, "Failed to ioremap resource\n");
		goto failed;
	}

	rv1106->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					       &rv1106_codec_regmap_config);
	if (IS_ERR(rv1106->regmap)) {
		ret = PTR_ERR(rv1106->regmap);
		dev_err(&pdev->dev, "Failed to regmap mmio\n");
		goto failed;
	}

	platform_set_drvdata(pdev, rv1106);
	ret = devm_snd_soc_register_component(&pdev->dev, &soc_codec_dev_rv1106,
					      rv1106_dai, ARRAY_SIZE(rv1106_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register codec: %d\n", ret);
		goto failed;
	}

	return ret;

failed:
	clk_disable_unprepare(rv1106->mclk_acodec);
failed_1:
	clk_disable_unprepare(rv1106->pclk_acodec);
failed_2:
#if defined(CONFIG_DEBUG_FS)
	debugfs_remove_recursive(rv1106->dbg_codec);
#endif
	rv1106_codec_sysfs_exit(rv1106);

	return ret;
}

static int rv1106_platform_remove(struct platform_device *pdev)
{
	struct rv1106_codec_priv *rv1106 =
		(struct rv1106_codec_priv *)platform_get_drvdata(pdev);

	clk_disable_unprepare(rv1106->mclk_acodec);
	clk_disable_unprepare(rv1106->pclk_acodec);
#if defined(CONFIG_DEBUG_FS)
	debugfs_remove_recursive(rv1106->dbg_codec);
#endif
	rv1106_codec_sysfs_exit(rv1106);

	return 0;
}

static struct platform_driver rv1106_codec_driver = {
	.driver = {
		   .name = CODEC_DRV_NAME,
		   .of_match_table = of_match_ptr(rv1106_codec_of_match),
	},
	.probe = rv1106_platform_probe,
	.remove = rv1106_platform_remove,
};
module_platform_driver(rv1106_codec_driver);

MODULE_DESCRIPTION("ASoC RV1106 Codec Driver");
MODULE_AUTHOR("Jason Zhu <jason.zhu@rock-chips.com>");
MODULE_LICENSE("GPL");
