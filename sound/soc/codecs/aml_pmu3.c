/*
 * aml_pmu3.c  --  AML_PMU3 ALSA Soc Codec driver
 *
 * Copyright 2013 AMLOGIC.
 *
 * Author: Shuai Li<shuai.li@amlogic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>
//#include <sound/aml_pmu3.h>
#include <linux/amlogic/aml_pmu.h>
#include "aml_pmu3.h"
#include <linux/reboot.h>
#include <linux/notifier.h>


static u16 pmu3_reg_defaults[] = {
	0x0000,     /* R00h	- SW Reset */
	0x0000, 	/* R01h	- Block Enable1 */
	0x0000, 	/* R02h	- Block Enable2 */
	0x0000, 	/* R03h	- PGAIN */
	0x0000, 	/* R04h	- MIXINL */
	0x0000, 	/* R05h	- MIXINR */
	0x0000, 	/* R06h	- MiXOUTL */
	0x0000, 	/* R07h	- MiXOUTR */
	0x0000, 	/* R08h	- RXV TO MIXOUT */
	0x0000, 	/* R09h	- Lineout&HP Driver */
	0x0000, 	/* R0Ah	- HP DC Offset */
	0x2000, 	/* R0Bh	- ADC & DAC */
	0x00A8, 	/* R0Ch	- HP & MIC Detect */
	0x5050, 	/* R0Dh	- ADC Digital Volume */
	0xC0C0, 	/* R0Eh	- DAC Digital Volume */
	0xA000, 	/* R0Fh	- Soft Mute and Unmute */
	0x0000, 	/* R10h	- Digital Sidetone & Mixing */
	0x0000, 	/* R11h	- DMIC & GPIO_AUDIO */
	0x0000,		/* R12h  - Monitor register */
};

struct aml_pmu3_priv {
	struct snd_soc_codec *codec;
	int sysclk;
	enum snd_soc_control_type control_type;
	void *control_data;
};

static int pmu3_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case PMU3_SOFTWARE_RESET:
		return 1;

	default:
		return 0;
	}
}

static void pmu3_reset(struct snd_soc_codec *codec)
{
	snd_soc_write(codec, PMU3_SOFTWARE_RESET, 0x500);
	msleep(1);
}

static int pmu3_adc_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	u16 val = snd_soc_read(codec, PMU3_BLOCK_ENABLE_2);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val |= 0x2;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		val &= ~0x2;
		break;

	default:
		BUG();
	}

	snd_soc_write(codec, PMU3_BLOCK_ENABLE_2, val);

	return 0;
}

static int pmu3_dac_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	u16 val = snd_soc_read(codec, PMU3_BLOCK_ENABLE_2);
        unsigned int mask = 1<<w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		val |= 0x1;
		val |= mask;
		break;

	case SND_SOC_DAPM_POST_PMD:
		//val &= ~0x1;
		break;

	default:
		BUG();
	}

	snd_soc_write(codec, PMU3_BLOCK_ENABLE_2, val);

	return 0;
}

static int pmu3_hp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		break;

	case SND_SOC_DAPM_POST_PMU:
		/* Enable the input stage of HP driver 
		and start the HP DC offset cancellation process */
		snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_2 , 0x1800, 0x1800);

		msleep(15);

		/* Finish the HP DC offset cancellation process */
		snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_2 , 0x1000, 0);

		/* Remove shorting the HP driver output and enable its output stage */
		snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_2 , 0x400, 0x400);
		snd_soc_update_bits(codec, PMU3_LINEOUT_HP_DRV , 0x2, 0x2);

		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, PMU3_LINEOUT_HP_DRV , 0x2, 0x0);
		snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_2 , 0x400, 0x0);
		snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_2 , 0x1800, 0x0);

		break;

	case SND_SOC_DAPM_POST_PMD:
		break;

	default:
		BUG();
	}

	return 0;
}

static const DECLARE_TLV_DB_SCALE(out_pga_tlv, -4000, 200, 1);
static const DECLARE_TLV_DB_SCALE(dac_boost_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -3000, 37, 1);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -7200, 37, 1);
static const DECLARE_TLV_DB_SCALE(adc_svol_tlv, -3750, 250, 1);

static const DECLARE_TLV_DB_SCALE(pga_in_tlv, -250, 250, 1);
static const DECLARE_TLV_DB_SCALE(pga2mixin_tlv, -1600, 150, 1);
static const DECLARE_TLV_DB_SCALE(xx2mixout_tlv, -5400, 200, 1);
static const DECLARE_TLV_DB_SCALE(xx2mixin_tlv, -1600, 150, 1);

static const char *mic_bias_level_txt[] = { "0.72*CPAVDD25", "0.85*CPAVDD25" };

/* ADC HPF Mode*/
static const char *adc_hpf_mode_txt[] = {
	"Voice", "Hi-fi" };

static const char *lr_txt[] = {
	"Left", "Right" };

static const char *rl_txt[] = {
	"Right", "Left" };

static const char *sidetone_txt[] = {
	"Disabled", "Left ADC", "Right ADC" };

static const char *dac_mute_rate_txt[] = { "Fast", "Slow" };

static const char *pmu3_lol1_in_texts[] = {
	"Off", "MIXOUTL", "MIXOUTR", "Inverted MIXOUTR"
};

static const char *pmu3_lol2_in_texts[] = {
	"Off", "MIXOUTL", "Inverted MIXOUTL", "Inverted MIXOUTR"
};

static const char *pmu3_lor1_in_texts[] = {
	"Off", "MIXOUTR", "MIXOUTL", "Inverted MIXOUTL"
};

static const char *pmu3_lor2_in_texts[] = {
	"Off", "MIXOUTR", "Inverted MIXOUTR", "Inverted MIXOUTL"
};

static const struct soc_enum pmu3_lineout_enum[] = {
	SOC_ENUM_SINGLE(PMU3_LINEOUT_HP_DRV, 14, ARRAY_SIZE(pmu3_lol1_in_texts),
			pmu3_lol1_in_texts),
	SOC_ENUM_SINGLE(PMU3_LINEOUT_HP_DRV, 12, ARRAY_SIZE(pmu3_lol2_in_texts),
			pmu3_lol2_in_texts),
	SOC_ENUM_SINGLE(PMU3_LINEOUT_HP_DRV, 10, ARRAY_SIZE(pmu3_lor1_in_texts),
			pmu3_lor1_in_texts),
	SOC_ENUM_SINGLE(PMU3_LINEOUT_HP_DRV, 8, ARRAY_SIZE(pmu3_lor2_in_texts),
			pmu3_lor2_in_texts),
};

static const SOC_ENUM_SINGLE_DECL(
	mic_bias1_enum, PMU3_HP_MIC_DET,
	PMU3_MIC_BIAS1_SHIFT, mic_bias_level_txt);

static const SOC_ENUM_SINGLE_DECL(
	mic_bias2_enum, PMU3_HP_MIC_DET,
	PMU3_MIC_BIAS2_SHIFT, mic_bias_level_txt);

static const SOC_ENUM_SINGLE_DECL(
	adc_hpf_mode, PMU3_ADC_DAC,
	PMU3_ADC_HPF_MODE_SHIFT, adc_hpf_mode_txt);
static const SOC_ENUM_SINGLE_DECL(
	aifl_src, PMU3_SIDETONE_MIXING,
	PMU3_ADCDATL_SRC_SHIFT, lr_txt);
static const SOC_ENUM_SINGLE_DECL(
	aifr_src, PMU3_SIDETONE_MIXING,
	PMU3_ADCDATR_SRC_SHIFT, rl_txt);
static const SOC_ENUM_SINGLE_DECL(
	dacl_src, PMU3_SIDETONE_MIXING,
	PMU3_DACDATL_SRC_SHIFT, lr_txt);
static const SOC_ENUM_SINGLE_DECL(
	dacr_src, PMU3_SIDETONE_MIXING,
	PMU3_DACDATR_SRC_SHIFT, rl_txt);
static const SOC_ENUM_SINGLE_DECL(
	dacl_sidetone, PMU3_SIDETONE_MIXING,
	PMU3_DACL_ST_SRC_SHIFT, sidetone_txt);
static const SOC_ENUM_SINGLE_DECL(
	dacr_sidetone, PMU3_SIDETONE_MIXING,
	PMU3_DACR_ST_SRC_SHIFT, sidetone_txt);

static const SOC_ENUM_SINGLE_DECL(
	dac_mute_rate, PMU3_SOFT_MUTE,
	PMU3_DAC_RAMP_RATE_SHIFT, dac_mute_rate_txt);

static const struct snd_kcontrol_new pmu3_snd_controls[] = {
/* MIC */
SOC_ENUM("Mic Bias1 Level", mic_bias1_enum),
SOC_ENUM("Mic Bias2 Level", mic_bias2_enum),

SOC_SINGLE_TLV("PGAIN Left Gain", PMU3_PGA_IN, 12, 0xf, 0, pga_in_tlv),
SOC_SINGLE_TLV("PGAIN Right Gain", PMU3_PGA_IN, 4, 0xf, 0, pga_in_tlv),


/* ADC */
SOC_SINGLE_TLV("Left ADC Sidetone Volume", PMU3_SIDETONE_MIXING, 12, 0xf, 0,
	       	adc_svol_tlv),
SOC_SINGLE_TLV("Right ADC Sidetone Volume", PMU3_SIDETONE_MIXING, 8, 0xf, 0,
			adc_svol_tlv),

SOC_DOUBLE_TLV("Digital Capture Volume", 
		PMU3_ADC_VOLUME_CTL, 8, 0, 0x7f, 0, adc_tlv),

SOC_SINGLE("ADC HPF Switch", PMU3_ADC_DAC, 11, 1, 0),
SOC_ENUM("ADC HPF Mode", adc_hpf_mode),

SOC_ENUM("Left Digital Audio Source", aifl_src),
SOC_ENUM("Right Digital Audio Source", aifr_src),
SOC_SINGLE_TLV("DACL to MIXOUTL Volume", PMU3_MIXOUT_L, 11, 0x1f, 0,
		   xx2mixout_tlv),
SOC_SINGLE_TLV("DACR to MIXOUTR Volume", PMU3_MIXOUT_R, 11, 0x1f, 0,
		   xx2mixout_tlv),

SOC_ENUM("DACL DATA Source", dacl_src),
SOC_ENUM("DACR DATA Source", dacr_src),
SOC_ENUM("DACL Sidetone Source", dacl_sidetone),
SOC_ENUM("DACR Sidetone Source", dacr_sidetone),
SOC_DOUBLE("DAC Invert Switch", PMU3_SOFT_MUTE, 8, 7, 1, 0),
SOC_DOUBLE("ADC Invert Switch", PMU3_SOFT_MUTE, 10, 9, 1, 0),

SOC_SINGLE("DAC Soft Mute Switch", PMU3_SOFT_MUTE, 15, 1, 0),
SOC_ENUM("DAC Mute Rate", dac_mute_rate),
//SOC_SINGLE("DAC Mono Switch", PMU3_SIDETONE_MIXING, 5, 1, 0),

SOC_DOUBLE_TLV("Digital Playback Volume",
		 PMU3_DAC_VOLUME_CTL, 8, 0, 0xff, 0, dac_tlv),
};

static const struct snd_kcontrol_new pmu3_dapm_mixer_out_l_controls[] = {
SOC_DAPM_SINGLE_TLV("DACL to MIXOUTL Volume", PMU3_MIXOUT_L, 11, 0x1f, 0, xx2mixout_tlv),
SOC_DAPM_SINGLE_TLV("PGAINL to MIXOUTL Volume", PMU3_MIXOUT_L, 6, 0x1f, 0, xx2mixout_tlv),
SOC_DAPM_SINGLE_TLV("PGAINR to MIXOUTL Volume", PMU3_MIXOUT_L, 1, 0x1f, 0, xx2mixout_tlv),
SOC_DAPM_SINGLE_TLV("RXV to MIXOUTL Volume", PMU3_RXV_TO_MIXOUT, 11, 0x1f, 0, xx2mixout_tlv),
SOC_DAPM_SINGLE("DACR Mono Switch", PMU3_SIDETONE_MIXING, 5, 1, 0),
};

static const struct snd_kcontrol_new pmu3_dapm_mixer_out_r_controls[] = {
SOC_DAPM_SINGLE_TLV("DACR to MIXOUTR Volume", PMU3_MIXOUT_R, 11, 0x1f, 0, xx2mixout_tlv),
SOC_DAPM_SINGLE_TLV("PGAINR to MIXOUTR Volume", PMU3_MIXOUT_R, 6, 0x1f, 0, xx2mixout_tlv),
SOC_DAPM_SINGLE_TLV("PGAINL to MIXOUTR Volume", PMU3_MIXOUT_R, 1, 0x1f, 0, xx2mixout_tlv),
SOC_DAPM_SINGLE_TLV("RXV to MIXOUTR Volume", PMU3_RXV_TO_MIXOUT, 6, 0x1f, 0, xx2mixout_tlv),
SOC_DAPM_SINGLE("DACL Mono Switch", PMU3_SIDETONE_MIXING, 5, 1, 0),
};

static const struct snd_kcontrol_new pmu3_dapm_mixer_in_l_controls[] = {
SOC_DAPM_SINGLE_TLV("PGAINL to MIXINL Volume", PMU3_MIXIN_L, 12, 0xf, 0,
			   xx2mixin_tlv),
SOC_DAPM_SINGLE_TLV("RXV to MIXINL Volume", PMU3_MIXIN_L, 8, 0xf, 0,
			   xx2mixin_tlv),
SOC_DAPM_SINGLE_TLV("RECL to MIXINL Volume", PMU3_MIXIN_L, 4, 0xf, 0,
			   xx2mixin_tlv),
};
static const struct snd_kcontrol_new pmu3_dapm_mixer_in_r_controls[] = {
SOC_DAPM_SINGLE_TLV("PGAINR to MIXINR Volume", PMU3_MIXIN_R, 12, 0xf, 0,
			   xx2mixin_tlv),
SOC_DAPM_SINGLE_TLV("RXV to MIXINR Volume", PMU3_MIXIN_R, 8, 0xf, 0,
			   xx2mixin_tlv),
SOC_DAPM_SINGLE_TLV("RECR to MIXINR Volume", PMU3_MIXIN_R, 4, 0xf, 0,
			   xx2mixin_tlv),
};

static const struct snd_kcontrol_new lol1_mux_controls =
	SOC_DAPM_ENUM("Route", pmu3_lineout_enum[0]);

static const struct snd_kcontrol_new lol2_mux_controls =
	SOC_DAPM_ENUM("Route", pmu3_lineout_enum[1]);

static const struct snd_kcontrol_new lor1_mux_controls =
	SOC_DAPM_ENUM("Route", pmu3_lineout_enum[2]);

static const struct snd_kcontrol_new lor2_mux_controls =
	SOC_DAPM_ENUM("Route", pmu3_lineout_enum[3]);

static const struct snd_kcontrol_new pmu3_dapm_hpin_mixer_l_controls[] = {
	SOC_DAPM_SINGLE("DACL Switch", PMU3_LINEOUT_HP_DRV, 7, 1, 0),
	SOC_DAPM_SINGLE("MIXOUTL Switch", PMU3_LINEOUT_HP_DRV, 6, 1, 0),
	SOC_DAPM_SINGLE("MIXOUTR Switch", PMU3_LINEOUT_HP_DRV, 5, 1, 0),
};

static const struct snd_kcontrol_new pmu3_dapm_hpin_mixer_r_controls[] = {
	SOC_DAPM_SINGLE("DACR Switch", PMU3_LINEOUT_HP_DRV, 4, 1, 0),
	SOC_DAPM_SINGLE("MIXOUTR Switch", PMU3_LINEOUT_HP_DRV, 3, 1, 0),
	SOC_DAPM_SINGLE("MIXOUTL Switch", PMU3_LINEOUT_HP_DRV, 2, 1, 0),
};

static const struct snd_kcontrol_new pgain_lp_switch_controls =
	SOC_DAPM_SINGLE("Switch", PMU3_PGA_IN, 10, 1, 0);

static const struct snd_kcontrol_new pgain_rp_switch_controls =
	SOC_DAPM_SINGLE("Switch", PMU3_PGA_IN, 2, 1, 0);

static const char *plin_text[] = { "None", "AINL2", "AINL1" };
static const struct soc_enum pgainl_enum =
	SOC_ENUM_SINGLE(PMU3_PGA_IN, 8, ARRAY_SIZE(plin_text), plin_text);
static const struct snd_kcontrol_new pgain_ln_mux[] = {
	SOC_DAPM_ENUM("route", pgainl_enum),
};
static const char *prin_text[] = { "None", "AINR2", "AINR1" };
static const struct soc_enum pgainr_enum =
	SOC_ENUM_SINGLE(PMU3_PGA_IN, 0, ARRAY_SIZE(prin_text), prin_text);
static const struct snd_kcontrol_new pgain_rn_mux[] = {
	SOC_DAPM_ENUM("route", pgainr_enum),
};


static const struct snd_soc_dapm_widget pmu3_dapm_widgets[] = {
/* Externally visible pins */
SND_SOC_DAPM_OUTPUT("LINEOUTL1"),
SND_SOC_DAPM_OUTPUT("LINEOUTR1"),
SND_SOC_DAPM_OUTPUT("LINEOUTL2"),
SND_SOC_DAPM_OUTPUT("LINEOUTR2"),
SND_SOC_DAPM_OUTPUT("HP_L"),
SND_SOC_DAPM_OUTPUT("HP_R"),
//SND_SOC_DAPM_OUTPUT("AUXOUT"),
//SND_SOC_DAPM_INPUT("RXV"),

SND_SOC_DAPM_INPUT("LINEINLP"),
SND_SOC_DAPM_INPUT("LINEINLN1"),
SND_SOC_DAPM_INPUT("LINEINLN2"),
SND_SOC_DAPM_INPUT("LINEINRP"),
SND_SOC_DAPM_INPUT("LINEINRN1"),
SND_SOC_DAPM_INPUT("LINEINRN2"),

/* Input */
SND_SOC_DAPM_PGA("PGAIN LEFT", PMU3_BLOCK_ENABLE_1, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("PGAIN RIGHT", PMU3_BLOCK_ENABLE_1, 4, 0, NULL, 0),
SND_SOC_DAPM_SWITCH("PGAIN LEFT Pos", SND_SOC_NOPM, 0, 0,
			&pgain_lp_switch_controls),
SND_SOC_DAPM_SWITCH("PGAIN RIGHT Pos", SND_SOC_NOPM, 0, 0,
			&pgain_rp_switch_controls),
SND_SOC_DAPM_MUX("PGAIN LEFT Neg", SND_SOC_NOPM, 0, 0, pgain_ln_mux),
SND_SOC_DAPM_MUX("PGAIN RIGHT Neg", SND_SOC_NOPM, 0, 0, pgain_rn_mux),

SND_SOC_DAPM_MIXER("MIXINL", PMU3_BLOCK_ENABLE_1, 7, 0,
		   &pmu3_dapm_mixer_in_l_controls[0],
		   ARRAY_SIZE(pmu3_dapm_mixer_in_l_controls)),
SND_SOC_DAPM_MIXER("MIXINR", PMU3_BLOCK_ENABLE_1, 6, 0,
		   &pmu3_dapm_mixer_in_r_controls[0],
		   ARRAY_SIZE(pmu3_dapm_mixer_in_r_controls)),

SND_SOC_DAPM_MICBIAS("Mic Bias1", PMU3_BLOCK_ENABLE_2, 15, 0),
SND_SOC_DAPM_MICBIAS("Mic Bias2", PMU3_BLOCK_ENABLE_2, 14, 0),

SND_SOC_DAPM_ADC_E("ADCL", "Capture", PMU3_BLOCK_ENABLE_2, 5, 0, 
			pmu3_adc_event, SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_ADC_E("ADCR", "Capture", PMU3_BLOCK_ENABLE_2, 4, 0,
			pmu3_adc_event, SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_PRE_PMD),

/* Output */
SND_SOC_DAPM_DAC_E("DACL", "Playback", SND_SOC_NOPM, 3, 0,
			pmu3_dac_event, SND_SOC_DAPM_PRE_PMU|SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_DAC_E("DACR", "Playback", SND_SOC_NOPM, 2, 0,
			pmu3_dac_event, SND_SOC_DAPM_PRE_PMU|SND_SOC_DAPM_POST_PMD),

SND_SOC_DAPM_MIXER("MIXOUTL", PMU3_BLOCK_ENABLE_1, 3, 0,
		   &pmu3_dapm_mixer_out_l_controls[0],
		   ARRAY_SIZE(pmu3_dapm_mixer_out_l_controls)),
SND_SOC_DAPM_MIXER("MIXOUTR", PMU3_BLOCK_ENABLE_1, 2, 0,
		   &pmu3_dapm_mixer_out_r_controls[0],
		   ARRAY_SIZE(pmu3_dapm_mixer_out_r_controls)),

SND_SOC_DAPM_MUX("Lineout1 Left in",
			SND_SOC_NOPM, 0, 0, &lol1_mux_controls),
SND_SOC_DAPM_MUX("Lineout2 Left in",
			SND_SOC_NOPM, 0, 0, &lol2_mux_controls),
SND_SOC_DAPM_MUX("Lineout1 Right in",
			SND_SOC_NOPM, 0, 0, &lor1_mux_controls),
SND_SOC_DAPM_MUX("Lineout2 Right in",
			SND_SOC_NOPM, 0, 0, &lor2_mux_controls),
			
SND_SOC_DAPM_MIXER("HPINL Mixer", SND_SOC_NOPM, 0, 0,
		   &pmu3_dapm_hpin_mixer_l_controls[0],
		   ARRAY_SIZE(pmu3_dapm_hpin_mixer_l_controls)),

SND_SOC_DAPM_MIXER("HPINR Mixer", SND_SOC_NOPM, 0, 0,
		   &pmu3_dapm_hpin_mixer_r_controls[0],
		   ARRAY_SIZE(pmu3_dapm_hpin_mixer_r_controls)),

SND_SOC_DAPM_OUT_DRV("LINEOUT1 Left driver", PMU3_BLOCK_ENABLE_1, 11, 0, NULL, 0),
SND_SOC_DAPM_OUT_DRV("LINEOUT2 Left driver", PMU3_BLOCK_ENABLE_1, 10, 0, NULL, 0),
SND_SOC_DAPM_OUT_DRV("LINEOUT1 Right driver", PMU3_BLOCK_ENABLE_1, 9, 0, NULL, 0),
SND_SOC_DAPM_OUT_DRV("LINEOUT2 Right driver", PMU3_BLOCK_ENABLE_1, 8, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("Headphone Amplifier", PMU3_BLOCK_ENABLE_2, 13, 0, NULL, 0,
		   pmu3_hp_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA("RXV", SND_SOC_NOPM, 8, 0, NULL, 0),
};

static const struct snd_soc_dapm_route pmu3_intercon[] = {
/* Inputs */
{"PGAIN LEFT Pos", "Switch", "LINEINLP"},
{"PGAIN LEFT Neg", "AINL1", "LINEINLN1"},
{"PGAIN LEFT Neg", "AINL2", "LINEINLN2"},

{"PGAIN RIGHT Pos", "Switch", "LINEINRP"},
{"PGAIN RIGHT Neg", "AINR1", "LINEINRN1"},
{"PGAIN RIGHT Neg", "AINR2", "LINEINRN2"},

{"PGAIN LEFT", NULL, "PGAIN LEFT Pos"},
{"PGAIN LEFT", NULL, "PGAIN LEFT Neg"},

{"PGAIN RIGHT", NULL, "PGAIN RIGHT Pos"},
{"PGAIN RIGHT", NULL, "PGAIN RIGHT Neg"},

{"RXV", NULL, "LINEINLN2"},
{"RXV", NULL, "LINEINRN2"},

{"MIXINL", "PGAINL to MIXINL Volume", "PGAIN LEFT"},
{"MIXINL", "RXV to MIXINL Volume", "RXV"},
{"MIXINL", "RECL to MIXINL Volume", "MIXOUTL"},

{"MIXINR", "PGAINR to MIXINR Volume", "PGAIN RIGHT"},
{"MIXINR", "RXV to MIXINR Volume", "RXV"},
{"MIXINR", "RECR to MIXINR Volume", "MIXOUTR"},

{"ADCL", NULL, "MIXINL"},
{"ADCR", NULL, "MIXINR"},

/* Outputs */
/* LINEOUTL1 */
{"LINEOUTL1", NULL, "LINEOUT1 Left driver"},
{"LINEOUT1 Left driver", NULL, "Lineout1 Left in"},

{"Lineout1 Left in", "MIXOUTL", "MIXOUTL"},
{"Lineout1 Left in", "MIXOUTR", "MIXOUTR"},
{"Lineout1 Left in", "Inverted MIXOUTR", "MIXOUTR"},

/* LINEOUTR1 */
{"LINEOUTR1", NULL, "LINEOUT1 Right driver"},
{"LINEOUT1 Right driver", NULL, "Lineout1 Right in"},

{"Lineout1 Right in", "MIXOUTR", "MIXOUTL"},
{"Lineout1 Right in", "MIXOUTL", "MIXOUTR"},
{"Lineout1 Right in", "Inverted MIXOUTL", "MIXOUTL"},

/* LINEOUTL2 */
{"LINEOUTL2", NULL, "LINEOUT2 Left driver"},
{"LINEOUT2 Left driver", NULL, "Lineout2 Left in"},

{"Lineout2 Left in", "MIXOUTL", "MIXOUTL"},
{"Lineout2 Left in", "Inverted MIXOUTL", "MIXOUTL"},
{"Lineout2 Left in", "Inverted MIXOUTR", "MIXOUTR"},

/* LINEOUTR2 */
{"LINEOUTR2", NULL, "LINEOUT2 Right driver"},
{"LINEOUT2 Right driver", NULL, "Lineout2 Right in"},

{"Lineout2 Right in", "MIXOUTR", "MIXOUTR"},
{"Lineout2 Right in", "Inverted MIXOUTR", "MIXOUTR"},
{"Lineout2 Right in", "Inverted MIXOUTL", "MIXOUTL"},

/* MIXOUT */
{"MIXOUTL", "DACL to MIXOUTL Volume", "DACL"},
{"MIXOUTL", "PGAINL to MIXOUTL Volume", "PGAIN LEFT"},
{"MIXOUTL", "PGAINR to MIXOUTL Volume", "PGAIN RIGHT"},
{"MIXOUTL", "RXV to MIXOUTL Volume", "RXV"},
{"MIXOUTL", "DACR Mono Switch", "DACR"},

{"MIXOUTR", "DACR to MIXOUTR Volume", "DACR"},
{"MIXOUTR", "PGAINR to MIXOUTR Volume", "PGAIN RIGHT"},
{"MIXOUTR", "PGAINL to MIXOUTR Volume", "PGAIN LEFT"},
{"MIXOUTR", "RXV to MIXOUTR Volume", "RXV"},
{"MIXOUTR", "DACL Mono Switch", "DACL"},

/* Headphone */

{"HPINL Mixer", "DACL Switch", "DACL"},
{"HPINL Mixer", "MIXOUTL Switch", "MIXOUTL"},
{"HPINL Mixer", "MIXOUTR Switch", "MIXOUTR"},

{"HPINR Mixer", "DACR Switch", "DACR"},
{"HPINR Mixer", "MIXOUTR Switch", "MIXOUTR"},
{"HPINR Mixer", "MIXOUTL Switch", "MIXOUTL"},

{"Headphone Amplifier", NULL, "HPINL Mixer"},
{"Headphone Amplifier", NULL, "HPINR Mixer"},
{"HP_L", NULL, "Headphone Amplifier"},
{"HP_R", NULL, "Headphone Amplifier"},
};

static int pmu3_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			/* VMID Gen & Bias Current & Refp Buf */
			snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_1,
						0xf000, 0xf000);
			msleep(200);
			/* Clear Fast Charge */
			snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_1,
						0x4000, 0x0);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, PMU3_BLOCK_ENABLE_1, 0);

		break;
	}

	codec->dapm.bias_level = level;

	return 0;
}

static int pmu3_set_dai_sysclk(struct snd_soc_dai *codec_dai,
					 int clk_id, unsigned int freq, int dir)
{
	//struct snd_soc_codec *codec = codec_dai->codec;
	//struct aml_pmu3_priv *pmu3 = snd_soc_codec_get_drvdata(codec);

	//pmu3->sysclk = freq;

	return 0;
}


static int pmu3_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

	iface = snd_soc_read(codec, PMU3_ADC_DAC);
	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface |= (0x1 << 14);
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	//default:
		//return -EINVAL;
	}

	snd_soc_write(codec, PMU3_ADC_DAC, iface);

	return 0;
}

static int pmu3_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	reg = snd_soc_read(codec, PMU3_SOFT_MUTE);

	if (mute)
		reg |= 0x8000;
	else
		reg &= ~0x8000;

	snd_soc_write(codec, PMU3_SOFT_MUTE, reg);

	return 0;
}

static int pmu3_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
    struct snd_soc_codec *codec = dai->codec;
    u16 reg;
    if(stream == SNDRV_PCM_STREAM_PLAYBACK){
        reg = snd_soc_read(codec, PMU3_SOFT_MUTE);
        if (mute){
           // reg |= 0x8000;
        }else
            reg &= ~0x8000;
        snd_soc_write(codec, PMU3_SOFT_MUTE, reg);
    }
    if(stream == SNDRV_PCM_STREAM_CAPTURE){
        if (mute){
            snd_soc_write(codec, PMU3_ADC_VOLUME_CTL, 0);
        }else{
            msleep(300);
            snd_soc_write(codec, PMU3_ADC_VOLUME_CTL, 0x6a6a);
        }
    }
    return 0;

}


static struct {
	int rate;
	int value;
} sample_rates[] = {
	{  8000,  0 },
	{ 11025,  1 },
	{ 12000,  2 },
	{ 16000,  3 },
	{ 22050,  4 },
	{ 24000,  5 },
	{ 32000,  6 },
	{ 44100,  7 },
	{ 48000,  8 },
};

static int pmu3_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec =rtd->codec;
	//struct aml_pmu3_priv *pmu3 = snd_soc_codec_get_drvdata(codec);
	int fs = params_rate(params);
	u16 reg = 0;
	int i;
	
	for (i = 0;i < ARRAY_SIZE(sample_rates); i++){
		if (sample_rates[i].rate == fs){
			reg = sample_rates[i].value;
		}
	}
	printk("pmu3_hw_params(rate)%x\n", reg);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_update_bits(codec, PMU3_ADC_DAC, 0xf0, reg << 4);
	} else {
		snd_soc_update_bits(codec, PMU3_ADC_DAC, 0xf, reg);
	}
	
	return 0;
}
#if 0
static int pmu3_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	//struct snd_soc_codec *codec = codec_dai->codec;
	//u16 reg;

	//reg = snd_soc_read(codec, PMU3_SAMPLE_RATE_CTRL) & 0xEFFF;
	//snd_soc_write(codec, PMU3_SAMPLE_RATE_CTRL, reg | (div << 12) | (1 << 13));

	return 0;
}
#endif
#define AML_PMU3_PLAYBACK_RATES SNDRV_PCM_RATE_8000_48000
#define AML_PMU3_CAPTURE_RATES SNDRV_PCM_RATE_8000_48000
#define AML_PMU3_FORMATS SNDRV_PCM_FMTBIT_S16_LE|SNDRV_PCM_FMTBIT_S24_LE

static struct snd_soc_dai_ops pmu3_dai_ops = {
	.hw_params	= pmu3_hw_params,
	.digital_mute	= pmu3_digital_mute,
	.mute_stream = pmu3_mute_stream,
	.set_fmt	= pmu3_set_dai_fmt,
	//.set_clkdiv = pmu3_set_dai_clkdiv,
	.set_sysclk	= pmu3_set_dai_sysclk,
};

static struct snd_soc_dai_driver pmu3_dai = {
	.name = "pmu3-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = AML_PMU3_PLAYBACK_RATES,
		.formats = AML_PMU3_FORMATS,
	},
	.capture = {
		 .stream_name = "Capture",
		 .channels_min = 2,
		 .channels_max = 2,
		 .rates = AML_PMU3_CAPTURE_RATES,
		 .formats = AML_PMU3_FORMATS,
	 },
	.ops = &pmu3_dai_ops,
	.symmetric_rates = 1,
};

static int pmu3_suspend(struct snd_soc_codec *codec)
{
	pmu3_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int pmu3_resume(struct snd_soc_codec *codec)
{
	//int i;
	//u16 *reg_cache = codec->reg_cache;
	//u16 *tmp_cache = kmemdup(reg_cache, sizeof(pmu3_reg_defaults),
	//			 GFP_KERNEL);
	snd_soc_cache_sync(codec);

	/* Bring the codec back up to standby first to minimise pop/clicks */
	pmu3_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Sync back everything else */
	/*if (tmp_cache) {
		for (i = 2; i < ARRAY_SIZE(pmu3_reg_defaults); i++)
			if (tmp_cache[i] != reg_cache[i])
				snd_soc_write(codec, i, tmp_cache[i]);
		kfree(tmp_cache);
	} else {
		dev_err(codec->dev, "Failed to allocate temporary cache\n");
	}*/

	return 0;
}
static int pmu3_audio_power_init(struct snd_soc_codec *codec)
{


	uint8_t val = 0;

	// LDO1v8 for audio
	aml1218_read(0x010e, &val);
	val |= 0x1;
	aml1218_write(0x010e, val);
	// LDO2v5 for audio
	aml1218_write(0x0139, 0x1f);
	val = 0;
	aml1218_read(0x010f, &val);
	val |= 0x1;
	aml1218_write(0x010f, val);
	return 0;
}

static int pmu3_write(struct snd_soc_codec *codec, unsigned int reg,
							unsigned int value)
{
	uint32_t addr;

	addr = PMU3_AUDIO_BASE + (reg<<1);
	aml1218_write16(addr, value);

	return 0;
}

static unsigned int pmu3_read(struct snd_soc_codec *codec,
							unsigned int reg)
{
	uint32_t addr;
	uint16_t val;
	
	addr = PMU3_AUDIO_BASE + (reg<<1);
	aml1218_read16(addr, &val);

	return val;
}


static int pmu3_probe(struct snd_soc_codec *codec)
{
	int ret = 0;

	printk("aml pmu3 codec probe\n");
	pmu3_audio_power_init(codec);
	pmu3_reset(codec);
	/* power on device */
	pmu3_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Enable DAC soft mute by default */
	snd_soc_update_bits(codec, PMU3_SOFT_MUTE, 0x7800, 0x7800);

	/* Enable ZC */
	snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_2, 0x3c0, 0x0);
	
	snd_soc_add_codec_controls(codec, pmu3_snd_controls,
				ARRAY_SIZE(pmu3_snd_controls));
	/* ADC high pass filter Hi-fi mode */
	snd_soc_update_bits(codec, PMU3_ADC_DAC, 0xc00, 0xc00);

	snd_soc_write(codec, PMU3_MIXOUT_L, 0xe000);
	snd_soc_write(codec, PMU3_MIXOUT_R, 0xe000);
	snd_soc_write(codec, PMU3_MIXIN_L, 0xf000);
	snd_soc_write(codec, PMU3_MIXIN_R, 0xf000);
	snd_soc_write(codec, PMU3_PGA_IN, 0x1616);


	snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_1, 0xf0c, 0xf0c);
	snd_soc_update_bits(codec, PMU3_BLOCK_ENABLE_2, 0xf, 0xf);

	snd_soc_update_bits(codec, PMU3_LINEOUT_HP_DRV, 0x90, 0x90);
	snd_soc_write(codec, PMU3_DAC_VOLUME_CTL, 0xb9b9);
	snd_soc_write(codec, PMU3_ADC_VOLUME_CTL, 0x6a6a);

	return ret;
}

/* power down chip */
static int pmu3_remove(struct snd_soc_codec *codec)
{
	pmu3_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_pmu3 = {
	.probe =	pmu3_probe,
	.remove =	pmu3_remove,
	.suspend =	pmu3_suspend,
	.resume =	pmu3_resume,
	.read = pmu3_read,
	.write = pmu3_write,
	.set_bias_level = pmu3_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(pmu3_reg_defaults),
	.reg_word_size = sizeof(u16),
	.reg_cache_step = 1,
	.reg_cache_default = pmu3_reg_defaults,
	.volatile_register = pmu3_volatile_register,
	.dapm_widgets = pmu3_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(pmu3_dapm_widgets),
	.dapm_routes = pmu3_intercon,
	.num_dapm_routes = ARRAY_SIZE(pmu3_intercon),
};


static int pmu3_audio_codec_mute(void)
{
    uint32_t addr;
    unsigned int value = 0x8000;
    unsigned int reg = PMU3_SOFT_MUTE;
    printk("pmu3_audio_codec_mute\n");

    addr = PMU3_AUDIO_BASE + (reg<<1);
    aml1218_write16(addr, value);

    return 0;
}



static int aml_pmu3_audio_reboot_work(struct notifier_block *nb, unsigned long state, void *cmd)
{
    
    printk(KERN_DEBUG "\n%s\n", __func__);

    pmu3_audio_codec_mute();
    
    return NOTIFY_DONE;
}


static struct notifier_block aml_pmu3_audio_reboot_nb = {
    .notifier_call    = aml_pmu3_audio_reboot_work,
    .priority = 0,
};

static int aml_pmu3_codec_probe(struct platform_device *pdev)
{
    int ret = snd_soc_register_codec(&pdev->dev, 
        &soc_codec_dev_pmu3, &pmu3_dai, 1);
    register_reboot_notifier(&aml_pmu3_audio_reboot_nb);

    return ret;
}



static int aml_pmu3_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_pmu3_codec_dt_match[]={
    { .compatible = "amlogic,aml_pmu3_codec", },
    {},
};
#else
#define amlogic_audio_dt_match NULL
#endif
static struct platform_driver aml_pmu3_codec_platform_driver = {
	.driver = {
		.name = "aml_pmu3_codec",
		.owner = THIS_MODULE,
        .of_match_table = amlogic_pmu3_codec_dt_match,
	},
	.probe = aml_pmu3_codec_probe,
	.remove = aml_pmu3_codec_remove,
	//.shutdown = aml_pmu3_codec_shutdown,
};

static int __init aml_pmu3_modinit(void)
{
	int ret = 0;

	ret = platform_driver_register(&aml_pmu3_codec_platform_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register AML PMU3 codec platform driver: %d\n",
		       ret);
	}

	return ret;
}
module_init(aml_pmu3_modinit);

static void __exit aml_pmu3_exit(void)
{
	platform_driver_unregister(&aml_pmu3_codec_platform_driver);
}
module_exit(aml_pmu3_exit);

MODULE_DESCRIPTION("ASoC AML_PUM3 driver");
MODULE_AUTHOR("Shuai Li <Shuai.li@amlogic.com>");
MODULE_LICENSE("GPL");

