/*
 * NAU88L24 ALSA SoC audio driver
 *
 * Copyright 2016 Nuvoton Technology Corp.
 * Author: John Hsu <KCHSU0@nuvoton.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/acpi.h>
#include <linux/math64.h>
#include <linux/semaphore.h>

#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include "nau8824.h"


static int nau8824_config_sysclk(struct nau8824 *nau8824,
	int clk_id, unsigned int freq);
static bool nau8824_is_jack_inserted(struct nau8824 *nau8824);

/* the ADC threshold of headset */
#define DMIC_CLK 3072000

/* the ADC threshold of headset */
#define HEADSET_SARADC_THD 0x80

/* the parameter threshold of FLL */
#define NAU_FREF_MAX 13500000
#define NAU_FVCO_MAX 100000000
#define NAU_FVCO_MIN 90000000

/* scaling for mclk from sysclk_src output */
static const struct nau8824_fll_attr mclk_src_scaling[] = {
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
};

/* ratio for input clk freq */
static const struct nau8824_fll_attr fll_ratio[] = {
	{ 512000, 0x01 },
	{ 256000, 0x02 },
	{ 128000, 0x04 },
	{ 64000, 0x08 },
	{ 32000, 0x10 },
	{ 8000, 0x20 },
	{ 4000, 0x40 },
};

static const struct nau8824_fll_attr fll_pre_scalar[] = {
	{ 1, 0x0 },
	{ 2, 0x1 },
	{ 4, 0x2 },
	{ 8, 0x3 },
};

/* the maximum frequency of CLK_ADC and CLK_DAC */
#define CLK_DA_AD_MAX 6144000

/* over sampling rate */
static const struct nau8824_osr_attr osr_dac_sel[] = {
	{ 64, 2 },	/* OSR 64, SRC 1/4 */
	{ 256, 0 },	/* OSR 256, SRC 1 */
	{ 128, 1 },	/* OSR 128, SRC 1/2 */
	{ 0, 0 },
	{ 32, 3 },	/* OSR 32, SRC 1/8 */
};

static const struct nau8824_osr_attr osr_adc_sel[] = {
	{ 32, 3 },	/* OSR 32, SRC 1/8 */
	{ 64, 2 },	/* OSR 64, SRC 1/4 */
	{ 128, 1 },	/* OSR 128, SRC 1/2 */
	{ 256, 0 },	/* OSR 256, SRC 1 */
};

static const struct reg_default nau8824_reg_defaults[] = {
	{ NAU8824_REG_ENA_CTRL, 0x0000 },
	{ NAU8824_REG_CLK_GATING_ENA, 0x0000 },
	{ NAU8824_REG_CLK_DIVIDER, 0x0000 },
	{ NAU8824_REG_FLL1, 0x0000 },
	{ NAU8824_REG_FLL2, 0x3126 },
	{ NAU8824_REG_FLL3, 0x0008 },
	{ NAU8824_REG_FLL4, 0x0010 },
	{ NAU8824_REG_FLL5, 0xC000 },
	{ NAU8824_REG_FLL6, 0x6000 },
	{ NAU8824_REG_FLL_VCO_RSV, 0xF13C },
	{ NAU8824_REG_JACK_DET_CTRL, 0x0000 },
	{ NAU8824_REG_INTERRUPT_SETTING_1, 0x0000 },
	{ NAU8824_REG_IRQ, 0x0000 },
	{ NAU8824_REG_CLEAR_INT_REG, 0x0000 },
	{ NAU8824_REG_INTERRUPT_SETTING, 0x1000 },
	{ NAU8824_REG_SAR_ADC, 0x0015 },
	{ NAU8824_REG_VDET_COEFFICIENT, 0x0110 },
	{ NAU8824_REG_VDET_THRESHOLD_1, 0x0000 },
	{ NAU8824_REG_VDET_THRESHOLD_2, 0x0000 },
	{ NAU8824_REG_VDET_THRESHOLD_3, 0x0000 },
	{ NAU8824_REG_VDET_THRESHOLD_4, 0x0000 },
	{ NAU8824_REG_GPIO_SEL, 0x0000 },
	{ NAU8824_REG_PORT0_I2S_PCM_CTRL_1, 0x000B },
	{ NAU8824_REG_PORT0_I2S_PCM_CTRL_2, 0x0010 },
	{ NAU8824_REG_PORT0_LEFT_TIME_SLOT, 0x0000 },
	{ NAU8824_REG_PORT0_RIGHT_TIME_SLOT, 0x0000 },
	{ NAU8824_REG_TDM_CTRL, 0x0000 },
	{ NAU8824_REG_ADC_HPF_FILTER, 0x0000 },
	{ NAU8824_REG_ADC_FILTER_CTRL, 0x0002 },
	{ NAU8824_REG_DAC_FILTER_CTRL_1, 0x0000 },
	{ NAU8824_REG_DAC_FILTER_CTRL_2, 0x0000 },
	{ NAU8824_REG_NOTCH_FILTER_1, 0x0000 },
	{ NAU8824_REG_NOTCH_FILTER_2, 0x0000 },
	{ NAU8824_REG_EQ1_LOW, 0x112C },
	{ NAU8824_REG_EQ2_EQ3, 0x2C2C },
	{ NAU8824_REG_EQ4_EQ5, 0x2C2C },
	{ NAU8824_REG_ADC_CH0_DGAIN_CTRL, 0x0100 },
	{ NAU8824_REG_ADC_CH1_DGAIN_CTRL, 0x0100 },
	{ NAU8824_REG_ADC_CH2_DGAIN_CTRL, 0x0100 },
	{ NAU8824_REG_ADC_CH3_DGAIN_CTRL, 0x0100 },
	{ NAU8824_REG_DAC_MUTE_CTRL, 0x0000 },
	{ NAU8824_REG_DAC_CH0_DGAIN_CTRL, 0x0100 },
	{ NAU8824_REG_DAC_CH1_DGAIN_CTRL, 0x0100 },
	{ NAU8824_REG_ADC_TO_DAC_ST, 0x0000 },
	{ NAU8824_REG_DRC_KNEE_IP12_ADC_CH01, 0x1486 },
	{ NAU8824_REG_DRC_KNEE_IP34_ADC_CH01, 0x0F12 },
	{ NAU8824_REG_DRC_SLOPE_ADC_CH01, 0x25FF },
	{ NAU8824_REG_DRC_ATKDCY_ADC_CH01, 0x3457 },
	{ NAU8824_REG_DRC_KNEE_IP12_ADC_CH23, 0x1486 },
	{ NAU8824_REG_DRC_KNEE_IP34_ADC_CH23, 0x0F12 },
	{ NAU8824_REG_DRC_SLOPE_ADC_CH23, 0x25FF },
	{ NAU8824_REG_DRC_ATKDCY_ADC_CH23, 0x3457 },
	{ NAU8824_REG_DRC_GAINL_ADC0, 0x0200 },
	{ NAU8824_REG_DRC_GAINL_ADC1, 0x0200 },
	{ NAU8824_REG_DRC_GAINL_ADC2, 0x0200 },
	{ NAU8824_REG_DRC_GAINL_ADC3, 0x0200 },
	{ NAU8824_REG_DRC_KNEE_IP12_DAC, 0x1486 },
	{ NAU8824_REG_DRC_KNEE_IP34_DAC, 0x0F12 },
	{ NAU8824_REG_DRC_SLOPE_DAC, 0x25F9 },
	{ NAU8824_REG_DRC_ATKDCY_DAC, 0x3457 },
	{ NAU8824_REG_DRC_GAIN_DAC_CH0, 0x0200 },
	{ NAU8824_REG_DRC_GAIN_DAC_CH1, 0x0200 },
	{ NAU8824_REG_MODE, 0x0000 },
	{ NAU8824_REG_MODE1, 0x0000 },
	{ NAU8824_REG_MODE2, 0x0000 },
	{ NAU8824_REG_CLASSG, 0x0000 },
	{ NAU8824_REG_OTP_EFUSE, 0x0000 },
	{ NAU8824_REG_OTPDOUT_1, 0x0000 },
	{ NAU8824_REG_OTPDOUT_2, 0x0000 },
	{ NAU8824_REG_MISC_CTRL, 0x0000 },
	{ NAU8824_REG_I2C_TIMEOUT, 0xEFFF },
	{ NAU8824_REG_TEST_MODE, 0x0000 },
	{ NAU8824_REG_I2C_DEVICE_ID, 0x1AF1 },
	{ NAU8824_REG_SAR_ADC_DATA_OUT, 0x00FF },
	{ NAU8824_REG_BIAS_ADJ, 0x0000 },
	{ NAU8824_REG_PGA_GAIN, 0x0000 },
	{ NAU8824_REG_TRIM_SETTINGS, 0x0000 },
	{ NAU8824_REG_ANALOG_CONTROL_1, 0x0000 },
	{ NAU8824_REG_ANALOG_CONTROL_2, 0x0000 },
	{ NAU8824_REG_ENABLE_LO, 0x0000 },
	{ NAU8824_REG_GAIN_LO, 0x0000 },
	{ NAU8824_REG_CLASSD_GAIN_1, 0x0000 },
	{ NAU8824_REG_CLASSD_GAIN_2, 0x0000 },
	{ NAU8824_REG_ANALOG_ADC_1, 0x0011 },
	{ NAU8824_REG_ANALOG_ADC_2, 0x0020 },
	{ NAU8824_REG_RDAC, 0x0008 },
	{ NAU8824_REG_MIC_BIAS, 0x0006 },
	{ NAU8824_REG_HS_VOLUME_CONTROL, 0x0000 },
	{ NAU8824_REG_BOOST, 0x0000 },
	{ NAU8824_REG_FEPGA, 0x0000 },
	{ NAU8824_REG_FEPGA_II, 0x0000 },
	{ NAU8824_REG_FEPGA_SE, 0x0000 },
	{ NAU8824_REG_FEPGA_ATTENUATION, 0x0000 },
	{ NAU8824_REG_ATT_PORT0, 0x0000 },
	{ NAU8824_REG_ATT_PORT1, 0x0000 },
	{ NAU8824_REG_POWER_UP_CONTROL, 0x0000 },
	{ NAU8824_REG_CHARGE_PUMP_CONTROL, 0x0300 },
	{ NAU8824_REG_CHARGE_PUMP_INPUT, 0x0013 },
};

static int nau8824_sema_acquire(struct nau8824 *nau8824, long timeout)
{
	int ret;

	if (timeout) {
		ret = down_timeout(&nau8824->jd_sem, timeout);
		if (ret < 0)
			dev_warn(nau8824->dev, "Acquire semaphore timeout\n");
	} else {
		ret = down_interruptible(&nau8824->jd_sem);
		if (ret < 0)
			dev_warn(nau8824->dev, "Acquire semaphore fail\n");
	}

	return ret;
}

static inline void nau8824_sema_release(struct nau8824 *nau8824)
{
	up(&nau8824->jd_sem);
}

static bool nau8824_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8824_REG_ENA_CTRL ... NAU8824_REG_FLL_VCO_RSV:
	case NAU8824_REG_JACK_DET_CTRL:
	case NAU8824_REG_INTERRUPT_SETTING_1:
	case NAU8824_REG_IRQ:
	case NAU8824_REG_CLEAR_INT_REG ... NAU8824_REG_VDET_THRESHOLD_4:
	case NAU8824_REG_GPIO_SEL:
	case NAU8824_REG_PORT0_I2S_PCM_CTRL_1 ... NAU8824_REG_TDM_CTRL:
	case NAU8824_REG_ADC_HPF_FILTER ... NAU8824_REG_EQ4_EQ5:
	case NAU8824_REG_ADC_CH0_DGAIN_CTRL ... NAU8824_REG_ADC_TO_DAC_ST:
	case NAU8824_REG_DRC_KNEE_IP12_ADC_CH01 ... NAU8824_REG_DRC_GAINL_ADC3:
	case NAU8824_REG_DRC_KNEE_IP12_DAC ... NAU8824_REG_DRC_GAIN_DAC_CH1:
	case NAU8824_REG_CLASSG ... NAU8824_REG_OTP_EFUSE:
	case NAU8824_REG_OTPDOUT_1 ... NAU8824_REG_OTPDOUT_2:
	case NAU8824_REG_I2C_TIMEOUT:
	case NAU8824_REG_I2C_DEVICE_ID ... NAU8824_REG_SAR_ADC_DATA_OUT:
	case NAU8824_REG_BIAS_ADJ ... NAU8824_REG_CLASSD_GAIN_2:
	case NAU8824_REG_ANALOG_ADC_1 ... NAU8824_REG_ATT_PORT1:
	case NAU8824_REG_POWER_UP_CONTROL ... NAU8824_REG_CHARGE_PUMP_INPUT:
		return true;
	default:
		return false;
	}

}

static bool nau8824_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8824_REG_RESET ... NAU8824_REG_FLL_VCO_RSV:
	case NAU8824_REG_JACK_DET_CTRL:
	case NAU8824_REG_INTERRUPT_SETTING_1:
	case NAU8824_REG_CLEAR_INT_REG ... NAU8824_REG_VDET_THRESHOLD_4:
	case NAU8824_REG_GPIO_SEL:
	case NAU8824_REG_PORT0_I2S_PCM_CTRL_1 ... NAU8824_REG_TDM_CTRL:
	case NAU8824_REG_ADC_HPF_FILTER ... NAU8824_REG_EQ4_EQ5:
	case NAU8824_REG_ADC_CH0_DGAIN_CTRL ... NAU8824_REG_ADC_TO_DAC_ST:
	case NAU8824_REG_DRC_KNEE_IP12_ADC_CH01:
	case NAU8824_REG_DRC_KNEE_IP34_ADC_CH01:
	case NAU8824_REG_DRC_SLOPE_ADC_CH01:
	case NAU8824_REG_DRC_ATKDCY_ADC_CH01:
	case NAU8824_REG_DRC_KNEE_IP12_ADC_CH23:
	case NAU8824_REG_DRC_KNEE_IP34_ADC_CH23:
	case NAU8824_REG_DRC_SLOPE_ADC_CH23:
	case NAU8824_REG_DRC_ATKDCY_ADC_CH23:
	case NAU8824_REG_DRC_KNEE_IP12_DAC ... NAU8824_REG_DRC_ATKDCY_DAC:
	case NAU8824_REG_CLASSG ... NAU8824_REG_OTP_EFUSE:
	case NAU8824_REG_I2C_TIMEOUT:
	case NAU8824_REG_BIAS_ADJ ... NAU8824_REG_CLASSD_GAIN_2:
	case NAU8824_REG_ANALOG_ADC_1 ... NAU8824_REG_ATT_PORT1:
	case NAU8824_REG_POWER_UP_CONTROL ... NAU8824_REG_CHARGE_PUMP_CONTROL:
		return true;
	default:
		return false;
	}
}

static bool nau8824_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8824_REG_RESET:
	case NAU8824_REG_IRQ ... NAU8824_REG_CLEAR_INT_REG:
	case NAU8824_REG_DRC_GAINL_ADC0 ... NAU8824_REG_DRC_GAINL_ADC3:
	case NAU8824_REG_DRC_GAIN_DAC_CH0 ... NAU8824_REG_DRC_GAIN_DAC_CH1:
	case NAU8824_REG_OTPDOUT_1 ... NAU8824_REG_OTPDOUT_2:
	case NAU8824_REG_I2C_DEVICE_ID ... NAU8824_REG_SAR_ADC_DATA_OUT:
	case NAU8824_REG_CHARGE_PUMP_INPUT:
		return true;
	default:
		return false;
	}
}

static const char * const nau8824_companding[] = {
	"Off", "NC", "u-law", "A-law" };

static const struct soc_enum nau8824_companding_adc_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_PORT0_I2S_PCM_CTRL_1, 12,
		ARRAY_SIZE(nau8824_companding), nau8824_companding);

static const struct soc_enum nau8824_companding_dac_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_PORT0_I2S_PCM_CTRL_1, 14,
		ARRAY_SIZE(nau8824_companding), nau8824_companding);

static const char * const nau8824_adc_decimation[] = {
	"32", "64", "128", "256" };

static const struct soc_enum nau8824_adc_decimation_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_ADC_FILTER_CTRL, 0,
		ARRAY_SIZE(nau8824_adc_decimation), nau8824_adc_decimation);

static const char * const nau8824_dac_oversampl[] = {
	"64", "256", "128", "", "32" };

static const struct soc_enum nau8824_dac_oversampl_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_DAC_FILTER_CTRL_1, 0,
		ARRAY_SIZE(nau8824_dac_oversampl), nau8824_dac_oversampl);

static const char * const nau8824_input_channel[] = {
	"Input CH0", "Input CH1", "Input CH2", "Input CH3" };

static const struct soc_enum nau8824_adc_ch0_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_ADC_CH0_DGAIN_CTRL, 9,
		ARRAY_SIZE(nau8824_input_channel), nau8824_input_channel);

static const struct soc_enum nau8824_adc_ch1_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_ADC_CH1_DGAIN_CTRL, 9,
		ARRAY_SIZE(nau8824_input_channel), nau8824_input_channel);

static const struct soc_enum nau8824_adc_ch2_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_ADC_CH2_DGAIN_CTRL, 9,
		ARRAY_SIZE(nau8824_input_channel), nau8824_input_channel);

static const struct soc_enum nau8824_adc_ch3_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_ADC_CH3_DGAIN_CTRL, 9,
		ARRAY_SIZE(nau8824_input_channel), nau8824_input_channel);

static const char * const nau8824_tdm_slot[] = {
	"Slot 0", "Slot 1", "Slot 2", "Slot 3" };

static const struct soc_enum nau8824_dac_left_sel_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_TDM_CTRL, 6,
		ARRAY_SIZE(nau8824_tdm_slot), nau8824_tdm_slot);

static const struct soc_enum nau8824_dac_right_sel_enum =
	SOC_ENUM_SINGLE(NAU8824_REG_TDM_CTRL, 4,
		ARRAY_SIZE(nau8824_tdm_slot), nau8824_tdm_slot);

static const DECLARE_TLV_DB_MINMAX_MUTE(spk_vol_tlv, 0, 2400);
static const DECLARE_TLV_DB_MINMAX(hp_vol_tlv, -3000, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, 0, 200, 0);
static const DECLARE_TLV_DB_SCALE(dmic_vol_tlv, -12800, 50, 0);

static const struct snd_kcontrol_new nau8824_snd_controls[] = {
	SOC_ENUM("ADC Companding", nau8824_companding_adc_enum),
	SOC_ENUM("DAC Companding", nau8824_companding_dac_enum),

	SOC_ENUM("ADC Decimation Rate", nau8824_adc_decimation_enum),
	SOC_ENUM("DAC Oversampling Rate", nau8824_dac_oversampl_enum),

	SOC_SINGLE_TLV("Speaker Right DACR Volume",
		NAU8824_REG_CLASSD_GAIN_1, 8, 0x1f, 0, spk_vol_tlv),
	SOC_SINGLE_TLV("Speaker Left DACL Volume",
		NAU8824_REG_CLASSD_GAIN_2, 0, 0x1f, 0, spk_vol_tlv),
	SOC_SINGLE_TLV("Speaker Left DACR Volume",
		NAU8824_REG_CLASSD_GAIN_1, 0, 0x1f, 0, spk_vol_tlv),
	SOC_SINGLE_TLV("Speaker Right DACL Volume",
		NAU8824_REG_CLASSD_GAIN_2, 8, 0x1f, 0, spk_vol_tlv),

	SOC_SINGLE_TLV("Headphone Right DACR Volume",
		NAU8824_REG_ATT_PORT0, 8, 0x1f, 0, hp_vol_tlv),
	SOC_SINGLE_TLV("Headphone Left DACL Volume",
		NAU8824_REG_ATT_PORT0, 0, 0x1f, 0, hp_vol_tlv),
	SOC_SINGLE_TLV("Headphone Right DACL Volume",
		NAU8824_REG_ATT_PORT1, 8, 0x1f, 0, hp_vol_tlv),
	SOC_SINGLE_TLV("Headphone Left DACR Volume",
		NAU8824_REG_ATT_PORT1, 0, 0x1f, 0, hp_vol_tlv),

	SOC_SINGLE_TLV("MIC1 Volume", NAU8824_REG_FEPGA_II,
		NAU8824_FEPGA_GAINL_SFT, 0x12, 0, mic_vol_tlv),
	SOC_SINGLE_TLV("MIC2 Volume", NAU8824_REG_FEPGA_II,
		NAU8824_FEPGA_GAINR_SFT, 0x12, 0, mic_vol_tlv),

	SOC_SINGLE_TLV("DMIC1 Volume", NAU8824_REG_ADC_CH0_DGAIN_CTRL,
		0, 0x164, 0, dmic_vol_tlv),
	SOC_SINGLE_TLV("DMIC2 Volume", NAU8824_REG_ADC_CH1_DGAIN_CTRL,
		0, 0x164, 0, dmic_vol_tlv),
	SOC_SINGLE_TLV("DMIC3 Volume", NAU8824_REG_ADC_CH2_DGAIN_CTRL,
		0, 0x164, 0, dmic_vol_tlv),
	SOC_SINGLE_TLV("DMIC4 Volume", NAU8824_REG_ADC_CH3_DGAIN_CTRL,
		0, 0x164, 0, dmic_vol_tlv),

	SOC_ENUM("ADC CH0 Select", nau8824_adc_ch0_enum),
	SOC_ENUM("ADC CH1 Select", nau8824_adc_ch1_enum),
	SOC_ENUM("ADC CH2 Select", nau8824_adc_ch2_enum),
	SOC_ENUM("ADC CH3 Select", nau8824_adc_ch3_enum),

	SOC_SINGLE("ADC CH0 TX Switch", NAU8824_REG_TDM_CTRL, 0, 1, 0),
	SOC_SINGLE("ADC CH1 TX Switch", NAU8824_REG_TDM_CTRL, 1, 1, 0),
	SOC_SINGLE("ADC CH2 TX Switch", NAU8824_REG_TDM_CTRL, 2, 1, 0),
	SOC_SINGLE("ADC CH3 TX Switch", NAU8824_REG_TDM_CTRL, 3, 1, 0),

	SOC_ENUM("DACL Channel Source", nau8824_dac_left_sel_enum),
	SOC_ENUM("DACR Channel Source", nau8824_dac_right_sel_enum),

	SOC_SINGLE("DACL LR Mix", NAU8824_REG_DAC_MUTE_CTRL, 0, 1, 0),
	SOC_SINGLE("DACR LR Mix", NAU8824_REG_DAC_MUTE_CTRL, 1, 1, 0),

	SOC_SINGLE("THD for key media",
		NAU8824_REG_VDET_THRESHOLD_1, 8, 0xff, 0),
	SOC_SINGLE("THD for key voice command",
		NAU8824_REG_VDET_THRESHOLD_1, 0, 0xff, 0),
	SOC_SINGLE("THD for key volume up",
		NAU8824_REG_VDET_THRESHOLD_2, 8, 0xff, 0),
	SOC_SINGLE("THD for key volume down",
		NAU8824_REG_VDET_THRESHOLD_2, 0, 0xff, 0),
};

static int nau8824_output_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Disables the TESTDAC to let DAC signal pass through. */
		regmap_update_bits(nau8824->regmap, NAU8824_REG_ENABLE_LO,
			NAU8824_TEST_DAC_EN, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(nau8824->regmap, NAU8824_REG_ENABLE_LO,
			NAU8824_TEST_DAC_EN, NAU8824_TEST_DAC_EN);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nau8824_spk_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(nau8824->regmap,
			NAU8824_REG_ANALOG_CONTROL_2,
			NAU8824_CLASSD_CLAMP_DIS, NAU8824_CLASSD_CLAMP_DIS);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(nau8824->regmap,
			NAU8824_REG_ANALOG_CONTROL_2,
			NAU8824_CLASSD_CLAMP_DIS, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nau8824_pump_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Prevent startup click by letting charge pump to ramp up */
		msleep(10);
		regmap_update_bits(nau8824->regmap,
			NAU8824_REG_CHARGE_PUMP_CONTROL,
			NAU8824_JAMNODCLOW, NAU8824_JAMNODCLOW);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(nau8824->regmap,
			NAU8824_REG_CHARGE_PUMP_CONTROL,
			NAU8824_JAMNODCLOW, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int system_clock_control(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *k, int  event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = nau8824->regmap;
	unsigned int value;
	bool clk_fll, error;

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		dev_dbg(nau8824->dev, "system clock control : POWER OFF\n");
		/* Set clock source to disable or internal clock before the
		 * playback or capture end. Codec needs clock for Jack
		 * detection and button press if jack inserted; otherwise,
		 * the clock should be closed.
		 */
		if (nau8824_is_jack_inserted(nau8824)) {
			nau8824_config_sysclk(nau8824,
				NAU8824_CLK_INTERNAL, 0);
		} else {
			nau8824_config_sysclk(nau8824, NAU8824_CLK_DIS, 0);
		}
	} else {
		dev_dbg(nau8824->dev, "system clock control : POWER ON\n");
		/* Check the clock source setting is proper or not
		 * no matter the source is from FLL or MCLK.
		 */
		regmap_read(regmap, NAU8824_REG_FLL1, &value);
		clk_fll = value & NAU8824_FLL_RATIO_MASK;
		/* It's error to use internal clock when playback */
		regmap_read(regmap, NAU8824_REG_FLL6, &value);
		error = value & NAU8824_DCO_EN;
		if (!error) {
			/* Check error depending on source is FLL or MCLK. */
			regmap_read(regmap, NAU8824_REG_CLK_DIVIDER, &value);
			if (clk_fll)
				error = !(value & NAU8824_CLK_SRC_VCO);
			else
				error = value & NAU8824_CLK_SRC_VCO;
		}
		/* Recover the clock source setting if error. */
		if (error) {
			if (clk_fll) {
				regmap_update_bits(regmap,
					NAU8824_REG_FLL6, NAU8824_DCO_EN, 0);
				regmap_update_bits(regmap,
					NAU8824_REG_CLK_DIVIDER,
					NAU8824_CLK_SRC_MASK,
					NAU8824_CLK_SRC_VCO);
			} else {
				nau8824_config_sysclk(nau8824,
					NAU8824_CLK_MCLK, 0);
			}
		}
	}

	return 0;
}

static int dmic_clock_control(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *k, int  event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);
	int src;

	/* The DMIC clock is gotten from system clock (256fs) divided by
	 * DMIC_SRC (1, 2, 4, 8, 16, 32). The clock has to be equal or
	 * less than 3.072 MHz.
	 */
	for (src = 0; src < 5; src++) {
		if ((0x1 << (8 - src)) * nau8824->fs <= DMIC_CLK)
			break;
	}
	dev_dbg(nau8824->dev, "dmic src %d for mclk %d\n", src, nau8824->fs * 256);
	regmap_update_bits(nau8824->regmap, NAU8824_REG_CLK_DIVIDER,
		NAU8824_CLK_DMIC_SRC_MASK, (src << NAU8824_CLK_DMIC_SRC_SFT));

	return 0;
}

static const struct snd_kcontrol_new nau8824_adc_ch0_dmic =
	SOC_DAPM_SINGLE("Switch", NAU8824_REG_ENA_CTRL,
		NAU8824_ADC_CH0_DMIC_SFT, 1, 0);

static const struct snd_kcontrol_new nau8824_adc_ch1_dmic =
	SOC_DAPM_SINGLE("Switch", NAU8824_REG_ENA_CTRL,
		NAU8824_ADC_CH1_DMIC_SFT, 1, 0);

static const struct snd_kcontrol_new nau8824_adc_ch2_dmic =
	SOC_DAPM_SINGLE("Switch", NAU8824_REG_ENA_CTRL,
		NAU8824_ADC_CH2_DMIC_SFT, 1, 0);

static const struct snd_kcontrol_new nau8824_adc_ch3_dmic =
	SOC_DAPM_SINGLE("Switch", NAU8824_REG_ENA_CTRL,
		NAU8824_ADC_CH3_DMIC_SFT, 1, 0);

static const struct snd_kcontrol_new nau8824_adc_left_mixer[] = {
	SOC_DAPM_SINGLE("MIC Switch", NAU8824_REG_FEPGA,
		NAU8824_FEPGA_MODEL_MIC1_SFT, 1, 0),
	SOC_DAPM_SINGLE("HSMIC Switch", NAU8824_REG_FEPGA,
		NAU8824_FEPGA_MODEL_HSMIC_SFT, 1, 0),
};

static const struct snd_kcontrol_new nau8824_adc_right_mixer[] = {
	SOC_DAPM_SINGLE("MIC Switch", NAU8824_REG_FEPGA,
		NAU8824_FEPGA_MODER_MIC2_SFT, 1, 0),
	SOC_DAPM_SINGLE("HSMIC Switch", NAU8824_REG_FEPGA,
		NAU8824_FEPGA_MODER_HSMIC_SFT, 1, 0),
};

static const struct snd_kcontrol_new nau8824_hp_left_mixer[] = {
	SOC_DAPM_SINGLE("DAC Right Switch", NAU8824_REG_ENABLE_LO,
		NAU8824_DACR_HPL_EN_SFT, 1, 0),
	SOC_DAPM_SINGLE("DAC Left Switch", NAU8824_REG_ENABLE_LO,
		NAU8824_DACL_HPL_EN_SFT, 1, 0),
};

static const struct snd_kcontrol_new nau8824_hp_right_mixer[] = {
	SOC_DAPM_SINGLE("DAC Left Switch", NAU8824_REG_ENABLE_LO,
		NAU8824_DACL_HPR_EN_SFT, 1, 0),
	SOC_DAPM_SINGLE("DAC Right Switch", NAU8824_REG_ENABLE_LO,
		NAU8824_DACR_HPR_EN_SFT, 1, 0),
};

static const char * const nau8824_dac_src[] = { "DACL", "DACR" };

static SOC_ENUM_SINGLE_DECL(
	nau8824_dacl_enum, NAU8824_REG_DAC_CH0_DGAIN_CTRL,
	NAU8824_DAC_CH0_SEL_SFT, nau8824_dac_src);

static SOC_ENUM_SINGLE_DECL(
	nau8824_dacr_enum, NAU8824_REG_DAC_CH1_DGAIN_CTRL,
	NAU8824_DAC_CH1_SEL_SFT, nau8824_dac_src);

static const struct snd_kcontrol_new nau8824_dacl_mux =
	SOC_DAPM_ENUM("DACL Source", nau8824_dacl_enum);

static const struct snd_kcontrol_new nau8824_dacr_mux =
	SOC_DAPM_ENUM("DACR Source", nau8824_dacr_enum);


static const struct snd_soc_dapm_widget nau8824_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("System Clock", SND_SOC_NOPM, 0, 0,
		system_clock_control, SND_SOC_DAPM_POST_PMD |
		SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_INPUT("HSMIC1"),
	SND_SOC_DAPM_INPUT("HSMIC2"),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_INPUT("DMIC3"),
	SND_SOC_DAPM_INPUT("DMIC4"),

	SND_SOC_DAPM_SUPPLY("SAR", NAU8824_REG_SAR_ADC,
		NAU8824_SAR_ADC_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS", NAU8824_REG_MIC_BIAS,
		NAU8824_MICBIAS_POWERUP_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC12 Power", NAU8824_REG_BIAS_ADJ,
		NAU8824_DMIC1_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC34 Power", NAU8824_REG_BIAS_ADJ,
		NAU8824_DMIC2_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC Clock", SND_SOC_NOPM, 0, 0,
		dmic_clock_control, SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_SWITCH("DMIC1 Enable", SND_SOC_NOPM,
		0, 0, &nau8824_adc_ch0_dmic),
	SND_SOC_DAPM_SWITCH("DMIC2 Enable", SND_SOC_NOPM,
		0, 0, &nau8824_adc_ch1_dmic),
	SND_SOC_DAPM_SWITCH("DMIC3 Enable", SND_SOC_NOPM,
		0, 0, &nau8824_adc_ch2_dmic),
	SND_SOC_DAPM_SWITCH("DMIC4 Enable", SND_SOC_NOPM,
		0, 0, &nau8824_adc_ch3_dmic),

	SND_SOC_DAPM_MIXER("Left ADC", NAU8824_REG_POWER_UP_CONTROL,
		12, 0, nau8824_adc_left_mixer,
		ARRAY_SIZE(nau8824_adc_left_mixer)),
	SND_SOC_DAPM_MIXER("Right ADC", NAU8824_REG_POWER_UP_CONTROL,
		13, 0, nau8824_adc_right_mixer,
		ARRAY_SIZE(nau8824_adc_right_mixer)),

	SND_SOC_DAPM_ADC("ADCL", NULL, NAU8824_REG_ANALOG_ADC_2,
		NAU8824_ADCL_EN_SFT, 0),
	SND_SOC_DAPM_ADC("ADCR", NULL, NAU8824_REG_ANALOG_ADC_2,
		NAU8824_ADCR_EN_SFT, 0),

	SND_SOC_DAPM_AIF_OUT("AIFTX", "HiFi Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIFRX", "HiFi Playback", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DACL", NULL, NAU8824_REG_RDAC,
		NAU8824_DACL_EN_SFT, 0),
	SND_SOC_DAPM_SUPPLY("DACL Clock", NAU8824_REG_RDAC,
		NAU8824_DACL_CLK_SFT, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DACR", NULL, NAU8824_REG_RDAC,
		NAU8824_DACR_EN_SFT, 0),
	SND_SOC_DAPM_SUPPLY("DACR Clock", NAU8824_REG_RDAC,
		NAU8824_DACR_CLK_SFT, 0, NULL, 0),

	SND_SOC_DAPM_MUX("DACL Mux", SND_SOC_NOPM, 0, 0, &nau8824_dacl_mux),
	SND_SOC_DAPM_MUX("DACR Mux", SND_SOC_NOPM, 0, 0, &nau8824_dacr_mux),

	SND_SOC_DAPM_PGA_S("Output DACL", 0, NAU8824_REG_CHARGE_PUMP_CONTROL,
		8, 1, nau8824_output_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_S("Output DACR", 0, NAU8824_REG_CHARGE_PUMP_CONTROL,
		9, 1, nau8824_output_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_PGA_S("ClassD", 0, NAU8824_REG_CLASSD_GAIN_1,
		NAU8824_CLASSD_EN_SFT, 0, nau8824_spk_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER("Left Headphone", NAU8824_REG_CLASSG,
		NAU8824_CLASSG_LDAC_EN_SFT, 0, nau8824_hp_left_mixer,
		ARRAY_SIZE(nau8824_hp_left_mixer)),
	SND_SOC_DAPM_MIXER("Right Headphone", NAU8824_REG_CLASSG,
		NAU8824_CLASSG_RDAC_EN_SFT, 0, nau8824_hp_right_mixer,
		ARRAY_SIZE(nau8824_hp_right_mixer)),
	SND_SOC_DAPM_PGA_S("Charge Pump", 1, NAU8824_REG_CHARGE_PUMP_CONTROL,
		NAU8824_CHARGE_PUMP_EN_SFT, 0, nau8824_pump_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA("Output Driver L",
		NAU8824_REG_POWER_UP_CONTROL, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Output Driver R",
		NAU8824_REG_POWER_UP_CONTROL, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Main Driver L",
		NAU8824_REG_POWER_UP_CONTROL, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Main Driver R",
		NAU8824_REG_POWER_UP_CONTROL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HP Boost Driver", NAU8824_REG_BOOST,
		NAU8824_HP_BOOST_DIS_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Class G", NAU8824_REG_CLASSG,
		NAU8824_CLASSG_EN_SFT, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("SPKOUTL"),
	SND_SOC_DAPM_OUTPUT("SPKOUTR"),
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_route nau8824_dapm_routes[] = {
	{"DMIC1 Enable", "Switch", "DMIC1"},
	{"DMIC2 Enable", "Switch", "DMIC2"},
	{"DMIC3 Enable", "Switch", "DMIC3"},
	{"DMIC4 Enable", "Switch", "DMIC4"},

	{"DMIC1", NULL, "DMIC12 Power"},
	{"DMIC2", NULL, "DMIC12 Power"},
	{"DMIC3", NULL, "DMIC34 Power"},
	{"DMIC4", NULL, "DMIC34 Power"},
	{"DMIC12 Power", NULL, "DMIC Clock"},
	{"DMIC34 Power", NULL, "DMIC Clock"},

	{"Left ADC", "MIC Switch", "MIC1"},
	{"Left ADC", "HSMIC Switch", "HSMIC1"},
	{"Right ADC", "MIC Switch", "MIC2"},
	{"Right ADC", "HSMIC Switch", "HSMIC2"},

	{"ADCL", NULL, "Left ADC"},
	{"ADCR", NULL, "Right ADC"},

	{"AIFTX", NULL, "MICBIAS"},
	{"AIFTX", NULL, "ADCL"},
	{"AIFTX", NULL, "ADCR"},
	{"AIFTX", NULL, "DMIC1 Enable"},
	{"AIFTX", NULL, "DMIC2 Enable"},
	{"AIFTX", NULL, "DMIC3 Enable"},
	{"AIFTX", NULL, "DMIC4 Enable"},

	{"AIFTX", NULL, "System Clock"},
	{"AIFRX", NULL, "System Clock"},

	{"DACL", NULL, "AIFRX"},
	{"DACL", NULL, "DACL Clock"},
	{"DACR", NULL, "AIFRX"},
	{"DACR", NULL, "DACR Clock"},

	{"DACL Mux", "DACL", "DACL"},
	{"DACL Mux", "DACR", "DACR"},
	{"DACR Mux", "DACL", "DACL"},
	{"DACR Mux", "DACR", "DACR"},

	{"Output DACL", NULL, "DACL Mux"},
	{"Output DACR", NULL, "DACR Mux"},

	{"ClassD", NULL, "Output DACL"},
	{"ClassD", NULL, "Output DACR"},

	{"Left Headphone", "DAC Left Switch", "Output DACL"},
	{"Left Headphone", "DAC Right Switch", "Output DACR"},
	{"Right Headphone", "DAC Left Switch", "Output DACL"},
	{"Right Headphone", "DAC Right Switch", "Output DACR"},

	{"Charge Pump", NULL, "Left Headphone"},
	{"Charge Pump", NULL, "Right Headphone"},
	{"Output Driver L", NULL, "Charge Pump"},
	{"Output Driver R", NULL, "Charge Pump"},
	{"Main Driver L", NULL, "Output Driver L"},
	{"Main Driver R", NULL, "Output Driver R"},
	{"Class G", NULL, "Main Driver L"},
	{"Class G", NULL, "Main Driver R"},
	{"HP Boost Driver", NULL, "Class G"},

	{"SPKOUTL", NULL, "ClassD"},
	{"SPKOUTR", NULL, "ClassD"},
	{"HPOL", NULL, "HP Boost Driver"},
	{"HPOR", NULL, "HP Boost Driver"},
};

static bool nau8824_is_jack_inserted(struct nau8824 *nau8824)
{
	struct snd_soc_jack *jack = nau8824->jack;
	bool insert = false;

	if (nau8824->irq && jack)
		insert = jack->status & SND_JACK_HEADPHONE;

	return insert;
}

static void nau8824_int_status_clear_all(struct regmap *regmap)
{
	int active_irq, clear_irq, i;

	/* Reset the intrruption status from rightmost bit if the corres-
	 * ponding irq event occurs.
	 */
	regmap_read(regmap, NAU8824_REG_IRQ, &active_irq);
	for (i = 0; i < NAU8824_REG_DATA_LEN; i++) {
		clear_irq = (0x1 << i);
		if (active_irq & clear_irq)
			regmap_write(regmap,
				NAU8824_REG_CLEAR_INT_REG, clear_irq);
	}
}

static void nau8824_eject_jack(struct nau8824 *nau8824)
{
	struct snd_soc_dapm_context *dapm = nau8824->dapm;
	struct regmap *regmap = nau8824->regmap;

	/* Clear all interruption status */
	nau8824_int_status_clear_all(regmap);

	snd_soc_dapm_disable_pin(dapm, "SAR");
	snd_soc_dapm_disable_pin(dapm, "MICBIAS");
	snd_soc_dapm_sync(dapm);

	/* Enable the insertion interruption, disable the ejection
	 * interruption, and then bypass de-bounce circuit.
	 */
	regmap_update_bits(regmap, NAU8824_REG_INTERRUPT_SETTING,
		NAU8824_IRQ_KEY_RELEASE_DIS | NAU8824_IRQ_KEY_SHORT_PRESS_DIS |
		NAU8824_IRQ_EJECT_DIS | NAU8824_IRQ_INSERT_DIS,
		NAU8824_IRQ_KEY_RELEASE_DIS | NAU8824_IRQ_KEY_SHORT_PRESS_DIS |
		NAU8824_IRQ_EJECT_DIS);
	regmap_update_bits(regmap, NAU8824_REG_INTERRUPT_SETTING_1,
		NAU8824_IRQ_INSERT_EN | NAU8824_IRQ_EJECT_EN,
		NAU8824_IRQ_INSERT_EN);
	regmap_update_bits(regmap, NAU8824_REG_ENA_CTRL,
		NAU8824_JD_SLEEP_MODE, NAU8824_JD_SLEEP_MODE);

	/* Close clock for jack type detection at manual mode */
	if (dapm->bias_level < SND_SOC_BIAS_PREPARE)
		nau8824_config_sysclk(nau8824, NAU8824_CLK_DIS, 0);
}

static void nau8824_jdet_work(struct work_struct *work)
{
	struct nau8824 *nau8824 = container_of(
		work, struct nau8824, jdet_work);
	struct snd_soc_dapm_context *dapm = nau8824->dapm;
	struct regmap *regmap = nau8824->regmap;
	int adc_value, event = 0, event_mask = 0;

	snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");
	snd_soc_dapm_force_enable_pin(dapm, "SAR");
	snd_soc_dapm_sync(dapm);

	msleep(100);

	regmap_read(regmap, NAU8824_REG_SAR_ADC_DATA_OUT, &adc_value);
	adc_value = adc_value & NAU8824_SAR_ADC_DATA_MASK;
	dev_dbg(nau8824->dev, "SAR ADC data 0x%02x\n", adc_value);
	if (adc_value < HEADSET_SARADC_THD) {
		event |= SND_JACK_HEADPHONE;

		snd_soc_dapm_disable_pin(dapm, "SAR");
		snd_soc_dapm_disable_pin(dapm, "MICBIAS");
		snd_soc_dapm_sync(dapm);
	} else {
		event |= SND_JACK_HEADSET;
	}
	event_mask |= SND_JACK_HEADSET;
	snd_soc_jack_report(nau8824->jack, event, event_mask);

	/* Enable short key press and release interruption. */
	regmap_update_bits(regmap, NAU8824_REG_INTERRUPT_SETTING,
		NAU8824_IRQ_KEY_RELEASE_DIS |
		NAU8824_IRQ_KEY_SHORT_PRESS_DIS, 0);

	nau8824_sema_release(nau8824);
}

static void nau8824_setup_auto_irq(struct nau8824 *nau8824)
{
	struct regmap *regmap = nau8824->regmap;

	/* Enable jack ejection interruption. */
	regmap_update_bits(regmap, NAU8824_REG_INTERRUPT_SETTING_1,
		NAU8824_IRQ_INSERT_EN | NAU8824_IRQ_EJECT_EN,
		NAU8824_IRQ_EJECT_EN);
	regmap_update_bits(regmap, NAU8824_REG_INTERRUPT_SETTING,
		NAU8824_IRQ_EJECT_DIS, 0);
	/* Enable internal VCO needed for interruptions */
	if (nau8824->dapm->bias_level < SND_SOC_BIAS_PREPARE)
		nau8824_config_sysclk(nau8824, NAU8824_CLK_INTERNAL, 0);
	regmap_update_bits(regmap, NAU8824_REG_ENA_CTRL,
		NAU8824_JD_SLEEP_MODE, 0);
}

static int nau8824_button_decode(int value)
{
	int buttons = 0;

	/* The chip supports up to 8 buttons, but ALSA defines
	 * only 6 buttons.
	 */
	if (value & BIT(0))
		buttons |= SND_JACK_BTN_0;
	if (value & BIT(1))
		buttons |= SND_JACK_BTN_1;
	if (value & BIT(2))
		buttons |= SND_JACK_BTN_2;
	if (value & BIT(3))
		buttons |= SND_JACK_BTN_3;
	if (value & BIT(4))
		buttons |= SND_JACK_BTN_4;
	if (value & BIT(5))
		buttons |= SND_JACK_BTN_5;

	return buttons;
}

#define NAU8824_BUTTONS (SND_JACK_BTN_0 | SND_JACK_BTN_1 | \
		SND_JACK_BTN_2 | SND_JACK_BTN_3)

static irqreturn_t nau8824_interrupt(int irq, void *data)
{
	struct nau8824 *nau8824 = (struct nau8824 *)data;
	struct regmap *regmap = nau8824->regmap;
	int active_irq, clear_irq = 0, event = 0, event_mask = 0;

	if (regmap_read(regmap, NAU8824_REG_IRQ, &active_irq)) {
		dev_err(nau8824->dev, "failed to read irq status\n");
		return IRQ_NONE;
	}
	dev_dbg(nau8824->dev, "IRQ %x\n", active_irq);

	if (active_irq & NAU8824_JACK_EJECTION_DETECTED) {
		nau8824_eject_jack(nau8824);
		event_mask |= SND_JACK_HEADSET;
		clear_irq = NAU8824_JACK_EJECTION_DETECTED;
		/* release semaphore held after resume,
		 * and cancel jack detection
		 */
		nau8824_sema_release(nau8824);
		cancel_work_sync(&nau8824->jdet_work);
	} else if (active_irq & NAU8824_KEY_SHORT_PRESS_IRQ) {
		int key_status, button_pressed;

		regmap_read(regmap, NAU8824_REG_CLEAR_INT_REG,
			&key_status);

		/* lower 8 bits of the register are for pressed keys */
		button_pressed = nau8824_button_decode(key_status);

		event |= button_pressed;
		dev_dbg(nau8824->dev, "button %x pressed\n", event);
		event_mask |= NAU8824_BUTTONS;
		clear_irq = NAU8824_KEY_SHORT_PRESS_IRQ;
	} else if (active_irq & NAU8824_KEY_RELEASE_IRQ) {
		event_mask = NAU8824_BUTTONS;
		clear_irq = NAU8824_KEY_RELEASE_IRQ;
	} else if (active_irq & NAU8824_JACK_INSERTION_DETECTED) {
		/* Turn off insertion interruption at manual mode */
		regmap_update_bits(regmap,
			NAU8824_REG_INTERRUPT_SETTING,
			NAU8824_IRQ_INSERT_DIS,
			NAU8824_IRQ_INSERT_DIS);
		regmap_update_bits(regmap,
			NAU8824_REG_INTERRUPT_SETTING_1,
			NAU8824_IRQ_INSERT_EN, 0);
		/* detect microphone and jack type */
		cancel_work_sync(&nau8824->jdet_work);
		schedule_work(&nau8824->jdet_work);

		/* Enable interruption for jack type detection at audo
		 * mode which can detect microphone and jack type.
		 */
		nau8824_setup_auto_irq(nau8824);
	}

	if (!clear_irq)
		clear_irq = active_irq;
	/* clears the rightmost interruption */
	regmap_write(regmap, NAU8824_REG_CLEAR_INT_REG, clear_irq);

	if (event_mask)
		snd_soc_jack_report(nau8824->jack, event, event_mask);

	return IRQ_HANDLED;
}

static int nau8824_clock_check(struct nau8824 *nau8824,
	int stream, int rate, int osr)
{
	int osrate;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (osr >= ARRAY_SIZE(osr_dac_sel))
			return -EINVAL;
		osrate = osr_dac_sel[osr].osr;
	} else {
		if (osr >= ARRAY_SIZE(osr_adc_sel))
			return -EINVAL;
		osrate = osr_adc_sel[osr].osr;
	}

	if (!osrate || rate * osr > CLK_DA_AD_MAX) {
		dev_err(nau8824->dev, "exceed the maximum frequency of CLK_ADC or CLK_DAC\n");
		return -EINVAL;
	}

	return 0;
}

static int nau8824_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);
	unsigned int val_len = 0, osr, ctrl_val, bclk_fs, bclk_div;

	nau8824_sema_acquire(nau8824, HZ);

	/* CLK_DAC or CLK_ADC = OSR * FS
	 * DAC or ADC clock frequency is defined as Over Sampling Rate (OSR)
	 * multiplied by the audio sample rate (Fs). Note that the OSR and Fs
	 * values must be selected such that the maximum frequency is less
	 * than 6.144 MHz.
	 */
	nau8824->fs = params_rate(params);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_read(nau8824->regmap,
			NAU8824_REG_DAC_FILTER_CTRL_1, &osr);
		osr &= NAU8824_DAC_OVERSAMPLE_MASK;
		if (nau8824_clock_check(nau8824, substream->stream,
			nau8824->fs, osr))
			return -EINVAL;
		regmap_update_bits(nau8824->regmap, NAU8824_REG_CLK_DIVIDER,
			NAU8824_CLK_DAC_SRC_MASK,
			osr_dac_sel[osr].clk_src << NAU8824_CLK_DAC_SRC_SFT);
	} else {
		regmap_read(nau8824->regmap,
			NAU8824_REG_ADC_FILTER_CTRL, &osr);
		osr &= NAU8824_ADC_SYNC_DOWN_MASK;
		if (nau8824_clock_check(nau8824, substream->stream,
			nau8824->fs, osr))
			return -EINVAL;
		regmap_update_bits(nau8824->regmap, NAU8824_REG_CLK_DIVIDER,
			NAU8824_CLK_ADC_SRC_MASK,
			osr_adc_sel[osr].clk_src << NAU8824_CLK_ADC_SRC_SFT);
	}

	/* make BCLK and LRC divde configuration if the codec as master. */
	regmap_read(nau8824->regmap,
		NAU8824_REG_PORT0_I2S_PCM_CTRL_2, &ctrl_val);
	if (ctrl_val & NAU8824_I2S_MS_MASTER) {
		/* get the bclk and fs ratio */
		bclk_fs = snd_soc_params_to_bclk(params) / nau8824->fs;
		if (bclk_fs <= 32)
			bclk_div = 0x3;
		else if (bclk_fs <= 64)
			bclk_div = 0x2;
		else if (bclk_fs <= 128)
			bclk_div = 0x1;
		else if (bclk_fs <= 256)
			bclk_div = 0;
		else
			return -EINVAL;
		regmap_update_bits(nau8824->regmap,
			NAU8824_REG_PORT0_I2S_PCM_CTRL_2,
			NAU8824_I2S_LRC_DIV_MASK | NAU8824_I2S_BLK_DIV_MASK,
			(bclk_div << NAU8824_I2S_LRC_DIV_SFT) | bclk_div);
	}

	switch (params_width(params)) {
	case 16:
		val_len |= NAU8824_I2S_DL_16;
		break;
	case 20:
		val_len |= NAU8824_I2S_DL_20;
		break;
	case 24:
		val_len |= NAU8824_I2S_DL_24;
		break;
	case 32:
		val_len |= NAU8824_I2S_DL_32;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(nau8824->regmap, NAU8824_REG_PORT0_I2S_PCM_CTRL_1,
		NAU8824_I2S_DL_MASK, val_len);

	nau8824_sema_release(nau8824);

	return 0;
}

static int nau8824_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);
	unsigned int ctrl1_val = 0, ctrl2_val = 0;

	nau8824_sema_acquire(nau8824, HZ);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		ctrl2_val |= NAU8824_I2S_MS_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ctrl1_val |= NAU8824_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ctrl1_val |= NAU8824_I2S_DF_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ctrl1_val |= NAU8824_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ctrl1_val |= NAU8824_I2S_DF_RIGTH;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		ctrl1_val |= NAU8824_I2S_DF_PCM_AB;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		ctrl1_val |= NAU8824_I2S_DF_PCM_AB;
		ctrl1_val |= NAU8824_I2S_PCMB_EN;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(nau8824->regmap, NAU8824_REG_PORT0_I2S_PCM_CTRL_1,
		NAU8824_I2S_DF_MASK | NAU8824_I2S_BP_MASK |
		NAU8824_I2S_PCMB_EN, ctrl1_val);
	regmap_update_bits(nau8824->regmap, NAU8824_REG_PORT0_I2S_PCM_CTRL_2,
		NAU8824_I2S_MS_MASK, ctrl2_val);

	nau8824_sema_release(nau8824);

	return 0;
}

/**
 * nau8824_set_tdm_slot - configure DAI TDM.
 * @dai: DAI
 * @tx_mask: Bitmask representing active TX slots. Ex.
 *                 0xf for normal 4 channel TDM.
 *                 0xf0 for shifted 4 channel TDM
 * @rx_mask: Bitmask [0:1] representing active DACR RX slots.
 *                 Bitmask [2:3] representing active DACL RX slots.
 *                 00=CH0,01=CH1,10=CH2,11=CH3. Ex.
 *                 0xf for DACL/R selecting TDM CH3.
 *                 0xf0 for DACL/R selecting shifted TDM CH3.
 * @slots: Number of slots in use.
 * @slot_width: Width in bits for each slot.
 *
 * Configures a DAI for TDM operation. Only support 4 slots TDM.
 */
static int nau8824_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);
	unsigned int tslot_l = 0, ctrl_val = 0;

	if (slots > 4 || ((tx_mask & 0xf0) && (tx_mask & 0xf)) ||
		((rx_mask & 0xf0) && (rx_mask & 0xf)) ||
		((rx_mask & 0xf0) && (tx_mask & 0xf)) ||
		((rx_mask & 0xf) && (tx_mask & 0xf0)))
		return -EINVAL;

	ctrl_val |= (NAU8824_TDM_MODE | NAU8824_TDM_OFFSET_EN);
	if (tx_mask & 0xf0) {
		tslot_l = 4 * slot_width;
		ctrl_val |= (tx_mask >> 4);
	} else {
		ctrl_val |= tx_mask;
	}
	if (rx_mask & 0xf0)
		ctrl_val |= ((rx_mask >> 4) << NAU8824_TDM_DACR_RX_SFT);
	else
		ctrl_val |= (rx_mask << NAU8824_TDM_DACR_RX_SFT);

	regmap_update_bits(nau8824->regmap, NAU8824_REG_TDM_CTRL,
		NAU8824_TDM_MODE | NAU8824_TDM_OFFSET_EN |
		NAU8824_TDM_DACL_RX_MASK | NAU8824_TDM_DACR_RX_MASK |
		NAU8824_TDM_TX_MASK, ctrl_val);
	regmap_update_bits(nau8824->regmap, NAU8824_REG_PORT0_LEFT_TIME_SLOT,
		NAU8824_TSLOT_L_MASK, tslot_l);

	return 0;
}

/**
 * nau8824_calc_fll_param - Calculate FLL parameters.
 * @fll_in: external clock provided to codec.
 * @fs: sampling rate.
 * @fll_param: Pointer to structure of FLL parameters.
 *
 * Calculate FLL parameters to configure codec.
 *
 * Returns 0 for success or negative error code.
 */
static int nau8824_calc_fll_param(unsigned int fll_in,
	unsigned int fs, struct nau8824_fll *fll_param)
{
	u64 fvco, fvco_max;
	unsigned int fref, i, fvco_sel;

	/* Ensure the reference clock frequency (FREF) is <= 13.5MHz by dividing
	 * freq_in by 1, 2, 4, or 8 using FLL pre-scalar.
	 * FREF = freq_in / NAU8824_FLL_REF_DIV_MASK
	 */
	for (i = 0; i < ARRAY_SIZE(fll_pre_scalar); i++) {
		fref = fll_in / fll_pre_scalar[i].param;
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
	 * FDCO must be within the 90MHz - 124MHz or the FFL cannot be
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

	/* Calculate the FLL 10-bit integer input and the FLL 16-bit fractional
	 * input based on FDCO, FREF and FLL ratio.
	 */
	fvco = div_u64(fvco_max << 16, fref * fll_param->ratio);
	fll_param->fll_int = (fvco >> 16) & 0x3FF;
	fll_param->fll_frac = fvco & 0xFFFF;
	return 0;
}

static void nau8824_fll_apply(struct regmap *regmap,
	struct nau8824_fll *fll_param)
{
	regmap_update_bits(regmap, NAU8824_REG_CLK_DIVIDER,
		NAU8824_CLK_SRC_MASK | NAU8824_CLK_MCLK_SRC_MASK,
		NAU8824_CLK_SRC_MCLK | fll_param->mclk_src);
	regmap_update_bits(regmap, NAU8824_REG_FLL1,
		NAU8824_FLL_RATIO_MASK, fll_param->ratio);
	/* FLL 16-bit fractional input */
	regmap_write(regmap, NAU8824_REG_FLL2, fll_param->fll_frac);
	/* FLL 10-bit integer input */
	regmap_update_bits(regmap, NAU8824_REG_FLL3,
		NAU8824_FLL_INTEGER_MASK, fll_param->fll_int);
	/* FLL pre-scaler */
	regmap_update_bits(regmap, NAU8824_REG_FLL4,
		NAU8824_FLL_REF_DIV_MASK,
		fll_param->clk_ref_div << NAU8824_FLL_REF_DIV_SFT);
	/* select divided VCO input */
	regmap_update_bits(regmap, NAU8824_REG_FLL5,
		NAU8824_FLL_CLK_SW_MASK, NAU8824_FLL_CLK_SW_REF);
	/* Disable free-running mode */
	regmap_update_bits(regmap,
		NAU8824_REG_FLL6, NAU8824_DCO_EN, 0);
	if (fll_param->fll_frac) {
		regmap_update_bits(regmap, NAU8824_REG_FLL5,
			NAU8824_FLL_PDB_DAC_EN | NAU8824_FLL_LOOP_FTR_EN |
			NAU8824_FLL_FTR_SW_MASK,
			NAU8824_FLL_PDB_DAC_EN | NAU8824_FLL_LOOP_FTR_EN |
			NAU8824_FLL_FTR_SW_FILTER);
		regmap_update_bits(regmap, NAU8824_REG_FLL6,
			NAU8824_SDM_EN, NAU8824_SDM_EN);
	} else {
		regmap_update_bits(regmap, NAU8824_REG_FLL5,
			NAU8824_FLL_PDB_DAC_EN | NAU8824_FLL_LOOP_FTR_EN |
			NAU8824_FLL_FTR_SW_MASK, NAU8824_FLL_FTR_SW_ACCU);
		regmap_update_bits(regmap,
			NAU8824_REG_FLL6, NAU8824_SDM_EN, 0);
	}
}

/* freq_out must be 256*Fs in order to achieve the best performance */
static int nau8824_set_pll(struct snd_soc_component *component, int pll_id, int source,
		unsigned int freq_in, unsigned int freq_out)
{
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);
	struct nau8824_fll fll_param;
	int ret, fs;

	fs = freq_out / 256;
	ret = nau8824_calc_fll_param(freq_in, fs, &fll_param);
	if (ret < 0) {
		dev_err(nau8824->dev, "Unsupported input clock %d\n", freq_in);
		return ret;
	}
	dev_dbg(nau8824->dev, "mclk_src=%x ratio=%x fll_frac=%x fll_int=%x clk_ref_div=%x\n",
		fll_param.mclk_src, fll_param.ratio, fll_param.fll_frac,
		fll_param.fll_int, fll_param.clk_ref_div);

	nau8824_fll_apply(nau8824->regmap, &fll_param);
	mdelay(2);
	regmap_update_bits(nau8824->regmap, NAU8824_REG_CLK_DIVIDER,
		NAU8824_CLK_SRC_MASK, NAU8824_CLK_SRC_VCO);

	return 0;
}

static int nau8824_config_sysclk(struct nau8824 *nau8824,
	int clk_id, unsigned int freq)
{
	struct regmap *regmap = nau8824->regmap;

	switch (clk_id) {
	case NAU8824_CLK_DIS:
		regmap_update_bits(regmap, NAU8824_REG_CLK_DIVIDER,
			NAU8824_CLK_SRC_MASK, NAU8824_CLK_SRC_MCLK);
		regmap_update_bits(regmap, NAU8824_REG_FLL6,
			NAU8824_DCO_EN, 0);
		break;

	case NAU8824_CLK_MCLK:
		nau8824_sema_acquire(nau8824, HZ);
		regmap_update_bits(regmap, NAU8824_REG_CLK_DIVIDER,
			NAU8824_CLK_SRC_MASK, NAU8824_CLK_SRC_MCLK);
		regmap_update_bits(regmap, NAU8824_REG_FLL6,
			NAU8824_DCO_EN, 0);
		nau8824_sema_release(nau8824);
		break;

	case NAU8824_CLK_INTERNAL:
		regmap_update_bits(regmap, NAU8824_REG_FLL6,
			NAU8824_DCO_EN, NAU8824_DCO_EN);
		regmap_update_bits(regmap, NAU8824_REG_CLK_DIVIDER,
			NAU8824_CLK_SRC_MASK, NAU8824_CLK_SRC_VCO);
		break;

	case NAU8824_CLK_FLL_MCLK:
		nau8824_sema_acquire(nau8824, HZ);
		regmap_update_bits(regmap, NAU8824_REG_FLL3,
			NAU8824_FLL_CLK_SRC_MASK, NAU8824_FLL_CLK_SRC_MCLK);
		nau8824_sema_release(nau8824);
		break;

	case NAU8824_CLK_FLL_BLK:
		nau8824_sema_acquire(nau8824, HZ);
		regmap_update_bits(regmap, NAU8824_REG_FLL3,
			NAU8824_FLL_CLK_SRC_MASK, NAU8824_FLL_CLK_SRC_BLK);
		nau8824_sema_release(nau8824);
		break;

	case NAU8824_CLK_FLL_FS:
		nau8824_sema_acquire(nau8824, HZ);
		regmap_update_bits(regmap, NAU8824_REG_FLL3,
			NAU8824_FLL_CLK_SRC_MASK, NAU8824_FLL_CLK_SRC_FS);
		nau8824_sema_release(nau8824);
		break;

	default:
		dev_err(nau8824->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	dev_dbg(nau8824->dev, "Sysclk is %dHz and clock id is %d\n", freq,
		clk_id);

	return 0;
}

static int nau8824_set_sysclk(struct snd_soc_component *component,
	int clk_id, int source, unsigned int freq, int dir)
{
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);

	return nau8824_config_sysclk(nau8824, clk_id, freq);
}

static void nau8824_resume_setup(struct nau8824 *nau8824)
{
	nau8824_config_sysclk(nau8824, NAU8824_CLK_DIS, 0);
	if (nau8824->irq) {
		/* Clear all interruption status */
		nau8824_int_status_clear_all(nau8824->regmap);
		/* Enable jack detection at sleep mode, insertion detection,
		 * and ejection detection.
		 */
		regmap_update_bits(nau8824->regmap, NAU8824_REG_ENA_CTRL,
			NAU8824_JD_SLEEP_MODE, NAU8824_JD_SLEEP_MODE);
		regmap_update_bits(nau8824->regmap,
			NAU8824_REG_INTERRUPT_SETTING_1,
			NAU8824_IRQ_EJECT_EN | NAU8824_IRQ_INSERT_EN,
			NAU8824_IRQ_EJECT_EN | NAU8824_IRQ_INSERT_EN);
		regmap_update_bits(nau8824->regmap,
			NAU8824_REG_INTERRUPT_SETTING,
			NAU8824_IRQ_EJECT_DIS | NAU8824_IRQ_INSERT_DIS, 0);
	}
}

static int nau8824_set_bias_level(struct snd_soc_component *component,
	enum snd_soc_bias_level level)
{
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			/* Setup codec configuration after resume */
			nau8824_resume_setup(nau8824);
		}
		break;

	case SND_SOC_BIAS_OFF:
		regmap_update_bits(nau8824->regmap,
			NAU8824_REG_INTERRUPT_SETTING, 0x3ff, 0x3ff);
		regmap_update_bits(nau8824->regmap,
			NAU8824_REG_INTERRUPT_SETTING_1,
			NAU8824_IRQ_EJECT_EN | NAU8824_IRQ_INSERT_EN, 0);
		break;
	}

	return 0;
}

static int nau8824_component_probe(struct snd_soc_component *component)
{
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	nau8824->dapm = dapm;

	return 0;
}

static int __maybe_unused nau8824_suspend(struct snd_soc_component *component)
{
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);

	if (nau8824->irq) {
		disable_irq(nau8824->irq);
		snd_soc_component_force_bias_level(component, SND_SOC_BIAS_OFF);
	}
	regcache_cache_only(nau8824->regmap, true);
	regcache_mark_dirty(nau8824->regmap);

	return 0;
}

static int __maybe_unused nau8824_resume(struct snd_soc_component *component)
{
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(nau8824->regmap, false);
	regcache_sync(nau8824->regmap);
	if (nau8824->irq) {
		/* Hold semaphore to postpone playback happening
		 * until jack detection done.
		 */
		nau8824_sema_acquire(nau8824, 0);
		enable_irq(nau8824->irq);
	}

	return 0;
}

static const struct snd_soc_component_driver nau8824_component_driver = {
	.probe			= nau8824_component_probe,
	.set_sysclk		= nau8824_set_sysclk,
	.set_pll		= nau8824_set_pll,
	.set_bias_level		= nau8824_set_bias_level,
	.suspend		= nau8824_suspend,
	.resume			= nau8824_resume,
	.controls		= nau8824_snd_controls,
	.num_controls		= ARRAY_SIZE(nau8824_snd_controls),
	.dapm_widgets		= nau8824_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(nau8824_dapm_widgets),
	.dapm_routes		= nau8824_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(nau8824_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct snd_soc_dai_ops nau8824_dai_ops = {
	.hw_params = nau8824_hw_params,
	.set_fmt = nau8824_set_fmt,
	.set_tdm_slot = nau8824_set_tdm_slot,
};

#define NAU8824_RATES SNDRV_PCM_RATE_8000_192000
#define NAU8824_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE \
	 | SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver nau8824_dai = {
	.name = NAU8824_CODEC_DAI,
	.playback = {
		.stream_name	 = "Playback",
		.channels_min	 = 1,
		.channels_max	 = 2,
		.rates		 = NAU8824_RATES,
		.formats	 = NAU8824_FORMATS,
	},
	.capture = {
		.stream_name	 = "Capture",
		.channels_min	 = 1,
		.channels_max	 = 2,
		.rates		 = NAU8824_RATES,
		.formats	 = NAU8824_FORMATS,
	},
	.ops = &nau8824_dai_ops,
};

static const struct regmap_config nau8824_regmap_config = {
	.val_bits = NAU8824_REG_ADDR_LEN,
	.reg_bits = NAU8824_REG_DATA_LEN,

	.max_register = NAU8824_REG_MAX,
	.readable_reg = nau8824_readable_reg,
	.writeable_reg = nau8824_writeable_reg,
	.volatile_reg = nau8824_volatile_reg,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = nau8824_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(nau8824_reg_defaults),
};

/**
 * nau8824_enable_jack_detect - Specify a jack for event reporting
 *
 * @component:  component to register the jack with
 * @jack: jack to use to report headset and button events on
 *
 * After this function has been called the headset insert/remove and button
 * events will be routed to the given jack.  Jack can be null to stop
 * reporting.
 */
int nau8824_enable_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *jack)
{
	struct nau8824 *nau8824 = snd_soc_component_get_drvdata(component);
	int ret;

	nau8824->jack = jack;
	/* Initiate jack detection work queue */
	INIT_WORK(&nau8824->jdet_work, nau8824_jdet_work);
	ret = devm_request_threaded_irq(nau8824->dev, nau8824->irq, NULL,
		nau8824_interrupt, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
		"nau8824", nau8824);
	if (ret) {
		dev_err(nau8824->dev, "Cannot request irq %d (%d)\n",
			nau8824->irq, ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nau8824_enable_jack_detect);

static void nau8824_reset_chip(struct regmap *regmap)
{
	regmap_write(regmap, NAU8824_REG_RESET, 0x00);
	regmap_write(regmap, NAU8824_REG_RESET, 0x00);
}

static void nau8824_setup_buttons(struct nau8824 *nau8824)
{
	struct regmap *regmap = nau8824->regmap;

	regmap_update_bits(regmap, NAU8824_REG_SAR_ADC,
		NAU8824_SAR_TRACKING_GAIN_MASK,
		nau8824->sar_voltage << NAU8824_SAR_TRACKING_GAIN_SFT);
	regmap_update_bits(regmap, NAU8824_REG_SAR_ADC,
		NAU8824_SAR_COMPARE_TIME_MASK,
		nau8824->sar_compare_time << NAU8824_SAR_COMPARE_TIME_SFT);
	regmap_update_bits(regmap, NAU8824_REG_SAR_ADC,
		NAU8824_SAR_SAMPLING_TIME_MASK,
		nau8824->sar_sampling_time << NAU8824_SAR_SAMPLING_TIME_SFT);

	regmap_update_bits(regmap, NAU8824_REG_VDET_COEFFICIENT,
		NAU8824_LEVELS_NR_MASK,
		(nau8824->sar_threshold_num - 1) << NAU8824_LEVELS_NR_SFT);
	regmap_update_bits(regmap, NAU8824_REG_VDET_COEFFICIENT,
		NAU8824_HYSTERESIS_MASK,
		nau8824->sar_hysteresis << NAU8824_HYSTERESIS_SFT);
	regmap_update_bits(regmap, NAU8824_REG_VDET_COEFFICIENT,
		NAU8824_SHORTKEY_DEBOUNCE_MASK,
		nau8824->key_debounce << NAU8824_SHORTKEY_DEBOUNCE_SFT);

	regmap_write(regmap, NAU8824_REG_VDET_THRESHOLD_1,
		(nau8824->sar_threshold[0] << 8) | nau8824->sar_threshold[1]);
	regmap_write(regmap, NAU8824_REG_VDET_THRESHOLD_2,
		(nau8824->sar_threshold[2] << 8) | nau8824->sar_threshold[3]);
	regmap_write(regmap, NAU8824_REG_VDET_THRESHOLD_3,
		(nau8824->sar_threshold[4] << 8) | nau8824->sar_threshold[5]);
	regmap_write(regmap, NAU8824_REG_VDET_THRESHOLD_4,
		(nau8824->sar_threshold[6] << 8) | nau8824->sar_threshold[7]);
}

static void nau8824_init_regs(struct nau8824 *nau8824)
{
	struct regmap *regmap = nau8824->regmap;

	/* Enable Bias/VMID/VMID Tieoff */
	regmap_update_bits(regmap, NAU8824_REG_BIAS_ADJ,
		NAU8824_VMID | NAU8824_VMID_SEL_MASK, NAU8824_VMID |
		(nau8824->vref_impedance << NAU8824_VMID_SEL_SFT));
	regmap_update_bits(regmap, NAU8824_REG_BOOST,
		NAU8824_GLOBAL_BIAS_EN, NAU8824_GLOBAL_BIAS_EN);
	mdelay(2);
	regmap_update_bits(regmap, NAU8824_REG_MIC_BIAS,
		NAU8824_MICBIAS_VOLTAGE_MASK, nau8824->micbias_voltage);
	/* Disable Boost Driver, Automatic Short circuit protection enable */
	regmap_update_bits(regmap, NAU8824_REG_BOOST,
		NAU8824_PRECHARGE_DIS | NAU8824_HP_BOOST_DIS |
		NAU8824_HP_BOOST_G_DIS | NAU8824_SHORT_SHUTDOWN_EN,
		NAU8824_PRECHARGE_DIS | NAU8824_HP_BOOST_DIS |
		NAU8824_HP_BOOST_G_DIS | NAU8824_SHORT_SHUTDOWN_EN);
	/* Scaling for ADC and DAC clock */
	regmap_update_bits(regmap, NAU8824_REG_CLK_DIVIDER,
		NAU8824_CLK_ADC_SRC_MASK | NAU8824_CLK_DAC_SRC_MASK,
		(0x1 << NAU8824_CLK_ADC_SRC_SFT) |
		(0x1 << NAU8824_CLK_DAC_SRC_SFT));
	regmap_update_bits(regmap, NAU8824_REG_DAC_MUTE_CTRL,
		NAU8824_DAC_ZC_EN, NAU8824_DAC_ZC_EN);
	regmap_update_bits(regmap, NAU8824_REG_ENA_CTRL,
		NAU8824_DAC_CH1_EN | NAU8824_DAC_CH0_EN |
		NAU8824_ADC_CH0_EN | NAU8824_ADC_CH1_EN |
		NAU8824_ADC_CH2_EN | NAU8824_ADC_CH3_EN,
		NAU8824_DAC_CH1_EN | NAU8824_DAC_CH0_EN |
		NAU8824_ADC_CH0_EN | NAU8824_ADC_CH1_EN |
		NAU8824_ADC_CH2_EN | NAU8824_ADC_CH3_EN);
	regmap_update_bits(regmap, NAU8824_REG_CLK_GATING_ENA,
		NAU8824_CLK_ADC_CH23_EN | NAU8824_CLK_ADC_CH01_EN |
		NAU8824_CLK_DAC_CH1_EN | NAU8824_CLK_DAC_CH0_EN |
		NAU8824_CLK_I2S_EN | NAU8824_CLK_GAIN_EN |
		NAU8824_CLK_SAR_EN | NAU8824_CLK_DMIC_CH23_EN,
		NAU8824_CLK_ADC_CH23_EN | NAU8824_CLK_ADC_CH01_EN |
		NAU8824_CLK_DAC_CH1_EN | NAU8824_CLK_DAC_CH0_EN |
		NAU8824_CLK_I2S_EN | NAU8824_CLK_GAIN_EN |
		NAU8824_CLK_SAR_EN | NAU8824_CLK_DMIC_CH23_EN);
	/* Class G timer 64ms */
	regmap_update_bits(regmap, NAU8824_REG_CLASSG,
		NAU8824_CLASSG_TIMER_MASK,
		0x20 << NAU8824_CLASSG_TIMER_SFT);
	regmap_update_bits(regmap, NAU8824_REG_TRIM_SETTINGS,
		NAU8824_DRV_CURR_INC, NAU8824_DRV_CURR_INC);
	/* Disable DACR/L power */
	regmap_update_bits(regmap, NAU8824_REG_CHARGE_PUMP_CONTROL,
		NAU8824_SPKR_PULL_DOWN | NAU8824_SPKL_PULL_DOWN |
		NAU8824_POWER_DOWN_DACR | NAU8824_POWER_DOWN_DACL,
		NAU8824_SPKR_PULL_DOWN | NAU8824_SPKL_PULL_DOWN |
		NAU8824_POWER_DOWN_DACR | NAU8824_POWER_DOWN_DACL);
	/* Enable TESTDAC. This sets the analog DAC inputs to a '0' input
	 * signal to avoid any glitches due to power up transients in both
	 * the analog and digital DAC circuit.
	 */
	regmap_update_bits(regmap, NAU8824_REG_ENABLE_LO,
		NAU8824_TEST_DAC_EN, NAU8824_TEST_DAC_EN);
	/* Config L/R channel */
	regmap_update_bits(regmap, NAU8824_REG_DAC_CH0_DGAIN_CTRL,
		NAU8824_DAC_CH0_SEL_MASK, NAU8824_DAC_CH0_SEL_I2S0);
	regmap_update_bits(regmap, NAU8824_REG_DAC_CH1_DGAIN_CTRL,
		NAU8824_DAC_CH1_SEL_MASK, NAU8824_DAC_CH1_SEL_I2S1);
	regmap_update_bits(regmap, NAU8824_REG_ENABLE_LO,
		NAU8824_DACR_HPR_EN | NAU8824_DACL_HPL_EN,
		NAU8824_DACR_HPR_EN | NAU8824_DACL_HPL_EN);
	/* Default oversampling/decimations settings are unusable
	 * (audible hiss). Set it to something better.
	 */
	regmap_update_bits(regmap, NAU8824_REG_ADC_FILTER_CTRL,
		NAU8824_ADC_SYNC_DOWN_MASK, NAU8824_ADC_SYNC_DOWN_64);
	regmap_update_bits(regmap, NAU8824_REG_DAC_FILTER_CTRL_1,
		NAU8824_DAC_CICCLP_OFF | NAU8824_DAC_OVERSAMPLE_MASK,
		NAU8824_DAC_CICCLP_OFF | NAU8824_DAC_OVERSAMPLE_64);
	/* DAC clock delay 2ns, VREF */
	regmap_update_bits(regmap, NAU8824_REG_RDAC,
		NAU8824_RDAC_CLK_DELAY_MASK | NAU8824_RDAC_VREF_MASK,
		(0x2 << NAU8824_RDAC_CLK_DELAY_SFT) |
		(0x3 << NAU8824_RDAC_VREF_SFT));
	/* PGA input mode selection */
	regmap_update_bits(regmap, NAU8824_REG_FEPGA,
		NAU8824_FEPGA_MODEL_SHORT_EN | NAU8824_FEPGA_MODER_SHORT_EN,
		NAU8824_FEPGA_MODEL_SHORT_EN | NAU8824_FEPGA_MODER_SHORT_EN);
	/* Digital microphone control */
	regmap_update_bits(regmap, NAU8824_REG_ANALOG_CONTROL_1,
		NAU8824_DMIC_CLK_DRV_STRG | NAU8824_DMIC_CLK_SLEW_FAST,
		NAU8824_DMIC_CLK_DRV_STRG | NAU8824_DMIC_CLK_SLEW_FAST);
	regmap_update_bits(regmap, NAU8824_REG_JACK_DET_CTRL,
		NAU8824_JACK_LOGIC,
		/* jkdet_polarity - 1  is for active-low */
		nau8824->jkdet_polarity ? 0 : NAU8824_JACK_LOGIC);
	regmap_update_bits(regmap,
		NAU8824_REG_JACK_DET_CTRL, NAU8824_JACK_EJECT_DT_MASK,
		(nau8824->jack_eject_debounce << NAU8824_JACK_EJECT_DT_SFT));
	if (nau8824->sar_threshold_num)
		nau8824_setup_buttons(nau8824);
}

static int nau8824_setup_irq(struct nau8824 *nau8824)
{
	/* Disable interruption before codec initiation done */
	regmap_update_bits(nau8824->regmap, NAU8824_REG_ENA_CTRL,
		NAU8824_JD_SLEEP_MODE, NAU8824_JD_SLEEP_MODE);
	regmap_update_bits(nau8824->regmap,
		NAU8824_REG_INTERRUPT_SETTING, 0x3ff, 0x3ff);
	regmap_update_bits(nau8824->regmap, NAU8824_REG_INTERRUPT_SETTING_1,
		NAU8824_IRQ_EJECT_EN | NAU8824_IRQ_INSERT_EN, 0);

	return 0;
}

static void nau8824_print_device_properties(struct nau8824 *nau8824)
{
	struct device *dev = nau8824->dev;
	int i;

	dev_dbg(dev, "jkdet-polarity:       %d\n", nau8824->jkdet_polarity);
	dev_dbg(dev, "micbias-voltage:      %d\n", nau8824->micbias_voltage);
	dev_dbg(dev, "vref-impedance:       %d\n", nau8824->vref_impedance);

	dev_dbg(dev, "sar-threshold-num:    %d\n", nau8824->sar_threshold_num);
	for (i = 0; i < nau8824->sar_threshold_num; i++)
		dev_dbg(dev, "sar-threshold[%d]=%x\n", i,
				nau8824->sar_threshold[i]);

	dev_dbg(dev, "sar-hysteresis:       %d\n", nau8824->sar_hysteresis);
	dev_dbg(dev, "sar-voltage:          %d\n", nau8824->sar_voltage);
	dev_dbg(dev, "sar-compare-time:     %d\n", nau8824->sar_compare_time);
	dev_dbg(dev, "sar-sampling-time:    %d\n", nau8824->sar_sampling_time);
	dev_dbg(dev, "short-key-debounce:   %d\n", nau8824->key_debounce);
	dev_dbg(dev, "jack-eject-debounce:  %d\n",
			nau8824->jack_eject_debounce);
}

static int nau8824_read_device_properties(struct device *dev,
	struct nau8824 *nau8824) {
	int ret;

	ret = device_property_read_u32(dev, "nuvoton,jkdet-polarity",
		&nau8824->jkdet_polarity);
	if (ret)
		nau8824->jkdet_polarity = 1;
	ret = device_property_read_u32(dev, "nuvoton,micbias-voltage",
		&nau8824->micbias_voltage);
	if (ret)
		nau8824->micbias_voltage = 6;
	ret = device_property_read_u32(dev, "nuvoton,vref-impedance",
		&nau8824->vref_impedance);
	if (ret)
		nau8824->vref_impedance = 2;
	ret = device_property_read_u32(dev, "nuvoton,sar-threshold-num",
		&nau8824->sar_threshold_num);
	if (ret)
		nau8824->sar_threshold_num = 4;
	ret = device_property_read_u32_array(dev, "nuvoton,sar-threshold",
		nau8824->sar_threshold, nau8824->sar_threshold_num);
	if (ret) {
		nau8824->sar_threshold[0] = 0x0a;
		nau8824->sar_threshold[1] = 0x14;
		nau8824->sar_threshold[2] = 0x26;
		nau8824->sar_threshold[3] = 0x73;
	}
	ret = device_property_read_u32(dev, "nuvoton,sar-hysteresis",
		&nau8824->sar_hysteresis);
	if (ret)
		nau8824->sar_hysteresis = 0;
	ret = device_property_read_u32(dev, "nuvoton,sar-voltage",
		&nau8824->sar_voltage);
	if (ret)
		nau8824->sar_voltage = 6;
	ret = device_property_read_u32(dev, "nuvoton,sar-compare-time",
		&nau8824->sar_compare_time);
	if (ret)
		nau8824->sar_compare_time = 1;
	ret = device_property_read_u32(dev, "nuvoton,sar-sampling-time",
		&nau8824->sar_sampling_time);
	if (ret)
		nau8824->sar_sampling_time = 1;
	ret = device_property_read_u32(dev, "nuvoton,short-key-debounce",
		&nau8824->key_debounce);
	if (ret)
		nau8824->key_debounce = 0;
	ret = device_property_read_u32(dev, "nuvoton,jack-eject-debounce",
		&nau8824->jack_eject_debounce);
	if (ret)
		nau8824->jack_eject_debounce = 1;

	return 0;
}

static int nau8824_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct nau8824 *nau8824 = dev_get_platdata(dev);
	int ret, value;

	if (!nau8824) {
		nau8824 = devm_kzalloc(dev, sizeof(*nau8824), GFP_KERNEL);
		if (!nau8824)
			return -ENOMEM;
		ret = nau8824_read_device_properties(dev, nau8824);
		if (ret)
			return ret;
	}
	i2c_set_clientdata(i2c, nau8824);

	nau8824->regmap = devm_regmap_init_i2c(i2c, &nau8824_regmap_config);
	if (IS_ERR(nau8824->regmap))
		return PTR_ERR(nau8824->regmap);
	nau8824->dev = dev;
	nau8824->irq = i2c->irq;
	sema_init(&nau8824->jd_sem, 1);

	nau8824_print_device_properties(nau8824);

	ret = regmap_read(nau8824->regmap, NAU8824_REG_I2C_DEVICE_ID, &value);
	if (ret < 0) {
		dev_err(dev, "Failed to read device id from the NAU8824: %d\n",
			ret);
		return ret;
	}
	nau8824_reset_chip(nau8824->regmap);
	nau8824_init_regs(nau8824);

	if (i2c->irq)
		nau8824_setup_irq(nau8824);

	return devm_snd_soc_register_component(dev,
		&nau8824_component_driver, &nau8824_dai, 1);
}

static const struct i2c_device_id nau8824_i2c_ids[] = {
	{ "nau8824", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nau8824_i2c_ids);

#ifdef CONFIG_OF
static const struct of_device_id nau8824_of_ids[] = {
	{ .compatible = "nuvoton,nau8824", },
	{}
};
MODULE_DEVICE_TABLE(of, nau8824_of_ids);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id nau8824_acpi_match[] = {
	{ "10508824", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, nau8824_acpi_match);
#endif

static struct i2c_driver nau8824_i2c_driver = {
	.driver = {
		.name = "nau8824",
		.of_match_table = of_match_ptr(nau8824_of_ids),
		.acpi_match_table = ACPI_PTR(nau8824_acpi_match),
	},
	.probe = nau8824_i2c_probe,
	.id_table = nau8824_i2c_ids,
};
module_i2c_driver(nau8824_i2c_driver);


MODULE_DESCRIPTION("ASoC NAU88L24 driver");
MODULE_AUTHOR("John Hsu <KCHSU0@nuvoton.com>");
MODULE_LICENSE("GPL v2");
