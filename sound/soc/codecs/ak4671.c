/*
 * ak4671.c  --  audio driver for AK4671
 *
 * Copyright (C) 2009 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "ak4671.h"


/* codec private data */
struct ak4671_priv {
	enum snd_soc_control_type control_type;
};

/* ak4671 register cache & default register settings */
static const u8 ak4671_reg[AK4671_CACHEREGNUM] = {
	0x00,	/* AK4671_AD_DA_POWER_MANAGEMENT	(0x00)	*/
	0xf6,	/* AK4671_PLL_MODE_SELECT0		(0x01)	*/
	0x00,	/* AK4671_PLL_MODE_SELECT1		(0x02)	*/
	0x02,	/* AK4671_FORMAT_SELECT			(0x03)	*/
	0x00,	/* AK4671_MIC_SIGNAL_SELECT		(0x04)	*/
	0x55,	/* AK4671_MIC_AMP_GAIN			(0x05)	*/
	0x00,	/* AK4671_MIXING_POWER_MANAGEMENT0	(0x06)	*/
	0x00,	/* AK4671_MIXING_POWER_MANAGEMENT1	(0x07)	*/
	0xb5,	/* AK4671_OUTPUT_VOLUME_CONTROL		(0x08)	*/
	0x00,	/* AK4671_LOUT1_SIGNAL_SELECT		(0x09)	*/
	0x00,	/* AK4671_ROUT1_SIGNAL_SELECT		(0x0a)	*/
	0x00,	/* AK4671_LOUT2_SIGNAL_SELECT		(0x0b)	*/
	0x00,	/* AK4671_ROUT2_SIGNAL_SELECT		(0x0c)	*/
	0x00,	/* AK4671_LOUT3_SIGNAL_SELECT		(0x0d)	*/
	0x00,	/* AK4671_ROUT3_SIGNAL_SELECT		(0x0e)	*/
	0x00,	/* AK4671_LOUT1_POWER_MANAGERMENT	(0x0f)	*/
	0x00,	/* AK4671_LOUT2_POWER_MANAGERMENT	(0x10)	*/
	0x80,	/* AK4671_LOUT3_POWER_MANAGERMENT	(0x11)	*/
	0x91,	/* AK4671_LCH_INPUT_VOLUME_CONTROL	(0x12)	*/
	0x91,	/* AK4671_RCH_INPUT_VOLUME_CONTROL	(0x13)	*/
	0xe1,	/* AK4671_ALC_REFERENCE_SELECT		(0x14)	*/
	0x00,	/* AK4671_DIGITAL_MIXING_CONTROL	(0x15)	*/
	0x00,	/* AK4671_ALC_TIMER_SELECT		(0x16)	*/
	0x00,	/* AK4671_ALC_MODE_CONTROL		(0x17)	*/
	0x02,	/* AK4671_MODE_CONTROL1			(0x18)	*/
	0x01,	/* AK4671_MODE_CONTROL2			(0x19)	*/
	0x18,	/* AK4671_LCH_OUTPUT_VOLUME_CONTROL	(0x1a)	*/
	0x18,	/* AK4671_RCH_OUTPUT_VOLUME_CONTROL	(0x1b)	*/
	0x00,	/* AK4671_SIDETONE_A_CONTROL		(0x1c)	*/
	0x02,	/* AK4671_DIGITAL_FILTER_SELECT		(0x1d)	*/
	0x00,	/* AK4671_FIL3_COEFFICIENT0		(0x1e)	*/
	0x00,	/* AK4671_FIL3_COEFFICIENT1		(0x1f)	*/
	0x00,	/* AK4671_FIL3_COEFFICIENT2		(0x20)	*/
	0x00,	/* AK4671_FIL3_COEFFICIENT3		(0x21)	*/
	0x00,	/* AK4671_EQ_COEFFICIENT0		(0x22)	*/
	0x00,	/* AK4671_EQ_COEFFICIENT1		(0x23)	*/
	0x00,	/* AK4671_EQ_COEFFICIENT2		(0x24)	*/
	0x00,	/* AK4671_EQ_COEFFICIENT3		(0x25)	*/
	0x00,	/* AK4671_EQ_COEFFICIENT4		(0x26)	*/
	0x00,	/* AK4671_EQ_COEFFICIENT5		(0x27)	*/
	0xa9,	/* AK4671_FIL1_COEFFICIENT0		(0x28)	*/
	0x1f,	/* AK4671_FIL1_COEFFICIENT1		(0x29)	*/
	0xad,	/* AK4671_FIL1_COEFFICIENT2		(0x2a)	*/
	0x20,	/* AK4671_FIL1_COEFFICIENT3		(0x2b)	*/
	0x00,	/* AK4671_FIL2_COEFFICIENT0		(0x2c)	*/
	0x00,	/* AK4671_FIL2_COEFFICIENT1		(0x2d)	*/
	0x00,	/* AK4671_FIL2_COEFFICIENT2		(0x2e)	*/
	0x00,	/* AK4671_FIL2_COEFFICIENT3		(0x2f)	*/
	0x00,	/* AK4671_DIGITAL_FILTER_SELECT2	(0x30)	*/
	0x00,	/* this register not used			*/
	0x00,	/* AK4671_E1_COEFFICIENT0		(0x32)	*/
	0x00,	/* AK4671_E1_COEFFICIENT1		(0x33)	*/
	0x00,	/* AK4671_E1_COEFFICIENT2		(0x34)	*/
	0x00,	/* AK4671_E1_COEFFICIENT3		(0x35)	*/
	0x00,	/* AK4671_E1_COEFFICIENT4		(0x36)	*/
	0x00,	/* AK4671_E1_COEFFICIENT5		(0x37)	*/
	0x00,	/* AK4671_E2_COEFFICIENT0		(0x38)	*/
	0x00,	/* AK4671_E2_COEFFICIENT1		(0x39)	*/
	0x00,	/* AK4671_E2_COEFFICIENT2		(0x3a)	*/
	0x00,	/* AK4671_E2_COEFFICIENT3		(0x3b)	*/
	0x00,	/* AK4671_E2_COEFFICIENT4		(0x3c)	*/
	0x00,	/* AK4671_E2_COEFFICIENT5		(0x3d)	*/
	0x00,	/* AK4671_E3_COEFFICIENT0		(0x3e)	*/
	0x00,	/* AK4671_E3_COEFFICIENT1		(0x3f)	*/
	0x00,	/* AK4671_E3_COEFFICIENT2		(0x40)	*/
	0x00,	/* AK4671_E3_COEFFICIENT3		(0x41)	*/
	0x00,	/* AK4671_E3_COEFFICIENT4		(0x42)	*/
	0x00,	/* AK4671_E3_COEFFICIENT5		(0x43)	*/
	0x00,	/* AK4671_E4_COEFFICIENT0		(0x44)	*/
	0x00,	/* AK4671_E4_COEFFICIENT1		(0x45)	*/
	0x00,	/* AK4671_E4_COEFFICIENT2		(0x46)	*/
	0x00,	/* AK4671_E4_COEFFICIENT3		(0x47)	*/
	0x00,	/* AK4671_E4_COEFFICIENT4		(0x48)	*/
	0x00,	/* AK4671_E4_COEFFICIENT5		(0x49)	*/
	0x00,	/* AK4671_E5_COEFFICIENT0		(0x4a)	*/
	0x00,	/* AK4671_E5_COEFFICIENT1		(0x4b)	*/
	0x00,	/* AK4671_E5_COEFFICIENT2		(0x4c)	*/
	0x00,	/* AK4671_E5_COEFFICIENT3		(0x4d)	*/
	0x00,	/* AK4671_E5_COEFFICIENT4		(0x4e)	*/
	0x00,	/* AK4671_E5_COEFFICIENT5		(0x4f)	*/
	0x88,	/* AK4671_EQ_CONTROL_250HZ_100HZ	(0x50)	*/
	0x88,	/* AK4671_EQ_CONTROL_3500HZ_1KHZ	(0x51)	*/
	0x08,	/* AK4671_EQ_CONTRO_10KHZ		(0x52)	*/
	0x00,	/* AK4671_PCM_IF_CONTROL0		(0x53)	*/
	0x00,	/* AK4671_PCM_IF_CONTROL1		(0x54)	*/
	0x00,	/* AK4671_PCM_IF_CONTROL2		(0x55)	*/
	0x18,	/* AK4671_DIGITAL_VOLUME_B_CONTROL	(0x56)	*/
	0x18,	/* AK4671_DIGITAL_VOLUME_C_CONTROL	(0x57)	*/
	0x00,	/* AK4671_SIDETONE_VOLUME_CONTROL	(0x58)	*/
	0x00,	/* AK4671_DIGITAL_MIXING_CONTROL2	(0x59)	*/
	0x00,	/* AK4671_SAR_ADC_CONTROL		(0x5a)	*/
};

/*
 * LOUT1/ROUT1 output volume control:
 * from -24 to 6 dB in 6 dB steps (mute instead of -30 dB)
 */
static DECLARE_TLV_DB_SCALE(out1_tlv, -3000, 600, 1);

/*
 * LOUT2/ROUT2 output volume control:
 * from -33 to 6 dB in 3 dB steps (mute instead of -33 dB)
 */
static DECLARE_TLV_DB_SCALE(out2_tlv, -3300, 300, 1);

/*
 * LOUT3/ROUT3 output volume control:
 * from -6 to 3 dB in 3 dB steps
 */
static DECLARE_TLV_DB_SCALE(out3_tlv, -600, 300, 0);

/*
 * Mic amp gain control:
 * from -15 to 30 dB in 3 dB steps
 * REVISIT: The actual min value(0x01) is -12 dB and the reg value 0x00 is not
 * available
 */
static DECLARE_TLV_DB_SCALE(mic_amp_tlv, -1500, 300, 0);

static const struct snd_kcontrol_new ak4671_snd_controls[] = {
	/* Common playback gain controls */
	SOC_SINGLE_TLV("Line Output1 Playback Volume",
			AK4671_OUTPUT_VOLUME_CONTROL, 0, 0x6, 0, out1_tlv),
	SOC_SINGLE_TLV("Headphone Output2 Playback Volume",
			AK4671_OUTPUT_VOLUME_CONTROL, 4, 0xd, 0, out2_tlv),
	SOC_SINGLE_TLV("Line Output3 Playback Volume",
			AK4671_LOUT3_POWER_MANAGERMENT, 6, 0x3, 0, out3_tlv),

	/* Common capture gain controls */
	SOC_DOUBLE_TLV("Mic Amp Capture Volume",
			AK4671_MIC_AMP_GAIN, 0, 4, 0xf, 0, mic_amp_tlv),
};

/* event handlers */
static int ak4671_out2_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, AK4671_LOUT2_POWER_MANAGERMENT,
				    AK4671_MUTEN, AK4671_MUTEN);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, AK4671_LOUT2_POWER_MANAGERMENT,
				    AK4671_MUTEN, 0);
		break;
	}

	return 0;
}

/* Output Mixers */
static const struct snd_kcontrol_new ak4671_lout1_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACL", AK4671_LOUT1_SIGNAL_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("LINL1", AK4671_LOUT1_SIGNAL_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("LINL2", AK4671_LOUT1_SIGNAL_SELECT, 2, 1, 0),
	SOC_DAPM_SINGLE("LINL3", AK4671_LOUT1_SIGNAL_SELECT, 3, 1, 0),
	SOC_DAPM_SINGLE("LINL4", AK4671_LOUT1_SIGNAL_SELECT, 4, 1, 0),
	SOC_DAPM_SINGLE("LOOPL", AK4671_LOUT1_SIGNAL_SELECT, 5, 1, 0),
};

static const struct snd_kcontrol_new ak4671_rout1_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACR", AK4671_ROUT1_SIGNAL_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("RINR1", AK4671_ROUT1_SIGNAL_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("RINR2", AK4671_ROUT1_SIGNAL_SELECT, 2, 1, 0),
	SOC_DAPM_SINGLE("RINR3", AK4671_ROUT1_SIGNAL_SELECT, 3, 1, 0),
	SOC_DAPM_SINGLE("RINR4", AK4671_ROUT1_SIGNAL_SELECT, 4, 1, 0),
	SOC_DAPM_SINGLE("LOOPR", AK4671_ROUT1_SIGNAL_SELECT, 5, 1, 0),
};

static const struct snd_kcontrol_new ak4671_lout2_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACHL", AK4671_LOUT2_SIGNAL_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("LINH1", AK4671_LOUT2_SIGNAL_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("LINH2", AK4671_LOUT2_SIGNAL_SELECT, 2, 1, 0),
	SOC_DAPM_SINGLE("LINH3", AK4671_LOUT2_SIGNAL_SELECT, 3, 1, 0),
	SOC_DAPM_SINGLE("LINH4", AK4671_LOUT2_SIGNAL_SELECT, 4, 1, 0),
	SOC_DAPM_SINGLE("LOOPHL", AK4671_LOUT2_SIGNAL_SELECT, 5, 1, 0),
};

static const struct snd_kcontrol_new ak4671_rout2_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACHR", AK4671_ROUT2_SIGNAL_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("RINH1", AK4671_ROUT2_SIGNAL_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("RINH2", AK4671_ROUT2_SIGNAL_SELECT, 2, 1, 0),
	SOC_DAPM_SINGLE("RINH3", AK4671_ROUT2_SIGNAL_SELECT, 3, 1, 0),
	SOC_DAPM_SINGLE("RINH4", AK4671_ROUT2_SIGNAL_SELECT, 4, 1, 0),
	SOC_DAPM_SINGLE("LOOPHR", AK4671_ROUT2_SIGNAL_SELECT, 5, 1, 0),
};

static const struct snd_kcontrol_new ak4671_lout3_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACSL", AK4671_LOUT3_SIGNAL_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("LINS1", AK4671_LOUT3_SIGNAL_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("LINS2", AK4671_LOUT3_SIGNAL_SELECT, 2, 1, 0),
	SOC_DAPM_SINGLE("LINS3", AK4671_LOUT3_SIGNAL_SELECT, 3, 1, 0),
	SOC_DAPM_SINGLE("LINS4", AK4671_LOUT3_SIGNAL_SELECT, 4, 1, 0),
	SOC_DAPM_SINGLE("LOOPSL", AK4671_LOUT3_SIGNAL_SELECT, 5, 1, 0),
};

static const struct snd_kcontrol_new ak4671_rout3_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACSR", AK4671_ROUT3_SIGNAL_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("RINS1", AK4671_ROUT3_SIGNAL_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("RINS2", AK4671_ROUT3_SIGNAL_SELECT, 2, 1, 0),
	SOC_DAPM_SINGLE("RINS3", AK4671_ROUT3_SIGNAL_SELECT, 3, 1, 0),
	SOC_DAPM_SINGLE("RINS4", AK4671_ROUT3_SIGNAL_SELECT, 4, 1, 0),
	SOC_DAPM_SINGLE("LOOPSR", AK4671_ROUT3_SIGNAL_SELECT, 5, 1, 0),
};

/* Input MUXs */
static const char *ak4671_lin_mux_texts[] =
		{"LIN1", "LIN2", "LIN3", "LIN4"};
static const struct soc_enum ak4671_lin_mux_enum =
	SOC_ENUM_SINGLE(AK4671_MIC_SIGNAL_SELECT, 0,
			ARRAY_SIZE(ak4671_lin_mux_texts),
			ak4671_lin_mux_texts);
static const struct snd_kcontrol_new ak4671_lin_mux_control =
	SOC_DAPM_ENUM("Route", ak4671_lin_mux_enum);

static const char *ak4671_rin_mux_texts[] =
		{"RIN1", "RIN2", "RIN3", "RIN4"};
static const struct soc_enum ak4671_rin_mux_enum =
	SOC_ENUM_SINGLE(AK4671_MIC_SIGNAL_SELECT, 2,
			ARRAY_SIZE(ak4671_rin_mux_texts),
			ak4671_rin_mux_texts);
static const struct snd_kcontrol_new ak4671_rin_mux_control =
	SOC_DAPM_ENUM("Route", ak4671_rin_mux_enum);

static const struct snd_soc_dapm_widget ak4671_dapm_widgets[] = {
	/* Inputs */
	SND_SOC_DAPM_INPUT("LIN1"),
	SND_SOC_DAPM_INPUT("RIN1"),
	SND_SOC_DAPM_INPUT("LIN2"),
	SND_SOC_DAPM_INPUT("RIN2"),
	SND_SOC_DAPM_INPUT("LIN3"),
	SND_SOC_DAPM_INPUT("RIN3"),
	SND_SOC_DAPM_INPUT("LIN4"),
	SND_SOC_DAPM_INPUT("RIN4"),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),
	SND_SOC_DAPM_OUTPUT("LOUT3"),
	SND_SOC_DAPM_OUTPUT("ROUT3"),

	/* DAC */
	SND_SOC_DAPM_DAC("DAC Left", "Left HiFi Playback",
			AK4671_AD_DA_POWER_MANAGEMENT, 6, 0),
	SND_SOC_DAPM_DAC("DAC Right", "Right HiFi Playback",
			AK4671_AD_DA_POWER_MANAGEMENT, 7, 0),

	/* ADC */
	SND_SOC_DAPM_ADC("ADC Left", "Left HiFi Capture",
			AK4671_AD_DA_POWER_MANAGEMENT, 4, 0),
	SND_SOC_DAPM_ADC("ADC Right", "Right HiFi Capture",
			AK4671_AD_DA_POWER_MANAGEMENT, 5, 0),

	/* PGA */
	SND_SOC_DAPM_PGA("LOUT2 Mix Amp",
			AK4671_LOUT2_POWER_MANAGERMENT, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ROUT2 Mix Amp",
			AK4671_LOUT2_POWER_MANAGERMENT, 6, 0, NULL, 0),

	SND_SOC_DAPM_PGA("LIN1 Mixing Circuit",
			AK4671_MIXING_POWER_MANAGEMENT1, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RIN1 Mixing Circuit",
			AK4671_MIXING_POWER_MANAGEMENT1, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("LIN2 Mixing Circuit",
			AK4671_MIXING_POWER_MANAGEMENT1, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RIN2 Mixing Circuit",
			AK4671_MIXING_POWER_MANAGEMENT1, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("LIN3 Mixing Circuit",
			AK4671_MIXING_POWER_MANAGEMENT1, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RIN3 Mixing Circuit",
			AK4671_MIXING_POWER_MANAGEMENT1, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("LIN4 Mixing Circuit",
			AK4671_MIXING_POWER_MANAGEMENT1, 6, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RIN4 Mixing Circuit",
			AK4671_MIXING_POWER_MANAGEMENT1, 7, 0, NULL, 0),

	/* Output Mixers */
	SND_SOC_DAPM_MIXER("LOUT1 Mixer", AK4671_LOUT1_POWER_MANAGERMENT, 0, 0,
			&ak4671_lout1_mixer_controls[0],
			ARRAY_SIZE(ak4671_lout1_mixer_controls)),
	SND_SOC_DAPM_MIXER("ROUT1 Mixer", AK4671_LOUT1_POWER_MANAGERMENT, 1, 0,
			&ak4671_rout1_mixer_controls[0],
			ARRAY_SIZE(ak4671_rout1_mixer_controls)),
	SND_SOC_DAPM_MIXER_E("LOUT2 Mixer", AK4671_LOUT2_POWER_MANAGERMENT,
			0, 0, &ak4671_lout2_mixer_controls[0],
			ARRAY_SIZE(ak4671_lout2_mixer_controls),
			ak4671_out2_event,
			SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MIXER_E("ROUT2 Mixer", AK4671_LOUT2_POWER_MANAGERMENT,
			1, 0, &ak4671_rout2_mixer_controls[0],
			ARRAY_SIZE(ak4671_rout2_mixer_controls),
			ak4671_out2_event,
			SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MIXER("LOUT3 Mixer", AK4671_LOUT3_POWER_MANAGERMENT, 0, 0,
			&ak4671_lout3_mixer_controls[0],
			ARRAY_SIZE(ak4671_lout3_mixer_controls)),
	SND_SOC_DAPM_MIXER("ROUT3 Mixer", AK4671_LOUT3_POWER_MANAGERMENT, 1, 0,
			&ak4671_rout3_mixer_controls[0],
			ARRAY_SIZE(ak4671_rout3_mixer_controls)),

	/* Input MUXs */
	SND_SOC_DAPM_MUX("LIN MUX", AK4671_AD_DA_POWER_MANAGEMENT, 2, 0,
			&ak4671_lin_mux_control),
	SND_SOC_DAPM_MUX("RIN MUX", AK4671_AD_DA_POWER_MANAGEMENT, 3, 0,
			&ak4671_rin_mux_control),

	/* Mic Power */
	SND_SOC_DAPM_MICBIAS("Mic Bias", AK4671_AD_DA_POWER_MANAGEMENT, 1, 0),

	/* Supply */
	SND_SOC_DAPM_SUPPLY("PMPLL", AK4671_PLL_MODE_SELECT1, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route ak4671_intercon[] = {
	{"DAC Left", "NULL", "PMPLL"},
	{"DAC Right", "NULL", "PMPLL"},
	{"ADC Left", "NULL", "PMPLL"},
	{"ADC Right", "NULL", "PMPLL"},

	/* Outputs */
	{"LOUT1", "NULL", "LOUT1 Mixer"},
	{"ROUT1", "NULL", "ROUT1 Mixer"},
	{"LOUT2", "NULL", "LOUT2 Mix Amp"},
	{"ROUT2", "NULL", "ROUT2 Mix Amp"},
	{"LOUT3", "NULL", "LOUT3 Mixer"},
	{"ROUT3", "NULL", "ROUT3 Mixer"},

	{"LOUT1 Mixer", "DACL", "DAC Left"},
	{"ROUT1 Mixer", "DACR", "DAC Right"},
	{"LOUT2 Mixer", "DACHL", "DAC Left"},
	{"ROUT2 Mixer", "DACHR", "DAC Right"},
	{"LOUT2 Mix Amp", "NULL", "LOUT2 Mixer"},
	{"ROUT2 Mix Amp", "NULL", "ROUT2 Mixer"},
	{"LOUT3 Mixer", "DACSL", "DAC Left"},
	{"ROUT3 Mixer", "DACSR", "DAC Right"},

	/* Inputs */
	{"LIN MUX", "LIN1", "LIN1"},
	{"LIN MUX", "LIN2", "LIN2"},
	{"LIN MUX", "LIN3", "LIN3"},
	{"LIN MUX", "LIN4", "LIN4"},

	{"RIN MUX", "RIN1", "RIN1"},
	{"RIN MUX", "RIN2", "RIN2"},
	{"RIN MUX", "RIN3", "RIN3"},
	{"RIN MUX", "RIN4", "RIN4"},

	{"LIN1", NULL, "Mic Bias"},
	{"RIN1", NULL, "Mic Bias"},
	{"LIN2", NULL, "Mic Bias"},
	{"RIN2", NULL, "Mic Bias"},

	{"ADC Left", "NULL", "LIN MUX"},
	{"ADC Right", "NULL", "RIN MUX"},

	/* Analog Loops */
	{"LIN1 Mixing Circuit", "NULL", "LIN1"},
	{"RIN1 Mixing Circuit", "NULL", "RIN1"},
	{"LIN2 Mixing Circuit", "NULL", "LIN2"},
	{"RIN2 Mixing Circuit", "NULL", "RIN2"},
	{"LIN3 Mixing Circuit", "NULL", "LIN3"},
	{"RIN3 Mixing Circuit", "NULL", "RIN3"},
	{"LIN4 Mixing Circuit", "NULL", "LIN4"},
	{"RIN4 Mixing Circuit", "NULL", "RIN4"},

	{"LOUT1 Mixer", "LINL1", "LIN1 Mixing Circuit"},
	{"ROUT1 Mixer", "RINR1", "RIN1 Mixing Circuit"},
	{"LOUT2 Mixer", "LINH1", "LIN1 Mixing Circuit"},
	{"ROUT2 Mixer", "RINH1", "RIN1 Mixing Circuit"},
	{"LOUT3 Mixer", "LINS1", "LIN1 Mixing Circuit"},
	{"ROUT3 Mixer", "RINS1", "RIN1 Mixing Circuit"},

	{"LOUT1 Mixer", "LINL2", "LIN2 Mixing Circuit"},
	{"ROUT1 Mixer", "RINR2", "RIN2 Mixing Circuit"},
	{"LOUT2 Mixer", "LINH2", "LIN2 Mixing Circuit"},
	{"ROUT2 Mixer", "RINH2", "RIN2 Mixing Circuit"},
	{"LOUT3 Mixer", "LINS2", "LIN2 Mixing Circuit"},
	{"ROUT3 Mixer", "RINS2", "RIN2 Mixing Circuit"},

	{"LOUT1 Mixer", "LINL3", "LIN3 Mixing Circuit"},
	{"ROUT1 Mixer", "RINR3", "RIN3 Mixing Circuit"},
	{"LOUT2 Mixer", "LINH3", "LIN3 Mixing Circuit"},
	{"ROUT2 Mixer", "RINH3", "RIN3 Mixing Circuit"},
	{"LOUT3 Mixer", "LINS3", "LIN3 Mixing Circuit"},
	{"ROUT3 Mixer", "RINS3", "RIN3 Mixing Circuit"},

	{"LOUT1 Mixer", "LINL4", "LIN4 Mixing Circuit"},
	{"ROUT1 Mixer", "RINR4", "RIN4 Mixing Circuit"},
	{"LOUT2 Mixer", "LINH4", "LIN4 Mixing Circuit"},
	{"ROUT2 Mixer", "RINH4", "RIN4 Mixing Circuit"},
	{"LOUT3 Mixer", "LINS4", "LIN4 Mixing Circuit"},
	{"ROUT3 Mixer", "RINS4", "RIN4 Mixing Circuit"},
};

static int ak4671_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 fs;

	fs = snd_soc_read(codec, AK4671_PLL_MODE_SELECT0);
	fs &= ~AK4671_FS;

	switch (params_rate(params)) {
	case 8000:
		fs |= AK4671_FS_8KHZ;
		break;
	case 12000:
		fs |= AK4671_FS_12KHZ;
		break;
	case 16000:
		fs |= AK4671_FS_16KHZ;
		break;
	case 24000:
		fs |= AK4671_FS_24KHZ;
		break;
	case 11025:
		fs |= AK4671_FS_11_025KHZ;
		break;
	case 22050:
		fs |= AK4671_FS_22_05KHZ;
		break;
	case 32000:
		fs |= AK4671_FS_32KHZ;
		break;
	case 44100:
		fs |= AK4671_FS_44_1KHZ;
		break;
	case 48000:
		fs |= AK4671_FS_48KHZ;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, AK4671_PLL_MODE_SELECT0, fs);

	return 0;
}

static int ak4671_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 pll;

	pll = snd_soc_read(codec, AK4671_PLL_MODE_SELECT0);
	pll &= ~AK4671_PLL;

	switch (freq) {
	case 11289600:
		pll |= AK4671_PLL_11_2896MHZ;
		break;
	case 12000000:
		pll |= AK4671_PLL_12MHZ;
		break;
	case 12288000:
		pll |= AK4671_PLL_12_288MHZ;
		break;
	case 13000000:
		pll |= AK4671_PLL_13MHZ;
		break;
	case 13500000:
		pll |= AK4671_PLL_13_5MHZ;
		break;
	case 19200000:
		pll |= AK4671_PLL_19_2MHZ;
		break;
	case 24000000:
		pll |= AK4671_PLL_24MHZ;
		break;
	case 26000000:
		pll |= AK4671_PLL_26MHZ;
		break;
	case 27000000:
		pll |= AK4671_PLL_27MHZ;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, AK4671_PLL_MODE_SELECT0, pll);

	return 0;
}

static int ak4671_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 mode;
	u8 format;

	/* set master/slave audio interface */
	mode = snd_soc_read(codec, AK4671_PLL_MODE_SELECT1);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		mode |= AK4671_M_S;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		mode &= ~(AK4671_M_S);
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	format = snd_soc_read(codec, AK4671_FORMAT_SELECT);
	format &= ~AK4671_DIF;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format |= AK4671_DIF_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format |= AK4671_DIF_MSB_MODE;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		format |= AK4671_DIF_DSP_MODE;
		format |= AK4671_BCKP;
		format |= AK4671_MSBS;
		break;
	default:
		return -EINVAL;
	}

	/* set mode and format */
	snd_soc_write(codec, AK4671_PLL_MODE_SELECT1, mode);
	snd_soc_write(codec, AK4671_FORMAT_SELECT, format);

	return 0;
}

static int ak4671_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
	case SND_SOC_BIAS_STANDBY:
		snd_soc_update_bits(codec, AK4671_AD_DA_POWER_MANAGEMENT,
				    AK4671_PMVCM, AK4671_PMVCM);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, AK4671_AD_DA_POWER_MANAGEMENT, 0x00);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define AK4671_RATES		(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
				SNDRV_PCM_RATE_48000)

#define AK4671_FORMATS		SNDRV_PCM_FMTBIT_S16_LE

static struct snd_soc_dai_ops ak4671_dai_ops = {
	.hw_params	= ak4671_hw_params,
	.set_sysclk	= ak4671_set_dai_sysclk,
	.set_fmt	= ak4671_set_dai_fmt,
};

static struct snd_soc_dai_driver ak4671_dai = {
	.name = "ak4671-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AK4671_RATES,
		.formats = AK4671_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AK4671_RATES,
		.formats = AK4671_FORMATS,},
	.ops = &ak4671_dai_ops,
};

static int ak4671_probe(struct snd_soc_codec *codec)
{
	struct ak4671_priv *ak4671 = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, ak4671->control_type);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	snd_soc_add_controls(codec, ak4671_snd_controls,
			     ARRAY_SIZE(ak4671_snd_controls));

	ak4671_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return ret;
}

static int ak4671_remove(struct snd_soc_codec *codec)
{
	ak4671_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_ak4671 = {
	.probe = ak4671_probe,
	.remove = ak4671_remove,
	.set_bias_level = ak4671_set_bias_level,
	.reg_cache_size = AK4671_CACHEREGNUM,
	.reg_word_size = sizeof(u8),
	.reg_cache_default = ak4671_reg,
	.dapm_widgets = ak4671_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ak4671_dapm_widgets),
	.dapm_routes = ak4671_intercon,
	.num_dapm_routes = ARRAY_SIZE(ak4671_intercon),
};

static int __devinit ak4671_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct ak4671_priv *ak4671;
	int ret;

	ak4671 = kzalloc(sizeof(struct ak4671_priv), GFP_KERNEL);
	if (ak4671 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, ak4671);
	ak4671->control_type = SND_SOC_I2C;

	ret = snd_soc_register_codec(&client->dev,
			&soc_codec_dev_ak4671, &ak4671_dai, 1);
	if (ret < 0)
		kfree(ak4671);
	return ret;
}

static __devexit int ak4671_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id ak4671_i2c_id[] = {
	{ "ak4671", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4671_i2c_id);

static struct i2c_driver ak4671_i2c_driver = {
	.driver = {
		.name = "ak4671-codec",
		.owner = THIS_MODULE,
	},
	.probe = ak4671_i2c_probe,
	.remove = __devexit_p(ak4671_i2c_remove),
	.id_table = ak4671_i2c_id,
};

static int __init ak4671_modinit(void)
{
	return i2c_add_driver(&ak4671_i2c_driver);
}
module_init(ak4671_modinit);

static void __exit ak4671_exit(void)
{
	i2c_del_driver(&ak4671_i2c_driver);
}
module_exit(ak4671_exit);

MODULE_DESCRIPTION("ASoC AK4671 codec driver");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_LICENSE("GPL");
