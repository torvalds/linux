/*
 * wm8350.c -- WM8350 ALSA SoC audio driver
 *
 * Copyright (C) 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/mfd/wm8350/audio.h>
#include <linux/mfd/wm8350/core.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <trace/events/asoc.h>

#include "wm8350.h"

#define WM8350_OUTn_0dB 0x39

#define WM8350_RAMP_NONE	0
#define WM8350_RAMP_UP		1
#define WM8350_RAMP_DOWN	2

/* We only include the analogue supplies here; the digital supplies
 * need to be available well before this driver can be probed.
 */
static const char *supply_names[] = {
	"AVDD",
	"HPVDD",
};

struct wm8350_output {
	u16 active;
	u16 left_vol;
	u16 right_vol;
	u16 ramp;
	u16 mute;
};

struct wm8350_jack_data {
	struct snd_soc_jack *jack;
	struct delayed_work work;
	int report;
	int short_report;
};

struct wm8350_data {
	struct snd_soc_codec codec;
	struct wm8350_output out1;
	struct wm8350_output out2;
	struct wm8350_jack_data hpl;
	struct wm8350_jack_data hpr;
	struct wm8350_jack_data mic;
	struct regulator_bulk_data supplies[ARRAY_SIZE(supply_names)];
	int fll_freq_out;
	int fll_freq_in;
};

static unsigned int wm8350_codec_cache_read(struct snd_soc_codec *codec,
					    unsigned int reg)
{
	struct wm8350 *wm8350 = codec->control_data;
	return wm8350->reg_cache[reg];
}

static unsigned int wm8350_codec_read(struct snd_soc_codec *codec,
				      unsigned int reg)
{
	struct wm8350 *wm8350 = codec->control_data;
	return wm8350_reg_read(wm8350, reg);
}

static int wm8350_codec_write(struct snd_soc_codec *codec, unsigned int reg,
			      unsigned int value)
{
	struct wm8350 *wm8350 = codec->control_data;
	return wm8350_reg_write(wm8350, reg, value);
}

/*
 * Ramp OUT1 PGA volume to minimise pops at stream startup and shutdown.
 */
static inline int wm8350_out1_ramp_step(struct snd_soc_codec *codec)
{
	struct wm8350_data *wm8350_data = snd_soc_codec_get_drvdata(codec);
	struct wm8350_output *out1 = &wm8350_data->out1;
	struct wm8350 *wm8350 = codec->control_data;
	int left_complete = 0, right_complete = 0;
	u16 reg, val;

	/* left channel */
	reg = wm8350_reg_read(wm8350, WM8350_LOUT1_VOLUME);
	val = (reg & WM8350_OUT1L_VOL_MASK) >> WM8350_OUT1L_VOL_SHIFT;

	if (out1->ramp == WM8350_RAMP_UP) {
		/* ramp step up */
		if (val < out1->left_vol) {
			val++;
			reg &= ~WM8350_OUT1L_VOL_MASK;
			wm8350_reg_write(wm8350, WM8350_LOUT1_VOLUME,
					 reg | (val << WM8350_OUT1L_VOL_SHIFT));
		} else
			left_complete = 1;
	} else if (out1->ramp == WM8350_RAMP_DOWN) {
		/* ramp step down */
		if (val > 0) {
			val--;
			reg &= ~WM8350_OUT1L_VOL_MASK;
			wm8350_reg_write(wm8350, WM8350_LOUT1_VOLUME,
					 reg | (val << WM8350_OUT1L_VOL_SHIFT));
		} else
			left_complete = 1;
	} else
		return 1;

	/* right channel */
	reg = wm8350_reg_read(wm8350, WM8350_ROUT1_VOLUME);
	val = (reg & WM8350_OUT1R_VOL_MASK) >> WM8350_OUT1R_VOL_SHIFT;
	if (out1->ramp == WM8350_RAMP_UP) {
		/* ramp step up */
		if (val < out1->right_vol) {
			val++;
			reg &= ~WM8350_OUT1R_VOL_MASK;
			wm8350_reg_write(wm8350, WM8350_ROUT1_VOLUME,
					 reg | (val << WM8350_OUT1R_VOL_SHIFT));
		} else
			right_complete = 1;
	} else if (out1->ramp == WM8350_RAMP_DOWN) {
		/* ramp step down */
		if (val > 0) {
			val--;
			reg &= ~WM8350_OUT1R_VOL_MASK;
			wm8350_reg_write(wm8350, WM8350_ROUT1_VOLUME,
					 reg | (val << WM8350_OUT1R_VOL_SHIFT));
		} else
			right_complete = 1;
	}

	/* only hit the update bit if either volume has changed this step */
	if (!left_complete || !right_complete)
		wm8350_set_bits(wm8350, WM8350_LOUT1_VOLUME, WM8350_OUT1_VU);

	return left_complete & right_complete;
}

/*
 * Ramp OUT2 PGA volume to minimise pops at stream startup and shutdown.
 */
static inline int wm8350_out2_ramp_step(struct snd_soc_codec *codec)
{
	struct wm8350_data *wm8350_data = snd_soc_codec_get_drvdata(codec);
	struct wm8350_output *out2 = &wm8350_data->out2;
	struct wm8350 *wm8350 = codec->control_data;
	int left_complete = 0, right_complete = 0;
	u16 reg, val;

	/* left channel */
	reg = wm8350_reg_read(wm8350, WM8350_LOUT2_VOLUME);
	val = (reg & WM8350_OUT2L_VOL_MASK) >> WM8350_OUT1L_VOL_SHIFT;
	if (out2->ramp == WM8350_RAMP_UP) {
		/* ramp step up */
		if (val < out2->left_vol) {
			val++;
			reg &= ~WM8350_OUT2L_VOL_MASK;
			wm8350_reg_write(wm8350, WM8350_LOUT2_VOLUME,
					 reg | (val << WM8350_OUT1L_VOL_SHIFT));
		} else
			left_complete = 1;
	} else if (out2->ramp == WM8350_RAMP_DOWN) {
		/* ramp step down */
		if (val > 0) {
			val--;
			reg &= ~WM8350_OUT2L_VOL_MASK;
			wm8350_reg_write(wm8350, WM8350_LOUT2_VOLUME,
					 reg | (val << WM8350_OUT1L_VOL_SHIFT));
		} else
			left_complete = 1;
	} else
		return 1;

	/* right channel */
	reg = wm8350_reg_read(wm8350, WM8350_ROUT2_VOLUME);
	val = (reg & WM8350_OUT2R_VOL_MASK) >> WM8350_OUT1R_VOL_SHIFT;
	if (out2->ramp == WM8350_RAMP_UP) {
		/* ramp step up */
		if (val < out2->right_vol) {
			val++;
			reg &= ~WM8350_OUT2R_VOL_MASK;
			wm8350_reg_write(wm8350, WM8350_ROUT2_VOLUME,
					 reg | (val << WM8350_OUT1R_VOL_SHIFT));
		} else
			right_complete = 1;
	} else if (out2->ramp == WM8350_RAMP_DOWN) {
		/* ramp step down */
		if (val > 0) {
			val--;
			reg &= ~WM8350_OUT2R_VOL_MASK;
			wm8350_reg_write(wm8350, WM8350_ROUT2_VOLUME,
					 reg | (val << WM8350_OUT1R_VOL_SHIFT));
		} else
			right_complete = 1;
	}

	/* only hit the update bit if either volume has changed this step */
	if (!left_complete || !right_complete)
		wm8350_set_bits(wm8350, WM8350_LOUT2_VOLUME, WM8350_OUT2_VU);

	return left_complete & right_complete;
}

/*
 * This work ramps both output PGAs at stream start/stop time to
 * minimise pop associated with DAPM power switching.
 * It's best to enable Zero Cross when ramping occurs to minimise any
 * zipper noises.
 */
static void wm8350_pga_work(struct work_struct *work)
{
	struct snd_soc_dapm_context *dapm =
	    container_of(work, struct snd_soc_dapm_context, delayed_work.work);
	struct snd_soc_codec *codec = dapm->codec;
	struct wm8350_data *wm8350_data = snd_soc_codec_get_drvdata(codec);
	struct wm8350_output *out1 = &wm8350_data->out1,
	    *out2 = &wm8350_data->out2;
	int i, out1_complete, out2_complete;

	/* do we need to ramp at all ? */
	if (out1->ramp == WM8350_RAMP_NONE && out2->ramp == WM8350_RAMP_NONE)
		return;

	/* PGA volumes have 6 bits of resolution to ramp */
	for (i = 0; i <= 63; i++) {
		out1_complete = 1, out2_complete = 1;
		if (out1->ramp != WM8350_RAMP_NONE)
			out1_complete = wm8350_out1_ramp_step(codec);
		if (out2->ramp != WM8350_RAMP_NONE)
			out2_complete = wm8350_out2_ramp_step(codec);

		/* ramp finished ? */
		if (out1_complete && out2_complete)
			break;

		/* we need to delay longer on the up ramp */
		if (out1->ramp == WM8350_RAMP_UP ||
		    out2->ramp == WM8350_RAMP_UP) {
			/* delay is longer over 0dB as increases are larger */
			if (i >= WM8350_OUTn_0dB)
				schedule_timeout_interruptible(msecs_to_jiffies
							       (2));
			else
				schedule_timeout_interruptible(msecs_to_jiffies
							       (1));
		} else
			udelay(50);	/* doesn't matter if we delay longer */
	}

	out1->ramp = WM8350_RAMP_NONE;
	out2->ramp = WM8350_RAMP_NONE;
}

/*
 * WM8350 Controls
 */

static int pga_event(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct wm8350_data *wm8350_data = snd_soc_codec_get_drvdata(codec);
	struct wm8350_output *out;

	switch (w->shift) {
	case 0:
	case 1:
		out = &wm8350_data->out1;
		break;
	case 2:
	case 3:
		out = &wm8350_data->out2;
		break;

	default:
		BUG();
		return -1;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		out->ramp = WM8350_RAMP_UP;
		out->active = 1;

		if (!delayed_work_pending(&codec->dapm.delayed_work))
			schedule_delayed_work(&codec->dapm.delayed_work,
					      msecs_to_jiffies(1));
		break;

	case SND_SOC_DAPM_PRE_PMD:
		out->ramp = WM8350_RAMP_DOWN;
		out->active = 0;

		if (!delayed_work_pending(&codec->dapm.delayed_work))
			schedule_delayed_work(&codec->dapm.delayed_work,
					      msecs_to_jiffies(1));
		break;
	}

	return 0;
}

static int wm8350_put_volsw_2r_vu(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8350_data *wm8350_priv = snd_soc_codec_get_drvdata(codec);
	struct wm8350_output *out = NULL;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int ret;
	unsigned int reg = mc->reg;
	u16 val;

	/* For OUT1 and OUT2 we shadow the values and only actually write
	 * them out when active in order to ensure the amplifier comes on
	 * as quietly as possible. */
	switch (reg) {
	case WM8350_LOUT1_VOLUME:
		out = &wm8350_priv->out1;
		break;
	case WM8350_LOUT2_VOLUME:
		out = &wm8350_priv->out2;
		break;
	default:
		break;
	}

	if (out) {
		out->left_vol = ucontrol->value.integer.value[0];
		out->right_vol = ucontrol->value.integer.value[1];
		if (!out->active)
			return 1;
	}

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	/* now hit the volume update bits (always bit 8) */
	val = wm8350_codec_read(codec, reg);
	wm8350_codec_write(codec, reg, val | WM8350_OUT1_VU);
	return 1;
}

static int wm8350_get_volsw_2r(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8350_data *wm8350_priv = snd_soc_codec_get_drvdata(codec);
	struct wm8350_output *out1 = &wm8350_priv->out1;
	struct wm8350_output *out2 = &wm8350_priv->out2;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;

	/* If these are cached registers use the cache */
	switch (reg) {
	case WM8350_LOUT1_VOLUME:
		ucontrol->value.integer.value[0] = out1->left_vol;
		ucontrol->value.integer.value[1] = out1->right_vol;
		return 0;

	case WM8350_LOUT2_VOLUME:
		ucontrol->value.integer.value[0] = out2->left_vol;
		ucontrol->value.integer.value[1] = out2->right_vol;
		return 0;

	default:
		break;
	}

	return snd_soc_get_volsw(kcontrol, ucontrol);
}

static const char *wm8350_deemp[] = { "None", "32kHz", "44.1kHz", "48kHz" };
static const char *wm8350_pol[] = { "Normal", "Inv R", "Inv L", "Inv L & R" };
static const char *wm8350_dacmutem[] = { "Normal", "Soft" };
static const char *wm8350_dacmutes[] = { "Fast", "Slow" };
static const char *wm8350_adcfilter[] = { "None", "High Pass" };
static const char *wm8350_adchp[] = { "44.1kHz", "8kHz", "16kHz", "32kHz" };
static const char *wm8350_lr[] = { "Left", "Right" };

static const struct soc_enum wm8350_enum[] = {
	SOC_ENUM_SINGLE(WM8350_DAC_CONTROL, 4, 4, wm8350_deemp),
	SOC_ENUM_SINGLE(WM8350_DAC_CONTROL, 0, 4, wm8350_pol),
	SOC_ENUM_SINGLE(WM8350_DAC_MUTE_VOLUME, 14, 2, wm8350_dacmutem),
	SOC_ENUM_SINGLE(WM8350_DAC_MUTE_VOLUME, 13, 2, wm8350_dacmutes),
	SOC_ENUM_SINGLE(WM8350_ADC_CONTROL, 15, 2, wm8350_adcfilter),
	SOC_ENUM_SINGLE(WM8350_ADC_CONTROL, 8, 4, wm8350_adchp),
	SOC_ENUM_SINGLE(WM8350_ADC_CONTROL, 0, 4, wm8350_pol),
	SOC_ENUM_SINGLE(WM8350_INPUT_MIXER_VOLUME, 15, 2, wm8350_lr),
};

static DECLARE_TLV_DB_SCALE(pre_amp_tlv, -1200, 3525, 0);
static DECLARE_TLV_DB_SCALE(out_pga_tlv, -5700, 600, 0);
static DECLARE_TLV_DB_SCALE(dac_pcm_tlv, -7163, 36, 1);
static DECLARE_TLV_DB_SCALE(adc_pcm_tlv, -12700, 50, 1);
static DECLARE_TLV_DB_SCALE(out_mix_tlv, -1500, 300, 1);

static const unsigned int capture_sd_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 12, TLV_DB_SCALE_ITEM(-3600, 300, 1),
	13, 15, TLV_DB_SCALE_ITEM(0, 0, 0),
};

static const struct snd_kcontrol_new wm8350_snd_controls[] = {
	SOC_ENUM("Playback Deemphasis", wm8350_enum[0]),
	SOC_ENUM("Playback DAC Inversion", wm8350_enum[1]),
	SOC_DOUBLE_R_EXT_TLV("Playback PCM Volume",
				WM8350_DAC_DIGITAL_VOLUME_L,
				WM8350_DAC_DIGITAL_VOLUME_R,
				0, 255, 0, wm8350_get_volsw_2r,
				wm8350_put_volsw_2r_vu, dac_pcm_tlv),
	SOC_ENUM("Playback PCM Mute Function", wm8350_enum[2]),
	SOC_ENUM("Playback PCM Mute Speed", wm8350_enum[3]),
	SOC_ENUM("Capture PCM Filter", wm8350_enum[4]),
	SOC_ENUM("Capture PCM HP Filter", wm8350_enum[5]),
	SOC_ENUM("Capture ADC Inversion", wm8350_enum[6]),
	SOC_DOUBLE_R_EXT_TLV("Capture PCM Volume",
				WM8350_ADC_DIGITAL_VOLUME_L,
				WM8350_ADC_DIGITAL_VOLUME_R,
				0, 255, 0, wm8350_get_volsw_2r,
				wm8350_put_volsw_2r_vu, adc_pcm_tlv),
	SOC_DOUBLE_TLV("Capture Sidetone Volume",
		       WM8350_ADC_DIVIDER,
		       8, 4, 15, 1, capture_sd_tlv),
	SOC_DOUBLE_R_EXT_TLV("Capture Volume",
				WM8350_LEFT_INPUT_VOLUME,
				WM8350_RIGHT_INPUT_VOLUME,
				2, 63, 0, wm8350_get_volsw_2r,
				wm8350_put_volsw_2r_vu, pre_amp_tlv),
	SOC_DOUBLE_R("Capture ZC Switch",
		     WM8350_LEFT_INPUT_VOLUME,
		     WM8350_RIGHT_INPUT_VOLUME, 13, 1, 0),
	SOC_SINGLE_TLV("Left Input Left Sidetone Volume",
		       WM8350_OUTPUT_LEFT_MIXER_VOLUME, 1, 7, 0, out_mix_tlv),
	SOC_SINGLE_TLV("Left Input Right Sidetone Volume",
		       WM8350_OUTPUT_LEFT_MIXER_VOLUME,
		       5, 7, 0, out_mix_tlv),
	SOC_SINGLE_TLV("Left Input Bypass Volume",
		       WM8350_OUTPUT_LEFT_MIXER_VOLUME,
		       9, 7, 0, out_mix_tlv),
	SOC_SINGLE_TLV("Right Input Left Sidetone Volume",
		       WM8350_OUTPUT_RIGHT_MIXER_VOLUME,
		       1, 7, 0, out_mix_tlv),
	SOC_SINGLE_TLV("Right Input Right Sidetone Volume",
		       WM8350_OUTPUT_RIGHT_MIXER_VOLUME,
		       5, 7, 0, out_mix_tlv),
	SOC_SINGLE_TLV("Right Input Bypass Volume",
		       WM8350_OUTPUT_RIGHT_MIXER_VOLUME,
		       13, 7, 0, out_mix_tlv),
	SOC_SINGLE("Left Input Mixer +20dB Switch",
		   WM8350_INPUT_MIXER_VOLUME_L, 0, 1, 0),
	SOC_SINGLE("Right Input Mixer +20dB Switch",
		   WM8350_INPUT_MIXER_VOLUME_R, 0, 1, 0),
	SOC_SINGLE_TLV("Out4 Capture Volume",
		       WM8350_INPUT_MIXER_VOLUME,
		       1, 7, 0, out_mix_tlv),
	SOC_DOUBLE_R_EXT_TLV("Out1 Playback Volume",
				WM8350_LOUT1_VOLUME,
				WM8350_ROUT1_VOLUME,
				2, 63, 0, wm8350_get_volsw_2r,
				wm8350_put_volsw_2r_vu, out_pga_tlv),
	SOC_DOUBLE_R("Out1 Playback ZC Switch",
		     WM8350_LOUT1_VOLUME,
		     WM8350_ROUT1_VOLUME, 13, 1, 0),
	SOC_DOUBLE_R_EXT_TLV("Out2 Playback Volume",
				WM8350_LOUT2_VOLUME,
				WM8350_ROUT2_VOLUME,
				2, 63, 0, wm8350_get_volsw_2r,
				wm8350_put_volsw_2r_vu, out_pga_tlv),
	SOC_DOUBLE_R("Out2 Playback ZC Switch", WM8350_LOUT2_VOLUME,
		     WM8350_ROUT2_VOLUME, 13, 1, 0),
	SOC_SINGLE("Out2 Right Invert Switch", WM8350_ROUT2_VOLUME, 10, 1, 0),
	SOC_SINGLE_TLV("Out2 Beep Volume", WM8350_BEEP_VOLUME,
		       5, 7, 0, out_mix_tlv),

	SOC_DOUBLE_R("Out1 Playback Switch",
		     WM8350_LOUT1_VOLUME,
		     WM8350_ROUT1_VOLUME,
		     14, 1, 1),
	SOC_DOUBLE_R("Out2 Playback Switch",
		     WM8350_LOUT2_VOLUME,
		     WM8350_ROUT2_VOLUME,
		     14, 1, 1),
};

/*
 * DAPM Controls
 */

/* Left Playback Mixer */
static const struct snd_kcontrol_new wm8350_left_play_mixer_controls[] = {
	SOC_DAPM_SINGLE("Playback Switch",
			WM8350_LEFT_MIXER_CONTROL, 11, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch",
			WM8350_LEFT_MIXER_CONTROL, 2, 1, 0),
	SOC_DAPM_SINGLE("Right Playback Switch",
			WM8350_LEFT_MIXER_CONTROL, 12, 1, 0),
	SOC_DAPM_SINGLE("Left Sidetone Switch",
			WM8350_LEFT_MIXER_CONTROL, 0, 1, 0),
	SOC_DAPM_SINGLE("Right Sidetone Switch",
			WM8350_LEFT_MIXER_CONTROL, 1, 1, 0),
};

/* Right Playback Mixer */
static const struct snd_kcontrol_new wm8350_right_play_mixer_controls[] = {
	SOC_DAPM_SINGLE("Playback Switch",
			WM8350_RIGHT_MIXER_CONTROL, 12, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch",
			WM8350_RIGHT_MIXER_CONTROL, 3, 1, 0),
	SOC_DAPM_SINGLE("Left Playback Switch",
			WM8350_RIGHT_MIXER_CONTROL, 11, 1, 0),
	SOC_DAPM_SINGLE("Left Sidetone Switch",
			WM8350_RIGHT_MIXER_CONTROL, 0, 1, 0),
	SOC_DAPM_SINGLE("Right Sidetone Switch",
			WM8350_RIGHT_MIXER_CONTROL, 1, 1, 0),
};

/* Out4 Mixer */
static const struct snd_kcontrol_new wm8350_out4_mixer_controls[] = {
	SOC_DAPM_SINGLE("Right Playback Switch",
			WM8350_OUT4_MIXER_CONTROL, 12, 1, 0),
	SOC_DAPM_SINGLE("Left Playback Switch",
			WM8350_OUT4_MIXER_CONTROL, 11, 1, 0),
	SOC_DAPM_SINGLE("Right Capture Switch",
			WM8350_OUT4_MIXER_CONTROL, 9, 1, 0),
	SOC_DAPM_SINGLE("Out3 Playback Switch",
			WM8350_OUT4_MIXER_CONTROL, 2, 1, 0),
	SOC_DAPM_SINGLE("Right Mixer Switch",
			WM8350_OUT4_MIXER_CONTROL, 1, 1, 0),
	SOC_DAPM_SINGLE("Left Mixer Switch",
			WM8350_OUT4_MIXER_CONTROL, 0, 1, 0),
};

/* Out3 Mixer */
static const struct snd_kcontrol_new wm8350_out3_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left Playback Switch",
			WM8350_OUT3_MIXER_CONTROL, 11, 1, 0),
	SOC_DAPM_SINGLE("Left Capture Switch",
			WM8350_OUT3_MIXER_CONTROL, 8, 1, 0),
	SOC_DAPM_SINGLE("Out4 Playback Switch",
			WM8350_OUT3_MIXER_CONTROL, 3, 1, 0),
	SOC_DAPM_SINGLE("Left Mixer Switch",
			WM8350_OUT3_MIXER_CONTROL, 0, 1, 0),
};

/* Left Input Mixer */
static const struct snd_kcontrol_new wm8350_left_capt_mixer_controls[] = {
	SOC_DAPM_SINGLE_TLV("L2 Capture Volume",
			    WM8350_INPUT_MIXER_VOLUME_L, 1, 7, 0, out_mix_tlv),
	SOC_DAPM_SINGLE_TLV("L3 Capture Volume",
			    WM8350_INPUT_MIXER_VOLUME_L, 9, 7, 0, out_mix_tlv),
	SOC_DAPM_SINGLE("PGA Capture Switch",
			WM8350_LEFT_INPUT_VOLUME, 14, 1, 1),
};

/* Right Input Mixer */
static const struct snd_kcontrol_new wm8350_right_capt_mixer_controls[] = {
	SOC_DAPM_SINGLE_TLV("L2 Capture Volume",
			    WM8350_INPUT_MIXER_VOLUME_R, 5, 7, 0, out_mix_tlv),
	SOC_DAPM_SINGLE_TLV("L3 Capture Volume",
			    WM8350_INPUT_MIXER_VOLUME_R, 13, 7, 0, out_mix_tlv),
	SOC_DAPM_SINGLE("PGA Capture Switch",
			WM8350_RIGHT_INPUT_VOLUME, 14, 1, 1),
};

/* Left Mic Mixer */
static const struct snd_kcontrol_new wm8350_left_mic_mixer_controls[] = {
	SOC_DAPM_SINGLE("INN Capture Switch", WM8350_INPUT_CONTROL, 1, 1, 0),
	SOC_DAPM_SINGLE("INP Capture Switch", WM8350_INPUT_CONTROL, 0, 1, 0),
	SOC_DAPM_SINGLE("IN2 Capture Switch", WM8350_INPUT_CONTROL, 2, 1, 0),
};

/* Right Mic Mixer */
static const struct snd_kcontrol_new wm8350_right_mic_mixer_controls[] = {
	SOC_DAPM_SINGLE("INN Capture Switch", WM8350_INPUT_CONTROL, 9, 1, 0),
	SOC_DAPM_SINGLE("INP Capture Switch", WM8350_INPUT_CONTROL, 8, 1, 0),
	SOC_DAPM_SINGLE("IN2 Capture Switch", WM8350_INPUT_CONTROL, 10, 1, 0),
};

/* Beep Switch */
static const struct snd_kcontrol_new wm8350_beep_switch_controls =
SOC_DAPM_SINGLE("Switch", WM8350_BEEP_VOLUME, 15, 1, 1);

/* Out4 Capture Mux */
static const struct snd_kcontrol_new wm8350_out4_capture_controls =
SOC_DAPM_ENUM("Route", wm8350_enum[7]);

static const struct snd_soc_dapm_widget wm8350_dapm_widgets[] = {

	SND_SOC_DAPM_PGA("IN3R PGA", WM8350_POWER_MGMT_2, 11, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IN3L PGA", WM8350_POWER_MGMT_2, 10, 0, NULL, 0),
	SND_SOC_DAPM_PGA_E("Right Out2 PGA", WM8350_POWER_MGMT_3, 3, 0, NULL,
			   0, pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("Left Out2 PGA", WM8350_POWER_MGMT_3, 2, 0, NULL, 0,
			   pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("Right Out1 PGA", WM8350_POWER_MGMT_3, 1, 0, NULL,
			   0, pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("Left Out1 PGA", WM8350_POWER_MGMT_3, 0, 0, NULL, 0,
			   pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MIXER("Right Capture Mixer", WM8350_POWER_MGMT_2,
			   7, 0, &wm8350_right_capt_mixer_controls[0],
			   ARRAY_SIZE(wm8350_right_capt_mixer_controls)),

	SND_SOC_DAPM_MIXER("Left Capture Mixer", WM8350_POWER_MGMT_2,
			   6, 0, &wm8350_left_capt_mixer_controls[0],
			   ARRAY_SIZE(wm8350_left_capt_mixer_controls)),

	SND_SOC_DAPM_MIXER("Out4 Mixer", WM8350_POWER_MGMT_2, 5, 0,
			   &wm8350_out4_mixer_controls[0],
			   ARRAY_SIZE(wm8350_out4_mixer_controls)),

	SND_SOC_DAPM_MIXER("Out3 Mixer", WM8350_POWER_MGMT_2, 4, 0,
			   &wm8350_out3_mixer_controls[0],
			   ARRAY_SIZE(wm8350_out3_mixer_controls)),

	SND_SOC_DAPM_MIXER("Right Playback Mixer", WM8350_POWER_MGMT_2, 1, 0,
			   &wm8350_right_play_mixer_controls[0],
			   ARRAY_SIZE(wm8350_right_play_mixer_controls)),

	SND_SOC_DAPM_MIXER("Left Playback Mixer", WM8350_POWER_MGMT_2, 0, 0,
			   &wm8350_left_play_mixer_controls[0],
			   ARRAY_SIZE(wm8350_left_play_mixer_controls)),

	SND_SOC_DAPM_MIXER("Left Mic Mixer", WM8350_POWER_MGMT_2, 8, 0,
			   &wm8350_left_mic_mixer_controls[0],
			   ARRAY_SIZE(wm8350_left_mic_mixer_controls)),

	SND_SOC_DAPM_MIXER("Right Mic Mixer", WM8350_POWER_MGMT_2, 9, 0,
			   &wm8350_right_mic_mixer_controls[0],
			   ARRAY_SIZE(wm8350_right_mic_mixer_controls)),

	/* virtual mixer for Beep and Out2R */
	SND_SOC_DAPM_MIXER("Out2 Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SWITCH("Beep", WM8350_POWER_MGMT_3, 7, 0,
			    &wm8350_beep_switch_controls),

	SND_SOC_DAPM_ADC("Right ADC", "Right Capture",
			 WM8350_POWER_MGMT_4, 3, 0),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture",
			 WM8350_POWER_MGMT_4, 2, 0),
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback",
			 WM8350_POWER_MGMT_4, 5, 0),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback",
			 WM8350_POWER_MGMT_4, 4, 0),

	SND_SOC_DAPM_MICBIAS("Mic Bias", WM8350_POWER_MGMT_1, 4, 0),

	SND_SOC_DAPM_MUX("Out4 Capture Channel", SND_SOC_NOPM, 0, 0,
			 &wm8350_out4_capture_controls),

	SND_SOC_DAPM_OUTPUT("OUT1R"),
	SND_SOC_DAPM_OUTPUT("OUT1L"),
	SND_SOC_DAPM_OUTPUT("OUT2R"),
	SND_SOC_DAPM_OUTPUT("OUT2L"),
	SND_SOC_DAPM_OUTPUT("OUT3"),
	SND_SOC_DAPM_OUTPUT("OUT4"),

	SND_SOC_DAPM_INPUT("IN1RN"),
	SND_SOC_DAPM_INPUT("IN1RP"),
	SND_SOC_DAPM_INPUT("IN2R"),
	SND_SOC_DAPM_INPUT("IN1LP"),
	SND_SOC_DAPM_INPUT("IN1LN"),
	SND_SOC_DAPM_INPUT("IN2L"),
	SND_SOC_DAPM_INPUT("IN3R"),
	SND_SOC_DAPM_INPUT("IN3L"),
};

static const struct snd_soc_dapm_route wm8350_dapm_routes[] = {

	/* left playback mixer */
	{"Left Playback Mixer", "Playback Switch", "Left DAC"},
	{"Left Playback Mixer", "Left Bypass Switch", "IN3L PGA"},
	{"Left Playback Mixer", "Right Playback Switch", "Right DAC"},
	{"Left Playback Mixer", "Left Sidetone Switch", "Left Mic Mixer"},
	{"Left Playback Mixer", "Right Sidetone Switch", "Right Mic Mixer"},

	/* right playback mixer */
	{"Right Playback Mixer", "Playback Switch", "Right DAC"},
	{"Right Playback Mixer", "Right Bypass Switch", "IN3R PGA"},
	{"Right Playback Mixer", "Left Playback Switch", "Left DAC"},
	{"Right Playback Mixer", "Left Sidetone Switch", "Left Mic Mixer"},
	{"Right Playback Mixer", "Right Sidetone Switch", "Right Mic Mixer"},

	/* out4 playback mixer */
	{"Out4 Mixer", "Right Playback Switch", "Right DAC"},
	{"Out4 Mixer", "Left Playback Switch", "Left DAC"},
	{"Out4 Mixer", "Right Capture Switch", "Right Capture Mixer"},
	{"Out4 Mixer", "Out3 Playback Switch", "Out3 Mixer"},
	{"Out4 Mixer", "Right Mixer Switch", "Right Playback Mixer"},
	{"Out4 Mixer", "Left Mixer Switch", "Left Playback Mixer"},
	{"OUT4", NULL, "Out4 Mixer"},

	/* out3 playback mixer */
	{"Out3 Mixer", "Left Playback Switch", "Left DAC"},
	{"Out3 Mixer", "Left Capture Switch", "Left Capture Mixer"},
	{"Out3 Mixer", "Left Mixer Switch", "Left Playback Mixer"},
	{"Out3 Mixer", "Out4 Playback Switch", "Out4 Mixer"},
	{"OUT3", NULL, "Out3 Mixer"},

	/* out2 */
	{"Right Out2 PGA", NULL, "Right Playback Mixer"},
	{"Left Out2 PGA", NULL, "Left Playback Mixer"},
	{"OUT2L", NULL, "Left Out2 PGA"},
	{"OUT2R", NULL, "Right Out2 PGA"},

	/* out1 */
	{"Right Out1 PGA", NULL, "Right Playback Mixer"},
	{"Left Out1 PGA", NULL, "Left Playback Mixer"},
	{"OUT1L", NULL, "Left Out1 PGA"},
	{"OUT1R", NULL, "Right Out1 PGA"},

	/* ADCs */
	{"Left ADC", NULL, "Left Capture Mixer"},
	{"Right ADC", NULL, "Right Capture Mixer"},

	/* Left capture mixer */
	{"Left Capture Mixer", "L2 Capture Volume", "IN2L"},
	{"Left Capture Mixer", "L3 Capture Volume", "IN3L PGA"},
	{"Left Capture Mixer", "PGA Capture Switch", "Left Mic Mixer"},
	{"Left Capture Mixer", NULL, "Out4 Capture Channel"},

	/* Right capture mixer */
	{"Right Capture Mixer", "L2 Capture Volume", "IN2R"},
	{"Right Capture Mixer", "L3 Capture Volume", "IN3R PGA"},
	{"Right Capture Mixer", "PGA Capture Switch", "Right Mic Mixer"},
	{"Right Capture Mixer", NULL, "Out4 Capture Channel"},

	/* L3 Inputs */
	{"IN3L PGA", NULL, "IN3L"},
	{"IN3R PGA", NULL, "IN3R"},

	/* Left Mic mixer */
	{"Left Mic Mixer", "INN Capture Switch", "IN1LN"},
	{"Left Mic Mixer", "INP Capture Switch", "IN1LP"},
	{"Left Mic Mixer", "IN2 Capture Switch", "IN2L"},

	/* Right Mic mixer */
	{"Right Mic Mixer", "INN Capture Switch", "IN1RN"},
	{"Right Mic Mixer", "INP Capture Switch", "IN1RP"},
	{"Right Mic Mixer", "IN2 Capture Switch", "IN2R"},

	/* out 4 capture */
	{"Out4 Capture Channel", NULL, "Out4 Mixer"},

	/* Beep */
	{"Beep", NULL, "IN3R PGA"},
};

static int wm8350_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8350 *wm8350 = codec->control_data;
	u16 fll_4;

	switch (clk_id) {
	case WM8350_MCLK_SEL_MCLK:
		wm8350_clear_bits(wm8350, WM8350_CLOCK_CONTROL_1,
				  WM8350_MCLK_SEL);
		break;
	case WM8350_MCLK_SEL_PLL_MCLK:
	case WM8350_MCLK_SEL_PLL_DAC:
	case WM8350_MCLK_SEL_PLL_ADC:
	case WM8350_MCLK_SEL_PLL_32K:
		wm8350_set_bits(wm8350, WM8350_CLOCK_CONTROL_1,
				WM8350_MCLK_SEL);
		fll_4 = wm8350_codec_read(codec, WM8350_FLL_CONTROL_4) &
		    ~WM8350_FLL_CLK_SRC_MASK;
		wm8350_codec_write(codec, WM8350_FLL_CONTROL_4, fll_4 | clk_id);
		break;
	}

	/* MCLK direction */
	if (dir == SND_SOC_CLOCK_OUT)
		wm8350_set_bits(wm8350, WM8350_CLOCK_CONTROL_2,
				WM8350_MCLK_DIR);
	else
		wm8350_clear_bits(wm8350, WM8350_CLOCK_CONTROL_2,
				  WM8350_MCLK_DIR);

	return 0;
}

static int wm8350_set_clkdiv(struct snd_soc_dai *codec_dai, int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 val;

	switch (div_id) {
	case WM8350_ADC_CLKDIV:
		val = wm8350_codec_read(codec, WM8350_ADC_DIVIDER) &
		    ~WM8350_ADC_CLKDIV_MASK;
		wm8350_codec_write(codec, WM8350_ADC_DIVIDER, val | div);
		break;
	case WM8350_DAC_CLKDIV:
		val = wm8350_codec_read(codec, WM8350_DAC_CLOCK_CONTROL) &
		    ~WM8350_DAC_CLKDIV_MASK;
		wm8350_codec_write(codec, WM8350_DAC_CLOCK_CONTROL, val | div);
		break;
	case WM8350_BCLK_CLKDIV:
		val = wm8350_codec_read(codec, WM8350_CLOCK_CONTROL_1) &
		    ~WM8350_BCLK_DIV_MASK;
		wm8350_codec_write(codec, WM8350_CLOCK_CONTROL_1, val | div);
		break;
	case WM8350_OPCLK_CLKDIV:
		val = wm8350_codec_read(codec, WM8350_CLOCK_CONTROL_1) &
		    ~WM8350_OPCLK_DIV_MASK;
		wm8350_codec_write(codec, WM8350_CLOCK_CONTROL_1, val | div);
		break;
	case WM8350_SYS_CLKDIV:
		val = wm8350_codec_read(codec, WM8350_CLOCK_CONTROL_1) &
		    ~WM8350_MCLK_DIV_MASK;
		wm8350_codec_write(codec, WM8350_CLOCK_CONTROL_1, val | div);
		break;
	case WM8350_DACLR_CLKDIV:
		val = wm8350_codec_read(codec, WM8350_DAC_LR_RATE) &
		    ~WM8350_DACLRC_RATE_MASK;
		wm8350_codec_write(codec, WM8350_DAC_LR_RATE, val | div);
		break;
	case WM8350_ADCLR_CLKDIV:
		val = wm8350_codec_read(codec, WM8350_ADC_LR_RATE) &
		    ~WM8350_ADCLRC_RATE_MASK;
		wm8350_codec_write(codec, WM8350_ADC_LR_RATE, val | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8350_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = wm8350_codec_read(codec, WM8350_AI_FORMATING) &
	    ~(WM8350_AIF_BCLK_INV | WM8350_AIF_LRCLK_INV | WM8350_AIF_FMT_MASK);
	u16 master = wm8350_codec_read(codec, WM8350_AI_DAC_CONTROL) &
	    ~WM8350_BCLK_MSTR;
	u16 dac_lrc = wm8350_codec_read(codec, WM8350_DAC_LR_RATE) &
	    ~WM8350_DACLRC_ENA;
	u16 adc_lrc = wm8350_codec_read(codec, WM8350_ADC_LR_RATE) &
	    ~WM8350_ADCLRC_ENA;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		master |= WM8350_BCLK_MSTR;
		dac_lrc |= WM8350_DACLRC_ENA;
		adc_lrc |= WM8350_ADCLRC_ENA;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x2 << 8;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x1 << 8;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x3 << 8;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x3 << 8 | WM8350_AIF_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= WM8350_AIF_LRCLK_INV | WM8350_AIF_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= WM8350_AIF_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= WM8350_AIF_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	wm8350_codec_write(codec, WM8350_AI_FORMATING, iface);
	wm8350_codec_write(codec, WM8350_AI_DAC_CONTROL, master);
	wm8350_codec_write(codec, WM8350_DAC_LR_RATE, dac_lrc);
	wm8350_codec_write(codec, WM8350_ADC_LR_RATE, adc_lrc);
	return 0;
}

static int wm8350_pcm_trigger(struct snd_pcm_substream *substream,
			      int cmd, struct snd_soc_dai *codec_dai)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int master = wm8350_codec_cache_read(codec, WM8350_AI_DAC_CONTROL) &
	    WM8350_BCLK_MSTR;
	int enabled = 0;

	/* Check that the DACs or ADCs are enabled since they are
	 * required for LRC in master mode. The DACs or ADCs need a
	 * valid audio path i.e. pin -> ADC or DAC -> pin before
	 * the LRC will be enabled in master mode. */
	if (!master || cmd != SNDRV_PCM_TRIGGER_START)
		return 0;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		enabled = wm8350_codec_cache_read(codec, WM8350_POWER_MGMT_4) &
		    (WM8350_ADCR_ENA | WM8350_ADCL_ENA);
	} else {
		enabled = wm8350_codec_cache_read(codec, WM8350_POWER_MGMT_4) &
		    (WM8350_DACR_ENA | WM8350_DACL_ENA);
	}

	if (!enabled) {
		dev_err(codec->dev,
		       "%s: invalid audio path - no clocks available\n",
		       __func__);
		return -EINVAL;
	}
	return 0;
}

static int wm8350_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *codec_dai)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8350 *wm8350 = codec->control_data;
	u16 iface = wm8350_codec_read(codec, WM8350_AI_FORMATING) &
	    ~WM8350_AIF_WL_MASK;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x1 << 10;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x2 << 10;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x3 << 10;
		break;
	}

	wm8350_codec_write(codec, WM8350_AI_FORMATING, iface);

	/* The sloping stopband filter is recommended for use with
	 * lower sample rates to improve performance.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (params_rate(params) < 24000)
			wm8350_set_bits(wm8350, WM8350_DAC_MUTE_VOLUME,
					WM8350_DAC_SB_FILT);
		else
			wm8350_clear_bits(wm8350, WM8350_DAC_MUTE_VOLUME,
					  WM8350_DAC_SB_FILT);
	}

	return 0;
}

static int wm8350_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8350 *wm8350 = codec->control_data;

	if (mute)
		wm8350_set_bits(wm8350, WM8350_DAC_MUTE, WM8350_DAC_MUTE_ENA);
	else
		wm8350_clear_bits(wm8350, WM8350_DAC_MUTE, WM8350_DAC_MUTE_ENA);
	return 0;
}

/* FLL divisors */
struct _fll_div {
	int div;		/* FLL_OUTDIV */
	int n;
	int k;
	int ratio;		/* FLL_FRATIO */
};

/* The size in bits of the fll divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

static inline int fll_factors(struct _fll_div *fll_div, unsigned int input,
			      unsigned int output)
{
	u64 Kpart;
	unsigned int t1, t2, K, Nmod;

	if (output >= 2815250 && output <= 3125000)
		fll_div->div = 0x4;
	else if (output >= 5625000 && output <= 6250000)
		fll_div->div = 0x3;
	else if (output >= 11250000 && output <= 12500000)
		fll_div->div = 0x2;
	else if (output >= 22500000 && output <= 25000000)
		fll_div->div = 0x1;
	else {
		printk(KERN_ERR "wm8350: fll freq %d out of range\n", output);
		return -EINVAL;
	}

	if (input > 48000)
		fll_div->ratio = 1;
	else
		fll_div->ratio = 8;

	t1 = output * (1 << (fll_div->div + 1));
	t2 = input * fll_div->ratio;

	fll_div->n = t1 / t2;
	Nmod = t1 % t2;

	if (Nmod) {
		Kpart = FIXED_FLL_SIZE * (long long)Nmod;
		do_div(Kpart, t2);
		K = Kpart & 0xFFFFFFFF;

		/* Check if we need to round */
		if ((K % 10) >= 5)
			K += 5;

		/* Move down to proper range now rounding is done */
		K /= 10;
		fll_div->k = K;
	} else
		fll_div->k = 0;

	return 0;
}

static int wm8350_set_fll(struct snd_soc_dai *codec_dai,
			  int pll_id, int source, unsigned int freq_in,
			  unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8350 *wm8350 = codec->control_data;
	struct wm8350_data *priv = snd_soc_codec_get_drvdata(codec);
	struct _fll_div fll_div;
	int ret = 0;
	u16 fll_1, fll_4;

	if (freq_in == priv->fll_freq_in && freq_out == priv->fll_freq_out)
		return 0;

	/* power down FLL - we need to do this for reconfiguration */
	wm8350_clear_bits(wm8350, WM8350_POWER_MGMT_4,
			  WM8350_FLL_ENA | WM8350_FLL_OSC_ENA);

	if (freq_out == 0 || freq_in == 0)
		return ret;

	ret = fll_factors(&fll_div, freq_in, freq_out);
	if (ret < 0)
		return ret;
	dev_dbg(wm8350->dev,
		"FLL in %u FLL out %u N 0x%x K 0x%x div %d ratio %d",
		freq_in, freq_out, fll_div.n, fll_div.k, fll_div.div,
		fll_div.ratio);

	/* set up N.K & dividers */
	fll_1 = wm8350_codec_read(codec, WM8350_FLL_CONTROL_1) &
	    ~(WM8350_FLL_OUTDIV_MASK | WM8350_FLL_RSP_RATE_MASK | 0xc000);
	wm8350_codec_write(codec, WM8350_FLL_CONTROL_1,
			   fll_1 | (fll_div.div << 8) | 0x50);
	wm8350_codec_write(codec, WM8350_FLL_CONTROL_2,
			   (fll_div.ratio << 11) | (fll_div.
						    n & WM8350_FLL_N_MASK));
	wm8350_codec_write(codec, WM8350_FLL_CONTROL_3, fll_div.k);
	fll_4 = wm8350_codec_read(codec, WM8350_FLL_CONTROL_4) &
	    ~(WM8350_FLL_FRAC | WM8350_FLL_SLOW_LOCK_REF);
	wm8350_codec_write(codec, WM8350_FLL_CONTROL_4,
			   fll_4 | (fll_div.k ? WM8350_FLL_FRAC : 0) |
			   (fll_div.ratio == 8 ? WM8350_FLL_SLOW_LOCK_REF : 0));

	/* power FLL on */
	wm8350_set_bits(wm8350, WM8350_POWER_MGMT_4, WM8350_FLL_OSC_ENA);
	wm8350_set_bits(wm8350, WM8350_POWER_MGMT_4, WM8350_FLL_ENA);

	priv->fll_freq_out = freq_out;
	priv->fll_freq_in = freq_in;

	return 0;
}

static int wm8350_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8350 *wm8350 = codec->control_data;
	struct wm8350_data *priv = snd_soc_codec_get_drvdata(codec);
	struct wm8350_audio_platform_data *platform =
		wm8350->codec.platform_data;
	u16 pm1;
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		pm1 = wm8350_reg_read(wm8350, WM8350_POWER_MGMT_1) &
		    ~(WM8350_VMID_MASK | WM8350_CODEC_ISEL_MASK);
		wm8350_reg_write(wm8350, WM8350_POWER_MGMT_1,
				 pm1 | WM8350_VMID_50K |
				 platform->codec_current_on << 14);
		break;

	case SND_SOC_BIAS_PREPARE:
		pm1 = wm8350_reg_read(wm8350, WM8350_POWER_MGMT_1);
		pm1 &= ~WM8350_VMID_MASK;
		wm8350_reg_write(wm8350, WM8350_POWER_MGMT_1,
				 pm1 | WM8350_VMID_50K);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(priv->supplies),
						    priv->supplies);
			if (ret != 0)
				return ret;

			/* Enable the system clock */
			wm8350_set_bits(wm8350, WM8350_POWER_MGMT_4,
					WM8350_SYSCLK_ENA);

			/* mute DAC & outputs */
			wm8350_set_bits(wm8350, WM8350_DAC_MUTE,
					WM8350_DAC_MUTE_ENA);

			/* discharge cap memory */
			wm8350_reg_write(wm8350, WM8350_ANTI_POP_CONTROL,
					 platform->dis_out1 |
					 (platform->dis_out2 << 2) |
					 (platform->dis_out3 << 4) |
					 (platform->dis_out4 << 6));

			/* wait for discharge */
			schedule_timeout_interruptible(msecs_to_jiffies
						       (platform->
							cap_discharge_msecs));

			/* enable antipop */
			wm8350_reg_write(wm8350, WM8350_ANTI_POP_CONTROL,
					 (platform->vmid_s_curve << 8));

			/* ramp up vmid */
			wm8350_reg_write(wm8350, WM8350_POWER_MGMT_1,
					 (platform->
					  codec_current_charge << 14) |
					 WM8350_VMID_5K | WM8350_VMIDEN |
					 WM8350_VBUFEN);

			/* wait for vmid */
			schedule_timeout_interruptible(msecs_to_jiffies
						       (platform->
							vmid_charge_msecs));

			/* turn on vmid 300k  */
			pm1 = wm8350_reg_read(wm8350, WM8350_POWER_MGMT_1) &
			    ~(WM8350_VMID_MASK | WM8350_CODEC_ISEL_MASK);
			pm1 |= WM8350_VMID_300K |
				(platform->codec_current_standby << 14);
			wm8350_reg_write(wm8350, WM8350_POWER_MGMT_1,
					 pm1);


			/* enable analogue bias */
			pm1 |= WM8350_BIASEN;
			wm8350_reg_write(wm8350, WM8350_POWER_MGMT_1, pm1);

			/* disable antipop */
			wm8350_reg_write(wm8350, WM8350_ANTI_POP_CONTROL, 0);

		} else {
			/* turn on vmid 300k and reduce current */
			pm1 = wm8350_reg_read(wm8350, WM8350_POWER_MGMT_1) &
			    ~(WM8350_VMID_MASK | WM8350_CODEC_ISEL_MASK);
			wm8350_reg_write(wm8350, WM8350_POWER_MGMT_1,
					 pm1 | WM8350_VMID_300K |
					 (platform->
					  codec_current_standby << 14));

		}
		break;

	case SND_SOC_BIAS_OFF:

		/* mute DAC & enable outputs */
		wm8350_set_bits(wm8350, WM8350_DAC_MUTE, WM8350_DAC_MUTE_ENA);

		wm8350_set_bits(wm8350, WM8350_POWER_MGMT_3,
				WM8350_OUT1L_ENA | WM8350_OUT1R_ENA |
				WM8350_OUT2L_ENA | WM8350_OUT2R_ENA);

		/* enable anti pop S curve */
		wm8350_reg_write(wm8350, WM8350_ANTI_POP_CONTROL,
				 (platform->vmid_s_curve << 8));

		/* turn off vmid  */
		pm1 = wm8350_reg_read(wm8350, WM8350_POWER_MGMT_1) &
		    ~WM8350_VMIDEN;
		wm8350_reg_write(wm8350, WM8350_POWER_MGMT_1, pm1);

		/* wait */
		schedule_timeout_interruptible(msecs_to_jiffies
					       (platform->
						vmid_discharge_msecs));

		wm8350_reg_write(wm8350, WM8350_ANTI_POP_CONTROL,
				 (platform->vmid_s_curve << 8) |
				 platform->dis_out1 |
				 (platform->dis_out2 << 2) |
				 (platform->dis_out3 << 4) |
				 (platform->dis_out4 << 6));

		/* turn off VBuf and drain */
		pm1 = wm8350_reg_read(wm8350, WM8350_POWER_MGMT_1) &
		    ~(WM8350_VBUFEN | WM8350_VMID_MASK);
		wm8350_reg_write(wm8350, WM8350_POWER_MGMT_1,
				 pm1 | WM8350_OUTPUT_DRAIN_EN);

		/* wait */
		schedule_timeout_interruptible(msecs_to_jiffies
					       (platform->drain_msecs));

		pm1 &= ~WM8350_BIASEN;
		wm8350_reg_write(wm8350, WM8350_POWER_MGMT_1, pm1);

		/* disable anti-pop */
		wm8350_reg_write(wm8350, WM8350_ANTI_POP_CONTROL, 0);

		wm8350_clear_bits(wm8350, WM8350_LOUT1_VOLUME,
				  WM8350_OUT1L_ENA);
		wm8350_clear_bits(wm8350, WM8350_ROUT1_VOLUME,
				  WM8350_OUT1R_ENA);
		wm8350_clear_bits(wm8350, WM8350_LOUT2_VOLUME,
				  WM8350_OUT2L_ENA);
		wm8350_clear_bits(wm8350, WM8350_ROUT2_VOLUME,
				  WM8350_OUT2R_ENA);

		/* disable clock gen */
		wm8350_clear_bits(wm8350, WM8350_POWER_MGMT_4,
				  WM8350_SYSCLK_ENA);

		regulator_bulk_disable(ARRAY_SIZE(priv->supplies),
				       priv->supplies);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static int wm8350_suspend(struct snd_soc_codec *codec)
{
	wm8350_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8350_resume(struct snd_soc_codec *codec)
{
	wm8350_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

static void wm8350_hp_work(struct wm8350_data *priv,
			   struct wm8350_jack_data *jack,
			   u16 mask)
{
	struct wm8350 *wm8350 = priv->codec.control_data;
	u16 reg;
	int report;

	reg = wm8350_reg_read(wm8350, WM8350_JACK_PIN_STATUS);
	if (reg & mask)
		report = jack->report;
	else
		report = 0;

	snd_soc_jack_report(jack->jack, report, jack->report);

}

static void wm8350_hpl_work(struct work_struct *work)
{
	struct wm8350_data *priv =
	    container_of(work, struct wm8350_data, hpl.work.work);

	wm8350_hp_work(priv, &priv->hpl, WM8350_JACK_L_LVL);
}

static void wm8350_hpr_work(struct work_struct *work)
{
	struct wm8350_data *priv =
	    container_of(work, struct wm8350_data, hpr.work.work);
	
	wm8350_hp_work(priv, &priv->hpr, WM8350_JACK_R_LVL);
}

static irqreturn_t wm8350_hp_jack_handler(int irq, void *data)
{
	struct wm8350_data *priv = data;
	struct wm8350 *wm8350 = priv->codec.control_data;
	struct wm8350_jack_data *jack = NULL;

	switch (irq - wm8350->irq_base) {
	case WM8350_IRQ_CODEC_JCK_DET_L:
#ifndef CONFIG_SND_SOC_WM8350_MODULE
		trace_snd_soc_jack_irq("WM8350 HPL");
#endif
		jack = &priv->hpl;
		break;

	case WM8350_IRQ_CODEC_JCK_DET_R:
#ifndef CONFIG_SND_SOC_WM8350_MODULE
		trace_snd_soc_jack_irq("WM8350 HPR");
#endif
		jack = &priv->hpr;
		break;

	default:
		BUG();
	}

	if (device_may_wakeup(wm8350->dev))
		pm_wakeup_event(wm8350->dev, 250);

	schedule_delayed_work(&jack->work, 200);

	return IRQ_HANDLED;
}

/**
 * wm8350_hp_jack_detect - Enable headphone jack detection.
 *
 * @codec:  WM8350 codec
 * @which:  left or right jack detect signal
 * @jack:   jack to report detection events on
 * @report: value to report
 *
 * Enables the headphone jack detection of the WM8350.  If no report
 * is specified then detection is disabled.
 */
int wm8350_hp_jack_detect(struct snd_soc_codec *codec, enum wm8350_jack which,
			  struct snd_soc_jack *jack, int report)
{
	struct wm8350_data *priv = snd_soc_codec_get_drvdata(codec);
	struct wm8350 *wm8350 = codec->control_data;
	int irq;
	int ena;

	switch (which) {
	case WM8350_JDL:
		priv->hpl.jack = jack;
		priv->hpl.report = report;
		irq = WM8350_IRQ_CODEC_JCK_DET_L;
		ena = WM8350_JDL_ENA;
		break;

	case WM8350_JDR:
		priv->hpr.jack = jack;
		priv->hpr.report = report;
		irq = WM8350_IRQ_CODEC_JCK_DET_R;
		ena = WM8350_JDR_ENA;
		break;

	default:
		return -EINVAL;
	}

	if (report) {
		wm8350_set_bits(wm8350, WM8350_POWER_MGMT_4, WM8350_TOCLK_ENA);
		wm8350_set_bits(wm8350, WM8350_JACK_DETECT, ena);
	} else {
		wm8350_clear_bits(wm8350, WM8350_JACK_DETECT, ena);
	}

	/* Sync status */
	wm8350_hp_jack_handler(irq + wm8350->irq_base, priv);

	return 0;
}
EXPORT_SYMBOL_GPL(wm8350_hp_jack_detect);

static irqreturn_t wm8350_mic_handler(int irq, void *data)
{
	struct wm8350_data *priv = data;
	struct wm8350 *wm8350 = priv->codec.control_data;
	u16 reg;
	int report = 0;

#ifndef CONFIG_SND_SOC_WM8350_MODULE
	trace_snd_soc_jack_irq("WM8350 mic");
#endif

	reg = wm8350_reg_read(wm8350, WM8350_JACK_PIN_STATUS);
	if (reg & WM8350_JACK_MICSCD_LVL)
		report |= priv->mic.short_report;
	if (reg & WM8350_JACK_MICSD_LVL)
		report |= priv->mic.report;

	snd_soc_jack_report(priv->mic.jack, report,
			    priv->mic.report | priv->mic.short_report);

	return IRQ_HANDLED;
}

/**
 * wm8350_mic_jack_detect - Enable microphone jack detection.
 *
 * @codec:         WM8350 codec
 * @jack:          jack to report detection events on
 * @detect_report: value to report when presence detected
 * @short_report:  value to report when microphone short detected
 *
 * Enables the microphone jack detection of the WM8350.  If both reports
 * are specified as zero then detection is disabled.
 */
int wm8350_mic_jack_detect(struct snd_soc_codec *codec,
			   struct snd_soc_jack *jack,
			   int detect_report, int short_report)
{
	struct wm8350_data *priv = snd_soc_codec_get_drvdata(codec);
	struct wm8350 *wm8350 = codec->control_data;

	priv->mic.jack = jack;
	priv->mic.report = detect_report;
	priv->mic.short_report = short_report;

	if (detect_report || short_report) {
		wm8350_set_bits(wm8350, WM8350_POWER_MGMT_4, WM8350_TOCLK_ENA);
		wm8350_set_bits(wm8350, WM8350_POWER_MGMT_1,
				WM8350_MIC_DET_ENA);
	} else {
		wm8350_clear_bits(wm8350, WM8350_POWER_MGMT_1,
				  WM8350_MIC_DET_ENA);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm8350_mic_jack_detect);

#define WM8350_RATES (SNDRV_PCM_RATE_8000_96000)

#define WM8350_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8350_dai_ops = {
	 .hw_params	= wm8350_pcm_hw_params,
	 .digital_mute	= wm8350_mute,
	 .trigger	= wm8350_pcm_trigger,
	 .set_fmt	= wm8350_set_dai_fmt,
	 .set_sysclk	= wm8350_set_dai_sysclk,
	 .set_pll	= wm8350_set_fll,
	 .set_clkdiv	= wm8350_set_clkdiv,
};

static struct snd_soc_dai_driver wm8350_dai = {
	.name = "wm8350-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8350_RATES,
		.formats = WM8350_FORMATS,
	},
	.capture = {
		 .stream_name = "Capture",
		 .channels_min = 1,
		 .channels_max = 2,
		 .rates = WM8350_RATES,
		 .formats = WM8350_FORMATS,
	 },
	.ops = &wm8350_dai_ops,
};

static  int wm8350_codec_probe(struct snd_soc_codec *codec)
{
	struct wm8350 *wm8350 = dev_get_platdata(codec->dev);
	struct wm8350_data *priv;
	struct wm8350_output *out1;
	struct wm8350_output *out2;
	int ret, i;

	if (wm8350->codec.platform_data == NULL) {
		dev_err(codec->dev, "No audio platform data supplied\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(codec->dev, sizeof(struct wm8350_data),
			    GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;
	snd_soc_codec_set_drvdata(codec, priv);

	for (i = 0; i < ARRAY_SIZE(supply_names); i++)
		priv->supplies[i].supply = supply_names[i];

	ret = regulator_bulk_get(wm8350->dev, ARRAY_SIZE(priv->supplies),
				 priv->supplies);
	if (ret != 0)
		return ret;

	wm8350->codec.codec = codec;
	codec->control_data = wm8350;

	/* Put the codec into reset if it wasn't already */
	wm8350_clear_bits(wm8350, WM8350_POWER_MGMT_5, WM8350_CODEC_ENA);

	INIT_DELAYED_WORK(&codec->dapm.delayed_work, wm8350_pga_work);
	INIT_DELAYED_WORK(&priv->hpl.work, wm8350_hpl_work);
	INIT_DELAYED_WORK(&priv->hpr.work, wm8350_hpr_work);

	/* Enable the codec */
	wm8350_set_bits(wm8350, WM8350_POWER_MGMT_5, WM8350_CODEC_ENA);

	/* Enable robust clocking mode in ADC */
	wm8350_codec_write(codec, WM8350_SECURITY, 0xa7);
	wm8350_codec_write(codec, 0xde, 0x13);
	wm8350_codec_write(codec, WM8350_SECURITY, 0);

	/* read OUT1 & OUT2 volumes */
	out1 = &priv->out1;
	out2 = &priv->out2;
	out1->left_vol = (wm8350_reg_read(wm8350, WM8350_LOUT1_VOLUME) &
			  WM8350_OUT1L_VOL_MASK) >> WM8350_OUT1L_VOL_SHIFT;
	out1->right_vol = (wm8350_reg_read(wm8350, WM8350_ROUT1_VOLUME) &
			   WM8350_OUT1R_VOL_MASK) >> WM8350_OUT1R_VOL_SHIFT;
	out2->left_vol = (wm8350_reg_read(wm8350, WM8350_LOUT2_VOLUME) &
			  WM8350_OUT2L_VOL_MASK) >> WM8350_OUT1L_VOL_SHIFT;
	out2->right_vol = (wm8350_reg_read(wm8350, WM8350_ROUT2_VOLUME) &
			   WM8350_OUT2R_VOL_MASK) >> WM8350_OUT1R_VOL_SHIFT;
	wm8350_reg_write(wm8350, WM8350_LOUT1_VOLUME, 0);
	wm8350_reg_write(wm8350, WM8350_ROUT1_VOLUME, 0);
	wm8350_reg_write(wm8350, WM8350_LOUT2_VOLUME, 0);
	wm8350_reg_write(wm8350, WM8350_ROUT2_VOLUME, 0);

	/* Latch VU bits & mute */
	wm8350_set_bits(wm8350, WM8350_LOUT1_VOLUME,
			WM8350_OUT1_VU | WM8350_OUT1L_MUTE);
	wm8350_set_bits(wm8350, WM8350_LOUT2_VOLUME,
			WM8350_OUT2_VU | WM8350_OUT2L_MUTE);
	wm8350_set_bits(wm8350, WM8350_ROUT1_VOLUME,
			WM8350_OUT1_VU | WM8350_OUT1R_MUTE);
	wm8350_set_bits(wm8350, WM8350_ROUT2_VOLUME,
			WM8350_OUT2_VU | WM8350_OUT2R_MUTE);

	/* Make sure AIF tristating is disabled by default */
	wm8350_clear_bits(wm8350, WM8350_AI_FORMATING, WM8350_AIF_TRI);

	/* Make sure we've got a sane companding setup too */
	wm8350_clear_bits(wm8350, WM8350_ADC_DAC_COMP,
			  WM8350_DAC_COMP | WM8350_LOOPBACK);

	/* Make sure jack detect is disabled to start off with */
	wm8350_clear_bits(wm8350, WM8350_JACK_DETECT,
			  WM8350_JDL_ENA | WM8350_JDR_ENA);

	wm8350_register_irq(wm8350, WM8350_IRQ_CODEC_JCK_DET_L,
			    wm8350_hp_jack_handler, 0, "Left jack detect",
			    priv);
	wm8350_register_irq(wm8350, WM8350_IRQ_CODEC_JCK_DET_R,
			    wm8350_hp_jack_handler, 0, "Right jack detect",
			    priv);
	wm8350_register_irq(wm8350, WM8350_IRQ_CODEC_MICSCD,
			    wm8350_mic_handler, 0, "Microphone short", priv);
	wm8350_register_irq(wm8350, WM8350_IRQ_CODEC_MICD,
			    wm8350_mic_handler, 0, "Microphone detect", priv);


	wm8350_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

static int  wm8350_codec_remove(struct snd_soc_codec *codec)
{
	struct wm8350_data *priv = snd_soc_codec_get_drvdata(codec);
	struct wm8350 *wm8350 = dev_get_platdata(codec->dev);

	wm8350_clear_bits(wm8350, WM8350_JACK_DETECT,
			  WM8350_JDL_ENA | WM8350_JDR_ENA);
	wm8350_clear_bits(wm8350, WM8350_POWER_MGMT_4, WM8350_TOCLK_ENA);

	wm8350_free_irq(wm8350, WM8350_IRQ_CODEC_MICD, priv);
	wm8350_free_irq(wm8350, WM8350_IRQ_CODEC_MICSCD, priv);
	wm8350_free_irq(wm8350, WM8350_IRQ_CODEC_JCK_DET_L, priv);
	wm8350_free_irq(wm8350, WM8350_IRQ_CODEC_JCK_DET_R, priv);

	priv->hpl.jack = NULL;
	priv->hpr.jack = NULL;
	priv->mic.jack = NULL;

	cancel_delayed_work_sync(&priv->hpl.work);
	cancel_delayed_work_sync(&priv->hpr.work);

	/* if there was any work waiting then we run it now and
	 * wait for its completion */
	flush_delayed_work_sync(&codec->dapm.delayed_work);

	wm8350_set_bias_level(codec, SND_SOC_BIAS_OFF);

	wm8350_clear_bits(wm8350, WM8350_POWER_MGMT_5, WM8350_CODEC_ENA);

	regulator_bulk_free(ARRAY_SIZE(priv->supplies), priv->supplies);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8350 = {
	.probe =	wm8350_codec_probe,
	.remove =	wm8350_codec_remove,
	.suspend = 	wm8350_suspend,
	.resume =	wm8350_resume,
	.read = wm8350_codec_read,
	.write = wm8350_codec_write,
	.set_bias_level = wm8350_set_bias_level,

	.controls = wm8350_snd_controls,
	.num_controls = ARRAY_SIZE(wm8350_snd_controls),
	.dapm_widgets = wm8350_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8350_dapm_widgets),
	.dapm_routes = wm8350_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm8350_dapm_routes),
};

static int __devinit wm8350_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_wm8350,
			&wm8350_dai, 1);
}

static int __devexit wm8350_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver wm8350_codec_driver = {
	.driver = {
		   .name = "wm8350-codec",
		   .owner = THIS_MODULE,
		   },
	.probe = wm8350_probe,
	.remove = __devexit_p(wm8350_remove),
};

module_platform_driver(wm8350_codec_driver);

MODULE_DESCRIPTION("ASoC WM8350 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8350-codec");
