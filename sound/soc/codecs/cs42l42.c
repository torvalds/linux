// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs42l42.c -- CS42L42 ALSA SoC audio driver
 *
 * Copyright 2016 Cirrus Logic, Inc.
 *
 * Author: James Schulman <james.schulman@cirrus.com>
 * Author: Brian Austin <brian.austin@cirrus.com>
 * Author: Michael White <michael.white@cirrus.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <dt-bindings/sound/cs42l42.h>

#include "cs42l42.h"
#include "cirrus_legacy.h"

static const char * const cs42l42_supply_names[] = {
	"VA",
	"VP",
	"VCP",
	"VD_FILT",
	"VL",
};

static const struct reg_default cs42l42_reg_defaults[] = {
	{ CS42L42_FRZ_CTL,			0x00 },
	{ CS42L42_SRC_CTL,			0x10 },
	{ CS42L42_MCLK_CTL,			0x02 },
	{ CS42L42_SFTRAMP_RATE,			0xA4 },
	{ CS42L42_SLOW_START_ENABLE,		0x70 },
	{ CS42L42_I2C_DEBOUNCE,			0x88 },
	{ CS42L42_I2C_STRETCH,			0x03 },
	{ CS42L42_I2C_TIMEOUT,			0xB7 },
	{ CS42L42_PWR_CTL1,			0xFF },
	{ CS42L42_PWR_CTL2,			0x84 },
	{ CS42L42_PWR_CTL3,			0x20 },
	{ CS42L42_RSENSE_CTL1,			0x40 },
	{ CS42L42_RSENSE_CTL2,			0x00 },
	{ CS42L42_OSC_SWITCH,			0x00 },
	{ CS42L42_RSENSE_CTL3,			0x1B },
	{ CS42L42_TSENSE_CTL,			0x1B },
	{ CS42L42_TSRS_INT_DISABLE,		0x00 },
	{ CS42L42_HSDET_CTL1,			0x77 },
	{ CS42L42_HSDET_CTL2,			0x00 },
	{ CS42L42_HS_SWITCH_CTL,		0xF3 },
	{ CS42L42_HS_CLAMP_DISABLE,		0x00 },
	{ CS42L42_MCLK_SRC_SEL,			0x00 },
	{ CS42L42_SPDIF_CLK_CFG,		0x00 },
	{ CS42L42_FSYNC_PW_LOWER,		0x00 },
	{ CS42L42_FSYNC_PW_UPPER,		0x00 },
	{ CS42L42_FSYNC_P_LOWER,		0xF9 },
	{ CS42L42_FSYNC_P_UPPER,		0x00 },
	{ CS42L42_ASP_CLK_CFG,			0x00 },
	{ CS42L42_ASP_FRM_CFG,			0x10 },
	{ CS42L42_FS_RATE_EN,			0x00 },
	{ CS42L42_IN_ASRC_CLK,			0x00 },
	{ CS42L42_OUT_ASRC_CLK,			0x00 },
	{ CS42L42_PLL_DIV_CFG1,			0x00 },
	{ CS42L42_ADC_OVFL_INT_MASK,		0x01 },
	{ CS42L42_MIXER_INT_MASK,		0x0F },
	{ CS42L42_SRC_INT_MASK,			0x0F },
	{ CS42L42_ASP_RX_INT_MASK,		0x1F },
	{ CS42L42_ASP_TX_INT_MASK,		0x0F },
	{ CS42L42_CODEC_INT_MASK,		0x03 },
	{ CS42L42_SRCPL_INT_MASK,		0x7F },
	{ CS42L42_VPMON_INT_MASK,		0x01 },
	{ CS42L42_PLL_LOCK_INT_MASK,		0x01 },
	{ CS42L42_TSRS_PLUG_INT_MASK,		0x0F },
	{ CS42L42_PLL_CTL1,			0x00 },
	{ CS42L42_PLL_DIV_FRAC0,		0x00 },
	{ CS42L42_PLL_DIV_FRAC1,		0x00 },
	{ CS42L42_PLL_DIV_FRAC2,		0x00 },
	{ CS42L42_PLL_DIV_INT,			0x40 },
	{ CS42L42_PLL_CTL3,			0x10 },
	{ CS42L42_PLL_CAL_RATIO,		0x80 },
	{ CS42L42_PLL_CTL4,			0x03 },
	{ CS42L42_LOAD_DET_EN,			0x00 },
	{ CS42L42_HSBIAS_SC_AUTOCTL,		0x03 },
	{ CS42L42_WAKE_CTL,			0xC0 },
	{ CS42L42_ADC_DISABLE_MUTE,		0x00 },
	{ CS42L42_TIPSENSE_CTL,			0x02 },
	{ CS42L42_MISC_DET_CTL,			0x03 },
	{ CS42L42_MIC_DET_CTL1,			0x1F },
	{ CS42L42_MIC_DET_CTL2,			0x2F },
	{ CS42L42_DET_INT1_MASK,		0xE0 },
	{ CS42L42_DET_INT2_MASK,		0xFF },
	{ CS42L42_HS_BIAS_CTL,			0xC2 },
	{ CS42L42_ADC_CTL,			0x00 },
	{ CS42L42_ADC_VOLUME,			0x00 },
	{ CS42L42_ADC_WNF_HPF_CTL,		0x71 },
	{ CS42L42_DAC_CTL1,			0x00 },
	{ CS42L42_DAC_CTL2,			0x02 },
	{ CS42L42_HP_CTL,			0x0D },
	{ CS42L42_CLASSH_CTL,			0x07 },
	{ CS42L42_MIXER_CHA_VOL,		0x3F },
	{ CS42L42_MIXER_ADC_VOL,		0x3F },
	{ CS42L42_MIXER_CHB_VOL,		0x3F },
	{ CS42L42_EQ_COEF_IN0,			0x00 },
	{ CS42L42_EQ_COEF_IN1,			0x00 },
	{ CS42L42_EQ_COEF_IN2,			0x00 },
	{ CS42L42_EQ_COEF_IN3,			0x00 },
	{ CS42L42_EQ_COEF_RW,			0x00 },
	{ CS42L42_EQ_COEF_OUT0,			0x00 },
	{ CS42L42_EQ_COEF_OUT1,			0x00 },
	{ CS42L42_EQ_COEF_OUT2,			0x00 },
	{ CS42L42_EQ_COEF_OUT3,			0x00 },
	{ CS42L42_EQ_INIT_STAT,			0x00 },
	{ CS42L42_EQ_START_FILT,		0x00 },
	{ CS42L42_EQ_MUTE_CTL,			0x00 },
	{ CS42L42_SP_RX_CH_SEL,			0x04 },
	{ CS42L42_SP_RX_ISOC_CTL,		0x04 },
	{ CS42L42_SP_RX_FS,			0x8C },
	{ CS42l42_SPDIF_CH_SEL,			0x0E },
	{ CS42L42_SP_TX_ISOC_CTL,		0x04 },
	{ CS42L42_SP_TX_FS,			0xCC },
	{ CS42L42_SPDIF_SW_CTL1,		0x3F },
	{ CS42L42_SRC_SDIN_FS,			0x40 },
	{ CS42L42_SRC_SDOUT_FS,			0x40 },
	{ CS42L42_SPDIF_CTL1,			0x01 },
	{ CS42L42_SPDIF_CTL2,			0x00 },
	{ CS42L42_SPDIF_CTL3,			0x00 },
	{ CS42L42_SPDIF_CTL4,			0x42 },
	{ CS42L42_ASP_TX_SZ_EN,			0x00 },
	{ CS42L42_ASP_TX_CH_EN,			0x00 },
	{ CS42L42_ASP_TX_CH_AP_RES,		0x0F },
	{ CS42L42_ASP_TX_CH1_BIT_MSB,		0x00 },
	{ CS42L42_ASP_TX_CH1_BIT_LSB,		0x00 },
	{ CS42L42_ASP_TX_HIZ_DLY_CFG,		0x00 },
	{ CS42L42_ASP_TX_CH2_BIT_MSB,		0x00 },
	{ CS42L42_ASP_TX_CH2_BIT_LSB,		0x00 },
	{ CS42L42_ASP_RX_DAI0_EN,		0x00 },
	{ CS42L42_ASP_RX_DAI0_CH1_AP_RES,	0x03 },
	{ CS42L42_ASP_RX_DAI0_CH1_BIT_MSB,	0x00 },
	{ CS42L42_ASP_RX_DAI0_CH1_BIT_LSB,	0x00 },
	{ CS42L42_ASP_RX_DAI0_CH2_AP_RES,	0x03 },
	{ CS42L42_ASP_RX_DAI0_CH2_BIT_MSB,	0x00 },
	{ CS42L42_ASP_RX_DAI0_CH2_BIT_LSB,	0x00 },
	{ CS42L42_ASP_RX_DAI0_CH3_AP_RES,	0x03 },
	{ CS42L42_ASP_RX_DAI0_CH3_BIT_MSB,	0x00 },
	{ CS42L42_ASP_RX_DAI0_CH3_BIT_LSB,	0x00 },
	{ CS42L42_ASP_RX_DAI0_CH4_AP_RES,	0x03 },
	{ CS42L42_ASP_RX_DAI0_CH4_BIT_MSB,	0x00 },
	{ CS42L42_ASP_RX_DAI0_CH4_BIT_LSB,	0x00 },
	{ CS42L42_ASP_RX_DAI1_CH1_AP_RES,	0x03 },
	{ CS42L42_ASP_RX_DAI1_CH1_BIT_MSB,	0x00 },
	{ CS42L42_ASP_RX_DAI1_CH1_BIT_LSB,	0x00 },
	{ CS42L42_ASP_RX_DAI1_CH2_AP_RES,	0x03 },
	{ CS42L42_ASP_RX_DAI1_CH2_BIT_MSB,	0x00 },
	{ CS42L42_ASP_RX_DAI1_CH2_BIT_LSB,	0x00 },
};

bool cs42l42_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L42_PAGE_REGISTER:
	case CS42L42_DEVID_AB:
	case CS42L42_DEVID_CD:
	case CS42L42_DEVID_E:
	case CS42L42_FABID:
	case CS42L42_REVID:
	case CS42L42_FRZ_CTL:
	case CS42L42_SRC_CTL:
	case CS42L42_MCLK_STATUS:
	case CS42L42_MCLK_CTL:
	case CS42L42_SFTRAMP_RATE:
	case CS42L42_SLOW_START_ENABLE:
	case CS42L42_I2C_DEBOUNCE:
	case CS42L42_I2C_STRETCH:
	case CS42L42_I2C_TIMEOUT:
	case CS42L42_PWR_CTL1:
	case CS42L42_PWR_CTL2:
	case CS42L42_PWR_CTL3:
	case CS42L42_RSENSE_CTL1:
	case CS42L42_RSENSE_CTL2:
	case CS42L42_OSC_SWITCH:
	case CS42L42_OSC_SWITCH_STATUS:
	case CS42L42_RSENSE_CTL3:
	case CS42L42_TSENSE_CTL:
	case CS42L42_TSRS_INT_DISABLE:
	case CS42L42_TRSENSE_STATUS:
	case CS42L42_HSDET_CTL1:
	case CS42L42_HSDET_CTL2:
	case CS42L42_HS_SWITCH_CTL:
	case CS42L42_HS_DET_STATUS:
	case CS42L42_HS_CLAMP_DISABLE:
	case CS42L42_MCLK_SRC_SEL:
	case CS42L42_SPDIF_CLK_CFG:
	case CS42L42_FSYNC_PW_LOWER:
	case CS42L42_FSYNC_PW_UPPER:
	case CS42L42_FSYNC_P_LOWER:
	case CS42L42_FSYNC_P_UPPER:
	case CS42L42_ASP_CLK_CFG:
	case CS42L42_ASP_FRM_CFG:
	case CS42L42_FS_RATE_EN:
	case CS42L42_IN_ASRC_CLK:
	case CS42L42_OUT_ASRC_CLK:
	case CS42L42_PLL_DIV_CFG1:
	case CS42L42_ADC_OVFL_STATUS:
	case CS42L42_MIXER_STATUS:
	case CS42L42_SRC_STATUS:
	case CS42L42_ASP_RX_STATUS:
	case CS42L42_ASP_TX_STATUS:
	case CS42L42_CODEC_STATUS:
	case CS42L42_DET_INT_STATUS1:
	case CS42L42_DET_INT_STATUS2:
	case CS42L42_SRCPL_INT_STATUS:
	case CS42L42_VPMON_STATUS:
	case CS42L42_PLL_LOCK_STATUS:
	case CS42L42_TSRS_PLUG_STATUS:
	case CS42L42_ADC_OVFL_INT_MASK:
	case CS42L42_MIXER_INT_MASK:
	case CS42L42_SRC_INT_MASK:
	case CS42L42_ASP_RX_INT_MASK:
	case CS42L42_ASP_TX_INT_MASK:
	case CS42L42_CODEC_INT_MASK:
	case CS42L42_SRCPL_INT_MASK:
	case CS42L42_VPMON_INT_MASK:
	case CS42L42_PLL_LOCK_INT_MASK:
	case CS42L42_TSRS_PLUG_INT_MASK:
	case CS42L42_PLL_CTL1:
	case CS42L42_PLL_DIV_FRAC0:
	case CS42L42_PLL_DIV_FRAC1:
	case CS42L42_PLL_DIV_FRAC2:
	case CS42L42_PLL_DIV_INT:
	case CS42L42_PLL_CTL3:
	case CS42L42_PLL_CAL_RATIO:
	case CS42L42_PLL_CTL4:
	case CS42L42_LOAD_DET_RCSTAT:
	case CS42L42_LOAD_DET_DONE:
	case CS42L42_LOAD_DET_EN:
	case CS42L42_HSBIAS_SC_AUTOCTL:
	case CS42L42_WAKE_CTL:
	case CS42L42_ADC_DISABLE_MUTE:
	case CS42L42_TIPSENSE_CTL:
	case CS42L42_MISC_DET_CTL:
	case CS42L42_MIC_DET_CTL1:
	case CS42L42_MIC_DET_CTL2:
	case CS42L42_DET_STATUS1:
	case CS42L42_DET_STATUS2:
	case CS42L42_DET_INT1_MASK:
	case CS42L42_DET_INT2_MASK:
	case CS42L42_HS_BIAS_CTL:
	case CS42L42_ADC_CTL:
	case CS42L42_ADC_VOLUME:
	case CS42L42_ADC_WNF_HPF_CTL:
	case CS42L42_DAC_CTL1:
	case CS42L42_DAC_CTL2:
	case CS42L42_HP_CTL:
	case CS42L42_CLASSH_CTL:
	case CS42L42_MIXER_CHA_VOL:
	case CS42L42_MIXER_ADC_VOL:
	case CS42L42_MIXER_CHB_VOL:
	case CS42L42_EQ_COEF_IN0:
	case CS42L42_EQ_COEF_IN1:
	case CS42L42_EQ_COEF_IN2:
	case CS42L42_EQ_COEF_IN3:
	case CS42L42_EQ_COEF_RW:
	case CS42L42_EQ_COEF_OUT0:
	case CS42L42_EQ_COEF_OUT1:
	case CS42L42_EQ_COEF_OUT2:
	case CS42L42_EQ_COEF_OUT3:
	case CS42L42_EQ_INIT_STAT:
	case CS42L42_EQ_START_FILT:
	case CS42L42_EQ_MUTE_CTL:
	case CS42L42_SP_RX_CH_SEL:
	case CS42L42_SP_RX_ISOC_CTL:
	case CS42L42_SP_RX_FS:
	case CS42l42_SPDIF_CH_SEL:
	case CS42L42_SP_TX_ISOC_CTL:
	case CS42L42_SP_TX_FS:
	case CS42L42_SPDIF_SW_CTL1:
	case CS42L42_SRC_SDIN_FS:
	case CS42L42_SRC_SDOUT_FS:
	case CS42L42_SOFT_RESET_REBOOT:
	case CS42L42_SPDIF_CTL1:
	case CS42L42_SPDIF_CTL2:
	case CS42L42_SPDIF_CTL3:
	case CS42L42_SPDIF_CTL4:
	case CS42L42_ASP_TX_SZ_EN:
	case CS42L42_ASP_TX_CH_EN:
	case CS42L42_ASP_TX_CH_AP_RES:
	case CS42L42_ASP_TX_CH1_BIT_MSB:
	case CS42L42_ASP_TX_CH1_BIT_LSB:
	case CS42L42_ASP_TX_HIZ_DLY_CFG:
	case CS42L42_ASP_TX_CH2_BIT_MSB:
	case CS42L42_ASP_TX_CH2_BIT_LSB:
	case CS42L42_ASP_RX_DAI0_EN:
	case CS42L42_ASP_RX_DAI0_CH1_AP_RES:
	case CS42L42_ASP_RX_DAI0_CH1_BIT_MSB:
	case CS42L42_ASP_RX_DAI0_CH1_BIT_LSB:
	case CS42L42_ASP_RX_DAI0_CH2_AP_RES:
	case CS42L42_ASP_RX_DAI0_CH2_BIT_MSB:
	case CS42L42_ASP_RX_DAI0_CH2_BIT_LSB:
	case CS42L42_ASP_RX_DAI0_CH3_AP_RES:
	case CS42L42_ASP_RX_DAI0_CH3_BIT_MSB:
	case CS42L42_ASP_RX_DAI0_CH3_BIT_LSB:
	case CS42L42_ASP_RX_DAI0_CH4_AP_RES:
	case CS42L42_ASP_RX_DAI0_CH4_BIT_MSB:
	case CS42L42_ASP_RX_DAI0_CH4_BIT_LSB:
	case CS42L42_ASP_RX_DAI1_CH1_AP_RES:
	case CS42L42_ASP_RX_DAI1_CH1_BIT_MSB:
	case CS42L42_ASP_RX_DAI1_CH1_BIT_LSB:
	case CS42L42_ASP_RX_DAI1_CH2_AP_RES:
	case CS42L42_ASP_RX_DAI1_CH2_BIT_MSB:
	case CS42L42_ASP_RX_DAI1_CH2_BIT_LSB:
	case CS42L42_SUB_REVID:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS_GPL(cs42l42_readable_register, SND_SOC_CS42L42_CORE);

bool cs42l42_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L42_DEVID_AB:
	case CS42L42_DEVID_CD:
	case CS42L42_DEVID_E:
	case CS42L42_MCLK_STATUS:
	case CS42L42_OSC_SWITCH_STATUS:
	case CS42L42_TRSENSE_STATUS:
	case CS42L42_HS_DET_STATUS:
	case CS42L42_ADC_OVFL_STATUS:
	case CS42L42_MIXER_STATUS:
	case CS42L42_SRC_STATUS:
	case CS42L42_ASP_RX_STATUS:
	case CS42L42_ASP_TX_STATUS:
	case CS42L42_CODEC_STATUS:
	case CS42L42_DET_INT_STATUS1:
	case CS42L42_DET_INT_STATUS2:
	case CS42L42_SRCPL_INT_STATUS:
	case CS42L42_VPMON_STATUS:
	case CS42L42_PLL_LOCK_STATUS:
	case CS42L42_TSRS_PLUG_STATUS:
	case CS42L42_LOAD_DET_RCSTAT:
	case CS42L42_LOAD_DET_DONE:
	case CS42L42_DET_STATUS1:
	case CS42L42_DET_STATUS2:
	case CS42L42_SOFT_RESET_REBOOT:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS_GPL(cs42l42_volatile_register, SND_SOC_CS42L42_CORE);

const struct regmap_range_cfg cs42l42_page_range = {
	.name = "Pages",
	.range_min = 0,
	.range_max = CS42L42_MAX_REGISTER,
	.selector_reg = CS42L42_PAGE_REGISTER,
	.selector_mask = 0xff,
	.selector_shift = 0,
	.window_start = 0,
	.window_len = 256,
};
EXPORT_SYMBOL_NS_GPL(cs42l42_page_range, SND_SOC_CS42L42_CORE);

const struct regmap_config cs42l42_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = cs42l42_readable_register,
	.volatile_reg = cs42l42_volatile_register,

	.ranges = &cs42l42_page_range,
	.num_ranges = 1,

	.max_register = CS42L42_MAX_REGISTER,
	.reg_defaults = cs42l42_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs42l42_reg_defaults),
	.cache_type = REGCACHE_MAPLE,

	.use_single_read = true,
	.use_single_write = true,
};
EXPORT_SYMBOL_NS_GPL(cs42l42_regmap, SND_SOC_CS42L42_CORE);

static DECLARE_TLV_DB_SCALE(adc_tlv, -9700, 100, true);
static DECLARE_TLV_DB_SCALE(mixer_tlv, -6300, 100, true);

static int cs42l42_slow_start_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	u8 val;

	/* all bits of SLOW_START_EN must change together */
	switch (ucontrol->value.integer.value[0]) {
	case 0:
		val = 0;
		break;
	case 1:
		val = CS42L42_SLOW_START_EN_MASK;
		break;
	default:
		return -EINVAL;
	}

	return snd_soc_component_update_bits(component, CS42L42_SLOW_START_ENABLE,
					     CS42L42_SLOW_START_EN_MASK, val);
}

static const char * const cs42l42_hpf_freq_text[] = {
	"1.86Hz", "120Hz", "235Hz", "466Hz"
};

static SOC_ENUM_SINGLE_DECL(cs42l42_hpf_freq_enum, CS42L42_ADC_WNF_HPF_CTL,
			    CS42L42_ADC_HPF_CF_SHIFT,
			    cs42l42_hpf_freq_text);

static const char * const cs42l42_wnf3_freq_text[] = {
	"160Hz", "180Hz", "200Hz", "220Hz",
	"240Hz", "260Hz", "280Hz", "300Hz"
};

static SOC_ENUM_SINGLE_DECL(cs42l42_wnf3_freq_enum, CS42L42_ADC_WNF_HPF_CTL,
			    CS42L42_ADC_WNF_CF_SHIFT,
			    cs42l42_wnf3_freq_text);

static const struct snd_kcontrol_new cs42l42_snd_controls[] = {
	/* ADC Volume and Filter Controls */
	SOC_SINGLE("ADC Notch Switch", CS42L42_ADC_CTL,
				CS42L42_ADC_NOTCH_DIS_SHIFT, true, true),
	SOC_SINGLE("ADC Weak Force Switch", CS42L42_ADC_CTL,
				CS42L42_ADC_FORCE_WEAK_VCM_SHIFT, true, false),
	SOC_SINGLE("ADC Invert Switch", CS42L42_ADC_CTL,
				CS42L42_ADC_INV_SHIFT, true, false),
	SOC_SINGLE("ADC Boost Switch", CS42L42_ADC_CTL,
				CS42L42_ADC_DIG_BOOST_SHIFT, true, false),
	SOC_SINGLE_S8_TLV("ADC Volume", CS42L42_ADC_VOLUME, -97, 12, adc_tlv),
	SOC_SINGLE("ADC WNF Switch", CS42L42_ADC_WNF_HPF_CTL,
				CS42L42_ADC_WNF_EN_SHIFT, true, false),
	SOC_SINGLE("ADC HPF Switch", CS42L42_ADC_WNF_HPF_CTL,
				CS42L42_ADC_HPF_EN_SHIFT, true, false),
	SOC_ENUM("HPF Corner Freq", cs42l42_hpf_freq_enum),
	SOC_ENUM("WNF 3dB Freq", cs42l42_wnf3_freq_enum),

	/* DAC Volume and Filter Controls */
	SOC_SINGLE("DACA Invert Switch", CS42L42_DAC_CTL1,
				CS42L42_DACA_INV_SHIFT, true, false),
	SOC_SINGLE("DACB Invert Switch", CS42L42_DAC_CTL1,
				CS42L42_DACB_INV_SHIFT, true, false),
	SOC_SINGLE("DAC HPF Switch", CS42L42_DAC_CTL2,
				CS42L42_DAC_HPF_EN_SHIFT, true, false),
	SOC_DOUBLE_R_TLV("Mixer Volume", CS42L42_MIXER_CHA_VOL,
			 CS42L42_MIXER_CHB_VOL, CS42L42_MIXER_CH_VOL_SHIFT,
				0x3f, 1, mixer_tlv),

	SOC_SINGLE_EXT("Slow Start Switch", CS42L42_SLOW_START_ENABLE,
			CS42L42_SLOW_START_EN_SHIFT, true, false,
			snd_soc_get_volsw, cs42l42_slow_start_put),
};

static int cs42l42_hp_adc_ev(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		cs42l42->hp_adc_up_pending = true;
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* Only need one delay if HP and ADC are both powering-up */
		if (cs42l42->hp_adc_up_pending) {
			usleep_range(CS42L42_HP_ADC_EN_TIME_US,
				     CS42L42_HP_ADC_EN_TIME_US + 1000);
			cs42l42->hp_adc_up_pending = false;
		}
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget cs42l42_dapm_widgets[] = {
	/* Playback Path */
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_DAC_E("DAC", NULL, CS42L42_PWR_CTL1, CS42L42_HP_PDN_SHIFT, 1,
			   cs42l42_hp_adc_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER("MIXER", CS42L42_PWR_CTL1, CS42L42_MIXER_PDN_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_AIF_IN("SDIN1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SDIN2", NULL, 1, SND_SOC_NOPM, 0, 0),

	/* Playback Requirements */
	SND_SOC_DAPM_SUPPLY("ASP DAI0", CS42L42_PWR_CTL1, CS42L42_ASP_DAI_PDN_SHIFT, 1, NULL, 0),

	/* Capture Path */
	SND_SOC_DAPM_INPUT("HS"),
	SND_SOC_DAPM_ADC_E("ADC", NULL, CS42L42_PWR_CTL1, CS42L42_ADC_PDN_SHIFT, 1,
			   cs42l42_hp_adc_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_AIF_OUT("SDOUT1", NULL, 0, CS42L42_ASP_TX_CH_EN, CS42L42_ASP_TX0_CH1_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("SDOUT2", NULL, 1, CS42L42_ASP_TX_CH_EN, CS42L42_ASP_TX0_CH2_SHIFT, 0),

	/* Capture Requirements */
	SND_SOC_DAPM_SUPPLY("ASP DAO0", CS42L42_PWR_CTL1, CS42L42_ASP_DAO_PDN_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ASP TX EN", CS42L42_ASP_TX_SZ_EN, CS42L42_ASP_TX_EN_SHIFT, 0, NULL, 0),

	/* Playback/Capture Requirements */
	SND_SOC_DAPM_SUPPLY("SCLK", CS42L42_ASP_CLK_CFG, CS42L42_ASP_SCLK_EN_SHIFT, 0, NULL, 0),

	/* Soundwire SRC power control */
	SND_SOC_DAPM_PGA("DACSRC", CS42L42_PWR_CTL2, CS42L42_DAC_SRC_PDNB_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADCSRC", CS42L42_PWR_CTL2, CS42L42_ADC_SRC_PDNB_SHIFT, 0, NULL, 0),
};

static const struct snd_soc_dapm_route cs42l42_audio_map[] = {
	/* Playback Path */
	{"HP", NULL, "DAC"},
	{"DAC", NULL, "MIXER"},
	{"MIXER", NULL, "SDIN1"},
	{"MIXER", NULL, "SDIN2"},
	{"SDIN1", NULL, "Playback"},
	{"SDIN2", NULL, "Playback"},

	/* Playback Requirements */
	{"SDIN1", NULL, "ASP DAI0"},
	{"SDIN2", NULL, "ASP DAI0"},
	{"SDIN1", NULL, "SCLK"},
	{"SDIN2", NULL, "SCLK"},

	/* Capture Path */
	{"ADC", NULL, "HS"},
	{ "SDOUT1", NULL, "ADC" },
	{ "SDOUT2", NULL, "ADC" },
	{ "Capture", NULL, "SDOUT1" },
	{ "Capture", NULL, "SDOUT2" },

	/* Capture Requirements */
	{ "SDOUT1", NULL, "ASP DAO0" },
	{ "SDOUT2", NULL, "ASP DAO0" },
	{ "SDOUT1", NULL, "SCLK" },
	{ "SDOUT2", NULL, "SCLK" },
	{ "SDOUT1", NULL, "ASP TX EN" },
	{ "SDOUT2", NULL, "ASP TX EN" },
};

static int cs42l42_set_jack(struct snd_soc_component *component, struct snd_soc_jack *jk, void *d)
{
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(component);

	/* Prevent race with interrupt handler */
	mutex_lock(&cs42l42->irq_lock);
	cs42l42->jack = jk;

	if (jk) {
		switch (cs42l42->hs_type) {
		case CS42L42_PLUG_CTIA:
		case CS42L42_PLUG_OMTP:
			snd_soc_jack_report(jk, SND_JACK_HEADSET, SND_JACK_HEADSET);
			break;
		case CS42L42_PLUG_HEADPHONE:
			snd_soc_jack_report(jk, SND_JACK_HEADPHONE, SND_JACK_HEADPHONE);
			break;
		default:
			break;
		}
	}
	mutex_unlock(&cs42l42->irq_lock);

	return 0;
}

const struct snd_soc_component_driver cs42l42_soc_component = {
	.set_jack		= cs42l42_set_jack,
	.dapm_widgets		= cs42l42_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs42l42_dapm_widgets),
	.dapm_routes		= cs42l42_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(cs42l42_audio_map),
	.controls		= cs42l42_snd_controls,
	.num_controls		= ARRAY_SIZE(cs42l42_snd_controls),
	.endianness		= 1,
};
EXPORT_SYMBOL_NS_GPL(cs42l42_soc_component, SND_SOC_CS42L42_CORE);

/* Switch to SCLK. Atomic delay after the write to allow the switch to complete. */
static const struct reg_sequence cs42l42_to_sclk_seq[] = {
	{
		.reg = CS42L42_OSC_SWITCH,
		.def = CS42L42_SCLK_PRESENT_MASK,
		.delay_us = CS42L42_CLOCK_SWITCH_DELAY_US,
	},
};

/* Switch to OSC. Atomic delay after the write to allow the switch to complete. */
static const struct reg_sequence cs42l42_to_osc_seq[] = {
	{
		.reg = CS42L42_OSC_SWITCH,
		.def = 0,
		.delay_us = CS42L42_CLOCK_SWITCH_DELAY_US,
	},
};

struct cs42l42_pll_params {
	u32 sclk;
	u8 mclk_src_sel;
	u8 sclk_prediv;
	u8 pll_div_int;
	u32 pll_div_frac;
	u8 pll_mode;
	u8 pll_divout;
	u32 mclk_int;
	u8 pll_cal_ratio;
	u8 n;
};

/*
 * Common PLL Settings for given SCLK
 * Table 4-5 from the Datasheet
 */
static const struct cs42l42_pll_params pll_ratio_table[] = {
	{ 1411200,  1, 0x00, 0x80, 0x000000, 0x03, 0x10, 11289600, 128, 2},
	{ 1536000,  1, 0x00, 0x7D, 0x000000, 0x03, 0x10, 12000000, 125, 2},
	{ 2304000,  1, 0x00, 0x55, 0xC00000, 0x02, 0x10, 12288000,  85, 2},
	{ 2400000,  1, 0x00, 0x50, 0x000000, 0x03, 0x10, 12000000,  80, 2},
	{ 2822400,  1, 0x00, 0x40, 0x000000, 0x03, 0x10, 11289600, 128, 1},
	{ 3000000,  1, 0x00, 0x40, 0x000000, 0x03, 0x10, 12000000, 128, 1},
	{ 3072000,  1, 0x00, 0x3E, 0x800000, 0x03, 0x10, 12000000, 125, 1},
	{ 4000000,  1, 0x00, 0x30, 0x800000, 0x03, 0x10, 12000000,  96, 1},
	{ 4096000,  1, 0x00, 0x2E, 0xE00000, 0x03, 0x10, 12000000,  94, 1},
	{ 4800000,  1, 0x01, 0x50, 0x000000, 0x03, 0x10, 12000000,  80, 2},
	{ 4800000,  1, 0x01, 0x50, 0x000000, 0x01, 0x10, 12288000,  82, 2},
	{ 5644800,  1, 0x01, 0x40, 0x000000, 0x03, 0x10, 11289600, 128, 1},
	{ 6000000,  1, 0x01, 0x40, 0x000000, 0x03, 0x10, 12000000, 128, 1},
	{ 6144000,  1, 0x01, 0x3E, 0x800000, 0x03, 0x10, 12000000, 125, 1},
	{ 6144000,  1, 0x01, 0x40, 0x000000, 0x03, 0x10, 12288000, 128, 1},
	{ 9600000,  1, 0x02, 0x50, 0x000000, 0x03, 0x10, 12000000,  80, 2},
	{ 9600000,  1, 0x02, 0x50, 0x000000, 0x01, 0x10, 12288000,  82, 2},
	{ 11289600, 0, 0, 0, 0, 0, 0, 11289600, 0, 1},
	{ 12000000, 0, 0, 0, 0, 0, 0, 12000000, 0, 1},
	{ 12288000, 0, 0, 0, 0, 0, 0, 12288000, 0, 1},
	{ 19200000, 1, 0x03, 0x50, 0x000000, 0x03, 0x10, 12000000,  80, 2},
	{ 19200000, 1, 0x03, 0x50, 0x000000, 0x01, 0x10, 12288000,  82, 2},
	{ 22579200, 1, 0x03, 0x40, 0x000000, 0x03, 0x10, 11289600, 128, 1},
	{ 24000000, 1, 0x03, 0x40, 0x000000, 0x03, 0x10, 12000000, 128, 1},
	{ 24576000, 1, 0x03, 0x40, 0x000000, 0x03, 0x10, 12288000, 128, 1}
};

int cs42l42_pll_config(struct snd_soc_component *component, unsigned int clk,
		       unsigned int sample_rate)
{
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(component);
	int i;

	/* Don't reconfigure if there is an audio stream running */
	if (cs42l42->stream_use) {
		if (pll_ratio_table[cs42l42->pll_config].sclk == clk)
			return 0;
		else
			return -EBUSY;
	}

	for (i = 0; i < ARRAY_SIZE(pll_ratio_table); i++) {
		/* MCLKint must be a multiple of the sample rate */
		if (pll_ratio_table[i].mclk_int % sample_rate)
			continue;

		if (pll_ratio_table[i].sclk == clk) {
			cs42l42->pll_config = i;

			/* Configure the internal sample rate */
			snd_soc_component_update_bits(component, CS42L42_MCLK_CTL,
					CS42L42_INTERNAL_FS_MASK,
					((pll_ratio_table[i].mclk_int !=
					12000000) &&
					(pll_ratio_table[i].mclk_int !=
					24000000)) <<
					CS42L42_INTERNAL_FS_SHIFT);
			if (pll_ratio_table[i].mclk_src_sel == 0) {
				/* Pass the clock straight through */
				snd_soc_component_update_bits(component,
					CS42L42_PLL_CTL1,
					CS42L42_PLL_START_MASK,	0);
			} else {
				/* Configure PLL per table 4-5 */
				snd_soc_component_update_bits(component,
					CS42L42_PLL_DIV_CFG1,
					CS42L42_SCLK_PREDIV_MASK,
					pll_ratio_table[i].sclk_prediv
					<< CS42L42_SCLK_PREDIV_SHIFT);
				snd_soc_component_update_bits(component,
					CS42L42_PLL_DIV_INT,
					CS42L42_PLL_DIV_INT_MASK,
					pll_ratio_table[i].pll_div_int
					<< CS42L42_PLL_DIV_INT_SHIFT);
				snd_soc_component_update_bits(component,
					CS42L42_PLL_DIV_FRAC0,
					CS42L42_PLL_DIV_FRAC_MASK,
					CS42L42_FRAC0_VAL(
					pll_ratio_table[i].pll_div_frac)
					<< CS42L42_PLL_DIV_FRAC_SHIFT);
				snd_soc_component_update_bits(component,
					CS42L42_PLL_DIV_FRAC1,
					CS42L42_PLL_DIV_FRAC_MASK,
					CS42L42_FRAC1_VAL(
					pll_ratio_table[i].pll_div_frac)
					<< CS42L42_PLL_DIV_FRAC_SHIFT);
				snd_soc_component_update_bits(component,
					CS42L42_PLL_DIV_FRAC2,
					CS42L42_PLL_DIV_FRAC_MASK,
					CS42L42_FRAC2_VAL(
					pll_ratio_table[i].pll_div_frac)
					<< CS42L42_PLL_DIV_FRAC_SHIFT);
				snd_soc_component_update_bits(component,
					CS42L42_PLL_CTL4,
					CS42L42_PLL_MODE_MASK,
					pll_ratio_table[i].pll_mode
					<< CS42L42_PLL_MODE_SHIFT);
				snd_soc_component_update_bits(component,
					CS42L42_PLL_CTL3,
					CS42L42_PLL_DIVOUT_MASK,
					(pll_ratio_table[i].pll_divout * pll_ratio_table[i].n)
					<< CS42L42_PLL_DIVOUT_SHIFT);
				snd_soc_component_update_bits(component,
					CS42L42_PLL_CAL_RATIO,
					CS42L42_PLL_CAL_RATIO_MASK,
					pll_ratio_table[i].pll_cal_ratio
					<< CS42L42_PLL_CAL_RATIO_SHIFT);
			}
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_NS_GPL(cs42l42_pll_config, SND_SOC_CS42L42_CORE);

void cs42l42_src_config(struct snd_soc_component *component, unsigned int sample_rate)
{
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(component);
	unsigned int fs;

	/* Don't reconfigure if there is an audio stream running */
	if (cs42l42->stream_use)
		return;

	/* SRC MCLK must be as close as possible to 125 * sample rate */
	if (sample_rate <= 48000)
		fs = CS42L42_CLK_IASRC_SEL_6;
	else
		fs = CS42L42_CLK_IASRC_SEL_12;

	/* Set the sample rates (96k or lower) */
	snd_soc_component_update_bits(component,
				      CS42L42_FS_RATE_EN,
				      CS42L42_FS_EN_MASK,
				      (CS42L42_FS_EN_IASRC_96K |
				       CS42L42_FS_EN_OASRC_96K) <<
				      CS42L42_FS_EN_SHIFT);

	snd_soc_component_update_bits(component,
				      CS42L42_IN_ASRC_CLK,
				      CS42L42_CLK_IASRC_SEL_MASK,
				      fs << CS42L42_CLK_IASRC_SEL_SHIFT);
	snd_soc_component_update_bits(component,
				      CS42L42_OUT_ASRC_CLK,
				      CS42L42_CLK_OASRC_SEL_MASK,
				      fs << CS42L42_CLK_OASRC_SEL_SHIFT);
}
EXPORT_SYMBOL_NS_GPL(cs42l42_src_config, SND_SOC_CS42L42_CORE);

static int cs42l42_asp_config(struct snd_soc_component *component,
			      unsigned int sclk, unsigned int sample_rate)
{
	u32 fsync = sclk / sample_rate;

	/* Set up the LRCLK */
	if (((fsync * sample_rate) != sclk) || ((fsync % 2) != 0)) {
		dev_err(component->dev,
			"Unsupported sclk %d/sample rate %d\n",
			sclk,
			sample_rate);
		return -EINVAL;
	}
	/* Set the LRCLK period */
	snd_soc_component_update_bits(component,
				      CS42L42_FSYNC_P_LOWER,
				      CS42L42_FSYNC_PERIOD_MASK,
				      CS42L42_FRAC0_VAL(fsync - 1) <<
				      CS42L42_FSYNC_PERIOD_SHIFT);
	snd_soc_component_update_bits(component,
				      CS42L42_FSYNC_P_UPPER,
				      CS42L42_FSYNC_PERIOD_MASK,
				      CS42L42_FRAC1_VAL(fsync - 1) <<
				      CS42L42_FSYNC_PERIOD_SHIFT);
	/* Set the LRCLK to 50% duty cycle */
	fsync = fsync / 2;
	snd_soc_component_update_bits(component,
				      CS42L42_FSYNC_PW_LOWER,
				      CS42L42_FSYNC_PULSE_WIDTH_MASK,
				      CS42L42_FRAC0_VAL(fsync - 1) <<
				      CS42L42_FSYNC_PULSE_WIDTH_SHIFT);
	snd_soc_component_update_bits(component,
				      CS42L42_FSYNC_PW_UPPER,
				      CS42L42_FSYNC_PULSE_WIDTH_MASK,
				      CS42L42_FRAC1_VAL(fsync - 1) <<
				      CS42L42_FSYNC_PULSE_WIDTH_SHIFT);

	return 0;
}

static int cs42l42_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u32 asp_cfg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFM:
		asp_cfg_val |= CS42L42_ASP_MASTER_MODE <<
				CS42L42_ASP_MODE_SHIFT;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		asp_cfg_val |= CS42L42_ASP_SLAVE_MODE <<
				CS42L42_ASP_MODE_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/*
		 * 5050 mode, frame starts on falling edge of LRCLK,
		 * frame delayed by 1.0 SCLKs
		 */
		snd_soc_component_update_bits(component,
					      CS42L42_ASP_FRM_CFG,
					      CS42L42_ASP_STP_MASK |
					      CS42L42_ASP_5050_MASK |
					      CS42L42_ASP_FSD_MASK,
					      CS42L42_ASP_5050_MASK |
					      (CS42L42_ASP_FSD_1_0 <<
						CS42L42_ASP_FSD_SHIFT));
		break;
	default:
		return -EINVAL;
	}

	/* Bitclock/frame inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		asp_cfg_val |= CS42L42_ASP_SCPOL_NOR << CS42L42_ASP_SCPOL_SHIFT;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		asp_cfg_val |= CS42L42_ASP_SCPOL_NOR << CS42L42_ASP_SCPOL_SHIFT;
		asp_cfg_val |= CS42L42_ASP_LCPOL_INV << CS42L42_ASP_LCPOL_SHIFT;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		asp_cfg_val |= CS42L42_ASP_LCPOL_INV << CS42L42_ASP_LCPOL_SHIFT;
		break;
	}

	snd_soc_component_update_bits(component, CS42L42_ASP_CLK_CFG, CS42L42_ASP_MODE_MASK |
								      CS42L42_ASP_SCPOL_MASK |
								      CS42L42_ASP_LCPOL_MASK,
								      asp_cfg_val);

	return 0;
}

static int cs42l42_dai_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(component);

	/*
	 * Sample rates < 44.1 kHz would produce an out-of-range SCLK with
	 * a standard I2S frame. If the machine driver sets SCLK it must be
	 * legal.
	 */
	if (cs42l42->sclk)
		return 0;

	/* Machine driver has not set a SCLK, limit bottom end to 44.1 kHz */
	return snd_pcm_hw_constraint_minmax(substream->runtime,
					    SNDRV_PCM_HW_PARAM_RATE,
					    44100, 96000);
}

static int cs42l42_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(component);
	unsigned int channels = params_channels(params);
	unsigned int width = (params_width(params) / 8) - 1;
	unsigned int sample_rate = params_rate(params);
	unsigned int slot_width = 0;
	unsigned int val = 0;
	unsigned int bclk;
	int ret;

	if (cs42l42->bclk_ratio) {
		/* machine driver has set the BCLK/samp-rate ratio */
		bclk = cs42l42->bclk_ratio * params_rate(params);
	} else if (cs42l42->sclk) {
		/* machine driver has set the SCLK */
		bclk = cs42l42->sclk;
	} else {
		/*
		 * Assume 24-bit samples are in 32-bit slots, to prevent SCLK being
		 * more than assumed (which would result in overclocking).
		 */
		if (params_width(params) == 24)
			slot_width = 32;

		/* I2S frame always has multiple of 2 channels */
		bclk = snd_soc_tdm_params_to_bclk(params, slot_width, 0, 2);
	}

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_CAPTURE:
		/* channel 2 on high LRCLK */
		val = CS42L42_ASP_TX_CH2_AP_MASK |
		      (width << CS42L42_ASP_TX_CH2_RES_SHIFT) |
		      (width << CS42L42_ASP_TX_CH1_RES_SHIFT);

		snd_soc_component_update_bits(component, CS42L42_ASP_TX_CH_AP_RES,
				CS42L42_ASP_TX_CH1_AP_MASK | CS42L42_ASP_TX_CH2_AP_MASK |
				CS42L42_ASP_TX_CH2_RES_MASK | CS42L42_ASP_TX_CH1_RES_MASK, val);
		break;
	case SNDRV_PCM_STREAM_PLAYBACK:
		val |= width << CS42L42_ASP_RX_CH_RES_SHIFT;
		/* channel 1 on low LRCLK */
		snd_soc_component_update_bits(component, CS42L42_ASP_RX_DAI0_CH1_AP_RES,
							 CS42L42_ASP_RX_CH_AP_MASK |
							 CS42L42_ASP_RX_CH_RES_MASK, val);
		/* Channel 2 on high LRCLK */
		val |= CS42L42_ASP_RX_CH_AP_HI << CS42L42_ASP_RX_CH_AP_SHIFT;
		snd_soc_component_update_bits(component, CS42L42_ASP_RX_DAI0_CH2_AP_RES,
							 CS42L42_ASP_RX_CH_AP_MASK |
							 CS42L42_ASP_RX_CH_RES_MASK, val);

		/* Channel B comes from the last active channel */
		snd_soc_component_update_bits(component, CS42L42_SP_RX_CH_SEL,
					      CS42L42_SP_RX_CHB_SEL_MASK,
					      (channels - 1) << CS42L42_SP_RX_CHB_SEL_SHIFT);

		/* Both LRCLK slots must be enabled */
		snd_soc_component_update_bits(component, CS42L42_ASP_RX_DAI0_EN,
					      CS42L42_ASP_RX0_CH_EN_MASK,
					      BIT(CS42L42_ASP_RX0_CH1_SHIFT) |
					      BIT(CS42L42_ASP_RX0_CH2_SHIFT));
		break;
	default:
		break;
	}

	ret = cs42l42_pll_config(component, bclk, sample_rate);
	if (ret)
		return ret;

	ret = cs42l42_asp_config(component, bclk, sample_rate);
	if (ret)
		return ret;

	cs42l42_src_config(component, sample_rate);

	return 0;
}

static int cs42l42_set_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(component);
	int i;

	if (freq == 0) {
		cs42l42->sclk = 0;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(pll_ratio_table); i++) {
		if (pll_ratio_table[i].sclk == freq) {
			cs42l42->sclk = freq;
			return 0;
		}
	}

	dev_err(component->dev, "SCLK %u not supported\n", freq);

	return -EINVAL;
}

static int cs42l42_set_bclk_ratio(struct snd_soc_dai *dai,
				unsigned int bclk_ratio)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(component);

	cs42l42->bclk_ratio = bclk_ratio;

	return 0;
}

int cs42l42_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(component);
	unsigned int regval;
	int ret;

	if (mute) {
		/* Mute the headphone */
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			snd_soc_component_update_bits(component, CS42L42_HP_CTL,
						      CS42L42_HP_ANA_AMUTE_MASK |
						      CS42L42_HP_ANA_BMUTE_MASK,
						      CS42L42_HP_ANA_AMUTE_MASK |
						      CS42L42_HP_ANA_BMUTE_MASK);

		cs42l42->stream_use &= ~(1 << stream);
		if (!cs42l42->stream_use) {
			/*
			 * Switch to the internal oscillator.
			 * SCLK must remain running until after this clock switch.
			 * Without a source of clock the I2C bus doesn't work.
			 */
			regmap_multi_reg_write(cs42l42->regmap, cs42l42_to_osc_seq,
					       ARRAY_SIZE(cs42l42_to_osc_seq));

			/* Must disconnect PLL before stopping it */
			snd_soc_component_update_bits(component,
						      CS42L42_MCLK_SRC_SEL,
						      CS42L42_MCLK_SRC_SEL_MASK,
						      0);
			usleep_range(100, 200);

			snd_soc_component_update_bits(component, CS42L42_PLL_CTL1,
						      CS42L42_PLL_START_MASK, 0);
		}
	} else {
		if (!cs42l42->stream_use) {
			/* SCLK must be running before codec unmute.
			 *
			 * PLL must not be started with ADC and HP both off
			 * otherwise the FILT+ supply will not charge properly.
			 * DAPM widgets power-up before stream unmute so at least
			 * one of the "DAC" or "ADC" widgets will already have
			 * powered-up.
			 */
			if (pll_ratio_table[cs42l42->pll_config].mclk_src_sel) {
				snd_soc_component_update_bits(component, CS42L42_PLL_CTL1,
							      CS42L42_PLL_START_MASK, 1);

				if (pll_ratio_table[cs42l42->pll_config].n > 1) {
					usleep_range(CS42L42_PLL_DIVOUT_TIME_US,
						     CS42L42_PLL_DIVOUT_TIME_US * 2);
					regval = pll_ratio_table[cs42l42->pll_config].pll_divout;
					snd_soc_component_update_bits(component, CS42L42_PLL_CTL3,
								      CS42L42_PLL_DIVOUT_MASK,
								      regval <<
								      CS42L42_PLL_DIVOUT_SHIFT);
				}

				ret = regmap_read_poll_timeout(cs42l42->regmap,
							       CS42L42_PLL_LOCK_STATUS,
							       regval,
							       (regval & 1),
							       CS42L42_PLL_LOCK_POLL_US,
							       CS42L42_PLL_LOCK_TIMEOUT_US);
				if (ret < 0)
					dev_warn(component->dev, "PLL failed to lock: %d\n", ret);

				/* PLL must be running to drive glitchless switch logic */
				snd_soc_component_update_bits(component,
							      CS42L42_MCLK_SRC_SEL,
							      CS42L42_MCLK_SRC_SEL_MASK,
							      CS42L42_MCLK_SRC_SEL_MASK);
			}

			/* Mark SCLK as present, turn off internal oscillator */
			regmap_multi_reg_write(cs42l42->regmap, cs42l42_to_sclk_seq,
					       ARRAY_SIZE(cs42l42_to_sclk_seq));
		}
		cs42l42->stream_use |= 1 << stream;

		if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/* Un-mute the headphone */
			snd_soc_component_update_bits(component, CS42L42_HP_CTL,
						      CS42L42_HP_ANA_AMUTE_MASK |
						      CS42L42_HP_ANA_BMUTE_MASK,
						      0);
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs42l42_mute_stream, SND_SOC_CS42L42_CORE);

#define CS42L42_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops cs42l42_ops = {
	.startup	= cs42l42_dai_startup,
	.hw_params	= cs42l42_pcm_hw_params,
	.set_fmt	= cs42l42_set_dai_fmt,
	.set_sysclk	= cs42l42_set_sysclk,
	.set_bclk_ratio	= cs42l42_set_bclk_ratio,
	.mute_stream	= cs42l42_mute_stream,
};

struct snd_soc_dai_driver cs42l42_dai = {
		.name = "cs42l42",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = CS42L42_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = CS42L42_FORMATS,
		},
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
		.ops = &cs42l42_ops,
};
EXPORT_SYMBOL_NS_GPL(cs42l42_dai, SND_SOC_CS42L42_CORE);

static void cs42l42_manual_hs_type_detect(struct cs42l42_private *cs42l42)
{
	unsigned int hs_det_status;
	unsigned int hs_det_comp1;
	unsigned int hs_det_comp2;
	unsigned int hs_det_sw;

	/* Set hs detect to manual, active mode */
	regmap_update_bits(cs42l42->regmap,
		CS42L42_HSDET_CTL2,
		CS42L42_HSDET_CTRL_MASK |
		CS42L42_HSDET_SET_MASK |
		CS42L42_HSBIAS_REF_MASK |
		CS42L42_HSDET_AUTO_TIME_MASK,
		(1 << CS42L42_HSDET_CTRL_SHIFT) |
		(0 << CS42L42_HSDET_SET_SHIFT) |
		(0 << CS42L42_HSBIAS_REF_SHIFT) |
		(0 << CS42L42_HSDET_AUTO_TIME_SHIFT));

	/* Configure HS DET comparator reference levels. */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_HSDET_CTL1,
				CS42L42_HSDET_COMP1_LVL_MASK |
				CS42L42_HSDET_COMP2_LVL_MASK,
				(CS42L42_HSDET_COMP1_LVL_VAL << CS42L42_HSDET_COMP1_LVL_SHIFT) |
				(CS42L42_HSDET_COMP2_LVL_VAL << CS42L42_HSDET_COMP2_LVL_SHIFT));

	/* Open the SW_HSB_HS3 switch and close SW_HSB_HS4 for a Type 1 headset. */
	regmap_write(cs42l42->regmap, CS42L42_HS_SWITCH_CTL, CS42L42_HSDET_SW_COMP1);

	msleep(100);

	regmap_read(cs42l42->regmap, CS42L42_HS_DET_STATUS, &hs_det_status);

	hs_det_comp1 = (hs_det_status & CS42L42_HSDET_COMP1_OUT_MASK) >>
			CS42L42_HSDET_COMP1_OUT_SHIFT;
	hs_det_comp2 = (hs_det_status & CS42L42_HSDET_COMP2_OUT_MASK) >>
			CS42L42_HSDET_COMP2_OUT_SHIFT;

	/* Close the SW_HSB_HS3 switch for a Type 2 headset. */
	regmap_write(cs42l42->regmap, CS42L42_HS_SWITCH_CTL, CS42L42_HSDET_SW_COMP2);

	msleep(100);

	regmap_read(cs42l42->regmap, CS42L42_HS_DET_STATUS, &hs_det_status);

	hs_det_comp1 |= ((hs_det_status & CS42L42_HSDET_COMP1_OUT_MASK) >>
			CS42L42_HSDET_COMP1_OUT_SHIFT) << 1;
	hs_det_comp2 |= ((hs_det_status & CS42L42_HSDET_COMP2_OUT_MASK) >>
			CS42L42_HSDET_COMP2_OUT_SHIFT) << 1;

	/* Use Comparator 1 with 1.25V Threshold. */
	switch (hs_det_comp1) {
	case CS42L42_HSDET_COMP_TYPE1:
		cs42l42->hs_type = CS42L42_PLUG_CTIA;
		hs_det_sw = CS42L42_HSDET_SW_TYPE1;
		break;
	case CS42L42_HSDET_COMP_TYPE2:
		cs42l42->hs_type = CS42L42_PLUG_OMTP;
		hs_det_sw = CS42L42_HSDET_SW_TYPE2;
		break;
	default:
		/* Fallback to Comparator 2 with 1.75V Threshold. */
		switch (hs_det_comp2) {
		case CS42L42_HSDET_COMP_TYPE1:
			cs42l42->hs_type = CS42L42_PLUG_CTIA;
			hs_det_sw = CS42L42_HSDET_SW_TYPE1;
			break;
		case CS42L42_HSDET_COMP_TYPE2:
			cs42l42->hs_type = CS42L42_PLUG_OMTP;
			hs_det_sw = CS42L42_HSDET_SW_TYPE2;
			break;
		/* Detect Type 3 and Type 4 Headsets as Headphones */
		default:
			cs42l42->hs_type = CS42L42_PLUG_HEADPHONE;
			hs_det_sw = CS42L42_HSDET_SW_TYPE3;
			break;
		}
	}

	/* Set Switches */
	regmap_write(cs42l42->regmap, CS42L42_HS_SWITCH_CTL, hs_det_sw);

	/* Set HSDET mode to Manual—Disabled */
	regmap_update_bits(cs42l42->regmap,
		CS42L42_HSDET_CTL2,
		CS42L42_HSDET_CTRL_MASK |
		CS42L42_HSDET_SET_MASK |
		CS42L42_HSBIAS_REF_MASK |
		CS42L42_HSDET_AUTO_TIME_MASK,
		(0 << CS42L42_HSDET_CTRL_SHIFT) |
		(0 << CS42L42_HSDET_SET_SHIFT) |
		(0 << CS42L42_HSBIAS_REF_SHIFT) |
		(0 << CS42L42_HSDET_AUTO_TIME_SHIFT));

	/* Configure HS DET comparator reference levels. */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_HSDET_CTL1,
				CS42L42_HSDET_COMP1_LVL_MASK |
				CS42L42_HSDET_COMP2_LVL_MASK,
				(CS42L42_HSDET_COMP1_LVL_DEFAULT << CS42L42_HSDET_COMP1_LVL_SHIFT) |
				(CS42L42_HSDET_COMP2_LVL_DEFAULT << CS42L42_HSDET_COMP2_LVL_SHIFT));
}

static void cs42l42_process_hs_type_detect(struct cs42l42_private *cs42l42)
{
	unsigned int hs_det_status;
	unsigned int int_status;

	/* Read and save the hs detection result */
	regmap_read(cs42l42->regmap, CS42L42_HS_DET_STATUS, &hs_det_status);

	/* Mask the auto detect interrupt */
	regmap_update_bits(cs42l42->regmap,
		CS42L42_CODEC_INT_MASK,
		CS42L42_PDN_DONE_MASK |
		CS42L42_HSDET_AUTO_DONE_MASK,
		(1 << CS42L42_PDN_DONE_SHIFT) |
		(1 << CS42L42_HSDET_AUTO_DONE_SHIFT));


	cs42l42->hs_type = (hs_det_status & CS42L42_HSDET_TYPE_MASK) >>
				CS42L42_HSDET_TYPE_SHIFT;

	/* Set hs detect to automatic, disabled mode */
	regmap_update_bits(cs42l42->regmap,
		CS42L42_HSDET_CTL2,
		CS42L42_HSDET_CTRL_MASK |
		CS42L42_HSDET_SET_MASK |
		CS42L42_HSBIAS_REF_MASK |
		CS42L42_HSDET_AUTO_TIME_MASK,
		(2 << CS42L42_HSDET_CTRL_SHIFT) |
		(2 << CS42L42_HSDET_SET_SHIFT) |
		(0 << CS42L42_HSBIAS_REF_SHIFT) |
		(3 << CS42L42_HSDET_AUTO_TIME_SHIFT));

	/* Run Manual detection if auto detect has not found a headset.
	 * We Re-Run with Manual Detection if the original detection was invalid or headphones,
	 * to ensure that a headset mic is detected in all cases.
	 */
	if (cs42l42->hs_type == CS42L42_PLUG_INVALID ||
		cs42l42->hs_type == CS42L42_PLUG_HEADPHONE) {
		dev_dbg(cs42l42->dev, "Running Manual Detection Fallback\n");
		cs42l42_manual_hs_type_detect(cs42l42);
	}

	/* Set up button detection */
	if ((cs42l42->hs_type == CS42L42_PLUG_CTIA) ||
	      (cs42l42->hs_type == CS42L42_PLUG_OMTP)) {
		/* Set auto HS bias settings to default */
		regmap_update_bits(cs42l42->regmap,
			CS42L42_HSBIAS_SC_AUTOCTL,
			CS42L42_HSBIAS_SENSE_EN_MASK |
			CS42L42_AUTO_HSBIAS_HIZ_MASK |
			CS42L42_TIP_SENSE_EN_MASK |
			CS42L42_HSBIAS_SENSE_TRIP_MASK,
			(0 << CS42L42_HSBIAS_SENSE_EN_SHIFT) |
			(0 << CS42L42_AUTO_HSBIAS_HIZ_SHIFT) |
			(0 << CS42L42_TIP_SENSE_EN_SHIFT) |
			(3 << CS42L42_HSBIAS_SENSE_TRIP_SHIFT));

		/* Set up hs detect level sensitivity */
		regmap_update_bits(cs42l42->regmap,
			CS42L42_MIC_DET_CTL1,
			CS42L42_LATCH_TO_VP_MASK |
			CS42L42_EVENT_STAT_SEL_MASK |
			CS42L42_HS_DET_LEVEL_MASK,
			(1 << CS42L42_LATCH_TO_VP_SHIFT) |
			(0 << CS42L42_EVENT_STAT_SEL_SHIFT) |
			(cs42l42->bias_thresholds[0] <<
			CS42L42_HS_DET_LEVEL_SHIFT));

		/* Set auto HS bias settings to default */
		regmap_update_bits(cs42l42->regmap,
			CS42L42_HSBIAS_SC_AUTOCTL,
			CS42L42_HSBIAS_SENSE_EN_MASK |
			CS42L42_AUTO_HSBIAS_HIZ_MASK |
			CS42L42_TIP_SENSE_EN_MASK |
			CS42L42_HSBIAS_SENSE_TRIP_MASK,
			(cs42l42->hs_bias_sense_en << CS42L42_HSBIAS_SENSE_EN_SHIFT) |
			(1 << CS42L42_AUTO_HSBIAS_HIZ_SHIFT) |
			(0 << CS42L42_TIP_SENSE_EN_SHIFT) |
			(3 << CS42L42_HSBIAS_SENSE_TRIP_SHIFT));

		/* Turn on level detect circuitry */
		regmap_update_bits(cs42l42->regmap,
			CS42L42_MISC_DET_CTL,
			CS42L42_HSBIAS_CTL_MASK |
			CS42L42_PDN_MIC_LVL_DET_MASK,
			(3 << CS42L42_HSBIAS_CTL_SHIFT) |
			(0 << CS42L42_PDN_MIC_LVL_DET_SHIFT));

		msleep(cs42l42->btn_det_init_dbnce);

		/* Clear any button interrupts before unmasking them */
		regmap_read(cs42l42->regmap, CS42L42_DET_INT_STATUS2,
			    &int_status);

		/* Unmask button detect interrupts */
		regmap_update_bits(cs42l42->regmap,
			CS42L42_DET_INT2_MASK,
			CS42L42_M_DETECT_TF_MASK |
			CS42L42_M_DETECT_FT_MASK |
			CS42L42_M_HSBIAS_HIZ_MASK |
			CS42L42_M_SHORT_RLS_MASK |
			CS42L42_M_SHORT_DET_MASK,
			(0 << CS42L42_M_DETECT_TF_SHIFT) |
			(0 << CS42L42_M_DETECT_FT_SHIFT) |
			(0 << CS42L42_M_HSBIAS_HIZ_SHIFT) |
			(1 << CS42L42_M_SHORT_RLS_SHIFT) |
			(1 << CS42L42_M_SHORT_DET_SHIFT));
	} else {
		/* Make sure button detect and HS bias circuits are off */
		regmap_update_bits(cs42l42->regmap,
			CS42L42_MISC_DET_CTL,
			CS42L42_HSBIAS_CTL_MASK |
			CS42L42_PDN_MIC_LVL_DET_MASK,
			(1 << CS42L42_HSBIAS_CTL_SHIFT) |
			(1 << CS42L42_PDN_MIC_LVL_DET_SHIFT));
	}

	regmap_update_bits(cs42l42->regmap,
				CS42L42_DAC_CTL2,
				CS42L42_HPOUT_PULLDOWN_MASK |
				CS42L42_HPOUT_LOAD_MASK |
				CS42L42_HPOUT_CLAMP_MASK |
				CS42L42_DAC_HPF_EN_MASK |
				CS42L42_DAC_MON_EN_MASK,
				(0 << CS42L42_HPOUT_PULLDOWN_SHIFT) |
				(0 << CS42L42_HPOUT_LOAD_SHIFT) |
				(0 << CS42L42_HPOUT_CLAMP_SHIFT) |
				(1 << CS42L42_DAC_HPF_EN_SHIFT) |
				(0 << CS42L42_DAC_MON_EN_SHIFT));

	/* Unmask tip sense interrupts */
	regmap_update_bits(cs42l42->regmap,
		CS42L42_TSRS_PLUG_INT_MASK,
		CS42L42_TS_PLUG_MASK |
		CS42L42_TS_UNPLUG_MASK,
		(0 << CS42L42_TS_PLUG_SHIFT) |
		(0 << CS42L42_TS_UNPLUG_SHIFT));
}

static void cs42l42_init_hs_type_detect(struct cs42l42_private *cs42l42)
{
	/* Mask tip sense interrupts */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_TSRS_PLUG_INT_MASK,
				CS42L42_TS_PLUG_MASK |
				CS42L42_TS_UNPLUG_MASK,
				(1 << CS42L42_TS_PLUG_SHIFT) |
				(1 << CS42L42_TS_UNPLUG_SHIFT));

	/* Make sure button detect and HS bias circuits are off */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_MISC_DET_CTL,
				CS42L42_HSBIAS_CTL_MASK |
				CS42L42_PDN_MIC_LVL_DET_MASK,
				(1 << CS42L42_HSBIAS_CTL_SHIFT) |
				(1 << CS42L42_PDN_MIC_LVL_DET_SHIFT));

	/* Set auto HS bias settings to default */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_HSBIAS_SC_AUTOCTL,
				CS42L42_HSBIAS_SENSE_EN_MASK |
				CS42L42_AUTO_HSBIAS_HIZ_MASK |
				CS42L42_TIP_SENSE_EN_MASK |
				CS42L42_HSBIAS_SENSE_TRIP_MASK,
				(0 << CS42L42_HSBIAS_SENSE_EN_SHIFT) |
				(0 << CS42L42_AUTO_HSBIAS_HIZ_SHIFT) |
				(0 << CS42L42_TIP_SENSE_EN_SHIFT) |
				(3 << CS42L42_HSBIAS_SENSE_TRIP_SHIFT));

	/* Set hs detect to manual, disabled mode */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_HSDET_CTL2,
				CS42L42_HSDET_CTRL_MASK |
				CS42L42_HSDET_SET_MASK |
				CS42L42_HSBIAS_REF_MASK |
				CS42L42_HSDET_AUTO_TIME_MASK,
				(0 << CS42L42_HSDET_CTRL_SHIFT) |
				(2 << CS42L42_HSDET_SET_SHIFT) |
				(0 << CS42L42_HSBIAS_REF_SHIFT) |
				(3 << CS42L42_HSDET_AUTO_TIME_SHIFT));

	regmap_update_bits(cs42l42->regmap,
				CS42L42_DAC_CTL2,
				CS42L42_HPOUT_PULLDOWN_MASK |
				CS42L42_HPOUT_LOAD_MASK |
				CS42L42_HPOUT_CLAMP_MASK |
				CS42L42_DAC_HPF_EN_MASK |
				CS42L42_DAC_MON_EN_MASK,
				(8 << CS42L42_HPOUT_PULLDOWN_SHIFT) |
				(0 << CS42L42_HPOUT_LOAD_SHIFT) |
				(1 << CS42L42_HPOUT_CLAMP_SHIFT) |
				(1 << CS42L42_DAC_HPF_EN_SHIFT) |
				(1 << CS42L42_DAC_MON_EN_SHIFT));

	/* Power up HS bias to 2.7V */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_MISC_DET_CTL,
				CS42L42_HSBIAS_CTL_MASK |
				CS42L42_PDN_MIC_LVL_DET_MASK,
				(3 << CS42L42_HSBIAS_CTL_SHIFT) |
				(1 << CS42L42_PDN_MIC_LVL_DET_SHIFT));

	/* Wait for HS bias to ramp up */
	msleep(cs42l42->hs_bias_ramp_time);

	/* Unmask auto detect interrupt */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_CODEC_INT_MASK,
				CS42L42_PDN_DONE_MASK |
				CS42L42_HSDET_AUTO_DONE_MASK,
				(1 << CS42L42_PDN_DONE_SHIFT) |
				(0 << CS42L42_HSDET_AUTO_DONE_SHIFT));

	/* Set hs detect to automatic, enabled mode */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_HSDET_CTL2,
				CS42L42_HSDET_CTRL_MASK |
				CS42L42_HSDET_SET_MASK |
				CS42L42_HSBIAS_REF_MASK |
				CS42L42_HSDET_AUTO_TIME_MASK,
				(3 << CS42L42_HSDET_CTRL_SHIFT) |
				(2 << CS42L42_HSDET_SET_SHIFT) |
				(0 << CS42L42_HSBIAS_REF_SHIFT) |
				(3 << CS42L42_HSDET_AUTO_TIME_SHIFT));
}

static void cs42l42_cancel_hs_type_detect(struct cs42l42_private *cs42l42)
{
	/* Mask button detect interrupts */
	regmap_update_bits(cs42l42->regmap,
		CS42L42_DET_INT2_MASK,
		CS42L42_M_DETECT_TF_MASK |
		CS42L42_M_DETECT_FT_MASK |
		CS42L42_M_HSBIAS_HIZ_MASK |
		CS42L42_M_SHORT_RLS_MASK |
		CS42L42_M_SHORT_DET_MASK,
		(1 << CS42L42_M_DETECT_TF_SHIFT) |
		(1 << CS42L42_M_DETECT_FT_SHIFT) |
		(1 << CS42L42_M_HSBIAS_HIZ_SHIFT) |
		(1 << CS42L42_M_SHORT_RLS_SHIFT) |
		(1 << CS42L42_M_SHORT_DET_SHIFT));

	/* Ground HS bias */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_MISC_DET_CTL,
				CS42L42_HSBIAS_CTL_MASK |
				CS42L42_PDN_MIC_LVL_DET_MASK,
				(1 << CS42L42_HSBIAS_CTL_SHIFT) |
				(1 << CS42L42_PDN_MIC_LVL_DET_SHIFT));

	/* Set auto HS bias settings to default */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_HSBIAS_SC_AUTOCTL,
				CS42L42_HSBIAS_SENSE_EN_MASK |
				CS42L42_AUTO_HSBIAS_HIZ_MASK |
				CS42L42_TIP_SENSE_EN_MASK |
				CS42L42_HSBIAS_SENSE_TRIP_MASK,
				(0 << CS42L42_HSBIAS_SENSE_EN_SHIFT) |
				(0 << CS42L42_AUTO_HSBIAS_HIZ_SHIFT) |
				(0 << CS42L42_TIP_SENSE_EN_SHIFT) |
				(3 << CS42L42_HSBIAS_SENSE_TRIP_SHIFT));

	/* Set hs detect to manual, disabled mode */
	regmap_update_bits(cs42l42->regmap,
				CS42L42_HSDET_CTL2,
				CS42L42_HSDET_CTRL_MASK |
				CS42L42_HSDET_SET_MASK |
				CS42L42_HSBIAS_REF_MASK |
				CS42L42_HSDET_AUTO_TIME_MASK,
				(0 << CS42L42_HSDET_CTRL_SHIFT) |
				(2 << CS42L42_HSDET_SET_SHIFT) |
				(0 << CS42L42_HSBIAS_REF_SHIFT) |
				(3 << CS42L42_HSDET_AUTO_TIME_SHIFT));
}

static int cs42l42_handle_button_press(struct cs42l42_private *cs42l42)
{
	int bias_level;
	unsigned int detect_status;

	/* Mask button detect interrupts */
	regmap_update_bits(cs42l42->regmap,
		CS42L42_DET_INT2_MASK,
		CS42L42_M_DETECT_TF_MASK |
		CS42L42_M_DETECT_FT_MASK |
		CS42L42_M_HSBIAS_HIZ_MASK |
		CS42L42_M_SHORT_RLS_MASK |
		CS42L42_M_SHORT_DET_MASK,
		(1 << CS42L42_M_DETECT_TF_SHIFT) |
		(1 << CS42L42_M_DETECT_FT_SHIFT) |
		(1 << CS42L42_M_HSBIAS_HIZ_SHIFT) |
		(1 << CS42L42_M_SHORT_RLS_SHIFT) |
		(1 << CS42L42_M_SHORT_DET_SHIFT));

	usleep_range(cs42l42->btn_det_event_dbnce * 1000,
		     cs42l42->btn_det_event_dbnce * 2000);

	/* Test all 4 level detect biases */
	bias_level = 1;
	do {
		/* Adjust button detect level sensitivity */
		regmap_update_bits(cs42l42->regmap,
			CS42L42_MIC_DET_CTL1,
			CS42L42_LATCH_TO_VP_MASK |
			CS42L42_EVENT_STAT_SEL_MASK |
			CS42L42_HS_DET_LEVEL_MASK,
			(1 << CS42L42_LATCH_TO_VP_SHIFT) |
			(0 << CS42L42_EVENT_STAT_SEL_SHIFT) |
			(cs42l42->bias_thresholds[bias_level] <<
			CS42L42_HS_DET_LEVEL_SHIFT));

		regmap_read(cs42l42->regmap, CS42L42_DET_STATUS2,
				&detect_status);
	} while ((detect_status & CS42L42_HS_TRUE_MASK) &&
		(++bias_level < CS42L42_NUM_BIASES));

	switch (bias_level) {
	case 1: /* Function C button press */
		bias_level = SND_JACK_BTN_2;
		dev_dbg(cs42l42->dev, "Function C button press\n");
		break;
	case 2: /* Function B button press */
		bias_level = SND_JACK_BTN_1;
		dev_dbg(cs42l42->dev, "Function B button press\n");
		break;
	case 3: /* Function D button press */
		bias_level = SND_JACK_BTN_3;
		dev_dbg(cs42l42->dev, "Function D button press\n");
		break;
	case 4: /* Function A button press */
		bias_level = SND_JACK_BTN_0;
		dev_dbg(cs42l42->dev, "Function A button press\n");
		break;
	default:
		bias_level = 0;
		break;
	}

	/* Set button detect level sensitivity back to default */
	regmap_update_bits(cs42l42->regmap,
		CS42L42_MIC_DET_CTL1,
		CS42L42_LATCH_TO_VP_MASK |
		CS42L42_EVENT_STAT_SEL_MASK |
		CS42L42_HS_DET_LEVEL_MASK,
		(1 << CS42L42_LATCH_TO_VP_SHIFT) |
		(0 << CS42L42_EVENT_STAT_SEL_SHIFT) |
		(cs42l42->bias_thresholds[0] << CS42L42_HS_DET_LEVEL_SHIFT));

	/* Clear any button interrupts before unmasking them */
	regmap_read(cs42l42->regmap, CS42L42_DET_INT_STATUS2,
		    &detect_status);

	/* Unmask button detect interrupts */
	regmap_update_bits(cs42l42->regmap,
		CS42L42_DET_INT2_MASK,
		CS42L42_M_DETECT_TF_MASK |
		CS42L42_M_DETECT_FT_MASK |
		CS42L42_M_HSBIAS_HIZ_MASK |
		CS42L42_M_SHORT_RLS_MASK |
		CS42L42_M_SHORT_DET_MASK,
		(0 << CS42L42_M_DETECT_TF_SHIFT) |
		(0 << CS42L42_M_DETECT_FT_SHIFT) |
		(0 << CS42L42_M_HSBIAS_HIZ_SHIFT) |
		(1 << CS42L42_M_SHORT_RLS_SHIFT) |
		(1 << CS42L42_M_SHORT_DET_SHIFT));

	return bias_level;
}

struct cs42l42_irq_params {
	u16 status_addr;
	u16 mask_addr;
	u8 mask;
};

static const struct cs42l42_irq_params irq_params_table[] = {
	{CS42L42_ADC_OVFL_STATUS, CS42L42_ADC_OVFL_INT_MASK,
		CS42L42_ADC_OVFL_VAL_MASK},
	{CS42L42_MIXER_STATUS, CS42L42_MIXER_INT_MASK,
		CS42L42_MIXER_VAL_MASK},
	{CS42L42_SRC_STATUS, CS42L42_SRC_INT_MASK,
		CS42L42_SRC_VAL_MASK},
	{CS42L42_ASP_RX_STATUS, CS42L42_ASP_RX_INT_MASK,
		CS42L42_ASP_RX_VAL_MASK},
	{CS42L42_ASP_TX_STATUS, CS42L42_ASP_TX_INT_MASK,
		CS42L42_ASP_TX_VAL_MASK},
	{CS42L42_CODEC_STATUS, CS42L42_CODEC_INT_MASK,
		CS42L42_CODEC_VAL_MASK},
	{CS42L42_DET_INT_STATUS1, CS42L42_DET_INT1_MASK,
		CS42L42_DET_INT_VAL1_MASK},
	{CS42L42_DET_INT_STATUS2, CS42L42_DET_INT2_MASK,
		CS42L42_DET_INT_VAL2_MASK},
	{CS42L42_SRCPL_INT_STATUS, CS42L42_SRCPL_INT_MASK,
		CS42L42_SRCPL_VAL_MASK},
	{CS42L42_VPMON_STATUS, CS42L42_VPMON_INT_MASK,
		CS42L42_VPMON_VAL_MASK},
	{CS42L42_PLL_LOCK_STATUS, CS42L42_PLL_LOCK_INT_MASK,
		CS42L42_PLL_LOCK_VAL_MASK},
	{CS42L42_TSRS_PLUG_STATUS, CS42L42_TSRS_PLUG_INT_MASK,
		CS42L42_TSRS_PLUG_VAL_MASK}
};

irqreturn_t cs42l42_irq_thread(int irq, void *data)
{
	struct cs42l42_private *cs42l42 = (struct cs42l42_private *)data;
	unsigned int stickies[12];
	unsigned int masks[12];
	unsigned int current_plug_status;
	unsigned int current_button_status;
	unsigned int i;

	pm_runtime_get_sync(cs42l42->dev);
	mutex_lock(&cs42l42->irq_lock);
	if (cs42l42->suspended || !cs42l42->init_done) {
		mutex_unlock(&cs42l42->irq_lock);
		pm_runtime_put_autosuspend(cs42l42->dev);
		return IRQ_NONE;
	}

	/* Read sticky registers to clear interurpt */
	for (i = 0; i < ARRAY_SIZE(stickies); i++) {
		regmap_read(cs42l42->regmap, irq_params_table[i].status_addr,
				&(stickies[i]));
		regmap_read(cs42l42->regmap, irq_params_table[i].mask_addr,
				&(masks[i]));
		stickies[i] = stickies[i] & (~masks[i]) &
				irq_params_table[i].mask;
	}

	/* Read tip sense status before handling type detect */
	current_plug_status = (stickies[11] &
		(CS42L42_TS_PLUG_MASK | CS42L42_TS_UNPLUG_MASK)) >>
		CS42L42_TS_PLUG_SHIFT;

	/* Read button sense status */
	current_button_status = stickies[7] &
		(CS42L42_M_DETECT_TF_MASK |
		CS42L42_M_DETECT_FT_MASK |
		CS42L42_M_HSBIAS_HIZ_MASK);

	/*
	 * Check auto-detect status. Don't assume a previous unplug event has
	 * cleared the flags. If the jack is unplugged and plugged during
	 * system suspend there won't have been an unplug event.
	 */
	if ((~masks[5]) & irq_params_table[5].mask) {
		if (stickies[5] & CS42L42_HSDET_AUTO_DONE_MASK) {
			cs42l42_process_hs_type_detect(cs42l42);
			switch (cs42l42->hs_type) {
			case CS42L42_PLUG_CTIA:
			case CS42L42_PLUG_OMTP:
				snd_soc_jack_report(cs42l42->jack, SND_JACK_HEADSET,
						    SND_JACK_HEADSET |
						    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
						    SND_JACK_BTN_2 | SND_JACK_BTN_3);
				break;
			case CS42L42_PLUG_HEADPHONE:
				snd_soc_jack_report(cs42l42->jack, SND_JACK_HEADPHONE,
						    SND_JACK_HEADSET |
						    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
						    SND_JACK_BTN_2 | SND_JACK_BTN_3);
				break;
			default:
				break;
			}
			dev_dbg(cs42l42->dev, "Auto detect done (%d)\n", cs42l42->hs_type);
		}
	}

	/* Check tip sense status */
	if ((~masks[11]) & irq_params_table[11].mask) {
		switch (current_plug_status) {
		case CS42L42_TS_PLUG:
			if (cs42l42->plug_state != CS42L42_TS_PLUG) {
				cs42l42->plug_state = CS42L42_TS_PLUG;
				cs42l42_init_hs_type_detect(cs42l42);
			}
			break;

		case CS42L42_TS_UNPLUG:
			if (cs42l42->plug_state != CS42L42_TS_UNPLUG) {
				cs42l42->plug_state = CS42L42_TS_UNPLUG;
				cs42l42_cancel_hs_type_detect(cs42l42);

				snd_soc_jack_report(cs42l42->jack, 0,
						    SND_JACK_HEADSET |
						    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
						    SND_JACK_BTN_2 | SND_JACK_BTN_3);

				dev_dbg(cs42l42->dev, "Unplug event\n");
			}
			break;

		default:
			cs42l42->plug_state = CS42L42_TS_TRANS;
		}
	}

	/* Check button detect status */
	if (cs42l42->plug_state == CS42L42_TS_PLUG && ((~masks[7]) & irq_params_table[7].mask)) {
		if (!(current_button_status &
			CS42L42_M_HSBIAS_HIZ_MASK)) {

			if (current_button_status & CS42L42_M_DETECT_TF_MASK) {
				dev_dbg(cs42l42->dev, "Button released\n");
				snd_soc_jack_report(cs42l42->jack, 0,
						    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
						    SND_JACK_BTN_2 | SND_JACK_BTN_3);
			} else if (current_button_status & CS42L42_M_DETECT_FT_MASK) {
				snd_soc_jack_report(cs42l42->jack,
						    cs42l42_handle_button_press(cs42l42),
						    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
						    SND_JACK_BTN_2 | SND_JACK_BTN_3);
			}
		}
	}

	mutex_unlock(&cs42l42->irq_lock);
	pm_runtime_mark_last_busy(cs42l42->dev);
	pm_runtime_put_autosuspend(cs42l42->dev);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_NS_GPL(cs42l42_irq_thread, SND_SOC_CS42L42_CORE);

static void cs42l42_set_interrupt_masks(struct cs42l42_private *cs42l42)
{
	regmap_update_bits(cs42l42->regmap, CS42L42_ADC_OVFL_INT_MASK,
			CS42L42_ADC_OVFL_MASK,
			(1 << CS42L42_ADC_OVFL_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_MIXER_INT_MASK,
			CS42L42_MIX_CHB_OVFL_MASK |
			CS42L42_MIX_CHA_OVFL_MASK |
			CS42L42_EQ_OVFL_MASK |
			CS42L42_EQ_BIQUAD_OVFL_MASK,
			(1 << CS42L42_MIX_CHB_OVFL_SHIFT) |
			(1 << CS42L42_MIX_CHA_OVFL_SHIFT) |
			(1 << CS42L42_EQ_OVFL_SHIFT) |
			(1 << CS42L42_EQ_BIQUAD_OVFL_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_SRC_INT_MASK,
			CS42L42_SRC_ILK_MASK |
			CS42L42_SRC_OLK_MASK |
			CS42L42_SRC_IUNLK_MASK |
			CS42L42_SRC_OUNLK_MASK,
			(1 << CS42L42_SRC_ILK_SHIFT) |
			(1 << CS42L42_SRC_OLK_SHIFT) |
			(1 << CS42L42_SRC_IUNLK_SHIFT) |
			(1 << CS42L42_SRC_OUNLK_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_ASP_RX_INT_MASK,
			CS42L42_ASPRX_NOLRCK_MASK |
			CS42L42_ASPRX_EARLY_MASK |
			CS42L42_ASPRX_LATE_MASK |
			CS42L42_ASPRX_ERROR_MASK |
			CS42L42_ASPRX_OVLD_MASK,
			(1 << CS42L42_ASPRX_NOLRCK_SHIFT) |
			(1 << CS42L42_ASPRX_EARLY_SHIFT) |
			(1 << CS42L42_ASPRX_LATE_SHIFT) |
			(1 << CS42L42_ASPRX_ERROR_SHIFT) |
			(1 << CS42L42_ASPRX_OVLD_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_ASP_TX_INT_MASK,
			CS42L42_ASPTX_NOLRCK_MASK |
			CS42L42_ASPTX_EARLY_MASK |
			CS42L42_ASPTX_LATE_MASK |
			CS42L42_ASPTX_SMERROR_MASK,
			(1 << CS42L42_ASPTX_NOLRCK_SHIFT) |
			(1 << CS42L42_ASPTX_EARLY_SHIFT) |
			(1 << CS42L42_ASPTX_LATE_SHIFT) |
			(1 << CS42L42_ASPTX_SMERROR_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_CODEC_INT_MASK,
			CS42L42_PDN_DONE_MASK |
			CS42L42_HSDET_AUTO_DONE_MASK,
			(1 << CS42L42_PDN_DONE_SHIFT) |
			(1 << CS42L42_HSDET_AUTO_DONE_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_SRCPL_INT_MASK,
			CS42L42_SRCPL_ADC_LK_MASK |
			CS42L42_SRCPL_DAC_LK_MASK |
			CS42L42_SRCPL_ADC_UNLK_MASK |
			CS42L42_SRCPL_DAC_UNLK_MASK,
			(1 << CS42L42_SRCPL_ADC_LK_SHIFT) |
			(1 << CS42L42_SRCPL_DAC_LK_SHIFT) |
			(1 << CS42L42_SRCPL_ADC_UNLK_SHIFT) |
			(1 << CS42L42_SRCPL_DAC_UNLK_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_DET_INT1_MASK,
			CS42L42_TIP_SENSE_UNPLUG_MASK |
			CS42L42_TIP_SENSE_PLUG_MASK |
			CS42L42_HSBIAS_SENSE_MASK,
			(1 << CS42L42_TIP_SENSE_UNPLUG_SHIFT) |
			(1 << CS42L42_TIP_SENSE_PLUG_SHIFT) |
			(1 << CS42L42_HSBIAS_SENSE_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_DET_INT2_MASK,
			CS42L42_M_DETECT_TF_MASK |
			CS42L42_M_DETECT_FT_MASK |
			CS42L42_M_HSBIAS_HIZ_MASK |
			CS42L42_M_SHORT_RLS_MASK |
			CS42L42_M_SHORT_DET_MASK,
			(1 << CS42L42_M_DETECT_TF_SHIFT) |
			(1 << CS42L42_M_DETECT_FT_SHIFT) |
			(1 << CS42L42_M_HSBIAS_HIZ_SHIFT) |
			(1 << CS42L42_M_SHORT_RLS_SHIFT) |
			(1 << CS42L42_M_SHORT_DET_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_VPMON_INT_MASK,
			CS42L42_VPMON_MASK,
			(1 << CS42L42_VPMON_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_PLL_LOCK_INT_MASK,
			CS42L42_PLL_LOCK_MASK,
			(1 << CS42L42_PLL_LOCK_SHIFT));

	regmap_update_bits(cs42l42->regmap, CS42L42_TSRS_PLUG_INT_MASK,
			CS42L42_RS_PLUG_MASK |
			CS42L42_RS_UNPLUG_MASK |
			CS42L42_TS_PLUG_MASK |
			CS42L42_TS_UNPLUG_MASK,
			(1 << CS42L42_RS_PLUG_SHIFT) |
			(1 << CS42L42_RS_UNPLUG_SHIFT) |
			(0 << CS42L42_TS_PLUG_SHIFT) |
			(0 << CS42L42_TS_UNPLUG_SHIFT));
}

static void cs42l42_setup_hs_type_detect(struct cs42l42_private *cs42l42)
{
	unsigned int reg;

	cs42l42->hs_type = CS42L42_PLUG_INVALID;

	/*
	 * DETECT_MODE must always be 0 with ADC and HP both off otherwise the
	 * FILT+ supply will not charge properly.
	 */
	regmap_update_bits(cs42l42->regmap, CS42L42_MISC_DET_CTL,
			   CS42L42_DETECT_MODE_MASK, 0);

	/* Latch analog controls to VP power domain */
	regmap_update_bits(cs42l42->regmap, CS42L42_MIC_DET_CTL1,
			CS42L42_LATCH_TO_VP_MASK |
			CS42L42_EVENT_STAT_SEL_MASK |
			CS42L42_HS_DET_LEVEL_MASK,
			(1 << CS42L42_LATCH_TO_VP_SHIFT) |
			(0 << CS42L42_EVENT_STAT_SEL_SHIFT) |
			(cs42l42->bias_thresholds[0] <<
			CS42L42_HS_DET_LEVEL_SHIFT));

	/* Remove ground noise-suppression clamps */
	regmap_update_bits(cs42l42->regmap,
			CS42L42_HS_CLAMP_DISABLE,
			CS42L42_HS_CLAMP_DISABLE_MASK,
			(1 << CS42L42_HS_CLAMP_DISABLE_SHIFT));

	/* Enable the tip sense circuit */
	regmap_update_bits(cs42l42->regmap, CS42L42_TSENSE_CTL,
			   CS42L42_TS_INV_MASK, CS42L42_TS_INV_MASK);

	regmap_update_bits(cs42l42->regmap, CS42L42_TIPSENSE_CTL,
			CS42L42_TIP_SENSE_CTRL_MASK |
			CS42L42_TIP_SENSE_INV_MASK |
			CS42L42_TIP_SENSE_DEBOUNCE_MASK,
			(3 << CS42L42_TIP_SENSE_CTRL_SHIFT) |
			(!cs42l42->ts_inv << CS42L42_TIP_SENSE_INV_SHIFT) |
			(2 << CS42L42_TIP_SENSE_DEBOUNCE_SHIFT));

	/* Save the initial status of the tip sense */
	regmap_read(cs42l42->regmap,
			  CS42L42_TSRS_PLUG_STATUS,
			  &reg);
	cs42l42->plug_state = (((char) reg) &
		      (CS42L42_TS_PLUG_MASK | CS42L42_TS_UNPLUG_MASK)) >>
		      CS42L42_TS_PLUG_SHIFT;
}

static const unsigned int threshold_defaults[] = {
	CS42L42_HS_DET_LEVEL_15,
	CS42L42_HS_DET_LEVEL_8,
	CS42L42_HS_DET_LEVEL_4,
	CS42L42_HS_DET_LEVEL_1
};

static int cs42l42_handle_device_data(struct device *dev,
					struct cs42l42_private *cs42l42)
{
	unsigned int val;
	u32 thresholds[CS42L42_NUM_BIASES];
	int ret;
	int i;

	ret = device_property_read_u32(dev, "cirrus,ts-inv", &val);
	if (!ret) {
		switch (val) {
		case CS42L42_TS_INV_EN:
		case CS42L42_TS_INV_DIS:
			cs42l42->ts_inv = val;
			break;
		default:
			dev_err(dev,
				"Wrong cirrus,ts-inv DT value %d\n",
				val);
			cs42l42->ts_inv = CS42L42_TS_INV_DIS;
		}
	} else {
		cs42l42->ts_inv = CS42L42_TS_INV_DIS;
	}

	ret = device_property_read_u32(dev, "cirrus,ts-dbnc-rise", &val);
	if (!ret) {
		switch (val) {
		case CS42L42_TS_DBNCE_0:
		case CS42L42_TS_DBNCE_125:
		case CS42L42_TS_DBNCE_250:
		case CS42L42_TS_DBNCE_500:
		case CS42L42_TS_DBNCE_750:
		case CS42L42_TS_DBNCE_1000:
		case CS42L42_TS_DBNCE_1250:
		case CS42L42_TS_DBNCE_1500:
			cs42l42->ts_dbnc_rise = val;
			break;
		default:
			dev_err(dev,
				"Wrong cirrus,ts-dbnc-rise DT value %d\n",
				val);
			cs42l42->ts_dbnc_rise = CS42L42_TS_DBNCE_1000;
		}
	} else {
		cs42l42->ts_dbnc_rise = CS42L42_TS_DBNCE_1000;
	}

	regmap_update_bits(cs42l42->regmap, CS42L42_TSENSE_CTL,
			CS42L42_TS_RISE_DBNCE_TIME_MASK,
			(cs42l42->ts_dbnc_rise <<
			CS42L42_TS_RISE_DBNCE_TIME_SHIFT));

	ret = device_property_read_u32(dev, "cirrus,ts-dbnc-fall", &val);
	if (!ret) {
		switch (val) {
		case CS42L42_TS_DBNCE_0:
		case CS42L42_TS_DBNCE_125:
		case CS42L42_TS_DBNCE_250:
		case CS42L42_TS_DBNCE_500:
		case CS42L42_TS_DBNCE_750:
		case CS42L42_TS_DBNCE_1000:
		case CS42L42_TS_DBNCE_1250:
		case CS42L42_TS_DBNCE_1500:
			cs42l42->ts_dbnc_fall = val;
			break;
		default:
			dev_err(dev,
				"Wrong cirrus,ts-dbnc-fall DT value %d\n",
				val);
			cs42l42->ts_dbnc_fall = CS42L42_TS_DBNCE_0;
		}
	} else {
		cs42l42->ts_dbnc_fall = CS42L42_TS_DBNCE_0;
	}

	regmap_update_bits(cs42l42->regmap, CS42L42_TSENSE_CTL,
			CS42L42_TS_FALL_DBNCE_TIME_MASK,
			(cs42l42->ts_dbnc_fall <<
			CS42L42_TS_FALL_DBNCE_TIME_SHIFT));

	ret = device_property_read_u32(dev, "cirrus,btn-det-init-dbnce", &val);
	if (!ret) {
		if (val <= CS42L42_BTN_DET_INIT_DBNCE_MAX)
			cs42l42->btn_det_init_dbnce = val;
		else {
			dev_err(dev,
				"Wrong cirrus,btn-det-init-dbnce DT value %d\n",
				val);
			cs42l42->btn_det_init_dbnce =
				CS42L42_BTN_DET_INIT_DBNCE_DEFAULT;
		}
	} else {
		cs42l42->btn_det_init_dbnce =
			CS42L42_BTN_DET_INIT_DBNCE_DEFAULT;
	}

	ret = device_property_read_u32(dev, "cirrus,btn-det-event-dbnce", &val);
	if (!ret) {
		if (val <= CS42L42_BTN_DET_EVENT_DBNCE_MAX)
			cs42l42->btn_det_event_dbnce = val;
		else {
			dev_err(dev,
				"Wrong cirrus,btn-det-event-dbnce DT value %d\n", val);
			cs42l42->btn_det_event_dbnce =
				CS42L42_BTN_DET_EVENT_DBNCE_DEFAULT;
		}
	} else {
		cs42l42->btn_det_event_dbnce =
			CS42L42_BTN_DET_EVENT_DBNCE_DEFAULT;
	}

	ret = device_property_read_u32_array(dev, "cirrus,bias-lvls",
					     thresholds, ARRAY_SIZE(thresholds));
	if (!ret) {
		for (i = 0; i < CS42L42_NUM_BIASES; i++) {
			if (thresholds[i] <= CS42L42_HS_DET_LEVEL_MAX)
				cs42l42->bias_thresholds[i] = thresholds[i];
			else {
				dev_err(dev,
					"Wrong cirrus,bias-lvls[%d] DT value %d\n", i,
					thresholds[i]);
				cs42l42->bias_thresholds[i] = threshold_defaults[i];
			}
		}
	} else {
		for (i = 0; i < CS42L42_NUM_BIASES; i++)
			cs42l42->bias_thresholds[i] = threshold_defaults[i];
	}

	ret = device_property_read_u32(dev, "cirrus,hs-bias-ramp-rate", &val);
	if (!ret) {
		switch (val) {
		case CS42L42_HSBIAS_RAMP_FAST_RISE_SLOW_FALL:
			cs42l42->hs_bias_ramp_rate = val;
			cs42l42->hs_bias_ramp_time = CS42L42_HSBIAS_RAMP_TIME0;
			break;
		case CS42L42_HSBIAS_RAMP_FAST:
			cs42l42->hs_bias_ramp_rate = val;
			cs42l42->hs_bias_ramp_time = CS42L42_HSBIAS_RAMP_TIME1;
			break;
		case CS42L42_HSBIAS_RAMP_SLOW:
			cs42l42->hs_bias_ramp_rate = val;
			cs42l42->hs_bias_ramp_time = CS42L42_HSBIAS_RAMP_TIME2;
			break;
		case CS42L42_HSBIAS_RAMP_SLOWEST:
			cs42l42->hs_bias_ramp_rate = val;
			cs42l42->hs_bias_ramp_time = CS42L42_HSBIAS_RAMP_TIME3;
			break;
		default:
			dev_err(dev,
				"Wrong cirrus,hs-bias-ramp-rate DT value %d\n",
				val);
			cs42l42->hs_bias_ramp_rate = CS42L42_HSBIAS_RAMP_SLOW;
			cs42l42->hs_bias_ramp_time = CS42L42_HSBIAS_RAMP_TIME2;
		}
	} else {
		cs42l42->hs_bias_ramp_rate = CS42L42_HSBIAS_RAMP_SLOW;
		cs42l42->hs_bias_ramp_time = CS42L42_HSBIAS_RAMP_TIME2;
	}

	regmap_update_bits(cs42l42->regmap, CS42L42_HS_BIAS_CTL,
			CS42L42_HSBIAS_RAMP_MASK,
			(cs42l42->hs_bias_ramp_rate <<
			CS42L42_HSBIAS_RAMP_SHIFT));

	if (device_property_read_bool(dev, "cirrus,hs-bias-sense-disable"))
		cs42l42->hs_bias_sense_en = 0;
	else
		cs42l42->hs_bias_sense_en = 1;

	return 0;
}

/* Datasheet suspend sequence */
static const struct reg_sequence __maybe_unused cs42l42_shutdown_seq[] = {
	REG_SEQ0(CS42L42_MIC_DET_CTL1,		0x9F),
	REG_SEQ0(CS42L42_ADC_OVFL_INT_MASK,	0x01),
	REG_SEQ0(CS42L42_MIXER_INT_MASK,	0x0F),
	REG_SEQ0(CS42L42_SRC_INT_MASK,		0x0F),
	REG_SEQ0(CS42L42_ASP_RX_INT_MASK,	0x1F),
	REG_SEQ0(CS42L42_ASP_TX_INT_MASK,	0x0F),
	REG_SEQ0(CS42L42_CODEC_INT_MASK,	0x03),
	REG_SEQ0(CS42L42_SRCPL_INT_MASK,	0x7F),
	REG_SEQ0(CS42L42_VPMON_INT_MASK,	0x01),
	REG_SEQ0(CS42L42_PLL_LOCK_INT_MASK,	0x01),
	REG_SEQ0(CS42L42_TSRS_PLUG_INT_MASK,	0x0F),
	REG_SEQ0(CS42L42_WAKE_CTL,		0xE1),
	REG_SEQ0(CS42L42_DET_INT1_MASK,		0xE0),
	REG_SEQ0(CS42L42_DET_INT2_MASK,		0xFF),
	REG_SEQ0(CS42L42_MIXER_CHA_VOL,		0x3F),
	REG_SEQ0(CS42L42_MIXER_ADC_VOL,		0x3F),
	REG_SEQ0(CS42L42_MIXER_CHB_VOL,		0x3F),
	REG_SEQ0(CS42L42_HP_CTL,		0x0F),
	REG_SEQ0(CS42L42_ASP_RX_DAI0_EN,	0x00),
	REG_SEQ0(CS42L42_ASP_CLK_CFG,		0x00),
	REG_SEQ0(CS42L42_HSDET_CTL2,		0x00),
	REG_SEQ0(CS42L42_PWR_CTL1,		0xFE),
	REG_SEQ0(CS42L42_PWR_CTL2,		0x8C),
	REG_SEQ0(CS42L42_DAC_CTL2,		0x02),
	REG_SEQ0(CS42L42_HS_CLAMP_DISABLE,	0x00),
	REG_SEQ0(CS42L42_MISC_DET_CTL,		0x03),
	REG_SEQ0(CS42L42_TIPSENSE_CTL,		0x02),
	REG_SEQ0(CS42L42_HSBIAS_SC_AUTOCTL,	0x03),
	REG_SEQ0(CS42L42_PWR_CTL1,		0xFF)
};

int cs42l42_suspend(struct device *dev)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(dev);
	unsigned int reg;
	u8 save_regs[ARRAY_SIZE(cs42l42_shutdown_seq)];
	int i, ret;

	if (!cs42l42->init_done)
		return 0;

	/*
	 * Wait for threaded irq handler to be idle and stop it processing
	 * future interrupts. This ensures a safe disable if the interrupt
	 * is shared.
	 */
	mutex_lock(&cs42l42->irq_lock);
	cs42l42->suspended = true;

	/* Save register values that will be overwritten by shutdown sequence */
	for (i = 0; i < ARRAY_SIZE(cs42l42_shutdown_seq); ++i) {
		regmap_read(cs42l42->regmap, cs42l42_shutdown_seq[i].reg, &reg);
		save_regs[i] = (u8)reg;
	}

	/* Shutdown codec */
	regmap_multi_reg_write(cs42l42->regmap,
			       cs42l42_shutdown_seq,
			       ARRAY_SIZE(cs42l42_shutdown_seq));

	/* All interrupt sources are now disabled */
	mutex_unlock(&cs42l42->irq_lock);

	/* Wait for power-down complete */
	msleep(CS42L42_PDN_DONE_TIME_MS);
	ret = regmap_read_poll_timeout(cs42l42->regmap,
				       CS42L42_CODEC_STATUS, reg,
				       (reg & CS42L42_PDN_DONE_MASK),
				       CS42L42_PDN_DONE_POLL_US,
				       CS42L42_PDN_DONE_TIMEOUT_US);
	if (ret)
		dev_warn(dev, "Failed to get PDN_DONE: %d\n", ret);

	/* Discharge FILT+ */
	regmap_update_bits(cs42l42->regmap, CS42L42_PWR_CTL2,
			   CS42L42_DISCHARGE_FILT_MASK, CS42L42_DISCHARGE_FILT_MASK);
	msleep(CS42L42_FILT_DISCHARGE_TIME_MS);

	regcache_cache_only(cs42l42->regmap, true);
	gpiod_set_value_cansleep(cs42l42->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(cs42l42->supplies), cs42l42->supplies);

	/* Restore register values to the regmap cache */
	for (i = 0; i < ARRAY_SIZE(cs42l42_shutdown_seq); ++i)
		regmap_write(cs42l42->regmap, cs42l42_shutdown_seq[i].reg, save_regs[i]);

	/* The cached address page register value is now stale */
	regcache_drop_region(cs42l42->regmap, CS42L42_PAGE_REGISTER, CS42L42_PAGE_REGISTER);

	dev_dbg(dev, "System suspended\n");

	return 0;

}
EXPORT_SYMBOL_NS_GPL(cs42l42_suspend, SND_SOC_CS42L42_CORE);

int cs42l42_resume(struct device *dev)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(dev);
	int ret;

	if (!cs42l42->init_done)
		return 0;

	/*
	 * If jack was unplugged and re-plugged during suspend it could
	 * have changed type but the tip-sense state hasn't changed.
	 * Force a plugged state to be re-evaluated.
	 */
	if (cs42l42->plug_state != CS42L42_TS_UNPLUG)
		cs42l42->plug_state = CS42L42_TS_TRANS;

	ret = regulator_bulk_enable(ARRAY_SIZE(cs42l42->supplies), cs42l42->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(cs42l42->reset_gpio, 1);
	usleep_range(CS42L42_BOOT_TIME_US, CS42L42_BOOT_TIME_US * 2);

	dev_dbg(dev, "System resume powered up\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs42l42_resume, SND_SOC_CS42L42_CORE);

void cs42l42_resume_restore(struct device *dev)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(dev);

	regcache_cache_only(cs42l42->regmap, false);
	regcache_mark_dirty(cs42l42->regmap);

	mutex_lock(&cs42l42->irq_lock);
	/* Sync LATCH_TO_VP first so the VP domain registers sync correctly */
	regcache_sync_region(cs42l42->regmap, CS42L42_MIC_DET_CTL1, CS42L42_MIC_DET_CTL1);
	regcache_sync(cs42l42->regmap);

	cs42l42->suspended = false;
	mutex_unlock(&cs42l42->irq_lock);

	dev_dbg(dev, "System resumed\n");
}
EXPORT_SYMBOL_NS_GPL(cs42l42_resume_restore, SND_SOC_CS42L42_CORE);

static int __maybe_unused cs42l42_i2c_resume(struct device *dev)
{
	int ret;

	ret = cs42l42_resume(dev);
	if (ret)
		return ret;

	cs42l42_resume_restore(dev);

	return 0;
}

int cs42l42_common_probe(struct cs42l42_private *cs42l42,
			 const struct snd_soc_component_driver *component_drv,
			 struct snd_soc_dai_driver *dai)
{
	int ret, i;

	dev_set_drvdata(cs42l42->dev, cs42l42);
	mutex_init(&cs42l42->irq_lock);

	BUILD_BUG_ON(ARRAY_SIZE(cs42l42_supply_names) != ARRAY_SIZE(cs42l42->supplies));
	for (i = 0; i < ARRAY_SIZE(cs42l42->supplies); i++)
		cs42l42->supplies[i].supply = cs42l42_supply_names[i];

	ret = devm_regulator_bulk_get(cs42l42->dev,
				      ARRAY_SIZE(cs42l42->supplies),
				      cs42l42->supplies);
	if (ret != 0) {
		dev_err(cs42l42->dev,
			"Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(cs42l42->supplies),
				    cs42l42->supplies);
	if (ret != 0) {
		dev_err(cs42l42->dev,
			"Failed to enable supplies: %d\n", ret);
		return ret;
	}

	/* Reset the Device */
	cs42l42->reset_gpio = devm_gpiod_get_optional(cs42l42->dev,
		"reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs42l42->reset_gpio)) {
		ret = PTR_ERR(cs42l42->reset_gpio);
		goto err_disable_noreset;
	}

	if (cs42l42->reset_gpio) {
		dev_dbg(cs42l42->dev, "Found reset GPIO\n");

		/*
		 * ACPI can override the default GPIO state we requested
		 * so ensure that we start with RESET low.
		 */
		gpiod_set_value_cansleep(cs42l42->reset_gpio, 0);

		/* Ensure minimum reset pulse width */
		usleep_range(10, 500);

		/*
		 * On SoundWire keep the chip in reset until we get an UNATTACH
		 * notification from the SoundWire core. This acts as a
		 * synchronization point to reject stale ATTACH notifications
		 * if the chip was already enumerated before we reset it.
		 */
		if (cs42l42->sdw_peripheral)
			cs42l42->sdw_waiting_first_unattach = true;
		else
			gpiod_set_value_cansleep(cs42l42->reset_gpio, 1);
	}
	usleep_range(CS42L42_BOOT_TIME_US, CS42L42_BOOT_TIME_US * 2);

	/* Request IRQ if one was specified */
	if (cs42l42->irq) {
		ret = request_threaded_irq(cs42l42->irq,
					   NULL, cs42l42_irq_thread,
					   IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					   "cs42l42", cs42l42);
		if (ret) {
			dev_err_probe(cs42l42->dev, ret,
				"Failed to request IRQ\n");
			goto err_disable_noirq;
		}
	}

	/* Register codec now so it can EPROBE_DEFER */
	ret = devm_snd_soc_register_component(cs42l42->dev, component_drv, dai, 1);
	if (ret < 0)
		goto err;

	return 0;

err:
	if (cs42l42->irq)
		free_irq(cs42l42->irq, cs42l42);

err_disable_noirq:
	gpiod_set_value_cansleep(cs42l42->reset_gpio, 0);
err_disable_noreset:
	regulator_bulk_disable(ARRAY_SIZE(cs42l42->supplies), cs42l42->supplies);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs42l42_common_probe, SND_SOC_CS42L42_CORE);

int cs42l42_init(struct cs42l42_private *cs42l42)
{
	unsigned int reg;
	int devid, ret;

	/* initialize codec */
	devid = cirrus_read_device_id(cs42l42->regmap, CS42L42_DEVID_AB);
	if (devid < 0) {
		ret = devid;
		dev_err(cs42l42->dev, "Failed to read device ID: %d\n", ret);
		goto err_disable;
	}

	if (devid != cs42l42->devid) {
		ret = -ENODEV;
		dev_err(cs42l42->dev,
			"CS42L%x Device ID (%X). Expected %X\n",
			cs42l42->devid & 0xff, devid, cs42l42->devid);
		goto err_disable;
	}

	ret = regmap_read(cs42l42->regmap, CS42L42_REVID, &reg);
	if (ret < 0) {
		dev_err(cs42l42->dev, "Get Revision ID failed\n");
		goto err_shutdown;
	}

	dev_info(cs42l42->dev,
		 "Cirrus Logic CS42L%x, Revision: %02X\n",
		 cs42l42->devid & 0xff, reg & 0xFF);

	/* Power up the codec */
	regmap_update_bits(cs42l42->regmap, CS42L42_PWR_CTL1,
			CS42L42_ASP_DAO_PDN_MASK |
			CS42L42_ASP_DAI_PDN_MASK |
			CS42L42_MIXER_PDN_MASK |
			CS42L42_EQ_PDN_MASK |
			CS42L42_HP_PDN_MASK |
			CS42L42_ADC_PDN_MASK |
			CS42L42_PDN_ALL_MASK,
			(1 << CS42L42_ASP_DAO_PDN_SHIFT) |
			(1 << CS42L42_ASP_DAI_PDN_SHIFT) |
			(1 << CS42L42_MIXER_PDN_SHIFT) |
			(1 << CS42L42_EQ_PDN_SHIFT) |
			(1 << CS42L42_HP_PDN_SHIFT) |
			(1 << CS42L42_ADC_PDN_SHIFT) |
			(0 << CS42L42_PDN_ALL_SHIFT));

	ret = cs42l42_handle_device_data(cs42l42->dev, cs42l42);
	if (ret != 0)
		goto err_shutdown;

	/*
	 * SRC power is linked to ASP power so doesn't work in Soundwire mode.
	 * Override it and use DAPM to control SRC power for Soundwire.
	 */
	if (cs42l42->sdw_peripheral) {
		regmap_update_bits(cs42l42->regmap, CS42L42_PWR_CTL2,
				   CS42L42_SRC_PDN_OVERRIDE_MASK |
				   CS42L42_DAC_SRC_PDNB_MASK |
				   CS42L42_ADC_SRC_PDNB_MASK,
				   CS42L42_SRC_PDN_OVERRIDE_MASK);
	}

	/* Setup headset detection */
	cs42l42_setup_hs_type_detect(cs42l42);

	/*
	 * Set init_done before unmasking interrupts so any triggered
	 * immediately will be handled.
	 */
	cs42l42->init_done = true;

	/* Mask/Unmask Interrupts */
	cs42l42_set_interrupt_masks(cs42l42);

	return 0;

err_shutdown:
	regmap_write(cs42l42->regmap, CS42L42_CODEC_INT_MASK, 0xff);
	regmap_write(cs42l42->regmap, CS42L42_TSRS_PLUG_INT_MASK, 0xff);
	regmap_write(cs42l42->regmap, CS42L42_PWR_CTL1, 0xff);

err_disable:
	if (cs42l42->irq)
		free_irq(cs42l42->irq, cs42l42);

	gpiod_set_value_cansleep(cs42l42->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(cs42l42->supplies),
				cs42l42->supplies);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs42l42_init, SND_SOC_CS42L42_CORE);

void cs42l42_common_remove(struct cs42l42_private *cs42l42)
{
	if (cs42l42->irq)
		free_irq(cs42l42->irq, cs42l42);

	/*
	 * The driver might not have control of reset and power supplies,
	 * so ensure that the chip internals are powered down.
	 */
	if (cs42l42->init_done) {
		regmap_write(cs42l42->regmap, CS42L42_CODEC_INT_MASK, 0xff);
		regmap_write(cs42l42->regmap, CS42L42_TSRS_PLUG_INT_MASK, 0xff);
		regmap_write(cs42l42->regmap, CS42L42_PWR_CTL1, 0xff);
	}

	gpiod_set_value_cansleep(cs42l42->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(cs42l42->supplies), cs42l42->supplies);
}
EXPORT_SYMBOL_NS_GPL(cs42l42_common_remove, SND_SOC_CS42L42_CORE);

MODULE_DESCRIPTION("ASoC CS42L42 driver");
MODULE_AUTHOR("James Schulman, Cirrus Logic Inc, <james.schulman@cirrus.com>");
MODULE_AUTHOR("Brian Austin, Cirrus Logic Inc, <brian.austin@cirrus.com>");
MODULE_AUTHOR("Michael White, Cirrus Logic Inc, <michael.white@cirrus.com>");
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_AUTHOR("Vitaly Rodionov <vitalyr@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
