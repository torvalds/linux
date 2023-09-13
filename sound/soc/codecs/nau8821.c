// SPDX-License-Identifier: GPL-2.0-only
//
// nau8821.c -- Nuvoton NAU88L21 audio codec driver
//
// Copyright 2021 Nuvoton Technology Corp.
// Author: John Hsu <kchsu0@nuvoton.com>
// Co-author: Seven Lee <wtli@nuvoton.com>
//

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "nau8821.h"

#define NAU8821_JD_ACTIVE_HIGH			BIT(0)

static int nau8821_quirk;
static int quirk_override = -1;
module_param_named(quirk, quirk_override, uint, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");

#define NAU_FREF_MAX 13500000
#define NAU_FVCO_MAX 100000000
#define NAU_FVCO_MIN 90000000

#define NAU8821_BUTTON SND_JACK_BTN_0

/* the maximum frequency of CLK_ADC and CLK_DAC */
#define CLK_DA_AD_MAX 6144000

static int nau8821_configure_sysclk(struct nau8821 *nau8821,
	int clk_id, unsigned int freq);
static bool nau8821_is_jack_inserted(struct regmap *regmap);

struct nau8821_fll {
	int mclk_src;
	int ratio;
	int fll_frac;
	int fll_int;
	int clk_ref_div;
};

struct nau8821_fll_attr {
	unsigned int param;
	unsigned int val;
};

/* scaling for mclk from sysclk_src output */
static const struct nau8821_fll_attr mclk_src_scaling[] = {
	{ 1, 0x0 },
	{ 2, 0x2 },
	{ 4, 0x3 },
	{ 8, 0x4 },
	{ 16, 0x5 },
	{ 32, 0x6 },
	{ 3, 0x7 },
	{ 6, 0xa },
	{ 12, 0xb },
	{ 24, 0xc },
	{ 48, 0xd },
	{ 96, 0xe },
	{ 5, 0xf },
};

/* ratio for input clk freq */
static const struct nau8821_fll_attr fll_ratio[] = {
	{ 512000, 0x01 },
	{ 256000, 0x02 },
	{ 128000, 0x04 },
	{ 64000, 0x08 },
	{ 32000, 0x10 },
	{ 8000, 0x20 },
	{ 4000, 0x40 },
};

static const struct nau8821_fll_attr fll_pre_scalar[] = {
	{ 0, 0x0 },
	{ 1, 0x1 },
	{ 2, 0x2 },
	{ 3, 0x3 },
};

/* over sampling rate */
struct nau8821_osr_attr {
	unsigned int osr;
	unsigned int clk_src;
};

static const struct nau8821_osr_attr osr_dac_sel[] = {
	{ 64, 2 },	/* OSR 64, SRC 1/4 */
	{ 256, 0 },	/* OSR 256, SRC 1 */
	{ 128, 1 },	/* OSR 128, SRC 1/2 */
	{ 0, 0 },
	{ 32, 3 },	/* OSR 32, SRC 1/8 */
};

static const struct nau8821_osr_attr osr_adc_sel[] = {
	{ 32, 3 },	/* OSR 32, SRC 1/8 */
	{ 64, 2 },	/* OSR 64, SRC 1/4 */
	{ 128, 1 },	/* OSR 128, SRC 1/2 */
	{ 256, 0 },	/* OSR 256, SRC 1 */
};

struct nau8821_dmic_speed {
	unsigned int param;
	unsigned int val;
};

static const struct nau8821_dmic_speed dmic_speed_sel[] = {
	{ 0, 0x0 },	/*SPEED 1, SRC 1 */
	{ 1, 0x1 },	/*SPEED 2, SRC 1/2 */
	{ 2, 0x2 },	/*SPEED 4, SRC 1/4 */
	{ 3, 0x3 },	/*SPEED 8, SRC 1/8 */
};

static const struct reg_default nau8821_reg_defaults[] = {
	{ NAU8821_R01_ENA_CTRL, 0x00ff },
	{ NAU8821_R03_CLK_DIVIDER, 0x0050 },
	{ NAU8821_R04_FLL1, 0x0 },
	{ NAU8821_R05_FLL2, 0x00bc },
	{ NAU8821_R06_FLL3, 0x0008 },
	{ NAU8821_R07_FLL4, 0x0010 },
	{ NAU8821_R08_FLL5, 0x4000 },
	{ NAU8821_R09_FLL6, 0x6900 },
	{ NAU8821_R0A_FLL7, 0x0031 },
	{ NAU8821_R0B_FLL8, 0x26e9 },
	{ NAU8821_R0D_JACK_DET_CTRL, 0x0 },
	{ NAU8821_R0F_INTERRUPT_MASK, 0x0 },
	{ NAU8821_R12_INTERRUPT_DIS_CTRL, 0xffff },
	{ NAU8821_R13_DMIC_CTRL, 0x0 },
	{ NAU8821_R1A_GPIO12_CTRL, 0x0 },
	{ NAU8821_R1B_TDM_CTRL, 0x0 },
	{ NAU8821_R1C_I2S_PCM_CTRL1, 0x000a },
	{ NAU8821_R1D_I2S_PCM_CTRL2, 0x8010 },
	{ NAU8821_R1E_LEFT_TIME_SLOT, 0x0 },
	{ NAU8821_R1F_RIGHT_TIME_SLOT, 0x0 },
	{ NAU8821_R21_BIQ0_COF1, 0x0 },
	{ NAU8821_R22_BIQ0_COF2, 0x0 },
	{ NAU8821_R23_BIQ0_COF3, 0x0 },
	{ NAU8821_R24_BIQ0_COF4, 0x0 },
	{ NAU8821_R25_BIQ0_COF5, 0x0 },
	{ NAU8821_R26_BIQ0_COF6, 0x0 },
	{ NAU8821_R27_BIQ0_COF7, 0x0 },
	{ NAU8821_R28_BIQ0_COF8, 0x0 },
	{ NAU8821_R29_BIQ0_COF9, 0x0 },
	{ NAU8821_R2A_BIQ0_COF10, 0x0 },
	{ NAU8821_R2B_ADC_RATE, 0x0002 },
	{ NAU8821_R2C_DAC_CTRL1, 0x0082 },
	{ NAU8821_R2D_DAC_CTRL2, 0x0 },
	{ NAU8821_R2F_DAC_DGAIN_CTRL, 0x0 },
	{ NAU8821_R30_ADC_DGAIN_CTRL, 0x0 },
	{ NAU8821_R31_MUTE_CTRL, 0x0 },
	{ NAU8821_R32_HSVOL_CTRL, 0x0 },
	{ NAU8821_R34_DACR_CTRL, 0xcfcf },
	{ NAU8821_R35_ADC_DGAIN_CTRL1, 0xcfcf },
	{ NAU8821_R36_ADC_DRC_KNEE_IP12, 0x1486 },
	{ NAU8821_R37_ADC_DRC_KNEE_IP34, 0x0f12 },
	{ NAU8821_R38_ADC_DRC_SLOPES, 0x25ff },
	{ NAU8821_R39_ADC_DRC_ATKDCY, 0x3457 },
	{ NAU8821_R3A_DAC_DRC_KNEE_IP12, 0x1486 },
	{ NAU8821_R3B_DAC_DRC_KNEE_IP34, 0x0f12 },
	{ NAU8821_R3C_DAC_DRC_SLOPES, 0x25f9 },
	{ NAU8821_R3D_DAC_DRC_ATKDCY, 0x3457 },
	{ NAU8821_R41_BIQ1_COF1, 0x0 },
	{ NAU8821_R42_BIQ1_COF2, 0x0 },
	{ NAU8821_R43_BIQ1_COF3, 0x0 },
	{ NAU8821_R44_BIQ1_COF4, 0x0 },
	{ NAU8821_R45_BIQ1_COF5, 0x0 },
	{ NAU8821_R46_BIQ1_COF6, 0x0 },
	{ NAU8821_R47_BIQ1_COF7, 0x0 },
	{ NAU8821_R48_BIQ1_COF8, 0x0 },
	{ NAU8821_R49_BIQ1_COF9, 0x0 },
	{ NAU8821_R4A_BIQ1_COF10, 0x0 },
	{ NAU8821_R4B_CLASSG_CTRL, 0x0 },
	{ NAU8821_R4C_IMM_MODE_CTRL, 0x0 },
	{ NAU8821_R4D_IMM_RMS_L, 0x0 },
	{ NAU8821_R53_OTPDOUT_1, 0xaad8 },
	{ NAU8821_R54_OTPDOUT_2, 0x0002 },
	{ NAU8821_R55_MISC_CTRL, 0x0 },
	{ NAU8821_R66_BIAS_ADJ, 0x0 },
	{ NAU8821_R68_TRIM_SETTINGS, 0x0 },
	{ NAU8821_R69_ANALOG_CONTROL_1, 0x0 },
	{ NAU8821_R6A_ANALOG_CONTROL_2, 0x0 },
	{ NAU8821_R6B_PGA_MUTE, 0x0 },
	{ NAU8821_R71_ANALOG_ADC_1, 0x0011 },
	{ NAU8821_R72_ANALOG_ADC_2, 0x0020 },
	{ NAU8821_R73_RDAC, 0x0008 },
	{ NAU8821_R74_MIC_BIAS, 0x0006 },
	{ NAU8821_R76_BOOST, 0x0 },
	{ NAU8821_R77_FEPGA, 0x0 },
	{ NAU8821_R7E_PGA_GAIN, 0x0 },
	{ NAU8821_R7F_POWER_UP_CONTROL, 0x0 },
	{ NAU8821_R80_CHARGE_PUMP, 0x0 },
};

static bool nau8821_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8821_R00_RESET ... NAU8821_R01_ENA_CTRL:
	case NAU8821_R03_CLK_DIVIDER ... NAU8821_R0B_FLL8:
	case NAU8821_R0D_JACK_DET_CTRL:
	case NAU8821_R0F_INTERRUPT_MASK ... NAU8821_R13_DMIC_CTRL:
	case NAU8821_R1A_GPIO12_CTRL ... NAU8821_R1F_RIGHT_TIME_SLOT:
	case NAU8821_R21_BIQ0_COF1 ... NAU8821_R2D_DAC_CTRL2:
	case NAU8821_R2F_DAC_DGAIN_CTRL ... NAU8821_R32_HSVOL_CTRL:
	case NAU8821_R34_DACR_CTRL ... NAU8821_R3D_DAC_DRC_ATKDCY:
	case NAU8821_R41_BIQ1_COF1 ... NAU8821_R4F_FUSE_CTRL3:
	case NAU8821_R51_FUSE_CTRL1:
	case NAU8821_R53_OTPDOUT_1 ... NAU8821_R55_MISC_CTRL:
	case NAU8821_R58_I2C_DEVICE_ID ... NAU8821_R5A_SOFTWARE_RST:
	case NAU8821_R66_BIAS_ADJ:
	case NAU8821_R68_TRIM_SETTINGS ... NAU8821_R6B_PGA_MUTE:
	case NAU8821_R71_ANALOG_ADC_1 ... NAU8821_R74_MIC_BIAS:
	case NAU8821_R76_BOOST ... NAU8821_R77_FEPGA:
	case NAU8821_R7E_PGA_GAIN ... NAU8821_R82_GENERAL_STATUS:
		return true;
	default:
		return false;
	}
}

static bool nau8821_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8821_R00_RESET ... NAU8821_R01_ENA_CTRL:
	case NAU8821_R03_CLK_DIVIDER ... NAU8821_R0B_FLL8:
	case NAU8821_R0D_JACK_DET_CTRL:
	case NAU8821_R0F_INTERRUPT_MASK:
	case NAU8821_R11_INT_CLR_KEY_STATUS ... NAU8821_R13_DMIC_CTRL:
	case NAU8821_R1A_GPIO12_CTRL ... NAU8821_R1F_RIGHT_TIME_SLOT:
	case NAU8821_R21_BIQ0_COF1 ... NAU8821_R2D_DAC_CTRL2:
	case NAU8821_R2F_DAC_DGAIN_CTRL ... NAU8821_R32_HSVOL_CTRL:
	case NAU8821_R34_DACR_CTRL ... NAU8821_R3D_DAC_DRC_ATKDCY:
	case NAU8821_R41_BIQ1_COF1 ... NAU8821_R4C_IMM_MODE_CTRL:
	case NAU8821_R4E_FUSE_CTRL2 ... NAU8821_R4F_FUSE_CTRL3:
	case NAU8821_R51_FUSE_CTRL1:
	case NAU8821_R55_MISC_CTRL:
	case NAU8821_R5A_SOFTWARE_RST:
	case NAU8821_R66_BIAS_ADJ:
	case NAU8821_R68_TRIM_SETTINGS ... NAU8821_R6B_PGA_MUTE:
	case NAU8821_R71_ANALOG_ADC_1 ... NAU8821_R74_MIC_BIAS:
	case NAU8821_R76_BOOST ... NAU8821_R77_FEPGA:
	case NAU8821_R7E_PGA_GAIN ... NAU8821_R80_CHARGE_PUMP:
		return true;
	default:
		return false;
	}
}

static bool nau8821_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8821_R00_RESET:
	case NAU8821_R10_IRQ_STATUS ... NAU8821_R11_INT_CLR_KEY_STATUS:
	case NAU8821_R21_BIQ0_COF1 ... NAU8821_R2A_BIQ0_COF10:
	case NAU8821_R41_BIQ1_COF1 ... NAU8821_R4A_BIQ1_COF10:
	case NAU8821_R4D_IMM_RMS_L:
	case NAU8821_R53_OTPDOUT_1 ... NAU8821_R54_OTPDOUT_2:
	case NAU8821_R58_I2C_DEVICE_ID ... NAU8821_R5A_SOFTWARE_RST:
	case NAU8821_R81_CHARGE_PUMP_INPUT_READ ... NAU8821_R82_GENERAL_STATUS:
		return true;
	default:
		return false;
	}
}

static int nau8821_biq_coeff_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;

	if (!component->regmap)
		return -EINVAL;

	regmap_raw_read(component->regmap, NAU8821_R21_BIQ0_COF1,
		ucontrol->value.bytes.data, params->max);

	return 0;
}

static int nau8821_biq_coeff_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;
	void *data;

	if (!component->regmap)
		return -EINVAL;

	data = kmemdup(ucontrol->value.bytes.data,
		params->max, GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;

	regmap_raw_write(component->regmap, NAU8821_R21_BIQ0_COF1,
		data, params->max);

	kfree(data);

	return 0;
}

static const char * const nau8821_adc_decimation[] = {
	"32", "64", "128", "256" };

static const struct soc_enum nau8821_adc_decimation_enum =
	SOC_ENUM_SINGLE(NAU8821_R2B_ADC_RATE, NAU8821_ADC_SYNC_DOWN_SFT,
		ARRAY_SIZE(nau8821_adc_decimation), nau8821_adc_decimation);

static const char * const nau8821_dac_oversampl[] = {
	"64", "256", "128", "", "32" };

static const struct soc_enum nau8821_dac_oversampl_enum =
	SOC_ENUM_SINGLE(NAU8821_R2C_DAC_CTRL1, NAU8821_DAC_OVERSAMPLE_SFT,
		ARRAY_SIZE(nau8821_dac_oversampl), nau8821_dac_oversampl);

static const char * const nau8821_adc_drc_noise_gate[] = {
	"1:1", "2:1", "4:1", "8:1" };

static const struct soc_enum nau8821_adc_drc_noise_gate_enum =
	SOC_ENUM_SINGLE(NAU8821_R38_ADC_DRC_SLOPES, NAU8821_DRC_NG_SLP_ADC_SFT,
		ARRAY_SIZE(nau8821_adc_drc_noise_gate),
		nau8821_adc_drc_noise_gate);

static const char * const nau8821_adc_drc_expansion_slope[] = {
	"1:1", "2:1", "4:1" };

static const struct soc_enum nau8821_adc_drc_expansion_slope_enum =
	SOC_ENUM_SINGLE(NAU8821_R38_ADC_DRC_SLOPES, NAU8821_DRC_EXP_SLP_ADC_SFT,
		ARRAY_SIZE(nau8821_adc_drc_expansion_slope),
		nau8821_adc_drc_expansion_slope);

static const char * const nau8821_adc_drc_lower_region[] = {
	"0", "1:2", "1:4", "1:8", "1:16", "", "", "1:1" };

static const struct soc_enum nau8821_adc_drc_lower_region_enum =
	SOC_ENUM_SINGLE(NAU8821_R38_ADC_DRC_SLOPES,
		NAU8821_DRC_CMP2_SLP_ADC_SFT,
		ARRAY_SIZE(nau8821_adc_drc_lower_region),
		nau8821_adc_drc_lower_region);

static const char * const nau8821_higher_region[] = {
	"0", "1:2", "1:4", "1:8", "1:16", "", "", "1:1" };

static const struct soc_enum nau8821_higher_region_enum =
	SOC_ENUM_SINGLE(NAU8821_R38_ADC_DRC_SLOPES,
		NAU8821_DRC_CMP1_SLP_ADC_SFT,
		ARRAY_SIZE(nau8821_higher_region),
		nau8821_higher_region);

static const char * const nau8821_limiter_slope[] = {
	"0", "1:2", "1:4", "1:8", "1:16", "1:32", "1:64", "1:1" };

static const struct soc_enum nau8821_limiter_slope_enum =
	SOC_ENUM_SINGLE(NAU8821_R38_ADC_DRC_SLOPES,
		NAU8821_DRC_LMT_SLP_ADC_SFT, ARRAY_SIZE(nau8821_limiter_slope),
		nau8821_limiter_slope);

static const char * const nau8821_detection_attack_time[] = {
	"Ts", "3Ts", "7Ts", "15Ts", "31Ts", "63Ts", "127Ts", "255Ts",
	"", "511Ts" };

static const struct soc_enum nau8821_detection_attack_time_enum =
	SOC_ENUM_SINGLE(NAU8821_R39_ADC_DRC_ATKDCY,
		NAU8821_DRC_PK_COEF1_ADC_SFT,
		ARRAY_SIZE(nau8821_detection_attack_time),
		nau8821_detection_attack_time);

static const char * const nau8821_detection_release_time[] = {
	"63Ts", "127Ts", "255Ts", "511Ts", "1023Ts", "2047Ts", "4095Ts",
	"8191Ts", "", "16383Ts" };

static const struct soc_enum nau8821_detection_release_time_enum =
	SOC_ENUM_SINGLE(NAU8821_R39_ADC_DRC_ATKDCY,
		NAU8821_DRC_PK_COEF2_ADC_SFT,
		ARRAY_SIZE(nau8821_detection_release_time),
		nau8821_detection_release_time);

static const char * const nau8821_attack_time[] = {
	"Ts", "3Ts", "7Ts", "15Ts", "31Ts", "63Ts", "127Ts", "255Ts",
	"511Ts", "1023Ts", "2047Ts", "4095Ts", "8191Ts" };

static const struct soc_enum nau8821_attack_time_enum =
	SOC_ENUM_SINGLE(NAU8821_R39_ADC_DRC_ATKDCY, NAU8821_DRC_ATK_ADC_SFT,
		ARRAY_SIZE(nau8821_attack_time), nau8821_attack_time);

static const char * const nau8821_decay_time[] = {
	"63Ts", "127Ts", "255Ts", "511Ts", "1023Ts", "2047Ts", "4095Ts",
	"8191Ts", "16383Ts", "32757Ts", "65535Ts" };

static const struct soc_enum nau8821_decay_time_enum =
	SOC_ENUM_SINGLE(NAU8821_R39_ADC_DRC_ATKDCY, NAU8821_DRC_DCY_ADC_SFT,
		ARRAY_SIZE(nau8821_decay_time), nau8821_decay_time);

static const DECLARE_TLV_DB_MINMAX_MUTE(adc_vol_tlv, -6600, 2400);
static const DECLARE_TLV_DB_MINMAX_MUTE(sidetone_vol_tlv, -4200, 0);
static const DECLARE_TLV_DB_MINMAX(hp_vol_tlv, -900, 0);
static const DECLARE_TLV_DB_SCALE(playback_vol_tlv, -6600, 50, 1);
static const DECLARE_TLV_DB_MINMAX(fepga_gain_tlv, -100, 3600);
static const DECLARE_TLV_DB_MINMAX_MUTE(crosstalk_vol_tlv, -7000, 2400);
static const DECLARE_TLV_DB_MINMAX(drc_knee4_tlv, -9800, -3500);
static const DECLARE_TLV_DB_MINMAX(drc_knee3_tlv, -8100, -1800);

static const struct snd_kcontrol_new nau8821_controls[] = {
	SOC_DOUBLE_TLV("Mic Volume", NAU8821_R35_ADC_DGAIN_CTRL1,
		NAU8821_ADCL_CH_VOL_SFT, NAU8821_ADCR_CH_VOL_SFT,
		0xff, 0, adc_vol_tlv),
	SOC_DOUBLE_TLV("Headphone Bypass Volume", NAU8821_R30_ADC_DGAIN_CTRL,
		12, 8, 0x0f, 0, sidetone_vol_tlv),
	SOC_DOUBLE_TLV("Headphone Volume", NAU8821_R32_HSVOL_CTRL,
		NAU8821_HPL_VOL_SFT, NAU8821_HPR_VOL_SFT, 0x3, 1, hp_vol_tlv),
	SOC_DOUBLE_TLV("Digital Playback Volume", NAU8821_R34_DACR_CTRL,
		NAU8821_DACL_CH_VOL_SFT, NAU8821_DACR_CH_VOL_SFT,
		0xcf, 0, playback_vol_tlv),
	SOC_DOUBLE_TLV("Frontend PGA Volume", NAU8821_R7E_PGA_GAIN,
		NAU8821_PGA_GAIN_L_SFT, NAU8821_PGA_GAIN_R_SFT,
		37, 0, fepga_gain_tlv),
	SOC_DOUBLE_TLV("Headphone Crosstalk Volume",
		NAU8821_R2F_DAC_DGAIN_CTRL,
		0, 8, 0xff, 0, crosstalk_vol_tlv),
	SOC_SINGLE_TLV("ADC DRC KNEE4", NAU8821_R37_ADC_DRC_KNEE_IP34,
		NAU8821_DRC_KNEE4_IP_ADC_SFT, 0x3f, 1, drc_knee4_tlv),
	SOC_SINGLE_TLV("ADC DRC KNEE3", NAU8821_R37_ADC_DRC_KNEE_IP34,
		NAU8821_DRC_KNEE3_IP_ADC_SFT, 0x3f, 1, drc_knee3_tlv),

	SOC_ENUM("ADC DRC Noise Gate", nau8821_adc_drc_noise_gate_enum),
	SOC_ENUM("ADC DRC Expansion Slope", nau8821_adc_drc_expansion_slope_enum),
	SOC_ENUM("ADC DRC Lower Region", nau8821_adc_drc_lower_region_enum),
	SOC_ENUM("ADC DRC Higher Region", nau8821_higher_region_enum),
	SOC_ENUM("ADC DRC Limiter Slope", nau8821_limiter_slope_enum),
	SOC_ENUM("ADC DRC Peak Detection Attack Time", nau8821_detection_attack_time_enum),
	SOC_ENUM("ADC DRC Peak Detection Release Time", nau8821_detection_release_time_enum),
	SOC_ENUM("ADC DRC Attack Time", nau8821_attack_time_enum),
	SOC_ENUM("ADC DRC Decay Time", nau8821_decay_time_enum),
	SOC_SINGLE("DRC Enable Switch", NAU8821_R36_ADC_DRC_KNEE_IP12,
		NAU8821_DRC_ENA_ADC_SFT, 1, 0),

	SOC_ENUM("ADC Decimation Rate", nau8821_adc_decimation_enum),
	SOC_ENUM("DAC Oversampling Rate", nau8821_dac_oversampl_enum),
	SND_SOC_BYTES_EXT("BIQ Coefficients", 20,
		nau8821_biq_coeff_get, nau8821_biq_coeff_put),
	SOC_SINGLE("ADC Phase Switch", NAU8821_R1B_TDM_CTRL,
		NAU8821_ADCPHS_SFT, 1, 0),
};

static const struct snd_kcontrol_new nau8821_dmic_mode_switch =
	SOC_DAPM_SINGLE("Switch", NAU8821_R13_DMIC_CTRL,
		NAU8821_DMIC_EN_SFT, 1, 0);

static int dmic_clock_control(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *k, int  event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);
	int i, speed_selection = -1, clk_adc_src, clk_adc;
	unsigned int clk_divider_r03;

	/* The DMIC clock is gotten from adc clock divided by
	 * CLK_DMIC_SRC (1, 2, 4, 8). The clock has to be equal or
	 * less than nau8821->dmic_clk_threshold.
	 */
	regmap_read(nau8821->regmap, NAU8821_R03_CLK_DIVIDER,
		&clk_divider_r03);
	clk_adc_src = (clk_divider_r03 & NAU8821_CLK_ADC_SRC_MASK)
		>> NAU8821_CLK_ADC_SRC_SFT;
	clk_adc = (nau8821->fs * 256) >> clk_adc_src;

	for (i = 0 ; i < 4 ; i++)
		if ((clk_adc >> dmic_speed_sel[i].param) <=
			nau8821->dmic_clk_threshold) {
			speed_selection = dmic_speed_sel[i].val;
			break;
		}
	if (i == 4)
		return -EINVAL;

	dev_dbg(nau8821->dev,
		"clk_adc=%d, dmic_clk_threshold = %d, param=%d, val = %d\n",
		clk_adc, nau8821->dmic_clk_threshold,
		dmic_speed_sel[i].param, dmic_speed_sel[i].val);
	regmap_update_bits(nau8821->regmap, NAU8821_R13_DMIC_CTRL,
		NAU8821_DMIC_SRC_MASK,
		(speed_selection << NAU8821_DMIC_SRC_SFT));

	return 0;
}

static int nau8821_left_adc_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(125);
		regmap_update_bits(nau8821->regmap, NAU8821_R01_ENA_CTRL,
			NAU8821_EN_ADCL, NAU8821_EN_ADCL);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(nau8821->regmap,
			NAU8821_R01_ENA_CTRL, NAU8821_EN_ADCL, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nau8821_right_adc_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(125);
		regmap_update_bits(nau8821->regmap, NAU8821_R01_ENA_CTRL,
			NAU8821_EN_ADCR, NAU8821_EN_ADCR);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(nau8821->regmap,
			NAU8821_R01_ENA_CTRL, NAU8821_EN_ADCR, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nau8821_pump_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct nau8821 *nau8821 =
		snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Prevent startup click by letting charge pump to ramp up */
		msleep(20);
		regmap_update_bits(nau8821->regmap, NAU8821_R80_CHARGE_PUMP,
			NAU8821_JAMNODCLOW, NAU8821_JAMNODCLOW);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(nau8821->regmap, NAU8821_R80_CHARGE_PUMP,
			NAU8821_JAMNODCLOW, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nau8821_output_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Disables the TESTDAC to let DAC signal pass through. */
		regmap_update_bits(nau8821->regmap, NAU8821_R66_BIAS_ADJ,
			NAU8821_BIAS_TESTDAC_EN, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(nau8821->regmap, NAU8821_R66_BIAS_ADJ,
			NAU8821_BIAS_TESTDAC_EN, NAU8821_BIAS_TESTDAC_EN);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int system_clock_control(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int  event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		dev_dbg(nau8821->dev, "system clock control : POWER OFF\n");
		/* Set clock source to disable or internal clock before the
		 * playback or capture end. Codec needs clock for Jack
		 * detection and button press if jack inserted; otherwise,
		 * the clock should be closed.
		 */
		if (nau8821_is_jack_inserted(nau8821->regmap)) {
			nau8821_configure_sysclk(nau8821,
				NAU8821_CLK_INTERNAL, 0);
		} else {
			nau8821_configure_sysclk(nau8821, NAU8821_CLK_DIS, 0);
		}
	}
	return 0;
}

static int nau8821_left_fepga_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);

	if (!nau8821->left_input_single_end)
		return 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(nau8821->regmap, NAU8821_R77_FEPGA,
			NAU8821_ACDC_CTRL_MASK | NAU8821_FEPGA_MODEL_MASK,
			NAU8821_ACDC_VREF_MICN | NAU8821_FEPGA_MODEL_AAF);
		regmap_update_bits(nau8821->regmap, NAU8821_R76_BOOST,
			NAU8821_HP_BOOST_DISCHRG_EN, NAU8821_HP_BOOST_DISCHRG_EN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(nau8821->regmap, NAU8821_R77_FEPGA,
			NAU8821_ACDC_CTRL_MASK | NAU8821_FEPGA_MODEL_MASK, 0);
		regmap_update_bits(nau8821->regmap, NAU8821_R76_BOOST,
			NAU8821_HP_BOOST_DISCHRG_EN, 0);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget nau8821_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("System Clock", SND_SOC_NOPM, 0, 0,
		system_clock_control, SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MICBIAS", NAU8821_R74_MIC_BIAS,
		NAU8821_MICBIAS_POWERUP_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC Clock", SND_SOC_NOPM, 0, 0,
		dmic_clock_control, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC("ADCL Power", NULL, NAU8821_R72_ANALOG_ADC_2,
		NAU8821_POWERUP_ADCL_SFT, 0),
	SND_SOC_DAPM_ADC("ADCR Power", NULL, NAU8821_R72_ANALOG_ADC_2,
		NAU8821_POWERUP_ADCR_SFT, 0),
	/* single-ended design only on the left */
	SND_SOC_DAPM_PGA_S("Frontend PGA L", 1, NAU8821_R7F_POWER_UP_CONTROL,
		NAU8821_PUP_PGA_L_SFT, 0, nau8821_left_fepga_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_S("Frontend PGA R", 1, NAU8821_R7F_POWER_UP_CONTROL,
		NAU8821_PUP_PGA_R_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADCL Digital path", 0, NAU8821_R01_ENA_CTRL,
		NAU8821_EN_ADCL_SFT, 0, nau8821_left_adc_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_S("ADCR Digital path", 0, NAU8821_R01_ENA_CTRL,
		NAU8821_EN_ADCR_SFT, 0, nau8821_right_adc_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SWITCH("DMIC Enable", SND_SOC_NOPM,
		0, 0, &nau8821_dmic_mode_switch),
	SND_SOC_DAPM_AIF_OUT("AIFTX", "Capture", 0, NAU8821_R1D_I2S_PCM_CTRL2,
		NAU8821_I2S_TRISTATE_SFT, 1),
	SND_SOC_DAPM_AIF_IN("AIFRX", "Playback", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_PGA_S("ADACL", 2, NAU8821_R73_RDAC,
		NAU8821_DACL_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADACR", 2, NAU8821_R73_RDAC,
		NAU8821_DACR_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADACL Clock", 3, NAU8821_R73_RDAC,
		NAU8821_DACL_CLK_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADACR Clock", 3, NAU8821_R73_RDAC,
		NAU8821_DACR_CLK_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DDACR", NULL, NAU8821_R01_ENA_CTRL,
		NAU8821_EN_DACR_SFT, 0),
	SND_SOC_DAPM_DAC("DDACL", NULL, NAU8821_R01_ENA_CTRL,
		NAU8821_EN_DACL_SFT, 0),
	SND_SOC_DAPM_PGA_S("HP amp L", 0, NAU8821_R4B_CLASSG_CTRL,
		NAU8821_CLASSG_LDAC_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("HP amp R", 0, NAU8821_R4B_CLASSG_CTRL,
		NAU8821_CLASSG_RDAC_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Charge Pump", 1, NAU8821_R80_CHARGE_PUMP,
		NAU8821_CHANRGE_PUMP_EN_SFT, 0, nau8821_pump_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_S("Output Driver R Stage 1", 4,
		NAU8821_R7F_POWER_UP_CONTROL,
		NAU8821_PUP_INTEG_R_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver L Stage 1", 4,
		NAU8821_R7F_POWER_UP_CONTROL,
		NAU8821_PUP_INTEG_L_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver R Stage 2", 5,
		NAU8821_R7F_POWER_UP_CONTROL,
		NAU8821_PUP_DRV_INSTG_R_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver L Stage 2", 5,
		NAU8821_R7F_POWER_UP_CONTROL,
		NAU8821_PUP_DRV_INSTG_L_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver R Stage 3", 6,
		NAU8821_R7F_POWER_UP_CONTROL,
		NAU8821_PUP_MAIN_DRV_R_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver L Stage 3", 6,
		NAU8821_R7F_POWER_UP_CONTROL,
		NAU8821_PUP_MAIN_DRV_L_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output DACL", 7,
		NAU8821_R80_CHARGE_PUMP, NAU8821_POWER_DOWN_DACL_SFT,
		0, nau8821_output_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_S("Output DACR", 7,
		NAU8821_R80_CHARGE_PUMP, NAU8821_POWER_DOWN_DACR_SFT,
		0, nau8821_output_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* HPOL/R are ungrounded by disabling 16 Ohm pull-downs on playback */
	SND_SOC_DAPM_PGA_S("HPOL Pulldown", 8,
		NAU8821_R0D_JACK_DET_CTRL,
		NAU8821_SPKR_DWN1L_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("HPOR Pulldown", 8,
		NAU8821_R0D_JACK_DET_CTRL,
		NAU8821_SPKR_DWN1R_SFT, 0, NULL, 0),

	/* High current HPOL/R boost driver */
	SND_SOC_DAPM_PGA_S("HP Boost Driver", 9,
		NAU8821_R76_BOOST, NAU8821_HP_BOOST_DIS_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Class G", NAU8821_R4B_CLASSG_CTRL,
		NAU8821_CLASSG_EN_SFT, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("MICL"),
	SND_SOC_DAPM_INPUT("MICR"),
	SND_SOC_DAPM_INPUT("DMIC"),
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_route nau8821_dapm_routes[] = {
	{"DMIC Enable", "Switch", "DMIC"},
	{"DMIC Enable", NULL, "DMIC Clock"},

	{"Frontend PGA L", NULL, "MICL"},
	{"Frontend PGA R", NULL, "MICR"},
	{"Frontend PGA L", NULL, "MICBIAS"},
	{"Frontend PGA R", NULL, "MICBIAS"},

	{"ADCL Power", NULL, "Frontend PGA L"},
	{"ADCR Power", NULL, "Frontend PGA R"},

	{"ADCL Digital path", NULL, "ADCL Power"},
	{"ADCR Digital path", NULL, "ADCR Power"},
	{"ADCL Digital path", NULL, "DMIC Enable"},
	{"ADCR Digital path", NULL, "DMIC Enable"},

	{"AIFTX", NULL, "ADCL Digital path"},
	{"AIFTX", NULL, "ADCR Digital path"},

	{"AIFTX", NULL, "System Clock"},
	{"AIFRX", NULL, "System Clock"},

	{"DDACL", NULL, "AIFRX"},
	{"DDACR", NULL, "AIFRX"},

	{"HP amp L", NULL, "DDACL"},
	{"HP amp R", NULL, "DDACR"},

	{"Charge Pump", NULL, "HP amp L"},
	{"Charge Pump", NULL, "HP amp R"},

	{"ADACL", NULL, "Charge Pump"},
	{"ADACR", NULL, "Charge Pump"},
	{"ADACL Clock", NULL, "ADACL"},
	{"ADACR Clock", NULL, "ADACR"},

	{"Output Driver L Stage 1", NULL, "ADACL Clock"},
	{"Output Driver R Stage 1", NULL, "ADACR Clock"},
	{"Output Driver L Stage 2", NULL, "Output Driver L Stage 1"},
	{"Output Driver R Stage 2", NULL, "Output Driver R Stage 1"},
	{"Output Driver L Stage 3", NULL, "Output Driver L Stage 2"},
	{"Output Driver R Stage 3", NULL, "Output Driver R Stage 2"},
	{"Output DACL", NULL, "Output Driver L Stage 3"},
	{"Output DACR", NULL, "Output Driver R Stage 3"},

	{"HPOL Pulldown", NULL, "Output DACL"},
	{"HPOR Pulldown", NULL, "Output DACR"},
	{"HP Boost Driver", NULL, "HPOL Pulldown"},
	{"HP Boost Driver", NULL, "HPOR Pulldown"},

	{"Class G", NULL, "HP Boost Driver"},
	{"HPOL", NULL, "Class G"},
	{"HPOR", NULL, "Class G"},
};

static const struct nau8821_osr_attr *
nau8821_get_osr(struct nau8821 *nau8821, int stream)
{
	unsigned int osr;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_read(nau8821->regmap, NAU8821_R2C_DAC_CTRL1, &osr);
		osr &= NAU8821_DAC_OVERSAMPLE_MASK;
		if (osr >= ARRAY_SIZE(osr_dac_sel))
			return NULL;
		return &osr_dac_sel[osr];
	} else {
		regmap_read(nau8821->regmap, NAU8821_R2B_ADC_RATE, &osr);
		osr &= NAU8821_ADC_SYNC_DOWN_MASK;
		if (osr >= ARRAY_SIZE(osr_adc_sel))
			return NULL;
		return &osr_adc_sel[osr];
	}
}

static int nau8821_dai_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);
	const struct nau8821_osr_attr *osr;

	osr = nau8821_get_osr(nau8821, substream->stream);
	if (!osr || !osr->osr)
		return -EINVAL;

	return snd_pcm_hw_constraint_minmax(substream->runtime,
					    SNDRV_PCM_HW_PARAM_RATE,
					    0, CLK_DA_AD_MAX / osr->osr);
}

static int nau8821_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);
	unsigned int val_len = 0, ctrl_val, bclk_fs, clk_div;
	const struct nau8821_osr_attr *osr;

	nau8821->fs = params_rate(params);
	/* CLK_DAC or CLK_ADC = OSR * FS
	 * DAC or ADC clock frequency is defined as Over Sampling Rate (OSR)
	 * multiplied by the audio sample rate (Fs). Note that the OSR and Fs
	 * values must be selected such that the maximum frequency is less
	 * than 6.144 MHz.
	 */
	osr = nau8821_get_osr(nau8821, substream->stream);
	if (!osr || !osr->osr)
		return -EINVAL;
	if (nau8821->fs * osr->osr > CLK_DA_AD_MAX)
		return -EINVAL;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_update_bits(nau8821->regmap, NAU8821_R03_CLK_DIVIDER,
			NAU8821_CLK_DAC_SRC_MASK,
			osr->clk_src << NAU8821_CLK_DAC_SRC_SFT);
	else
		regmap_update_bits(nau8821->regmap, NAU8821_R03_CLK_DIVIDER,
			NAU8821_CLK_ADC_SRC_MASK,
			osr->clk_src << NAU8821_CLK_ADC_SRC_SFT);

	/* make BCLK and LRC divde configuration if the codec as master. */
	regmap_read(nau8821->regmap, NAU8821_R1D_I2S_PCM_CTRL2, &ctrl_val);
	if (ctrl_val & NAU8821_I2S_MS_MASTER) {
		/* get the bclk and fs ratio */
		bclk_fs = snd_soc_params_to_bclk(params) / nau8821->fs;
		if (bclk_fs <= 32)
			clk_div = 3;
		else if (bclk_fs <= 64)
			clk_div = 2;
		else if (bclk_fs <= 128)
			clk_div = 1;
		else {
			return -EINVAL;
		}
		regmap_update_bits(nau8821->regmap, NAU8821_R1D_I2S_PCM_CTRL2,
			NAU8821_I2S_LRC_DIV_MASK | NAU8821_I2S_BLK_DIV_MASK,
			(clk_div << NAU8821_I2S_LRC_DIV_SFT) | clk_div);
	}

	switch (params_width(params)) {
	case 16:
		val_len |= NAU8821_I2S_DL_16;
		break;
	case 20:
		val_len |= NAU8821_I2S_DL_20;
		break;
	case 24:
		val_len |= NAU8821_I2S_DL_24;
		break;
	case 32:
		val_len |= NAU8821_I2S_DL_32;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(nau8821->regmap, NAU8821_R1C_I2S_PCM_CTRL1,
		NAU8821_I2S_DL_MASK, val_len);

	return 0;
}

static int nau8821_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);
	unsigned int ctrl1_val = 0, ctrl2_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		ctrl2_val |= NAU8821_I2S_MS_MASTER;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ctrl1_val |= NAU8821_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ctrl1_val |= NAU8821_I2S_DF_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ctrl1_val |= NAU8821_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ctrl1_val |= NAU8821_I2S_DF_RIGTH;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		ctrl1_val |= NAU8821_I2S_DF_PCM_AB;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		ctrl1_val |= NAU8821_I2S_DF_PCM_AB;
		ctrl1_val |= NAU8821_I2S_PCMB_EN;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(nau8821->regmap, NAU8821_R1C_I2S_PCM_CTRL1,
		NAU8821_I2S_DL_MASK | NAU8821_I2S_DF_MASK |
		NAU8821_I2S_BP_MASK | NAU8821_I2S_PCMB_MASK, ctrl1_val);
	regmap_update_bits(nau8821->regmap, NAU8821_R1D_I2S_PCM_CTRL2,
		NAU8821_I2S_MS_MASK, ctrl2_val);

	return 0;
}

static int nau8821_digital_mute(struct snd_soc_dai *dai, int mute,
		int direction)
{
	struct snd_soc_component *component = dai->component;
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);
	unsigned int val = 0;

	if (mute)
		val = NAU8821_DAC_SOFT_MUTE;

	return regmap_update_bits(nau8821->regmap,
		NAU8821_R31_MUTE_CTRL, NAU8821_DAC_SOFT_MUTE, val);
}

static const struct snd_soc_dai_ops nau8821_dai_ops = {
	.startup = nau8821_dai_startup,
	.hw_params = nau8821_hw_params,
	.set_fmt = nau8821_set_dai_fmt,
	.mute_stream = nau8821_digital_mute,
	.no_capture_mute = 1,
};

#define NAU8821_RATES SNDRV_PCM_RATE_8000_192000
#define NAU8821_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE \
	| SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver nau8821_dai = {
	.name = NUVOTON_CODEC_DAI,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = NAU8821_RATES,
		.formats = NAU8821_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = NAU8821_RATES,
		.formats = NAU8821_FORMATS,
	},
	.ops = &nau8821_dai_ops,
};


static bool nau8821_is_jack_inserted(struct regmap *regmap)
{
	bool active_high, is_high;
	int status, jkdet;

	regmap_read(regmap, NAU8821_R0D_JACK_DET_CTRL, &jkdet);
	active_high = jkdet & NAU8821_JACK_POLARITY;
	regmap_read(regmap, NAU8821_R82_GENERAL_STATUS, &status);
	is_high = status & NAU8821_GPIO2_IN;
	/* return jack connection status according to jack insertion logic
	 * active high or active low.
	 */
	return active_high == is_high;
}

static void nau8821_int_status_clear_all(struct regmap *regmap)
{
	int active_irq, clear_irq, i;

	/* Reset the intrruption status from rightmost bit if the corres-
	 * ponding irq event occurs.
	 */
	regmap_read(regmap, NAU8821_R10_IRQ_STATUS, &active_irq);
	for (i = 0; i < NAU8821_REG_DATA_LEN; i++) {
		clear_irq = (0x1 << i);
		if (active_irq & clear_irq)
			regmap_write(regmap,
				NAU8821_R11_INT_CLR_KEY_STATUS, clear_irq);
	}
}

static void nau8821_eject_jack(struct nau8821 *nau8821)
{
	struct snd_soc_dapm_context *dapm = nau8821->dapm;
	struct regmap *regmap = nau8821->regmap;
	struct snd_soc_component *component = snd_soc_dapm_to_component(dapm);

	/* Detach 2kOhm Resistors from MICBIAS to MICGND */
	regmap_update_bits(regmap, NAU8821_R74_MIC_BIAS,
		NAU8821_MICBIAS_JKR2, 0);
	/* HPL/HPR short to ground */
	regmap_update_bits(regmap, NAU8821_R0D_JACK_DET_CTRL,
		NAU8821_SPKR_DWN1R | NAU8821_SPKR_DWN1L, 0);
	snd_soc_component_disable_pin(component, "MICBIAS");
	snd_soc_dapm_sync(dapm);

	/* Clear all interruption status */
	nau8821_int_status_clear_all(regmap);

	/* Enable the insertion interruption, disable the ejection inter-
	 * ruption, and then bypass de-bounce circuit.
	 */
	regmap_update_bits(regmap, NAU8821_R12_INTERRUPT_DIS_CTRL,
		NAU8821_IRQ_EJECT_DIS | NAU8821_IRQ_INSERT_DIS,
		NAU8821_IRQ_EJECT_DIS);
	/* Mask unneeded IRQs: 1 - disable, 0 - enable */
	regmap_update_bits(regmap, NAU8821_R0F_INTERRUPT_MASK,
		NAU8821_IRQ_EJECT_EN | NAU8821_IRQ_INSERT_EN,
		NAU8821_IRQ_EJECT_EN);

	regmap_update_bits(regmap, NAU8821_R0D_JACK_DET_CTRL,
		NAU8821_JACK_DET_DB_BYPASS, NAU8821_JACK_DET_DB_BYPASS);

	/* Close clock for jack type detection at manual mode */
	if (dapm->bias_level < SND_SOC_BIAS_PREPARE)
		nau8821_configure_sysclk(nau8821, NAU8821_CLK_DIS, 0);

	/* Recover to normal channel input */
	regmap_update_bits(regmap, NAU8821_R2B_ADC_RATE,
			NAU8821_ADC_R_SRC_EN, 0);
	if (nau8821->key_enable) {
		regmap_update_bits(regmap, NAU8821_R0F_INTERRUPT_MASK,
			NAU8821_IRQ_KEY_RELEASE_EN |
			NAU8821_IRQ_KEY_PRESS_EN,
			NAU8821_IRQ_KEY_RELEASE_EN |
			NAU8821_IRQ_KEY_PRESS_EN);
		regmap_update_bits(regmap,
			NAU8821_R12_INTERRUPT_DIS_CTRL,
			NAU8821_IRQ_KEY_RELEASE_DIS |
			NAU8821_IRQ_KEY_PRESS_DIS,
			NAU8821_IRQ_KEY_RELEASE_DIS |
			NAU8821_IRQ_KEY_PRESS_DIS);
	}

}

static void nau8821_jdet_work(struct work_struct *work)
{
	struct nau8821 *nau8821 =
		container_of(work, struct nau8821, jdet_work);
	struct snd_soc_dapm_context *dapm = nau8821->dapm;
	struct snd_soc_component *component = snd_soc_dapm_to_component(dapm);
	struct regmap *regmap = nau8821->regmap;
	int jack_status_reg, mic_detected, event = 0, event_mask = 0;

	snd_soc_component_force_enable_pin(component, "MICBIAS");
	snd_soc_dapm_sync(dapm);
	msleep(20);

	regmap_read(regmap, NAU8821_R58_I2C_DEVICE_ID, &jack_status_reg);
	mic_detected = !(jack_status_reg & NAU8821_KEYDET);
	if (mic_detected) {
		dev_dbg(nau8821->dev, "Headset connected\n");
		event |= SND_JACK_HEADSET;

		/* 2kOhm Resistor from MICBIAS to MICGND1 */
		regmap_update_bits(regmap, NAU8821_R74_MIC_BIAS,
			NAU8821_MICBIAS_JKR2, NAU8821_MICBIAS_JKR2);
		/* Latch Right Channel Analog data
		 * input into the Right Channel Filter
		 */
		regmap_update_bits(regmap, NAU8821_R2B_ADC_RATE,
			NAU8821_ADC_R_SRC_EN, NAU8821_ADC_R_SRC_EN);
		if (nau8821->key_enable) {
			regmap_update_bits(regmap, NAU8821_R0F_INTERRUPT_MASK,
				NAU8821_IRQ_KEY_RELEASE_EN |
				NAU8821_IRQ_KEY_PRESS_EN, 0);
			regmap_update_bits(regmap,
				NAU8821_R12_INTERRUPT_DIS_CTRL,
				NAU8821_IRQ_KEY_RELEASE_DIS |
				NAU8821_IRQ_KEY_PRESS_DIS, 0);
		} else {
			snd_soc_component_disable_pin(component, "MICBIAS");
			snd_soc_dapm_sync(nau8821->dapm);
		}
	} else {
		dev_dbg(nau8821->dev, "Headphone connected\n");
		event |= SND_JACK_HEADPHONE;
		snd_soc_component_disable_pin(component, "MICBIAS");
		snd_soc_dapm_sync(dapm);
	}
	event_mask |= SND_JACK_HEADSET;
	snd_soc_jack_report(nau8821->jack, event, event_mask);
}

/* Enable interruptions with internal clock. */
static void nau8821_setup_inserted_irq(struct nau8821 *nau8821)
{
	struct regmap *regmap = nau8821->regmap;

	/* Enable internal VCO needed for interruptions */
	if (nau8821->dapm->bias_level < SND_SOC_BIAS_PREPARE)
		nau8821_configure_sysclk(nau8821, NAU8821_CLK_INTERNAL, 0);

	/* Chip needs one FSCLK cycle in order to generate interruptions,
	 * as we cannot guarantee one will be provided by the system. Turning
	 * master mode on then off enables us to generate that FSCLK cycle
	 * with a minimum of contention on the clock bus.
	 */
	regmap_update_bits(regmap, NAU8821_R1D_I2S_PCM_CTRL2,
		NAU8821_I2S_MS_MASK, NAU8821_I2S_MS_MASTER);
	regmap_update_bits(regmap, NAU8821_R1D_I2S_PCM_CTRL2,
		NAU8821_I2S_MS_MASK, NAU8821_I2S_MS_SLAVE);

	/* Not bypass de-bounce circuit */
	regmap_update_bits(regmap, NAU8821_R0D_JACK_DET_CTRL,
		NAU8821_JACK_DET_DB_BYPASS, 0);

	regmap_update_bits(regmap, NAU8821_R0F_INTERRUPT_MASK,
		NAU8821_IRQ_EJECT_EN, 0);
	regmap_update_bits(regmap, NAU8821_R12_INTERRUPT_DIS_CTRL,
		NAU8821_IRQ_EJECT_DIS, 0);
}

static irqreturn_t nau8821_interrupt(int irq, void *data)
{
	struct nau8821 *nau8821 = (struct nau8821 *)data;
	struct regmap *regmap = nau8821->regmap;
	int active_irq, clear_irq = 0, event = 0, event_mask = 0;

	if (regmap_read(regmap, NAU8821_R10_IRQ_STATUS, &active_irq)) {
		dev_err(nau8821->dev, "failed to read irq status\n");
		return IRQ_NONE;
	}

	dev_dbg(nau8821->dev, "IRQ %d\n", active_irq);

	if ((active_irq & NAU8821_JACK_EJECT_IRQ_MASK) ==
		NAU8821_JACK_EJECT_DETECTED) {
		regmap_update_bits(regmap, NAU8821_R71_ANALOG_ADC_1,
			NAU8821_MICDET_MASK, NAU8821_MICDET_DIS);
		nau8821_eject_jack(nau8821);
		event_mask |= SND_JACK_HEADSET;
		clear_irq = NAU8821_JACK_EJECT_IRQ_MASK;
	} else if (active_irq & NAU8821_KEY_SHORT_PRESS_IRQ) {
		event |= NAU8821_BUTTON;
		event_mask |= NAU8821_BUTTON;
		clear_irq = NAU8821_KEY_SHORT_PRESS_IRQ;
	} else if (active_irq & NAU8821_KEY_RELEASE_IRQ) {
		event_mask = NAU8821_BUTTON;
		clear_irq = NAU8821_KEY_RELEASE_IRQ;
	} else if ((active_irq & NAU8821_JACK_INSERT_IRQ_MASK) ==
		NAU8821_JACK_INSERT_DETECTED) {
		regmap_update_bits(regmap, NAU8821_R71_ANALOG_ADC_1,
			NAU8821_MICDET_MASK, NAU8821_MICDET_EN);
		if (nau8821_is_jack_inserted(regmap)) {
			/* detect microphone and jack type */
			cancel_work_sync(&nau8821->jdet_work);
			schedule_work(&nau8821->jdet_work);
			/* Turn off insertion interruption at manual mode */
			regmap_update_bits(regmap,
				NAU8821_R12_INTERRUPT_DIS_CTRL,
				NAU8821_IRQ_INSERT_DIS,
				NAU8821_IRQ_INSERT_DIS);
			regmap_update_bits(regmap,
				NAU8821_R0F_INTERRUPT_MASK,
				NAU8821_IRQ_INSERT_EN,
				NAU8821_IRQ_INSERT_EN);
			nau8821_setup_inserted_irq(nau8821);
		} else {
			dev_warn(nau8821->dev,
				"Inserted IRQ fired but not connected\n");
			nau8821_eject_jack(nau8821);
		}
	}

	if (!clear_irq)
		clear_irq = active_irq;
	/* clears the rightmost interruption */
	regmap_write(regmap, NAU8821_R11_INT_CLR_KEY_STATUS, clear_irq);

	if (event_mask)
		snd_soc_jack_report(nau8821->jack, event, event_mask);

	return IRQ_HANDLED;
}

static const struct regmap_config nau8821_regmap_config = {
	.val_bits = NAU8821_REG_DATA_LEN,
	.reg_bits = NAU8821_REG_ADDR_LEN,

	.max_register = NAU8821_REG_MAX,
	.readable_reg = nau8821_readable_reg,
	.writeable_reg = nau8821_writeable_reg,
	.volatile_reg = nau8821_volatile_reg,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = nau8821_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(nau8821_reg_defaults),
};

static int nau8821_component_probe(struct snd_soc_component *component)
{
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);

	nau8821->dapm = dapm;

	return 0;
}

/**
 * nau8821_calc_fll_param - Calculate FLL parameters.
 * @fll_in: external clock provided to codec.
 * @fs: sampling rate.
 * @fll_param: Pointer to structure of FLL parameters.
 *
 * Calculate FLL parameters to configure codec.
 *
 * Returns 0 for success or negative error code.
 */
static int nau8821_calc_fll_param(unsigned int fll_in,
	unsigned int fs, struct nau8821_fll *fll_param)
{
	u64 fvco, fvco_max;
	unsigned int fref, i, fvco_sel;

	/* Ensure the reference clock frequency (FREF) is <= 13.5MHz by
	 * dividing freq_in by 1, 2, 4, or 8 using FLL pre-scalar.
	 * FREF = freq_in / NAU8821_FLL_REF_DIV_MASK
	 */
	for (i = 0; i < ARRAY_SIZE(fll_pre_scalar); i++) {
		fref = fll_in >> fll_pre_scalar[i].param;
		if (fref <= NAU_FREF_MAX)
			break;
	}
	if (i == ARRAY_SIZE(fll_pre_scalar))
		return -EINVAL;
	fll_param->clk_ref_div = fll_pre_scalar[i].val;

	/* Choose the FLL ratio based on FREF */
	for (i = 0; i < ARRAY_SIZE(fll_ratio); i++) {
		if (fref >= fll_ratio[i].param)
			break;
	}
	if (i == ARRAY_SIZE(fll_ratio))
		return -EINVAL;
	fll_param->ratio = fll_ratio[i].val;

	/* Calculate the frequency of DCO (FDCO) given freq_out = 256 * Fs.
	 * FDCO must be within the 90MHz - 100MHz or the FFL cannot be
	 * guaranteed across the full range of operation.
	 * FDCO = freq_out * 2 * mclk_src_scaling
	 */
	fvco_max = 0;
	fvco_sel = ARRAY_SIZE(mclk_src_scaling);
	for (i = 0; i < ARRAY_SIZE(mclk_src_scaling); i++) {
		fvco = 256ULL * fs * 2 * mclk_src_scaling[i].param;
		if (fvco > NAU_FVCO_MIN && fvco < NAU_FVCO_MAX &&
			fvco_max < fvco) {
			fvco_max = fvco;
			fvco_sel = i;
		}
	}
	if (ARRAY_SIZE(mclk_src_scaling) == fvco_sel)
		return -EINVAL;
	fll_param->mclk_src = mclk_src_scaling[fvco_sel].val;

	/* Calculate the FLL 10-bit integer input and the FLL 24-bit fractional
	 * input based on FDCO, FREF and FLL ratio.
	 */
	fvco = div_u64(fvco_max << 24, fref * fll_param->ratio);
	fll_param->fll_int = (fvco >> 24) & 0x3ff;
	fll_param->fll_frac = fvco & 0xffffff;

	return 0;
}

static void nau8821_fll_apply(struct nau8821 *nau8821,
		struct nau8821_fll *fll_param)
{
	struct regmap *regmap = nau8821->regmap;

	regmap_update_bits(regmap, NAU8821_R03_CLK_DIVIDER,
		NAU8821_CLK_SRC_MASK | NAU8821_CLK_MCLK_SRC_MASK,
		NAU8821_CLK_SRC_MCLK | fll_param->mclk_src);
	/* Make DSP operate at high speed for better performance. */
	regmap_update_bits(regmap, NAU8821_R04_FLL1,
		NAU8821_FLL_RATIO_MASK | NAU8821_ICTRL_LATCH_MASK,
		fll_param->ratio | (0x6 << NAU8821_ICTRL_LATCH_SFT));
	/* FLL 24-bit fractional input */
	regmap_write(regmap, NAU8821_R0A_FLL7,
		(fll_param->fll_frac >> 16) & 0xff);
	regmap_write(regmap, NAU8821_R0B_FLL8, fll_param->fll_frac & 0xffff);
	/* FLL 10-bit integer input */
	regmap_update_bits(regmap, NAU8821_R06_FLL3,
		NAU8821_FLL_INTEGER_MASK, fll_param->fll_int);
	/* FLL pre-scaler */
	regmap_update_bits(regmap, NAU8821_R07_FLL4,
		NAU8821_HIGHBW_EN | NAU8821_FLL_REF_DIV_MASK,
		NAU8821_HIGHBW_EN |
		(fll_param->clk_ref_div << NAU8821_FLL_REF_DIV_SFT));
	/* select divided VCO input */
	regmap_update_bits(regmap, NAU8821_R08_FLL5,
		NAU8821_FLL_CLK_SW_MASK, NAU8821_FLL_CLK_SW_REF);
	/* Disable free-running mode */
	regmap_update_bits(regmap,
		NAU8821_R09_FLL6, NAU8821_DCO_EN, 0);
	if (fll_param->fll_frac) {
		/* set FLL loop filter enable and cutoff frequency at 500Khz */
		regmap_update_bits(regmap, NAU8821_R08_FLL5,
			NAU8821_FLL_PDB_DAC_EN | NAU8821_FLL_LOOP_FTR_EN |
			NAU8821_FLL_FTR_SW_MASK,
			NAU8821_FLL_PDB_DAC_EN | NAU8821_FLL_LOOP_FTR_EN |
			NAU8821_FLL_FTR_SW_FILTER);
		regmap_update_bits(regmap, NAU8821_R09_FLL6,
			NAU8821_SDM_EN | NAU8821_CUTOFF500,
			NAU8821_SDM_EN | NAU8821_CUTOFF500);
	} else {
		/* disable FLL loop filter and cutoff frequency */
		regmap_update_bits(regmap, NAU8821_R08_FLL5,
			NAU8821_FLL_PDB_DAC_EN | NAU8821_FLL_LOOP_FTR_EN |
			NAU8821_FLL_FTR_SW_MASK, NAU8821_FLL_FTR_SW_ACCU);
		regmap_update_bits(regmap, NAU8821_R09_FLL6,
			NAU8821_SDM_EN | NAU8821_CUTOFF500, 0);
	}
}

/**
 * nau8821_set_fll - FLL configuration of nau8821
 * @component:  codec component
 * @pll_id:  PLL requested
 * @source:  clock source
 * @freq_in:  frequency of input clock source
 * @freq_out:  must be 256*Fs in order to achieve the best performance
 *
 * The FLL function can select BCLK or MCLK as the input clock source.
 *
 * Returns 0 if the parameters have been applied successfully
 * or negative error code.
 */
static int nau8821_set_fll(struct snd_soc_component *component,
	int pll_id, int source, unsigned int freq_in, unsigned int freq_out)
{
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);
	struct nau8821_fll fll_set_param, *fll_param = &fll_set_param;
	int ret, fs;

	fs = freq_out >> 8;
	ret = nau8821_calc_fll_param(freq_in, fs, fll_param);
	if (ret) {
		dev_err(nau8821->dev,
			"Unsupported input clock %d to output clock %d\n",
			freq_in, freq_out);
		return ret;
	}
	dev_dbg(nau8821->dev,
		"mclk_src=%x ratio=%x fll_frac=%x fll_int=%x clk_ref_div=%x\n",
		fll_param->mclk_src, fll_param->ratio, fll_param->fll_frac,
		fll_param->fll_int, fll_param->clk_ref_div);

	nau8821_fll_apply(nau8821, fll_param);
	mdelay(2);
	regmap_update_bits(nau8821->regmap, NAU8821_R03_CLK_DIVIDER,
		NAU8821_CLK_SRC_MASK, NAU8821_CLK_SRC_VCO);

	return 0;
}

static void nau8821_configure_mclk_as_sysclk(struct regmap *regmap)
{
	regmap_update_bits(regmap, NAU8821_R03_CLK_DIVIDER,
		NAU8821_CLK_SRC_MASK, NAU8821_CLK_SRC_MCLK);
	regmap_update_bits(regmap, NAU8821_R09_FLL6,
		NAU8821_DCO_EN, 0);
	/* Make DSP operate as default setting for power saving. */
	regmap_update_bits(regmap, NAU8821_R04_FLL1,
		NAU8821_ICTRL_LATCH_MASK, 0);
}

static int nau8821_configure_sysclk(struct nau8821 *nau8821,
	int clk_id, unsigned int freq)
{
	struct regmap *regmap = nau8821->regmap;

	switch (clk_id) {
	case NAU8821_CLK_DIS:
		/* Clock provided externally and disable internal VCO clock */
		nau8821_configure_mclk_as_sysclk(regmap);
		break;
	case NAU8821_CLK_MCLK:
		nau8821_configure_mclk_as_sysclk(regmap);
		/* MCLK not changed by clock tree */
		regmap_update_bits(regmap, NAU8821_R03_CLK_DIVIDER,
			NAU8821_CLK_MCLK_SRC_MASK, 0);
		break;
	case NAU8821_CLK_INTERNAL:
		if (nau8821_is_jack_inserted(regmap)) {
			regmap_update_bits(regmap, NAU8821_R09_FLL6,
				NAU8821_DCO_EN, NAU8821_DCO_EN);
			regmap_update_bits(regmap, NAU8821_R03_CLK_DIVIDER,
				NAU8821_CLK_SRC_MASK, NAU8821_CLK_SRC_VCO);
			/* Decrease the VCO frequency and make DSP operate
			 * as default setting for power saving.
			 */
			regmap_update_bits(regmap, NAU8821_R03_CLK_DIVIDER,
				NAU8821_CLK_MCLK_SRC_MASK, 0xf);
			regmap_update_bits(regmap, NAU8821_R04_FLL1,
				NAU8821_ICTRL_LATCH_MASK |
				NAU8821_FLL_RATIO_MASK, 0x10);
			regmap_update_bits(regmap, NAU8821_R09_FLL6,
				NAU8821_SDM_EN, NAU8821_SDM_EN);
		}
		break;
	case NAU8821_CLK_FLL_MCLK:
		/* Higher FLL reference input frequency can only set lower
		 * gain error, such as 0000 for input reference from MCLK
		 * 12.288Mhz.
		 */
		regmap_update_bits(regmap, NAU8821_R06_FLL3,
			NAU8821_FLL_CLK_SRC_MASK | NAU8821_GAIN_ERR_MASK,
			NAU8821_FLL_CLK_SRC_MCLK | 0);
		break;
	case NAU8821_CLK_FLL_BLK:
		/* If FLL reference input is from low frequency source,
		 * higher error gain can apply such as 0xf which has
		 * the most sensitive gain error correction threshold,
		 * Therefore, FLL has the most accurate DCO to
		 * target frequency.
		 */
		regmap_update_bits(regmap, NAU8821_R06_FLL3,
			NAU8821_FLL_CLK_SRC_MASK | NAU8821_GAIN_ERR_MASK,
			NAU8821_FLL_CLK_SRC_BLK |
			(0xf << NAU8821_GAIN_ERR_SFT));
		break;
	case NAU8821_CLK_FLL_FS:
		/* If FLL reference input is from low frequency source,
		 * higher error gain can apply such as 0xf which has
		 * the most sensitive gain error correction threshold,
		 * Therefore, FLL has the most accurate DCO to
		 * target frequency.
		 */
		regmap_update_bits(regmap, NAU8821_R06_FLL3,
			NAU8821_FLL_CLK_SRC_MASK | NAU8821_GAIN_ERR_MASK,
			NAU8821_FLL_CLK_SRC_FS |
			(0xf << NAU8821_GAIN_ERR_SFT));
		break;
	default:
		dev_err(nau8821->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	nau8821->clk_id = clk_id;
	dev_dbg(nau8821->dev, "Sysclk is %dHz and clock id is %d\n", freq,
		nau8821->clk_id);

	return 0;
}

static int nau8821_set_sysclk(struct snd_soc_component *component, int clk_id,
	int source, unsigned int freq, int dir)
{
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);

	return nau8821_configure_sysclk(nau8821, clk_id, freq);
}

static int nau8821_resume_setup(struct nau8821 *nau8821)
{
	struct regmap *regmap = nau8821->regmap;

	/* Close clock when jack type detection at manual mode */
	nau8821_configure_sysclk(nau8821, NAU8821_CLK_DIS, 0);
	if (nau8821->irq) {
		/* Clear all interruption status */
		nau8821_int_status_clear_all(regmap);

		/* Enable both insertion and ejection interruptions, and then
		 * bypass de-bounce circuit.
		 */
		regmap_update_bits(regmap, NAU8821_R0F_INTERRUPT_MASK,
			NAU8821_IRQ_EJECT_EN | NAU8821_IRQ_INSERT_EN, 0);
		regmap_update_bits(regmap, NAU8821_R0D_JACK_DET_CTRL,
			NAU8821_JACK_DET_DB_BYPASS,
			NAU8821_JACK_DET_DB_BYPASS);
		regmap_update_bits(regmap, NAU8821_R12_INTERRUPT_DIS_CTRL,
			NAU8821_IRQ_INSERT_DIS | NAU8821_IRQ_EJECT_DIS, 0);
	}

	return 0;
}

static int nau8821_set_bias_level(struct snd_soc_component *component,
		enum snd_soc_bias_level level)
{
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = nau8821->regmap;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		/* Setup codec configuration after resume */
		if (snd_soc_component_get_bias_level(component) ==
			SND_SOC_BIAS_OFF)
			nau8821_resume_setup(nau8821);
		break;

	case SND_SOC_BIAS_OFF:
		/* HPL/HPR short to ground */
		regmap_update_bits(regmap, NAU8821_R0D_JACK_DET_CTRL,
			NAU8821_SPKR_DWN1R | NAU8821_SPKR_DWN1L, 0);
		if (nau8821->irq) {
			/* Reset the configuration of jack type for detection.
			 * Detach 2kOhm Resistors from MICBIAS to MICGND1/2.
			 */
			regmap_update_bits(regmap, NAU8821_R74_MIC_BIAS,
				NAU8821_MICBIAS_JKR2, 0);
			/* Turn off all interruptions before system shutdown.
			 * Keep theinterruption quiet before resume
			 * setup completes.
			 */
			regmap_write(regmap,
				NAU8821_R12_INTERRUPT_DIS_CTRL, 0xffff);
			regmap_update_bits(regmap, NAU8821_R0F_INTERRUPT_MASK,
				NAU8821_IRQ_EJECT_EN | NAU8821_IRQ_INSERT_EN,
				NAU8821_IRQ_EJECT_EN | NAU8821_IRQ_INSERT_EN);
		}
		break;
	default:
		break;
	}

	return 0;
}

static int __maybe_unused nau8821_suspend(struct snd_soc_component *component)
{
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);

	if (nau8821->irq)
		disable_irq(nau8821->irq);
	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_OFF);
	/* Power down codec power; don't support button wakeup */
	snd_soc_component_disable_pin(component, "MICBIAS");
	snd_soc_dapm_sync(nau8821->dapm);
	regcache_cache_only(nau8821->regmap, true);
	regcache_mark_dirty(nau8821->regmap);

	return 0;
}

static int __maybe_unused nau8821_resume(struct snd_soc_component *component)
{
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(nau8821->regmap, false);
	regcache_sync(nau8821->regmap);
	if (nau8821->irq)
		enable_irq(nau8821->irq);

	return 0;
}

static const struct snd_soc_component_driver nau8821_component_driver = {
	.probe			= nau8821_component_probe,
	.set_sysclk		= nau8821_set_sysclk,
	.set_pll		= nau8821_set_fll,
	.set_bias_level		= nau8821_set_bias_level,
	.suspend		= nau8821_suspend,
	.resume			= nau8821_resume,
	.controls		= nau8821_controls,
	.num_controls		= ARRAY_SIZE(nau8821_controls),
	.dapm_widgets		= nau8821_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(nau8821_dapm_widgets),
	.dapm_routes		= nau8821_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(nau8821_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

/**
 * nau8821_enable_jack_detect - Specify a jack for event reporting
 *
 * @component:  component to register the jack with
 * @jack: jack to use to report headset and button events on
 *
 * After this function has been called the headset insert/remove and button
 * events will be routed to the given jack.  Jack can be null to stop
 * reporting.
 */
int nau8821_enable_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *jack)
{
	struct nau8821 *nau8821 = snd_soc_component_get_drvdata(component);
	int ret;

	nau8821->jack = jack;
	/* Initiate jack detection work queue */
	INIT_WORK(&nau8821->jdet_work, nau8821_jdet_work);
	ret = devm_request_threaded_irq(nau8821->dev, nau8821->irq, NULL,
		nau8821_interrupt, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
		"nau8821", nau8821);
	if (ret) {
		dev_err(nau8821->dev, "Cannot request irq %d (%d)\n",
			nau8821->irq, ret);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nau8821_enable_jack_detect);

static void nau8821_reset_chip(struct regmap *regmap)
{
	regmap_write(regmap, NAU8821_R00_RESET, 0xffff);
	regmap_write(regmap, NAU8821_R00_RESET, 0xffff);
}

static void nau8821_print_device_properties(struct nau8821 *nau8821)
{
	struct device *dev = nau8821->dev;

	dev_dbg(dev, "jkdet-enable:         %d\n", nau8821->jkdet_enable);
	dev_dbg(dev, "jkdet-pull-enable:    %d\n", nau8821->jkdet_pull_enable);
	dev_dbg(dev, "jkdet-pull-up:        %d\n", nau8821->jkdet_pull_up);
	dev_dbg(dev, "jkdet-polarity:       %d\n", nau8821->jkdet_polarity);
	dev_dbg(dev, "micbias-voltage:      %d\n", nau8821->micbias_voltage);
	dev_dbg(dev, "vref-impedance:       %d\n", nau8821->vref_impedance);
	dev_dbg(dev, "jack-insert-debounce: %d\n",
		nau8821->jack_insert_debounce);
	dev_dbg(dev, "jack-eject-debounce:  %d\n",
		nau8821->jack_eject_debounce);
	dev_dbg(dev, "dmic-clk-threshold:       %d\n",
		nau8821->dmic_clk_threshold);
	dev_dbg(dev, "key_enable:       %d\n", nau8821->key_enable);
}

static int nau8821_read_device_properties(struct device *dev,
	struct nau8821 *nau8821)
{
	int ret;

	nau8821->jkdet_enable = device_property_read_bool(dev,
		"nuvoton,jkdet-enable");
	nau8821->jkdet_pull_enable = device_property_read_bool(dev,
		"nuvoton,jkdet-pull-enable");
	nau8821->jkdet_pull_up = device_property_read_bool(dev,
		"nuvoton,jkdet-pull-up");
	nau8821->key_enable = device_property_read_bool(dev,
		"nuvoton,key-enable");
	nau8821->left_input_single_end = device_property_read_bool(dev,
		"nuvoton,left-input-single-end");
	ret = device_property_read_u32(dev, "nuvoton,jkdet-polarity",
		&nau8821->jkdet_polarity);
	if (ret)
		nau8821->jkdet_polarity = 1;
	ret = device_property_read_u32(dev, "nuvoton,micbias-voltage",
		&nau8821->micbias_voltage);
	if (ret)
		nau8821->micbias_voltage = 6;
	ret = device_property_read_u32(dev, "nuvoton,vref-impedance",
		&nau8821->vref_impedance);
	if (ret)
		nau8821->vref_impedance = 2;
	ret = device_property_read_u32(dev, "nuvoton,jack-insert-debounce",
		&nau8821->jack_insert_debounce);
	if (ret)
		nau8821->jack_insert_debounce = 7;
	ret = device_property_read_u32(dev, "nuvoton,jack-eject-debounce",
		&nau8821->jack_eject_debounce);
	if (ret)
		nau8821->jack_eject_debounce = 0;
	ret = device_property_read_u32(dev, "nuvoton,dmic-clk-threshold",
		&nau8821->dmic_clk_threshold);
	if (ret)
		nau8821->dmic_clk_threshold = 3072000;

	return 0;
}

static void nau8821_init_regs(struct nau8821 *nau8821)
{
	struct regmap *regmap = nau8821->regmap;

	/* Enable Bias/Vmid */
	regmap_update_bits(regmap, NAU8821_R66_BIAS_ADJ,
		NAU8821_BIAS_VMID, NAU8821_BIAS_VMID);
	regmap_update_bits(regmap, NAU8821_R76_BOOST,
		NAU8821_GLOBAL_BIAS_EN, NAU8821_GLOBAL_BIAS_EN);
	/* VMID Tieoff setting and enable TESTDAC.
	 * This sets the analog DAC inputs to a '0' input signal to avoid
	 * any glitches due to power up transients in both the analog and
	 * digital DAC circuit.
	 */
	regmap_update_bits(regmap, NAU8821_R66_BIAS_ADJ,
		NAU8821_BIAS_VMID_SEL_MASK | NAU8821_BIAS_TESTDAC_EN,
		(nau8821->vref_impedance << NAU8821_BIAS_VMID_SEL_SFT) |
		NAU8821_BIAS_TESTDAC_EN);
	/* Disable short Frame Sync detection logic */
	regmap_update_bits(regmap, NAU8821_R1E_LEFT_TIME_SLOT,
		NAU8821_DIS_FS_SHORT_DET, NAU8821_DIS_FS_SHORT_DET);
	/* Disable Boost Driver, Automatic Short circuit protection enable */
	regmap_update_bits(regmap, NAU8821_R76_BOOST,
		NAU8821_PRECHARGE_DIS | NAU8821_HP_BOOST_DIS |
		NAU8821_HP_BOOST_G_DIS | NAU8821_SHORT_SHUTDOWN_EN,
		NAU8821_PRECHARGE_DIS | NAU8821_HP_BOOST_DIS |
		NAU8821_HP_BOOST_G_DIS | NAU8821_SHORT_SHUTDOWN_EN);
	/* Class G timer 64ms */
	regmap_update_bits(regmap, NAU8821_R4B_CLASSG_CTRL,
		NAU8821_CLASSG_TIMER_MASK,
		0x20 << NAU8821_CLASSG_TIMER_SFT);
	/* Class AB bias current to 2x, DAC Capacitor enable MSB/LSB */
	regmap_update_bits(regmap, NAU8821_R6A_ANALOG_CONTROL_2,
		NAU8821_HP_NON_CLASSG_CURRENT_2xADJ |
		NAU8821_DAC_CAPACITOR_MSB | NAU8821_DAC_CAPACITOR_LSB,
		NAU8821_HP_NON_CLASSG_CURRENT_2xADJ |
		NAU8821_DAC_CAPACITOR_MSB | NAU8821_DAC_CAPACITOR_LSB);
	/* Disable DACR/L power */
	regmap_update_bits(regmap, NAU8821_R80_CHARGE_PUMP,
		NAU8821_POWER_DOWN_DACR | NAU8821_POWER_DOWN_DACL, 0);
	/* DAC clock delay 2ns, VREF */
	regmap_update_bits(regmap, NAU8821_R73_RDAC,
		NAU8821_DAC_CLK_DELAY_MASK | NAU8821_DAC_VREF_MASK,
		(0x2 << NAU8821_DAC_CLK_DELAY_SFT) |
		(0x3 << NAU8821_DAC_VREF_SFT));

	regmap_update_bits(regmap, NAU8821_R74_MIC_BIAS,
		NAU8821_MICBIAS_VOLTAGE_MASK, nau8821->micbias_voltage);
	/* Default oversampling/decimations settings are unusable
	 * (audible hiss). Set it to something better.
	 */
	regmap_update_bits(regmap, NAU8821_R2B_ADC_RATE,
		NAU8821_ADC_SYNC_DOWN_MASK, NAU8821_ADC_SYNC_DOWN_64);
	regmap_update_bits(regmap, NAU8821_R2C_DAC_CTRL1,
		NAU8821_DAC_OVERSAMPLE_MASK, NAU8821_DAC_OVERSAMPLE_64);
	if (nau8821->left_input_single_end) {
		regmap_update_bits(regmap, NAU8821_R6B_PGA_MUTE,
			NAU8821_MUTE_MICNL_EN, NAU8821_MUTE_MICNL_EN);
		regmap_update_bits(regmap, NAU8821_R74_MIC_BIAS,
			NAU8821_MICBIAS_LOWNOISE_EN, NAU8821_MICBIAS_LOWNOISE_EN);
	}
}

static int nau8821_setup_irq(struct nau8821 *nau8821)
{
	struct regmap *regmap = nau8821->regmap;

	/* Jack detection */
	regmap_update_bits(regmap, NAU8821_R1A_GPIO12_CTRL,
		NAU8821_JKDET_OUTPUT_EN,
		nau8821->jkdet_enable ? 0 : NAU8821_JKDET_OUTPUT_EN);
	regmap_update_bits(regmap, NAU8821_R1A_GPIO12_CTRL,
		NAU8821_JKDET_PULL_EN,
		nau8821->jkdet_pull_enable ? 0 : NAU8821_JKDET_PULL_EN);
	regmap_update_bits(regmap, NAU8821_R1A_GPIO12_CTRL,
		NAU8821_JKDET_PULL_UP,
		nau8821->jkdet_pull_up ? NAU8821_JKDET_PULL_UP : 0);
	regmap_update_bits(regmap, NAU8821_R0D_JACK_DET_CTRL,
		NAU8821_JACK_POLARITY,
		/* jkdet_polarity - 1  is for active-low */
		nau8821->jkdet_polarity ? 0 : NAU8821_JACK_POLARITY);
	regmap_update_bits(regmap, NAU8821_R0D_JACK_DET_CTRL,
		NAU8821_JACK_INSERT_DEBOUNCE_MASK,
		nau8821->jack_insert_debounce <<
		NAU8821_JACK_INSERT_DEBOUNCE_SFT);
	regmap_update_bits(regmap, NAU8821_R0D_JACK_DET_CTRL,
		NAU8821_JACK_EJECT_DEBOUNCE_MASK,
		nau8821->jack_eject_debounce <<
		NAU8821_JACK_EJECT_DEBOUNCE_SFT);
	/* Pull up IRQ pin */
	regmap_update_bits(regmap, NAU8821_R0F_INTERRUPT_MASK,
		NAU8821_IRQ_PIN_PULL_UP | NAU8821_IRQ_PIN_PULL_EN |
		NAU8821_IRQ_OUTPUT_EN, NAU8821_IRQ_PIN_PULL_UP |
		NAU8821_IRQ_PIN_PULL_EN | NAU8821_IRQ_OUTPUT_EN);
	/* Disable interruption before codec initiation done */
	/* Mask unneeded IRQs: 1 - disable, 0 - enable */
	regmap_update_bits(regmap, NAU8821_R0F_INTERRUPT_MASK, 0x3f5, 0x3f5);

	return 0;
}

/* Please keep this list alphabetically sorted */
static const struct dmi_system_id nau8821_quirk_table[] = {
	{
		/* Positivo CW14Q01P-V2 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Positivo Tecnologia SA"),
			DMI_MATCH(DMI_BOARD_NAME, "CW14Q01P-V2"),
		},
		.driver_data = (void *)(NAU8821_JD_ACTIVE_HIGH),
	},
	{}
};

static void nau8821_check_quirks(void)
{
	const struct dmi_system_id *dmi_id;

	if (quirk_override != -1) {
		nau8821_quirk = quirk_override;
		return;
	}

	dmi_id = dmi_first_match(nau8821_quirk_table);
	if (dmi_id)
		nau8821_quirk = (unsigned long)dmi_id->driver_data;
}

static int nau8821_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct nau8821 *nau8821 = dev_get_platdata(&i2c->dev);
	int ret, value;

	if (!nau8821) {
		nau8821 = devm_kzalloc(dev, sizeof(*nau8821), GFP_KERNEL);
		if (!nau8821)
			return -ENOMEM;
		nau8821_read_device_properties(dev, nau8821);
	}
	i2c_set_clientdata(i2c, nau8821);

	nau8821->regmap = devm_regmap_init_i2c(i2c, &nau8821_regmap_config);
	if (IS_ERR(nau8821->regmap))
		return PTR_ERR(nau8821->regmap);

	nau8821->dev = dev;
	nau8821->irq = i2c->irq;

	nau8821_check_quirks();

	if (nau8821_quirk & NAU8821_JD_ACTIVE_HIGH)
		nau8821->jkdet_polarity = 0;

	nau8821_print_device_properties(nau8821);

	nau8821_reset_chip(nau8821->regmap);
	ret = regmap_read(nau8821->regmap, NAU8821_R58_I2C_DEVICE_ID, &value);
	if (ret) {
		dev_err(dev, "Failed to read device id (%d)\n", ret);
		return ret;
	}
	nau8821_init_regs(nau8821);

	if (i2c->irq)
		nau8821_setup_irq(nau8821);

	ret = devm_snd_soc_register_component(&i2c->dev,
		&nau8821_component_driver, &nau8821_dai, 1);

	return ret;
}

static const struct i2c_device_id nau8821_i2c_ids[] = {
	{ "nau8821", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nau8821_i2c_ids);

#ifdef CONFIG_OF
static const struct of_device_id nau8821_of_ids[] = {
	{ .compatible = "nuvoton,nau8821", },
	{}
};
MODULE_DEVICE_TABLE(of, nau8821_of_ids);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id nau8821_acpi_match[] = {
	{ "NVTN2020", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, nau8821_acpi_match);
#endif

static struct i2c_driver nau8821_driver = {
	.driver = {
		.name = "nau8821",
		.of_match_table = of_match_ptr(nau8821_of_ids),
		.acpi_match_table = ACPI_PTR(nau8821_acpi_match),
	},
	.probe = nau8821_i2c_probe,
	.id_table = nau8821_i2c_ids,
};
module_i2c_driver(nau8821_driver);

MODULE_DESCRIPTION("ASoC nau8821 driver");
MODULE_AUTHOR("John Hsu <kchsu0@nuvoton.com>");
MODULE_AUTHOR("Seven Lee <wtli@nuvoton.com>");
MODULE_LICENSE("GPL");
