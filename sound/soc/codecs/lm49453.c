// SPDX-License-Identifier: GPL-2.0-only
/*
 * lm49453.c  -  LM49453 ALSA Soc Audio driver
 *
 * Copyright (c) 2012 Texas Instruments, Inc
 *
 * Initially based on sound/soc/codecs/wm8350.c
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <asm/div64.h>
#include "lm49453.h"

static const struct reg_default lm49453_reg_defs[] = {
	{ 0, 0x00 },
	{ 1, 0x00 },
	{ 2, 0x00 },
	{ 3, 0x00 },
	{ 4, 0x00 },
	{ 5, 0x00 },
	{ 6, 0x00 },
	{ 7, 0x00 },
	{ 8, 0x00 },
	{ 9, 0x00 },
	{ 10, 0x00 },
	{ 11, 0x00 },
	{ 12, 0x00 },
	{ 13, 0x00 },
	{ 14, 0x00 },
	{ 15, 0x00 },
	{ 16, 0x00 },
	{ 17, 0x00 },
	{ 18, 0x00 },
	{ 19, 0x00 },
	{ 20, 0x00 },
	{ 21, 0x00 },
	{ 22, 0x00 },
	{ 23, 0x00 },
	{ 32, 0x00 },
	{ 33, 0x00 },
	{ 35, 0x00 },
	{ 36, 0x00 },
	{ 37, 0x00 },
	{ 46, 0x00 },
	{ 48, 0x00 },
	{ 49, 0x00 },
	{ 51, 0x00 },
	{ 56, 0x00 },
	{ 58, 0x00 },
	{ 59, 0x00 },
	{ 60, 0x00 },
	{ 61, 0x00 },
	{ 62, 0x00 },
	{ 63, 0x00 },
	{ 64, 0x00 },
	{ 65, 0x00 },
	{ 66, 0x00 },
	{ 67, 0x00 },
	{ 68, 0x00 },
	{ 69, 0x00 },
	{ 70, 0x00 },
	{ 71, 0x00 },
	{ 72, 0x00 },
	{ 73, 0x00 },
	{ 74, 0x00 },
	{ 75, 0x00 },
	{ 76, 0x00 },
	{ 77, 0x00 },
	{ 78, 0x00 },
	{ 79, 0x00 },
	{ 80, 0x00 },
	{ 81, 0x00 },
	{ 82, 0x00 },
	{ 83, 0x00 },
	{ 85, 0x00 },
	{ 85, 0x00 },
	{ 86, 0x00 },
	{ 87, 0x00 },
	{ 88, 0x00 },
	{ 89, 0x00 },
	{ 90, 0x00 },
	{ 91, 0x00 },
	{ 92, 0x00 },
	{ 93, 0x00 },
	{ 94, 0x00 },
	{ 95, 0x00 },
	{ 96, 0x01 },
	{ 97, 0x00 },
	{ 98, 0x00 },
	{ 99, 0x00 },
	{ 100, 0x00 },
	{ 101, 0x00 },
	{ 102, 0x00 },
	{ 103, 0x01 },
	{ 104, 0x01 },
	{ 105, 0x00 },
	{ 106, 0x01 },
	{ 107, 0x00 },
	{ 108, 0x00 },
	{ 109, 0x00 },
	{ 110, 0x00 },
	{ 111, 0x02 },
	{ 112, 0x02 },
	{ 113, 0x00 },
	{ 121, 0x80 },
	{ 122, 0xBB },
	{ 123, 0x80 },
	{ 124, 0xBB },
	{ 128, 0x00 },
	{ 130, 0x00 },
	{ 131, 0x00 },
	{ 132, 0x00 },
	{ 133, 0x0A },
	{ 134, 0x0A },
	{ 135, 0x0A },
	{ 136, 0x0F },
	{ 137, 0x00 },
	{ 138, 0x73 },
	{ 139, 0x33 },
	{ 140, 0x73 },
	{ 141, 0x33 },
	{ 142, 0x73 },
	{ 143, 0x33 },
	{ 144, 0x73 },
	{ 145, 0x33 },
	{ 146, 0x73 },
	{ 147, 0x33 },
	{ 148, 0x73 },
	{ 149, 0x33 },
	{ 150, 0x73 },
	{ 151, 0x33 },
	{ 152, 0x00 },
	{ 153, 0x00 },
	{ 154, 0x00 },
	{ 155, 0x00 },
	{ 176, 0x00 },
	{ 177, 0x00 },
	{ 178, 0x00 },
	{ 179, 0x00 },
	{ 180, 0x00 },
	{ 181, 0x00 },
	{ 182, 0x00 },
	{ 183, 0x00 },
	{ 184, 0x00 },
	{ 185, 0x00 },
	{ 186, 0x00 },
	{ 187, 0x00 },
	{ 188, 0x00 },
	{ 189, 0x00 },
	{ 208, 0x06 },
	{ 209, 0x00 },
	{ 210, 0x08 },
	{ 211, 0x54 },
	{ 212, 0x14 },
	{ 213, 0x0d },
	{ 214, 0x0d },
	{ 215, 0x14 },
	{ 216, 0x60 },
	{ 221, 0x00 },
	{ 222, 0x00 },
	{ 223, 0x00 },
	{ 224, 0x00 },
	{ 248, 0x00 },
	{ 249, 0x00 },
	{ 250, 0x00 },
	{ 255, 0x00 },
};

/* codec private data */
struct lm49453_priv {
	struct regmap *regmap;
};

/* capture path controls */

static const char *lm49453_mic2mode_text[] = {"Single Ended", "Differential"};

static SOC_ENUM_SINGLE_DECL(lm49453_mic2mode_enum, LM49453_P0_MICR_REG, 5,
			    lm49453_mic2mode_text);

static const char *lm49453_dmic_cfg_text[] = {"DMICDAT1", "DMICDAT2"};

static SOC_ENUM_SINGLE_DECL(lm49453_dmic12_cfg_enum,
			    LM49453_P0_DIGITAL_MIC1_CONFIG_REG, 7,
			    lm49453_dmic_cfg_text);

static SOC_ENUM_SINGLE_DECL(lm49453_dmic34_cfg_enum,
			    LM49453_P0_DIGITAL_MIC2_CONFIG_REG, 7,
			    lm49453_dmic_cfg_text);

/* MUX Controls */
static const char *lm49453_adcl_mux_text[] = { "MIC1", "Aux_L" };

static const char *lm49453_adcr_mux_text[] = { "MIC2", "Aux_R" };

static SOC_ENUM_SINGLE_DECL(lm49453_adcl_enum,
			    LM49453_P0_ANALOG_MIXER_ADC_REG, 0,
			    lm49453_adcl_mux_text);

static SOC_ENUM_SINGLE_DECL(lm49453_adcr_enum,
			    LM49453_P0_ANALOG_MIXER_ADC_REG, 1,
			    lm49453_adcr_mux_text);

static const struct snd_kcontrol_new lm49453_adcl_mux_control =
	SOC_DAPM_ENUM("ADC Left Mux", lm49453_adcl_enum);

static const struct snd_kcontrol_new lm49453_adcr_mux_control =
	SOC_DAPM_ENUM("ADC Right Mux", lm49453_adcr_enum);

static const struct snd_kcontrol_new lm49453_headset_left_mixer[] = {
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_DACHPL1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_DACHPL1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("Port1_3 Switch", LM49453_P0_DACHPL1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("Port1_4 Switch", LM49453_P0_DACHPL1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("Port1_5 Switch", LM49453_P0_DACHPL1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("Port1_6 Switch", LM49453_P0_DACHPL1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_7 Switch", LM49453_P0_DACHPL1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port1_8 Switch", LM49453_P0_DACHPL1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_DACHPL2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_DACHPL2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_DACHPL2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_DACHPL2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_DACHPL2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_DACHPL2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_DACHPL2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_DACHPL2_REG, 7, 1, 0),
SOC_DAPM_SINGLE("Sidetone Switch", LM49453_P0_STN_SEL_REG, 0, 0, 0),
};

static const struct snd_kcontrol_new lm49453_headset_right_mixer[] = {
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_DACHPR1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_DACHPR1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("Port1_3 Switch", LM49453_P0_DACHPR1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("Port1_4 Switch", LM49453_P0_DACHPR1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("Port1_5 Switch", LM49453_P0_DACHPR1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("Port1_6 Switch", LM49453_P0_DACHPR1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_7 Switch", LM49453_P0_DACHPR1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port1_8 Switch", LM49453_P0_DACHPR1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_DACHPR2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_DACHPR2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_DACHPR2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_DACHPR2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_DACHPR2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_DACHPR2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_DACHPR2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_DACHPR2_REG, 7, 1, 0),
SOC_DAPM_SINGLE("Sidetone Switch", LM49453_P0_STN_SEL_REG, 1, 0, 0),
};

static const struct snd_kcontrol_new lm49453_speaker_left_mixer[] = {
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_DACLSL1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_DACLSL1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("Port1_3 Switch", LM49453_P0_DACLSL1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("Port1_4 Switch", LM49453_P0_DACLSL1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("Port1_5 Switch", LM49453_P0_DACLSL1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("Port1_6 Switch", LM49453_P0_DACLSL1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_7 Switch", LM49453_P0_DACLSL1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port1_8 Switch", LM49453_P0_DACLSL1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_DACLSL2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_DACLSL2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_DACLSL2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_DACLSL2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_DACLSL2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_DACLSL2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_DACLSL2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_DACLSL2_REG, 7, 1, 0),
SOC_DAPM_SINGLE("Sidetone Switch", LM49453_P0_STN_SEL_REG, 2, 0, 0),
};

static const struct snd_kcontrol_new lm49453_speaker_right_mixer[] = {
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_DACLSR1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_DACLSR1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("Port1_3 Switch", LM49453_P0_DACLSR1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("Port1_4 Switch", LM49453_P0_DACLSR1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("Port1_5 Switch", LM49453_P0_DACLSR1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("Port1_6 Switch", LM49453_P0_DACLSR1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_7 Switch", LM49453_P0_DACLSR1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port1_8 Switch", LM49453_P0_DACLSR1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_DACLSR2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_DACLSR2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_DACLSR2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_DACLSR2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_DACLSR2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_DACLSR2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_DACLSR2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_DACLSR2_REG, 7, 1, 0),
SOC_DAPM_SINGLE("Sidetone Switch", LM49453_P0_STN_SEL_REG, 3, 0, 0),
};

static const struct snd_kcontrol_new lm49453_haptic_left_mixer[] = {
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_DACHAL1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_DACHAL1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("Port1_3 Switch", LM49453_P0_DACHAL1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("Port1_4 Switch", LM49453_P0_DACHAL1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("Port1_5 Switch", LM49453_P0_DACHAL1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("Port1_6 Switch", LM49453_P0_DACHAL1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_7 Switch", LM49453_P0_DACHAL1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port1_8 Switch", LM49453_P0_DACHAL1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_DACHAL2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_DACHAL2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_DACHAL2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_DACHAL2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_DACHAL2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_DACHAL2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_DACHAL2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_DACHAL2_REG, 7, 1, 0),
SOC_DAPM_SINGLE("Sidetone Switch", LM49453_P0_STN_SEL_REG, 4, 0, 0),
};

static const struct snd_kcontrol_new lm49453_haptic_right_mixer[] = {
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_DACHAR1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_DACHAR1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("Port1_3 Switch", LM49453_P0_DACHAR1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("Port1_4 Switch", LM49453_P0_DACHAR1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("Port1_5 Switch", LM49453_P0_DACHAR1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("Port1_6 Switch", LM49453_P0_DACHAR1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_7 Switch", LM49453_P0_DACHAR1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port1_8 Switch", LM49453_P0_DACHAR1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_DACHAR2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_DACHAR2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_DACHAR2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_DACHAR2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_DACHAR2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_DACHAR2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_DACHAR2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_DACHAR2_REG, 7, 1, 0),
SOC_DAPM_SINGLE("Sidetone Switch", LM49453_P0_STN_SEL_REG, 5, 0, 0),
};

static const struct snd_kcontrol_new lm49453_lineout_left_mixer[] = {
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_DACLOL1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_DACLOL1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("Port1_3 Switch", LM49453_P0_DACLOL1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("Port1_4 Switch", LM49453_P0_DACLOL1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("Port1_5 Switch", LM49453_P0_DACLOL1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("Port1_6 Switch", LM49453_P0_DACLOL1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_7 Switch", LM49453_P0_DACLOL1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port1_8 Switch", LM49453_P0_DACLOL1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_DACLOL2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_DACLOL2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_DACLOL2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_DACLOL2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_DACLOL2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_DACLOL2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_DACLOL2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_DACLOL2_REG, 7, 1, 0),
SOC_DAPM_SINGLE("Sidetone Switch", LM49453_P0_STN_SEL_REG, 6, 0, 0),
};

static const struct snd_kcontrol_new lm49453_lineout_right_mixer[] = {
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_DACLOR1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_DACLOR1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("Port1_3 Switch", LM49453_P0_DACLOR1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("Port1_4 Switch", LM49453_P0_DACLOR1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("Port1_5 Switch", LM49453_P0_DACLOR1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("Port1_6 Switch", LM49453_P0_DACLOR1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_7 Switch", LM49453_P0_DACLOR1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port1_8 Switch", LM49453_P0_DACLOR1_REG, 7, 1, 0),
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_DACLOR2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_DACLOR2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_DACLOR2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_DACLOR2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_DACLOR2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_DACLOR2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_DACLOR2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_DACLOR2_REG, 7, 1, 0),
SOC_DAPM_SINGLE("Sidetone Switch", LM49453_P0_STN_SEL_REG, 7, 0, 0),
};

static const struct snd_kcontrol_new lm49453_port1_tx1_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT1_TX1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT1_TX1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT1_TX1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT1_TX1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT1_TX1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT1_TX1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_PORT1_TX1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_PORT1_TX1_REG, 7, 1, 0),
};

static const struct snd_kcontrol_new lm49453_port1_tx2_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT1_TX2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT1_TX2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT1_TX2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT1_TX2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT1_TX2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT1_TX2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_PORT1_TX2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_PORT1_TX2_REG, 7, 1, 0),
};

static const struct snd_kcontrol_new lm49453_port1_tx3_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT1_TX3_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT1_TX3_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT1_TX3_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT1_TX3_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT1_TX3_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT1_TX3_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_3 Switch", LM49453_P0_PORT1_TX3_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new lm49453_port1_tx4_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT1_TX4_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT1_TX4_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT1_TX4_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT1_TX4_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT1_TX4_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT1_TX4_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_4 Switch", LM49453_P0_PORT1_TX4_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new lm49453_port1_tx5_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT1_TX5_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT1_TX5_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT1_TX5_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT1_TX5_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT1_TX5_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT1_TX5_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_5 Switch", LM49453_P0_PORT1_TX5_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new lm49453_port1_tx6_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT1_TX6_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT1_TX6_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT1_TX6_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT1_TX6_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT1_TX6_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT1_TX6_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_6 Switch", LM49453_P0_PORT1_TX6_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new lm49453_port1_tx7_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT1_TX7_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT1_TX7_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT1_TX7_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT1_TX7_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT1_TX7_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT1_TX7_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_7 Switch", LM49453_P0_PORT1_TX7_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new lm49453_port1_tx8_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT1_TX8_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT1_TX8_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT1_TX8_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT1_TX8_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT1_TX8_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT1_TX8_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_8 Switch", LM49453_P0_PORT1_TX8_REG, 6, 1, 0),
};

static const struct snd_kcontrol_new lm49453_port2_tx1_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT2_TX1_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT2_TX1_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT2_TX1_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT2_TX1_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT2_TX1_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT2_TX1_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_1 Switch", LM49453_P0_PORT2_TX1_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_1 Switch", LM49453_P0_PORT2_TX1_REG, 7, 1, 0),
};

static const struct snd_kcontrol_new lm49453_port2_tx2_mixer[] = {
SOC_DAPM_SINGLE("DMIC1L Switch", LM49453_P0_PORT2_TX2_REG, 0, 1, 0),
SOC_DAPM_SINGLE("DMIC1R Switch", LM49453_P0_PORT2_TX2_REG, 1, 1, 0),
SOC_DAPM_SINGLE("DMIC2L Switch", LM49453_P0_PORT2_TX2_REG, 2, 1, 0),
SOC_DAPM_SINGLE("DMIC2R Switch", LM49453_P0_PORT2_TX2_REG, 3, 1, 0),
SOC_DAPM_SINGLE("ADCL Switch", LM49453_P0_PORT2_TX2_REG, 4, 1, 0),
SOC_DAPM_SINGLE("ADCR Switch", LM49453_P0_PORT2_TX2_REG, 5, 1, 0),
SOC_DAPM_SINGLE("Port1_2 Switch", LM49453_P0_PORT2_TX2_REG, 6, 1, 0),
SOC_DAPM_SINGLE("Port2_2 Switch", LM49453_P0_PORT2_TX2_REG, 7, 1, 0),
};

/* TLV Declarations */
static const DECLARE_TLV_DB_SCALE(adc_dac_tlv, -7650, 150, 1);
static const DECLARE_TLV_DB_SCALE(mic_tlv, 0, 200, 1);
static const DECLARE_TLV_DB_SCALE(port_tlv, -1800, 600, 0);
static const DECLARE_TLV_DB_SCALE(stn_tlv, -7200, 150, 0);

static const struct snd_kcontrol_new lm49453_sidetone_mixer_controls[] = {
/* Sidetone supports mono only */
SOC_DAPM_SINGLE_TLV("Sidetone ADCL Volume", LM49453_P0_STN_VOL_ADCL_REG,
		     0, 0x3F, 0, stn_tlv),
SOC_DAPM_SINGLE_TLV("Sidetone ADCR Volume", LM49453_P0_STN_VOL_ADCR_REG,
		     0, 0x3F, 0, stn_tlv),
SOC_DAPM_SINGLE_TLV("Sidetone DMIC1L Volume", LM49453_P0_STN_VOL_DMIC1L_REG,
		     0, 0x3F, 0, stn_tlv),
SOC_DAPM_SINGLE_TLV("Sidetone DMIC1R Volume", LM49453_P0_STN_VOL_DMIC1R_REG,
		     0, 0x3F, 0, stn_tlv),
SOC_DAPM_SINGLE_TLV("Sidetone DMIC2L Volume", LM49453_P0_STN_VOL_DMIC2L_REG,
		     0, 0x3F, 0, stn_tlv),
SOC_DAPM_SINGLE_TLV("Sidetone DMIC2R Volume", LM49453_P0_STN_VOL_DMIC2R_REG,
		     0, 0x3F, 0, stn_tlv),
};

static const struct snd_kcontrol_new lm49453_snd_controls[] = {
	/* mic1 and mic2 supports mono only */
	SOC_SINGLE_TLV("Mic1 Volume", LM49453_P0_MICL_REG, 0, 15, 0, mic_tlv),
	SOC_SINGLE_TLV("Mic2 Volume", LM49453_P0_MICR_REG, 0, 15, 0, mic_tlv),

	SOC_SINGLE_TLV("ADCL Volume", LM49453_P0_ADC_LEVELL_REG, 0, 63,
			0, adc_dac_tlv),
	SOC_SINGLE_TLV("ADCR Volume", LM49453_P0_ADC_LEVELR_REG, 0, 63,
			0, adc_dac_tlv),

	SOC_DOUBLE_R_TLV("DMIC1 Volume", LM49453_P0_DMIC1_LEVELL_REG,
			  LM49453_P0_DMIC1_LEVELR_REG, 0, 63, 0, adc_dac_tlv),
	SOC_DOUBLE_R_TLV("DMIC2 Volume", LM49453_P0_DMIC2_LEVELL_REG,
			  LM49453_P0_DMIC2_LEVELR_REG, 0, 63, 0, adc_dac_tlv),

	SOC_DAPM_ENUM("Mic2Mode", lm49453_mic2mode_enum),
	SOC_DAPM_ENUM("DMIC12 SRC", lm49453_dmic12_cfg_enum),
	SOC_DAPM_ENUM("DMIC34 SRC", lm49453_dmic34_cfg_enum),

	/* Capture path filter enable */
	SOC_SINGLE("DMIC1 HPFilter Switch", LM49453_P0_ADC_FX_ENABLES_REG,
					    0, 1, 0),
	SOC_SINGLE("DMIC2 HPFilter Switch", LM49453_P0_ADC_FX_ENABLES_REG,
					    1, 1, 0),
	SOC_SINGLE("ADC HPFilter Switch", LM49453_P0_ADC_FX_ENABLES_REG,
					  2, 1, 0),

	SOC_DOUBLE_R_TLV("DAC HP Volume", LM49453_P0_DAC_HP_LEVELL_REG,
			  LM49453_P0_DAC_HP_LEVELR_REG, 0, 63, 0, adc_dac_tlv),
	SOC_DOUBLE_R_TLV("DAC LO Volume", LM49453_P0_DAC_LO_LEVELL_REG,
			  LM49453_P0_DAC_LO_LEVELR_REG, 0, 63, 0, adc_dac_tlv),
	SOC_DOUBLE_R_TLV("DAC LS Volume", LM49453_P0_DAC_LS_LEVELL_REG,
			  LM49453_P0_DAC_LS_LEVELR_REG, 0, 63, 0, adc_dac_tlv),
	SOC_DOUBLE_R_TLV("DAC HA Volume", LM49453_P0_DAC_HA_LEVELL_REG,
			  LM49453_P0_DAC_HA_LEVELR_REG, 0, 63, 0, adc_dac_tlv),

	SOC_SINGLE_TLV("EP Volume", LM49453_P0_DAC_LS_LEVELL_REG,
			0, 63, 0, adc_dac_tlv),

	SOC_SINGLE_TLV("PORT1_1_RX_LVL Volume", LM49453_P0_PORT1_RX_LVL1_REG,
			0, 3, 0, port_tlv),
	SOC_SINGLE_TLV("PORT1_2_RX_LVL Volume", LM49453_P0_PORT1_RX_LVL1_REG,
			2, 3, 0, port_tlv),
	SOC_SINGLE_TLV("PORT1_3_RX_LVL Volume", LM49453_P0_PORT1_RX_LVL1_REG,
			4, 3, 0, port_tlv),
	SOC_SINGLE_TLV("PORT1_4_RX_LVL Volume", LM49453_P0_PORT1_RX_LVL1_REG,
			6, 3, 0, port_tlv),
	SOC_SINGLE_TLV("PORT1_5_RX_LVL Volume", LM49453_P0_PORT1_RX_LVL2_REG,
			0, 3, 0, port_tlv),
	SOC_SINGLE_TLV("PORT1_6_RX_LVL Volume", LM49453_P0_PORT1_RX_LVL2_REG,
			2, 3, 0, port_tlv),
	SOC_SINGLE_TLV("PORT1_7_RX_LVL Volume", LM49453_P0_PORT1_RX_LVL2_REG,
			4, 3, 0, port_tlv),
	SOC_SINGLE_TLV("PORT1_8_RX_LVL Volume", LM49453_P0_PORT1_RX_LVL2_REG,
			6, 3, 0, port_tlv),

	SOC_SINGLE_TLV("PORT2_1_RX_LVL Volume", LM49453_P0_PORT2_RX_LVL_REG,
			0, 3, 0, port_tlv),
	SOC_SINGLE_TLV("PORT2_2_RX_LVL Volume", LM49453_P0_PORT2_RX_LVL_REG,
			2, 3, 0, port_tlv),

	SOC_SINGLE("Port1 Playback Switch", LM49453_P0_AUDIO_PORT1_BASIC_REG,
		    1, 1, 0),
	SOC_SINGLE("Port2 Playback Switch", LM49453_P0_AUDIO_PORT2_BASIC_REG,
		    1, 1, 0),
	SOC_SINGLE("Port1 Capture Switch", LM49453_P0_AUDIO_PORT1_BASIC_REG,
		    2, 1, 0),
	SOC_SINGLE("Port2 Capture Switch", LM49453_P0_AUDIO_PORT2_BASIC_REG,
		    2, 1, 0)

};

/* DAPM widgets */
static const struct snd_soc_dapm_widget lm49453_dapm_widgets[] = {

	/* All end points HP,EP, LS, Lineout and Haptic */
	SND_SOC_DAPM_OUTPUT("HPOUTL"),
	SND_SOC_DAPM_OUTPUT("HPOUTR"),
	SND_SOC_DAPM_OUTPUT("EPOUT"),
	SND_SOC_DAPM_OUTPUT("LSOUTL"),
	SND_SOC_DAPM_OUTPUT("LSOUTR"),
	SND_SOC_DAPM_OUTPUT("LOOUTR"),
	SND_SOC_DAPM_OUTPUT("LOOUTL"),
	SND_SOC_DAPM_OUTPUT("HAOUTL"),
	SND_SOC_DAPM_OUTPUT("HAOUTR"),

	SND_SOC_DAPM_INPUT("AMIC1"),
	SND_SOC_DAPM_INPUT("AMIC2"),
	SND_SOC_DAPM_INPUT("DMIC1DAT"),
	SND_SOC_DAPM_INPUT("DMIC2DAT"),
	SND_SOC_DAPM_INPUT("AUXL"),
	SND_SOC_DAPM_INPUT("AUXR"),

	SND_SOC_DAPM_PGA("PORT1_1_RX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PORT1_2_RX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PORT1_3_RX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PORT1_4_RX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PORT1_5_RX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PORT1_6_RX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PORT1_7_RX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PORT1_8_RX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PORT2_1_RX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PORT2_2_RX", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("AMIC1Bias", LM49453_P0_MICL_REG, 6, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AMIC2Bias", LM49453_P0_MICR_REG, 6, 0, NULL, 0),

	/* playback path driver enables */
	SND_SOC_DAPM_OUT_DRV("Headset Switch",
			LM49453_P0_PMC_SETUP_REG, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Earpiece Switch",
			LM49453_P0_EP_REG, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Speaker Left Switch",
			LM49453_P0_DIS_PKVL_FB_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Speaker Right Switch",
			LM49453_P0_DIS_PKVL_FB_REG, 1, 1, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Haptic Left Switch",
			LM49453_P0_DIS_PKVL_FB_REG, 2, 1, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Haptic Right Switch",
			LM49453_P0_DIS_PKVL_FB_REG, 3, 1, NULL, 0),

	/* DAC */
	SND_SOC_DAPM_DAC("HPL DAC", "Headset", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("HPR DAC", "Headset", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("LSL DAC", "Speaker", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("LSR DAC", "Speaker", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("HAL DAC", "Haptic", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("HAR DAC", "Haptic", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("LOL DAC", "Lineout", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("LOR DAC", "Lineout", SND_SOC_NOPM, 0, 0),


	SND_SOC_DAPM_PGA("AUXL Input",
			LM49453_P0_ANALOG_MIXER_ADC_REG, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AUXR Input",
			LM49453_P0_ANALOG_MIXER_ADC_REG, 3, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Sidetone", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* ADC */
	SND_SOC_DAPM_ADC("DMIC1 Left", "Capture", SND_SOC_NOPM, 1, 0),
	SND_SOC_DAPM_ADC("DMIC1 Right", "Capture", SND_SOC_NOPM, 1, 0),
	SND_SOC_DAPM_ADC("DMIC2 Left", "Capture", SND_SOC_NOPM, 1, 0),
	SND_SOC_DAPM_ADC("DMIC2 Right", "Capture", SND_SOC_NOPM, 1, 0),

	SND_SOC_DAPM_ADC("ADC Left", "Capture", SND_SOC_NOPM, 1, 0),
	SND_SOC_DAPM_ADC("ADC Right", "Capture", SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MUX("ADCL Mux", SND_SOC_NOPM, 0, 0,
			  &lm49453_adcl_mux_control),
	SND_SOC_DAPM_MUX("ADCR Mux", SND_SOC_NOPM, 0, 0,
			  &lm49453_adcr_mux_control),

	SND_SOC_DAPM_MUX("Mic1 Input",
			SND_SOC_NOPM, 0, 0, &lm49453_adcl_mux_control),

	SND_SOC_DAPM_MUX("Mic2 Input",
			SND_SOC_NOPM, 0, 0, &lm49453_adcr_mux_control),

	/* AIF */
	SND_SOC_DAPM_AIF_IN("PORT1_SDI", NULL, 0,
			    LM49453_P0_PULL_CONFIG1_REG, 2, 0),
	SND_SOC_DAPM_AIF_IN("PORT2_SDI", NULL, 0,
			    LM49453_P0_PULL_CONFIG1_REG, 6, 0),

	SND_SOC_DAPM_AIF_OUT("PORT1_SDO", NULL, 0,
			     LM49453_P0_PULL_CONFIG1_REG, 3, 0),
	SND_SOC_DAPM_AIF_OUT("PORT2_SDO", NULL, 0,
			      LM49453_P0_PULL_CONFIG1_REG, 7, 0),

	/* Port1 TX controls */
	SND_SOC_DAPM_OUT_DRV("P1_1_TX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("P1_2_TX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("P1_3_TX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("P1_4_TX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("P1_5_TX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("P1_6_TX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("P1_7_TX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("P1_8_TX", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Port2 TX controls */
	SND_SOC_DAPM_OUT_DRV("P2_1_TX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("P2_2_TX", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Sidetone Mixer */
	SND_SOC_DAPM_MIXER("Sidetone Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_sidetone_mixer_controls,
			    ARRAY_SIZE(lm49453_sidetone_mixer_controls)),

	/* DAC MIXERS */
	SND_SOC_DAPM_MIXER("HPL Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_headset_left_mixer,
			    ARRAY_SIZE(lm49453_headset_left_mixer)),
	SND_SOC_DAPM_MIXER("HPR Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_headset_right_mixer,
			    ARRAY_SIZE(lm49453_headset_right_mixer)),
	SND_SOC_DAPM_MIXER("LOL Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_lineout_left_mixer,
			    ARRAY_SIZE(lm49453_lineout_left_mixer)),
	SND_SOC_DAPM_MIXER("LOR Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_lineout_right_mixer,
			    ARRAY_SIZE(lm49453_lineout_right_mixer)),
	SND_SOC_DAPM_MIXER("LSL Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_speaker_left_mixer,
			    ARRAY_SIZE(lm49453_speaker_left_mixer)),
	SND_SOC_DAPM_MIXER("LSR Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_speaker_right_mixer,
			    ARRAY_SIZE(lm49453_speaker_right_mixer)),
	SND_SOC_DAPM_MIXER("HAL Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_haptic_left_mixer,
			    ARRAY_SIZE(lm49453_haptic_left_mixer)),
	SND_SOC_DAPM_MIXER("HAR Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_haptic_right_mixer,
			    ARRAY_SIZE(lm49453_haptic_right_mixer)),

	/* Capture Mixer */
	SND_SOC_DAPM_MIXER("Port1_1 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port1_tx1_mixer,
			    ARRAY_SIZE(lm49453_port1_tx1_mixer)),
	SND_SOC_DAPM_MIXER("Port1_2 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port1_tx2_mixer,
			    ARRAY_SIZE(lm49453_port1_tx2_mixer)),
	SND_SOC_DAPM_MIXER("Port1_3 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port1_tx3_mixer,
			    ARRAY_SIZE(lm49453_port1_tx3_mixer)),
	SND_SOC_DAPM_MIXER("Port1_4 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port1_tx4_mixer,
			    ARRAY_SIZE(lm49453_port1_tx4_mixer)),
	SND_SOC_DAPM_MIXER("Port1_5 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port1_tx5_mixer,
			    ARRAY_SIZE(lm49453_port1_tx5_mixer)),
	SND_SOC_DAPM_MIXER("Port1_6 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port1_tx6_mixer,
			    ARRAY_SIZE(lm49453_port1_tx6_mixer)),
	SND_SOC_DAPM_MIXER("Port1_7 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port1_tx7_mixer,
			    ARRAY_SIZE(lm49453_port1_tx7_mixer)),
	SND_SOC_DAPM_MIXER("Port1_8 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port1_tx8_mixer,
			    ARRAY_SIZE(lm49453_port1_tx8_mixer)),

	SND_SOC_DAPM_MIXER("Port2_1 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port2_tx1_mixer,
			    ARRAY_SIZE(lm49453_port2_tx1_mixer)),
	SND_SOC_DAPM_MIXER("Port2_2 Mixer", SND_SOC_NOPM, 0, 0,
			    lm49453_port2_tx2_mixer,
			    ARRAY_SIZE(lm49453_port2_tx2_mixer)),
};

static const struct snd_soc_dapm_route lm49453_audio_map[] = {
	/* Port SDI mapping */
	{ "PORT1_1_RX", "Port1 Playback Switch", "PORT1_SDI" },
	{ "PORT1_2_RX", "Port1 Playback Switch", "PORT1_SDI" },
	{ "PORT1_3_RX", "Port1 Playback Switch", "PORT1_SDI" },
	{ "PORT1_4_RX", "Port1 Playback Switch", "PORT1_SDI" },
	{ "PORT1_5_RX", "Port1 Playback Switch", "PORT1_SDI" },
	{ "PORT1_6_RX", "Port1 Playback Switch", "PORT1_SDI" },
	{ "PORT1_7_RX", "Port1 Playback Switch", "PORT1_SDI" },
	{ "PORT1_8_RX", "Port1 Playback Switch", "PORT1_SDI" },

	{ "PORT2_1_RX", "Port2 Playback Switch", "PORT2_SDI" },
	{ "PORT2_2_RX", "Port2 Playback Switch", "PORT2_SDI" },

	/* HP mapping */
	{ "HPL Mixer", "Port1_1 Switch", "PORT1_1_RX" },
	{ "HPL Mixer", "Port1_2 Switch", "PORT1_2_RX" },
	{ "HPL Mixer", "Port1_3 Switch", "PORT1_3_RX" },
	{ "HPL Mixer", "Port1_4 Switch", "PORT1_4_RX" },
	{ "HPL Mixer", "Port1_5 Switch", "PORT1_5_RX" },
	{ "HPL Mixer", "Port1_6 Switch", "PORT1_6_RX" },
	{ "HPL Mixer", "Port1_7 Switch", "PORT1_7_RX" },
	{ "HPL Mixer", "Port1_8 Switch", "PORT1_8_RX" },

	{ "HPL Mixer", "Port2_1 Switch", "PORT2_1_RX" },
	{ "HPL Mixer", "Port2_2 Switch", "PORT2_2_RX" },

	{ "HPL Mixer", "ADCL Switch", "ADC Left" },
	{ "HPL Mixer", "ADCR Switch", "ADC Right" },
	{ "HPL Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "HPL Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "HPL Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "HPL Mixer", "DMIC2R Switch", "DMIC2 Right" },
	{ "HPL Mixer", "Sidetone Switch", "Sidetone" },

	{ "HPL DAC", NULL, "HPL Mixer" },

	{ "HPR Mixer", "Port1_1 Switch", "PORT1_1_RX" },
	{ "HPR Mixer", "Port1_2 Switch", "PORT1_2_RX" },
	{ "HPR Mixer", "Port1_3 Switch", "PORT1_3_RX" },
	{ "HPR Mixer", "Port1_4 Switch", "PORT1_4_RX" },
	{ "HPR Mixer", "Port1_5 Switch", "PORT1_5_RX" },
	{ "HPR Mixer", "Port1_6 Switch", "PORT1_6_RX" },
	{ "HPR Mixer", "Port1_7 Switch", "PORT1_7_RX" },
	{ "HPR Mixer", "Port1_8 Switch", "PORT1_8_RX" },

	/* Port 2 */
	{ "HPR Mixer", "Port2_1 Switch", "PORT2_1_RX" },
	{ "HPR Mixer", "Port2_2 Switch", "PORT2_2_RX" },

	{ "HPR Mixer", "ADCL Switch", "ADC Left" },
	{ "HPR Mixer", "ADCR Switch", "ADC Right" },
	{ "HPR Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "HPR Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "HPR Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "HPR Mixer", "DMIC2L Switch", "DMIC2 Right" },
	{ "HPR Mixer", "Sidetone Switch", "Sidetone" },

	{ "HPR DAC", NULL, "HPR Mixer" },

	{ "HPOUTL", "Headset Switch", "HPL DAC"},
	{ "HPOUTR", "Headset Switch", "HPR DAC"},

	/* EP map */
	{ "EPOUT", "Earpiece Switch", "HPL DAC" },

	/* Speaker map */
	{ "LSL Mixer", "Port1_1 Switch", "PORT1_1_RX" },
	{ "LSL Mixer", "Port1_2 Switch", "PORT1_2_RX" },
	{ "LSL Mixer", "Port1_3 Switch", "PORT1_3_RX" },
	{ "LSL Mixer", "Port1_4 Switch", "PORT1_4_RX" },
	{ "LSL Mixer", "Port1_5 Switch", "PORT1_5_RX" },
	{ "LSL Mixer", "Port1_6 Switch", "PORT1_6_RX" },
	{ "LSL Mixer", "Port1_7 Switch", "PORT1_7_RX" },
	{ "LSL Mixer", "Port1_8 Switch", "PORT1_8_RX" },

	/* Port 2 */
	{ "LSL Mixer", "Port2_1 Switch", "PORT2_1_RX" },
	{ "LSL Mixer", "Port2_2 Switch", "PORT2_2_RX" },

	{ "LSL Mixer", "ADCL Switch", "ADC Left" },
	{ "LSL Mixer", "ADCR Switch", "ADC Right" },
	{ "LSL Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "LSL Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "LSL Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "LSL Mixer", "DMIC2R Switch", "DMIC2 Right" },
	{ "LSL Mixer", "Sidetone Switch", "Sidetone" },

	{ "LSL DAC", NULL, "LSL Mixer" },

	{ "LSR Mixer", "Port1_1 Switch", "PORT1_1_RX" },
	{ "LSR Mixer", "Port1_2 Switch", "PORT1_2_RX" },
	{ "LSR Mixer", "Port1_3 Switch", "PORT1_3_RX" },
	{ "LSR Mixer", "Port1_4 Switch", "PORT1_4_RX" },
	{ "LSR Mixer", "Port1_5 Switch", "PORT1_5_RX" },
	{ "LSR Mixer", "Port1_6 Switch", "PORT1_6_RX" },
	{ "LSR Mixer", "Port1_7 Switch", "PORT1_7_RX" },
	{ "LSR Mixer", "Port1_8 Switch", "PORT1_8_RX" },

	/* Port 2 */
	{ "LSR Mixer", "Port2_1 Switch", "PORT2_1_RX" },
	{ "LSR Mixer", "Port2_2 Switch", "PORT2_2_RX" },

	{ "LSR Mixer", "ADCL Switch", "ADC Left" },
	{ "LSR Mixer", "ADCR Switch", "ADC Right" },
	{ "LSR Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "LSR Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "LSR Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "LSR Mixer", "DMIC2R Switch", "DMIC2 Right" },
	{ "LSR Mixer", "Sidetone Switch", "Sidetone" },

	{ "LSR DAC", NULL, "LSR Mixer" },

	{ "LSOUTL", "Speaker Left Switch", "LSL DAC"},
	{ "LSOUTR", "Speaker Left Switch", "LSR DAC"},

	/* Haptic map */
	{ "HAL Mixer", "Port1_1 Switch", "PORT1_1_RX" },
	{ "HAL Mixer", "Port1_2 Switch", "PORT1_2_RX" },
	{ "HAL Mixer", "Port1_3 Switch", "PORT1_3_RX" },
	{ "HAL Mixer", "Port1_4 Switch", "PORT1_4_RX" },
	{ "HAL Mixer", "Port1_5 Switch", "PORT1_5_RX" },
	{ "HAL Mixer", "Port1_6 Switch", "PORT1_6_RX" },
	{ "HAL Mixer", "Port1_7 Switch", "PORT1_7_RX" },
	{ "HAL Mixer", "Port1_8 Switch", "PORT1_8_RX" },

	/* Port 2 */
	{ "HAL Mixer", "Port2_1 Switch", "PORT2_1_RX" },
	{ "HAL Mixer", "Port2_2 Switch", "PORT2_2_RX" },

	{ "HAL Mixer", "ADCL Switch", "ADC Left" },
	{ "HAL Mixer", "ADCR Switch", "ADC Right" },
	{ "HAL Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "HAL Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "HAL Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "HAL Mixer", "DMIC2R Switch", "DMIC2 Right" },
	{ "HAL Mixer", "Sidetone Switch", "Sidetone" },

	{ "HAL DAC", NULL, "HAL Mixer" },

	{ "HAR Mixer", "Port1_1 Switch", "PORT1_1_RX" },
	{ "HAR Mixer", "Port1_2 Switch", "PORT1_2_RX" },
	{ "HAR Mixer", "Port1_3 Switch", "PORT1_3_RX" },
	{ "HAR Mixer", "Port1_4 Switch", "PORT1_4_RX" },
	{ "HAR Mixer", "Port1_5 Switch", "PORT1_5_RX" },
	{ "HAR Mixer", "Port1_6 Switch", "PORT1_6_RX" },
	{ "HAR Mixer", "Port1_7 Switch", "PORT1_7_RX" },
	{ "HAR Mixer", "Port1_8 Switch", "PORT1_8_RX" },

	/* Port 2 */
	{ "HAR Mixer", "Port2_1 Switch", "PORT2_1_RX" },
	{ "HAR Mixer", "Port2_2 Switch", "PORT2_2_RX" },

	{ "HAR Mixer", "ADCL Switch", "ADC Left" },
	{ "HAR Mixer", "ADCR Switch", "ADC Right" },
	{ "HAR Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "HAR Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "HAR Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "HAR Mixer", "DMIC2R Switch", "DMIC2 Right" },
	{ "HAR Mixer", "Sideton Switch", "Sidetone" },

	{ "HAR DAC", NULL, "HAR Mixer" },

	{ "HAOUTL", "Haptic Left Switch", "HAL DAC" },
	{ "HAOUTR", "Haptic Right Switch", "HAR DAC" },

	/* Lineout map */
	{ "LOL Mixer", "Port1_1 Switch", "PORT1_1_RX" },
	{ "LOL Mixer", "Port1_2 Switch", "PORT1_2_RX" },
	{ "LOL Mixer", "Port1_3 Switch", "PORT1_3_RX" },
	{ "LOL Mixer", "Port1_4 Switch", "PORT1_4_RX" },
	{ "LOL Mixer", "Port1_5 Switch", "PORT1_5_RX" },
	{ "LOL Mixer", "Port1_6 Switch", "PORT1_6_RX" },
	{ "LOL Mixer", "Port1_7 Switch", "PORT1_7_RX" },
	{ "LOL Mixer", "Port1_8 Switch", "PORT1_8_RX" },

	/* Port 2 */
	{ "LOL Mixer", "Port2_1 Switch", "PORT2_1_RX" },
	{ "LOL Mixer", "Port2_2 Switch", "PORT2_2_RX" },

	{ "LOL Mixer", "ADCL Switch", "ADC Left" },
	{ "LOL Mixer", "ADCR Switch", "ADC Right" },
	{ "LOL Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "LOL Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "LOL Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "LOL Mixer", "DMIC2R Switch", "DMIC2 Right" },
	{ "LOL Mixer", "Sidetone Switch", "Sidetone" },

	{ "LOL DAC", NULL, "LOL Mixer" },

	{ "LOR Mixer", "Port1_1 Switch", "PORT1_1_RX" },
	{ "LOR Mixer", "Port1_2 Switch", "PORT1_2_RX" },
	{ "LOR Mixer", "Port1_3 Switch", "PORT1_3_RX" },
	{ "LOR Mixer", "Port1_4 Switch", "PORT1_4_RX" },
	{ "LOR Mixer", "Port1_5 Switch", "PORT1_5_RX" },
	{ "LOR Mixer", "Port1_6 Switch", "PORT1_6_RX" },
	{ "LOR Mixer", "Port1_7 Switch", "PORT1_7_RX" },
	{ "LOR Mixer", "Port1_8 Switch", "PORT1_8_RX" },

	/* Port 2 */
	{ "LOR Mixer", "Port2_1 Switch", "PORT2_1_RX" },
	{ "LOR Mixer", "Port2_2 Switch", "PORT2_2_RX" },

	{ "LOR Mixer", "ADCL Switch", "ADC Left" },
	{ "LOR Mixer", "ADCR Switch", "ADC Right" },
	{ "LOR Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "LOR Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "LOR Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "LOR Mixer", "DMIC2R Switch", "DMIC2 Right" },
	{ "LOR Mixer", "Sidetone Switch", "Sidetone" },

	{ "LOR DAC", NULL, "LOR Mixer" },

	{ "LOOUTL", NULL, "LOL DAC" },
	{ "LOOUTR", NULL, "LOR DAC" },

	/* TX map */
	/* Port1 mappings */
	{ "Port1_1 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port1_1 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port1_1 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port1_1 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port1_1 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port1_1 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "Port1_2 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port1_2 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port1_2 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port1_2 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port1_2 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port1_2 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "Port1_3 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port1_3 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port1_3 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port1_3 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port1_3 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port1_3 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "Port1_4 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port1_4 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port1_4 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port1_4 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port1_4 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port1_4 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "Port1_5 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port1_5 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port1_5 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port1_5 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port1_5 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port1_5 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "Port1_6 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port1_6 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port1_6 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port1_6 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port1_6 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port1_6 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "Port1_7 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port1_7 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port1_7 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port1_7 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port1_7 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port1_7 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "Port1_8 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port1_8 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port1_8 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port1_8 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port1_8 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port1_8 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "Port2_1 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port2_1 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port2_1 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port2_1 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port2_1 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port2_1 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "Port2_2 Mixer", "ADCL Switch", "ADC Left" },
	{ "Port2_2 Mixer", "ADCR Switch", "ADC Right" },
	{ "Port2_2 Mixer", "DMIC1L Switch", "DMIC1 Left" },
	{ "Port2_2 Mixer", "DMIC1R Switch", "DMIC1 Right" },
	{ "Port2_2 Mixer", "DMIC2L Switch", "DMIC2 Left" },
	{ "Port2_2 Mixer", "DMIC2R Switch", "DMIC2 Right" },

	{ "P1_1_TX", NULL, "Port1_1 Mixer" },
	{ "P1_2_TX", NULL, "Port1_2 Mixer" },
	{ "P1_3_TX", NULL, "Port1_3 Mixer" },
	{ "P1_4_TX", NULL, "Port1_4 Mixer" },
	{ "P1_5_TX", NULL, "Port1_5 Mixer" },
	{ "P1_6_TX", NULL, "Port1_6 Mixer" },
	{ "P1_7_TX", NULL, "Port1_7 Mixer" },
	{ "P1_8_TX", NULL, "Port1_8 Mixer" },

	{ "P2_1_TX", NULL, "Port2_1 Mixer" },
	{ "P2_2_TX", NULL, "Port2_2 Mixer" },

	{ "PORT1_SDO", "Port1 Capture Switch", "P1_1_TX"},
	{ "PORT1_SDO", "Port1 Capture Switch", "P1_2_TX"},
	{ "PORT1_SDO", "Port1 Capture Switch", "P1_3_TX"},
	{ "PORT1_SDO", "Port1 Capture Switch", "P1_4_TX"},
	{ "PORT1_SDO", "Port1 Capture Switch", "P1_5_TX"},
	{ "PORT1_SDO", "Port1 Capture Switch", "P1_6_TX"},
	{ "PORT1_SDO", "Port1 Capture Switch", "P1_7_TX"},
	{ "PORT1_SDO", "Port1 Capture Switch", "P1_8_TX"},

	{ "PORT2_SDO", "Port2 Capture Switch", "P2_1_TX"},
	{ "PORT2_SDO", "Port2 Capture Switch", "P2_2_TX"},

	{ "Mic1 Input", NULL, "AMIC1" },
	{ "Mic2 Input", NULL, "AMIC2" },

	{ "AUXL Input", NULL, "AUXL" },
	{ "AUXR Input", NULL, "AUXR" },

	/* AUX connections */
	{ "ADCL Mux", "Aux_L", "AUXL Input" },
	{ "ADCL Mux", "MIC1", "Mic1 Input" },

	{ "ADCR Mux", "Aux_R", "AUXR Input" },
	{ "ADCR Mux", "MIC2", "Mic2 Input" },

	/* ADC connection */
	{ "ADC Left", NULL, "ADCL Mux"},
	{ "ADC Right", NULL, "ADCR Mux"},

	{ "DMIC1 Left", NULL, "DMIC1DAT"},
	{ "DMIC1 Right", NULL, "DMIC1DAT"},
	{ "DMIC2 Left", NULL, "DMIC2DAT"},
	{ "DMIC2 Right", NULL, "DMIC2DAT"},

	/* Sidetone map */
	{ "Sidetone Mixer", NULL, "ADC Left" },
	{ "Sidetone Mixer", NULL, "ADC Right" },
	{ "Sidetone Mixer", NULL, "DMIC1 Left" },
	{ "Sidetone Mixer", NULL, "DMIC1 Right" },
	{ "Sidetone Mixer", NULL, "DMIC2 Left" },
	{ "Sidetone Mixer", NULL, "DMIC2 Right" },

	{ "Sidetone", "Sidetone Switch", "Sidetone Mixer" },
};

static int lm49453_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	u16 clk_div = 0;

	/* Setting DAC clock dividers based on substream sample rate. */
	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 32000:
	case 24000:
	case 48000:
		clk_div = 256;
		break;
	case 11025:
	case 22050:
	case 44100:
		clk_div = 216;
		break;
	case 96000:
		clk_div = 127;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, LM49453_P0_ADC_CLK_DIV_REG, clk_div);
	snd_soc_component_write(component, LM49453_P0_DAC_HP_CLK_DIV_REG, clk_div);

	return 0;
}

static int lm49453_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;

	u16 aif_val;
	int mode = 0;
	int clk_phase = 0;
	int clk_shift = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		aif_val = 0;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		aif_val = LM49453_AUDIO_PORT1_BASIC_SYNC_MS;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		aif_val = LM49453_AUDIO_PORT1_BASIC_CLK_MS;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif_val = LM49453_AUDIO_PORT1_BASIC_CLK_MS |
			  LM49453_AUDIO_PORT1_BASIC_SYNC_MS;
		break;
	default:
		return -EINVAL;
	}


	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_DSP_A:
		mode = 1;
		clk_phase = (1 << 5);
		clk_shift = 1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		mode = 1;
		clk_phase = (1 << 5);
		clk_shift = 0;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, LM49453_P0_AUDIO_PORT1_BASIC_REG,
			    LM49453_AUDIO_PORT1_BASIC_FMT_MASK|BIT(0)|BIT(5),
			    (aif_val | mode | clk_phase));

	snd_soc_component_write(component, LM49453_P0_AUDIO_PORT1_RX_MSB_REG, clk_shift);

	return 0;
}

static int lm49453_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				  unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	u16 pll_clk = 0;

	switch (freq) {
	case 12288000:
	case 26000000:
	case 19200000:
		/* pll clk slection */
		pll_clk = 0;
		break;
	case 48000:
	case 32576:
		/* fll clk slection */
		pll_clk = BIT(4);
		return 0;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, LM49453_P0_PMC_SETUP_REG, BIT(4), pll_clk);

	return 0;
}

static int lm49453_hp_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	snd_soc_component_update_bits(dai->component, LM49453_P0_DAC_DSP_REG, BIT(1)|BIT(0),
			    (mute ? (BIT(1)|BIT(0)) : 0));
	return 0;
}

static int lm49453_lo_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	snd_soc_component_update_bits(dai->component, LM49453_P0_DAC_DSP_REG, BIT(3)|BIT(2),
			    (mute ? (BIT(3)|BIT(2)) : 0));
	return 0;
}

static int lm49453_ls_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	snd_soc_component_update_bits(dai->component, LM49453_P0_DAC_DSP_REG, BIT(5)|BIT(4),
			    (mute ? (BIT(5)|BIT(4)) : 0));
	return 0;
}

static int lm49453_ep_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	snd_soc_component_update_bits(dai->component, LM49453_P0_DAC_DSP_REG, BIT(4),
			    (mute ? BIT(4) : 0));
	return 0;
}

static int lm49453_ha_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	snd_soc_component_update_bits(dai->component, LM49453_P0_DAC_DSP_REG, BIT(7)|BIT(6),
			    (mute ? (BIT(7)|BIT(6)) : 0));
	return 0;
}

static int lm49453_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	struct lm49453_priv *lm49453 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			regcache_sync(lm49453->regmap);

		snd_soc_component_update_bits(component, LM49453_P0_PMC_SETUP_REG,
				    LM49453_PMC_SETUP_CHIP_EN, LM49453_CHIP_EN);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, LM49453_P0_PMC_SETUP_REG,
				    LM49453_PMC_SETUP_CHIP_EN, 0);
		break;
	}

	return 0;
}

/* Formates supported by LM49453 driver. */
#define LM49453_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops lm49453_headset_dai_ops = {
	.hw_params	= lm49453_hw_params,
	.set_sysclk	= lm49453_set_dai_sysclk,
	.set_fmt	= lm49453_set_dai_fmt,
	.mute_stream	= lm49453_hp_mute,
	.no_capture_mute = 1,
};

static const struct snd_soc_dai_ops lm49453_speaker_dai_ops = {
	.hw_params	= lm49453_hw_params,
	.set_sysclk	= lm49453_set_dai_sysclk,
	.set_fmt	= lm49453_set_dai_fmt,
	.mute_stream	= lm49453_ls_mute,
	.no_capture_mute = 1,
};

static const struct snd_soc_dai_ops lm49453_haptic_dai_ops = {
	.hw_params	= lm49453_hw_params,
	.set_sysclk	= lm49453_set_dai_sysclk,
	.set_fmt	= lm49453_set_dai_fmt,
	.mute_stream	= lm49453_ha_mute,
	.no_capture_mute = 1,
};

static const struct snd_soc_dai_ops lm49453_ep_dai_ops = {
	.hw_params	= lm49453_hw_params,
	.set_sysclk	= lm49453_set_dai_sysclk,
	.set_fmt	= lm49453_set_dai_fmt,
	.mute_stream	= lm49453_ep_mute,
	.no_capture_mute = 1,
};

static const struct snd_soc_dai_ops lm49453_lineout_dai_ops = {
	.hw_params	= lm49453_hw_params,
	.set_sysclk	= lm49453_set_dai_sysclk,
	.set_fmt	= lm49453_set_dai_fmt,
	.mute_stream	= lm49453_lo_mute,
	.no_capture_mute = 1,
};

/* LM49453 dai structure. */
static struct snd_soc_dai_driver lm49453_dai[] = {
	{
		.name = "LM49453 Headset",
		.playback = {
			.stream_name = "Headset",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = LM49453_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 5,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = LM49453_FORMATS,
		},
		.ops = &lm49453_headset_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "LM49453 Speaker",
		.playback = {
			.stream_name = "Speaker",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = LM49453_FORMATS,
		},
		.ops = &lm49453_speaker_dai_ops,
	},
	{
		.name = "LM49453 Haptic",
		.playback = {
			.stream_name = "Haptic",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = LM49453_FORMATS,
		},
		.ops = &lm49453_haptic_dai_ops,
	},
	{
		.name = "LM49453 Earpiece",
		.playback = {
			.stream_name = "Earpiece",
			.channels_min = 1,
			.channels_max = 1,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = LM49453_FORMATS,
		},
		.ops = &lm49453_ep_dai_ops,
	},
	{
		.name = "LM49453 line out",
		.playback = {
			.stream_name = "Lineout",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = LM49453_FORMATS,
		},
		.ops = &lm49453_lineout_dai_ops,
	},
};

static const struct snd_soc_component_driver soc_component_dev_lm49453 = {
	.set_bias_level		= lm49453_set_bias_level,
	.controls		= lm49453_snd_controls,
	.num_controls		= ARRAY_SIZE(lm49453_snd_controls),
	.dapm_widgets		= lm49453_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(lm49453_dapm_widgets),
	.dapm_routes		= lm49453_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(lm49453_audio_map),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config lm49453_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LM49453_MAX_REGISTER,
	.reg_defaults = lm49453_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(lm49453_reg_defs),
	.cache_type = REGCACHE_RBTREE,
};

static int lm49453_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct lm49453_priv *lm49453;
	int ret = 0;

	lm49453 = devm_kzalloc(&i2c->dev, sizeof(struct lm49453_priv),
				GFP_KERNEL);

	if (lm49453 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, lm49453);

	lm49453->regmap = devm_regmap_init_i2c(i2c, &lm49453_regmap_config);
	if (IS_ERR(lm49453->regmap)) {
		ret = PTR_ERR(lm49453->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret =  devm_snd_soc_register_component(&i2c->dev,
				      &soc_component_dev_lm49453,
				      lm49453_dai, ARRAY_SIZE(lm49453_dai));
	if (ret < 0)
		dev_err(&i2c->dev, "Failed to register component: %d\n", ret);

	return ret;
}

static int lm49453_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id lm49453_i2c_id[] = {
	{ "lm49453", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm49453_i2c_id);

static struct i2c_driver lm49453_i2c_driver = {
	.driver = {
		.name = "lm49453",
	},
	.probe = lm49453_i2c_probe,
	.remove = lm49453_i2c_remove,
	.id_table = lm49453_i2c_id,
};

module_i2c_driver(lm49453_i2c_driver);

MODULE_DESCRIPTION("ASoC LM49453 driver");
MODULE_AUTHOR("M R Swami Reddy <MR.Swami.Reddy@ti.com>");
MODULE_LICENSE("GPL v2");
