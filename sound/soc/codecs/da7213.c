/*
 * DA7213 ALSA SoC Codec Driver
 *
 * Copyright (c) 2013 Dialog Semiconductor
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 * Based on DA9055 ALSA SoC codec driver.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/acpi.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include <sound/da7213.h>
#include "da7213.h"


/* Gain and Volume */
static const DECLARE_TLV_DB_RANGE(aux_vol_tlv,
	/* -54dB */
	0x0, 0x11, TLV_DB_SCALE_ITEM(-5400, 0, 0),
	/* -52.5dB to 15dB */
	0x12, 0x3f, TLV_DB_SCALE_ITEM(-5250, 150, 0)
);

static const DECLARE_TLV_DB_RANGE(digital_gain_tlv,
	0x0, 0x07, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
	/* -78dB to 12dB */
	0x08, 0x7f, TLV_DB_SCALE_ITEM(-7800, 75, 0)
);

static const DECLARE_TLV_DB_RANGE(alc_analog_gain_tlv,
	0x0, 0x0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
	/* 0dB to 36dB */
	0x01, 0x07, TLV_DB_SCALE_ITEM(0, 600, 0)
);

static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(mixin_gain_tlv, -450, 150, 0);
static const DECLARE_TLV_DB_SCALE(eq_gain_tlv, -1050, 150, 0);
static const DECLARE_TLV_DB_SCALE(hp_vol_tlv, -5700, 100, 0);
static const DECLARE_TLV_DB_SCALE(lineout_vol_tlv, -4800, 100, 0);
static const DECLARE_TLV_DB_SCALE(alc_threshold_tlv, -9450, 150, 0);
static const DECLARE_TLV_DB_SCALE(alc_gain_tlv, 0, 600, 0);

/* ADC and DAC voice mode (8kHz) high pass cutoff value */
static const char * const da7213_voice_hpf_corner_txt[] = {
	"2.5Hz", "25Hz", "50Hz", "100Hz", "150Hz", "200Hz", "300Hz", "400Hz"
};

static SOC_ENUM_SINGLE_DECL(da7213_dac_voice_hpf_corner,
			    DA7213_DAC_FILTERS1,
			    DA7213_VOICE_HPF_CORNER_SHIFT,
			    da7213_voice_hpf_corner_txt);

static SOC_ENUM_SINGLE_DECL(da7213_adc_voice_hpf_corner,
			    DA7213_ADC_FILTERS1,
			    DA7213_VOICE_HPF_CORNER_SHIFT,
			    da7213_voice_hpf_corner_txt);

/* ADC and DAC high pass filter cutoff value */
static const char * const da7213_audio_hpf_corner_txt[] = {
	"Fs/24000", "Fs/12000", "Fs/6000", "Fs/3000"
};

static SOC_ENUM_SINGLE_DECL(da7213_dac_audio_hpf_corner,
			    DA7213_DAC_FILTERS1
			    , DA7213_AUDIO_HPF_CORNER_SHIFT,
			    da7213_audio_hpf_corner_txt);

static SOC_ENUM_SINGLE_DECL(da7213_adc_audio_hpf_corner,
			    DA7213_ADC_FILTERS1,
			    DA7213_AUDIO_HPF_CORNER_SHIFT,
			    da7213_audio_hpf_corner_txt);

/* Gain ramping rate value */
static const char * const da7213_gain_ramp_rate_txt[] = {
	"nominal rate * 8", "nominal rate * 16", "nominal rate / 16",
	"nominal rate / 32"
};

static SOC_ENUM_SINGLE_DECL(da7213_gain_ramp_rate,
			    DA7213_GAIN_RAMP_CTRL,
			    DA7213_GAIN_RAMP_RATE_SHIFT,
			    da7213_gain_ramp_rate_txt);

/* DAC noise gate setup time value */
static const char * const da7213_dac_ng_setup_time_txt[] = {
	"256 samples", "512 samples", "1024 samples", "2048 samples"
};

static SOC_ENUM_SINGLE_DECL(da7213_dac_ng_setup_time,
			    DA7213_DAC_NG_SETUP_TIME,
			    DA7213_DAC_NG_SETUP_TIME_SHIFT,
			    da7213_dac_ng_setup_time_txt);

/* DAC noise gate rampup rate value */
static const char * const da7213_dac_ng_rampup_txt[] = {
	"0.02 ms/dB", "0.16 ms/dB"
};

static SOC_ENUM_SINGLE_DECL(da7213_dac_ng_rampup_rate,
			    DA7213_DAC_NG_SETUP_TIME,
			    DA7213_DAC_NG_RAMPUP_RATE_SHIFT,
			    da7213_dac_ng_rampup_txt);

/* DAC noise gate rampdown rate value */
static const char * const da7213_dac_ng_rampdown_txt[] = {
	"0.64 ms/dB", "20.48 ms/dB"
};

static SOC_ENUM_SINGLE_DECL(da7213_dac_ng_rampdown_rate,
			    DA7213_DAC_NG_SETUP_TIME,
			    DA7213_DAC_NG_RAMPDN_RATE_SHIFT,
			    da7213_dac_ng_rampdown_txt);

/* DAC soft mute rate value */
static const char * const da7213_dac_soft_mute_rate_txt[] = {
	"1", "2", "4", "8", "16", "32", "64"
};

static SOC_ENUM_SINGLE_DECL(da7213_dac_soft_mute_rate,
			    DA7213_DAC_FILTERS5,
			    DA7213_DAC_SOFTMUTE_RATE_SHIFT,
			    da7213_dac_soft_mute_rate_txt);

/* ALC Attack Rate select */
static const char * const da7213_alc_attack_rate_txt[] = {
	"44/fs", "88/fs", "176/fs", "352/fs", "704/fs", "1408/fs", "2816/fs",
	"5632/fs", "11264/fs", "22528/fs", "45056/fs", "90112/fs", "180224/fs"
};

static SOC_ENUM_SINGLE_DECL(da7213_alc_attack_rate,
			    DA7213_ALC_CTRL2,
			    DA7213_ALC_ATTACK_SHIFT,
			    da7213_alc_attack_rate_txt);

/* ALC Release Rate select */
static const char * const da7213_alc_release_rate_txt[] = {
	"176/fs", "352/fs", "704/fs", "1408/fs", "2816/fs", "5632/fs",
	"11264/fs", "22528/fs", "45056/fs", "90112/fs", "180224/fs"
};

static SOC_ENUM_SINGLE_DECL(da7213_alc_release_rate,
			    DA7213_ALC_CTRL2,
			    DA7213_ALC_RELEASE_SHIFT,
			    da7213_alc_release_rate_txt);

/* ALC Hold Time select */
static const char * const da7213_alc_hold_time_txt[] = {
	"62/fs", "124/fs", "248/fs", "496/fs", "992/fs", "1984/fs", "3968/fs",
	"7936/fs", "15872/fs", "31744/fs", "63488/fs", "126976/fs",
	"253952/fs", "507904/fs", "1015808/fs", "2031616/fs"
};

static SOC_ENUM_SINGLE_DECL(da7213_alc_hold_time,
			    DA7213_ALC_CTRL3,
			    DA7213_ALC_HOLD_SHIFT,
			    da7213_alc_hold_time_txt);

/* ALC Input Signal Tracking rate select */
static const char * const da7213_alc_integ_rate_txt[] = {
	"1/4", "1/16", "1/256", "1/65536"
};

static SOC_ENUM_SINGLE_DECL(da7213_alc_integ_attack_rate,
			    DA7213_ALC_CTRL3,
			    DA7213_ALC_INTEG_ATTACK_SHIFT,
			    da7213_alc_integ_rate_txt);

static SOC_ENUM_SINGLE_DECL(da7213_alc_integ_release_rate,
			    DA7213_ALC_CTRL3,
			    DA7213_ALC_INTEG_RELEASE_SHIFT,
			    da7213_alc_integ_rate_txt);


/*
 * Control Functions
 */

static int da7213_get_alc_data(struct snd_soc_component *component, u8 reg_val)
{
	int mid_data, top_data;
	int sum = 0;
	u8 iteration;

	for (iteration = 0; iteration < DA7213_ALC_AVG_ITERATIONS;
	     iteration++) {
		/* Select the left or right channel and capture data */
		snd_soc_component_write(component, DA7213_ALC_CIC_OP_LVL_CTRL, reg_val);

		/* Select middle 8 bits for read back from data register */
		snd_soc_component_write(component, DA7213_ALC_CIC_OP_LVL_CTRL,
			      reg_val | DA7213_ALC_DATA_MIDDLE);
		mid_data = snd_soc_component_read32(component, DA7213_ALC_CIC_OP_LVL_DATA);

		/* Select top 8 bits for read back from data register */
		snd_soc_component_write(component, DA7213_ALC_CIC_OP_LVL_CTRL,
			      reg_val | DA7213_ALC_DATA_TOP);
		top_data = snd_soc_component_read32(component, DA7213_ALC_CIC_OP_LVL_DATA);

		sum += ((mid_data << 8) | (top_data << 16));
	}

	return sum / DA7213_ALC_AVG_ITERATIONS;
}

static void da7213_alc_calib_man(struct snd_soc_component *component)
{
	u8 reg_val;
	int avg_left_data, avg_right_data, offset_l, offset_r;

	/* Calculate average for Left and Right data */
	/* Left Data */
	avg_left_data = da7213_get_alc_data(component,
			DA7213_ALC_CIC_OP_CHANNEL_LEFT);
	/* Right Data */
	avg_right_data = da7213_get_alc_data(component,
			 DA7213_ALC_CIC_OP_CHANNEL_RIGHT);

	/* Calculate DC offset */
	offset_l = -avg_left_data;
	offset_r = -avg_right_data;

	reg_val = (offset_l & DA7213_ALC_OFFSET_15_8) >> 8;
	snd_soc_component_write(component, DA7213_ALC_OFFSET_MAN_M_L, reg_val);
	reg_val = (offset_l & DA7213_ALC_OFFSET_19_16) >> 16;
	snd_soc_component_write(component, DA7213_ALC_OFFSET_MAN_U_L, reg_val);

	reg_val = (offset_r & DA7213_ALC_OFFSET_15_8) >> 8;
	snd_soc_component_write(component, DA7213_ALC_OFFSET_MAN_M_R, reg_val);
	reg_val = (offset_r & DA7213_ALC_OFFSET_19_16) >> 16;
	snd_soc_component_write(component, DA7213_ALC_OFFSET_MAN_U_R, reg_val);

	/* Enable analog/digital gain mode & offset cancellation */
	snd_soc_component_update_bits(component, DA7213_ALC_CTRL1,
			    DA7213_ALC_OFFSET_EN | DA7213_ALC_SYNC_MODE,
			    DA7213_ALC_OFFSET_EN | DA7213_ALC_SYNC_MODE);
}

static void da7213_alc_calib_auto(struct snd_soc_component *component)
{
	u8 alc_ctrl1;

	/* Begin auto calibration and wait for completion */
	snd_soc_component_update_bits(component, DA7213_ALC_CTRL1, DA7213_ALC_AUTO_CALIB_EN,
			    DA7213_ALC_AUTO_CALIB_EN);
	do {
		alc_ctrl1 = snd_soc_component_read32(component, DA7213_ALC_CTRL1);
	} while (alc_ctrl1 & DA7213_ALC_AUTO_CALIB_EN);

	/* If auto calibration fails, fall back to digital gain only mode */
	if (alc_ctrl1 & DA7213_ALC_CALIB_OVERFLOW) {
		dev_warn(component->dev,
			 "ALC auto calibration failed with overflow\n");
		snd_soc_component_update_bits(component, DA7213_ALC_CTRL1,
				    DA7213_ALC_OFFSET_EN | DA7213_ALC_SYNC_MODE,
				    0);
	} else {
		/* Enable analog/digital gain mode & offset cancellation */
		snd_soc_component_update_bits(component, DA7213_ALC_CTRL1,
				    DA7213_ALC_OFFSET_EN | DA7213_ALC_SYNC_MODE,
				    DA7213_ALC_OFFSET_EN | DA7213_ALC_SYNC_MODE);
	}

}

static void da7213_alc_calib(struct snd_soc_component *component)
{
	struct da7213_priv *da7213 = snd_soc_component_get_drvdata(component);
	u8 adc_l_ctrl, adc_r_ctrl;
	u8 mixin_l_sel, mixin_r_sel;
	u8 mic_1_ctrl, mic_2_ctrl;

	/* Save current values from ADC control registers */
	adc_l_ctrl = snd_soc_component_read32(component, DA7213_ADC_L_CTRL);
	adc_r_ctrl = snd_soc_component_read32(component, DA7213_ADC_R_CTRL);

	/* Save current values from MIXIN_L/R_SELECT registers */
	mixin_l_sel = snd_soc_component_read32(component, DA7213_MIXIN_L_SELECT);
	mixin_r_sel = snd_soc_component_read32(component, DA7213_MIXIN_R_SELECT);

	/* Save current values from MIC control registers */
	mic_1_ctrl = snd_soc_component_read32(component, DA7213_MIC_1_CTRL);
	mic_2_ctrl = snd_soc_component_read32(component, DA7213_MIC_2_CTRL);

	/* Enable ADC Left and Right */
	snd_soc_component_update_bits(component, DA7213_ADC_L_CTRL, DA7213_ADC_EN,
			    DA7213_ADC_EN);
	snd_soc_component_update_bits(component, DA7213_ADC_R_CTRL, DA7213_ADC_EN,
			    DA7213_ADC_EN);

	/* Enable MIC paths */
	snd_soc_component_update_bits(component, DA7213_MIXIN_L_SELECT,
			    DA7213_MIXIN_L_MIX_SELECT_MIC_1 |
			    DA7213_MIXIN_L_MIX_SELECT_MIC_2,
			    DA7213_MIXIN_L_MIX_SELECT_MIC_1 |
			    DA7213_MIXIN_L_MIX_SELECT_MIC_2);
	snd_soc_component_update_bits(component, DA7213_MIXIN_R_SELECT,
			    DA7213_MIXIN_R_MIX_SELECT_MIC_2 |
			    DA7213_MIXIN_R_MIX_SELECT_MIC_1,
			    DA7213_MIXIN_R_MIX_SELECT_MIC_2 |
			    DA7213_MIXIN_R_MIX_SELECT_MIC_1);

	/* Mute MIC PGAs */
	snd_soc_component_update_bits(component, DA7213_MIC_1_CTRL, DA7213_MUTE_EN,
			    DA7213_MUTE_EN);
	snd_soc_component_update_bits(component, DA7213_MIC_2_CTRL, DA7213_MUTE_EN,
			    DA7213_MUTE_EN);

	/* Perform calibration */
	if (da7213->alc_calib_auto)
		da7213_alc_calib_auto(component);
	else
		da7213_alc_calib_man(component);

	/* Restore MIXIN_L/R_SELECT registers to their original states */
	snd_soc_component_write(component, DA7213_MIXIN_L_SELECT, mixin_l_sel);
	snd_soc_component_write(component, DA7213_MIXIN_R_SELECT, mixin_r_sel);

	/* Restore ADC control registers to their original states */
	snd_soc_component_write(component, DA7213_ADC_L_CTRL, adc_l_ctrl);
	snd_soc_component_write(component, DA7213_ADC_R_CTRL, adc_r_ctrl);

	/* Restore original values of MIC control registers */
	snd_soc_component_write(component, DA7213_MIC_1_CTRL, mic_1_ctrl);
	snd_soc_component_write(component, DA7213_MIC_2_CTRL, mic_2_ctrl);
}

static int da7213_put_mixin_gain(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct da7213_priv *da7213 = snd_soc_component_get_drvdata(component);
	int ret;

	ret = snd_soc_put_volsw_2r(kcontrol, ucontrol);

	/* If ALC in operation, make sure calibrated offsets are updated */
	if ((!ret) && (da7213->alc_en))
		da7213_alc_calib(component);

	return ret;
}

static int da7213_put_alc_sw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct da7213_priv *da7213 = snd_soc_component_get_drvdata(component);

	/* Force ALC offset calibration if enabling ALC */
	if (ucontrol->value.integer.value[0] ||
	    ucontrol->value.integer.value[1]) {
		if (!da7213->alc_en) {
			da7213_alc_calib(component);
			da7213->alc_en = true;
		}
	} else {
		da7213->alc_en = false;
	}

	return snd_soc_put_volsw(kcontrol, ucontrol);
}


/*
 * KControls
 */

static const struct snd_kcontrol_new da7213_snd_controls[] = {

	/* Volume controls */
	SOC_SINGLE_TLV("Mic 1 Volume", DA7213_MIC_1_GAIN,
		       DA7213_MIC_AMP_GAIN_SHIFT, DA7213_MIC_AMP_GAIN_MAX,
		       DA7213_NO_INVERT, mic_vol_tlv),
	SOC_SINGLE_TLV("Mic 2 Volume", DA7213_MIC_2_GAIN,
		       DA7213_MIC_AMP_GAIN_SHIFT, DA7213_MIC_AMP_GAIN_MAX,
		       DA7213_NO_INVERT, mic_vol_tlv),
	SOC_DOUBLE_R_TLV("Aux Volume", DA7213_AUX_L_GAIN, DA7213_AUX_R_GAIN,
			 DA7213_AUX_AMP_GAIN_SHIFT, DA7213_AUX_AMP_GAIN_MAX,
			 DA7213_NO_INVERT, aux_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("Mixin PGA Volume", DA7213_MIXIN_L_GAIN,
			     DA7213_MIXIN_R_GAIN, DA7213_MIXIN_AMP_GAIN_SHIFT,
			     DA7213_MIXIN_AMP_GAIN_MAX, DA7213_NO_INVERT,
			     snd_soc_get_volsw_2r, da7213_put_mixin_gain,
			     mixin_gain_tlv),
	SOC_DOUBLE_R_TLV("ADC Volume", DA7213_ADC_L_GAIN, DA7213_ADC_R_GAIN,
			 DA7213_ADC_AMP_GAIN_SHIFT, DA7213_ADC_AMP_GAIN_MAX,
			 DA7213_NO_INVERT, digital_gain_tlv),
	SOC_DOUBLE_R_TLV("DAC Volume", DA7213_DAC_L_GAIN, DA7213_DAC_R_GAIN,
			 DA7213_DAC_AMP_GAIN_SHIFT, DA7213_DAC_AMP_GAIN_MAX,
			 DA7213_NO_INVERT, digital_gain_tlv),
	SOC_DOUBLE_R_TLV("Headphone Volume", DA7213_HP_L_GAIN, DA7213_HP_R_GAIN,
			 DA7213_HP_AMP_GAIN_SHIFT, DA7213_HP_AMP_GAIN_MAX,
			 DA7213_NO_INVERT, hp_vol_tlv),
	SOC_SINGLE_TLV("Lineout Volume", DA7213_LINE_GAIN,
		       DA7213_LINE_AMP_GAIN_SHIFT, DA7213_LINE_AMP_GAIN_MAX,
		       DA7213_NO_INVERT, lineout_vol_tlv),

	/* DAC Equalizer controls */
	SOC_SINGLE("DAC EQ Switch", DA7213_DAC_FILTERS4, DA7213_DAC_EQ_EN_SHIFT,
		   DA7213_DAC_EQ_EN_MAX, DA7213_NO_INVERT),
	SOC_SINGLE_TLV("DAC EQ1 Volume", DA7213_DAC_FILTERS2,
		       DA7213_DAC_EQ_BAND1_SHIFT, DA7213_DAC_EQ_BAND_MAX,
		       DA7213_NO_INVERT, eq_gain_tlv),
	SOC_SINGLE_TLV("DAC EQ2 Volume", DA7213_DAC_FILTERS2,
		       DA7213_DAC_EQ_BAND2_SHIFT, DA7213_DAC_EQ_BAND_MAX,
		       DA7213_NO_INVERT, eq_gain_tlv),
	SOC_SINGLE_TLV("DAC EQ3 Volume", DA7213_DAC_FILTERS3,
		       DA7213_DAC_EQ_BAND3_SHIFT, DA7213_DAC_EQ_BAND_MAX,
		       DA7213_NO_INVERT, eq_gain_tlv),
	SOC_SINGLE_TLV("DAC EQ4 Volume", DA7213_DAC_FILTERS3,
		       DA7213_DAC_EQ_BAND4_SHIFT, DA7213_DAC_EQ_BAND_MAX,
		       DA7213_NO_INVERT, eq_gain_tlv),
	SOC_SINGLE_TLV("DAC EQ5 Volume", DA7213_DAC_FILTERS4,
		       DA7213_DAC_EQ_BAND5_SHIFT, DA7213_DAC_EQ_BAND_MAX,
		       DA7213_NO_INVERT, eq_gain_tlv),

	/* High Pass Filter and Voice Mode controls */
	SOC_SINGLE("ADC HPF Switch", DA7213_ADC_FILTERS1, DA7213_HPF_EN_SHIFT,
		   DA7213_HPF_EN_MAX, DA7213_NO_INVERT),
	SOC_ENUM("ADC HPF Cutoff", da7213_adc_audio_hpf_corner),
	SOC_SINGLE("ADC Voice Mode Switch", DA7213_ADC_FILTERS1,
		   DA7213_VOICE_EN_SHIFT, DA7213_VOICE_EN_MAX,
		   DA7213_NO_INVERT),
	SOC_ENUM("ADC Voice Cutoff", da7213_adc_voice_hpf_corner),

	SOC_SINGLE("DAC HPF Switch", DA7213_DAC_FILTERS1, DA7213_HPF_EN_SHIFT,
		   DA7213_HPF_EN_MAX, DA7213_NO_INVERT),
	SOC_ENUM("DAC HPF Cutoff", da7213_dac_audio_hpf_corner),
	SOC_SINGLE("DAC Voice Mode Switch", DA7213_DAC_FILTERS1,
		   DA7213_VOICE_EN_SHIFT, DA7213_VOICE_EN_MAX,
		   DA7213_NO_INVERT),
	SOC_ENUM("DAC Voice Cutoff", da7213_dac_voice_hpf_corner),

	/* Mute controls */
	SOC_SINGLE("Mic 1 Switch", DA7213_MIC_1_CTRL, DA7213_MUTE_EN_SHIFT,
		   DA7213_MUTE_EN_MAX, DA7213_INVERT),
	SOC_SINGLE("Mic 2 Switch", DA7213_MIC_2_CTRL, DA7213_MUTE_EN_SHIFT,
		   DA7213_MUTE_EN_MAX, DA7213_INVERT),
	SOC_DOUBLE_R("Aux Switch", DA7213_AUX_L_CTRL, DA7213_AUX_R_CTRL,
		     DA7213_MUTE_EN_SHIFT, DA7213_MUTE_EN_MAX, DA7213_INVERT),
	SOC_DOUBLE_R("Mixin PGA Switch", DA7213_MIXIN_L_CTRL,
		     DA7213_MIXIN_R_CTRL, DA7213_MUTE_EN_SHIFT,
		     DA7213_MUTE_EN_MAX, DA7213_INVERT),
	SOC_DOUBLE_R("ADC Switch", DA7213_ADC_L_CTRL, DA7213_ADC_R_CTRL,
		     DA7213_MUTE_EN_SHIFT, DA7213_MUTE_EN_MAX, DA7213_INVERT),
	SOC_DOUBLE_R("Headphone Switch", DA7213_HP_L_CTRL, DA7213_HP_R_CTRL,
		     DA7213_MUTE_EN_SHIFT, DA7213_MUTE_EN_MAX, DA7213_INVERT),
	SOC_SINGLE("Lineout Switch", DA7213_LINE_CTRL, DA7213_MUTE_EN_SHIFT,
		   DA7213_MUTE_EN_MAX, DA7213_INVERT),
	SOC_SINGLE("DAC Soft Mute Switch", DA7213_DAC_FILTERS5,
		   DA7213_DAC_SOFTMUTE_EN_SHIFT, DA7213_DAC_SOFTMUTE_EN_MAX,
		   DA7213_NO_INVERT),
	SOC_ENUM("DAC Soft Mute Rate", da7213_dac_soft_mute_rate),

	/* Zero Cross controls */
	SOC_DOUBLE_R("Aux ZC Switch", DA7213_AUX_L_CTRL, DA7213_AUX_R_CTRL,
		     DA7213_ZC_EN_SHIFT, DA7213_ZC_EN_MAX, DA7213_NO_INVERT),
	SOC_DOUBLE_R("Mixin PGA ZC Switch", DA7213_MIXIN_L_CTRL,
		     DA7213_MIXIN_R_CTRL, DA7213_ZC_EN_SHIFT, DA7213_ZC_EN_MAX,
		     DA7213_NO_INVERT),
	SOC_DOUBLE_R("Headphone ZC Switch", DA7213_HP_L_CTRL, DA7213_HP_R_CTRL,
		     DA7213_ZC_EN_SHIFT, DA7213_ZC_EN_MAX, DA7213_NO_INVERT),

	/* Gain Ramping controls */
	SOC_DOUBLE_R("Aux Gain Ramping Switch", DA7213_AUX_L_CTRL,
		     DA7213_AUX_R_CTRL, DA7213_GAIN_RAMP_EN_SHIFT,
		     DA7213_GAIN_RAMP_EN_MAX, DA7213_NO_INVERT),
	SOC_DOUBLE_R("Mixin Gain Ramping Switch", DA7213_MIXIN_L_CTRL,
		     DA7213_MIXIN_R_CTRL, DA7213_GAIN_RAMP_EN_SHIFT,
		     DA7213_GAIN_RAMP_EN_MAX, DA7213_NO_INVERT),
	SOC_DOUBLE_R("ADC Gain Ramping Switch", DA7213_ADC_L_CTRL,
		     DA7213_ADC_R_CTRL, DA7213_GAIN_RAMP_EN_SHIFT,
		     DA7213_GAIN_RAMP_EN_MAX, DA7213_NO_INVERT),
	SOC_DOUBLE_R("DAC Gain Ramping Switch", DA7213_DAC_L_CTRL,
		     DA7213_DAC_R_CTRL, DA7213_GAIN_RAMP_EN_SHIFT,
		     DA7213_GAIN_RAMP_EN_MAX, DA7213_NO_INVERT),
	SOC_DOUBLE_R("Headphone Gain Ramping Switch", DA7213_HP_L_CTRL,
		     DA7213_HP_R_CTRL, DA7213_GAIN_RAMP_EN_SHIFT,
		     DA7213_GAIN_RAMP_EN_MAX, DA7213_NO_INVERT),
	SOC_SINGLE("Lineout Gain Ramping Switch", DA7213_LINE_CTRL,
		   DA7213_GAIN_RAMP_EN_SHIFT, DA7213_GAIN_RAMP_EN_MAX,
		   DA7213_NO_INVERT),
	SOC_ENUM("Gain Ramping Rate", da7213_gain_ramp_rate),

	/* DAC Noise Gate controls */
	SOC_SINGLE("DAC NG Switch", DA7213_DAC_NG_CTRL, DA7213_DAC_NG_EN_SHIFT,
		   DA7213_DAC_NG_EN_MAX, DA7213_NO_INVERT),
	SOC_ENUM("DAC NG Setup Time", da7213_dac_ng_setup_time),
	SOC_ENUM("DAC NG Rampup Rate", da7213_dac_ng_rampup_rate),
	SOC_ENUM("DAC NG Rampdown Rate", da7213_dac_ng_rampdown_rate),
	SOC_SINGLE("DAC NG OFF Threshold", DA7213_DAC_NG_OFF_THRESHOLD,
		   DA7213_DAC_NG_THRESHOLD_SHIFT, DA7213_DAC_NG_THRESHOLD_MAX,
		   DA7213_NO_INVERT),
	SOC_SINGLE("DAC NG ON Threshold", DA7213_DAC_NG_ON_THRESHOLD,
		   DA7213_DAC_NG_THRESHOLD_SHIFT, DA7213_DAC_NG_THRESHOLD_MAX,
		   DA7213_NO_INVERT),

	/* DAC Routing & Inversion */
	SOC_DOUBLE("DAC Mono Switch", DA7213_DIG_ROUTING_DAC,
		   DA7213_DAC_L_MONO_SHIFT, DA7213_DAC_R_MONO_SHIFT,
		   DA7213_DAC_MONO_MAX, DA7213_NO_INVERT),
	SOC_DOUBLE("DAC Invert Switch", DA7213_DIG_CTRL, DA7213_DAC_L_INV_SHIFT,
		   DA7213_DAC_R_INV_SHIFT, DA7213_DAC_INV_MAX,
		   DA7213_NO_INVERT),

	/* DMIC controls */
	SOC_DOUBLE_R("DMIC Switch", DA7213_MIXIN_L_SELECT,
		     DA7213_MIXIN_R_SELECT, DA7213_DMIC_EN_SHIFT,
		     DA7213_DMIC_EN_MAX, DA7213_NO_INVERT),

	/* ALC Controls */
	SOC_DOUBLE_EXT("ALC Switch", DA7213_ALC_CTRL1, DA7213_ALC_L_EN_SHIFT,
		       DA7213_ALC_R_EN_SHIFT, DA7213_ALC_EN_MAX,
		       DA7213_NO_INVERT, snd_soc_get_volsw, da7213_put_alc_sw),
	SOC_ENUM("ALC Attack Rate", da7213_alc_attack_rate),
	SOC_ENUM("ALC Release Rate", da7213_alc_release_rate),
	SOC_ENUM("ALC Hold Time", da7213_alc_hold_time),
	/*
	 * Rate at which input signal envelope is tracked as the signal gets
	 * larger
	 */
	SOC_ENUM("ALC Integ Attack Rate", da7213_alc_integ_attack_rate),
	/*
	 * Rate at which input signal envelope is tracked as the signal gets
	 * smaller
	 */
	SOC_ENUM("ALC Integ Release Rate", da7213_alc_integ_release_rate),
	SOC_SINGLE_TLV("ALC Noise Threshold Volume", DA7213_ALC_NOISE,
		       DA7213_ALC_THRESHOLD_SHIFT, DA7213_ALC_THRESHOLD_MAX,
		       DA7213_INVERT, alc_threshold_tlv),
	SOC_SINGLE_TLV("ALC Min Threshold Volume", DA7213_ALC_TARGET_MIN,
		       DA7213_ALC_THRESHOLD_SHIFT, DA7213_ALC_THRESHOLD_MAX,
		       DA7213_INVERT, alc_threshold_tlv),
	SOC_SINGLE_TLV("ALC Max Threshold Volume", DA7213_ALC_TARGET_MAX,
		       DA7213_ALC_THRESHOLD_SHIFT, DA7213_ALC_THRESHOLD_MAX,
		       DA7213_INVERT, alc_threshold_tlv),
	SOC_SINGLE_TLV("ALC Max Attenuation Volume", DA7213_ALC_GAIN_LIMITS,
		       DA7213_ALC_ATTEN_MAX_SHIFT,
		       DA7213_ALC_ATTEN_GAIN_MAX_MAX, DA7213_NO_INVERT,
		       alc_gain_tlv),
	SOC_SINGLE_TLV("ALC Max Gain Volume", DA7213_ALC_GAIN_LIMITS,
		       DA7213_ALC_GAIN_MAX_SHIFT, DA7213_ALC_ATTEN_GAIN_MAX_MAX,
		       DA7213_NO_INVERT, alc_gain_tlv),
	SOC_SINGLE_TLV("ALC Min Analog Gain Volume", DA7213_ALC_ANA_GAIN_LIMITS,
		       DA7213_ALC_ANA_GAIN_MIN_SHIFT, DA7213_ALC_ANA_GAIN_MAX,
		       DA7213_NO_INVERT, alc_analog_gain_tlv),
	SOC_SINGLE_TLV("ALC Max Analog Gain Volume", DA7213_ALC_ANA_GAIN_LIMITS,
		       DA7213_ALC_ANA_GAIN_MAX_SHIFT, DA7213_ALC_ANA_GAIN_MAX,
		       DA7213_NO_INVERT, alc_analog_gain_tlv),
	SOC_SINGLE("ALC Anticlip Mode Switch", DA7213_ALC_ANTICLIP_CTRL,
		   DA7213_ALC_ANTICLIP_EN_SHIFT, DA7213_ALC_ANTICLIP_EN_MAX,
		   DA7213_NO_INVERT),
	SOC_SINGLE("ALC Anticlip Level", DA7213_ALC_ANTICLIP_LEVEL,
		   DA7213_ALC_ANTICLIP_LEVEL_SHIFT,
		   DA7213_ALC_ANTICLIP_LEVEL_MAX, DA7213_NO_INVERT),
};


/*
 * DAPM
 */

/*
 * Enums
 */

/* MIC PGA source select */
static const char * const da7213_mic_amp_in_sel_txt[] = {
	"Differential", "MIC_P", "MIC_N"
};

static SOC_ENUM_SINGLE_DECL(da7213_mic_1_amp_in_sel,
			    DA7213_MIC_1_CTRL,
			    DA7213_MIC_AMP_IN_SEL_SHIFT,
			    da7213_mic_amp_in_sel_txt);
static const struct snd_kcontrol_new da7213_mic_1_amp_in_sel_mux =
	SOC_DAPM_ENUM("Mic 1 Amp Source MUX", da7213_mic_1_amp_in_sel);

static SOC_ENUM_SINGLE_DECL(da7213_mic_2_amp_in_sel,
			    DA7213_MIC_2_CTRL,
			    DA7213_MIC_AMP_IN_SEL_SHIFT,
			    da7213_mic_amp_in_sel_txt);
static const struct snd_kcontrol_new da7213_mic_2_amp_in_sel_mux =
	SOC_DAPM_ENUM("Mic 2 Amp Source MUX", da7213_mic_2_amp_in_sel);

/* DAI routing select */
static const char * const da7213_dai_src_txt[] = {
	"ADC Left", "ADC Right", "DAI Input Left", "DAI Input Right"
};

static SOC_ENUM_SINGLE_DECL(da7213_dai_l_src,
			    DA7213_DIG_ROUTING_DAI,
			    DA7213_DAI_L_SRC_SHIFT,
			    da7213_dai_src_txt);
static const struct snd_kcontrol_new da7213_dai_l_src_mux =
	SOC_DAPM_ENUM("DAI Left Source MUX", da7213_dai_l_src);

static SOC_ENUM_SINGLE_DECL(da7213_dai_r_src,
			    DA7213_DIG_ROUTING_DAI,
			    DA7213_DAI_R_SRC_SHIFT,
			    da7213_dai_src_txt);
static const struct snd_kcontrol_new da7213_dai_r_src_mux =
	SOC_DAPM_ENUM("DAI Right Source MUX", da7213_dai_r_src);

/* DAC routing select */
static const char * const da7213_dac_src_txt[] = {
	"ADC Output Left", "ADC Output Right", "DAI Input Left",
	"DAI Input Right"
};

static SOC_ENUM_SINGLE_DECL(da7213_dac_l_src,
			    DA7213_DIG_ROUTING_DAC,
			    DA7213_DAC_L_SRC_SHIFT,
			    da7213_dac_src_txt);
static const struct snd_kcontrol_new da7213_dac_l_src_mux =
	SOC_DAPM_ENUM("DAC Left Source MUX", da7213_dac_l_src);

static SOC_ENUM_SINGLE_DECL(da7213_dac_r_src,
			    DA7213_DIG_ROUTING_DAC,
			    DA7213_DAC_R_SRC_SHIFT,
			    da7213_dac_src_txt);
static const struct snd_kcontrol_new da7213_dac_r_src_mux =
	SOC_DAPM_ENUM("DAC Right Source MUX", da7213_dac_r_src);

/*
 * Mixer Controls
 */

/* Mixin Left */
static const struct snd_kcontrol_new da7213_dapm_mixinl_controls[] = {
	SOC_DAPM_SINGLE("Aux Left Switch", DA7213_MIXIN_L_SELECT,
			DA7213_MIXIN_L_MIX_SELECT_AUX_L_SHIFT,
			DA7213_MIXIN_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mic 1 Switch", DA7213_MIXIN_L_SELECT,
			DA7213_MIXIN_L_MIX_SELECT_MIC_1_SHIFT,
			DA7213_MIXIN_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mic 2 Switch", DA7213_MIXIN_L_SELECT,
			DA7213_MIXIN_L_MIX_SELECT_MIC_2_SHIFT,
			DA7213_MIXIN_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Right Switch", DA7213_MIXIN_L_SELECT,
			DA7213_MIXIN_L_MIX_SELECT_MIXIN_R_SHIFT,
			DA7213_MIXIN_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
};

/* Mixin Right */
static const struct snd_kcontrol_new da7213_dapm_mixinr_controls[] = {
	SOC_DAPM_SINGLE("Aux Right Switch", DA7213_MIXIN_R_SELECT,
			DA7213_MIXIN_R_MIX_SELECT_AUX_R_SHIFT,
			DA7213_MIXIN_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mic 2 Switch", DA7213_MIXIN_R_SELECT,
			DA7213_MIXIN_R_MIX_SELECT_MIC_2_SHIFT,
			DA7213_MIXIN_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mic 1 Switch", DA7213_MIXIN_R_SELECT,
			DA7213_MIXIN_R_MIX_SELECT_MIC_1_SHIFT,
			DA7213_MIXIN_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Left Switch", DA7213_MIXIN_R_SELECT,
			DA7213_MIXIN_R_MIX_SELECT_MIXIN_L_SHIFT,
			DA7213_MIXIN_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
};

/* Mixout Left */
static const struct snd_kcontrol_new da7213_dapm_mixoutl_controls[] = {
	SOC_DAPM_SINGLE("Aux Left Switch", DA7213_MIXOUT_L_SELECT,
			DA7213_MIXOUT_L_MIX_SELECT_AUX_L_SHIFT,
			DA7213_MIXOUT_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Left Switch", DA7213_MIXOUT_L_SELECT,
			DA7213_MIXOUT_L_MIX_SELECT_MIXIN_L_SHIFT,
			DA7213_MIXOUT_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Right Switch", DA7213_MIXOUT_L_SELECT,
			DA7213_MIXOUT_L_MIX_SELECT_MIXIN_R_SHIFT,
			DA7213_MIXOUT_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("DAC Left Switch", DA7213_MIXOUT_L_SELECT,
			DA7213_MIXOUT_L_MIX_SELECT_DAC_L_SHIFT,
			DA7213_MIXOUT_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Aux Left Invert Switch", DA7213_MIXOUT_L_SELECT,
			DA7213_MIXOUT_L_MIX_SELECT_AUX_L_INVERTED_SHIFT,
			DA7213_MIXOUT_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Left Invert Switch", DA7213_MIXOUT_L_SELECT,
			DA7213_MIXOUT_L_MIX_SELECT_MIXIN_L_INVERTED_SHIFT,
			DA7213_MIXOUT_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Right Invert Switch", DA7213_MIXOUT_L_SELECT,
			DA7213_MIXOUT_L_MIX_SELECT_MIXIN_R_INVERTED_SHIFT,
			DA7213_MIXOUT_L_MIX_SELECT_MAX, DA7213_NO_INVERT),
};

/* Mixout Right */
static const struct snd_kcontrol_new da7213_dapm_mixoutr_controls[] = {
	SOC_DAPM_SINGLE("Aux Right Switch", DA7213_MIXOUT_R_SELECT,
			DA7213_MIXOUT_R_MIX_SELECT_AUX_R_SHIFT,
			DA7213_MIXOUT_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Right Switch", DA7213_MIXOUT_R_SELECT,
			DA7213_MIXOUT_R_MIX_SELECT_MIXIN_R_SHIFT,
			DA7213_MIXOUT_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Left Switch", DA7213_MIXOUT_R_SELECT,
			DA7213_MIXOUT_R_MIX_SELECT_MIXIN_L_SHIFT,
			DA7213_MIXOUT_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("DAC Right Switch", DA7213_MIXOUT_R_SELECT,
			DA7213_MIXOUT_R_MIX_SELECT_DAC_R_SHIFT,
			DA7213_MIXOUT_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Aux Right Invert Switch", DA7213_MIXOUT_R_SELECT,
			DA7213_MIXOUT_R_MIX_SELECT_AUX_R_INVERTED_SHIFT,
			DA7213_MIXOUT_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Right Invert Switch", DA7213_MIXOUT_R_SELECT,
			DA7213_MIXOUT_R_MIX_SELECT_MIXIN_R_INVERTED_SHIFT,
			DA7213_MIXOUT_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
	SOC_DAPM_SINGLE("Mixin Left Invert Switch", DA7213_MIXOUT_R_SELECT,
			DA7213_MIXOUT_R_MIX_SELECT_MIXIN_L_INVERTED_SHIFT,
			DA7213_MIXOUT_R_MIX_SELECT_MAX, DA7213_NO_INVERT),
};


/*
 * DAPM Events
 */

static int da7213_dai_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct da7213_priv *da7213 = snd_soc_component_get_drvdata(component);
	u8 pll_ctrl, pll_status;
	int i = 0;
	bool srm_lock = false;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable DAI clks for master mode */
		if (da7213->master)
			snd_soc_component_update_bits(component, DA7213_DAI_CLK_MODE,
					    DA7213_DAI_CLK_EN_MASK,
					    DA7213_DAI_CLK_EN_MASK);

		/* PC synchronised to DAI */
		snd_soc_component_update_bits(component, DA7213_PC_COUNT,
				    DA7213_PC_FREERUN_MASK, 0);

		/* If SRM not enabled then nothing more to do */
		pll_ctrl = snd_soc_component_read32(component, DA7213_PLL_CTRL);
		if (!(pll_ctrl & DA7213_PLL_SRM_EN))
			return 0;

		/* Assist 32KHz mode PLL lock */
		if (pll_ctrl & DA7213_PLL_32K_MODE) {
			snd_soc_component_write(component, 0xF0, 0x8B);
			snd_soc_component_write(component, 0xF2, 0x03);
			snd_soc_component_write(component, 0xF0, 0x00);
		}

		/* Check SRM has locked */
		do {
			pll_status = snd_soc_component_read32(component, DA7213_PLL_STATUS);
			if (pll_status & DA7219_PLL_SRM_LOCK) {
				srm_lock = true;
			} else {
				++i;
				msleep(50);
			}
		} while ((i < DA7213_SRM_CHECK_RETRIES) && (!srm_lock));

		if (!srm_lock)
			dev_warn(component->dev, "SRM failed to lock\n");

		return 0;
	case SND_SOC_DAPM_POST_PMD:
		/* Revert 32KHz PLL lock udpates if applied previously */
		pll_ctrl = snd_soc_component_read32(component, DA7213_PLL_CTRL);
		if (pll_ctrl & DA7213_PLL_32K_MODE) {
			snd_soc_component_write(component, 0xF0, 0x8B);
			snd_soc_component_write(component, 0xF2, 0x01);
			snd_soc_component_write(component, 0xF0, 0x00);
		}

		/* PC free-running */
		snd_soc_component_update_bits(component, DA7213_PC_COUNT,
				    DA7213_PC_FREERUN_MASK,
				    DA7213_PC_FREERUN_MASK);

		/* Disable DAI clks if in master mode */
		if (da7213->master)
			snd_soc_component_update_bits(component, DA7213_DAI_CLK_MODE,
					    DA7213_DAI_CLK_EN_MASK, 0);
		return 0;
	default:
		return -EINVAL;
	}
}


/*
 * DAPM widgets
 */

static const struct snd_soc_dapm_widget da7213_dapm_widgets[] = {
	/*
	 * Input & Output
	 */

	/* Use a supply here as this controls both input & output DAIs */
	SND_SOC_DAPM_SUPPLY("DAI", DA7213_DAI_CTRL, DA7213_DAI_EN_SHIFT,
			    DA7213_NO_INVERT, da7213_dai_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/*
	 * Input
	 */

	/* Input Lines */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("AUXL"),
	SND_SOC_DAPM_INPUT("AUXR"),

	/* MUXs for Mic PGA source selection */
	SND_SOC_DAPM_MUX("Mic 1 Amp Source MUX", SND_SOC_NOPM, 0, 0,
			 &da7213_mic_1_amp_in_sel_mux),
	SND_SOC_DAPM_MUX("Mic 2 Amp Source MUX", SND_SOC_NOPM, 0, 0,
			 &da7213_mic_2_amp_in_sel_mux),

	/* Input PGAs */
	SND_SOC_DAPM_PGA("Mic 1 PGA", DA7213_MIC_1_CTRL, DA7213_AMP_EN_SHIFT,
			 DA7213_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_PGA("Mic 2 PGA", DA7213_MIC_2_CTRL, DA7213_AMP_EN_SHIFT,
			 DA7213_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_PGA("Aux Left PGA", DA7213_AUX_L_CTRL, DA7213_AMP_EN_SHIFT,
			 DA7213_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_PGA("Aux Right PGA", DA7213_AUX_R_CTRL,
			 DA7213_AMP_EN_SHIFT, DA7213_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_PGA("Mixin Left PGA", DA7213_MIXIN_L_CTRL,
			 DA7213_AMP_EN_SHIFT, DA7213_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_PGA("Mixin Right PGA", DA7213_MIXIN_R_CTRL,
			 DA7213_AMP_EN_SHIFT, DA7213_NO_INVERT, NULL, 0),

	/* Mic Biases */
	SND_SOC_DAPM_SUPPLY("Mic Bias 1", DA7213_MICBIAS_CTRL,
			    DA7213_MICBIAS1_EN_SHIFT, DA7213_NO_INVERT,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Bias 2", DA7213_MICBIAS_CTRL,
			    DA7213_MICBIAS2_EN_SHIFT, DA7213_NO_INVERT,
			    NULL, 0),

	/* Input Mixers */
	SND_SOC_DAPM_MIXER("Mixin Left", SND_SOC_NOPM, 0, 0,
			   &da7213_dapm_mixinl_controls[0],
			   ARRAY_SIZE(da7213_dapm_mixinl_controls)),
	SND_SOC_DAPM_MIXER("Mixin Right", SND_SOC_NOPM, 0, 0,
			   &da7213_dapm_mixinr_controls[0],
			   ARRAY_SIZE(da7213_dapm_mixinr_controls)),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC Left", NULL, DA7213_ADC_L_CTRL,
			 DA7213_ADC_EN_SHIFT, DA7213_NO_INVERT),
	SND_SOC_DAPM_ADC("ADC Right", NULL, DA7213_ADC_R_CTRL,
			 DA7213_ADC_EN_SHIFT, DA7213_NO_INVERT),

	/* DAI */
	SND_SOC_DAPM_MUX("DAI Left Source MUX", SND_SOC_NOPM, 0, 0,
			 &da7213_dai_l_src_mux),
	SND_SOC_DAPM_MUX("DAI Right Source MUX", SND_SOC_NOPM, 0, 0,
			 &da7213_dai_r_src_mux),
	SND_SOC_DAPM_AIF_OUT("DAIOUTL", "Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DAIOUTR", "Capture", 1, SND_SOC_NOPM, 0, 0),

	/*
	 * Output
	 */

	/* DAI */
	SND_SOC_DAPM_AIF_IN("DAIINL", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DAIINR", "Playback", 1, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MUX("DAC Left Source MUX", SND_SOC_NOPM, 0, 0,
			 &da7213_dac_l_src_mux),
	SND_SOC_DAPM_MUX("DAC Right Source MUX", SND_SOC_NOPM, 0, 0,
			 &da7213_dac_r_src_mux),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC Left", NULL, DA7213_DAC_L_CTRL,
			 DA7213_DAC_EN_SHIFT, DA7213_NO_INVERT),
	SND_SOC_DAPM_DAC("DAC Right", NULL, DA7213_DAC_R_CTRL,
			 DA7213_DAC_EN_SHIFT, DA7213_NO_INVERT),

	/* Output Mixers */
	SND_SOC_DAPM_MIXER("Mixout Left", SND_SOC_NOPM, 0, 0,
			   &da7213_dapm_mixoutl_controls[0],
			   ARRAY_SIZE(da7213_dapm_mixoutl_controls)),
	SND_SOC_DAPM_MIXER("Mixout Right", SND_SOC_NOPM, 0, 0,
			   &da7213_dapm_mixoutr_controls[0],
			   ARRAY_SIZE(da7213_dapm_mixoutr_controls)),

	/* Output PGAs */
	SND_SOC_DAPM_PGA("Mixout Left PGA", DA7213_MIXOUT_L_CTRL,
			 DA7213_AMP_EN_SHIFT, DA7213_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_PGA("Mixout Right PGA", DA7213_MIXOUT_R_CTRL,
			 DA7213_AMP_EN_SHIFT, DA7213_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_PGA("Lineout PGA", DA7213_LINE_CTRL, DA7213_AMP_EN_SHIFT,
			 DA7213_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_PGA("Headphone Left PGA", DA7213_HP_L_CTRL,
			 DA7213_AMP_EN_SHIFT, DA7213_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_PGA("Headphone Right PGA", DA7213_HP_R_CTRL,
			 DA7213_AMP_EN_SHIFT, DA7213_NO_INVERT, NULL, 0),

	/* Charge Pump */
	SND_SOC_DAPM_SUPPLY("Charge Pump", DA7213_CP_CTRL, DA7213_CP_EN_SHIFT,
			    DA7213_NO_INVERT, NULL, 0),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("LINE"),
};


/*
 * DAPM audio route definition
 */

static const struct snd_soc_dapm_route da7213_audio_map[] = {
	/* Dest       Connecting Widget    source */

	/* Input path */
	{"MIC1", NULL, "Mic Bias 1"},
	{"MIC2", NULL, "Mic Bias 2"},

	{"Mic 1 Amp Source MUX", "Differential", "MIC1"},
	{"Mic 1 Amp Source MUX", "MIC_P", "MIC1"},
	{"Mic 1 Amp Source MUX", "MIC_N", "MIC1"},

	{"Mic 2 Amp Source MUX", "Differential", "MIC2"},
	{"Mic 2 Amp Source MUX", "MIC_P", "MIC2"},
	{"Mic 2 Amp Source MUX", "MIC_N", "MIC2"},

	{"Mic 1 PGA", NULL, "Mic 1 Amp Source MUX"},
	{"Mic 2 PGA", NULL, "Mic 2 Amp Source MUX"},

	{"Aux Left PGA", NULL, "AUXL"},
	{"Aux Right PGA", NULL, "AUXR"},

	{"Mixin Left", "Aux Left Switch", "Aux Left PGA"},
	{"Mixin Left", "Mic 1 Switch", "Mic 1 PGA"},
	{"Mixin Left", "Mic 2 Switch", "Mic 2 PGA"},
	{"Mixin Left", "Mixin Right Switch", "Mixin Right PGA"},

	{"Mixin Right", "Aux Right Switch", "Aux Right PGA"},
	{"Mixin Right", "Mic 2 Switch", "Mic 2 PGA"},
	{"Mixin Right", "Mic 1 Switch", "Mic 1 PGA"},
	{"Mixin Right", "Mixin Left Switch", "Mixin Left PGA"},

	{"Mixin Left PGA", NULL, "Mixin Left"},
	{"ADC Left", NULL, "Mixin Left PGA"},

	{"Mixin Right PGA", NULL, "Mixin Right"},
	{"ADC Right", NULL, "Mixin Right PGA"},

	{"DAI Left Source MUX", "ADC Left", "ADC Left"},
	{"DAI Left Source MUX", "ADC Right", "ADC Right"},
	{"DAI Left Source MUX", "DAI Input Left", "DAIINL"},
	{"DAI Left Source MUX", "DAI Input Right", "DAIINR"},

	{"DAI Right Source MUX", "ADC Left", "ADC Left"},
	{"DAI Right Source MUX", "ADC Right", "ADC Right"},
	{"DAI Right Source MUX", "DAI Input Left", "DAIINL"},
	{"DAI Right Source MUX", "DAI Input Right", "DAIINR"},

	{"DAIOUTL", NULL, "DAI Left Source MUX"},
	{"DAIOUTR", NULL, "DAI Right Source MUX"},

	{"DAIOUTL", NULL, "DAI"},
	{"DAIOUTR", NULL, "DAI"},

	/* Output path */
	{"DAIINL", NULL, "DAI"},
	{"DAIINR", NULL, "DAI"},

	{"DAC Left Source MUX", "ADC Output Left", "ADC Left"},
	{"DAC Left Source MUX", "ADC Output Right", "ADC Right"},
	{"DAC Left Source MUX", "DAI Input Left", "DAIINL"},
	{"DAC Left Source MUX", "DAI Input Right", "DAIINR"},

	{"DAC Right Source MUX", "ADC Output Left", "ADC Left"},
	{"DAC Right Source MUX", "ADC Output Right", "ADC Right"},
	{"DAC Right Source MUX", "DAI Input Left", "DAIINL"},
	{"DAC Right Source MUX", "DAI Input Right", "DAIINR"},

	{"DAC Left", NULL, "DAC Left Source MUX"},
	{"DAC Right", NULL, "DAC Right Source MUX"},

	{"Mixout Left", "Aux Left Switch", "Aux Left PGA"},
	{"Mixout Left", "Mixin Left Switch", "Mixin Left PGA"},
	{"Mixout Left", "Mixin Right Switch", "Mixin Right PGA"},
	{"Mixout Left", "DAC Left Switch", "DAC Left"},
	{"Mixout Left", "Aux Left Invert Switch", "Aux Left PGA"},
	{"Mixout Left", "Mixin Left Invert Switch", "Mixin Left PGA"},
	{"Mixout Left", "Mixin Right Invert Switch", "Mixin Right PGA"},

	{"Mixout Right", "Aux Right Switch", "Aux Right PGA"},
	{"Mixout Right", "Mixin Right Switch", "Mixin Right PGA"},
	{"Mixout Right", "Mixin Left Switch", "Mixin Left PGA"},
	{"Mixout Right", "DAC Right Switch", "DAC Right"},
	{"Mixout Right", "Aux Right Invert Switch", "Aux Right PGA"},
	{"Mixout Right", "Mixin Right Invert Switch", "Mixin Right PGA"},
	{"Mixout Right", "Mixin Left Invert Switch", "Mixin Left PGA"},

	{"Mixout Left PGA", NULL, "Mixout Left"},
	{"Mixout Right PGA", NULL, "Mixout Right"},

	{"Headphone Left PGA", NULL, "Mixout Left PGA"},
	{"Headphone Left PGA", NULL, "Charge Pump"},
	{"HPL", NULL, "Headphone Left PGA"},

	{"Headphone Right PGA", NULL, "Mixout Right PGA"},
	{"Headphone Right PGA", NULL, "Charge Pump"},
	{"HPR", NULL, "Headphone Right PGA"},

	{"Lineout PGA", NULL, "Mixout Right PGA"},
	{"LINE", NULL, "Lineout PGA"},
};

static const struct reg_default da7213_reg_defaults[] = {
	{ DA7213_DIG_ROUTING_DAI, 0x10 },
	{ DA7213_SR, 0x0A },
	{ DA7213_REFERENCES, 0x80 },
	{ DA7213_PLL_FRAC_TOP, 0x00 },
	{ DA7213_PLL_FRAC_BOT, 0x00 },
	{ DA7213_PLL_INTEGER, 0x20 },
	{ DA7213_PLL_CTRL, 0x0C },
	{ DA7213_DAI_CLK_MODE, 0x01 },
	{ DA7213_DAI_CTRL, 0x08 },
	{ DA7213_DIG_ROUTING_DAC, 0x32 },
	{ DA7213_AUX_L_GAIN, 0x35 },
	{ DA7213_AUX_R_GAIN, 0x35 },
	{ DA7213_MIXIN_L_SELECT, 0x00 },
	{ DA7213_MIXIN_R_SELECT, 0x00 },
	{ DA7213_MIXIN_L_GAIN, 0x03 },
	{ DA7213_MIXIN_R_GAIN, 0x03 },
	{ DA7213_ADC_L_GAIN, 0x6F },
	{ DA7213_ADC_R_GAIN, 0x6F },
	{ DA7213_ADC_FILTERS1, 0x80 },
	{ DA7213_MIC_1_GAIN, 0x01 },
	{ DA7213_MIC_2_GAIN, 0x01 },
	{ DA7213_DAC_FILTERS5, 0x00 },
	{ DA7213_DAC_FILTERS2, 0x88 },
	{ DA7213_DAC_FILTERS3, 0x88 },
	{ DA7213_DAC_FILTERS4, 0x08 },
	{ DA7213_DAC_FILTERS1, 0x80 },
	{ DA7213_DAC_L_GAIN, 0x6F },
	{ DA7213_DAC_R_GAIN, 0x6F },
	{ DA7213_CP_CTRL, 0x61 },
	{ DA7213_HP_L_GAIN, 0x39 },
	{ DA7213_HP_R_GAIN, 0x39 },
	{ DA7213_LINE_GAIN, 0x30 },
	{ DA7213_MIXOUT_L_SELECT, 0x00 },
	{ DA7213_MIXOUT_R_SELECT, 0x00 },
	{ DA7213_SYSTEM_MODES_INPUT, 0x00 },
	{ DA7213_SYSTEM_MODES_OUTPUT, 0x00 },
	{ DA7213_AUX_L_CTRL, 0x44 },
	{ DA7213_AUX_R_CTRL, 0x44 },
	{ DA7213_MICBIAS_CTRL, 0x11 },
	{ DA7213_MIC_1_CTRL, 0x40 },
	{ DA7213_MIC_2_CTRL, 0x40 },
	{ DA7213_MIXIN_L_CTRL, 0x40 },
	{ DA7213_MIXIN_R_CTRL, 0x40 },
	{ DA7213_ADC_L_CTRL, 0x40 },
	{ DA7213_ADC_R_CTRL, 0x40 },
	{ DA7213_DAC_L_CTRL, 0x48 },
	{ DA7213_DAC_R_CTRL, 0x40 },
	{ DA7213_HP_L_CTRL, 0x41 },
	{ DA7213_HP_R_CTRL, 0x40 },
	{ DA7213_LINE_CTRL, 0x40 },
	{ DA7213_MIXOUT_L_CTRL, 0x10 },
	{ DA7213_MIXOUT_R_CTRL, 0x10 },
	{ DA7213_LDO_CTRL, 0x00 },
	{ DA7213_IO_CTRL, 0x00 },
	{ DA7213_GAIN_RAMP_CTRL, 0x00},
	{ DA7213_MIC_CONFIG, 0x00 },
	{ DA7213_PC_COUNT, 0x00 },
	{ DA7213_CP_VOL_THRESHOLD1, 0x32 },
	{ DA7213_CP_DELAY, 0x95 },
	{ DA7213_CP_DETECTOR, 0x00 },
	{ DA7213_DAI_OFFSET, 0x00 },
	{ DA7213_DIG_CTRL, 0x00 },
	{ DA7213_ALC_CTRL2, 0x00 },
	{ DA7213_ALC_CTRL3, 0x00 },
	{ DA7213_ALC_NOISE, 0x3F },
	{ DA7213_ALC_TARGET_MIN, 0x3F },
	{ DA7213_ALC_TARGET_MAX, 0x00 },
	{ DA7213_ALC_GAIN_LIMITS, 0xFF },
	{ DA7213_ALC_ANA_GAIN_LIMITS, 0x71 },
	{ DA7213_ALC_ANTICLIP_CTRL, 0x00 },
	{ DA7213_ALC_ANTICLIP_LEVEL, 0x00 },
	{ DA7213_ALC_OFFSET_MAN_M_L, 0x00 },
	{ DA7213_ALC_OFFSET_MAN_U_L, 0x00 },
	{ DA7213_ALC_OFFSET_MAN_M_R, 0x00 },
	{ DA7213_ALC_OFFSET_MAN_U_R, 0x00 },
	{ DA7213_ALC_CIC_OP_LVL_CTRL, 0x00 },
	{ DA7213_DAC_NG_SETUP_TIME, 0x00 },
	{ DA7213_DAC_NG_OFF_THRESHOLD, 0x00 },
	{ DA7213_DAC_NG_ON_THRESHOLD, 0x00 },
	{ DA7213_DAC_NG_CTRL, 0x00 },
};

static bool da7213_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA7213_STATUS1:
	case DA7213_PLL_STATUS:
	case DA7213_AUX_L_GAIN_STATUS:
	case DA7213_AUX_R_GAIN_STATUS:
	case DA7213_MIC_1_GAIN_STATUS:
	case DA7213_MIC_2_GAIN_STATUS:
	case DA7213_MIXIN_L_GAIN_STATUS:
	case DA7213_MIXIN_R_GAIN_STATUS:
	case DA7213_ADC_L_GAIN_STATUS:
	case DA7213_ADC_R_GAIN_STATUS:
	case DA7213_DAC_L_GAIN_STATUS:
	case DA7213_DAC_R_GAIN_STATUS:
	case DA7213_HP_L_GAIN_STATUS:
	case DA7213_HP_R_GAIN_STATUS:
	case DA7213_LINE_GAIN_STATUS:
	case DA7213_ALC_CTRL1:
	case DA7213_ALC_OFFSET_AUTO_M_L:
	case DA7213_ALC_OFFSET_AUTO_U_L:
	case DA7213_ALC_OFFSET_AUTO_M_R:
	case DA7213_ALC_OFFSET_AUTO_U_R:
	case DA7213_ALC_CIC_OP_LVL_DATA:
		return 1;
	default:
		return 0;
	}
}

static int da7213_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	u8 dai_ctrl = 0;
	u8 fs;

	/* Set DAI format */
	switch (params_width(params)) {
	case 16:
		dai_ctrl |= DA7213_DAI_WORD_LENGTH_S16_LE;
		break;
	case 20:
		dai_ctrl |= DA7213_DAI_WORD_LENGTH_S20_LE;
		break;
	case 24:
		dai_ctrl |= DA7213_DAI_WORD_LENGTH_S24_LE;
		break;
	case 32:
		dai_ctrl |= DA7213_DAI_WORD_LENGTH_S32_LE;
		break;
	default:
		return -EINVAL;
	}

	/* Set sampling rate */
	switch (params_rate(params)) {
	case 8000:
		fs = DA7213_SR_8000;
		break;
	case 11025:
		fs = DA7213_SR_11025;
		break;
	case 12000:
		fs = DA7213_SR_12000;
		break;
	case 16000:
		fs = DA7213_SR_16000;
		break;
	case 22050:
		fs = DA7213_SR_22050;
		break;
	case 32000:
		fs = DA7213_SR_32000;
		break;
	case 44100:
		fs = DA7213_SR_44100;
		break;
	case 48000:
		fs = DA7213_SR_48000;
		break;
	case 88200:
		fs = DA7213_SR_88200;
		break;
	case 96000:
		fs = DA7213_SR_96000;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, DA7213_DAI_CTRL, DA7213_DAI_WORD_LENGTH_MASK,
			    dai_ctrl);
	snd_soc_component_write(component, DA7213_SR, fs);

	return 0;
}

static int da7213_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct da7213_priv *da7213 = snd_soc_component_get_drvdata(component);
	u8 dai_clk_mode = 0, dai_ctrl = 0;
	u8 dai_offset = 0;

	/* Set master/slave mode */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		da7213->master = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		da7213->master = false;
		break;
	default:
		return -EINVAL;
	}

	/* Set clock normal/inverted */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_NB_IF:
			dai_clk_mode |= DA7213_DAI_WCLK_POL_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			dai_clk_mode |= DA7213_DAI_CLK_POL_INV;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			dai_clk_mode |= DA7213_DAI_WCLK_POL_INV |
					DA7213_DAI_CLK_POL_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAI_FORMAT_DSP_A:
	case SND_SOC_DAI_FORMAT_DSP_B:
		/* The bclk is inverted wrt ASoC conventions */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			dai_clk_mode |= DA7213_DAI_CLK_POL_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			dai_clk_mode |= DA7213_DAI_WCLK_POL_INV |
					DA7213_DAI_CLK_POL_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			dai_clk_mode |= DA7213_DAI_WCLK_POL_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	/* Only I2S is supported */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dai_ctrl |= DA7213_DAI_FORMAT_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dai_ctrl |= DA7213_DAI_FORMAT_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		dai_ctrl |= DA7213_DAI_FORMAT_RIGHT_J;
		break;
	case SND_SOC_DAI_FORMAT_DSP_A: /* L data MSB after FRM LRC */
		dai_ctrl |= DA7213_DAI_FORMAT_DSP;
		dai_offset = 1;
		break;
	case SND_SOC_DAI_FORMAT_DSP_B: /* L data MSB during FRM LRC */
		dai_ctrl |= DA7213_DAI_FORMAT_DSP;
		break;
	default:
		return -EINVAL;
	}

	/* By default only 64 BCLK per WCLK is supported */
	dai_clk_mode |= DA7213_DAI_BCLKS_PER_WCLK_64;

	snd_soc_component_write(component, DA7213_DAI_CLK_MODE, dai_clk_mode);
	snd_soc_component_update_bits(component, DA7213_DAI_CTRL, DA7213_DAI_FORMAT_MASK,
			    dai_ctrl);
	snd_soc_component_write(component, DA7213_DAI_OFFSET, dai_offset);

	return 0;
}

static int da7213_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;

	if (mute) {
		snd_soc_component_update_bits(component, DA7213_DAC_L_CTRL,
				    DA7213_MUTE_EN, DA7213_MUTE_EN);
		snd_soc_component_update_bits(component, DA7213_DAC_R_CTRL,
				    DA7213_MUTE_EN, DA7213_MUTE_EN);
	} else {
		snd_soc_component_update_bits(component, DA7213_DAC_L_CTRL,
				    DA7213_MUTE_EN, 0);
		snd_soc_component_update_bits(component, DA7213_DAC_R_CTRL,
				    DA7213_MUTE_EN, 0);
	}

	return 0;
}

#define DA7213_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static int da7213_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct da7213_priv *da7213 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	if ((da7213->clk_src == clk_id) && (da7213->mclk_rate == freq))
		return 0;

	if (((freq < 5000000) && (freq != 32768)) || (freq > 54000000)) {
		dev_err(codec_dai->dev, "Unsupported MCLK value %d\n",
			freq);
		return -EINVAL;
	}

	switch (clk_id) {
	case DA7213_CLKSRC_MCLK:
		snd_soc_component_update_bits(component, DA7213_PLL_CTRL,
				    DA7213_PLL_MCLK_SQR_EN, 0);
		break;
	case DA7213_CLKSRC_MCLK_SQR:
		snd_soc_component_update_bits(component, DA7213_PLL_CTRL,
				    DA7213_PLL_MCLK_SQR_EN,
				    DA7213_PLL_MCLK_SQR_EN);
		break;
	default:
		dev_err(codec_dai->dev, "Unknown clock source %d\n", clk_id);
		return -EINVAL;
	}

	da7213->clk_src = clk_id;

	if (da7213->mclk) {
		freq = clk_round_rate(da7213->mclk, freq);
		ret = clk_set_rate(da7213->mclk, freq);
		if (ret) {
			dev_err(codec_dai->dev, "Failed to set clock rate %d\n",
				freq);
			return ret;
		}
	}

	da7213->mclk_rate = freq;

	return 0;
}

/* Supported PLL input frequencies are 32KHz, 5MHz - 54MHz. */
static int da7213_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
			      int source, unsigned int fref, unsigned int fout)
{
	struct snd_soc_component *component = codec_dai->component;
	struct da7213_priv *da7213 = snd_soc_component_get_drvdata(component);

	u8 pll_ctrl, indiv_bits, indiv;
	u8 pll_frac_top, pll_frac_bot, pll_integer;
	u32 freq_ref;
	u64 frac_div;

	/* Workout input divider based on MCLK rate */
	if (da7213->mclk_rate == 32768) {
		if (!da7213->master) {
			dev_err(component->dev,
				"32KHz only valid if codec is clock master\n");
			return -EINVAL;
		}

		/* 32KHz PLL Mode */
		indiv_bits = DA7213_PLL_INDIV_9_TO_18_MHZ;
		indiv = DA7213_PLL_INDIV_9_TO_18_MHZ_VAL;
		source = DA7213_SYSCLK_PLL_32KHZ;
		freq_ref = 3750000;

	} else {
		if (da7213->mclk_rate < 5000000) {
			dev_err(component->dev,
				"PLL input clock %d below valid range\n",
				da7213->mclk_rate);
			return -EINVAL;
		} else if (da7213->mclk_rate <= 9000000) {
			indiv_bits = DA7213_PLL_INDIV_5_TO_9_MHZ;
			indiv = DA7213_PLL_INDIV_5_TO_9_MHZ_VAL;
		} else if (da7213->mclk_rate <= 18000000) {
			indiv_bits = DA7213_PLL_INDIV_9_TO_18_MHZ;
			indiv = DA7213_PLL_INDIV_9_TO_18_MHZ_VAL;
		} else if (da7213->mclk_rate <= 36000000) {
			indiv_bits = DA7213_PLL_INDIV_18_TO_36_MHZ;
			indiv = DA7213_PLL_INDIV_18_TO_36_MHZ_VAL;
		} else if (da7213->mclk_rate <= 54000000) {
			indiv_bits = DA7213_PLL_INDIV_36_TO_54_MHZ;
			indiv = DA7213_PLL_INDIV_36_TO_54_MHZ_VAL;
		} else {
			dev_err(component->dev,
				"PLL input clock %d above valid range\n",
				da7213->mclk_rate);
			return -EINVAL;
		}
		freq_ref = (da7213->mclk_rate / indiv);
	}

	pll_ctrl = indiv_bits;

	/* Configure PLL */
	switch (source) {
	case DA7213_SYSCLK_MCLK:
		snd_soc_component_update_bits(component, DA7213_PLL_CTRL,
				    DA7213_PLL_INDIV_MASK |
				    DA7213_PLL_MODE_MASK, pll_ctrl);
		return 0;
	case DA7213_SYSCLK_PLL:
		break;
	case DA7213_SYSCLK_PLL_SRM:
		pll_ctrl |= DA7213_PLL_SRM_EN;
		fout = DA7213_PLL_FREQ_OUT_94310400;
		break;
	case DA7213_SYSCLK_PLL_32KHZ:
		if (da7213->mclk_rate != 32768) {
			dev_err(component->dev,
				"32KHz mode only valid with 32KHz MCLK\n");
			return -EINVAL;
		}

		pll_ctrl |= DA7213_PLL_32K_MODE | DA7213_PLL_SRM_EN;
		fout = DA7213_PLL_FREQ_OUT_94310400;
		break;
	default:
		dev_err(component->dev, "Invalid PLL config\n");
		return -EINVAL;
	}

	/* Calculate dividers for PLL */
	pll_integer = fout / freq_ref;
	frac_div = (u64)(fout % freq_ref) * 8192ULL;
	do_div(frac_div, freq_ref);
	pll_frac_top = (frac_div >> DA7213_BYTE_SHIFT) & DA7213_BYTE_MASK;
	pll_frac_bot = (frac_div) & DA7213_BYTE_MASK;

	/* Write PLL dividers */
	snd_soc_component_write(component, DA7213_PLL_FRAC_TOP, pll_frac_top);
	snd_soc_component_write(component, DA7213_PLL_FRAC_BOT, pll_frac_bot);
	snd_soc_component_write(component, DA7213_PLL_INTEGER, pll_integer);

	/* Enable PLL */
	pll_ctrl |= DA7213_PLL_EN;
	snd_soc_component_update_bits(component, DA7213_PLL_CTRL,
			    DA7213_PLL_INDIV_MASK | DA7213_PLL_MODE_MASK,
			    pll_ctrl);

	/* Assist 32KHz mode PLL lock */
	if (source == DA7213_SYSCLK_PLL_32KHZ) {
		snd_soc_component_write(component, 0xF0, 0x8B);
		snd_soc_component_write(component, 0xF1, 0x03);
		snd_soc_component_write(component, 0xF1, 0x01);
		snd_soc_component_write(component, 0xF0, 0x00);
	}

	return 0;
}

/* DAI operations */
static const struct snd_soc_dai_ops da7213_dai_ops = {
	.hw_params	= da7213_hw_params,
	.set_fmt	= da7213_set_dai_fmt,
	.set_sysclk	= da7213_set_dai_sysclk,
	.set_pll	= da7213_set_dai_pll,
	.digital_mute	= da7213_mute,
};

static struct snd_soc_dai_driver da7213_dai = {
	.name = "da7213-hifi",
	/* Playback Capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = DA7213_FORMATS,
	},
	/* Capture Capabilities */
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = DA7213_FORMATS,
	},
	.ops = &da7213_dai_ops,
	.symmetric_rates = 1,
};

static int da7213_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct da7213_priv *da7213 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		/* Enable MCLK for transition to ON state */
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_STANDBY) {
			if (da7213->mclk) {
				ret = clk_prepare_enable(da7213->mclk);
				if (ret) {
					dev_err(component->dev,
						"Failed to enable mclk\n");
					return ret;
				}
			}
		}
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			/* Enable VMID reference & master bias */
			snd_soc_component_update_bits(component, DA7213_REFERENCES,
					    DA7213_VMID_EN | DA7213_BIAS_EN,
					    DA7213_VMID_EN | DA7213_BIAS_EN);
		} else {
			/* Remove MCLK */
			if (da7213->mclk)
				clk_disable_unprepare(da7213->mclk);
		}
		break;
	case SND_SOC_BIAS_OFF:
		/* Disable VMID reference & master bias */
		snd_soc_component_update_bits(component, DA7213_REFERENCES,
				    DA7213_VMID_EN | DA7213_BIAS_EN, 0);
		break;
	}
	return 0;
}

#if defined(CONFIG_OF)
/* DT */
static const struct of_device_id da7213_of_match[] = {
	{ .compatible = "dlg,da7213", },
	{ }
};
MODULE_DEVICE_TABLE(of, da7213_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id da7213_acpi_match[] = {
	{ "DLGS7212", 0},
	{ "DLGS7213", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, da7213_acpi_match);
#endif

static enum da7213_micbias_voltage
	da7213_of_micbias_lvl(struct snd_soc_component *component, u32 val)
{
	switch (val) {
	case 1600:
		return DA7213_MICBIAS_1_6V;
	case 2200:
		return DA7213_MICBIAS_2_2V;
	case 2500:
		return DA7213_MICBIAS_2_5V;
	case 3000:
		return DA7213_MICBIAS_3_0V;
	default:
		dev_warn(component->dev, "Invalid micbias level\n");
		return DA7213_MICBIAS_2_2V;
	}
}

static enum da7213_dmic_data_sel
	da7213_of_dmic_data_sel(struct snd_soc_component *component, const char *str)
{
	if (!strcmp(str, "lrise_rfall")) {
		return DA7213_DMIC_DATA_LRISE_RFALL;
	} else if (!strcmp(str, "lfall_rrise")) {
		return DA7213_DMIC_DATA_LFALL_RRISE;
	} else {
		dev_warn(component->dev, "Invalid DMIC data select type\n");
		return DA7213_DMIC_DATA_LRISE_RFALL;
	}
}

static enum da7213_dmic_samplephase
	da7213_of_dmic_samplephase(struct snd_soc_component *component, const char *str)
{
	if (!strcmp(str, "on_clkedge")) {
		return DA7213_DMIC_SAMPLE_ON_CLKEDGE;
	} else if (!strcmp(str, "between_clkedge")) {
		return DA7213_DMIC_SAMPLE_BETWEEN_CLKEDGE;
	} else {
		dev_warn(component->dev, "Invalid DMIC sample phase\n");
		return DA7213_DMIC_SAMPLE_ON_CLKEDGE;
	}
}

static enum da7213_dmic_clk_rate
	da7213_of_dmic_clkrate(struct snd_soc_component *component, u32 val)
{
	switch (val) {
	case 1500000:
		return DA7213_DMIC_CLK_1_5MHZ;
	case 3000000:
		return DA7213_DMIC_CLK_3_0MHZ;
	default:
		dev_warn(component->dev, "Invalid DMIC clock rate\n");
		return DA7213_DMIC_CLK_1_5MHZ;
	}
}

static struct da7213_platform_data
	*da7213_fw_to_pdata(struct snd_soc_component *component)
{
	struct device *dev = component->dev;
	struct da7213_platform_data *pdata;
	const char *fw_str;
	u32 fw_val32;

	pdata = devm_kzalloc(component->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (device_property_read_u32(dev, "dlg,micbias1-lvl", &fw_val32) >= 0)
		pdata->micbias1_lvl = da7213_of_micbias_lvl(component, fw_val32);
	else
		pdata->micbias1_lvl = DA7213_MICBIAS_2_2V;

	if (device_property_read_u32(dev, "dlg,micbias2-lvl", &fw_val32) >= 0)
		pdata->micbias2_lvl = da7213_of_micbias_lvl(component, fw_val32);
	else
		pdata->micbias2_lvl = DA7213_MICBIAS_2_2V;

	if (!device_property_read_string(dev, "dlg,dmic-data-sel", &fw_str))
		pdata->dmic_data_sel = da7213_of_dmic_data_sel(component, fw_str);
	else
		pdata->dmic_data_sel = DA7213_DMIC_DATA_LRISE_RFALL;

	if (!device_property_read_string(dev, "dlg,dmic-samplephase", &fw_str))
		pdata->dmic_samplephase =
			da7213_of_dmic_samplephase(component, fw_str);
	else
		pdata->dmic_samplephase = DA7213_DMIC_SAMPLE_ON_CLKEDGE;

	if (device_property_read_u32(dev, "dlg,dmic-clkrate", &fw_val32) >= 0)
		pdata->dmic_clk_rate = da7213_of_dmic_clkrate(component, fw_val32);
	else
		pdata->dmic_clk_rate = DA7213_DMIC_CLK_3_0MHZ;

	return pdata;
}


static int da7213_probe(struct snd_soc_component *component)
{
	struct da7213_priv *da7213 = snd_soc_component_get_drvdata(component);

	/* Default to using ALC auto offset calibration mode. */
	snd_soc_component_update_bits(component, DA7213_ALC_CTRL1,
			    DA7213_ALC_CALIB_MODE_MAN, 0);
	da7213->alc_calib_auto = true;

	/* Default PC counter to free-running */
	snd_soc_component_update_bits(component, DA7213_PC_COUNT, DA7213_PC_FREERUN_MASK,
			    DA7213_PC_FREERUN_MASK);

	/* Enable all Gain Ramps */
	snd_soc_component_update_bits(component, DA7213_AUX_L_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_AUX_R_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_MIXIN_L_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_MIXIN_R_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_ADC_L_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_ADC_R_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_DAC_L_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_DAC_R_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_HP_L_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_HP_R_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);
	snd_soc_component_update_bits(component, DA7213_LINE_CTRL,
			    DA7213_GAIN_RAMP_EN, DA7213_GAIN_RAMP_EN);

	/*
	 * There are two separate control bits for input and output mixers as
	 * well as headphone and line outs.
	 * One to enable corresponding amplifier and other to enable its
	 * output. As amplifier bits are related to power control, they are
	 * being managed by DAPM while other (non power related) bits are
	 * enabled here
	 */
	snd_soc_component_update_bits(component, DA7213_MIXIN_L_CTRL,
			    DA7213_MIXIN_MIX_EN, DA7213_MIXIN_MIX_EN);
	snd_soc_component_update_bits(component, DA7213_MIXIN_R_CTRL,
			    DA7213_MIXIN_MIX_EN, DA7213_MIXIN_MIX_EN);

	snd_soc_component_update_bits(component, DA7213_MIXOUT_L_CTRL,
			    DA7213_MIXOUT_MIX_EN, DA7213_MIXOUT_MIX_EN);
	snd_soc_component_update_bits(component, DA7213_MIXOUT_R_CTRL,
			    DA7213_MIXOUT_MIX_EN, DA7213_MIXOUT_MIX_EN);

	snd_soc_component_update_bits(component, DA7213_HP_L_CTRL,
			    DA7213_HP_AMP_OE, DA7213_HP_AMP_OE);
	snd_soc_component_update_bits(component, DA7213_HP_R_CTRL,
			    DA7213_HP_AMP_OE, DA7213_HP_AMP_OE);

	snd_soc_component_update_bits(component, DA7213_LINE_CTRL,
			    DA7213_LINE_AMP_OE, DA7213_LINE_AMP_OE);

	/* Handle DT/Platform data */
	da7213->pdata = dev_get_platdata(component->dev);
	if (!da7213->pdata)
		da7213->pdata = da7213_fw_to_pdata(component);

	/* Set platform data values */
	if (da7213->pdata) {
		struct da7213_platform_data *pdata = da7213->pdata;
		u8 micbias_lvl = 0, dmic_cfg = 0;

		/* Set Mic Bias voltages */
		switch (pdata->micbias1_lvl) {
		case DA7213_MICBIAS_1_6V:
		case DA7213_MICBIAS_2_2V:
		case DA7213_MICBIAS_2_5V:
		case DA7213_MICBIAS_3_0V:
			micbias_lvl |= (pdata->micbias1_lvl <<
					DA7213_MICBIAS1_LEVEL_SHIFT);
			break;
		}
		switch (pdata->micbias2_lvl) {
		case DA7213_MICBIAS_1_6V:
		case DA7213_MICBIAS_2_2V:
		case DA7213_MICBIAS_2_5V:
		case DA7213_MICBIAS_3_0V:
			micbias_lvl |= (pdata->micbias2_lvl <<
					 DA7213_MICBIAS2_LEVEL_SHIFT);
			break;
		}
		snd_soc_component_update_bits(component, DA7213_MICBIAS_CTRL,
				    DA7213_MICBIAS1_LEVEL_MASK |
				    DA7213_MICBIAS2_LEVEL_MASK, micbias_lvl);

		/* Set DMIC configuration */
		switch (pdata->dmic_data_sel) {
		case DA7213_DMIC_DATA_LFALL_RRISE:
		case DA7213_DMIC_DATA_LRISE_RFALL:
			dmic_cfg |= (pdata->dmic_data_sel <<
				     DA7213_DMIC_DATA_SEL_SHIFT);
			break;
		}
		switch (pdata->dmic_samplephase) {
		case DA7213_DMIC_SAMPLE_ON_CLKEDGE:
		case DA7213_DMIC_SAMPLE_BETWEEN_CLKEDGE:
			dmic_cfg |= (pdata->dmic_samplephase <<
				     DA7213_DMIC_SAMPLEPHASE_SHIFT);
			break;
		}
		switch (pdata->dmic_clk_rate) {
		case DA7213_DMIC_CLK_3_0MHZ:
		case DA7213_DMIC_CLK_1_5MHZ:
			dmic_cfg |= (pdata->dmic_clk_rate <<
				     DA7213_DMIC_CLK_RATE_SHIFT);
			break;
		}
		snd_soc_component_update_bits(component, DA7213_MIC_CONFIG,
				    DA7213_DMIC_DATA_SEL_MASK |
				    DA7213_DMIC_SAMPLEPHASE_MASK |
				    DA7213_DMIC_CLK_RATE_MASK, dmic_cfg);
	}

	/* Check if MCLK provided */
	da7213->mclk = devm_clk_get(component->dev, "mclk");
	if (IS_ERR(da7213->mclk)) {
		if (PTR_ERR(da7213->mclk) != -ENOENT)
			return PTR_ERR(da7213->mclk);
		else
			da7213->mclk = NULL;
	}

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_da7213 = {
	.probe			= da7213_probe,
	.set_bias_level		= da7213_set_bias_level,
	.controls		= da7213_snd_controls,
	.num_controls		= ARRAY_SIZE(da7213_snd_controls),
	.dapm_widgets		= da7213_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(da7213_dapm_widgets),
	.dapm_routes		= da7213_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(da7213_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config da7213_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.reg_defaults = da7213_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(da7213_reg_defaults),
	.volatile_reg = da7213_volatile_register,
	.cache_type = REGCACHE_RBTREE,
};

static int da7213_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct da7213_priv *da7213;
	int ret;

	da7213 = devm_kzalloc(&i2c->dev, sizeof(*da7213), GFP_KERNEL);
	if (!da7213)
		return -ENOMEM;

	i2c_set_clientdata(i2c, da7213);

	da7213->regmap = devm_regmap_init_i2c(i2c, &da7213_regmap_config);
	if (IS_ERR(da7213->regmap)) {
		ret = PTR_ERR(da7213->regmap);
		dev_err(&i2c->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_da7213, &da7213_dai, 1);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to register da7213 component: %d\n",
			ret);
	}
	return ret;
}

static const struct i2c_device_id da7213_i2c_id[] = {
	{ "da7213", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, da7213_i2c_id);

/* I2C codec control layer */
static struct i2c_driver da7213_i2c_driver = {
	.driver = {
		.name = "da7213",
		.of_match_table = of_match_ptr(da7213_of_match),
		.acpi_match_table = ACPI_PTR(da7213_acpi_match),
	},
	.probe		= da7213_i2c_probe,
	.id_table	= da7213_i2c_id,
};

module_i2c_driver(da7213_i2c_driver);

MODULE_DESCRIPTION("ASoC DA7213 Codec driver");
MODULE_AUTHOR("Adam Thomson <Adam.Thomson.Opensource@diasemi.com>");
MODULE_LICENSE("GPL");
