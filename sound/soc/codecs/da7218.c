/*
 * da7218.c - DA7218 ALSA SoC Codec Driver
 *
 * Copyright (c) 2015 Dialog Semiconductor
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <asm/div64.h>

#include <sound/da7218.h>
#include "da7218.h"


/*
 * TLVs and Enums
 */

/* Input TLVs */
static const DECLARE_TLV_DB_SCALE(da7218_mic_gain_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(da7218_mixin_gain_tlv, -450, 150, 0);
static const DECLARE_TLV_DB_SCALE(da7218_in_dig_gain_tlv, -8325, 75, 0);
static const DECLARE_TLV_DB_SCALE(da7218_ags_trigger_tlv, -9000, 600, 0);
static const DECLARE_TLV_DB_SCALE(da7218_ags_att_max_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(da7218_alc_threshold_tlv, -9450, 150, 0);
static const DECLARE_TLV_DB_SCALE(da7218_alc_gain_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(da7218_alc_ana_gain_tlv, 0, 600, 0);

/* Input/Output TLVs */
static const DECLARE_TLV_DB_SCALE(da7218_dmix_gain_tlv, -4200, 150, 0);

/* Output TLVs */
static const DECLARE_TLV_DB_SCALE(da7218_dgs_trigger_tlv, -9450, 150, 0);
static const DECLARE_TLV_DB_SCALE(da7218_dgs_anticlip_tlv, -4200, 600, 0);
static const DECLARE_TLV_DB_SCALE(da7218_dgs_signal_tlv, -9000, 600, 0);
static const DECLARE_TLV_DB_SCALE(da7218_out_eq_band_tlv, -1050, 150, 0);
static const DECLARE_TLV_DB_SCALE(da7218_out_dig_gain_tlv, -8325, 75, 0);
static const DECLARE_TLV_DB_SCALE(da7218_dac_ng_threshold_tlv, -10200, 600, 0);
static const DECLARE_TLV_DB_SCALE(da7218_mixout_gain_tlv, -100, 50, 0);
static const DECLARE_TLV_DB_SCALE(da7218_hp_gain_tlv, -5700, 150, 0);

/* Input Enums */
static const char * const da7218_alc_attack_rate_txt[] = {
	"7.33/fs", "14.66/fs", "29.32/fs", "58.64/fs", "117.3/fs", "234.6/fs",
	"469.1/fs", "938.2/fs", "1876/fs", "3753/fs", "7506/fs", "15012/fs",
	"30024/fs",
};

static const struct soc_enum da7218_alc_attack_rate =
	SOC_ENUM_SINGLE(DA7218_ALC_CTRL2, DA7218_ALC_ATTACK_SHIFT,
			DA7218_ALC_ATTACK_MAX, da7218_alc_attack_rate_txt);

static const char * const da7218_alc_release_rate_txt[] = {
	"28.66/fs", "57.33/fs", "114.6/fs", "229.3/fs", "458.6/fs", "917.1/fs",
	"1834/fs", "3668/fs", "7337/fs", "14674/fs", "29348/fs",
};

static const struct soc_enum da7218_alc_release_rate =
	SOC_ENUM_SINGLE(DA7218_ALC_CTRL2, DA7218_ALC_RELEASE_SHIFT,
			DA7218_ALC_RELEASE_MAX, da7218_alc_release_rate_txt);

static const char * const da7218_alc_hold_time_txt[] = {
	"62/fs", "124/fs", "248/fs", "496/fs", "992/fs", "1984/fs", "3968/fs",
	"7936/fs", "15872/fs", "31744/fs", "63488/fs", "126976/fs",
	"253952/fs", "507904/fs", "1015808/fs", "2031616/fs"
};

static const struct soc_enum da7218_alc_hold_time =
	SOC_ENUM_SINGLE(DA7218_ALC_CTRL3, DA7218_ALC_HOLD_SHIFT,
			DA7218_ALC_HOLD_MAX, da7218_alc_hold_time_txt);

static const char * const da7218_alc_anticlip_step_txt[] = {
	"0.034dB/fs", "0.068dB/fs", "0.136dB/fs", "0.272dB/fs",
};

static const struct soc_enum da7218_alc_anticlip_step =
	SOC_ENUM_SINGLE(DA7218_ALC_ANTICLIP_CTRL,
			DA7218_ALC_ANTICLIP_STEP_SHIFT,
			DA7218_ALC_ANTICLIP_STEP_MAX,
			da7218_alc_anticlip_step_txt);

static const char * const da7218_integ_rate_txt[] = {
	"1/4", "1/16", "1/256", "1/65536"
};

static const struct soc_enum da7218_integ_attack_rate =
	SOC_ENUM_SINGLE(DA7218_ENV_TRACK_CTRL, DA7218_INTEG_ATTACK_SHIFT,
			DA7218_INTEG_MAX, da7218_integ_rate_txt);

static const struct soc_enum da7218_integ_release_rate =
	SOC_ENUM_SINGLE(DA7218_ENV_TRACK_CTRL, DA7218_INTEG_RELEASE_SHIFT,
			DA7218_INTEG_MAX, da7218_integ_rate_txt);

/* Input/Output Enums */
static const char * const da7218_gain_ramp_rate_txt[] = {
	"Nominal Rate * 8", "Nominal Rate", "Nominal Rate / 8",
	"Nominal Rate / 16",
};

static const struct soc_enum da7218_gain_ramp_rate =
	SOC_ENUM_SINGLE(DA7218_GAIN_RAMP_CTRL, DA7218_GAIN_RAMP_RATE_SHIFT,
			DA7218_GAIN_RAMP_RATE_MAX, da7218_gain_ramp_rate_txt);

static const char * const da7218_hpf_mode_txt[] = {
	"Disabled", "Audio", "Voice",
};

static const unsigned int da7218_hpf_mode_val[] = {
	DA7218_HPF_DISABLED, DA7218_HPF_AUDIO_EN, DA7218_HPF_VOICE_EN,
};

static const struct soc_enum da7218_in1_hpf_mode =
	SOC_VALUE_ENUM_SINGLE(DA7218_IN_1_HPF_FILTER_CTRL,
			      DA7218_HPF_MODE_SHIFT, DA7218_HPF_MODE_MASK,
			      DA7218_HPF_MODE_MAX, da7218_hpf_mode_txt,
			      da7218_hpf_mode_val);

static const struct soc_enum da7218_in2_hpf_mode =
	SOC_VALUE_ENUM_SINGLE(DA7218_IN_2_HPF_FILTER_CTRL,
			      DA7218_HPF_MODE_SHIFT, DA7218_HPF_MODE_MASK,
			      DA7218_HPF_MODE_MAX, da7218_hpf_mode_txt,
			      da7218_hpf_mode_val);

static const struct soc_enum da7218_out1_hpf_mode =
	SOC_VALUE_ENUM_SINGLE(DA7218_OUT_1_HPF_FILTER_CTRL,
			      DA7218_HPF_MODE_SHIFT, DA7218_HPF_MODE_MASK,
			      DA7218_HPF_MODE_MAX, da7218_hpf_mode_txt,
			      da7218_hpf_mode_val);

static const char * const da7218_audio_hpf_corner_txt[] = {
	"2Hz", "4Hz", "8Hz", "16Hz",
};

static const struct soc_enum da7218_in1_audio_hpf_corner =
	SOC_ENUM_SINGLE(DA7218_IN_1_HPF_FILTER_CTRL,
			DA7218_IN_1_AUDIO_HPF_CORNER_SHIFT,
			DA7218_AUDIO_HPF_CORNER_MAX,
			da7218_audio_hpf_corner_txt);

static const struct soc_enum da7218_in2_audio_hpf_corner =
	SOC_ENUM_SINGLE(DA7218_IN_2_HPF_FILTER_CTRL,
			DA7218_IN_2_AUDIO_HPF_CORNER_SHIFT,
			DA7218_AUDIO_HPF_CORNER_MAX,
			da7218_audio_hpf_corner_txt);

static const struct soc_enum da7218_out1_audio_hpf_corner =
	SOC_ENUM_SINGLE(DA7218_OUT_1_HPF_FILTER_CTRL,
			DA7218_OUT_1_AUDIO_HPF_CORNER_SHIFT,
			DA7218_AUDIO_HPF_CORNER_MAX,
			da7218_audio_hpf_corner_txt);

static const char * const da7218_voice_hpf_corner_txt[] = {
	"2.5Hz", "25Hz", "50Hz", "100Hz", "150Hz", "200Hz", "300Hz", "400Hz",
};

static const struct soc_enum da7218_in1_voice_hpf_corner =
	SOC_ENUM_SINGLE(DA7218_IN_1_HPF_FILTER_CTRL,
			DA7218_IN_1_VOICE_HPF_CORNER_SHIFT,
			DA7218_VOICE_HPF_CORNER_MAX,
			da7218_voice_hpf_corner_txt);

static const struct soc_enum da7218_in2_voice_hpf_corner =
	SOC_ENUM_SINGLE(DA7218_IN_2_HPF_FILTER_CTRL,
			DA7218_IN_2_VOICE_HPF_CORNER_SHIFT,
			DA7218_VOICE_HPF_CORNER_MAX,
			da7218_voice_hpf_corner_txt);

static const struct soc_enum da7218_out1_voice_hpf_corner =
	SOC_ENUM_SINGLE(DA7218_OUT_1_HPF_FILTER_CTRL,
			DA7218_OUT_1_VOICE_HPF_CORNER_SHIFT,
			DA7218_VOICE_HPF_CORNER_MAX,
			da7218_voice_hpf_corner_txt);

static const char * const da7218_tonegen_dtmf_key_txt[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D",
	"*", "#"
};

static const struct soc_enum da7218_tonegen_dtmf_key =
	SOC_ENUM_SINGLE(DA7218_TONE_GEN_CFG1, DA7218_DTMF_REG_SHIFT,
			DA7218_DTMF_REG_MAX, da7218_tonegen_dtmf_key_txt);

static const char * const da7218_tonegen_swg_sel_txt[] = {
	"Sum", "SWG1", "SWG2", "SWG1_1-Cos"
};

static const struct soc_enum da7218_tonegen_swg_sel =
	SOC_ENUM_SINGLE(DA7218_TONE_GEN_CFG2, DA7218_SWG_SEL_SHIFT,
			DA7218_SWG_SEL_MAX, da7218_tonegen_swg_sel_txt);

/* Output Enums */
static const char * const da7218_dgs_rise_coeff_txt[] = {
	"1/1", "1/16", "1/64", "1/256", "1/1024", "1/4096", "1/16384",
};

static const struct soc_enum da7218_dgs_rise_coeff =
	SOC_ENUM_SINGLE(DA7218_DGS_RISE_FALL, DA7218_DGS_RISE_COEFF_SHIFT,
			DA7218_DGS_RISE_COEFF_MAX, da7218_dgs_rise_coeff_txt);

static const char * const da7218_dgs_fall_coeff_txt[] = {
	"1/4", "1/16", "1/64", "1/256", "1/1024", "1/4096", "1/16384", "1/65536",
};

static const struct soc_enum da7218_dgs_fall_coeff =
	SOC_ENUM_SINGLE(DA7218_DGS_RISE_FALL, DA7218_DGS_FALL_COEFF_SHIFT,
			DA7218_DGS_FALL_COEFF_MAX, da7218_dgs_fall_coeff_txt);

static const char * const da7218_dac_ng_setup_time_txt[] = {
	"256 Samples", "512 Samples", "1024 Samples", "2048 Samples"
};

static const struct soc_enum da7218_dac_ng_setup_time =
	SOC_ENUM_SINGLE(DA7218_DAC_NG_SETUP_TIME,
			DA7218_DAC_NG_SETUP_TIME_SHIFT,
			DA7218_DAC_NG_SETUP_TIME_MAX,
			da7218_dac_ng_setup_time_txt);

static const char * const da7218_dac_ng_rampup_txt[] = {
	"0.22ms/dB", "0.0138ms/dB"
};

static const struct soc_enum da7218_dac_ng_rampup_rate =
	SOC_ENUM_SINGLE(DA7218_DAC_NG_SETUP_TIME,
			DA7218_DAC_NG_RAMPUP_RATE_SHIFT,
			DA7218_DAC_NG_RAMPUP_RATE_MAX,
			da7218_dac_ng_rampup_txt);

static const char * const da7218_dac_ng_rampdown_txt[] = {
	"0.88ms/dB", "14.08ms/dB"
};

static const struct soc_enum da7218_dac_ng_rampdown_rate =
	SOC_ENUM_SINGLE(DA7218_DAC_NG_SETUP_TIME,
			DA7218_DAC_NG_RAMPDN_RATE_SHIFT,
			DA7218_DAC_NG_RAMPDN_RATE_MAX,
			da7218_dac_ng_rampdown_txt);

static const char * const da7218_cp_mchange_txt[] = {
	"Largest Volume", "DAC Volume", "Signal Magnitude"
};

static const unsigned int da7218_cp_mchange_val[] = {
	DA7218_CP_MCHANGE_LARGEST_VOL, DA7218_CP_MCHANGE_DAC_VOL,
	DA7218_CP_MCHANGE_SIG_MAG
};

static const struct soc_enum da7218_cp_mchange =
	SOC_VALUE_ENUM_SINGLE(DA7218_CP_CTRL, DA7218_CP_MCHANGE_SHIFT,
			      DA7218_CP_MCHANGE_REL_MASK, DA7218_CP_MCHANGE_MAX,
			      da7218_cp_mchange_txt, da7218_cp_mchange_val);

static const char * const da7218_cp_fcontrol_txt[] = {
	"1MHz", "500KHz", "250KHz", "125KHz", "63KHz", "0KHz"
};

static const struct soc_enum da7218_cp_fcontrol =
	SOC_ENUM_SINGLE(DA7218_CP_DELAY, DA7218_CP_FCONTROL_SHIFT,
			DA7218_CP_FCONTROL_MAX, da7218_cp_fcontrol_txt);

static const char * const da7218_cp_tau_delay_txt[] = {
	"0ms", "2ms", "4ms", "16ms", "64ms", "128ms", "256ms", "512ms"
};

static const struct soc_enum da7218_cp_tau_delay =
	SOC_ENUM_SINGLE(DA7218_CP_DELAY, DA7218_CP_TAU_DELAY_SHIFT,
			DA7218_CP_TAU_DELAY_MAX, da7218_cp_tau_delay_txt);

/*
 * Control Functions
 */

/* ALC */
static void da7218_alc_calib(struct snd_soc_codec *codec)
{
	u8 mic_1_ctrl, mic_2_ctrl;
	u8 mixin_1_ctrl, mixin_2_ctrl;
	u8 in_1l_filt_ctrl, in_1r_filt_ctrl, in_2l_filt_ctrl, in_2r_filt_ctrl;
	u8 in_1_hpf_ctrl, in_2_hpf_ctrl;
	u8 calib_ctrl;
	int i = 0;
	bool calibrated = false;

	/* Save current state of MIC control registers */
	mic_1_ctrl = snd_soc_read(codec, DA7218_MIC_1_CTRL);
	mic_2_ctrl = snd_soc_read(codec, DA7218_MIC_2_CTRL);

	/* Save current state of input mixer control registers */
	mixin_1_ctrl = snd_soc_read(codec, DA7218_MIXIN_1_CTRL);
	mixin_2_ctrl = snd_soc_read(codec, DA7218_MIXIN_2_CTRL);

	/* Save current state of input filter control registers */
	in_1l_filt_ctrl = snd_soc_read(codec, DA7218_IN_1L_FILTER_CTRL);
	in_1r_filt_ctrl = snd_soc_read(codec, DA7218_IN_1R_FILTER_CTRL);
	in_2l_filt_ctrl = snd_soc_read(codec, DA7218_IN_2L_FILTER_CTRL);
	in_2r_filt_ctrl = snd_soc_read(codec, DA7218_IN_2R_FILTER_CTRL);

	/* Save current state of input HPF control registers */
	in_1_hpf_ctrl = snd_soc_read(codec, DA7218_IN_1_HPF_FILTER_CTRL);
	in_2_hpf_ctrl = snd_soc_read(codec, DA7218_IN_2_HPF_FILTER_CTRL);

	/* Enable then Mute MIC PGAs */
	snd_soc_update_bits(codec, DA7218_MIC_1_CTRL, DA7218_MIC_1_AMP_EN_MASK,
			    DA7218_MIC_1_AMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_MIC_2_CTRL, DA7218_MIC_2_AMP_EN_MASK,
			    DA7218_MIC_2_AMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_MIC_1_CTRL,
			    DA7218_MIC_1_AMP_MUTE_EN_MASK,
			    DA7218_MIC_1_AMP_MUTE_EN_MASK);
	snd_soc_update_bits(codec, DA7218_MIC_2_CTRL,
			    DA7218_MIC_2_AMP_MUTE_EN_MASK,
			    DA7218_MIC_2_AMP_MUTE_EN_MASK);

	/* Enable input mixers unmuted */
	snd_soc_update_bits(codec, DA7218_MIXIN_1_CTRL,
			    DA7218_MIXIN_1_AMP_EN_MASK |
			    DA7218_MIXIN_1_AMP_MUTE_EN_MASK,
			    DA7218_MIXIN_1_AMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_MIXIN_2_CTRL,
			    DA7218_MIXIN_2_AMP_EN_MASK |
			    DA7218_MIXIN_2_AMP_MUTE_EN_MASK,
			    DA7218_MIXIN_2_AMP_EN_MASK);

	/* Enable input filters unmuted */
	snd_soc_update_bits(codec, DA7218_IN_1L_FILTER_CTRL,
			    DA7218_IN_1L_FILTER_EN_MASK |
			    DA7218_IN_1L_MUTE_EN_MASK,
			    DA7218_IN_1L_FILTER_EN_MASK);
	snd_soc_update_bits(codec, DA7218_IN_1R_FILTER_CTRL,
			    DA7218_IN_1R_FILTER_EN_MASK |
			    DA7218_IN_1R_MUTE_EN_MASK,
			    DA7218_IN_1R_FILTER_EN_MASK);
	snd_soc_update_bits(codec, DA7218_IN_2L_FILTER_CTRL,
			    DA7218_IN_2L_FILTER_EN_MASK |
			    DA7218_IN_2L_MUTE_EN_MASK,
			    DA7218_IN_2L_FILTER_EN_MASK);
	snd_soc_update_bits(codec, DA7218_IN_2R_FILTER_CTRL,
			    DA7218_IN_2R_FILTER_EN_MASK |
			    DA7218_IN_2R_MUTE_EN_MASK,
			    DA7218_IN_2R_FILTER_EN_MASK);

	/*
	 * Make sure input HPFs voice mode is disabled, otherwise for sampling
	 * rates above 32KHz the ADC signals will be stopped and will cause
	 * calibration to lock up.
	 */
	snd_soc_update_bits(codec, DA7218_IN_1_HPF_FILTER_CTRL,
			    DA7218_IN_1_VOICE_EN_MASK, 0);
	snd_soc_update_bits(codec, DA7218_IN_2_HPF_FILTER_CTRL,
			    DA7218_IN_2_VOICE_EN_MASK, 0);

	/* Perform auto calibration */
	snd_soc_update_bits(codec, DA7218_CALIB_CTRL, DA7218_CALIB_AUTO_EN_MASK,
			    DA7218_CALIB_AUTO_EN_MASK);
	do {
		calib_ctrl = snd_soc_read(codec, DA7218_CALIB_CTRL);
		if (calib_ctrl & DA7218_CALIB_AUTO_EN_MASK) {
			++i;
			usleep_range(DA7218_ALC_CALIB_DELAY_MIN,
				     DA7218_ALC_CALIB_DELAY_MAX);
		} else {
			calibrated = true;
		}

	} while ((i < DA7218_ALC_CALIB_MAX_TRIES) && (!calibrated));

	/* If auto calibration fails, disable DC offset, hybrid ALC */
	if ((!calibrated) || (calib_ctrl & DA7218_CALIB_OVERFLOW_MASK)) {
		dev_warn(codec->dev,
			 "ALC auto calibration failed - %s\n",
			 (calibrated) ? "overflow" : "timeout");
		snd_soc_update_bits(codec, DA7218_CALIB_CTRL,
				    DA7218_CALIB_OFFSET_EN_MASK, 0);
		snd_soc_update_bits(codec, DA7218_ALC_CTRL1,
				    DA7218_ALC_SYNC_MODE_MASK, 0);

	} else {
		/* Enable DC offset cancellation */
		snd_soc_update_bits(codec, DA7218_CALIB_CTRL,
				    DA7218_CALIB_OFFSET_EN_MASK,
				    DA7218_CALIB_OFFSET_EN_MASK);

		/* Enable ALC hybrid mode */
		snd_soc_update_bits(codec, DA7218_ALC_CTRL1,
				    DA7218_ALC_SYNC_MODE_MASK,
				    DA7218_ALC_SYNC_MODE_CH1 |
				    DA7218_ALC_SYNC_MODE_CH2);
	}

	/* Restore input HPF control registers to original states */
	snd_soc_write(codec, DA7218_IN_1_HPF_FILTER_CTRL, in_1_hpf_ctrl);
	snd_soc_write(codec, DA7218_IN_2_HPF_FILTER_CTRL, in_2_hpf_ctrl);

	/* Restore input filter control registers to original states */
	snd_soc_write(codec, DA7218_IN_1L_FILTER_CTRL, in_1l_filt_ctrl);
	snd_soc_write(codec, DA7218_IN_1R_FILTER_CTRL, in_1r_filt_ctrl);
	snd_soc_write(codec, DA7218_IN_2L_FILTER_CTRL, in_2l_filt_ctrl);
	snd_soc_write(codec, DA7218_IN_2R_FILTER_CTRL, in_2r_filt_ctrl);

	/* Restore input mixer control registers to original state */
	snd_soc_write(codec, DA7218_MIXIN_1_CTRL, mixin_1_ctrl);
	snd_soc_write(codec, DA7218_MIXIN_2_CTRL, mixin_2_ctrl);

	/* Restore MIC control registers to original states */
	snd_soc_write(codec, DA7218_MIC_1_CTRL, mic_1_ctrl);
	snd_soc_write(codec, DA7218_MIC_2_CTRL, mic_2_ctrl);
}

static int da7218_mixin_gain_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);

	/*
	 * If ALC in operation and value of control has been updated,
	 * make sure calibrated offsets are updated.
	 */
	if ((ret == 1) && (da7218->alc_en))
		da7218_alc_calib(codec);

	return ret;
}

static int da7218_alc_sw_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *) kcontrol->private_value;
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	unsigned int lvalue = ucontrol->value.integer.value[0];
	unsigned int rvalue = ucontrol->value.integer.value[1];
	unsigned int lshift = mc->shift;
	unsigned int rshift = mc->rshift;
	unsigned int mask = (mc->max << lshift) | (mc->max << rshift);

	/* Force ALC offset calibration if enabling ALC */
	if ((lvalue || rvalue) && (!da7218->alc_en))
		da7218_alc_calib(codec);

	/* Update bits to detail which channels are enabled/disabled */
	da7218->alc_en &= ~mask;
	da7218->alc_en |= (lvalue << lshift) | (rvalue << rshift);

	return snd_soc_put_volsw(kcontrol, ucontrol);
}

/* ToneGen */
static int da7218_tonegen_freq_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	struct soc_mixer_control *mixer_ctrl =
		(struct soc_mixer_control *) kcontrol->private_value;
	unsigned int reg = mixer_ctrl->reg;
	u16 val;
	int ret;

	/*
	 * Frequency value spans two 8-bit registers, lower then upper byte.
	 * Therefore we need to convert to host endianness here.
	 */
	ret = regmap_raw_read(da7218->regmap, reg, &val, 2);
	if (ret)
		return ret;

	ucontrol->value.integer.value[0] = le16_to_cpu(val);

	return 0;
}

static int da7218_tonegen_freq_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	struct soc_mixer_control *mixer_ctrl =
		(struct soc_mixer_control *) kcontrol->private_value;
	unsigned int reg = mixer_ctrl->reg;
	u16 val;

	/*
	 * Frequency value spans two 8-bit registers, lower then upper byte.
	 * Therefore we need to convert to little endian here to align with
	 * HW registers.
	 */
	val = cpu_to_le16(ucontrol->value.integer.value[0]);

	return regmap_raw_write(da7218->regmap, reg, &val, 2);
}

static int da7218_mic_lvl_det_sw_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	struct soc_mixer_control *mixer_ctrl =
		(struct soc_mixer_control *) kcontrol->private_value;
	unsigned int lvalue = ucontrol->value.integer.value[0];
	unsigned int rvalue = ucontrol->value.integer.value[1];
	unsigned int lshift = mixer_ctrl->shift;
	unsigned int rshift = mixer_ctrl->rshift;
	unsigned int mask = (mixer_ctrl->max << lshift) |
			    (mixer_ctrl->max << rshift);
	da7218->mic_lvl_det_en &= ~mask;
	da7218->mic_lvl_det_en |= (lvalue << lshift) | (rvalue << rshift);

	/*
	 * Here we only enable the feature on paths which are already
	 * powered. If a channel is enabled here for level detect, but that path
	 * isn't powered, then the channel will actually be enabled when we do
	 * power the path (IN_FILTER widget events). This handling avoids
	 * unwanted level detect events.
	 */
	return snd_soc_write(codec, mixer_ctrl->reg,
			     (da7218->in_filt_en & da7218->mic_lvl_det_en));
}

static int da7218_mic_lvl_det_sw_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	struct soc_mixer_control *mixer_ctrl =
		(struct soc_mixer_control *) kcontrol->private_value;
	unsigned int lshift = mixer_ctrl->shift;
	unsigned int rshift = mixer_ctrl->rshift;
	unsigned int lmask = (mixer_ctrl->max << lshift);
	unsigned int rmask = (mixer_ctrl->max << rshift);

	ucontrol->value.integer.value[0] =
		(da7218->mic_lvl_det_en & lmask) >> lshift;
	ucontrol->value.integer.value[1] =
		(da7218->mic_lvl_det_en & rmask) >> rshift;

	return 0;
}

static int da7218_biquad_coeff_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *) kcontrol->private_value;

	/* Determine which BiQuads we're setting based on size of config data */
	switch (bytes_ext->max) {
	case DA7218_OUT_1_BIQ_5STAGE_CFG_SIZE:
		memcpy(ucontrol->value.bytes.data, da7218->biq_5stage_coeff,
		       bytes_ext->max);
		break;
	case DA7218_SIDETONE_BIQ_3STAGE_CFG_SIZE:
		memcpy(ucontrol->value.bytes.data, da7218->stbiq_3stage_coeff,
		       bytes_ext->max);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int da7218_biquad_coeff_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *) kcontrol->private_value;
	u8 reg, out_filt1l;
	u8 cfg[DA7218_BIQ_CFG_SIZE];
	int i;

	/*
	 * Determine which BiQuads we're setting based on size of config data,
	 * and stored the data for use by get function.
	 */
	switch (bytes_ext->max) {
	case DA7218_OUT_1_BIQ_5STAGE_CFG_SIZE:
		reg = DA7218_OUT_1_BIQ_5STAGE_DATA;
		memcpy(da7218->biq_5stage_coeff, ucontrol->value.bytes.data,
		       bytes_ext->max);
		break;
	case DA7218_SIDETONE_BIQ_3STAGE_CFG_SIZE:
		reg = DA7218_SIDETONE_BIQ_3STAGE_DATA;
		memcpy(da7218->stbiq_3stage_coeff, ucontrol->value.bytes.data,
		       bytes_ext->max);
		break;
	default:
		return -EINVAL;
	}

	/* Make sure at least out filter1 enabled to allow programming */
	out_filt1l = snd_soc_read(codec, DA7218_OUT_1L_FILTER_CTRL);
	snd_soc_write(codec, DA7218_OUT_1L_FILTER_CTRL,
		      out_filt1l | DA7218_OUT_1L_FILTER_EN_MASK);

	for (i = 0; i < bytes_ext->max; ++i) {
		cfg[DA7218_BIQ_CFG_DATA] = ucontrol->value.bytes.data[i];
		cfg[DA7218_BIQ_CFG_ADDR] = i;
		regmap_raw_write(da7218->regmap, reg, cfg, DA7218_BIQ_CFG_SIZE);
	}

	/* Restore filter to previous setting */
	snd_soc_write(codec, DA7218_OUT_1L_FILTER_CTRL, out_filt1l);

	return 0;
}


/*
 * KControls
 */

static const struct snd_kcontrol_new da7218_snd_controls[] = {
	/* Mics */
	SOC_SINGLE_TLV("Mic1 Volume", DA7218_MIC_1_GAIN,
		       DA7218_MIC_1_AMP_GAIN_SHIFT, DA7218_MIC_AMP_GAIN_MAX,
		       DA7218_NO_INVERT, da7218_mic_gain_tlv),
	SOC_SINGLE("Mic1 Switch", DA7218_MIC_1_CTRL,
		   DA7218_MIC_1_AMP_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),
	SOC_SINGLE_TLV("Mic2 Volume", DA7218_MIC_2_GAIN,
		       DA7218_MIC_2_AMP_GAIN_SHIFT, DA7218_MIC_AMP_GAIN_MAX,
		       DA7218_NO_INVERT, da7218_mic_gain_tlv),
	SOC_SINGLE("Mic2 Switch", DA7218_MIC_2_CTRL,
		   DA7218_MIC_2_AMP_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),

	/* Mixer Input */
	SOC_SINGLE_EXT_TLV("Mixin1 Volume", DA7218_MIXIN_1_GAIN,
			   DA7218_MIXIN_1_AMP_GAIN_SHIFT,
			   DA7218_MIXIN_AMP_GAIN_MAX, DA7218_NO_INVERT,
			   snd_soc_get_volsw, da7218_mixin_gain_put,
			   da7218_mixin_gain_tlv),
	SOC_SINGLE("Mixin1 Switch", DA7218_MIXIN_1_CTRL,
		   DA7218_MIXIN_1_AMP_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),
	SOC_SINGLE("Mixin1 Gain Ramp Switch", DA7218_MIXIN_1_CTRL,
		   DA7218_MIXIN_1_AMP_RAMP_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("Mixin1 ZC Gain Switch", DA7218_MIXIN_1_CTRL,
		   DA7218_MIXIN_1_AMP_ZC_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE_EXT_TLV("Mixin2 Volume", DA7218_MIXIN_2_GAIN,
			   DA7218_MIXIN_2_AMP_GAIN_SHIFT,
			   DA7218_MIXIN_AMP_GAIN_MAX, DA7218_NO_INVERT,
			   snd_soc_get_volsw, da7218_mixin_gain_put,
			   da7218_mixin_gain_tlv),
	SOC_SINGLE("Mixin2 Switch", DA7218_MIXIN_2_CTRL,
		   DA7218_MIXIN_2_AMP_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),
	SOC_SINGLE("Mixin2 Gain Ramp Switch", DA7218_MIXIN_2_CTRL,
		   DA7218_MIXIN_2_AMP_RAMP_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("Mixin2 ZC Gain Switch", DA7218_MIXIN_2_CTRL,
		   DA7218_MIXIN_2_AMP_ZC_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),

	/* ADCs */
	SOC_SINGLE("ADC1 AAF Switch", DA7218_ADC_1_CTRL,
		   DA7218_ADC_1_AAF_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("ADC2 AAF Switch", DA7218_ADC_2_CTRL,
		   DA7218_ADC_2_AAF_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("ADC LP Mode Switch", DA7218_ADC_MODE,
		   DA7218_ADC_LP_MODE_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),

	/* Input Filters */
	SOC_SINGLE_TLV("In Filter1L Volume", DA7218_IN_1L_GAIN,
		       DA7218_IN_1L_DIGITAL_GAIN_SHIFT,
		       DA7218_IN_DIGITAL_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_in_dig_gain_tlv),
	SOC_SINGLE("In Filter1L Switch", DA7218_IN_1L_FILTER_CTRL,
		   DA7218_IN_1L_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),
	SOC_SINGLE("In Filter1L Gain Ramp Switch", DA7218_IN_1L_FILTER_CTRL,
		   DA7218_IN_1L_RAMP_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE_TLV("In Filter1R Volume", DA7218_IN_1R_GAIN,
		       DA7218_IN_1R_DIGITAL_GAIN_SHIFT,
		       DA7218_IN_DIGITAL_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_in_dig_gain_tlv),
	SOC_SINGLE("In Filter1R Switch", DA7218_IN_1R_FILTER_CTRL,
		   DA7218_IN_1R_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),
	SOC_SINGLE("In Filter1R Gain Ramp Switch",
		   DA7218_IN_1R_FILTER_CTRL, DA7218_IN_1R_RAMP_EN_SHIFT,
		   DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),
	SOC_SINGLE_TLV("In Filter2L Volume", DA7218_IN_2L_GAIN,
		       DA7218_IN_2L_DIGITAL_GAIN_SHIFT,
		       DA7218_IN_DIGITAL_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_in_dig_gain_tlv),
	SOC_SINGLE("In Filter2L Switch", DA7218_IN_2L_FILTER_CTRL,
		   DA7218_IN_2L_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),
	SOC_SINGLE("In Filter2L Gain Ramp Switch", DA7218_IN_2L_FILTER_CTRL,
		   DA7218_IN_2L_RAMP_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE_TLV("In Filter2R Volume", DA7218_IN_2R_GAIN,
		       DA7218_IN_2R_DIGITAL_GAIN_SHIFT,
		       DA7218_IN_DIGITAL_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_in_dig_gain_tlv),
	SOC_SINGLE("In Filter2R Switch", DA7218_IN_2R_FILTER_CTRL,
		   DA7218_IN_2R_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),
	SOC_SINGLE("In Filter2R Gain Ramp Switch",
		   DA7218_IN_2R_FILTER_CTRL, DA7218_IN_2R_RAMP_EN_SHIFT,
		   DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),

	/* AGS */
	SOC_SINGLE_TLV("AGS Trigger", DA7218_AGS_TRIGGER,
		       DA7218_AGS_TRIGGER_SHIFT, DA7218_AGS_TRIGGER_MAX,
		       DA7218_INVERT, da7218_ags_trigger_tlv),
	SOC_SINGLE_TLV("AGS Max Attenuation", DA7218_AGS_ATT_MAX,
		       DA7218_AGS_ATT_MAX_SHIFT, DA7218_AGS_ATT_MAX_MAX,
		       DA7218_NO_INVERT, da7218_ags_att_max_tlv),
	SOC_SINGLE("AGS Anticlip Switch", DA7218_AGS_ANTICLIP_CTRL,
		   DA7218_AGS_ANTICLIP_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("AGS Channel1 Switch", DA7218_AGS_ENABLE,
		   DA7218_AGS_ENABLE_CHAN1_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("AGS Channel2 Switch", DA7218_AGS_ENABLE,
		   DA7218_AGS_ENABLE_CHAN2_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),

	/* ALC */
	SOC_ENUM("ALC Attack Rate", da7218_alc_attack_rate),
	SOC_ENUM("ALC Release Rate", da7218_alc_release_rate),
	SOC_ENUM("ALC Hold Time", da7218_alc_hold_time),
	SOC_SINGLE_TLV("ALC Noise Threshold", DA7218_ALC_NOISE,
		       DA7218_ALC_NOISE_SHIFT, DA7218_ALC_THRESHOLD_MAX,
		       DA7218_INVERT, da7218_alc_threshold_tlv),
	SOC_SINGLE_TLV("ALC Min Threshold", DA7218_ALC_TARGET_MIN,
		       DA7218_ALC_THRESHOLD_MIN_SHIFT, DA7218_ALC_THRESHOLD_MAX,
		       DA7218_INVERT, da7218_alc_threshold_tlv),
	SOC_SINGLE_TLV("ALC Max Threshold", DA7218_ALC_TARGET_MAX,
		       DA7218_ALC_THRESHOLD_MAX_SHIFT, DA7218_ALC_THRESHOLD_MAX,
		       DA7218_INVERT, da7218_alc_threshold_tlv),
	SOC_SINGLE_TLV("ALC Max Attenuation", DA7218_ALC_GAIN_LIMITS,
		       DA7218_ALC_ATTEN_MAX_SHIFT, DA7218_ALC_ATTEN_GAIN_MAX,
		       DA7218_NO_INVERT, da7218_alc_gain_tlv),
	SOC_SINGLE_TLV("ALC Max Gain", DA7218_ALC_GAIN_LIMITS,
		       DA7218_ALC_GAIN_MAX_SHIFT, DA7218_ALC_ATTEN_GAIN_MAX,
		       DA7218_NO_INVERT, da7218_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC Min Analog Gain", DA7218_ALC_ANA_GAIN_LIMITS,
			     DA7218_ALC_ANA_GAIN_MIN_SHIFT,
			     DA7218_ALC_ANA_GAIN_MIN, DA7218_ALC_ANA_GAIN_MAX,
			     DA7218_NO_INVERT, da7218_alc_ana_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC Max Analog Gain", DA7218_ALC_ANA_GAIN_LIMITS,
			     DA7218_ALC_ANA_GAIN_MAX_SHIFT,
			     DA7218_ALC_ANA_GAIN_MIN, DA7218_ALC_ANA_GAIN_MAX,
			     DA7218_NO_INVERT, da7218_alc_ana_gain_tlv),
	SOC_ENUM("ALC Anticlip Step", da7218_alc_anticlip_step),
	SOC_SINGLE("ALC Anticlip Switch", DA7218_ALC_ANTICLIP_CTRL,
		   DA7218_ALC_ANTICLIP_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_DOUBLE_EXT("ALC Channel1 Switch", DA7218_ALC_CTRL1,
		       DA7218_ALC_CHAN1_L_EN_SHIFT, DA7218_ALC_CHAN1_R_EN_SHIFT,
		       DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT,
		       snd_soc_get_volsw, da7218_alc_sw_put),
	SOC_DOUBLE_EXT("ALC Channel2 Switch", DA7218_ALC_CTRL1,
		       DA7218_ALC_CHAN2_L_EN_SHIFT, DA7218_ALC_CHAN2_R_EN_SHIFT,
		       DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT,
		       snd_soc_get_volsw, da7218_alc_sw_put),

	/* Envelope Tracking */
	SOC_ENUM("Envelope Tracking Attack Rate", da7218_integ_attack_rate),
	SOC_ENUM("Envelope Tracking Release Rate", da7218_integ_release_rate),

	/* Input High-Pass Filters */
	SOC_ENUM("In Filter1 HPF Mode", da7218_in1_hpf_mode),
	SOC_ENUM("In Filter1 HPF Corner Audio", da7218_in1_audio_hpf_corner),
	SOC_ENUM("In Filter1 HPF Corner Voice", da7218_in1_voice_hpf_corner),
	SOC_ENUM("In Filter2 HPF Mode", da7218_in2_hpf_mode),
	SOC_ENUM("In Filter2 HPF Corner Audio", da7218_in2_audio_hpf_corner),
	SOC_ENUM("In Filter2 HPF Corner Voice", da7218_in2_voice_hpf_corner),

	/* Mic Level Detect */
	SOC_DOUBLE_EXT("Mic Level Detect Channel1 Switch", DA7218_LVL_DET_CTRL,
		       DA7218_LVL_DET_EN_CHAN1L_SHIFT,
		       DA7218_LVL_DET_EN_CHAN1R_SHIFT, DA7218_SWITCH_EN_MAX,
		       DA7218_NO_INVERT, da7218_mic_lvl_det_sw_get,
		       da7218_mic_lvl_det_sw_put),
	SOC_DOUBLE_EXT("Mic Level Detect Channel2 Switch", DA7218_LVL_DET_CTRL,
		       DA7218_LVL_DET_EN_CHAN2L_SHIFT,
		       DA7218_LVL_DET_EN_CHAN2R_SHIFT, DA7218_SWITCH_EN_MAX,
		       DA7218_NO_INVERT, da7218_mic_lvl_det_sw_get,
		       da7218_mic_lvl_det_sw_put),
	SOC_SINGLE("Mic Level Detect Level", DA7218_LVL_DET_LEVEL,
		   DA7218_LVL_DET_LEVEL_SHIFT, DA7218_LVL_DET_LEVEL_MAX,
		   DA7218_NO_INVERT),

	/* Digital Mixer (Input) */
	SOC_SINGLE_TLV("DMix In Filter1L Out1 DAIL Volume",
		       DA7218_DMIX_OUTDAI_1L_INFILT_1L_GAIN,
		       DA7218_OUTDAI_1L_INFILT_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter1L Out1 DAIR Volume",
		       DA7218_DMIX_OUTDAI_1R_INFILT_1L_GAIN,
		       DA7218_OUTDAI_1R_INFILT_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter1L Out2 DAIL Volume",
		       DA7218_DMIX_OUTDAI_2L_INFILT_1L_GAIN,
		       DA7218_OUTDAI_2L_INFILT_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter1L Out2 DAIR Volume",
		       DA7218_DMIX_OUTDAI_2R_INFILT_1L_GAIN,
		       DA7218_OUTDAI_2R_INFILT_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In Filter1R Out1 DAIL Volume",
		       DA7218_DMIX_OUTDAI_1L_INFILT_1R_GAIN,
		       DA7218_OUTDAI_1L_INFILT_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter1R Out1 DAIR Volume",
		       DA7218_DMIX_OUTDAI_1R_INFILT_1R_GAIN,
		       DA7218_OUTDAI_1R_INFILT_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter1R Out2 DAIL Volume",
		       DA7218_DMIX_OUTDAI_2L_INFILT_1R_GAIN,
		       DA7218_OUTDAI_2L_INFILT_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter1R Out2 DAIR Volume",
		       DA7218_DMIX_OUTDAI_2R_INFILT_1R_GAIN,
		       DA7218_OUTDAI_2R_INFILT_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In Filter2L Out1 DAIL Volume",
		       DA7218_DMIX_OUTDAI_1L_INFILT_2L_GAIN,
		       DA7218_OUTDAI_1L_INFILT_2L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter2L Out1 DAIR Volume",
		       DA7218_DMIX_OUTDAI_1R_INFILT_2L_GAIN,
		       DA7218_OUTDAI_1R_INFILT_2L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter2L Out2 DAIL Volume",
		       DA7218_DMIX_OUTDAI_2L_INFILT_2L_GAIN,
		       DA7218_OUTDAI_2L_INFILT_2L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter2L Out2 DAIR Volume",
		       DA7218_DMIX_OUTDAI_2R_INFILT_2L_GAIN,
		       DA7218_OUTDAI_2R_INFILT_2L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In Filter2R Out1 DAIL Volume",
		       DA7218_DMIX_OUTDAI_1L_INFILT_2R_GAIN,
		       DA7218_OUTDAI_1L_INFILT_2R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter2R Out1 DAIR Volume",
		       DA7218_DMIX_OUTDAI_1R_INFILT_2R_GAIN,
		       DA7218_OUTDAI_1R_INFILT_2R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter2R Out2 DAIL Volume",
		       DA7218_DMIX_OUTDAI_2L_INFILT_2R_GAIN,
		       DA7218_OUTDAI_2L_INFILT_2R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter2R Out2 DAIR Volume",
		       DA7218_DMIX_OUTDAI_2R_INFILT_2R_GAIN,
		       DA7218_OUTDAI_2R_INFILT_2R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix ToneGen Out1 DAIL Volume",
		       DA7218_DMIX_OUTDAI_1L_TONEGEN_GAIN,
		       DA7218_OUTDAI_1L_TONEGEN_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix ToneGen Out1 DAIR Volume",
		       DA7218_DMIX_OUTDAI_1R_TONEGEN_GAIN,
		       DA7218_OUTDAI_1R_TONEGEN_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix ToneGen Out2 DAIL Volume",
		       DA7218_DMIX_OUTDAI_2L_TONEGEN_GAIN,
		       DA7218_OUTDAI_2L_TONEGEN_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix ToneGen Out2 DAIR Volume",
		       DA7218_DMIX_OUTDAI_2R_TONEGEN_GAIN,
		       DA7218_OUTDAI_2R_TONEGEN_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In DAIL Out1 DAIL Volume",
		       DA7218_DMIX_OUTDAI_1L_INDAI_1L_GAIN,
		       DA7218_OUTDAI_1L_INDAI_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In DAIL Out1 DAIR Volume",
		       DA7218_DMIX_OUTDAI_1R_INDAI_1L_GAIN,
		       DA7218_OUTDAI_1R_INDAI_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In DAIL Out2 DAIL Volume",
		       DA7218_DMIX_OUTDAI_2L_INDAI_1L_GAIN,
		       DA7218_OUTDAI_2L_INDAI_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In DAIL Out2 DAIR Volume",
		       DA7218_DMIX_OUTDAI_2R_INDAI_1L_GAIN,
		       DA7218_OUTDAI_2R_INDAI_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In DAIR Out1 DAIL Volume",
		       DA7218_DMIX_OUTDAI_1L_INDAI_1R_GAIN,
		       DA7218_OUTDAI_1L_INDAI_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In DAIR Out1 DAIR Volume",
		       DA7218_DMIX_OUTDAI_1R_INDAI_1R_GAIN,
		       DA7218_OUTDAI_1R_INDAI_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In DAIR Out2 DAIL Volume",
		       DA7218_DMIX_OUTDAI_2L_INDAI_1R_GAIN,
		       DA7218_OUTDAI_2L_INDAI_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In DAIR Out2 DAIR Volume",
		       DA7218_DMIX_OUTDAI_2R_INDAI_1R_GAIN,
		       DA7218_OUTDAI_2R_INDAI_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	/* Digital Mixer (Output) */
	SOC_SINGLE_TLV("DMix In Filter1L Out FilterL Volume",
		       DA7218_DMIX_OUTFILT_1L_INFILT_1L_GAIN,
		       DA7218_OUTFILT_1L_INFILT_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter1L Out FilterR Volume",
		       DA7218_DMIX_OUTFILT_1R_INFILT_1L_GAIN,
		       DA7218_OUTFILT_1R_INFILT_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In Filter1R Out FilterL Volume",
		       DA7218_DMIX_OUTFILT_1L_INFILT_1R_GAIN,
		       DA7218_OUTFILT_1L_INFILT_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter1R Out FilterR Volume",
		       DA7218_DMIX_OUTFILT_1R_INFILT_1R_GAIN,
		       DA7218_OUTFILT_1R_INFILT_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In Filter2L Out FilterL Volume",
		       DA7218_DMIX_OUTFILT_1L_INFILT_2L_GAIN,
		       DA7218_OUTFILT_1L_INFILT_2L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter2L Out FilterR Volume",
		       DA7218_DMIX_OUTFILT_1R_INFILT_2L_GAIN,
		       DA7218_OUTFILT_1R_INFILT_2L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In Filter2R Out FilterL Volume",
		       DA7218_DMIX_OUTFILT_1L_INFILT_2R_GAIN,
		       DA7218_OUTFILT_1L_INFILT_2R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In Filter2R Out FilterR Volume",
		       DA7218_DMIX_OUTFILT_1R_INFILT_2R_GAIN,
		       DA7218_OUTFILT_1R_INFILT_2R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix ToneGen Out FilterL Volume",
		       DA7218_DMIX_OUTFILT_1L_TONEGEN_GAIN,
		       DA7218_OUTFILT_1L_TONEGEN_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix ToneGen Out FilterR Volume",
		       DA7218_DMIX_OUTFILT_1R_TONEGEN_GAIN,
		       DA7218_OUTFILT_1R_TONEGEN_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In DAIL Out FilterL Volume",
		       DA7218_DMIX_OUTFILT_1L_INDAI_1L_GAIN,
		       DA7218_OUTFILT_1L_INDAI_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In DAIL Out FilterR Volume",
		       DA7218_DMIX_OUTFILT_1R_INDAI_1L_GAIN,
		       DA7218_OUTFILT_1R_INDAI_1L_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	SOC_SINGLE_TLV("DMix In DAIR Out FilterL Volume",
		       DA7218_DMIX_OUTFILT_1L_INDAI_1R_GAIN,
		       DA7218_OUTFILT_1L_INDAI_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),
	SOC_SINGLE_TLV("DMix In DAIR Out FilterR Volume",
		       DA7218_DMIX_OUTFILT_1R_INDAI_1R_GAIN,
		       DA7218_OUTFILT_1R_INDAI_1R_GAIN_SHIFT,
		       DA7218_DMIX_GAIN_MAX, DA7218_NO_INVERT,
		       da7218_dmix_gain_tlv),

	/* Sidetone Filter */
	SND_SOC_BYTES_EXT("Sidetone BiQuad Coefficients",
			  DA7218_SIDETONE_BIQ_3STAGE_CFG_SIZE,
			  da7218_biquad_coeff_get, da7218_biquad_coeff_put),
	SOC_SINGLE_TLV("Sidetone Volume", DA7218_SIDETONE_GAIN,
		       DA7218_SIDETONE_GAIN_SHIFT, DA7218_DMIX_GAIN_MAX,
		       DA7218_NO_INVERT, da7218_dmix_gain_tlv),
	SOC_SINGLE("Sidetone Switch", DA7218_SIDETONE_CTRL,
		   DA7218_SIDETONE_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),

	/* Tone Generator */
	SOC_ENUM("ToneGen DTMF Key", da7218_tonegen_dtmf_key),
	SOC_SINGLE("ToneGen DTMF Switch", DA7218_TONE_GEN_CFG1,
		   DA7218_DTMF_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_ENUM("ToneGen Sinewave Gen Type", da7218_tonegen_swg_sel),
	SOC_SINGLE_EXT("ToneGen Sinewave1 Freq", DA7218_TONE_GEN_FREQ1_L,
		       DA7218_FREQ1_L_SHIFT, DA7218_FREQ_MAX, DA7218_NO_INVERT,
		       da7218_tonegen_freq_get, da7218_tonegen_freq_put),
	SOC_SINGLE_EXT("ToneGen Sinewave2 Freq", DA7218_TONE_GEN_FREQ2_L,
		       DA7218_FREQ2_L_SHIFT, DA7218_FREQ_MAX, DA7218_NO_INVERT,
		       da7218_tonegen_freq_get, da7218_tonegen_freq_put),
	SOC_SINGLE("ToneGen On Time", DA7218_TONE_GEN_ON_PER,
		   DA7218_BEEP_ON_PER_SHIFT, DA7218_BEEP_ON_OFF_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("ToneGen Off Time", DA7218_TONE_GEN_OFF_PER,
		   DA7218_BEEP_OFF_PER_SHIFT, DA7218_BEEP_ON_OFF_MAX,
		   DA7218_NO_INVERT),

	/* Gain ramping */
	SOC_ENUM("Gain Ramp Rate", da7218_gain_ramp_rate),

	/* DGS */
	SOC_SINGLE_TLV("DGS Trigger", DA7218_DGS_TRIGGER,
		       DA7218_DGS_TRIGGER_LVL_SHIFT, DA7218_DGS_TRIGGER_MAX,
		       DA7218_INVERT, da7218_dgs_trigger_tlv),
	SOC_ENUM("DGS Rise Coefficient", da7218_dgs_rise_coeff),
	SOC_ENUM("DGS Fall Coefficient", da7218_dgs_fall_coeff),
	SOC_SINGLE("DGS Sync Delay", DA7218_DGS_SYNC_DELAY,
		   DA7218_DGS_SYNC_DELAY_SHIFT, DA7218_DGS_SYNC_DELAY_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("DGS Fast SR Sync Delay", DA7218_DGS_SYNC_DELAY2,
		   DA7218_DGS_SYNC_DELAY2_SHIFT, DA7218_DGS_SYNC_DELAY_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("DGS Voice Filter Sync Delay", DA7218_DGS_SYNC_DELAY3,
		   DA7218_DGS_SYNC_DELAY3_SHIFT, DA7218_DGS_SYNC_DELAY3_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE_TLV("DGS Anticlip Level", DA7218_DGS_LEVELS,
		       DA7218_DGS_ANTICLIP_LVL_SHIFT,
		       DA7218_DGS_ANTICLIP_LVL_MAX, DA7218_INVERT,
		       da7218_dgs_anticlip_tlv),
	SOC_SINGLE_TLV("DGS Signal Level", DA7218_DGS_LEVELS,
		       DA7218_DGS_SIGNAL_LVL_SHIFT, DA7218_DGS_SIGNAL_LVL_MAX,
		       DA7218_INVERT, da7218_dgs_signal_tlv),
	SOC_SINGLE("DGS Gain Subrange Switch", DA7218_DGS_GAIN_CTRL,
		   DA7218_DGS_SUBR_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("DGS Gain Ramp Switch", DA7218_DGS_GAIN_CTRL,
		   DA7218_DGS_RAMP_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),
	SOC_SINGLE("DGS Gain Steps", DA7218_DGS_GAIN_CTRL,
		   DA7218_DGS_STEPS_SHIFT, DA7218_DGS_STEPS_MAX,
		   DA7218_NO_INVERT),
	SOC_DOUBLE("DGS Switch", DA7218_DGS_ENABLE, DA7218_DGS_ENABLE_L_SHIFT,
		   DA7218_DGS_ENABLE_R_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),

	/* Output High-Pass Filter */
	SOC_ENUM("Out Filter HPF Mode", da7218_out1_hpf_mode),
	SOC_ENUM("Out Filter HPF Corner Audio", da7218_out1_audio_hpf_corner),
	SOC_ENUM("Out Filter HPF Corner Voice", da7218_out1_voice_hpf_corner),

	/* 5-Band Equaliser */
	SOC_SINGLE_TLV("Out EQ Band1 Volume", DA7218_OUT_1_EQ_12_FILTER_CTRL,
		       DA7218_OUT_1_EQ_BAND1_SHIFT, DA7218_OUT_EQ_BAND_MAX,
		       DA7218_NO_INVERT, da7218_out_eq_band_tlv),
	SOC_SINGLE_TLV("Out EQ Band2 Volume", DA7218_OUT_1_EQ_12_FILTER_CTRL,
		       DA7218_OUT_1_EQ_BAND2_SHIFT, DA7218_OUT_EQ_BAND_MAX,
		       DA7218_NO_INVERT, da7218_out_eq_band_tlv),
	SOC_SINGLE_TLV("Out EQ Band3 Volume", DA7218_OUT_1_EQ_34_FILTER_CTRL,
		       DA7218_OUT_1_EQ_BAND3_SHIFT, DA7218_OUT_EQ_BAND_MAX,
		       DA7218_NO_INVERT, da7218_out_eq_band_tlv),
	SOC_SINGLE_TLV("Out EQ Band4 Volume", DA7218_OUT_1_EQ_34_FILTER_CTRL,
		       DA7218_OUT_1_EQ_BAND4_SHIFT, DA7218_OUT_EQ_BAND_MAX,
		       DA7218_NO_INVERT, da7218_out_eq_band_tlv),
	SOC_SINGLE_TLV("Out EQ Band5 Volume", DA7218_OUT_1_EQ_5_FILTER_CTRL,
		       DA7218_OUT_1_EQ_BAND5_SHIFT, DA7218_OUT_EQ_BAND_MAX,
		       DA7218_NO_INVERT, da7218_out_eq_band_tlv),
	SOC_SINGLE("Out EQ Switch", DA7218_OUT_1_EQ_5_FILTER_CTRL,
		   DA7218_OUT_1_EQ_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_NO_INVERT),

	/* BiQuad Filters */
	SND_SOC_BYTES_EXT("BiQuad Coefficients",
			  DA7218_OUT_1_BIQ_5STAGE_CFG_SIZE,
			  da7218_biquad_coeff_get, da7218_biquad_coeff_put),
	SOC_SINGLE("BiQuad Filter Switch", DA7218_OUT_1_BIQ_5STAGE_CTRL,
		   DA7218_OUT_1_BIQ_5STAGE_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		   DA7218_INVERT),

	/* Output Filters */
	SOC_DOUBLE_R_RANGE_TLV("Out Filter Volume", DA7218_OUT_1L_GAIN,
			       DA7218_OUT_1R_GAIN,
			       DA7218_OUT_1L_DIGITAL_GAIN_SHIFT,
			       DA7218_OUT_DIGITAL_GAIN_MIN,
			       DA7218_OUT_DIGITAL_GAIN_MAX, DA7218_NO_INVERT,
			       da7218_out_dig_gain_tlv),
	SOC_DOUBLE_R("Out Filter Switch", DA7218_OUT_1L_FILTER_CTRL,
		     DA7218_OUT_1R_FILTER_CTRL, DA7218_OUT_1L_MUTE_EN_SHIFT,
		     DA7218_SWITCH_EN_MAX, DA7218_INVERT),
	SOC_DOUBLE_R("Out Filter Gain Subrange Switch",
		     DA7218_OUT_1L_FILTER_CTRL, DA7218_OUT_1R_FILTER_CTRL,
		     DA7218_OUT_1L_SUBRANGE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		     DA7218_NO_INVERT),
	SOC_DOUBLE_R("Out Filter Gain Ramp Switch", DA7218_OUT_1L_FILTER_CTRL,
		     DA7218_OUT_1R_FILTER_CTRL, DA7218_OUT_1L_RAMP_EN_SHIFT,
		     DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),

	/* Mixer Output */
	SOC_DOUBLE_R_RANGE_TLV("Mixout Volume", DA7218_MIXOUT_L_GAIN,
			       DA7218_MIXOUT_R_GAIN,
			       DA7218_MIXOUT_L_AMP_GAIN_SHIFT,
			       DA7218_MIXOUT_AMP_GAIN_MIN,
			       DA7218_MIXOUT_AMP_GAIN_MAX, DA7218_NO_INVERT,
			       da7218_mixout_gain_tlv),

	/* DAC Noise Gate */
	SOC_ENUM("DAC NG Setup Time", da7218_dac_ng_setup_time),
	SOC_ENUM("DAC NG Rampup Rate", da7218_dac_ng_rampup_rate),
	SOC_ENUM("DAC NG Rampdown Rate", da7218_dac_ng_rampdown_rate),
	SOC_SINGLE_TLV("DAC NG Off Threshold", DA7218_DAC_NG_OFF_THRESH,
		       DA7218_DAC_NG_OFF_THRESHOLD_SHIFT,
		       DA7218_DAC_NG_THRESHOLD_MAX, DA7218_NO_INVERT,
		       da7218_dac_ng_threshold_tlv),
	SOC_SINGLE_TLV("DAC NG On Threshold", DA7218_DAC_NG_ON_THRESH,
		       DA7218_DAC_NG_ON_THRESHOLD_SHIFT,
		       DA7218_DAC_NG_THRESHOLD_MAX, DA7218_NO_INVERT,
		       da7218_dac_ng_threshold_tlv),
	SOC_SINGLE("DAC NG Switch", DA7218_DAC_NG_CTRL, DA7218_DAC_NG_EN_SHIFT,
		   DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),

	/* CP */
	SOC_ENUM("Charge Pump Track Mode", da7218_cp_mchange),
	SOC_ENUM("Charge Pump Frequency", da7218_cp_fcontrol),
	SOC_ENUM("Charge Pump Decay Rate", da7218_cp_tau_delay),
	SOC_SINGLE("Charge Pump Threshold", DA7218_CP_VOL_THRESHOLD1,
		   DA7218_CP_THRESH_VDD2_SHIFT, DA7218_CP_THRESH_VDD2_MAX,
		   DA7218_NO_INVERT),

	/* Headphones */
	SOC_DOUBLE_R_RANGE_TLV("Headphone Volume", DA7218_HP_L_GAIN,
			       DA7218_HP_R_GAIN, DA7218_HP_L_AMP_GAIN_SHIFT,
			       DA7218_HP_AMP_GAIN_MIN, DA7218_HP_AMP_GAIN_MAX,
			       DA7218_NO_INVERT, da7218_hp_gain_tlv),
	SOC_DOUBLE_R("Headphone Switch", DA7218_HP_L_CTRL, DA7218_HP_R_CTRL,
		     DA7218_HP_L_AMP_MUTE_EN_SHIFT, DA7218_SWITCH_EN_MAX,
		     DA7218_INVERT),
	SOC_DOUBLE_R("Headphone Gain Ramp Switch", DA7218_HP_L_CTRL,
		     DA7218_HP_R_CTRL, DA7218_HP_L_AMP_RAMP_EN_SHIFT,
		     DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),
	SOC_DOUBLE_R("Headphone ZC Gain Switch", DA7218_HP_L_CTRL,
		     DA7218_HP_R_CTRL, DA7218_HP_L_AMP_ZC_EN_SHIFT,
		     DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),
};


/*
 * DAPM Mux Controls
 */

static const char * const da7218_mic_sel_text[] = { "Analog", "Digital" };

static const struct soc_enum da7218_mic1_sel =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(da7218_mic_sel_text),
			    da7218_mic_sel_text);

static const struct snd_kcontrol_new da7218_mic1_sel_mux =
	SOC_DAPM_ENUM("Mic1 Mux", da7218_mic1_sel);

static const struct soc_enum da7218_mic2_sel =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(da7218_mic_sel_text),
			    da7218_mic_sel_text);

static const struct snd_kcontrol_new da7218_mic2_sel_mux =
	SOC_DAPM_ENUM("Mic2 Mux", da7218_mic2_sel);

static const char * const da7218_sidetone_in_sel_txt[] = {
	"In Filter1L", "In Filter1R", "In Filter2L", "In Filter2R"
};

static const struct soc_enum da7218_sidetone_in_sel =
	SOC_ENUM_SINGLE(DA7218_SIDETONE_IN_SELECT,
			DA7218_SIDETONE_IN_SELECT_SHIFT,
			DA7218_SIDETONE_IN_SELECT_MAX,
			da7218_sidetone_in_sel_txt);

static const struct snd_kcontrol_new da7218_sidetone_in_sel_mux =
	SOC_DAPM_ENUM("Sidetone Mux", da7218_sidetone_in_sel);

static const char * const da7218_out_filt_biq_sel_txt[] = {
	"Bypass", "Enabled"
};

static const struct soc_enum da7218_out_filtl_biq_sel =
	SOC_ENUM_SINGLE(DA7218_OUT_1L_FILTER_CTRL,
			DA7218_OUT_1L_BIQ_5STAGE_SEL_SHIFT,
			DA7218_OUT_BIQ_5STAGE_SEL_MAX,
			da7218_out_filt_biq_sel_txt);

static const struct snd_kcontrol_new da7218_out_filtl_biq_sel_mux =
	SOC_DAPM_ENUM("Out FilterL BiQuad Mux", da7218_out_filtl_biq_sel);

static const struct soc_enum da7218_out_filtr_biq_sel =
	SOC_ENUM_SINGLE(DA7218_OUT_1R_FILTER_CTRL,
			DA7218_OUT_1R_BIQ_5STAGE_SEL_SHIFT,
			DA7218_OUT_BIQ_5STAGE_SEL_MAX,
			da7218_out_filt_biq_sel_txt);

static const struct snd_kcontrol_new da7218_out_filtr_biq_sel_mux =
	SOC_DAPM_ENUM("Out FilterR BiQuad Mux", da7218_out_filtr_biq_sel);


/*
 * DAPM Mixer Controls
 */

#define DA7218_DMIX_CTRLS(reg)						\
	SOC_DAPM_SINGLE("In Filter1L Switch", reg,			\
			DA7218_DMIX_SRC_INFILT1L,			\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),	\
	SOC_DAPM_SINGLE("In Filter1R Switch", reg,			\
			DA7218_DMIX_SRC_INFILT1R,			\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),	\
	SOC_DAPM_SINGLE("In Filter2L Switch", reg,			\
			DA7218_DMIX_SRC_INFILT2L,			\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),	\
	SOC_DAPM_SINGLE("In Filter2R Switch", reg,			\
			DA7218_DMIX_SRC_INFILT2R,			\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),	\
	SOC_DAPM_SINGLE("ToneGen Switch", reg,				\
			DA7218_DMIX_SRC_TONEGEN,			\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),	\
	SOC_DAPM_SINGLE("DAIL Switch", reg, DA7218_DMIX_SRC_DAIL,	\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),	\
	SOC_DAPM_SINGLE("DAIR Switch", reg, DA7218_DMIX_SRC_DAIR,	\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT)

static const struct snd_kcontrol_new da7218_out_dai1l_mix_controls[] = {
	DA7218_DMIX_CTRLS(DA7218_DROUTING_OUTDAI_1L),
};

static const struct snd_kcontrol_new da7218_out_dai1r_mix_controls[] = {
	DA7218_DMIX_CTRLS(DA7218_DROUTING_OUTDAI_1R),
};

static const struct snd_kcontrol_new da7218_out_dai2l_mix_controls[] = {
	DA7218_DMIX_CTRLS(DA7218_DROUTING_OUTDAI_2L),
};

static const struct snd_kcontrol_new da7218_out_dai2r_mix_controls[] = {
	DA7218_DMIX_CTRLS(DA7218_DROUTING_OUTDAI_2R),
};

static const struct snd_kcontrol_new da7218_out_filtl_mix_controls[] = {
	DA7218_DMIX_CTRLS(DA7218_DROUTING_OUTFILT_1L),
};

static const struct snd_kcontrol_new da7218_out_filtr_mix_controls[] = {
	DA7218_DMIX_CTRLS(DA7218_DROUTING_OUTFILT_1R),
};

#define DA7218_DMIX_ST_CTRLS(reg)					\
	SOC_DAPM_SINGLE("Out FilterL Switch", reg,			\
			DA7218_DMIX_ST_SRC_OUTFILT1L,			\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),	\
	SOC_DAPM_SINGLE("Out FilterR Switch", reg,			\
			DA7218_DMIX_ST_SRC_OUTFILT1R,			\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT),	\
	SOC_DAPM_SINGLE("Sidetone Switch", reg,				\
			DA7218_DMIX_ST_SRC_SIDETONE,			\
			DA7218_SWITCH_EN_MAX, DA7218_NO_INVERT)		\

static const struct snd_kcontrol_new da7218_st_out_filtl_mix_controls[] = {
	DA7218_DMIX_ST_CTRLS(DA7218_DROUTING_ST_OUTFILT_1L),
};

static const struct snd_kcontrol_new da7218_st_out_filtr_mix_controls[] = {
	DA7218_DMIX_ST_CTRLS(DA7218_DROUTING_ST_OUTFILT_1R),
};


/*
 * DAPM Events
 */

/*
 * We keep track of which input filters are enabled. This is used in the logic
 * for controlling the mic level detect feature.
 */
static int da7218_in_filter_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	u8 mask;

	switch (w->reg) {
	case DA7218_IN_1L_FILTER_CTRL:
		mask = (1 << DA7218_LVL_DET_EN_CHAN1L_SHIFT);
		break;
	case DA7218_IN_1R_FILTER_CTRL:
		mask = (1 << DA7218_LVL_DET_EN_CHAN1R_SHIFT);
		break;
	case DA7218_IN_2L_FILTER_CTRL:
		mask = (1 << DA7218_LVL_DET_EN_CHAN2L_SHIFT);
		break;
	case DA7218_IN_2R_FILTER_CTRL:
		mask = (1 << DA7218_LVL_DET_EN_CHAN2R_SHIFT);
		break;
	default:
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		da7218->in_filt_en |= mask;
		/*
		 * If we're enabling path for mic level detect, wait for path
		 * to settle before enabling feature to avoid incorrect and
		 * unwanted detect events.
		 */
		if (mask & da7218->mic_lvl_det_en)
			msleep(DA7218_MIC_LVL_DET_DELAY);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		da7218->in_filt_en &= ~mask;
		break;
	default:
		return -EINVAL;
	}

	/* Enable configured level detection paths */
	snd_soc_write(codec, DA7218_LVL_DET_CTRL,
		      (da7218->in_filt_en & da7218->mic_lvl_det_en));

	return 0;
}

static int da7218_dai_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	u8 pll_ctrl, pll_status, refosc_cal;
	int i;
	bool success;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (da7218->master)
			/* Enable DAI clks for master mode */
			snd_soc_update_bits(codec, DA7218_DAI_CLK_MODE,
					    DA7218_DAI_CLK_EN_MASK,
					    DA7218_DAI_CLK_EN_MASK);

		/* Tune reference oscillator */
		snd_soc_write(codec, DA7218_PLL_REFOSC_CAL,
			      DA7218_PLL_REFOSC_CAL_START_MASK);
		snd_soc_write(codec, DA7218_PLL_REFOSC_CAL,
			      DA7218_PLL_REFOSC_CAL_START_MASK |
			      DA7218_PLL_REFOSC_CAL_EN_MASK);

		/* Check tuning complete */
		i = 0;
		success = false;
		do {
			refosc_cal = snd_soc_read(codec, DA7218_PLL_REFOSC_CAL);
			if (!(refosc_cal & DA7218_PLL_REFOSC_CAL_START_MASK)) {
				success = true;
			} else {
				++i;
				usleep_range(DA7218_REF_OSC_CHECK_DELAY_MIN,
					     DA7218_REF_OSC_CHECK_DELAY_MAX);
			}
		} while ((i < DA7218_REF_OSC_CHECK_TRIES) && (!success));

		if (!success)
			dev_warn(codec->dev,
				 "Reference oscillator failed calibration\n");

		/* PC synchronised to DAI */
		snd_soc_write(codec, DA7218_PC_COUNT,
			      DA7218_PC_RESYNC_AUTO_MASK);

		/* If SRM not enabled, we don't need to check status */
		pll_ctrl = snd_soc_read(codec, DA7218_PLL_CTRL);
		if ((pll_ctrl & DA7218_PLL_MODE_MASK) != DA7218_PLL_MODE_SRM)
			return 0;

		/* Check SRM has locked */
		i = 0;
		success = false;
		do {
			pll_status = snd_soc_read(codec, DA7218_PLL_STATUS);
			if (pll_status & DA7218_PLL_SRM_STATUS_SRM_LOCK) {
				success = true;
			} else {
				++i;
				msleep(DA7218_SRM_CHECK_DELAY);
			}
		} while ((i < DA7218_SRM_CHECK_TRIES) && (!success));

		if (!success)
			dev_warn(codec->dev, "SRM failed to lock\n");

		return 0;
	case SND_SOC_DAPM_POST_PMD:
		/* PC free-running */
		snd_soc_write(codec, DA7218_PC_COUNT, DA7218_PC_FREERUN_MASK);

		if (da7218->master)
			/* Disable DAI clks for master mode */
			snd_soc_update_bits(codec, DA7218_DAI_CLK_MODE,
					    DA7218_DAI_CLK_EN_MASK, 0);

		return 0;
	default:
		return -EINVAL;
	}
}

static int da7218_cp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);

	/*
	 * If this is DA7217 and we're using single supply for differential
	 * output, we really don't want to touch the charge pump.
	 */
	if (da7218->hp_single_supply)
		return 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(codec, DA7218_CP_CTRL, DA7218_CP_EN_MASK,
				    DA7218_CP_EN_MASK);
		return 0;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, DA7218_CP_CTRL, DA7218_CP_EN_MASK,
				    0);
		return 0;
	default:
		return -EINVAL;
	}
}

static int da7218_hp_pga_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Enable headphone output */
		snd_soc_update_bits(codec, w->reg, DA7218_HP_AMP_OE_MASK,
				    DA7218_HP_AMP_OE_MASK);
		return 0;
	case SND_SOC_DAPM_PRE_PMD:
		/* Headphone output high impedance */
		snd_soc_update_bits(codec, w->reg, DA7218_HP_AMP_OE_MASK, 0);
		return 0;
	default:
		return -EINVAL;
	}
}


/*
 * DAPM Widgets
 */

static const struct snd_soc_dapm_widget da7218_dapm_widgets[] = {
	/* Input Supplies */
	SND_SOC_DAPM_SUPPLY("Mic Bias1", DA7218_MICBIAS_EN,
			    DA7218_MICBIAS_1_EN_SHIFT, DA7218_NO_INVERT,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Bias2", DA7218_MICBIAS_EN,
			    DA7218_MICBIAS_2_EN_SHIFT, DA7218_NO_INVERT,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMic1 Left", DA7218_DMIC_1_CTRL,
			    DA7218_DMIC_1L_EN_SHIFT, DA7218_NO_INVERT,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMic1 Right", DA7218_DMIC_1_CTRL,
			    DA7218_DMIC_1R_EN_SHIFT, DA7218_NO_INVERT,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMic2 Left", DA7218_DMIC_2_CTRL,
			    DA7218_DMIC_2L_EN_SHIFT, DA7218_NO_INVERT,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMic2 Right", DA7218_DMIC_2_CTRL,
			    DA7218_DMIC_2R_EN_SHIFT, DA7218_NO_INVERT,
			    NULL, 0),

	/* Inputs */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("DMIC1L"),
	SND_SOC_DAPM_INPUT("DMIC1R"),
	SND_SOC_DAPM_INPUT("DMIC2L"),
	SND_SOC_DAPM_INPUT("DMIC2R"),

	/* Input Mixer Supplies */
	SND_SOC_DAPM_SUPPLY("Mixin1 Supply", DA7218_MIXIN_1_CTRL,
			    DA7218_MIXIN_1_MIX_SEL_SHIFT, DA7218_NO_INVERT,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mixin2 Supply", DA7218_MIXIN_2_CTRL,
			    DA7218_MIXIN_2_MIX_SEL_SHIFT, DA7218_NO_INVERT,
			    NULL, 0),

	/* Input PGAs */
	SND_SOC_DAPM_PGA("Mic1 PGA", DA7218_MIC_1_CTRL,
			 DA7218_MIC_1_AMP_EN_SHIFT, DA7218_NO_INVERT,
			 NULL, 0),
	SND_SOC_DAPM_PGA("Mic2 PGA", DA7218_MIC_2_CTRL,
			 DA7218_MIC_2_AMP_EN_SHIFT, DA7218_NO_INVERT,
			 NULL, 0),
	SND_SOC_DAPM_PGA("Mixin1 PGA", DA7218_MIXIN_1_CTRL,
			 DA7218_MIXIN_1_AMP_EN_SHIFT, DA7218_NO_INVERT,
			 NULL, 0),
	SND_SOC_DAPM_PGA("Mixin2 PGA", DA7218_MIXIN_2_CTRL,
			 DA7218_MIXIN_2_AMP_EN_SHIFT, DA7218_NO_INVERT,
			 NULL, 0),

	/* Mic/DMic Muxes */
	SND_SOC_DAPM_MUX("Mic1 Mux", SND_SOC_NOPM, 0, 0, &da7218_mic1_sel_mux),
	SND_SOC_DAPM_MUX("Mic2 Mux", SND_SOC_NOPM, 0, 0, &da7218_mic2_sel_mux),

	/* Input Filters */
	SND_SOC_DAPM_ADC_E("In Filter1L", NULL, DA7218_IN_1L_FILTER_CTRL,
			   DA7218_IN_1L_FILTER_EN_SHIFT, DA7218_NO_INVERT,
			   da7218_in_filter_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("In Filter1R", NULL, DA7218_IN_1R_FILTER_CTRL,
			   DA7218_IN_1R_FILTER_EN_SHIFT, DA7218_NO_INVERT,
			   da7218_in_filter_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("In Filter2L", NULL, DA7218_IN_2L_FILTER_CTRL,
			   DA7218_IN_2L_FILTER_EN_SHIFT, DA7218_NO_INVERT,
			   da7218_in_filter_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("In Filter2R", NULL, DA7218_IN_2R_FILTER_CTRL,
			   DA7218_IN_2R_FILTER_EN_SHIFT, DA7218_NO_INVERT,
			   da7218_in_filter_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	/* Tone Generator */
	SND_SOC_DAPM_SIGGEN("TONE"),
	SND_SOC_DAPM_PGA("Tone Generator", DA7218_TONE_GEN_CFG1,
			 DA7218_START_STOPN_SHIFT, DA7218_NO_INVERT, NULL, 0),

	/* Sidetone Input */
	SND_SOC_DAPM_MUX("Sidetone Mux", SND_SOC_NOPM, 0, 0,
			 &da7218_sidetone_in_sel_mux),
	SND_SOC_DAPM_ADC("Sidetone Filter", NULL, DA7218_SIDETONE_CTRL,
			 DA7218_SIDETONE_FILTER_EN_SHIFT, DA7218_NO_INVERT),

	/* Input Mixers */
	SND_SOC_DAPM_MIXER("Mixer DAI1L", SND_SOC_NOPM, 0, 0,
			   da7218_out_dai1l_mix_controls,
			   ARRAY_SIZE(da7218_out_dai1l_mix_controls)),
	SND_SOC_DAPM_MIXER("Mixer DAI1R", SND_SOC_NOPM, 0, 0,
			   da7218_out_dai1r_mix_controls,
			   ARRAY_SIZE(da7218_out_dai1r_mix_controls)),
	SND_SOC_DAPM_MIXER("Mixer DAI2L", SND_SOC_NOPM, 0, 0,
			   da7218_out_dai2l_mix_controls,
			   ARRAY_SIZE(da7218_out_dai2l_mix_controls)),
	SND_SOC_DAPM_MIXER("Mixer DAI2R", SND_SOC_NOPM, 0, 0,
			   da7218_out_dai2r_mix_controls,
			   ARRAY_SIZE(da7218_out_dai2r_mix_controls)),

	/* DAI Supply */
	SND_SOC_DAPM_SUPPLY("DAI", DA7218_DAI_CTRL, DA7218_DAI_EN_SHIFT,
			    DA7218_NO_INVERT, da7218_dai_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	/* DAI */
	SND_SOC_DAPM_AIF_OUT("DAIOUT", "Capture", 0, DA7218_DAI_TDM_CTRL,
			     DA7218_DAI_OE_SHIFT, DA7218_NO_INVERT),
	SND_SOC_DAPM_AIF_IN("DAIIN", "Playback", 0, SND_SOC_NOPM, 0, 0),

	/* Output Mixers */
	SND_SOC_DAPM_MIXER("Mixer Out FilterL", SND_SOC_NOPM, 0, 0,
			   da7218_out_filtl_mix_controls,
			   ARRAY_SIZE(da7218_out_filtl_mix_controls)),
	SND_SOC_DAPM_MIXER("Mixer Out FilterR", SND_SOC_NOPM, 0, 0,
			   da7218_out_filtr_mix_controls,
			   ARRAY_SIZE(da7218_out_filtr_mix_controls)),

	/* BiQuad Filters */
	SND_SOC_DAPM_MUX("Out FilterL BiQuad Mux", SND_SOC_NOPM, 0, 0,
			 &da7218_out_filtl_biq_sel_mux),
	SND_SOC_DAPM_MUX("Out FilterR BiQuad Mux", SND_SOC_NOPM, 0, 0,
			 &da7218_out_filtr_biq_sel_mux),
	SND_SOC_DAPM_DAC("BiQuad Filter", NULL, DA7218_OUT_1_BIQ_5STAGE_CTRL,
			 DA7218_OUT_1_BIQ_5STAGE_FILTER_EN_SHIFT,
			 DA7218_NO_INVERT),

	/* Sidetone Mixers */
	SND_SOC_DAPM_MIXER("ST Mixer Out FilterL", SND_SOC_NOPM, 0, 0,
			   da7218_st_out_filtl_mix_controls,
			   ARRAY_SIZE(da7218_st_out_filtl_mix_controls)),
	SND_SOC_DAPM_MIXER("ST Mixer Out FilterR", SND_SOC_NOPM, 0, 0,
			   da7218_st_out_filtr_mix_controls,
			   ARRAY_SIZE(da7218_st_out_filtr_mix_controls)),

	/* Output Filters */
	SND_SOC_DAPM_DAC("Out FilterL", NULL, DA7218_OUT_1L_FILTER_CTRL,
			 DA7218_OUT_1L_FILTER_EN_SHIFT, DA7218_NO_INVERT),
	SND_SOC_DAPM_DAC("Out FilterR", NULL, DA7218_OUT_1R_FILTER_CTRL,
			 DA7218_IN_1R_FILTER_EN_SHIFT, DA7218_NO_INVERT),

	/* Output PGAs */
	SND_SOC_DAPM_PGA("Mixout Left PGA", DA7218_MIXOUT_L_CTRL,
			 DA7218_MIXOUT_L_AMP_EN_SHIFT, DA7218_NO_INVERT,
			 NULL, 0),
	SND_SOC_DAPM_PGA("Mixout Right PGA", DA7218_MIXOUT_R_CTRL,
			 DA7218_MIXOUT_R_AMP_EN_SHIFT, DA7218_NO_INVERT,
			 NULL, 0),
	SND_SOC_DAPM_PGA_E("Headphone Left PGA", DA7218_HP_L_CTRL,
			   DA7218_HP_L_AMP_EN_SHIFT, DA7218_NO_INVERT, NULL, 0,
			   da7218_hp_pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("Headphone Right PGA", DA7218_HP_R_CTRL,
			   DA7218_HP_R_AMP_EN_SHIFT, DA7218_NO_INVERT, NULL, 0,
			   da7218_hp_pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	/* Output Supplies */
	SND_SOC_DAPM_SUPPLY("Charge Pump", SND_SOC_NOPM, 0, 0, da7218_cp_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
};


/*
 * DAPM Mixer Routes
 */

#define DA7218_DMIX_ROUTES(name)				\
	{name, "In Filter1L Switch", "In Filter1L"},		\
	{name, "In Filter1R Switch", "In Filter1R"},		\
	{name, "In Filter2L Switch", "In Filter2L"},		\
	{name, "In Filter2R Switch", "In Filter2R"},		\
	{name, "ToneGen Switch", "Tone Generator"},		\
	{name, "DAIL Switch", "DAIIN"},				\
	{name, "DAIR Switch", "DAIIN"}

#define DA7218_DMIX_ST_ROUTES(name)				\
	{name, "Out FilterL Switch", "Out FilterL BiQuad Mux"},	\
	{name, "Out FilterR Switch", "Out FilterR BiQuad Mux"},	\
	{name, "Sidetone Switch", "Sidetone Filter"}


/*
 * DAPM audio route definition
 */

static const struct snd_soc_dapm_route da7218_audio_map[] = {
	/* Input paths */
	{"MIC1", NULL, "Mic Bias1"},
	{"MIC2", NULL, "Mic Bias2"},
	{"DMIC1L", NULL, "Mic Bias1"},
	{"DMIC1L", NULL, "DMic1 Left"},
	{"DMIC1R", NULL, "Mic Bias1"},
	{"DMIC1R", NULL, "DMic1 Right"},
	{"DMIC2L", NULL, "Mic Bias2"},
	{"DMIC2L", NULL, "DMic2 Left"},
	{"DMIC2R", NULL, "Mic Bias2"},
	{"DMIC2R", NULL, "DMic2 Right"},

	{"Mic1 PGA", NULL, "MIC1"},
	{"Mic2 PGA", NULL, "MIC2"},

	{"Mixin1 PGA", NULL, "Mixin1 Supply"},
	{"Mixin2 PGA", NULL, "Mixin2 Supply"},

	{"Mixin1 PGA", NULL, "Mic1 PGA"},
	{"Mixin2 PGA", NULL, "Mic2 PGA"},

	{"Mic1 Mux", "Analog", "Mixin1 PGA"},
	{"Mic1 Mux", "Digital", "DMIC1L"},
	{"Mic1 Mux", "Digital", "DMIC1R"},
	{"Mic2 Mux", "Analog", "Mixin2 PGA"},
	{"Mic2 Mux", "Digital", "DMIC2L"},
	{"Mic2 Mux", "Digital", "DMIC2R"},

	{"In Filter1L", NULL, "Mic1 Mux"},
	{"In Filter1R", NULL, "Mic1 Mux"},
	{"In Filter2L", NULL, "Mic2 Mux"},
	{"In Filter2R", NULL, "Mic2 Mux"},

	{"Tone Generator", NULL, "TONE"},

	{"Sidetone Mux", "In Filter1L", "In Filter1L"},
	{"Sidetone Mux", "In Filter1R", "In Filter1R"},
	{"Sidetone Mux", "In Filter2L", "In Filter2L"},
	{"Sidetone Mux", "In Filter2R", "In Filter2R"},
	{"Sidetone Filter", NULL, "Sidetone Mux"},

	DA7218_DMIX_ROUTES("Mixer DAI1L"),
	DA7218_DMIX_ROUTES("Mixer DAI1R"),
	DA7218_DMIX_ROUTES("Mixer DAI2L"),
	DA7218_DMIX_ROUTES("Mixer DAI2R"),

	{"DAIOUT", NULL, "Mixer DAI1L"},
	{"DAIOUT", NULL, "Mixer DAI1R"},
	{"DAIOUT", NULL, "Mixer DAI2L"},
	{"DAIOUT", NULL, "Mixer DAI2R"},

	{"DAIOUT", NULL, "DAI"},

	/* Output paths */
	{"DAIIN", NULL, "DAI"},

	DA7218_DMIX_ROUTES("Mixer Out FilterL"),
	DA7218_DMIX_ROUTES("Mixer Out FilterR"),

	{"BiQuad Filter", NULL, "Mixer Out FilterL"},
	{"BiQuad Filter", NULL, "Mixer Out FilterR"},

	{"Out FilterL BiQuad Mux", "Bypass", "Mixer Out FilterL"},
	{"Out FilterL BiQuad Mux", "Enabled", "BiQuad Filter"},
	{"Out FilterR BiQuad Mux", "Bypass", "Mixer Out FilterR"},
	{"Out FilterR BiQuad Mux", "Enabled", "BiQuad Filter"},

	DA7218_DMIX_ST_ROUTES("ST Mixer Out FilterL"),
	DA7218_DMIX_ST_ROUTES("ST Mixer Out FilterR"),

	{"Out FilterL", NULL, "ST Mixer Out FilterL"},
	{"Out FilterR", NULL, "ST Mixer Out FilterR"},

	{"Mixout Left PGA", NULL, "Out FilterL"},
	{"Mixout Right PGA", NULL, "Out FilterR"},

	{"Headphone Left PGA", NULL, "Mixout Left PGA"},
	{"Headphone Right PGA", NULL, "Mixout Right PGA"},

	{"HPL", NULL, "Headphone Left PGA"},
	{"HPR", NULL, "Headphone Right PGA"},

	{"HPL", NULL, "Charge Pump"},
	{"HPR", NULL, "Charge Pump"},
};


/*
 * DAI operations
 */

static int da7218_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	int ret;

	if (da7218->mclk_rate == freq)
		return 0;

	if ((freq < 2000000) || (freq > 54000000)) {
		dev_err(codec_dai->dev, "Unsupported MCLK value %d\n",
			freq);
		return -EINVAL;
	}

	switch (clk_id) {
	case DA7218_CLKSRC_MCLK_SQR:
		snd_soc_update_bits(codec, DA7218_PLL_CTRL,
				    DA7218_PLL_MCLK_SQR_EN_MASK,
				    DA7218_PLL_MCLK_SQR_EN_MASK);
		break;
	case DA7218_CLKSRC_MCLK:
		snd_soc_update_bits(codec, DA7218_PLL_CTRL,
				    DA7218_PLL_MCLK_SQR_EN_MASK, 0);
		break;
	default:
		dev_err(codec_dai->dev, "Unknown clock source %d\n", clk_id);
		return -EINVAL;
	}

	if (da7218->mclk) {
		freq = clk_round_rate(da7218->mclk, freq);
		ret = clk_set_rate(da7218->mclk, freq);
		if (ret) {
			dev_err(codec_dai->dev, "Failed to set clock rate %d\n",
				freq);
			return ret;
		}
	}

	da7218->mclk_rate = freq;

	return 0;
}

static int da7218_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
			      int source, unsigned int fref, unsigned int fout)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);

	u8 pll_ctrl, indiv_bits, indiv;
	u8 pll_frac_top, pll_frac_bot, pll_integer;
	u32 freq_ref;
	u64 frac_div;

	/* Verify 2MHz - 54MHz MCLK provided, and set input divider */
	if (da7218->mclk_rate < 2000000) {
		dev_err(codec->dev, "PLL input clock %d below valid range\n",
			da7218->mclk_rate);
		return -EINVAL;
	} else if (da7218->mclk_rate <= 4500000) {
		indiv_bits = DA7218_PLL_INDIV_2_TO_4_5_MHZ;
		indiv = DA7218_PLL_INDIV_2_TO_4_5_MHZ_VAL;
	} else if (da7218->mclk_rate <= 9000000) {
		indiv_bits = DA7218_PLL_INDIV_4_5_TO_9_MHZ;
		indiv = DA7218_PLL_INDIV_4_5_TO_9_MHZ_VAL;
	} else if (da7218->mclk_rate <= 18000000) {
		indiv_bits = DA7218_PLL_INDIV_9_TO_18_MHZ;
		indiv = DA7218_PLL_INDIV_9_TO_18_MHZ_VAL;
	} else if (da7218->mclk_rate <= 36000000) {
		indiv_bits = DA7218_PLL_INDIV_18_TO_36_MHZ;
		indiv = DA7218_PLL_INDIV_18_TO_36_MHZ_VAL;
	} else if (da7218->mclk_rate <= 54000000) {
		indiv_bits = DA7218_PLL_INDIV_36_TO_54_MHZ;
		indiv = DA7218_PLL_INDIV_36_TO_54_MHZ_VAL;
	} else {
		dev_err(codec->dev, "PLL input clock %d above valid range\n",
			da7218->mclk_rate);
		return -EINVAL;
	}
	freq_ref = (da7218->mclk_rate / indiv);
	pll_ctrl = indiv_bits;

	/* Configure PLL */
	switch (source) {
	case DA7218_SYSCLK_MCLK:
		pll_ctrl |= DA7218_PLL_MODE_BYPASS;
		snd_soc_update_bits(codec, DA7218_PLL_CTRL,
				    DA7218_PLL_INDIV_MASK |
				    DA7218_PLL_MODE_MASK, pll_ctrl);
		return 0;
	case DA7218_SYSCLK_PLL:
		pll_ctrl |= DA7218_PLL_MODE_NORMAL;
		break;
	case DA7218_SYSCLK_PLL_SRM:
		pll_ctrl |= DA7218_PLL_MODE_SRM;
		break;
	default:
		dev_err(codec->dev, "Invalid PLL config\n");
		return -EINVAL;
	}

	/* Calculate dividers for PLL */
	pll_integer = fout / freq_ref;
	frac_div = (u64)(fout % freq_ref) * 8192ULL;
	do_div(frac_div, freq_ref);
	pll_frac_top = (frac_div >> DA7218_BYTE_SHIFT) & DA7218_BYTE_MASK;
	pll_frac_bot = (frac_div) & DA7218_BYTE_MASK;

	/* Write PLL config & dividers */
	snd_soc_write(codec, DA7218_PLL_FRAC_TOP, pll_frac_top);
	snd_soc_write(codec, DA7218_PLL_FRAC_BOT, pll_frac_bot);
	snd_soc_write(codec, DA7218_PLL_INTEGER, pll_integer);
	snd_soc_update_bits(codec, DA7218_PLL_CTRL,
			    DA7218_PLL_MODE_MASK | DA7218_PLL_INDIV_MASK,
			    pll_ctrl);

	return 0;
}

static int da7218_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	u8 dai_clk_mode = 0, dai_ctrl = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		da7218->master = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		da7218->master = false;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_NB_IF:
			dai_clk_mode |= DA7218_DAI_WCLK_POL_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			dai_clk_mode |= DA7218_DAI_CLK_POL_INV;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			dai_clk_mode |= DA7218_DAI_WCLK_POL_INV |
					DA7218_DAI_CLK_POL_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_DSP_B:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			dai_clk_mode |= DA7218_DAI_CLK_POL_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			dai_clk_mode |= DA7218_DAI_WCLK_POL_INV |
					DA7218_DAI_CLK_POL_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			dai_clk_mode |= DA7218_DAI_WCLK_POL_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dai_ctrl |= DA7218_DAI_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dai_ctrl |= DA7218_DAI_FORMAT_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		dai_ctrl |= DA7218_DAI_FORMAT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		dai_ctrl |= DA7218_DAI_FORMAT_DSP;
		break;
	default:
		return -EINVAL;
	}

	/* By default 64 BCLKs per WCLK is supported */
	dai_clk_mode |= DA7218_DAI_BCLKS_PER_WCLK_64;

	snd_soc_write(codec, DA7218_DAI_CLK_MODE, dai_clk_mode);
	snd_soc_update_bits(codec, DA7218_DAI_CTRL, DA7218_DAI_FORMAT_MASK,
			    dai_ctrl);

	return 0;
}

static int da7218_set_dai_tdm_slot(struct snd_soc_dai *dai,
				   unsigned int tx_mask, unsigned int rx_mask,
				   int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 dai_bclks_per_wclk;
	u32 frame_size;

	/* No channels enabled so disable TDM, revert to 64-bit frames */
	if (!tx_mask) {
		snd_soc_update_bits(codec, DA7218_DAI_TDM_CTRL,
				    DA7218_DAI_TDM_CH_EN_MASK |
				    DA7218_DAI_TDM_MODE_EN_MASK, 0);
		snd_soc_update_bits(codec, DA7218_DAI_CLK_MODE,
				    DA7218_DAI_BCLKS_PER_WCLK_MASK,
				    DA7218_DAI_BCLKS_PER_WCLK_64);
		return 0;
	}

	/* Check we have valid slots */
	if (fls(tx_mask) > DA7218_DAI_TDM_MAX_SLOTS) {
		dev_err(codec->dev, "Invalid number of slots, max = %d\n",
			DA7218_DAI_TDM_MAX_SLOTS);
		return -EINVAL;
	}

	/* Check we have a valid offset given (first 2 bytes of rx_mask) */
	if (rx_mask >> DA7218_2BYTE_SHIFT) {
		dev_err(codec->dev, "Invalid slot offset, max = %d\n",
			DA7218_2BYTE_MASK);
		return -EINVAL;
	}

	/* Calculate & validate frame size based on slot info provided. */
	frame_size = slots * slot_width;
	switch (frame_size) {
	case 32:
		dai_bclks_per_wclk = DA7218_DAI_BCLKS_PER_WCLK_32;
		break;
	case 64:
		dai_bclks_per_wclk = DA7218_DAI_BCLKS_PER_WCLK_64;
		break;
	case 128:
		dai_bclks_per_wclk = DA7218_DAI_BCLKS_PER_WCLK_128;
		break;
	case 256:
		dai_bclks_per_wclk = DA7218_DAI_BCLKS_PER_WCLK_256;
		break;
	default:
		dev_err(codec->dev, "Invalid frame size\n");
		return -EINVAL;
	}

	snd_soc_update_bits(codec, DA7218_DAI_CLK_MODE,
			    DA7218_DAI_BCLKS_PER_WCLK_MASK,
			    dai_bclks_per_wclk);
	snd_soc_write(codec, DA7218_DAI_OFFSET_LOWER,
		      (rx_mask & DA7218_BYTE_MASK));
	snd_soc_write(codec, DA7218_DAI_OFFSET_UPPER,
		      ((rx_mask >> DA7218_BYTE_SHIFT) & DA7218_BYTE_MASK));
	snd_soc_update_bits(codec, DA7218_DAI_TDM_CTRL,
			    DA7218_DAI_TDM_CH_EN_MASK |
			    DA7218_DAI_TDM_MODE_EN_MASK,
			    (tx_mask << DA7218_DAI_TDM_CH_EN_SHIFT) |
			    DA7218_DAI_TDM_MODE_EN_MASK);

	return 0;
}

static int da7218_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 dai_ctrl = 0, fs;
	unsigned int channels;

	switch (params_width(params)) {
	case 16:
		dai_ctrl |= DA7218_DAI_WORD_LENGTH_S16_LE;
		break;
	case 20:
		dai_ctrl |= DA7218_DAI_WORD_LENGTH_S20_LE;
		break;
	case 24:
		dai_ctrl |= DA7218_DAI_WORD_LENGTH_S24_LE;
		break;
	case 32:
		dai_ctrl |= DA7218_DAI_WORD_LENGTH_S32_LE;
		break;
	default:
		return -EINVAL;
	}

	channels = params_channels(params);
	if ((channels < 1) || (channels > DA7218_DAI_CH_NUM_MAX)) {
		dev_err(codec->dev,
			"Invalid number of channels, only 1 to %d supported\n",
			DA7218_DAI_CH_NUM_MAX);
		return -EINVAL;
	}
	dai_ctrl |= channels << DA7218_DAI_CH_NUM_SHIFT;

	switch (params_rate(params)) {
	case 8000:
		fs = DA7218_SR_8000;
		break;
	case 11025:
		fs = DA7218_SR_11025;
		break;
	case 12000:
		fs = DA7218_SR_12000;
		break;
	case 16000:
		fs = DA7218_SR_16000;
		break;
	case 22050:
		fs = DA7218_SR_22050;
		break;
	case 24000:
		fs = DA7218_SR_24000;
		break;
	case 32000:
		fs = DA7218_SR_32000;
		break;
	case 44100:
		fs = DA7218_SR_44100;
		break;
	case 48000:
		fs = DA7218_SR_48000;
		break;
	case 88200:
		fs = DA7218_SR_88200;
		break;
	case 96000:
		fs = DA7218_SR_96000;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, DA7218_DAI_CTRL,
			    DA7218_DAI_WORD_LENGTH_MASK | DA7218_DAI_CH_NUM_MASK,
			    dai_ctrl);
	/* SRs tied for ADCs and DACs. */
	snd_soc_write(codec, DA7218_SR,
		      (fs << DA7218_SR_DAC_SHIFT) | (fs << DA7218_SR_ADC_SHIFT));

	return 0;
}

static const struct snd_soc_dai_ops da7218_dai_ops = {
	.hw_params	= da7218_hw_params,
	.set_sysclk	= da7218_set_dai_sysclk,
	.set_pll	= da7218_set_dai_pll,
	.set_fmt	= da7218_set_dai_fmt,
	.set_tdm_slot	= da7218_set_dai_tdm_slot,
};

#define DA7218_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver da7218_dai = {
	.name = "da7218-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 4,	/* Only 2 channels of data */
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = DA7218_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = DA7218_FORMATS,
	},
	.ops = &da7218_dai_ops,
	.symmetric_rates = 1,
	.symmetric_channels = 1,
	.symmetric_samplebits = 1,
};


/*
 * HP Detect
 */

int da7218_hpldet(struct snd_soc_codec *codec, struct snd_soc_jack *jack)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);

	if (da7218->dev_id == DA7217_DEV_ID)
		return -EINVAL;

	da7218->jack = jack;
	snd_soc_update_bits(codec, DA7218_HPLDET_JACK,
			    DA7218_HPLDET_JACK_EN_MASK,
			    jack ? DA7218_HPLDET_JACK_EN_MASK : 0);

	return 0;
}
EXPORT_SYMBOL_GPL(da7218_hpldet);

static void da7218_micldet_irq(struct snd_soc_codec *codec)
{
	char *envp[] = {
		"EVENT=MIC_LEVEL_DETECT",
		NULL,
	};

	kobject_uevent_env(&codec->dev->kobj, KOBJ_CHANGE, envp);
}

static void da7218_hpldet_irq(struct snd_soc_codec *codec)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	u8 jack_status;
	int report;

	jack_status = snd_soc_read(codec, DA7218_EVENT_STATUS);

	if (jack_status & DA7218_HPLDET_JACK_STS_MASK)
		report = SND_JACK_HEADPHONE;
	else
		report = 0;

	snd_soc_jack_report(da7218->jack, report, SND_JACK_HEADPHONE);
}

/*
 * IRQ
 */

static irqreturn_t da7218_irq_thread(int irq, void *data)
{
	struct snd_soc_codec *codec = data;
	u8 status;

	/* Read IRQ status reg */
	status = snd_soc_read(codec, DA7218_EVENT);
	if (!status)
		return IRQ_NONE;

	/* Mic level detect */
	if (status & DA7218_LVL_DET_EVENT_MASK)
		da7218_micldet_irq(codec);

	/* HP detect */
	if (status & DA7218_HPLDET_JACK_EVENT_MASK)
		da7218_hpldet_irq(codec);

	/* Clear interrupts */
	snd_soc_write(codec, DA7218_EVENT, status);

	return IRQ_HANDLED;
}

/*
 * DT
 */

static const struct of_device_id da7218_of_match[] = {
	{ .compatible = "dlg,da7217", .data = (void *) DA7217_DEV_ID },
	{ .compatible = "dlg,da7218", .data = (void *) DA7218_DEV_ID },
	{ }
};
MODULE_DEVICE_TABLE(of, da7218_of_match);

static inline int da7218_of_get_id(struct device *dev)
{
	const struct of_device_id *id = of_match_device(da7218_of_match, dev);

	if (id)
		return (uintptr_t)id->data;
	else
		return -EINVAL;
}

static enum da7218_micbias_voltage
	da7218_of_micbias_lvl(struct snd_soc_codec *codec, u32 val)
{
	switch (val) {
	case 1200:
		return DA7218_MICBIAS_1_2V;
	case 1600:
		return DA7218_MICBIAS_1_6V;
	case 1800:
		return DA7218_MICBIAS_1_8V;
	case 2000:
		return DA7218_MICBIAS_2_0V;
	case 2200:
		return DA7218_MICBIAS_2_2V;
	case 2400:
		return DA7218_MICBIAS_2_4V;
	case 2600:
		return DA7218_MICBIAS_2_6V;
	case 2800:
		return DA7218_MICBIAS_2_8V;
	case 3000:
		return DA7218_MICBIAS_3_0V;
	default:
		dev_warn(codec->dev, "Invalid micbias level");
		return DA7218_MICBIAS_1_6V;
	}
}

static enum da7218_mic_amp_in_sel
	da7218_of_mic_amp_in_sel(struct snd_soc_codec *codec, const char *str)
{
	if (!strcmp(str, "diff")) {
		return DA7218_MIC_AMP_IN_SEL_DIFF;
	} else if (!strcmp(str, "se_p")) {
		return DA7218_MIC_AMP_IN_SEL_SE_P;
	} else if (!strcmp(str, "se_n")) {
		return DA7218_MIC_AMP_IN_SEL_SE_N;
	} else {
		dev_warn(codec->dev, "Invalid mic input type selection");
		return DA7218_MIC_AMP_IN_SEL_DIFF;
	}
}

static enum da7218_dmic_data_sel
	da7218_of_dmic_data_sel(struct snd_soc_codec *codec, const char *str)
{
	if (!strcmp(str, "lrise_rfall")) {
		return DA7218_DMIC_DATA_LRISE_RFALL;
	} else if (!strcmp(str, "lfall_rrise")) {
		return DA7218_DMIC_DATA_LFALL_RRISE;
	} else {
		dev_warn(codec->dev, "Invalid DMIC data type selection");
		return DA7218_DMIC_DATA_LRISE_RFALL;
	}
}

static enum da7218_dmic_samplephase
	da7218_of_dmic_samplephase(struct snd_soc_codec *codec, const char *str)
{
	if (!strcmp(str, "on_clkedge")) {
		return DA7218_DMIC_SAMPLE_ON_CLKEDGE;
	} else if (!strcmp(str, "between_clkedge")) {
		return DA7218_DMIC_SAMPLE_BETWEEN_CLKEDGE;
	} else {
		dev_warn(codec->dev, "Invalid DMIC sample phase");
		return DA7218_DMIC_SAMPLE_ON_CLKEDGE;
	}
}

static enum da7218_dmic_clk_rate
	da7218_of_dmic_clkrate(struct snd_soc_codec *codec, u32 val)
{
	switch (val) {
	case 1500000:
		return DA7218_DMIC_CLK_1_5MHZ;
	case 3000000:
		return DA7218_DMIC_CLK_3_0MHZ;
	default:
		dev_warn(codec->dev, "Invalid DMIC clock rate");
		return DA7218_DMIC_CLK_3_0MHZ;
	}
}

static enum da7218_hpldet_jack_rate
	da7218_of_jack_rate(struct snd_soc_codec *codec, u32 val)
{
	switch (val) {
	case 5:
		return DA7218_HPLDET_JACK_RATE_5US;
	case 10:
		return DA7218_HPLDET_JACK_RATE_10US;
	case 20:
		return DA7218_HPLDET_JACK_RATE_20US;
	case 40:
		return DA7218_HPLDET_JACK_RATE_40US;
	case 80:
		return DA7218_HPLDET_JACK_RATE_80US;
	case 160:
		return DA7218_HPLDET_JACK_RATE_160US;
	case 320:
		return DA7218_HPLDET_JACK_RATE_320US;
	case 640:
		return DA7218_HPLDET_JACK_RATE_640US;
	default:
		dev_warn(codec->dev, "Invalid jack detect rate");
		return DA7218_HPLDET_JACK_RATE_40US;
	}
}

static enum da7218_hpldet_jack_debounce
	da7218_of_jack_debounce(struct snd_soc_codec *codec, u32 val)
{
	switch (val) {
	case 0:
		return DA7218_HPLDET_JACK_DEBOUNCE_OFF;
	case 2:
		return DA7218_HPLDET_JACK_DEBOUNCE_2;
	case 3:
		return DA7218_HPLDET_JACK_DEBOUNCE_3;
	case 4:
		return DA7218_HPLDET_JACK_DEBOUNCE_4;
	default:
		dev_warn(codec->dev, "Invalid jack debounce");
		return DA7218_HPLDET_JACK_DEBOUNCE_2;
	}
}

static enum da7218_hpldet_jack_thr
	da7218_of_jack_thr(struct snd_soc_codec *codec, u32 val)
{
	switch (val) {
	case 84:
		return DA7218_HPLDET_JACK_THR_84PCT;
	case 88:
		return DA7218_HPLDET_JACK_THR_88PCT;
	case 92:
		return DA7218_HPLDET_JACK_THR_92PCT;
	case 96:
		return DA7218_HPLDET_JACK_THR_96PCT;
	default:
		dev_warn(codec->dev, "Invalid jack threshold level");
		return DA7218_HPLDET_JACK_THR_84PCT;
	}
}

static struct da7218_pdata *da7218_of_to_pdata(struct snd_soc_codec *codec)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	struct device_node *np = codec->dev->of_node;
	struct device_node *hpldet_np;
	struct da7218_pdata *pdata;
	struct da7218_hpldet_pdata *hpldet_pdata;
	const char *of_str;
	u32 of_val32;

	pdata = devm_kzalloc(codec->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_warn(codec->dev, "Failed to allocate memory for pdata\n");
		return NULL;
	}

	if (of_property_read_u32(np, "dlg,micbias1-lvl-millivolt", &of_val32) >= 0)
		pdata->micbias1_lvl = da7218_of_micbias_lvl(codec, of_val32);
	else
		pdata->micbias1_lvl = DA7218_MICBIAS_1_6V;

	if (of_property_read_u32(np, "dlg,micbias2-lvl-millivolt", &of_val32) >= 0)
		pdata->micbias2_lvl = da7218_of_micbias_lvl(codec, of_val32);
	else
		pdata->micbias2_lvl = DA7218_MICBIAS_1_6V;

	if (!of_property_read_string(np, "dlg,mic1-amp-in-sel", &of_str))
		pdata->mic1_amp_in_sel =
			da7218_of_mic_amp_in_sel(codec, of_str);
	else
		pdata->mic1_amp_in_sel = DA7218_MIC_AMP_IN_SEL_DIFF;

	if (!of_property_read_string(np, "dlg,mic2-amp-in-sel", &of_str))
		pdata->mic2_amp_in_sel =
			da7218_of_mic_amp_in_sel(codec, of_str);
	else
		pdata->mic2_amp_in_sel = DA7218_MIC_AMP_IN_SEL_DIFF;

	if (!of_property_read_string(np, "dlg,dmic1-data-sel", &of_str))
		pdata->dmic1_data_sel =	da7218_of_dmic_data_sel(codec, of_str);
	else
		pdata->dmic1_data_sel =	DA7218_DMIC_DATA_LRISE_RFALL;

	if (!of_property_read_string(np, "dlg,dmic1-samplephase", &of_str))
		pdata->dmic1_samplephase =
			da7218_of_dmic_samplephase(codec, of_str);
	else
		pdata->dmic1_samplephase = DA7218_DMIC_SAMPLE_ON_CLKEDGE;

	if (of_property_read_u32(np, "dlg,dmic1-clkrate-hz", &of_val32) >= 0)
		pdata->dmic1_clk_rate = da7218_of_dmic_clkrate(codec, of_val32);
	else
		pdata->dmic1_clk_rate = DA7218_DMIC_CLK_3_0MHZ;

	if (!of_property_read_string(np, "dlg,dmic2-data-sel", &of_str))
		pdata->dmic2_data_sel = da7218_of_dmic_data_sel(codec, of_str);
	else
		pdata->dmic2_data_sel =	DA7218_DMIC_DATA_LRISE_RFALL;

	if (!of_property_read_string(np, "dlg,dmic2-samplephase", &of_str))
		pdata->dmic2_samplephase =
			da7218_of_dmic_samplephase(codec, of_str);
	else
		pdata->dmic2_samplephase = DA7218_DMIC_SAMPLE_ON_CLKEDGE;

	if (of_property_read_u32(np, "dlg,dmic2-clkrate-hz", &of_val32) >= 0)
		pdata->dmic2_clk_rate = da7218_of_dmic_clkrate(codec, of_val32);
	else
		pdata->dmic2_clk_rate = DA7218_DMIC_CLK_3_0MHZ;

	if (da7218->dev_id == DA7217_DEV_ID) {
		if (of_property_read_bool(np, "dlg,hp-diff-single-supply"))
			pdata->hp_diff_single_supply = true;
	}

	if (da7218->dev_id == DA7218_DEV_ID) {
		hpldet_np = of_get_child_by_name(np, "da7218_hpldet");
		if (!hpldet_np)
			return pdata;

		hpldet_pdata = devm_kzalloc(codec->dev, sizeof(*hpldet_pdata),
					    GFP_KERNEL);
		if (!hpldet_pdata) {
			dev_warn(codec->dev,
				 "Failed to allocate memory for hpldet pdata\n");
			of_node_put(hpldet_np);
			return pdata;
		}
		pdata->hpldet_pdata = hpldet_pdata;

		if (of_property_read_u32(hpldet_np, "dlg,jack-rate-us",
					 &of_val32) >= 0)
			hpldet_pdata->jack_rate =
				da7218_of_jack_rate(codec, of_val32);
		else
			hpldet_pdata->jack_rate = DA7218_HPLDET_JACK_RATE_40US;

		if (of_property_read_u32(hpldet_np, "dlg,jack-debounce",
					 &of_val32) >= 0)
			hpldet_pdata->jack_debounce =
				da7218_of_jack_debounce(codec, of_val32);
		else
			hpldet_pdata->jack_debounce =
				DA7218_HPLDET_JACK_DEBOUNCE_2;

		if (of_property_read_u32(hpldet_np, "dlg,jack-threshold-pct",
					 &of_val32) >= 0)
			hpldet_pdata->jack_thr =
				da7218_of_jack_thr(codec, of_val32);
		else
			hpldet_pdata->jack_thr = DA7218_HPLDET_JACK_THR_84PCT;

		if (of_property_read_bool(hpldet_np, "dlg,comp-inv"))
			hpldet_pdata->comp_inv = true;

		if (of_property_read_bool(hpldet_np, "dlg,hyst"))
			hpldet_pdata->hyst = true;

		if (of_property_read_bool(hpldet_np, "dlg,discharge"))
			hpldet_pdata->discharge = true;

		of_node_put(hpldet_np);
	}

	return pdata;
}


/*
 * Codec driver functions
 */

static int da7218_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		/* Enable MCLK for transition to ON state */
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_STANDBY) {
			if (da7218->mclk) {
				ret = clk_prepare_enable(da7218->mclk);
				if (ret) {
					dev_err(codec->dev, "Failed to enable mclk\n");
					return ret;
				}
			}
		}

		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			/* Master bias */
			snd_soc_update_bits(codec, DA7218_REFERENCES,
					    DA7218_BIAS_EN_MASK,
					    DA7218_BIAS_EN_MASK);

			/* Internal LDO */
			snd_soc_update_bits(codec, DA7218_LDO_CTRL,
					    DA7218_LDO_EN_MASK,
					    DA7218_LDO_EN_MASK);
		} else {
			/* Remove MCLK */
			if (da7218->mclk)
				clk_disable_unprepare(da7218->mclk);
		}
		break;
	case SND_SOC_BIAS_OFF:
		/* Only disable if jack detection disabled */
		if (!da7218->jack) {
			/* Internal LDO */
			snd_soc_update_bits(codec, DA7218_LDO_CTRL,
					    DA7218_LDO_EN_MASK, 0);

			/* Master bias */
			snd_soc_update_bits(codec, DA7218_REFERENCES,
					    DA7218_BIAS_EN_MASK, 0);
		}
		break;
	}

	return 0;
}

static const char *da7218_supply_names[DA7218_NUM_SUPPLIES] = {
	[DA7218_SUPPLY_VDD] = "VDD",
	[DA7218_SUPPLY_VDDMIC] = "VDDMIC",
	[DA7218_SUPPLY_VDDIO] = "VDDIO",
};

static int da7218_handle_supplies(struct snd_soc_codec *codec)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	struct regulator *vddio;
	u8 io_voltage_lvl = DA7218_IO_VOLTAGE_LEVEL_2_5V_3_6V;
	int i, ret;

	/* Get required supplies */
	for (i = 0; i < DA7218_NUM_SUPPLIES; ++i)
		da7218->supplies[i].supply = da7218_supply_names[i];

	ret = devm_regulator_bulk_get(codec->dev, DA7218_NUM_SUPPLIES,
				      da7218->supplies);
	if (ret) {
		dev_err(codec->dev, "Failed to get supplies\n");
		return ret;
	}

	/* Determine VDDIO voltage provided */
	vddio = da7218->supplies[DA7218_SUPPLY_VDDIO].consumer;
	ret = regulator_get_voltage(vddio);
	if (ret < 1500000)
		dev_warn(codec->dev, "Invalid VDDIO voltage\n");
	else if (ret < 2500000)
		io_voltage_lvl = DA7218_IO_VOLTAGE_LEVEL_1_5V_2_5V;

	/* Enable main supplies */
	ret = regulator_bulk_enable(DA7218_NUM_SUPPLIES, da7218->supplies);
	if (ret) {
		dev_err(codec->dev, "Failed to enable supplies\n");
		return ret;
	}

	/* Ensure device in active mode */
	snd_soc_write(codec, DA7218_SYSTEM_ACTIVE, DA7218_SYSTEM_ACTIVE_MASK);

	/* Update IO voltage level range */
	snd_soc_write(codec, DA7218_IO_CTRL, io_voltage_lvl);

	return 0;
}

static void da7218_handle_pdata(struct snd_soc_codec *codec)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	struct da7218_pdata *pdata = da7218->pdata;

	if (pdata) {
		u8 micbias_lvl = 0, dmic_cfg = 0;

		/* Mic Bias voltages */
		switch (pdata->micbias1_lvl) {
		case DA7218_MICBIAS_1_2V:
			micbias_lvl |= DA7218_MICBIAS_1_LP_MODE_MASK;
			break;
		case DA7218_MICBIAS_1_6V:
		case DA7218_MICBIAS_1_8V:
		case DA7218_MICBIAS_2_0V:
		case DA7218_MICBIAS_2_2V:
		case DA7218_MICBIAS_2_4V:
		case DA7218_MICBIAS_2_6V:
		case DA7218_MICBIAS_2_8V:
		case DA7218_MICBIAS_3_0V:
			micbias_lvl |= (pdata->micbias1_lvl <<
					DA7218_MICBIAS_1_LEVEL_SHIFT);
			break;
		}

		switch (pdata->micbias2_lvl) {
		case DA7218_MICBIAS_1_2V:
			micbias_lvl |= DA7218_MICBIAS_2_LP_MODE_MASK;
			break;
		case DA7218_MICBIAS_1_6V:
		case DA7218_MICBIAS_1_8V:
		case DA7218_MICBIAS_2_0V:
		case DA7218_MICBIAS_2_2V:
		case DA7218_MICBIAS_2_4V:
		case DA7218_MICBIAS_2_6V:
		case DA7218_MICBIAS_2_8V:
		case DA7218_MICBIAS_3_0V:
			micbias_lvl |= (pdata->micbias2_lvl <<
					 DA7218_MICBIAS_2_LEVEL_SHIFT);
			break;
		}

		snd_soc_write(codec, DA7218_MICBIAS_CTRL, micbias_lvl);

		/* Mic */
		switch (pdata->mic1_amp_in_sel) {
		case DA7218_MIC_AMP_IN_SEL_DIFF:
		case DA7218_MIC_AMP_IN_SEL_SE_P:
		case DA7218_MIC_AMP_IN_SEL_SE_N:
			snd_soc_write(codec, DA7218_MIC_1_SELECT,
				      pdata->mic1_amp_in_sel);
			break;
		}

		switch (pdata->mic2_amp_in_sel) {
		case DA7218_MIC_AMP_IN_SEL_DIFF:
		case DA7218_MIC_AMP_IN_SEL_SE_P:
		case DA7218_MIC_AMP_IN_SEL_SE_N:
			snd_soc_write(codec, DA7218_MIC_2_SELECT,
				      pdata->mic2_amp_in_sel);
			break;
		}

		/* DMic */
		switch (pdata->dmic1_data_sel) {
		case DA7218_DMIC_DATA_LFALL_RRISE:
		case DA7218_DMIC_DATA_LRISE_RFALL:
			dmic_cfg |= (pdata->dmic1_data_sel <<
				     DA7218_DMIC_1_DATA_SEL_SHIFT);
			break;
		}

		switch (pdata->dmic1_samplephase) {
		case DA7218_DMIC_SAMPLE_ON_CLKEDGE:
		case DA7218_DMIC_SAMPLE_BETWEEN_CLKEDGE:
			dmic_cfg |= (pdata->dmic1_samplephase <<
				     DA7218_DMIC_1_SAMPLEPHASE_SHIFT);
			break;
		}

		switch (pdata->dmic1_clk_rate) {
		case DA7218_DMIC_CLK_3_0MHZ:
		case DA7218_DMIC_CLK_1_5MHZ:
			dmic_cfg |= (pdata->dmic1_clk_rate <<
				     DA7218_DMIC_1_CLK_RATE_SHIFT);
			break;
		}

		snd_soc_update_bits(codec, DA7218_DMIC_1_CTRL,
				    DA7218_DMIC_1_DATA_SEL_MASK |
				    DA7218_DMIC_1_SAMPLEPHASE_MASK |
				    DA7218_DMIC_1_CLK_RATE_MASK, dmic_cfg);

		dmic_cfg = 0;
		switch (pdata->dmic2_data_sel) {
		case DA7218_DMIC_DATA_LFALL_RRISE:
		case DA7218_DMIC_DATA_LRISE_RFALL:
			dmic_cfg |= (pdata->dmic2_data_sel <<
				     DA7218_DMIC_2_DATA_SEL_SHIFT);
			break;
		}

		switch (pdata->dmic2_samplephase) {
		case DA7218_DMIC_SAMPLE_ON_CLKEDGE:
		case DA7218_DMIC_SAMPLE_BETWEEN_CLKEDGE:
			dmic_cfg |= (pdata->dmic2_samplephase <<
				     DA7218_DMIC_2_SAMPLEPHASE_SHIFT);
			break;
		}

		switch (pdata->dmic2_clk_rate) {
		case DA7218_DMIC_CLK_3_0MHZ:
		case DA7218_DMIC_CLK_1_5MHZ:
			dmic_cfg |= (pdata->dmic2_clk_rate <<
				     DA7218_DMIC_2_CLK_RATE_SHIFT);
			break;
		}

		snd_soc_update_bits(codec, DA7218_DMIC_2_CTRL,
				    DA7218_DMIC_2_DATA_SEL_MASK |
				    DA7218_DMIC_2_SAMPLEPHASE_MASK |
				    DA7218_DMIC_2_CLK_RATE_MASK, dmic_cfg);

		/* DA7217 Specific */
		if (da7218->dev_id == DA7217_DEV_ID) {
			da7218->hp_single_supply =
				pdata->hp_diff_single_supply;

			if (da7218->hp_single_supply) {
				snd_soc_write(codec, DA7218_HP_DIFF_UNLOCK,
					      DA7218_HP_DIFF_UNLOCK_VAL);
				snd_soc_update_bits(codec, DA7218_HP_DIFF_CTRL,
						    DA7218_HP_AMP_SINGLE_SUPPLY_EN_MASK,
						    DA7218_HP_AMP_SINGLE_SUPPLY_EN_MASK);
			}
		}

		/* DA7218 Specific */
		if ((da7218->dev_id == DA7218_DEV_ID) &&
		    (pdata->hpldet_pdata)) {
			struct da7218_hpldet_pdata *hpldet_pdata =
				pdata->hpldet_pdata;
			u8 hpldet_cfg = 0;

			switch (hpldet_pdata->jack_rate) {
			case DA7218_HPLDET_JACK_RATE_5US:
			case DA7218_HPLDET_JACK_RATE_10US:
			case DA7218_HPLDET_JACK_RATE_20US:
			case DA7218_HPLDET_JACK_RATE_40US:
			case DA7218_HPLDET_JACK_RATE_80US:
			case DA7218_HPLDET_JACK_RATE_160US:
			case DA7218_HPLDET_JACK_RATE_320US:
			case DA7218_HPLDET_JACK_RATE_640US:
				hpldet_cfg |=
					(hpldet_pdata->jack_rate <<
					 DA7218_HPLDET_JACK_RATE_SHIFT);
				break;
			}

			switch (hpldet_pdata->jack_debounce) {
			case DA7218_HPLDET_JACK_DEBOUNCE_OFF:
			case DA7218_HPLDET_JACK_DEBOUNCE_2:
			case DA7218_HPLDET_JACK_DEBOUNCE_3:
			case DA7218_HPLDET_JACK_DEBOUNCE_4:
				hpldet_cfg |=
					(hpldet_pdata->jack_debounce <<
					 DA7218_HPLDET_JACK_DEBOUNCE_SHIFT);
				break;
			}

			switch (hpldet_pdata->jack_thr) {
			case DA7218_HPLDET_JACK_THR_84PCT:
			case DA7218_HPLDET_JACK_THR_88PCT:
			case DA7218_HPLDET_JACK_THR_92PCT:
			case DA7218_HPLDET_JACK_THR_96PCT:
				hpldet_cfg |=
					(hpldet_pdata->jack_thr <<
					 DA7218_HPLDET_JACK_THR_SHIFT);
				break;
			}
			snd_soc_update_bits(codec, DA7218_HPLDET_JACK,
					    DA7218_HPLDET_JACK_RATE_MASK |
					    DA7218_HPLDET_JACK_DEBOUNCE_MASK |
					    DA7218_HPLDET_JACK_THR_MASK,
					    hpldet_cfg);

			hpldet_cfg = 0;
			if (hpldet_pdata->comp_inv)
				hpldet_cfg |= DA7218_HPLDET_COMP_INV_MASK;

			if (hpldet_pdata->hyst)
				hpldet_cfg |= DA7218_HPLDET_HYST_EN_MASK;

			if (hpldet_pdata->discharge)
				hpldet_cfg |= DA7218_HPLDET_DISCHARGE_EN_MASK;

			snd_soc_write(codec, DA7218_HPLDET_CTRL, hpldet_cfg);
		}
	}
}

static int da7218_probe(struct snd_soc_codec *codec)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);
	int ret;

	/* Regulator configuration */
	ret = da7218_handle_supplies(codec);
	if (ret)
		return ret;

	/* Handle DT/Platform data */
	if (codec->dev->of_node)
		da7218->pdata = da7218_of_to_pdata(codec);
	else
		da7218->pdata = dev_get_platdata(codec->dev);

	da7218_handle_pdata(codec);

	/* Check if MCLK provided, if not the clock is NULL */
	da7218->mclk = devm_clk_get(codec->dev, "mclk");
	if (IS_ERR(da7218->mclk)) {
		if (PTR_ERR(da7218->mclk) != -ENOENT) {
			ret = PTR_ERR(da7218->mclk);
			goto err_disable_reg;
		} else {
			da7218->mclk = NULL;
		}
	}

	/* Default PC to free-running */
	snd_soc_write(codec, DA7218_PC_COUNT, DA7218_PC_FREERUN_MASK);

	/*
	 * Default Output Filter mixers to off otherwise DAPM will power
	 * Mic to HP passthrough paths by default at startup.
	 */
	snd_soc_write(codec, DA7218_DROUTING_OUTFILT_1L, 0);
	snd_soc_write(codec, DA7218_DROUTING_OUTFILT_1R, 0);

	/* Default CP to normal load, power mode */
	snd_soc_update_bits(codec, DA7218_CP_CTRL,
			    DA7218_CP_SMALL_SWITCH_FREQ_EN_MASK, 0);

	/* Default gain ramping */
	snd_soc_update_bits(codec, DA7218_MIXIN_1_CTRL,
			    DA7218_MIXIN_1_AMP_RAMP_EN_MASK,
			    DA7218_MIXIN_1_AMP_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_MIXIN_2_CTRL,
			    DA7218_MIXIN_2_AMP_RAMP_EN_MASK,
			    DA7218_MIXIN_2_AMP_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_IN_1L_FILTER_CTRL,
			    DA7218_IN_1L_RAMP_EN_MASK,
			    DA7218_IN_1L_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_IN_1R_FILTER_CTRL,
			    DA7218_IN_1R_RAMP_EN_MASK,
			    DA7218_IN_1R_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_IN_2L_FILTER_CTRL,
			    DA7218_IN_2L_RAMP_EN_MASK,
			    DA7218_IN_2L_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_IN_2R_FILTER_CTRL,
			    DA7218_IN_2R_RAMP_EN_MASK,
			    DA7218_IN_2R_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_DGS_GAIN_CTRL,
			    DA7218_DGS_RAMP_EN_MASK, DA7218_DGS_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_OUT_1L_FILTER_CTRL,
			    DA7218_OUT_1L_RAMP_EN_MASK,
			    DA7218_OUT_1L_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_OUT_1R_FILTER_CTRL,
			    DA7218_OUT_1R_RAMP_EN_MASK,
			    DA7218_OUT_1R_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_HP_L_CTRL,
			    DA7218_HP_L_AMP_RAMP_EN_MASK,
			    DA7218_HP_L_AMP_RAMP_EN_MASK);
	snd_soc_update_bits(codec, DA7218_HP_R_CTRL,
			    DA7218_HP_R_AMP_RAMP_EN_MASK,
			    DA7218_HP_R_AMP_RAMP_EN_MASK);

	/* Default infinite tone gen, start/stop by Kcontrol */
	snd_soc_write(codec, DA7218_TONE_GEN_CYCLES, DA7218_BEEP_CYCLES_MASK);

	/* DA7217 specific config */
	if (da7218->dev_id == DA7217_DEV_ID) {
		snd_soc_update_bits(codec, DA7218_HP_DIFF_CTRL,
				    DA7218_HP_AMP_DIFF_MODE_EN_MASK,
				    DA7218_HP_AMP_DIFF_MODE_EN_MASK);

		/* Only DA7218 supports HP detect, mask off for DA7217 */
		snd_soc_write(codec, DA7218_EVENT_MASK,
			      DA7218_HPLDET_JACK_EVENT_IRQ_MSK_MASK);
	}

	if (da7218->irq) {
		ret = devm_request_threaded_irq(codec->dev, da7218->irq, NULL,
						da7218_irq_thread,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						"da7218", codec);
		if (ret != 0) {
			dev_err(codec->dev, "Failed to request IRQ %d: %d\n",
				da7218->irq, ret);
			goto err_disable_reg;
		}

	}

	return 0;

err_disable_reg:
	regulator_bulk_disable(DA7218_NUM_SUPPLIES, da7218->supplies);

	return ret;
}

static int da7218_remove(struct snd_soc_codec *codec)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);

	regulator_bulk_disable(DA7218_NUM_SUPPLIES, da7218->supplies);

	return 0;
}

#ifdef CONFIG_PM
static int da7218_suspend(struct snd_soc_codec *codec)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);

	da7218_set_bias_level(codec, SND_SOC_BIAS_OFF);

	/* Put device into standby mode if jack detection disabled */
	if (!da7218->jack)
		snd_soc_write(codec, DA7218_SYSTEM_ACTIVE, 0);

	return 0;
}

static int da7218_resume(struct snd_soc_codec *codec)
{
	struct da7218_priv *da7218 = snd_soc_codec_get_drvdata(codec);

	/* Put device into active mode if previously moved to standby */
	if (!da7218->jack)
		snd_soc_write(codec, DA7218_SYSTEM_ACTIVE,
			      DA7218_SYSTEM_ACTIVE_MASK);

	da7218_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}
#else
#define da7218_suspend NULL
#define da7218_resume NULL
#endif

static const struct snd_soc_codec_driver soc_codec_dev_da7218 = {
	.probe			= da7218_probe,
	.remove			= da7218_remove,
	.suspend		= da7218_suspend,
	.resume			= da7218_resume,
	.set_bias_level		= da7218_set_bias_level,

	.component_driver = {
		.controls		= da7218_snd_controls,
		.num_controls		= ARRAY_SIZE(da7218_snd_controls),
		.dapm_widgets		= da7218_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(da7218_dapm_widgets),
		.dapm_routes		= da7218_audio_map,
		.num_dapm_routes	= ARRAY_SIZE(da7218_audio_map),
	},
};


/*
 * Regmap configs
 */

static struct reg_default da7218_reg_defaults[] = {
	{ DA7218_SYSTEM_ACTIVE, 0x00 },
	{ DA7218_CIF_CTRL, 0x00 },
	{ DA7218_SPARE1, 0x00 },
	{ DA7218_SR, 0xAA },
	{ DA7218_PC_COUNT, 0x02 },
	{ DA7218_GAIN_RAMP_CTRL, 0x00 },
	{ DA7218_CIF_TIMEOUT_CTRL, 0x01 },
	{ DA7218_SYSTEM_MODES_INPUT, 0x00 },
	{ DA7218_SYSTEM_MODES_OUTPUT, 0x00 },
	{ DA7218_IN_1L_FILTER_CTRL, 0x00 },
	{ DA7218_IN_1R_FILTER_CTRL, 0x00 },
	{ DA7218_IN_2L_FILTER_CTRL, 0x00 },
	{ DA7218_IN_2R_FILTER_CTRL, 0x00 },
	{ DA7218_OUT_1L_FILTER_CTRL, 0x40 },
	{ DA7218_OUT_1R_FILTER_CTRL, 0x40 },
	{ DA7218_OUT_1_HPF_FILTER_CTRL, 0x80 },
	{ DA7218_OUT_1_EQ_12_FILTER_CTRL, 0x77 },
	{ DA7218_OUT_1_EQ_34_FILTER_CTRL, 0x77 },
	{ DA7218_OUT_1_EQ_5_FILTER_CTRL, 0x07 },
	{ DA7218_OUT_1_BIQ_5STAGE_CTRL, 0x40 },
	{ DA7218_OUT_1_BIQ_5STAGE_DATA, 0x00 },
	{ DA7218_OUT_1_BIQ_5STAGE_ADDR, 0x00 },
	{ DA7218_MIXIN_1_CTRL, 0x48 },
	{ DA7218_MIXIN_1_GAIN, 0x03 },
	{ DA7218_MIXIN_2_CTRL, 0x48 },
	{ DA7218_MIXIN_2_GAIN, 0x03 },
	{ DA7218_ALC_CTRL1, 0x00 },
	{ DA7218_ALC_CTRL2, 0x00 },
	{ DA7218_ALC_CTRL3, 0x00 },
	{ DA7218_ALC_NOISE, 0x3F },
	{ DA7218_ALC_TARGET_MIN, 0x3F },
	{ DA7218_ALC_TARGET_MAX, 0x00 },
	{ DA7218_ALC_GAIN_LIMITS, 0xFF },
	{ DA7218_ALC_ANA_GAIN_LIMITS, 0x71 },
	{ DA7218_ALC_ANTICLIP_CTRL, 0x00 },
	{ DA7218_AGS_ENABLE, 0x00 },
	{ DA7218_AGS_TRIGGER, 0x09 },
	{ DA7218_AGS_ATT_MAX, 0x00 },
	{ DA7218_AGS_TIMEOUT, 0x00 },
	{ DA7218_AGS_ANTICLIP_CTRL, 0x00 },
	{ DA7218_ENV_TRACK_CTRL, 0x00 },
	{ DA7218_LVL_DET_CTRL, 0x00 },
	{ DA7218_LVL_DET_LEVEL, 0x7F },
	{ DA7218_DGS_TRIGGER, 0x24 },
	{ DA7218_DGS_ENABLE, 0x00 },
	{ DA7218_DGS_RISE_FALL, 0x50 },
	{ DA7218_DGS_SYNC_DELAY, 0xA3 },
	{ DA7218_DGS_SYNC_DELAY2, 0x31 },
	{ DA7218_DGS_SYNC_DELAY3, 0x11 },
	{ DA7218_DGS_LEVELS, 0x01 },
	{ DA7218_DGS_GAIN_CTRL, 0x74 },
	{ DA7218_DROUTING_OUTDAI_1L, 0x01 },
	{ DA7218_DMIX_OUTDAI_1L_INFILT_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1L_INFILT_1R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1L_INFILT_2L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1L_INFILT_2R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1L_TONEGEN_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1L_INDAI_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1L_INDAI_1R_GAIN, 0x1C },
	{ DA7218_DROUTING_OUTDAI_1R, 0x04 },
	{ DA7218_DMIX_OUTDAI_1R_INFILT_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1R_INFILT_1R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1R_INFILT_2L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1R_INFILT_2R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1R_TONEGEN_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1R_INDAI_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_1R_INDAI_1R_GAIN, 0x1C },
	{ DA7218_DROUTING_OUTFILT_1L, 0x01 },
	{ DA7218_DMIX_OUTFILT_1L_INFILT_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1L_INFILT_1R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1L_INFILT_2L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1L_INFILT_2R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1L_TONEGEN_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1L_INDAI_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1L_INDAI_1R_GAIN, 0x1C },
	{ DA7218_DROUTING_OUTFILT_1R, 0x04 },
	{ DA7218_DMIX_OUTFILT_1R_INFILT_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1R_INFILT_1R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1R_INFILT_2L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1R_INFILT_2R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1R_TONEGEN_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1R_INDAI_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTFILT_1R_INDAI_1R_GAIN, 0x1C },
	{ DA7218_DROUTING_OUTDAI_2L, 0x04 },
	{ DA7218_DMIX_OUTDAI_2L_INFILT_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2L_INFILT_1R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2L_INFILT_2L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2L_INFILT_2R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2L_TONEGEN_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2L_INDAI_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2L_INDAI_1R_GAIN, 0x1C },
	{ DA7218_DROUTING_OUTDAI_2R, 0x08 },
	{ DA7218_DMIX_OUTDAI_2R_INFILT_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2R_INFILT_1R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2R_INFILT_2L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2R_INFILT_2R_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2R_TONEGEN_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2R_INDAI_1L_GAIN, 0x1C },
	{ DA7218_DMIX_OUTDAI_2R_INDAI_1R_GAIN, 0x1C },
	{ DA7218_DAI_CTRL, 0x28 },
	{ DA7218_DAI_TDM_CTRL, 0x40 },
	{ DA7218_DAI_OFFSET_LOWER, 0x00 },
	{ DA7218_DAI_OFFSET_UPPER, 0x00 },
	{ DA7218_DAI_CLK_MODE, 0x01 },
	{ DA7218_PLL_CTRL, 0x04 },
	{ DA7218_PLL_FRAC_TOP, 0x00 },
	{ DA7218_PLL_FRAC_BOT, 0x00 },
	{ DA7218_PLL_INTEGER, 0x20 },
	{ DA7218_DAC_NG_CTRL, 0x00 },
	{ DA7218_DAC_NG_SETUP_TIME, 0x00 },
	{ DA7218_DAC_NG_OFF_THRESH, 0x00 },
	{ DA7218_DAC_NG_ON_THRESH, 0x00 },
	{ DA7218_TONE_GEN_CFG2, 0x00 },
	{ DA7218_TONE_GEN_FREQ1_L, 0x55 },
	{ DA7218_TONE_GEN_FREQ1_U, 0x15 },
	{ DA7218_TONE_GEN_FREQ2_L, 0x00 },
	{ DA7218_TONE_GEN_FREQ2_U, 0x40 },
	{ DA7218_TONE_GEN_CYCLES, 0x00 },
	{ DA7218_TONE_GEN_ON_PER, 0x02 },
	{ DA7218_TONE_GEN_OFF_PER, 0x01 },
	{ DA7218_CP_CTRL, 0x60 },
	{ DA7218_CP_DELAY, 0x11 },
	{ DA7218_CP_VOL_THRESHOLD1, 0x0E },
	{ DA7218_MIC_1_CTRL, 0x40 },
	{ DA7218_MIC_1_GAIN, 0x01 },
	{ DA7218_MIC_1_SELECT, 0x00 },
	{ DA7218_MIC_2_CTRL, 0x40 },
	{ DA7218_MIC_2_GAIN, 0x01 },
	{ DA7218_MIC_2_SELECT, 0x00 },
	{ DA7218_IN_1_HPF_FILTER_CTRL, 0x80 },
	{ DA7218_IN_2_HPF_FILTER_CTRL, 0x80 },
	{ DA7218_ADC_1_CTRL, 0x07 },
	{ DA7218_ADC_2_CTRL, 0x07 },
	{ DA7218_MIXOUT_L_CTRL, 0x00 },
	{ DA7218_MIXOUT_L_GAIN, 0x03 },
	{ DA7218_MIXOUT_R_CTRL, 0x00 },
	{ DA7218_MIXOUT_R_GAIN, 0x03 },
	{ DA7218_HP_L_CTRL, 0x40 },
	{ DA7218_HP_L_GAIN, 0x3B },
	{ DA7218_HP_R_CTRL, 0x40 },
	{ DA7218_HP_R_GAIN, 0x3B },
	{ DA7218_HP_DIFF_CTRL, 0x00 },
	{ DA7218_HP_DIFF_UNLOCK, 0xC3 },
	{ DA7218_HPLDET_JACK, 0x0B },
	{ DA7218_HPLDET_CTRL, 0x00 },
	{ DA7218_REFERENCES, 0x08 },
	{ DA7218_IO_CTRL, 0x00 },
	{ DA7218_LDO_CTRL, 0x00 },
	{ DA7218_SIDETONE_CTRL, 0x40 },
	{ DA7218_SIDETONE_IN_SELECT, 0x00 },
	{ DA7218_SIDETONE_GAIN, 0x1C },
	{ DA7218_DROUTING_ST_OUTFILT_1L, 0x01 },
	{ DA7218_DROUTING_ST_OUTFILT_1R, 0x02 },
	{ DA7218_SIDETONE_BIQ_3STAGE_DATA, 0x00 },
	{ DA7218_SIDETONE_BIQ_3STAGE_ADDR, 0x00 },
	{ DA7218_EVENT_MASK, 0x00 },
	{ DA7218_DMIC_1_CTRL, 0x00 },
	{ DA7218_DMIC_2_CTRL, 0x00 },
	{ DA7218_IN_1L_GAIN, 0x6F },
	{ DA7218_IN_1R_GAIN, 0x6F },
	{ DA7218_IN_2L_GAIN, 0x6F },
	{ DA7218_IN_2R_GAIN, 0x6F },
	{ DA7218_OUT_1L_GAIN, 0x6F },
	{ DA7218_OUT_1R_GAIN, 0x6F },
	{ DA7218_MICBIAS_CTRL, 0x00 },
	{ DA7218_MICBIAS_EN, 0x00 },
};

static bool da7218_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA7218_STATUS1:
	case DA7218_SOFT_RESET:
	case DA7218_SYSTEM_STATUS:
	case DA7218_CALIB_CTRL:
	case DA7218_CALIB_OFFSET_AUTO_M_1:
	case DA7218_CALIB_OFFSET_AUTO_U_1:
	case DA7218_CALIB_OFFSET_AUTO_M_2:
	case DA7218_CALIB_OFFSET_AUTO_U_2:
	case DA7218_PLL_STATUS:
	case DA7218_PLL_REFOSC_CAL:
	case DA7218_TONE_GEN_CFG1:
	case DA7218_ADC_MODE:
	case DA7218_HP_SNGL_CTRL:
	case DA7218_HPLDET_TEST:
	case DA7218_EVENT_STATUS:
	case DA7218_EVENT:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config da7218_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = DA7218_MICBIAS_EN,
	.reg_defaults = da7218_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(da7218_reg_defaults),
	.volatile_reg = da7218_volatile_register,
	.cache_type = REGCACHE_RBTREE,
};


/*
 * I2C layer
 */

static int da7218_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct da7218_priv *da7218;
	int ret;

	da7218 = devm_kzalloc(&i2c->dev, sizeof(struct da7218_priv),
			      GFP_KERNEL);
	if (!da7218)
		return -ENOMEM;

	i2c_set_clientdata(i2c, da7218);

	if (i2c->dev.of_node)
		da7218->dev_id = da7218_of_get_id(&i2c->dev);
	else
		da7218->dev_id = id->driver_data;

	if ((da7218->dev_id != DA7217_DEV_ID) &&
	    (da7218->dev_id != DA7218_DEV_ID)) {
		dev_err(&i2c->dev, "Invalid device Id\n");
		return -EINVAL;
	}

	da7218->irq = i2c->irq;

	da7218->regmap = devm_regmap_init_i2c(i2c, &da7218_regmap_config);
	if (IS_ERR(da7218->regmap)) {
		ret = PTR_ERR(da7218->regmap);
		dev_err(&i2c->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_da7218, &da7218_dai, 1);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to register da7218 codec: %d\n",
			ret);
	}
	return ret;
}

static int da7218_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id da7218_i2c_id[] = {
	{ "da7217", DA7217_DEV_ID },
	{ "da7218", DA7218_DEV_ID },
	{ }
};
MODULE_DEVICE_TABLE(i2c, da7218_i2c_id);

static struct i2c_driver da7218_i2c_driver = {
	.driver = {
		.name = "da7218",
		.of_match_table = of_match_ptr(da7218_of_match),
	},
	.probe		= da7218_i2c_probe,
	.remove		= da7218_i2c_remove,
	.id_table	= da7218_i2c_id,
};

module_i2c_driver(da7218_i2c_driver);

MODULE_DESCRIPTION("ASoC DA7218 Codec driver");
MODULE_AUTHOR("Adam Thomson <Adam.Thomson.Opensource@diasemi.com>");
MODULE_LICENSE("GPL");
