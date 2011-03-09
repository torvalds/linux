/*
 * wm_hubs.c  --  WM8993/4 common code
 *
 * Copyright 2009 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
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
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8993.h"
#include "wm_hubs.h"

const DECLARE_TLV_DB_SCALE(wm_hubs_spkmix_tlv, -300, 300, 0);
EXPORT_SYMBOL_GPL(wm_hubs_spkmix_tlv);

static const DECLARE_TLV_DB_SCALE(inpga_tlv, -1650, 150, 0);
static const DECLARE_TLV_DB_SCALE(inmix_sw_tlv, 0, 3000, 0);
static const DECLARE_TLV_DB_SCALE(inmix_tlv, -1500, 300, 1);
static const DECLARE_TLV_DB_SCALE(earpiece_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(outmix_tlv, -2100, 300, 0);
static const DECLARE_TLV_DB_SCALE(spkmixout_tlv, -1800, 600, 1);
static const DECLARE_TLV_DB_SCALE(outpga_tlv, -5700, 100, 0);
static const unsigned int spkboost_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 6, TLV_DB_SCALE_ITEM(0, 150, 0),
	7, 7, TLV_DB_SCALE_ITEM(1200, 0, 0),
};
static const DECLARE_TLV_DB_SCALE(line_tlv, -600, 600, 0);

static const char *speaker_ref_text[] = {
	"SPKVDD/2",
	"VMID",
};

static const struct soc_enum speaker_ref =
	SOC_ENUM_SINGLE(WM8993_SPEAKER_MIXER, 8, 2, speaker_ref_text);

static const char *speaker_mode_text[] = {
	"Class D",
	"Class AB",
};

static const struct soc_enum speaker_mode =
	SOC_ENUM_SINGLE(WM8993_SPKMIXR_ATTENUATION, 8, 2, speaker_mode_text);

static void wait_for_dc_servo(struct snd_soc_codec *codec, unsigned int op)
{
	unsigned int reg;
	int count = 0;
	unsigned int val;

	val = op | WM8993_DCS_ENA_CHAN_0 | WM8993_DCS_ENA_CHAN_1;

	/* Trigger the command */
	snd_soc_write(codec, WM8993_DC_SERVO_0, val);

	dev_dbg(codec->dev, "Waiting for DC servo...\n");

	do {
		count++;
		msleep(1);
		reg = snd_soc_read(codec, WM8993_DC_SERVO_0);
		dev_dbg(codec->dev, "DC servo: %x\n", reg);
	} while (reg & op && count < 400);

	if (reg & op)
		dev_err(codec->dev, "Timed out waiting for DC Servo\n");
}

/*
 * Startup calibration of the DC servo
 */
static void calibrate_dc_servo(struct snd_soc_codec *codec)
{
	struct wm_hubs_data *hubs = snd_soc_codec_get_drvdata(codec);
	u16 reg, reg_l, reg_r, dcs_cfg;

	/* If we're using a digital only path and have a previously
	 * callibrated DC servo offset stored then use that. */
	if (hubs->class_w && hubs->class_w_dcs) {
		dev_dbg(codec->dev, "Using cached DC servo offset %x\n",
			hubs->class_w_dcs);
		snd_soc_write(codec, WM8993_DC_SERVO_3, hubs->class_w_dcs);
		wait_for_dc_servo(codec,
				  WM8993_DCS_TRIG_DAC_WR_0 |
				  WM8993_DCS_TRIG_DAC_WR_1);
		return;
	}

	/* Devices not using a DCS code correction have startup mode */
	if (hubs->dcs_codes) {
		/* Set for 32 series updates */
		snd_soc_update_bits(codec, WM8993_DC_SERVO_1,
				    WM8993_DCS_SERIES_NO_01_MASK,
				    32 << WM8993_DCS_SERIES_NO_01_SHIFT);
		wait_for_dc_servo(codec,
				  WM8993_DCS_TRIG_SERIES_0 |
				  WM8993_DCS_TRIG_SERIES_1);
	} else {
		wait_for_dc_servo(codec,
				  WM8993_DCS_TRIG_STARTUP_0 |
				  WM8993_DCS_TRIG_STARTUP_1);
	}

	/* Different chips in the family support different readback
	 * methods.
	 */
	switch (hubs->dcs_readback_mode) {
	case 0:
		reg_l = snd_soc_read(codec, WM8993_DC_SERVO_READBACK_1)
			& WM8993_DCS_INTEG_CHAN_0_MASK;
		reg_r = snd_soc_read(codec, WM8993_DC_SERVO_READBACK_2)
			& WM8993_DCS_INTEG_CHAN_1_MASK;
		break;
	case 1:
		reg = snd_soc_read(codec, WM8993_DC_SERVO_3);
		reg_l = (reg & WM8993_DCS_DAC_WR_VAL_1_MASK)
			>> WM8993_DCS_DAC_WR_VAL_1_SHIFT;
		reg_r = reg & WM8993_DCS_DAC_WR_VAL_0_MASK;
		break;
	default:
		WARN(1, "Unknown DCS readback method\n");
		break;
	}

	dev_dbg(codec->dev, "DCS input: %x %x\n", reg_l, reg_r);

	/* Apply correction to DC servo result */
	if (hubs->dcs_codes) {
		dev_dbg(codec->dev, "Applying %d code DC servo correction\n",
			hubs->dcs_codes);

		/* HPOUT1L */
		if (reg_l + hubs->dcs_codes > 0 &&
		    reg_l + hubs->dcs_codes < 0xff)
			reg_l += hubs->dcs_codes;
		dcs_cfg = reg_l << WM8993_DCS_DAC_WR_VAL_1_SHIFT;

		/* HPOUT1R */
		if (reg_r + hubs->dcs_codes > 0 &&
		    reg_r + hubs->dcs_codes < 0xff)
			reg_r += hubs->dcs_codes;
		dcs_cfg |= reg_r;

		dev_dbg(codec->dev, "DCS result: %x\n", dcs_cfg);

		/* Do it */
		snd_soc_write(codec, WM8993_DC_SERVO_3, dcs_cfg);
		wait_for_dc_servo(codec,
				  WM8993_DCS_TRIG_DAC_WR_0 |
				  WM8993_DCS_TRIG_DAC_WR_1);
	} else {
		dcs_cfg = reg_l << WM8993_DCS_DAC_WR_VAL_1_SHIFT;
		dcs_cfg |= reg_r;
	}

	/* Save the callibrated offset if we're in class W mode and
	 * therefore don't have any analogue signal mixed in. */
	if (hubs->class_w)
		hubs->class_w_dcs = dcs_cfg;
}

/*
 * Update the DC servo calibration on gain changes
 */
static int wm8993_put_dc_servo(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm_hubs_data *hubs = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = snd_soc_put_volsw_2r(kcontrol, ucontrol);

	/* Updating the analogue gains invalidates the DC servo cache */
	hubs->class_w_dcs = 0;

	/* If we're applying an offset correction then updating the
	 * callibration would be likely to introduce further offsets. */
	if (hubs->dcs_codes)
		return ret;

	/* Only need to do this if the outputs are active */
	if (snd_soc_read(codec, WM8993_POWER_MANAGEMENT_1)
	    & (WM8993_HPOUT1L_ENA | WM8993_HPOUT1R_ENA))
		snd_soc_update_bits(codec,
				    WM8993_DC_SERVO_0,
				    WM8993_DCS_TRIG_SINGLE_0 |
				    WM8993_DCS_TRIG_SINGLE_1,
				    WM8993_DCS_TRIG_SINGLE_0 |
				    WM8993_DCS_TRIG_SINGLE_1);

	return ret;
}

static const struct snd_kcontrol_new analogue_snd_controls[] = {
SOC_SINGLE_TLV("IN1L Volume", WM8993_LEFT_LINE_INPUT_1_2_VOLUME, 0, 31, 0,
	       inpga_tlv),
SOC_SINGLE("IN1L Switch", WM8993_LEFT_LINE_INPUT_1_2_VOLUME, 7, 1, 1),
SOC_SINGLE("IN1L ZC Switch", WM8993_LEFT_LINE_INPUT_1_2_VOLUME, 7, 1, 0),

SOC_SINGLE_TLV("IN1R Volume", WM8993_RIGHT_LINE_INPUT_1_2_VOLUME, 0, 31, 0,
	       inpga_tlv),
SOC_SINGLE("IN1R Switch", WM8993_RIGHT_LINE_INPUT_1_2_VOLUME, 7, 1, 1),
SOC_SINGLE("IN1R ZC Switch", WM8993_RIGHT_LINE_INPUT_1_2_VOLUME, 7, 1, 0),


SOC_SINGLE_TLV("IN2L Volume", WM8993_LEFT_LINE_INPUT_3_4_VOLUME, 0, 31, 0,
	       inpga_tlv),
SOC_SINGLE("IN2L Switch", WM8993_LEFT_LINE_INPUT_3_4_VOLUME, 7, 1, 1),
SOC_SINGLE("IN2L ZC Switch", WM8993_LEFT_LINE_INPUT_3_4_VOLUME, 7, 1, 0),

SOC_SINGLE_TLV("IN2R Volume", WM8993_RIGHT_LINE_INPUT_3_4_VOLUME, 0, 31, 0,
	       inpga_tlv),
SOC_SINGLE("IN2R Switch", WM8993_RIGHT_LINE_INPUT_3_4_VOLUME, 7, 1, 1),
SOC_SINGLE("IN2R ZC Switch", WM8993_RIGHT_LINE_INPUT_3_4_VOLUME, 7, 1, 0),

SOC_SINGLE_TLV("MIXINL IN2L Volume", WM8993_INPUT_MIXER3, 7, 1, 0,
	       inmix_sw_tlv),
SOC_SINGLE_TLV("MIXINL IN1L Volume", WM8993_INPUT_MIXER3, 4, 1, 0,
	       inmix_sw_tlv),
SOC_SINGLE_TLV("MIXINL Output Record Volume", WM8993_INPUT_MIXER3, 0, 7, 0,
	       inmix_tlv),
SOC_SINGLE_TLV("MIXINL IN1LP Volume", WM8993_INPUT_MIXER5, 6, 7, 0, inmix_tlv),
SOC_SINGLE_TLV("MIXINL Direct Voice Volume", WM8993_INPUT_MIXER5, 0, 6, 0,
	       inmix_tlv),

SOC_SINGLE_TLV("MIXINR IN2R Volume", WM8993_INPUT_MIXER4, 7, 1, 0,
	       inmix_sw_tlv),
SOC_SINGLE_TLV("MIXINR IN1R Volume", WM8993_INPUT_MIXER4, 4, 1, 0,
	       inmix_sw_tlv),
SOC_SINGLE_TLV("MIXINR Output Record Volume", WM8993_INPUT_MIXER4, 0, 7, 0,
	       inmix_tlv),
SOC_SINGLE_TLV("MIXINR IN1RP Volume", WM8993_INPUT_MIXER6, 6, 7, 0, inmix_tlv),
SOC_SINGLE_TLV("MIXINR Direct Voice Volume", WM8993_INPUT_MIXER6, 0, 6, 0,
	       inmix_tlv),

SOC_SINGLE_TLV("Left Output Mixer IN2RN Volume", WM8993_OUTPUT_MIXER5, 6, 7, 1,
	       outmix_tlv),
SOC_SINGLE_TLV("Left Output Mixer IN2LN Volume", WM8993_OUTPUT_MIXER3, 6, 7, 1,
	       outmix_tlv),
SOC_SINGLE_TLV("Left Output Mixer IN2LP Volume", WM8993_OUTPUT_MIXER3, 9, 7, 1,
	       outmix_tlv),
SOC_SINGLE_TLV("Left Output Mixer IN1L Volume", WM8993_OUTPUT_MIXER3, 0, 7, 1,
	       outmix_tlv),
SOC_SINGLE_TLV("Left Output Mixer IN1R Volume", WM8993_OUTPUT_MIXER3, 3, 7, 1,
	       outmix_tlv),
SOC_SINGLE_TLV("Left Output Mixer Right Input Volume",
	       WM8993_OUTPUT_MIXER5, 3, 7, 1, outmix_tlv),
SOC_SINGLE_TLV("Left Output Mixer Left Input Volume",
	       WM8993_OUTPUT_MIXER5, 0, 7, 1, outmix_tlv),
SOC_SINGLE_TLV("Left Output Mixer DAC Volume", WM8993_OUTPUT_MIXER5, 9, 7, 1,
	       outmix_tlv),

SOC_SINGLE_TLV("Right Output Mixer IN2LN Volume",
	       WM8993_OUTPUT_MIXER6, 6, 7, 1, outmix_tlv),
SOC_SINGLE_TLV("Right Output Mixer IN2RN Volume",
	       WM8993_OUTPUT_MIXER4, 6, 7, 1, outmix_tlv),
SOC_SINGLE_TLV("Right Output Mixer IN1L Volume",
	       WM8993_OUTPUT_MIXER4, 3, 7, 1, outmix_tlv),
SOC_SINGLE_TLV("Right Output Mixer IN1R Volume",
	       WM8993_OUTPUT_MIXER4, 0, 7, 1, outmix_tlv),
SOC_SINGLE_TLV("Right Output Mixer IN2RP Volume",
	       WM8993_OUTPUT_MIXER4, 9, 7, 1, outmix_tlv),
SOC_SINGLE_TLV("Right Output Mixer Left Input Volume",
	       WM8993_OUTPUT_MIXER6, 3, 7, 1, outmix_tlv),
SOC_SINGLE_TLV("Right Output Mixer Right Input Volume",
	       WM8993_OUTPUT_MIXER6, 6, 7, 1, outmix_tlv),
SOC_SINGLE_TLV("Right Output Mixer DAC Volume",
	       WM8993_OUTPUT_MIXER6, 9, 7, 1, outmix_tlv),

SOC_DOUBLE_R_TLV("Output Volume", WM8993_LEFT_OPGA_VOLUME,
		 WM8993_RIGHT_OPGA_VOLUME, 0, 63, 0, outpga_tlv),
SOC_DOUBLE_R("Output Switch", WM8993_LEFT_OPGA_VOLUME,
	     WM8993_RIGHT_OPGA_VOLUME, 6, 1, 0),
SOC_DOUBLE_R("Output ZC Switch", WM8993_LEFT_OPGA_VOLUME,
	     WM8993_RIGHT_OPGA_VOLUME, 7, 1, 0),

SOC_SINGLE("Earpiece Switch", WM8993_HPOUT2_VOLUME, 5, 1, 1),
SOC_SINGLE_TLV("Earpiece Volume", WM8993_HPOUT2_VOLUME, 4, 1, 1, earpiece_tlv),

SOC_SINGLE_TLV("SPKL Input Volume", WM8993_SPKMIXL_ATTENUATION,
	       5, 1, 1, wm_hubs_spkmix_tlv),
SOC_SINGLE_TLV("SPKL IN1LP Volume", WM8993_SPKMIXL_ATTENUATION,
	       4, 1, 1, wm_hubs_spkmix_tlv),
SOC_SINGLE_TLV("SPKL Output Volume", WM8993_SPKMIXL_ATTENUATION,
	       3, 1, 1, wm_hubs_spkmix_tlv),

SOC_SINGLE_TLV("SPKR Input Volume", WM8993_SPKMIXR_ATTENUATION,
	       5, 1, 1, wm_hubs_spkmix_tlv),
SOC_SINGLE_TLV("SPKR IN1RP Volume", WM8993_SPKMIXR_ATTENUATION,
	       4, 1, 1, wm_hubs_spkmix_tlv),
SOC_SINGLE_TLV("SPKR Output Volume", WM8993_SPKMIXR_ATTENUATION,
	       3, 1, 1, wm_hubs_spkmix_tlv),

SOC_DOUBLE_R_TLV("Speaker Mixer Volume",
		 WM8993_SPKMIXL_ATTENUATION, WM8993_SPKMIXR_ATTENUATION,
		 0, 3, 1, spkmixout_tlv),
SOC_DOUBLE_R_TLV("Speaker Volume",
		 WM8993_SPEAKER_VOLUME_LEFT, WM8993_SPEAKER_VOLUME_RIGHT,
		 0, 63, 0, outpga_tlv),
SOC_DOUBLE_R("Speaker Switch",
	     WM8993_SPEAKER_VOLUME_LEFT, WM8993_SPEAKER_VOLUME_RIGHT,
	     6, 1, 0),
SOC_DOUBLE_R("Speaker ZC Switch",
	     WM8993_SPEAKER_VOLUME_LEFT, WM8993_SPEAKER_VOLUME_RIGHT,
	     7, 1, 0),
SOC_DOUBLE_TLV("Speaker Boost Volume", WM8993_SPKOUT_BOOST, 3, 0, 7, 0,
	       spkboost_tlv),
SOC_ENUM("Speaker Reference", speaker_ref),
SOC_ENUM("Speaker Mode", speaker_mode),

{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = "Headphone Volume",
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.tlv.p = outpga_tlv,
	.info = snd_soc_info_volsw_2r,
	.get = snd_soc_get_volsw_2r, .put = wm8993_put_dc_servo,
	.private_value = (unsigned long)&(struct soc_mixer_control) {
		.reg = WM8993_LEFT_OUTPUT_VOLUME,
		.rreg = WM8993_RIGHT_OUTPUT_VOLUME,
		.shift = 0, .max = 63
	},
},
SOC_DOUBLE_R("Headphone Switch", WM8993_LEFT_OUTPUT_VOLUME,
	     WM8993_RIGHT_OUTPUT_VOLUME, 6, 1, 0),
SOC_DOUBLE_R("Headphone ZC Switch", WM8993_LEFT_OUTPUT_VOLUME,
	     WM8993_RIGHT_OUTPUT_VOLUME, 7, 1, 0),

SOC_SINGLE("LINEOUT1N Switch", WM8993_LINE_OUTPUTS_VOLUME, 6, 1, 1),
SOC_SINGLE("LINEOUT1P Switch", WM8993_LINE_OUTPUTS_VOLUME, 5, 1, 1),
SOC_SINGLE_TLV("LINEOUT1 Volume", WM8993_LINE_OUTPUTS_VOLUME, 4, 1, 1,
	       line_tlv),

SOC_SINGLE("LINEOUT2N Switch", WM8993_LINE_OUTPUTS_VOLUME, 2, 1, 1),
SOC_SINGLE("LINEOUT2P Switch", WM8993_LINE_OUTPUTS_VOLUME, 1, 1, 1),
SOC_SINGLE_TLV("LINEOUT2 Volume", WM8993_LINE_OUTPUTS_VOLUME, 0, 1, 1,
	       line_tlv),
};

static int hp_supply_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct wm_hubs_data *hubs = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (hubs->hp_startup_mode) {
		case 0:
			break;
		case 1:
			/* Enable the headphone amp */
			snd_soc_update_bits(codec, WM8993_POWER_MANAGEMENT_1,
					    WM8993_HPOUT1L_ENA |
					    WM8993_HPOUT1R_ENA,
					    WM8993_HPOUT1L_ENA |
					    WM8993_HPOUT1R_ENA);

			/* Enable the second stage */
			snd_soc_update_bits(codec, WM8993_ANALOGUE_HP_0,
					    WM8993_HPOUT1L_DLY |
					    WM8993_HPOUT1R_DLY,
					    WM8993_HPOUT1L_DLY |
					    WM8993_HPOUT1R_DLY);
			break;
		default:
			dev_err(codec->dev, "Unknown HP startup mode %d\n",
				hubs->hp_startup_mode);
			break;
		}

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, WM8993_CHARGE_PUMP_1,
				    WM8993_CP_ENA, 0);
		break;
	}

	return 0;
}

static int hp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int reg = snd_soc_read(codec, WM8993_ANALOGUE_HP_0);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, WM8993_CHARGE_PUMP_1,
				    WM8993_CP_ENA, WM8993_CP_ENA);

		msleep(5);

		snd_soc_update_bits(codec, WM8993_POWER_MANAGEMENT_1,
				    WM8993_HPOUT1L_ENA | WM8993_HPOUT1R_ENA,
				    WM8993_HPOUT1L_ENA | WM8993_HPOUT1R_ENA);

		reg |= WM8993_HPOUT1L_DLY | WM8993_HPOUT1R_DLY;
		snd_soc_write(codec, WM8993_ANALOGUE_HP_0, reg);

		/* Smallest supported update interval */
		snd_soc_update_bits(codec, WM8993_DC_SERVO_1,
				    WM8993_DCS_TIMER_PERIOD_01_MASK, 1);

		calibrate_dc_servo(codec);

		reg |= WM8993_HPOUT1R_OUTP | WM8993_HPOUT1R_RMV_SHORT |
			WM8993_HPOUT1L_OUTP | WM8993_HPOUT1L_RMV_SHORT;
		snd_soc_write(codec, WM8993_ANALOGUE_HP_0, reg);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, WM8993_ANALOGUE_HP_0,
				    WM8993_HPOUT1L_OUTP |
				    WM8993_HPOUT1R_OUTP |
				    WM8993_HPOUT1L_RMV_SHORT |
				    WM8993_HPOUT1R_RMV_SHORT, 0);

		snd_soc_update_bits(codec, WM8993_ANALOGUE_HP_0,
				    WM8993_HPOUT1L_DLY |
				    WM8993_HPOUT1R_DLY, 0);

		snd_soc_write(codec, WM8993_DC_SERVO_0, 0);

		snd_soc_update_bits(codec, WM8993_POWER_MANAGEMENT_1,
				    WM8993_HPOUT1L_ENA | WM8993_HPOUT1R_ENA,
				    0);
		break;
	}

	return 0;
}

static int earpiece_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *control, int event)
{
	struct snd_soc_codec *codec = w->codec;
	u16 reg = snd_soc_read(codec, WM8993_ANTIPOP1) & ~WM8993_HPOUT2_IN_ENA;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		reg |= WM8993_HPOUT2_IN_ENA;
		snd_soc_write(codec, WM8993_ANTIPOP1, reg);
		udelay(50);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_write(codec, WM8993_ANTIPOP1, reg);
		break;

	default:
		BUG();
		break;
	}

	return 0;
}

static const struct snd_kcontrol_new in1l_pga[] = {
SOC_DAPM_SINGLE("IN1LP Switch", WM8993_INPUT_MIXER2, 5, 1, 0),
SOC_DAPM_SINGLE("IN1LN Switch", WM8993_INPUT_MIXER2, 4, 1, 0),
};

static const struct snd_kcontrol_new in1r_pga[] = {
SOC_DAPM_SINGLE("IN1RP Switch", WM8993_INPUT_MIXER2, 1, 1, 0),
SOC_DAPM_SINGLE("IN1RN Switch", WM8993_INPUT_MIXER2, 0, 1, 0),
};

static const struct snd_kcontrol_new in2l_pga[] = {
SOC_DAPM_SINGLE("IN2LP Switch", WM8993_INPUT_MIXER2, 7, 1, 0),
SOC_DAPM_SINGLE("IN2LN Switch", WM8993_INPUT_MIXER2, 6, 1, 0),
};

static const struct snd_kcontrol_new in2r_pga[] = {
SOC_DAPM_SINGLE("IN2RP Switch", WM8993_INPUT_MIXER2, 3, 1, 0),
SOC_DAPM_SINGLE("IN2RN Switch", WM8993_INPUT_MIXER2, 2, 1, 0),
};

static const struct snd_kcontrol_new mixinl[] = {
SOC_DAPM_SINGLE("IN2L Switch", WM8993_INPUT_MIXER3, 8, 1, 0),
SOC_DAPM_SINGLE("IN1L Switch", WM8993_INPUT_MIXER3, 5, 1, 0),
};

static const struct snd_kcontrol_new mixinr[] = {
SOC_DAPM_SINGLE("IN2R Switch", WM8993_INPUT_MIXER4, 8, 1, 0),
SOC_DAPM_SINGLE("IN1R Switch", WM8993_INPUT_MIXER4, 5, 1, 0),
};

static const struct snd_kcontrol_new left_output_mixer[] = {
SOC_DAPM_SINGLE("Right Input Switch", WM8993_OUTPUT_MIXER1, 7, 1, 0),
SOC_DAPM_SINGLE("Left Input Switch", WM8993_OUTPUT_MIXER1, 6, 1, 0),
SOC_DAPM_SINGLE("IN2RN Switch", WM8993_OUTPUT_MIXER1, 5, 1, 0),
SOC_DAPM_SINGLE("IN2LN Switch", WM8993_OUTPUT_MIXER1, 4, 1, 0),
SOC_DAPM_SINGLE("IN2LP Switch", WM8993_OUTPUT_MIXER1, 1, 1, 0),
SOC_DAPM_SINGLE("IN1R Switch", WM8993_OUTPUT_MIXER1, 3, 1, 0),
SOC_DAPM_SINGLE("IN1L Switch", WM8993_OUTPUT_MIXER1, 2, 1, 0),
SOC_DAPM_SINGLE("DAC Switch", WM8993_OUTPUT_MIXER1, 0, 1, 0),
};

static const struct snd_kcontrol_new right_output_mixer[] = {
SOC_DAPM_SINGLE("Left Input Switch", WM8993_OUTPUT_MIXER2, 7, 1, 0),
SOC_DAPM_SINGLE("Right Input Switch", WM8993_OUTPUT_MIXER2, 6, 1, 0),
SOC_DAPM_SINGLE("IN2LN Switch", WM8993_OUTPUT_MIXER2, 5, 1, 0),
SOC_DAPM_SINGLE("IN2RN Switch", WM8993_OUTPUT_MIXER2, 4, 1, 0),
SOC_DAPM_SINGLE("IN1L Switch", WM8993_OUTPUT_MIXER2, 3, 1, 0),
SOC_DAPM_SINGLE("IN1R Switch", WM8993_OUTPUT_MIXER2, 2, 1, 0),
SOC_DAPM_SINGLE("IN2RP Switch", WM8993_OUTPUT_MIXER2, 1, 1, 0),
SOC_DAPM_SINGLE("DAC Switch", WM8993_OUTPUT_MIXER2, 0, 1, 0),
};

static const struct snd_kcontrol_new earpiece_mixer[] = {
SOC_DAPM_SINGLE("Direct Voice Switch", WM8993_HPOUT2_MIXER, 5, 1, 0),
SOC_DAPM_SINGLE("Left Output Switch", WM8993_HPOUT2_MIXER, 4, 1, 0),
SOC_DAPM_SINGLE("Right Output Switch", WM8993_HPOUT2_MIXER, 3, 1, 0),
};

static const struct snd_kcontrol_new left_speaker_boost[] = {
SOC_DAPM_SINGLE("Direct Voice Switch", WM8993_SPKOUT_MIXERS, 5, 1, 0),
SOC_DAPM_SINGLE("SPKL Switch", WM8993_SPKOUT_MIXERS, 4, 1, 0),
SOC_DAPM_SINGLE("SPKR Switch", WM8993_SPKOUT_MIXERS, 3, 1, 0),
};

static const struct snd_kcontrol_new right_speaker_boost[] = {
SOC_DAPM_SINGLE("Direct Voice Switch", WM8993_SPKOUT_MIXERS, 2, 1, 0),
SOC_DAPM_SINGLE("SPKL Switch", WM8993_SPKOUT_MIXERS, 1, 1, 0),
SOC_DAPM_SINGLE("SPKR Switch", WM8993_SPKOUT_MIXERS, 0, 1, 0),
};

static const struct snd_kcontrol_new line1_mix[] = {
SOC_DAPM_SINGLE("IN1R Switch", WM8993_LINE_MIXER1, 2, 1, 0),
SOC_DAPM_SINGLE("IN1L Switch", WM8993_LINE_MIXER1, 1, 1, 0),
SOC_DAPM_SINGLE("Output Switch", WM8993_LINE_MIXER1, 0, 1, 0),
};

static const struct snd_kcontrol_new line1n_mix[] = {
SOC_DAPM_SINGLE("Left Output Switch", WM8993_LINE_MIXER1, 6, 1, 0),
SOC_DAPM_SINGLE("Right Output Switch", WM8993_LINE_MIXER1, 5, 1, 0),
};

static const struct snd_kcontrol_new line1p_mix[] = {
SOC_DAPM_SINGLE("Left Output Switch", WM8993_LINE_MIXER1, 0, 1, 0),
};

static const struct snd_kcontrol_new line2_mix[] = {
SOC_DAPM_SINGLE("IN2R Switch", WM8993_LINE_MIXER2, 2, 1, 0),
SOC_DAPM_SINGLE("IN2L Switch", WM8993_LINE_MIXER2, 1, 1, 0),
SOC_DAPM_SINGLE("Output Switch", WM8993_LINE_MIXER2, 0, 1, 0),
};

static const struct snd_kcontrol_new line2n_mix[] = {
SOC_DAPM_SINGLE("Left Output Switch", WM8993_LINE_MIXER2, 6, 1, 0),
SOC_DAPM_SINGLE("Right Output Switch", WM8993_LINE_MIXER2, 5, 1, 0),
};

static const struct snd_kcontrol_new line2p_mix[] = {
SOC_DAPM_SINGLE("Right Output Switch", WM8993_LINE_MIXER2, 0, 1, 0),
};

static const struct snd_soc_dapm_widget analogue_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1LN"),
SND_SOC_DAPM_INPUT("IN1LP"),
SND_SOC_DAPM_INPUT("IN2LN"),
SND_SOC_DAPM_INPUT("IN2LP:VXRN"),
SND_SOC_DAPM_INPUT("IN1RN"),
SND_SOC_DAPM_INPUT("IN1RP"),
SND_SOC_DAPM_INPUT("IN2RN"),
SND_SOC_DAPM_INPUT("IN2RP:VXRP"),

SND_SOC_DAPM_MICBIAS("MICBIAS2", WM8993_POWER_MANAGEMENT_1, 5, 0),
SND_SOC_DAPM_MICBIAS("MICBIAS1", WM8993_POWER_MANAGEMENT_1, 4, 0),

SND_SOC_DAPM_MIXER("IN1L PGA", WM8993_POWER_MANAGEMENT_2, 6, 0,
		   in1l_pga, ARRAY_SIZE(in1l_pga)),
SND_SOC_DAPM_MIXER("IN1R PGA", WM8993_POWER_MANAGEMENT_2, 4, 0,
		   in1r_pga, ARRAY_SIZE(in1r_pga)),

SND_SOC_DAPM_MIXER("IN2L PGA", WM8993_POWER_MANAGEMENT_2, 7, 0,
		   in2l_pga, ARRAY_SIZE(in2l_pga)),
SND_SOC_DAPM_MIXER("IN2R PGA", WM8993_POWER_MANAGEMENT_2, 5, 0,
		   in2r_pga, ARRAY_SIZE(in2r_pga)),

/* Dummy widgets to represent differential paths */
SND_SOC_DAPM_PGA("Direct Voice", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MIXER("MIXINL", WM8993_POWER_MANAGEMENT_2, 9, 0,
		   mixinl, ARRAY_SIZE(mixinl)),
SND_SOC_DAPM_MIXER("MIXINR", WM8993_POWER_MANAGEMENT_2, 8, 0,
		   mixinr, ARRAY_SIZE(mixinr)),

SND_SOC_DAPM_MIXER("Left Output Mixer", WM8993_POWER_MANAGEMENT_3, 5, 0,
		   left_output_mixer, ARRAY_SIZE(left_output_mixer)),
SND_SOC_DAPM_MIXER("Right Output Mixer", WM8993_POWER_MANAGEMENT_3, 4, 0,
		   right_output_mixer, ARRAY_SIZE(right_output_mixer)),

SND_SOC_DAPM_PGA("Left Output PGA", WM8993_POWER_MANAGEMENT_3, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Output PGA", WM8993_POWER_MANAGEMENT_3, 6, 0, NULL, 0),

SND_SOC_DAPM_SUPPLY("Headphone Supply", SND_SOC_NOPM, 0, 0, hp_supply_event, 
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA_E("Headphone PGA", SND_SOC_NOPM, 0, 0,
		   NULL, 0,
		   hp_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_MIXER("Earpiece Mixer", SND_SOC_NOPM, 0, 0,
		   earpiece_mixer, ARRAY_SIZE(earpiece_mixer)),
SND_SOC_DAPM_PGA_E("Earpiece Driver", WM8993_POWER_MANAGEMENT_1, 11, 0,
		   NULL, 0, earpiece_event,
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

SND_SOC_DAPM_MIXER("SPKL Boost", SND_SOC_NOPM, 0, 0,
		   left_speaker_boost, ARRAY_SIZE(left_speaker_boost)),
SND_SOC_DAPM_MIXER("SPKR Boost", SND_SOC_NOPM, 0, 0,
		   right_speaker_boost, ARRAY_SIZE(right_speaker_boost)),

SND_SOC_DAPM_PGA("SPKL Driver", WM8993_POWER_MANAGEMENT_1, 12, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("SPKR Driver", WM8993_POWER_MANAGEMENT_1, 13, 0,
		 NULL, 0),

SND_SOC_DAPM_MIXER("LINEOUT1 Mixer", SND_SOC_NOPM, 0, 0,
		   line1_mix, ARRAY_SIZE(line1_mix)),
SND_SOC_DAPM_MIXER("LINEOUT2 Mixer", SND_SOC_NOPM, 0, 0,
		   line2_mix, ARRAY_SIZE(line2_mix)),

SND_SOC_DAPM_MIXER("LINEOUT1N Mixer", SND_SOC_NOPM, 0, 0,
		   line1n_mix, ARRAY_SIZE(line1n_mix)),
SND_SOC_DAPM_MIXER("LINEOUT1P Mixer", SND_SOC_NOPM, 0, 0,
		   line1p_mix, ARRAY_SIZE(line1p_mix)),
SND_SOC_DAPM_MIXER("LINEOUT2N Mixer", SND_SOC_NOPM, 0, 0,
		   line2n_mix, ARRAY_SIZE(line2n_mix)),
SND_SOC_DAPM_MIXER("LINEOUT2P Mixer", SND_SOC_NOPM, 0, 0,
		   line2p_mix, ARRAY_SIZE(line2p_mix)),

SND_SOC_DAPM_PGA("LINEOUT1N Driver", WM8993_POWER_MANAGEMENT_3, 13, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LINEOUT1P Driver", WM8993_POWER_MANAGEMENT_3, 12, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LINEOUT2N Driver", WM8993_POWER_MANAGEMENT_3, 11, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LINEOUT2P Driver", WM8993_POWER_MANAGEMENT_3, 10, 0,
		 NULL, 0),

SND_SOC_DAPM_OUTPUT("SPKOUTLP"),
SND_SOC_DAPM_OUTPUT("SPKOUTLN"),
SND_SOC_DAPM_OUTPUT("SPKOUTRP"),
SND_SOC_DAPM_OUTPUT("SPKOUTRN"),
SND_SOC_DAPM_OUTPUT("HPOUT1L"),
SND_SOC_DAPM_OUTPUT("HPOUT1R"),
SND_SOC_DAPM_OUTPUT("HPOUT2P"),
SND_SOC_DAPM_OUTPUT("HPOUT2N"),
SND_SOC_DAPM_OUTPUT("LINEOUT1P"),
SND_SOC_DAPM_OUTPUT("LINEOUT1N"),
SND_SOC_DAPM_OUTPUT("LINEOUT2P"),
SND_SOC_DAPM_OUTPUT("LINEOUT2N"),
};

static const struct snd_soc_dapm_route analogue_routes[] = {
	{ "IN1L PGA", "IN1LP Switch", "IN1LP" },
	{ "IN1L PGA", "IN1LN Switch", "IN1LN" },

	{ "IN1R PGA", "IN1RP Switch", "IN1RP" },
	{ "IN1R PGA", "IN1RN Switch", "IN1RN" },

	{ "IN2L PGA", "IN2LP Switch", "IN2LP:VXRN" },
	{ "IN2L PGA", "IN2LN Switch", "IN2LN" },

	{ "IN2R PGA", "IN2RP Switch", "IN2RP:VXRP" },
	{ "IN2R PGA", "IN2RN Switch", "IN2RN" },

	{ "Direct Voice", NULL, "IN2LP:VXRN" },
	{ "Direct Voice", NULL, "IN2RP:VXRP" },

	{ "MIXINL", "IN1L Switch", "IN1L PGA" },
	{ "MIXINL", "IN2L Switch", "IN2L PGA" },
	{ "MIXINL", NULL, "Direct Voice" },
	{ "MIXINL", NULL, "IN1LP" },
	{ "MIXINL", NULL, "Left Output Mixer" },

	{ "MIXINR", "IN1R Switch", "IN1R PGA" },
	{ "MIXINR", "IN2R Switch", "IN2R PGA" },
	{ "MIXINR", NULL, "Direct Voice" },
	{ "MIXINR", NULL, "IN1RP" },
	{ "MIXINR", NULL, "Right Output Mixer" },

	{ "ADCL", NULL, "MIXINL" },
	{ "ADCR", NULL, "MIXINR" },

	{ "Left Output Mixer", "Left Input Switch", "MIXINL" },
	{ "Left Output Mixer", "Right Input Switch", "MIXINR" },
	{ "Left Output Mixer", "IN2RN Switch", "IN2RN" },
	{ "Left Output Mixer", "IN2LN Switch", "IN2LN" },
	{ "Left Output Mixer", "IN2LP Switch", "IN2LP:VXRN" },
	{ "Left Output Mixer", "IN1L Switch", "IN1L PGA" },
	{ "Left Output Mixer", "IN1R Switch", "IN1R PGA" },

	{ "Right Output Mixer", "Left Input Switch", "MIXINL" },
	{ "Right Output Mixer", "Right Input Switch", "MIXINR" },
	{ "Right Output Mixer", "IN2LN Switch", "IN2LN" },
	{ "Right Output Mixer", "IN2RN Switch", "IN2RN" },
	{ "Right Output Mixer", "IN2RP Switch", "IN2RP:VXRP" },
	{ "Right Output Mixer", "IN1L Switch", "IN1L PGA" },
	{ "Right Output Mixer", "IN1R Switch", "IN1R PGA" },

	{ "Left Output PGA", NULL, "Left Output Mixer" },
	{ "Left Output PGA", NULL, "TOCLK" },

	{ "Right Output PGA", NULL, "Right Output Mixer" },
	{ "Right Output PGA", NULL, "TOCLK" },

	{ "Earpiece Mixer", "Direct Voice Switch", "Direct Voice" },
	{ "Earpiece Mixer", "Left Output Switch", "Left Output PGA" },
	{ "Earpiece Mixer", "Right Output Switch", "Right Output PGA" },

	{ "Earpiece Driver", NULL, "Earpiece Mixer" },
	{ "HPOUT2N", NULL, "Earpiece Driver" },
	{ "HPOUT2P", NULL, "Earpiece Driver" },

	{ "SPKL", "Input Switch", "MIXINL" },
	{ "SPKL", "IN1LP Switch", "IN1LP" },
	{ "SPKL", "Output Switch", "Left Output Mixer" },
	{ "SPKL", NULL, "TOCLK" },

	{ "SPKR", "Input Switch", "MIXINR" },
	{ "SPKR", "IN1RP Switch", "IN1RP" },
	{ "SPKR", "Output Switch", "Right Output Mixer" },
	{ "SPKR", NULL, "TOCLK" },

	{ "SPKL Boost", "Direct Voice Switch", "Direct Voice" },
	{ "SPKL Boost", "SPKL Switch", "SPKL" },
	{ "SPKL Boost", "SPKR Switch", "SPKR" },

	{ "SPKR Boost", "Direct Voice Switch", "Direct Voice" },
	{ "SPKR Boost", "SPKR Switch", "SPKR" },
	{ "SPKR Boost", "SPKL Switch", "SPKL" },

	{ "SPKL Driver", NULL, "SPKL Boost" },
	{ "SPKL Driver", NULL, "CLK_SYS" },

	{ "SPKR Driver", NULL, "SPKR Boost" },
	{ "SPKR Driver", NULL, "CLK_SYS" },

	{ "SPKOUTLP", NULL, "SPKL Driver" },
	{ "SPKOUTLN", NULL, "SPKL Driver" },
	{ "SPKOUTRP", NULL, "SPKR Driver" },
	{ "SPKOUTRN", NULL, "SPKR Driver" },

	{ "Left Headphone Mux", "Mixer", "Left Output Mixer" },
	{ "Right Headphone Mux", "Mixer", "Right Output Mixer" },

	{ "Headphone PGA", NULL, "Left Headphone Mux" },
	{ "Headphone PGA", NULL, "Right Headphone Mux" },
	{ "Headphone PGA", NULL, "CLK_SYS" },
	{ "Headphone PGA", NULL, "Headphone Supply" },

	{ "HPOUT1L", NULL, "Headphone PGA" },
	{ "HPOUT1R", NULL, "Headphone PGA" },

	{ "LINEOUT1N", NULL, "LINEOUT1N Driver" },
	{ "LINEOUT1P", NULL, "LINEOUT1P Driver" },
	{ "LINEOUT2N", NULL, "LINEOUT2N Driver" },
	{ "LINEOUT2P", NULL, "LINEOUT2P Driver" },
};

static const struct snd_soc_dapm_route lineout1_diff_routes[] = {
	{ "LINEOUT1 Mixer", "IN1L Switch", "IN1L PGA" },
	{ "LINEOUT1 Mixer", "IN1R Switch", "IN1R PGA" },
	{ "LINEOUT1 Mixer", "Output Switch", "Left Output Mixer" },

	{ "LINEOUT1N Driver", NULL, "LINEOUT1 Mixer" },
	{ "LINEOUT1P Driver", NULL, "LINEOUT1 Mixer" },
};

static const struct snd_soc_dapm_route lineout1_se_routes[] = {
	{ "LINEOUT1N Mixer", "Left Output Switch", "Left Output Mixer" },
	{ "LINEOUT1N Mixer", "Right Output Switch", "Left Output Mixer" },

	{ "LINEOUT1P Mixer", "Left Output Switch", "Left Output Mixer" },

	{ "LINEOUT1N Driver", NULL, "LINEOUT1N Mixer" },
	{ "LINEOUT1P Driver", NULL, "LINEOUT1P Mixer" },
};

static const struct snd_soc_dapm_route lineout2_diff_routes[] = {
	{ "LINEOUT2 Mixer", "IN2L Switch", "IN2L PGA" },
	{ "LINEOUT2 Mixer", "IN2R Switch", "IN2R PGA" },
	{ "LINEOUT2 Mixer", "Output Switch", "Right Output Mixer" },

	{ "LINEOUT2N Driver", NULL, "LINEOUT2 Mixer" },
	{ "LINEOUT2P Driver", NULL, "LINEOUT2 Mixer" },
};

static const struct snd_soc_dapm_route lineout2_se_routes[] = {
	{ "LINEOUT2N Mixer", "Left Output Switch", "Left Output Mixer" },
	{ "LINEOUT2N Mixer", "Right Output Switch", "Left Output Mixer" },

	{ "LINEOUT2P Mixer", "Right Output Switch", "Right Output Mixer" },

	{ "LINEOUT2N Driver", NULL, "LINEOUT2N Mixer" },
	{ "LINEOUT2P Driver", NULL, "LINEOUT2P Mixer" },
};

int wm_hubs_add_analogue_controls(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* Latch volume update bits & default ZC on */
	snd_soc_update_bits(codec, WM8993_LEFT_LINE_INPUT_1_2_VOLUME,
			    WM8993_IN1_VU, WM8993_IN1_VU);
	snd_soc_update_bits(codec, WM8993_RIGHT_LINE_INPUT_1_2_VOLUME,
			    WM8993_IN1_VU, WM8993_IN1_VU);
	snd_soc_update_bits(codec, WM8993_LEFT_LINE_INPUT_3_4_VOLUME,
			    WM8993_IN2_VU, WM8993_IN2_VU);
	snd_soc_update_bits(codec, WM8993_RIGHT_LINE_INPUT_3_4_VOLUME,
			    WM8993_IN2_VU, WM8993_IN2_VU);

	snd_soc_update_bits(codec, WM8993_SPEAKER_VOLUME_RIGHT,
			    WM8993_SPKOUT_VU, WM8993_SPKOUT_VU);

	snd_soc_update_bits(codec, WM8993_LEFT_OUTPUT_VOLUME,
			    WM8993_HPOUT1L_ZC, WM8993_HPOUT1L_ZC);
	snd_soc_update_bits(codec, WM8993_RIGHT_OUTPUT_VOLUME,
			    WM8993_HPOUT1_VU | WM8993_HPOUT1R_ZC,
			    WM8993_HPOUT1_VU | WM8993_HPOUT1R_ZC);

	snd_soc_update_bits(codec, WM8993_LEFT_OPGA_VOLUME,
			    WM8993_MIXOUTL_ZC, WM8993_MIXOUTL_ZC);
	snd_soc_update_bits(codec, WM8993_RIGHT_OPGA_VOLUME,
			    WM8993_MIXOUTR_ZC | WM8993_MIXOUT_VU,
			    WM8993_MIXOUTR_ZC | WM8993_MIXOUT_VU);

	snd_soc_add_controls(codec, analogue_snd_controls,
			     ARRAY_SIZE(analogue_snd_controls));

	snd_soc_dapm_new_controls(dapm, analogue_dapm_widgets,
				  ARRAY_SIZE(analogue_dapm_widgets));
	return 0;
}
EXPORT_SYMBOL_GPL(wm_hubs_add_analogue_controls);

int wm_hubs_add_analogue_routes(struct snd_soc_codec *codec,
				int lineout1_diff, int lineout2_diff)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_add_routes(dapm, analogue_routes,
				ARRAY_SIZE(analogue_routes));

	if (lineout1_diff)
		snd_soc_dapm_add_routes(dapm,
					lineout1_diff_routes,
					ARRAY_SIZE(lineout1_diff_routes));
	else
		snd_soc_dapm_add_routes(dapm,
					lineout1_se_routes,
					ARRAY_SIZE(lineout1_se_routes));

	if (lineout2_diff)
		snd_soc_dapm_add_routes(dapm,
					lineout2_diff_routes,
					ARRAY_SIZE(lineout2_diff_routes));
	else
		snd_soc_dapm_add_routes(dapm,
					lineout2_se_routes,
					ARRAY_SIZE(lineout2_se_routes));

	return 0;
}
EXPORT_SYMBOL_GPL(wm_hubs_add_analogue_routes);

int wm_hubs_handle_analogue_pdata(struct snd_soc_codec *codec,
				  int lineout1_diff, int lineout2_diff,
				  int lineout1fb, int lineout2fb,
				  int jd_scthr, int jd_thr, int micbias1_lvl,
				  int micbias2_lvl)
{
	if (!lineout1_diff)
		snd_soc_update_bits(codec, WM8993_LINE_MIXER1,
				    WM8993_LINEOUT1_MODE,
				    WM8993_LINEOUT1_MODE);
	if (!lineout2_diff)
		snd_soc_update_bits(codec, WM8993_LINE_MIXER2,
				    WM8993_LINEOUT2_MODE,
				    WM8993_LINEOUT2_MODE);

	/* If the line outputs are differential then we aren't presenting
	 * VMID as an output and can disable it.
	 */
	if (lineout1_diff && lineout2_diff)
		codec->dapm.idle_bias_off = 1;

	if (lineout1fb)
		snd_soc_update_bits(codec, WM8993_ADDITIONAL_CONTROL,
				    WM8993_LINEOUT1_FB, WM8993_LINEOUT1_FB);

	if (lineout2fb)
		snd_soc_update_bits(codec, WM8993_ADDITIONAL_CONTROL,
				    WM8993_LINEOUT2_FB, WM8993_LINEOUT2_FB);

	snd_soc_update_bits(codec, WM8993_MICBIAS,
			    WM8993_JD_SCTHR_MASK | WM8993_JD_THR_MASK |
			    WM8993_MICB1_LVL | WM8993_MICB2_LVL,
			    jd_scthr << WM8993_JD_SCTHR_SHIFT |
			    jd_thr << WM8993_JD_THR_SHIFT |
			    micbias1_lvl |
			    micbias2_lvl << WM8993_MICB2_LVL_SHIFT);

	return 0;
}
EXPORT_SYMBOL_GPL(wm_hubs_handle_analogue_pdata);

MODULE_DESCRIPTION("Shared support for Wolfson hubs products");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
