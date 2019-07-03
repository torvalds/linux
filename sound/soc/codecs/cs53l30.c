// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs53l30.c  --  CS53l30 ALSA Soc Audio driver
 *
 * Copyright 2015 Cirrus Logic, Inc.
 *
 * Authors: Paul Handrigan <Paul.Handrigan@cirrus.com>,
 *          Tim Howe <Tim.Howe@cirrus.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "cs53l30.h"

#define CS53L30_NUM_SUPPLIES 2
static const char *const cs53l30_supply_names[CS53L30_NUM_SUPPLIES] = {
	"VA",
	"VP",
};

struct cs53l30_private {
	struct regulator_bulk_data	supplies[CS53L30_NUM_SUPPLIES];
	struct regmap			*regmap;
	struct gpio_desc		*reset_gpio;
	struct gpio_desc		*mute_gpio;
	struct clk			*mclk;
	bool				use_sdout2;
	u32				mclk_rate;
};

static const struct reg_default cs53l30_reg_defaults[] = {
	{ CS53L30_PWRCTL,		CS53L30_PWRCTL_DEFAULT },
	{ CS53L30_MCLKCTL,		CS53L30_MCLKCTL_DEFAULT },
	{ CS53L30_INT_SR_CTL,		CS53L30_INT_SR_CTL_DEFAULT },
	{ CS53L30_MICBIAS_CTL,		CS53L30_MICBIAS_CTL_DEFAULT },
	{ CS53L30_ASPCFG_CTL,		CS53L30_ASPCFG_CTL_DEFAULT },
	{ CS53L30_ASP_CTL1,		CS53L30_ASP_CTL1_DEFAULT },
	{ CS53L30_ASP_TDMTX_CTL1,	CS53L30_ASP_TDMTX_CTLx_DEFAULT },
	{ CS53L30_ASP_TDMTX_CTL2,	CS53L30_ASP_TDMTX_CTLx_DEFAULT },
	{ CS53L30_ASP_TDMTX_CTL3,	CS53L30_ASP_TDMTX_CTLx_DEFAULT },
	{ CS53L30_ASP_TDMTX_CTL4,	CS53L30_ASP_TDMTX_CTLx_DEFAULT },
	{ CS53L30_ASP_TDMTX_EN1,	CS53L30_ASP_TDMTX_ENx_DEFAULT },
	{ CS53L30_ASP_TDMTX_EN2,	CS53L30_ASP_TDMTX_ENx_DEFAULT },
	{ CS53L30_ASP_TDMTX_EN3,	CS53L30_ASP_TDMTX_ENx_DEFAULT },
	{ CS53L30_ASP_TDMTX_EN4,	CS53L30_ASP_TDMTX_ENx_DEFAULT },
	{ CS53L30_ASP_TDMTX_EN5,	CS53L30_ASP_TDMTX_ENx_DEFAULT },
	{ CS53L30_ASP_TDMTX_EN6,	CS53L30_ASP_TDMTX_ENx_DEFAULT },
	{ CS53L30_ASP_CTL2,		CS53L30_ASP_CTL2_DEFAULT },
	{ CS53L30_SFT_RAMP,		CS53L30_SFT_RMP_DEFAULT },
	{ CS53L30_LRCK_CTL1,		CS53L30_LRCK_CTLx_DEFAULT },
	{ CS53L30_LRCK_CTL2,		CS53L30_LRCK_CTLx_DEFAULT },
	{ CS53L30_MUTEP_CTL1,		CS53L30_MUTEP_CTL1_DEFAULT },
	{ CS53L30_MUTEP_CTL2,		CS53L30_MUTEP_CTL2_DEFAULT },
	{ CS53L30_INBIAS_CTL1,		CS53L30_INBIAS_CTL1_DEFAULT },
	{ CS53L30_INBIAS_CTL2,		CS53L30_INBIAS_CTL2_DEFAULT },
	{ CS53L30_DMIC1_STR_CTL,	CS53L30_DMIC1_STR_CTL_DEFAULT },
	{ CS53L30_DMIC2_STR_CTL,	CS53L30_DMIC2_STR_CTL_DEFAULT },
	{ CS53L30_ADCDMIC1_CTL1,	CS53L30_ADCDMICx_CTL1_DEFAULT },
	{ CS53L30_ADCDMIC1_CTL2,	CS53L30_ADCDMIC1_CTL2_DEFAULT },
	{ CS53L30_ADC1_CTL3,		CS53L30_ADCx_CTL3_DEFAULT },
	{ CS53L30_ADC1_NG_CTL,		CS53L30_ADCx_NG_CTL_DEFAULT },
	{ CS53L30_ADC1A_AFE_CTL,	CS53L30_ADCxy_AFE_CTL_DEFAULT },
	{ CS53L30_ADC1B_AFE_CTL,	CS53L30_ADCxy_AFE_CTL_DEFAULT },
	{ CS53L30_ADC1A_DIG_VOL,	CS53L30_ADCxy_DIG_VOL_DEFAULT },
	{ CS53L30_ADC1B_DIG_VOL,	CS53L30_ADCxy_DIG_VOL_DEFAULT },
	{ CS53L30_ADCDMIC2_CTL1,	CS53L30_ADCDMICx_CTL1_DEFAULT },
	{ CS53L30_ADCDMIC2_CTL2,	CS53L30_ADCDMIC1_CTL2_DEFAULT },
	{ CS53L30_ADC2_CTL3,		CS53L30_ADCx_CTL3_DEFAULT },
	{ CS53L30_ADC2_NG_CTL,		CS53L30_ADCx_NG_CTL_DEFAULT },
	{ CS53L30_ADC2A_AFE_CTL,	CS53L30_ADCxy_AFE_CTL_DEFAULT },
	{ CS53L30_ADC2B_AFE_CTL,	CS53L30_ADCxy_AFE_CTL_DEFAULT },
	{ CS53L30_ADC2A_DIG_VOL,	CS53L30_ADCxy_DIG_VOL_DEFAULT },
	{ CS53L30_ADC2B_DIG_VOL,	CS53L30_ADCxy_DIG_VOL_DEFAULT },
	{ CS53L30_INT_MASK,		CS53L30_DEVICE_INT_MASK },
};

static bool cs53l30_volatile_register(struct device *dev, unsigned int reg)
{
	if (reg == CS53L30_IS)
		return true;
	else
		return false;
}

static bool cs53l30_writeable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS53L30_DEVID_AB:
	case CS53L30_DEVID_CD:
	case CS53L30_DEVID_E:
	case CS53L30_REVID:
	case CS53L30_IS:
		return false;
	default:
		return true;
	}
}

static bool cs53l30_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS53L30_DEVID_AB:
	case CS53L30_DEVID_CD:
	case CS53L30_DEVID_E:
	case CS53L30_REVID:
	case CS53L30_PWRCTL:
	case CS53L30_MCLKCTL:
	case CS53L30_INT_SR_CTL:
	case CS53L30_MICBIAS_CTL:
	case CS53L30_ASPCFG_CTL:
	case CS53L30_ASP_CTL1:
	case CS53L30_ASP_TDMTX_CTL1:
	case CS53L30_ASP_TDMTX_CTL2:
	case CS53L30_ASP_TDMTX_CTL3:
	case CS53L30_ASP_TDMTX_CTL4:
	case CS53L30_ASP_TDMTX_EN1:
	case CS53L30_ASP_TDMTX_EN2:
	case CS53L30_ASP_TDMTX_EN3:
	case CS53L30_ASP_TDMTX_EN4:
	case CS53L30_ASP_TDMTX_EN5:
	case CS53L30_ASP_TDMTX_EN6:
	case CS53L30_ASP_CTL2:
	case CS53L30_SFT_RAMP:
	case CS53L30_LRCK_CTL1:
	case CS53L30_LRCK_CTL2:
	case CS53L30_MUTEP_CTL1:
	case CS53L30_MUTEP_CTL2:
	case CS53L30_INBIAS_CTL1:
	case CS53L30_INBIAS_CTL2:
	case CS53L30_DMIC1_STR_CTL:
	case CS53L30_DMIC2_STR_CTL:
	case CS53L30_ADCDMIC1_CTL1:
	case CS53L30_ADCDMIC1_CTL2:
	case CS53L30_ADC1_CTL3:
	case CS53L30_ADC1_NG_CTL:
	case CS53L30_ADC1A_AFE_CTL:
	case CS53L30_ADC1B_AFE_CTL:
	case CS53L30_ADC1A_DIG_VOL:
	case CS53L30_ADC1B_DIG_VOL:
	case CS53L30_ADCDMIC2_CTL1:
	case CS53L30_ADCDMIC2_CTL2:
	case CS53L30_ADC2_CTL3:
	case CS53L30_ADC2_NG_CTL:
	case CS53L30_ADC2A_AFE_CTL:
	case CS53L30_ADC2B_AFE_CTL:
	case CS53L30_ADC2A_DIG_VOL:
	case CS53L30_ADC2B_DIG_VOL:
	case CS53L30_INT_MASK:
		return true;
	default:
		return false;
	}
}

static DECLARE_TLV_DB_SCALE(adc_boost_tlv, 0, 2000, 0);
static DECLARE_TLV_DB_SCALE(adc_ng_boost_tlv, 0, 3000, 0);
static DECLARE_TLV_DB_SCALE(pga_tlv, -600, 50, 0);
static DECLARE_TLV_DB_SCALE(dig_tlv, -9600, 100, 1);
static DECLARE_TLV_DB_SCALE(pga_preamp_tlv, 0, 10000, 0);

static const char * const input1_sel_text[] = {
	"DMIC1 On AB In",
	"DMIC1 On A In",
	"DMIC1 On B In",
	"ADC1 On AB In",
	"ADC1 On A In",
	"ADC1 On B In",
	"DMIC1 Off ADC1 Off",
};

static unsigned int const input1_sel_values[] = {
	CS53L30_CH_TYPE,
	CS53L30_ADCxB_PDN | CS53L30_CH_TYPE,
	CS53L30_ADCxA_PDN | CS53L30_CH_TYPE,
	CS53L30_DMICx_PDN,
	CS53L30_ADCxB_PDN | CS53L30_DMICx_PDN,
	CS53L30_ADCxA_PDN | CS53L30_DMICx_PDN,
	CS53L30_ADCxA_PDN | CS53L30_ADCxB_PDN | CS53L30_DMICx_PDN,
};

static const char * const input2_sel_text[] = {
	"DMIC2 On AB In",
	"DMIC2 On A In",
	"DMIC2 On B In",
	"ADC2 On AB In",
	"ADC2 On A In",
	"ADC2 On B In",
	"DMIC2 Off ADC2 Off",
};

static unsigned int const input2_sel_values[] = {
	0x0,
	CS53L30_ADCxB_PDN,
	CS53L30_ADCxA_PDN,
	CS53L30_DMICx_PDN,
	CS53L30_ADCxB_PDN | CS53L30_DMICx_PDN,
	CS53L30_ADCxA_PDN | CS53L30_DMICx_PDN,
	CS53L30_ADCxA_PDN | CS53L30_ADCxB_PDN | CS53L30_DMICx_PDN,
};

static const char * const input1_route_sel_text[] = {
	"ADC1_SEL", "DMIC1_SEL",
};

static const struct soc_enum input1_route_sel_enum =
	SOC_ENUM_SINGLE(CS53L30_ADCDMIC1_CTL1, CS53L30_CH_TYPE_SHIFT,
			ARRAY_SIZE(input1_route_sel_text),
			input1_route_sel_text);

static SOC_VALUE_ENUM_SINGLE_DECL(input1_sel_enum, CS53L30_ADCDMIC1_CTL1, 0,
				  CS53L30_ADCDMICx_PDN_MASK, input1_sel_text,
				  input1_sel_values);

static const struct snd_kcontrol_new input1_route_sel_mux =
	SOC_DAPM_ENUM("Input 1 Route", input1_route_sel_enum);

static const char * const input2_route_sel_text[] = {
	"ADC2_SEL", "DMIC2_SEL",
};

/* Note: CS53L30_ADCDMIC1_CTL1 CH_TYPE controls inputs 1 and 2 */
static const struct soc_enum input2_route_sel_enum =
	SOC_ENUM_SINGLE(CS53L30_ADCDMIC1_CTL1, 0,
			ARRAY_SIZE(input2_route_sel_text),
			input2_route_sel_text);

static SOC_VALUE_ENUM_SINGLE_DECL(input2_sel_enum, CS53L30_ADCDMIC2_CTL1, 0,
				  CS53L30_ADCDMICx_PDN_MASK, input2_sel_text,
				  input2_sel_values);

static const struct snd_kcontrol_new input2_route_sel_mux =
	SOC_DAPM_ENUM("Input 2 Route", input2_route_sel_enum);

/*
 * TB = 6144*(MCLK(int) scaling factor)/MCLK(internal)
 * TB - Time base
 * NOTE: If MCLK_INT_SCALE = 0, then TB=1
 */
static const char * const cs53l30_ng_delay_text[] = {
	"TB*50ms", "TB*100ms", "TB*150ms", "TB*200ms",
};

static const struct soc_enum adc1_ng_delay_enum =
	SOC_ENUM_SINGLE(CS53L30_ADC1_NG_CTL, CS53L30_ADCx_NG_DELAY_SHIFT,
			ARRAY_SIZE(cs53l30_ng_delay_text),
			cs53l30_ng_delay_text);

static const struct soc_enum adc2_ng_delay_enum =
	SOC_ENUM_SINGLE(CS53L30_ADC2_NG_CTL, CS53L30_ADCx_NG_DELAY_SHIFT,
			ARRAY_SIZE(cs53l30_ng_delay_text),
			cs53l30_ng_delay_text);

/* The noise gate threshold selected will depend on NG Boost */
static const char * const cs53l30_ng_thres_text[] = {
	"-64dB/-34dB", "-66dB/-36dB", "-70dB/-40dB", "-73dB/-43dB",
	"-76dB/-46dB", "-82dB/-52dB", "-58dB", "-64dB",
};

static const struct soc_enum adc1_ng_thres_enum =
	SOC_ENUM_SINGLE(CS53L30_ADC1_NG_CTL, CS53L30_ADCx_NG_THRESH_SHIFT,
			ARRAY_SIZE(cs53l30_ng_thres_text),
			cs53l30_ng_thres_text);

static const struct soc_enum adc2_ng_thres_enum =
	SOC_ENUM_SINGLE(CS53L30_ADC2_NG_CTL, CS53L30_ADCx_NG_THRESH_SHIFT,
			ARRAY_SIZE(cs53l30_ng_thres_text),
			cs53l30_ng_thres_text);

/* Corner frequencies are with an Fs of 48kHz. */
static const char * const hpf_corner_freq_text[] = {
	"1.86Hz", "120Hz", "235Hz", "466Hz",
};

static const struct soc_enum adc1_hpf_enum =
	SOC_ENUM_SINGLE(CS53L30_ADC1_CTL3, CS53L30_ADCx_HPF_CF_SHIFT,
			ARRAY_SIZE(hpf_corner_freq_text), hpf_corner_freq_text);

static const struct soc_enum adc2_hpf_enum =
	SOC_ENUM_SINGLE(CS53L30_ADC2_CTL3, CS53L30_ADCx_HPF_CF_SHIFT,
			ARRAY_SIZE(hpf_corner_freq_text), hpf_corner_freq_text);

static const struct snd_kcontrol_new cs53l30_snd_controls[] = {
	SOC_SINGLE("Digital Soft-Ramp Switch", CS53L30_SFT_RAMP,
		   CS53L30_DIGSFT_SHIFT, 1, 0),
	SOC_SINGLE("ADC1 Noise Gate Ganging Switch", CS53L30_ADC1_CTL3,
		   CS53L30_ADCx_NG_ALL_SHIFT, 1, 0),
	SOC_SINGLE("ADC2 Noise Gate Ganging Switch", CS53L30_ADC2_CTL3,
		   CS53L30_ADCx_NG_ALL_SHIFT, 1, 0),
	SOC_SINGLE("ADC1A Noise Gate Enable Switch", CS53L30_ADC1_NG_CTL,
		   CS53L30_ADCxA_NG_SHIFT, 1, 0),
	SOC_SINGLE("ADC1B Noise Gate Enable Switch", CS53L30_ADC1_NG_CTL,
		   CS53L30_ADCxB_NG_SHIFT, 1, 0),
	SOC_SINGLE("ADC2A Noise Gate Enable Switch", CS53L30_ADC2_NG_CTL,
		   CS53L30_ADCxA_NG_SHIFT, 1, 0),
	SOC_SINGLE("ADC2B Noise Gate Enable Switch", CS53L30_ADC2_NG_CTL,
		   CS53L30_ADCxB_NG_SHIFT, 1, 0),
	SOC_SINGLE("ADC1 Notch Filter Switch", CS53L30_ADCDMIC1_CTL2,
		   CS53L30_ADCx_NOTCH_DIS_SHIFT, 1, 1),
	SOC_SINGLE("ADC2 Notch Filter Switch", CS53L30_ADCDMIC2_CTL2,
		   CS53L30_ADCx_NOTCH_DIS_SHIFT, 1, 1),
	SOC_SINGLE("ADC1A Invert Switch", CS53L30_ADCDMIC1_CTL2,
		   CS53L30_ADCxA_INV_SHIFT, 1, 0),
	SOC_SINGLE("ADC1B Invert Switch", CS53L30_ADCDMIC1_CTL2,
		   CS53L30_ADCxB_INV_SHIFT, 1, 0),
	SOC_SINGLE("ADC2A Invert Switch", CS53L30_ADCDMIC2_CTL2,
		   CS53L30_ADCxA_INV_SHIFT, 1, 0),
	SOC_SINGLE("ADC2B Invert Switch", CS53L30_ADCDMIC2_CTL2,
		   CS53L30_ADCxB_INV_SHIFT, 1, 0),

	SOC_SINGLE_TLV("ADC1A Digital Boost Volume", CS53L30_ADCDMIC1_CTL2,
		       CS53L30_ADCxA_DIG_BOOST_SHIFT, 1, 0, adc_boost_tlv),
	SOC_SINGLE_TLV("ADC1B Digital Boost Volume", CS53L30_ADCDMIC1_CTL2,
		       CS53L30_ADCxB_DIG_BOOST_SHIFT, 1, 0, adc_boost_tlv),
	SOC_SINGLE_TLV("ADC2A Digital Boost Volume", CS53L30_ADCDMIC2_CTL2,
		       CS53L30_ADCxA_DIG_BOOST_SHIFT, 1, 0, adc_boost_tlv),
	SOC_SINGLE_TLV("ADC2B Digital Boost Volume", CS53L30_ADCDMIC2_CTL2,
		       CS53L30_ADCxB_DIG_BOOST_SHIFT, 1, 0, adc_boost_tlv),
	SOC_SINGLE_TLV("ADC1 NG Boost Volume", CS53L30_ADC1_NG_CTL,
		       CS53L30_ADCx_NG_BOOST_SHIFT, 1, 0, adc_ng_boost_tlv),
	SOC_SINGLE_TLV("ADC2 NG Boost Volume", CS53L30_ADC2_NG_CTL,
		       CS53L30_ADCx_NG_BOOST_SHIFT, 1, 0, adc_ng_boost_tlv),

	SOC_DOUBLE_R_TLV("ADC1 Preamplifier Volume", CS53L30_ADC1A_AFE_CTL,
			 CS53L30_ADC1B_AFE_CTL, CS53L30_ADCxy_PREAMP_SHIFT,
			 2, 0, pga_preamp_tlv),
	SOC_DOUBLE_R_TLV("ADC2 Preamplifier Volume", CS53L30_ADC2A_AFE_CTL,
			 CS53L30_ADC2B_AFE_CTL, CS53L30_ADCxy_PREAMP_SHIFT,
			 2, 0, pga_preamp_tlv),

	SOC_ENUM("Input 1 Channel Select", input1_sel_enum),
	SOC_ENUM("Input 2 Channel Select", input2_sel_enum),

	SOC_ENUM("ADC1 HPF Select", adc1_hpf_enum),
	SOC_ENUM("ADC2 HPF Select", adc2_hpf_enum),
	SOC_ENUM("ADC1 NG Threshold", adc1_ng_thres_enum),
	SOC_ENUM("ADC2 NG Threshold", adc2_ng_thres_enum),
	SOC_ENUM("ADC1 NG Delay", adc1_ng_delay_enum),
	SOC_ENUM("ADC2 NG Delay", adc2_ng_delay_enum),

	SOC_SINGLE_SX_TLV("ADC1A PGA Volume",
		    CS53L30_ADC1A_AFE_CTL, 0, 0x34, 0x18, pga_tlv),
	SOC_SINGLE_SX_TLV("ADC1B PGA Volume",
		    CS53L30_ADC1B_AFE_CTL, 0, 0x34, 0x18, pga_tlv),
	SOC_SINGLE_SX_TLV("ADC2A PGA Volume",
		    CS53L30_ADC2A_AFE_CTL, 0, 0x34, 0x18, pga_tlv),
	SOC_SINGLE_SX_TLV("ADC2B PGA Volume",
		    CS53L30_ADC2B_AFE_CTL, 0, 0x34, 0x18, pga_tlv),

	SOC_SINGLE_SX_TLV("ADC1A Digital Volume",
		    CS53L30_ADC1A_DIG_VOL, 0, 0xA0, 0x0C, dig_tlv),
	SOC_SINGLE_SX_TLV("ADC1B Digital Volume",
		    CS53L30_ADC1B_DIG_VOL, 0, 0xA0, 0x0C, dig_tlv),
	SOC_SINGLE_SX_TLV("ADC2A Digital Volume",
		    CS53L30_ADC2A_DIG_VOL, 0, 0xA0, 0x0C, dig_tlv),
	SOC_SINGLE_SX_TLV("ADC2B Digital Volume",
		    CS53L30_ADC2B_DIG_VOL, 0, 0xA0, 0x0C, dig_tlv),
};

static const struct snd_soc_dapm_widget cs53l30_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("IN1_DMIC1"),
	SND_SOC_DAPM_INPUT("IN2"),
	SND_SOC_DAPM_INPUT("IN3_DMIC2"),
	SND_SOC_DAPM_INPUT("IN4"),
	SND_SOC_DAPM_SUPPLY("MIC1 Bias", CS53L30_MICBIAS_CTL,
			    CS53L30_MIC1_BIAS_PDN_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIC2 Bias", CS53L30_MICBIAS_CTL,
			    CS53L30_MIC2_BIAS_PDN_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIC3 Bias", CS53L30_MICBIAS_CTL,
			    CS53L30_MIC3_BIAS_PDN_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIC4 Bias", CS53L30_MICBIAS_CTL,
			    CS53L30_MIC4_BIAS_PDN_SHIFT, 1, NULL, 0),

	SND_SOC_DAPM_AIF_OUT("ASP_SDOUT1", NULL, 0, CS53L30_ASP_CTL1,
			     CS53L30_ASP_SDOUTx_PDN_SHIFT, 1),
	SND_SOC_DAPM_AIF_OUT("ASP_SDOUT2", NULL, 0, CS53L30_ASP_CTL2,
			     CS53L30_ASP_SDOUTx_PDN_SHIFT, 1),

	SND_SOC_DAPM_MUX("Input Mux 1", SND_SOC_NOPM, 0, 0,
			 &input1_route_sel_mux),
	SND_SOC_DAPM_MUX("Input Mux 2", SND_SOC_NOPM, 0, 0,
			 &input2_route_sel_mux),

	SND_SOC_DAPM_ADC("ADC1A", NULL, CS53L30_ADCDMIC1_CTL1,
			 CS53L30_ADCxA_PDN_SHIFT, 1),
	SND_SOC_DAPM_ADC("ADC1B", NULL, CS53L30_ADCDMIC1_CTL1,
			 CS53L30_ADCxB_PDN_SHIFT, 1),
	SND_SOC_DAPM_ADC("ADC2A", NULL, CS53L30_ADCDMIC2_CTL1,
			 CS53L30_ADCxA_PDN_SHIFT, 1),
	SND_SOC_DAPM_ADC("ADC2B", NULL, CS53L30_ADCDMIC2_CTL1,
			 CS53L30_ADCxB_PDN_SHIFT, 1),
	SND_SOC_DAPM_ADC("DMIC1", NULL, CS53L30_ADCDMIC1_CTL1,
			 CS53L30_DMICx_PDN_SHIFT, 1),
	SND_SOC_DAPM_ADC("DMIC2", NULL, CS53L30_ADCDMIC2_CTL1,
			 CS53L30_DMICx_PDN_SHIFT, 1),
};

static const struct snd_soc_dapm_route cs53l30_dapm_routes[] = {
	/* ADC Input Paths */
	{"ADC1A", NULL, "IN1_DMIC1"},
	{"Input Mux 1", "ADC1_SEL", "ADC1A"},
	{"ADC1B", NULL, "IN2"},

	{"ADC2A", NULL, "IN3_DMIC2"},
	{"Input Mux 2", "ADC2_SEL", "ADC2A"},
	{"ADC2B", NULL, "IN4"},

	/* MIC Bias Paths */
	{"ADC1A", NULL, "MIC1 Bias"},
	{"ADC1B", NULL, "MIC2 Bias"},
	{"ADC2A", NULL, "MIC3 Bias"},
	{"ADC2B", NULL, "MIC4 Bias"},

	/* DMIC Paths */
	{"DMIC1", NULL, "IN1_DMIC1"},
	{"Input Mux 1", "DMIC1_SEL", "DMIC1"},

	{"DMIC2", NULL, "IN3_DMIC2"},
	{"Input Mux 2", "DMIC2_SEL", "DMIC2"},
};

static const struct snd_soc_dapm_route cs53l30_dapm_routes_sdout1[] = {
	/* Output Paths when using SDOUT1 only */
	{"ASP_SDOUT1", NULL, "ADC1A" },
	{"ASP_SDOUT1", NULL, "Input Mux 1"},
	{"ASP_SDOUT1", NULL, "ADC1B"},

	{"ASP_SDOUT1", NULL, "ADC2A"},
	{"ASP_SDOUT1", NULL, "Input Mux 2"},
	{"ASP_SDOUT1", NULL, "ADC2B"},

	{"Capture", NULL, "ASP_SDOUT1"},
};

static const struct snd_soc_dapm_route cs53l30_dapm_routes_sdout2[] = {
	/* Output Paths when using both SDOUT1 and SDOUT2 */
	{"ASP_SDOUT1", NULL, "ADC1A" },
	{"ASP_SDOUT1", NULL, "Input Mux 1"},
	{"ASP_SDOUT1", NULL, "ADC1B"},

	{"ASP_SDOUT2", NULL, "ADC2A"},
	{"ASP_SDOUT2", NULL, "Input Mux 2"},
	{"ASP_SDOUT2", NULL, "ADC2B"},

	{"Capture", NULL, "ASP_SDOUT1"},
	{"Capture", NULL, "ASP_SDOUT2"},
};

struct cs53l30_mclk_div {
	u32 mclk_rate;
	u32 srate;
	u8 asp_rate;
	u8 internal_fs_ratio;
	u8 mclk_int_scale;
};

static const struct cs53l30_mclk_div cs53l30_mclk_coeffs[] = {
	/* NOTE: Enable MCLK_INT_SCALE to save power. */

	/* MCLK, Sample Rate, asp_rate, internal_fs_ratio, mclk_int_scale */
	{5644800, 11025, 0x4, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{5644800, 22050, 0x8, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{5644800, 44100, 0xC, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},

	{6000000,  8000, 0x1, 0, CS53L30_MCLK_INT_SCALE},
	{6000000, 11025, 0x2, 0, CS53L30_MCLK_INT_SCALE},
	{6000000, 12000, 0x4, 0, CS53L30_MCLK_INT_SCALE},
	{6000000, 16000, 0x5, 0, CS53L30_MCLK_INT_SCALE},
	{6000000, 22050, 0x6, 0, CS53L30_MCLK_INT_SCALE},
	{6000000, 24000, 0x8, 0, CS53L30_MCLK_INT_SCALE},
	{6000000, 32000, 0x9, 0, CS53L30_MCLK_INT_SCALE},
	{6000000, 44100, 0xA, 0, CS53L30_MCLK_INT_SCALE},
	{6000000, 48000, 0xC, 0, CS53L30_MCLK_INT_SCALE},

	{6144000,  8000, 0x1, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6144000, 11025, 0x2, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6144000, 12000, 0x4, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6144000, 16000, 0x5, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6144000, 22050, 0x6, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6144000, 24000, 0x8, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6144000, 32000, 0x9, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6144000, 44100, 0xA, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6144000, 48000, 0xC, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},

	{6400000,  8000, 0x1, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6400000, 11025, 0x2, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6400000, 12000, 0x4, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6400000, 16000, 0x5, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6400000, 22050, 0x6, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6400000, 24000, 0x8, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6400000, 32000, 0x9, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6400000, 44100, 0xA, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
	{6400000, 48000, 0xC, CS53L30_INTRNL_FS_RATIO, CS53L30_MCLK_INT_SCALE},
};

struct cs53l30_mclkx_div {
	u32 mclkx;
	u8 ratio;
	u8 mclkdiv;
};

static const struct cs53l30_mclkx_div cs53l30_mclkx_coeffs[] = {
	{5644800,  1, CS53L30_MCLK_DIV_BY_1},
	{6000000,  1, CS53L30_MCLK_DIV_BY_1},
	{6144000,  1, CS53L30_MCLK_DIV_BY_1},
	{11289600, 2, CS53L30_MCLK_DIV_BY_2},
	{12288000, 2, CS53L30_MCLK_DIV_BY_2},
	{12000000, 2, CS53L30_MCLK_DIV_BY_2},
	{19200000, 3, CS53L30_MCLK_DIV_BY_3},
};

static int cs53l30_get_mclkx_coeff(int mclkx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs53l30_mclkx_coeffs); i++) {
		if (cs53l30_mclkx_coeffs[i].mclkx == mclkx)
			return i;
	}

	return -EINVAL;
}

static int cs53l30_get_mclk_coeff(int mclk_rate, int srate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs53l30_mclk_coeffs); i++) {
		if (cs53l30_mclk_coeffs[i].mclk_rate == mclk_rate &&
		    cs53l30_mclk_coeffs[i].srate == srate)
			return i;
	}

	return -EINVAL;
}

static int cs53l30_set_sysclk(struct snd_soc_dai *dai,
			      int clk_id, unsigned int freq, int dir)
{
	struct cs53l30_private *priv = snd_soc_component_get_drvdata(dai->component);
	int mclkx_coeff;
	u32 mclk_rate;

	/* MCLKX -> MCLK */
	mclkx_coeff = cs53l30_get_mclkx_coeff(freq);
	if (mclkx_coeff < 0)
		return mclkx_coeff;

	mclk_rate = cs53l30_mclkx_coeffs[mclkx_coeff].mclkx /
		    cs53l30_mclkx_coeffs[mclkx_coeff].ratio;

	regmap_update_bits(priv->regmap, CS53L30_MCLKCTL,
			   CS53L30_MCLK_DIV_MASK,
			   cs53l30_mclkx_coeffs[mclkx_coeff].mclkdiv);

	priv->mclk_rate = mclk_rate;

	return 0;
}

static int cs53l30_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct cs53l30_private *priv = snd_soc_component_get_drvdata(dai->component);
	u8 aspcfg = 0, aspctl1 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		aspcfg |= CS53L30_ASP_MS;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* DAI mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* Set TDM_PDN to turn off TDM mode -- Reset default */
		aspctl1 |= CS53L30_ASP_TDM_PDN;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/*
		 * Clear TDM_PDN to turn on TDM mode; Use ASP_SCLK_INV = 0
		 * with SHIFT_LEFT = 1 combination as Figure 4-13 shows in
		 * the CS53L30 datasheet
		 */
		aspctl1 |= CS53L30_SHIFT_LEFT;
		break;
	default:
		return -EINVAL;
	}

	/* Check to see if the SCLK is inverted */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_NF:
	case SND_SOC_DAIFMT_IB_IF:
		aspcfg ^= CS53L30_ASP_SCLK_INV;
		break;
	default:
		break;
	}

	regmap_update_bits(priv->regmap, CS53L30_ASPCFG_CTL,
			   CS53L30_ASP_MS | CS53L30_ASP_SCLK_INV, aspcfg);

	regmap_update_bits(priv->regmap, CS53L30_ASP_CTL1,
			   CS53L30_ASP_TDM_PDN | CS53L30_SHIFT_LEFT, aspctl1);

	return 0;
}

static int cs53l30_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cs53l30_private *priv = snd_soc_component_get_drvdata(dai->component);
	int srate = params_rate(params);
	int mclk_coeff;

	/* MCLK -> srate */
	mclk_coeff = cs53l30_get_mclk_coeff(priv->mclk_rate, srate);
	if (mclk_coeff < 0)
		return -EINVAL;

	regmap_update_bits(priv->regmap, CS53L30_INT_SR_CTL,
			   CS53L30_INTRNL_FS_RATIO_MASK,
			   cs53l30_mclk_coeffs[mclk_coeff].internal_fs_ratio);

	regmap_update_bits(priv->regmap, CS53L30_MCLKCTL,
			   CS53L30_MCLK_INT_SCALE_MASK,
			   cs53l30_mclk_coeffs[mclk_coeff].mclk_int_scale);

	regmap_update_bits(priv->regmap, CS53L30_ASPCFG_CTL,
			   CS53L30_ASP_RATE_MASK,
			   cs53l30_mclk_coeffs[mclk_coeff].asp_rate);

	return 0;
}

static int cs53l30_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct cs53l30_private *priv = snd_soc_component_get_drvdata(component);
	unsigned int reg;
	int i, inter_max_check, ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level == SND_SOC_BIAS_STANDBY)
			regmap_update_bits(priv->regmap, CS53L30_PWRCTL,
					   CS53L30_PDN_LP_MASK, 0);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (dapm->bias_level == SND_SOC_BIAS_OFF) {
			ret = clk_prepare_enable(priv->mclk);
			if (ret) {
				dev_err(component->dev,
					"failed to enable MCLK: %d\n", ret);
				return ret;
			}
			regmap_update_bits(priv->regmap, CS53L30_MCLKCTL,
					   CS53L30_MCLK_DIS_MASK, 0);
			regmap_update_bits(priv->regmap, CS53L30_PWRCTL,
					   CS53L30_PDN_ULP_MASK, 0);
			msleep(50);
		} else {
			regmap_update_bits(priv->regmap, CS53L30_PWRCTL,
					   CS53L30_PDN_ULP_MASK,
					   CS53L30_PDN_ULP);
		}
		break;
	case SND_SOC_BIAS_OFF:
		regmap_update_bits(priv->regmap, CS53L30_INT_MASK,
				   CS53L30_PDN_DONE, 0);
		/*
		 * If digital softramp is set, the amount of time required
		 * for power down increases and depends on the digital
		 * volume setting.
		 */

		/* Set the max possible time if digsft is set */
		regmap_read(priv->regmap, CS53L30_SFT_RAMP, &reg);
		if (reg & CS53L30_DIGSFT_MASK)
			inter_max_check = CS53L30_PDN_POLL_MAX;
		else
			inter_max_check = 10;

		regmap_update_bits(priv->regmap, CS53L30_PWRCTL,
				   CS53L30_PDN_ULP_MASK,
				   CS53L30_PDN_ULP);
		/* PDN_DONE will take a min of 20ms to be set.*/
		msleep(20);
		/* Clr status */
		regmap_read(priv->regmap, CS53L30_IS, &reg);
		for (i = 0; i < inter_max_check; i++) {
			if (inter_max_check < 10) {
				usleep_range(1000, 1100);
				regmap_read(priv->regmap, CS53L30_IS, &reg);
				if (reg & CS53L30_PDN_DONE)
					break;
			} else {
				usleep_range(10000, 10100);
				regmap_read(priv->regmap, CS53L30_IS, &reg);
				if (reg & CS53L30_PDN_DONE)
					break;
			}
		}
		/* PDN_DONE is set. We now can disable the MCLK */
		regmap_update_bits(priv->regmap, CS53L30_INT_MASK,
				   CS53L30_PDN_DONE, CS53L30_PDN_DONE);
		regmap_update_bits(priv->regmap, CS53L30_MCLKCTL,
				   CS53L30_MCLK_DIS_MASK,
				   CS53L30_MCLK_DIS);
		clk_disable_unprepare(priv->mclk);
		break;
	}

	return 0;
}

static int cs53l30_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct cs53l30_private *priv = snd_soc_component_get_drvdata(dai->component);
	u8 val = tristate ? CS53L30_ASP_3ST : 0;

	return regmap_update_bits(priv->regmap, CS53L30_ASP_CTL1,
				  CS53L30_ASP_3ST_MASK, val);
}

static unsigned int const cs53l30_src_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};

static const struct snd_pcm_hw_constraint_list src_constraints = {
	.count = ARRAY_SIZE(cs53l30_src_rates),
	.list = cs53l30_src_rates,
};

static int cs53l30_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE, &src_constraints);

	return 0;
}

/*
 * Note: CS53L30 counts the slot number per byte while ASoC counts the slot
 * number per slot_width. So there is a difference between the slots of ASoC
 * and the slots of CS53L30.
 */
static int cs53l30_set_dai_tdm_slot(struct snd_soc_dai *dai,
				    unsigned int tx_mask, unsigned int rx_mask,
				    int slots, int slot_width)
{
	struct cs53l30_private *priv = snd_soc_component_get_drvdata(dai->component);
	unsigned int loc[CS53L30_TDM_SLOT_MAX] = {48, 48, 48, 48};
	unsigned int slot_next, slot_step;
	u64 tx_enable = 0;
	int i;

	if (!rx_mask) {
		dev_err(dai->dev, "rx masks must not be 0\n");
		return -EINVAL;
	}

	/* Assuming slot_width is not supposed to be greater than 64 */
	if (slots <= 0 || slot_width <= 0 || slot_width > 64) {
		dev_err(dai->dev, "invalid slot number or slot width\n");
		return -EINVAL;
	}

	if (slot_width & 0x7) {
		dev_err(dai->dev, "slot width must count in byte\n");
		return -EINVAL;
	}

	/* How many bytes in each ASoC slot */
	slot_step = slot_width >> 3;

	for (i = 0; rx_mask && i < CS53L30_TDM_SLOT_MAX; i++) {
		/* Find the first slot from LSB */
		slot_next = __ffs(rx_mask);
		/* Save the slot location by converting to CS53L30 slot */
		loc[i] = slot_next * slot_step;
		/* Create the mask of CS53L30 slot */
		tx_enable |= (u64)((u64)(1 << slot_step) - 1) << (u64)loc[i];
		/* Clear this slot from rx_mask */
		rx_mask &= ~(1 << slot_next);
	}

	/* Error out to avoid slot shift */
	if (rx_mask && i == CS53L30_TDM_SLOT_MAX) {
		dev_err(dai->dev, "rx_mask exceeds max slot number: %d\n",
			CS53L30_TDM_SLOT_MAX);
		return -EINVAL;
	}

	/* Validate the last active CS53L30 slot */
	slot_next = loc[i - 1] + slot_step - 1;
	if (slot_next > 47) {
		dev_err(dai->dev, "slot selection out of bounds: %u\n",
			slot_next);
		return -EINVAL;
	}

	for (i = 0; i < CS53L30_TDM_SLOT_MAX && loc[i] != 48; i++) {
		regmap_update_bits(priv->regmap, CS53L30_ASP_TDMTX_CTL(i),
				   CS53L30_ASP_CHx_TX_LOC_MASK, loc[i]);
		dev_dbg(dai->dev, "loc[%d]=%x\n", i, loc[i]);
	}

	for (i = 0; i < CS53L30_ASP_TDMTX_ENx_MAX && tx_enable; i++) {
		regmap_write(priv->regmap, CS53L30_ASP_TDMTX_ENx(i),
			     tx_enable & 0xff);
		tx_enable >>= 8;
		dev_dbg(dai->dev, "en_reg=%x, tx_enable=%llx\n",
			CS53L30_ASP_TDMTX_ENx(i), tx_enable & 0xff);
	}

	return 0;
}

static int cs53l30_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct cs53l30_private *priv = snd_soc_component_get_drvdata(dai->component);

	gpiod_set_value_cansleep(priv->mute_gpio, mute);

	return 0;
}

/* SNDRV_PCM_RATE_KNOT -> 12000, 24000 Hz, limit with constraint list */
#define CS53L30_RATES (SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_KNOT)

#define CS53L30_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops cs53l30_ops = {
	.startup = cs53l30_pcm_startup,
	.hw_params = cs53l30_pcm_hw_params,
	.set_fmt = cs53l30_set_dai_fmt,
	.set_sysclk = cs53l30_set_sysclk,
	.set_tristate = cs53l30_set_tristate,
	.set_tdm_slot = cs53l30_set_dai_tdm_slot,
	.mute_stream = cs53l30_mute_stream,
};

static struct snd_soc_dai_driver cs53l30_dai = {
	.name = "cs53l30",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = CS53L30_RATES,
		.formats = CS53L30_FORMATS,
	},
	.ops = &cs53l30_ops,
	.symmetric_rates = 1,
};

static int cs53l30_component_probe(struct snd_soc_component *component)
{
	struct cs53l30_private *priv = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	if (priv->use_sdout2)
		snd_soc_dapm_add_routes(dapm, cs53l30_dapm_routes_sdout2,
					ARRAY_SIZE(cs53l30_dapm_routes_sdout2));
	else
		snd_soc_dapm_add_routes(dapm, cs53l30_dapm_routes_sdout1,
					ARRAY_SIZE(cs53l30_dapm_routes_sdout1));

	return 0;
}

static const struct snd_soc_component_driver cs53l30_driver = {
	.probe			= cs53l30_component_probe,
	.set_bias_level		= cs53l30_set_bias_level,
	.controls		= cs53l30_snd_controls,
	.num_controls		= ARRAY_SIZE(cs53l30_snd_controls),
	.dapm_widgets		= cs53l30_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs53l30_dapm_widgets),
	.dapm_routes		= cs53l30_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(cs53l30_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct regmap_config cs53l30_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CS53L30_MAX_REGISTER,
	.reg_defaults = cs53l30_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs53l30_reg_defaults),
	.volatile_reg = cs53l30_volatile_register,
	.writeable_reg = cs53l30_writeable_register,
	.readable_reg = cs53l30_readable_register,
	.cache_type = REGCACHE_RBTREE,
};

static int cs53l30_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	const struct device_node *np = client->dev.of_node;
	struct device *dev = &client->dev;
	struct cs53l30_private *cs53l30;
	unsigned int devid = 0;
	unsigned int reg;
	int ret = 0, i;
	u8 val;

	cs53l30 = devm_kzalloc(dev, sizeof(*cs53l30), GFP_KERNEL);
	if (!cs53l30)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(cs53l30->supplies); i++)
		cs53l30->supplies[i].supply = cs53l30_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(cs53l30->supplies),
				      cs53l30->supplies);
	if (ret) {
		dev_err(dev, "failed to get supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(cs53l30->supplies),
				    cs53l30->supplies);
	if (ret) {
		dev_err(dev, "failed to enable supplies: %d\n", ret);
		return ret;
	}

	/* Reset the Device */
	cs53l30->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(cs53l30->reset_gpio)) {
		ret = PTR_ERR(cs53l30->reset_gpio);
		goto error;
	}

	gpiod_set_value_cansleep(cs53l30->reset_gpio, 1);

	i2c_set_clientdata(client, cs53l30);

	cs53l30->mclk_rate = 0;

	cs53l30->regmap = devm_regmap_init_i2c(client, &cs53l30_regmap);
	if (IS_ERR(cs53l30->regmap)) {
		ret = PTR_ERR(cs53l30->regmap);
		dev_err(dev, "regmap_init() failed: %d\n", ret);
		goto error;
	}

	/* Initialize codec */
	ret = regmap_read(cs53l30->regmap, CS53L30_DEVID_AB, &reg);
	devid = reg << 12;

	ret = regmap_read(cs53l30->regmap, CS53L30_DEVID_CD, &reg);
	devid |= reg << 4;

	ret = regmap_read(cs53l30->regmap, CS53L30_DEVID_E, &reg);
	devid |= (reg & 0xF0) >> 4;

	if (devid != CS53L30_DEVID) {
		ret = -ENODEV;
		dev_err(dev, "Device ID (%X). Expected %X\n",
			devid, CS53L30_DEVID);
		goto error;
	}

	ret = regmap_read(cs53l30->regmap, CS53L30_REVID, &reg);
	if (ret < 0) {
		dev_err(dev, "failed to get Revision ID: %d\n", ret);
		goto error;
	}

	/* Check if MCLK provided */
	cs53l30->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(cs53l30->mclk)) {
		if (PTR_ERR(cs53l30->mclk) != -ENOENT) {
			ret = PTR_ERR(cs53l30->mclk);
			goto error;
		}
		/* Otherwise mark the mclk pointer to NULL */
		cs53l30->mclk = NULL;
	}

	/* Fetch the MUTE control */
	cs53l30->mute_gpio = devm_gpiod_get_optional(dev, "mute",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(cs53l30->mute_gpio)) {
		ret = PTR_ERR(cs53l30->mute_gpio);
		goto error;
	}

	if (cs53l30->mute_gpio) {
		/* Enable MUTE controls via MUTE pin */
		regmap_write(cs53l30->regmap, CS53L30_MUTEP_CTL1,
			     CS53L30_MUTEP_CTL1_MUTEALL);
		/* Flip the polarity of MUTE pin */
		if (gpiod_is_active_low(cs53l30->mute_gpio))
			regmap_update_bits(cs53l30->regmap, CS53L30_MUTEP_CTL2,
					   CS53L30_MUTE_PIN_POLARITY, 0);
	}

	if (!of_property_read_u8(np, "cirrus,micbias-lvl", &val))
		regmap_update_bits(cs53l30->regmap, CS53L30_MICBIAS_CTL,
				   CS53L30_MIC_BIAS_CTRL_MASK, val);

	if (of_property_read_bool(np, "cirrus,use-sdout2"))
		cs53l30->use_sdout2 = true;

	dev_info(dev, "Cirrus Logic CS53L30, Revision: %02X\n", reg & 0xFF);

	ret = devm_snd_soc_register_component(dev, &cs53l30_driver, &cs53l30_dai, 1);
	if (ret) {
		dev_err(dev, "failed to register component: %d\n", ret);
		goto error;
	}

	return 0;

error:
	regulator_bulk_disable(ARRAY_SIZE(cs53l30->supplies),
			       cs53l30->supplies);
	return ret;
}

static int cs53l30_i2c_remove(struct i2c_client *client)
{
	struct cs53l30_private *cs53l30 = i2c_get_clientdata(client);

	/* Hold down reset */
	gpiod_set_value_cansleep(cs53l30->reset_gpio, 0);

	regulator_bulk_disable(ARRAY_SIZE(cs53l30->supplies),
			       cs53l30->supplies);

	return 0;
}

#ifdef CONFIG_PM
static int cs53l30_runtime_suspend(struct device *dev)
{
	struct cs53l30_private *cs53l30 = dev_get_drvdata(dev);

	regcache_cache_only(cs53l30->regmap, true);

	/* Hold down reset */
	gpiod_set_value_cansleep(cs53l30->reset_gpio, 0);

	regulator_bulk_disable(ARRAY_SIZE(cs53l30->supplies),
			       cs53l30->supplies);

	return 0;
}

static int cs53l30_runtime_resume(struct device *dev)
{
	struct cs53l30_private *cs53l30 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(cs53l30->supplies),
				    cs53l30->supplies);
	if (ret) {
		dev_err(dev, "failed to enable supplies: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(cs53l30->reset_gpio, 1);

	regcache_cache_only(cs53l30->regmap, false);
	ret = regcache_sync(cs53l30->regmap);
	if (ret) {
		dev_err(dev, "failed to synchronize regcache: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops cs53l30_runtime_pm = {
	SET_RUNTIME_PM_OPS(cs53l30_runtime_suspend, cs53l30_runtime_resume,
			   NULL)
};

static const struct of_device_id cs53l30_of_match[] = {
	{ .compatible = "cirrus,cs53l30", },
	{},
};

MODULE_DEVICE_TABLE(of, cs53l30_of_match);

static const struct i2c_device_id cs53l30_id[] = {
	{ "cs53l30", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, cs53l30_id);

static struct i2c_driver cs53l30_i2c_driver = {
	.driver = {
		.name = "cs53l30",
		.of_match_table = cs53l30_of_match,
		.pm = &cs53l30_runtime_pm,
	},
	.id_table = cs53l30_id,
	.probe = cs53l30_i2c_probe,
	.remove = cs53l30_i2c_remove,
};

module_i2c_driver(cs53l30_i2c_driver);

MODULE_DESCRIPTION("ASoC CS53L30 driver");
MODULE_AUTHOR("Paul Handrigan, Cirrus Logic Inc, <Paul.Handrigan@cirrus.com>");
MODULE_LICENSE("GPL");
