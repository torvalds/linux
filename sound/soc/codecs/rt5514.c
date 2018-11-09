/*
 * rt5514.c  --  RT5514 ALSA SoC audio codec driver
 *
 * Copyright 2015 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rl6231.h"
#include "rt5514.h"
#if IS_ENABLED(CONFIG_SND_SOC_RT5514_SPI)
#include "rt5514-spi.h"
#endif

static const struct reg_sequence rt5514_i2c_patch[] = {
	{0x1800101c, 0x00000000},
	{0x18001100, 0x0000031f},
	{0x18001104, 0x00000007},
	{0x18001108, 0x00000000},
	{0x1800110c, 0x00000000},
	{0x18001110, 0x00000000},
	{0x18001114, 0x00000001},
	{0x18001118, 0x00000000},
	{0x18002f08, 0x00000006},
	{0x18002f00, 0x00055149},
	{0x18002f00, 0x0005514b},
	{0x18002f00, 0x00055149},
	{0xfafafafa, 0x00000001},
	{0x18002f10, 0x00000001},
	{0x18002f10, 0x00000000},
	{0x18002f10, 0x00000001},
	{0xfafafafa, 0x00000001},
	{0x18002000, 0x000010ec},
	{0xfafafafa, 0x00000000},
};

static const struct reg_sequence rt5514_patch[] = {
	{RT5514_DIG_IO_CTRL,		0x00000040},
	{RT5514_CLK_CTRL1,		0x38020041},
	{RT5514_SRC_CTRL,		0x44000eee},
	{RT5514_ANA_CTRL_LDO10,		0x00028604},
	{RT5514_ANA_CTRL_ADCFED,	0x00000800},
	{RT5514_ASRC_IN_CTRL1,		0x00000003},
	{RT5514_DOWNFILTER0_CTRL3,	0x10000342},
	{RT5514_DOWNFILTER1_CTRL3,	0x10000342},
};

static const struct reg_default rt5514_reg[] = {
	{RT5514_RESET,			0x00000000},
	{RT5514_PWR_ANA1,		0x00808880},
	{RT5514_PWR_ANA2,		0x00220000},
	{RT5514_I2S_CTRL1,		0x00000330},
	{RT5514_I2S_CTRL2,		0x20000000},
	{RT5514_VAD_CTRL6,		0xc00007d2},
	{RT5514_EXT_VAD_CTRL,		0x80000080},
	{RT5514_DIG_IO_CTRL,		0x00000040},
	{RT5514_PAD_CTRL1,		0x00804000},
	{RT5514_DMIC_DATA_CTRL,		0x00000005},
	{RT5514_DIG_SOURCE_CTRL,	0x00000002},
	{RT5514_SRC_CTRL,		0x44000eee},
	{RT5514_DOWNFILTER2_CTRL1,	0x0000882f},
	{RT5514_PLL_SOURCE_CTRL,	0x00000004},
	{RT5514_CLK_CTRL1,		0x38020041},
	{RT5514_CLK_CTRL2,		0x00000000},
	{RT5514_PLL3_CALIB_CTRL1,	0x00400200},
	{RT5514_PLL3_CALIB_CTRL5,	0x40220012},
	{RT5514_DELAY_BUF_CTRL1,	0x7fff006a},
	{RT5514_DELAY_BUF_CTRL3,	0x00000000},
	{RT5514_ASRC_IN_CTRL1,		0x00000003},
	{RT5514_DOWNFILTER0_CTRL1,	0x00020c2f},
	{RT5514_DOWNFILTER0_CTRL2,	0x00020c2f},
	{RT5514_DOWNFILTER0_CTRL3,	0x10000342},
	{RT5514_DOWNFILTER1_CTRL1,	0x00020c2f},
	{RT5514_DOWNFILTER1_CTRL2,	0x00020c2f},
	{RT5514_DOWNFILTER1_CTRL3,	0x10000342},
	{RT5514_ANA_CTRL_LDO10,		0x00028604},
	{RT5514_ANA_CTRL_LDO18_16,	0x02000345},
	{RT5514_ANA_CTRL_ADC12,		0x0000a2a8},
	{RT5514_ANA_CTRL_ADC21,		0x00001180},
	{RT5514_ANA_CTRL_ADC22,		0x0000aaa8},
	{RT5514_ANA_CTRL_ADC23,		0x00151427},
	{RT5514_ANA_CTRL_MICBST,	0x00002000},
	{RT5514_ANA_CTRL_ADCFED,	0x00000800},
	{RT5514_ANA_CTRL_INBUF,		0x00000143},
	{RT5514_ANA_CTRL_VREF,		0x00008d50},
	{RT5514_ANA_CTRL_PLL3,		0x0000000e},
	{RT5514_ANA_CTRL_PLL1_1,	0x00000000},
	{RT5514_ANA_CTRL_PLL1_2,	0x00030220},
	{RT5514_DMIC_LP_CTRL,		0x00000000},
	{RT5514_MISC_CTRL_DSP,		0x00000000},
	{RT5514_DSP_CTRL1,		0x00055149},
	{RT5514_DSP_CTRL3,		0x00000006},
	{RT5514_DSP_CTRL4,		0x00000001},
	{RT5514_VENDOR_ID1,		0x00000001},
	{RT5514_VENDOR_ID2,		0x10ec5514},
};

static void rt5514_enable_dsp_prepare(struct rt5514_priv *rt5514)
{
	/* Reset */
	regmap_write(rt5514->i2c_regmap, 0x18002000, 0x000010ec);
	/* LDO_I_limit */
	regmap_write(rt5514->i2c_regmap, 0x18002200, 0x00028604);
	/* I2C bypass enable */
	regmap_write(rt5514->i2c_regmap, 0xfafafafa, 0x00000001);
	/* mini-core reset */
	regmap_write(rt5514->i2c_regmap, 0x18002f00, 0x0005514b);
	regmap_write(rt5514->i2c_regmap, 0x18002f00, 0x00055149);
	/* I2C bypass disable */
	regmap_write(rt5514->i2c_regmap, 0xfafafafa, 0x00000000);
	/* PIN config */
	regmap_write(rt5514->i2c_regmap, 0x18002070, 0x00000040);
	/* PLL3(QN)=RCOSC*(10+2) */
	regmap_write(rt5514->i2c_regmap, 0x18002240, 0x0000000a);
	/* PLL3 source=RCOSC, fsi=rt_clk */
	regmap_write(rt5514->i2c_regmap, 0x18002100, 0x0000000b);
	/* Power on RCOSC, pll3 */
	regmap_write(rt5514->i2c_regmap, 0x18002004, 0x00808b81);
	/* DSP clk source = pll3, ENABLE DSP clk */
	regmap_write(rt5514->i2c_regmap, 0x18002f08, 0x00000005);
	/* Enable DSP clk auto switch */
	regmap_write(rt5514->i2c_regmap, 0x18001114, 0x00000001);
	/* Reduce DSP power */
	regmap_write(rt5514->i2c_regmap, 0x18001118, 0x00000001);
}

static bool rt5514_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5514_VENDOR_ID1:
	case RT5514_VENDOR_ID2:
		return true;

	default:
		return false;
	}
}

static bool rt5514_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5514_RESET:
	case RT5514_PWR_ANA1:
	case RT5514_PWR_ANA2:
	case RT5514_I2S_CTRL1:
	case RT5514_I2S_CTRL2:
	case RT5514_VAD_CTRL6:
	case RT5514_EXT_VAD_CTRL:
	case RT5514_DIG_IO_CTRL:
	case RT5514_PAD_CTRL1:
	case RT5514_DMIC_DATA_CTRL:
	case RT5514_DIG_SOURCE_CTRL:
	case RT5514_SRC_CTRL:
	case RT5514_DOWNFILTER2_CTRL1:
	case RT5514_PLL_SOURCE_CTRL:
	case RT5514_CLK_CTRL1:
	case RT5514_CLK_CTRL2:
	case RT5514_PLL3_CALIB_CTRL1:
	case RT5514_PLL3_CALIB_CTRL5:
	case RT5514_DELAY_BUF_CTRL1:
	case RT5514_DELAY_BUF_CTRL3:
	case RT5514_ASRC_IN_CTRL1:
	case RT5514_DOWNFILTER0_CTRL1:
	case RT5514_DOWNFILTER0_CTRL2:
	case RT5514_DOWNFILTER0_CTRL3:
	case RT5514_DOWNFILTER1_CTRL1:
	case RT5514_DOWNFILTER1_CTRL2:
	case RT5514_DOWNFILTER1_CTRL3:
	case RT5514_ANA_CTRL_LDO10:
	case RT5514_ANA_CTRL_LDO18_16:
	case RT5514_ANA_CTRL_ADC12:
	case RT5514_ANA_CTRL_ADC21:
	case RT5514_ANA_CTRL_ADC22:
	case RT5514_ANA_CTRL_ADC23:
	case RT5514_ANA_CTRL_MICBST:
	case RT5514_ANA_CTRL_ADCFED:
	case RT5514_ANA_CTRL_INBUF:
	case RT5514_ANA_CTRL_VREF:
	case RT5514_ANA_CTRL_PLL3:
	case RT5514_ANA_CTRL_PLL1_1:
	case RT5514_ANA_CTRL_PLL1_2:
	case RT5514_DMIC_LP_CTRL:
	case RT5514_MISC_CTRL_DSP:
	case RT5514_DSP_CTRL1:
	case RT5514_DSP_CTRL3:
	case RT5514_DSP_CTRL4:
	case RT5514_VENDOR_ID1:
	case RT5514_VENDOR_ID2:
		return true;

	default:
		return false;
	}
}

static bool rt5514_i2c_readable_register(struct device *dev,
	unsigned int reg)
{
	switch (reg) {
	case RT5514_DSP_MAPPING | RT5514_RESET:
	case RT5514_DSP_MAPPING | RT5514_PWR_ANA1:
	case RT5514_DSP_MAPPING | RT5514_PWR_ANA2:
	case RT5514_DSP_MAPPING | RT5514_I2S_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_I2S_CTRL2:
	case RT5514_DSP_MAPPING | RT5514_VAD_CTRL6:
	case RT5514_DSP_MAPPING | RT5514_EXT_VAD_CTRL:
	case RT5514_DSP_MAPPING | RT5514_DIG_IO_CTRL:
	case RT5514_DSP_MAPPING | RT5514_PAD_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_DMIC_DATA_CTRL:
	case RT5514_DSP_MAPPING | RT5514_DIG_SOURCE_CTRL:
	case RT5514_DSP_MAPPING | RT5514_SRC_CTRL:
	case RT5514_DSP_MAPPING | RT5514_DOWNFILTER2_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_PLL_SOURCE_CTRL:
	case RT5514_DSP_MAPPING | RT5514_CLK_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_CLK_CTRL2:
	case RT5514_DSP_MAPPING | RT5514_PLL3_CALIB_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_PLL3_CALIB_CTRL5:
	case RT5514_DSP_MAPPING | RT5514_DELAY_BUF_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_DELAY_BUF_CTRL3:
	case RT5514_DSP_MAPPING | RT5514_ASRC_IN_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_DOWNFILTER0_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_DOWNFILTER0_CTRL2:
	case RT5514_DSP_MAPPING | RT5514_DOWNFILTER0_CTRL3:
	case RT5514_DSP_MAPPING | RT5514_DOWNFILTER1_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_DOWNFILTER1_CTRL2:
	case RT5514_DSP_MAPPING | RT5514_DOWNFILTER1_CTRL3:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_LDO10:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_LDO18_16:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_ADC12:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_ADC21:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_ADC22:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_ADC23:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_MICBST:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_ADCFED:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_INBUF:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_VREF:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_PLL3:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_PLL1_1:
	case RT5514_DSP_MAPPING | RT5514_ANA_CTRL_PLL1_2:
	case RT5514_DSP_MAPPING | RT5514_DMIC_LP_CTRL:
	case RT5514_DSP_MAPPING | RT5514_MISC_CTRL_DSP:
	case RT5514_DSP_MAPPING | RT5514_DSP_CTRL1:
	case RT5514_DSP_MAPPING | RT5514_DSP_CTRL3:
	case RT5514_DSP_MAPPING | RT5514_DSP_CTRL4:
	case RT5514_DSP_MAPPING | RT5514_VENDOR_ID1:
	case RT5514_DSP_MAPPING | RT5514_VENDOR_ID2:
		return true;

	default:
		return false;
	}
}

/* {-3, 0, +3, +4.5, +7.5, +9.5, +12, +14, +17} dB */
static const DECLARE_TLV_DB_RANGE(bst_tlv,
	0, 2, TLV_DB_SCALE_ITEM(-300, 300, 0),
	3, 3, TLV_DB_SCALE_ITEM(450, 0, 0),
	4, 4, TLV_DB_SCALE_ITEM(750, 0, 0),
	5, 5, TLV_DB_SCALE_ITEM(950, 0, 0),
	6, 6, TLV_DB_SCALE_ITEM(1200, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(1400, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(1700, 0, 0)
);

static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -1725, 75, 0);

static int rt5514_dsp_voice_wake_up_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rt5514->dsp_enabled;

	return 0;
}

static int rt5514_calibration(struct rt5514_priv *rt5514, bool on)
{
	if (on) {
		regmap_write(rt5514->regmap, RT5514_ANA_CTRL_PLL3, 0x0000000a);
		regmap_update_bits(rt5514->regmap, RT5514_PLL_SOURCE_CTRL, 0xf,
			0xa);
		regmap_update_bits(rt5514->regmap, RT5514_PWR_ANA1, 0x301,
			0x301);
		regmap_write(rt5514->regmap, RT5514_PLL3_CALIB_CTRL4,
			0x80000000 | rt5514->pll3_cal_value);
		regmap_write(rt5514->regmap, RT5514_PLL3_CALIB_CTRL1,
			0x8bb80800);
		regmap_update_bits(rt5514->regmap, RT5514_PLL3_CALIB_CTRL5,
			0xc0000000, 0x80000000);
		regmap_update_bits(rt5514->regmap, RT5514_PLL3_CALIB_CTRL5,
			0xc0000000, 0xc0000000);
	} else {
		regmap_update_bits(rt5514->regmap, RT5514_PLL3_CALIB_CTRL5,
			0xc0000000, 0x40000000);
		regmap_update_bits(rt5514->regmap, RT5514_PWR_ANA1, 0x301, 0);
		regmap_update_bits(rt5514->regmap, RT5514_PLL_SOURCE_CTRL, 0xf,
			0x4);
	}

	return 0;
}

static int rt5514_dsp_voice_wake_up_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);
	const struct firmware *fw = NULL;
	u8 buf[8];

	if (ucontrol->value.integer.value[0] == rt5514->dsp_enabled)
		return 0;

	if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
		rt5514->dsp_enabled = ucontrol->value.integer.value[0];

		if (rt5514->dsp_enabled) {
			if (rt5514->pdata.dsp_calib_clk_name &&
				!IS_ERR(rt5514->dsp_calib_clk)) {
				if (clk_set_rate(rt5514->dsp_calib_clk,
					rt5514->pdata.dsp_calib_clk_rate))
					dev_err(component->dev,
						"Can't set rate for mclk");

				if (clk_prepare_enable(rt5514->dsp_calib_clk))
					dev_err(component->dev,
						"Can't enable dsp_calib_clk");

				rt5514_calibration(rt5514, true);

				msleep(20);
#if IS_ENABLED(CONFIG_SND_SOC_RT5514_SPI)
				rt5514_spi_burst_read(RT5514_PLL3_CALIB_CTRL6 |
					RT5514_DSP_MAPPING, buf, sizeof(buf));
#else
				dev_err(component->dev, "There is no SPI driver for"
					" loading the firmware\n");
				memset(buf, 0, sizeof(buf));
#endif
				rt5514->pll3_cal_value = buf[0] | buf[1] << 8 |
					buf[2] << 16 | buf[3] << 24;

				rt5514_calibration(rt5514, false);
				clk_disable_unprepare(rt5514->dsp_calib_clk);
			}

			rt5514_enable_dsp_prepare(rt5514);

			request_firmware(&fw, RT5514_FIRMWARE1, component->dev);
			if (fw) {
#if IS_ENABLED(CONFIG_SND_SOC_RT5514_SPI)
				rt5514_spi_burst_write(0x4ff60000, fw->data,
					((fw->size/8)+1)*8);
#else
				dev_err(component->dev, "There is no SPI driver for"
					" loading the firmware\n");
#endif
				release_firmware(fw);
				fw = NULL;
			}

			request_firmware(&fw, RT5514_FIRMWARE2, component->dev);
			if (fw) {
#if IS_ENABLED(CONFIG_SND_SOC_RT5514_SPI)
				rt5514_spi_burst_write(0x4ffc0000, fw->data,
					((fw->size/8)+1)*8);
#else
				dev_err(component->dev, "There is no SPI driver for"
					" loading the firmware\n");
#endif
				release_firmware(fw);
				fw = NULL;
			}

			/* DSP run */
			regmap_write(rt5514->i2c_regmap, 0x18002f00,
				0x00055148);

			if (rt5514->pdata.dsp_calib_clk_name &&
				!IS_ERR(rt5514->dsp_calib_clk)) {
				msleep(20);

				regmap_write(rt5514->i2c_regmap, 0x1800211c,
					rt5514->pll3_cal_value);
				regmap_write(rt5514->i2c_regmap, 0x18002124,
					0x00220012);
				regmap_write(rt5514->i2c_regmap, 0x18002124,
					0x80220042);
				regmap_write(rt5514->i2c_regmap, 0x18002124,
					0xe0220042);
			}
		} else {
			regmap_multi_reg_write(rt5514->i2c_regmap,
				rt5514_i2c_patch, ARRAY_SIZE(rt5514_i2c_patch));
			regcache_mark_dirty(rt5514->regmap);
			regcache_sync(rt5514->regmap);
		}
	}

	return 0;
}

static const struct snd_kcontrol_new rt5514_snd_controls[] = {
	SOC_DOUBLE_TLV("MIC Boost Volume", RT5514_ANA_CTRL_MICBST,
		RT5514_SEL_BSTL_SFT, RT5514_SEL_BSTR_SFT, 8, 0, bst_tlv),
	SOC_DOUBLE_R_TLV("ADC1 Capture Volume", RT5514_DOWNFILTER0_CTRL1,
		RT5514_DOWNFILTER0_CTRL2, RT5514_AD_GAIN_SFT, 63, 0,
		adc_vol_tlv),
	SOC_DOUBLE_R_TLV("ADC2 Capture Volume", RT5514_DOWNFILTER1_CTRL1,
		RT5514_DOWNFILTER1_CTRL2, RT5514_AD_GAIN_SFT, 63, 0,
		adc_vol_tlv),
	SOC_SINGLE_EXT("DSP Voice Wake Up", SND_SOC_NOPM, 0, 1, 0,
		rt5514_dsp_voice_wake_up_get, rt5514_dsp_voice_wake_up_put),
};

/* ADC Mixer*/
static const struct snd_kcontrol_new rt5514_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("DMIC Switch", RT5514_DOWNFILTER0_CTRL1,
		RT5514_AD_DMIC_MIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("ADC Switch", RT5514_DOWNFILTER0_CTRL1,
		RT5514_AD_AD_MIX_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5514_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("DMIC Switch", RT5514_DOWNFILTER0_CTRL2,
		RT5514_AD_DMIC_MIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("ADC Switch", RT5514_DOWNFILTER0_CTRL2,
		RT5514_AD_AD_MIX_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5514_sto2_adc_l_mix[] = {
	SOC_DAPM_SINGLE("DMIC Switch", RT5514_DOWNFILTER1_CTRL1,
		RT5514_AD_DMIC_MIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("ADC Switch", RT5514_DOWNFILTER1_CTRL1,
		RT5514_AD_AD_MIX_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5514_sto2_adc_r_mix[] = {
	SOC_DAPM_SINGLE("DMIC Switch", RT5514_DOWNFILTER1_CTRL2,
		RT5514_AD_DMIC_MIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("ADC Switch", RT5514_DOWNFILTER1_CTRL2,
		RT5514_AD_AD_MIX_BIT, 1, 1),
};

/* DMIC Source */
static const char * const rt5514_dmic_src[] = {
	"DMIC1", "DMIC2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5514_stereo1_dmic_enum, RT5514_DIG_SOURCE_CTRL,
	RT5514_AD0_DMIC_INPUT_SEL_SFT, rt5514_dmic_src);

static const struct snd_kcontrol_new rt5514_sto1_dmic_mux =
	SOC_DAPM_ENUM("Stereo1 DMIC Source", rt5514_stereo1_dmic_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5514_stereo2_dmic_enum, RT5514_DIG_SOURCE_CTRL,
	RT5514_AD1_DMIC_INPUT_SEL_SFT, rt5514_dmic_src);

static const struct snd_kcontrol_new rt5514_sto2_dmic_mux =
	SOC_DAPM_ENUM("Stereo2 DMIC Source", rt5514_stereo2_dmic_enum);

/**
 * rt5514_calc_dmic_clk - Calculate the frequency divider parameter of dmic.
 *
 * @rate: base clock rate.
 *
 * Choose divider parameter that gives the highest possible DMIC frequency in
 * 1MHz - 3MHz range.
 */
static int rt5514_calc_dmic_clk(struct snd_soc_component *component, int rate)
{
	int div[] = {2, 3, 4, 8, 12, 16, 24, 32};
	int i;

	if (rate < 1000000 * div[0]) {
		pr_warn("Base clock rate %d is too low\n", rate);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(div); i++) {
		/* find divider that gives DMIC frequency below 3.072MHz */
		if (3072000 * div[i] >= rate)
			return i;
	}

	dev_warn(component->dev, "Base clock rate %d is too high\n", rate);
	return -EINVAL;
}

static int rt5514_set_dmic_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);
	int idx;

	idx = rt5514_calc_dmic_clk(component, rt5514->sysclk);
	if (idx < 0)
		dev_err(component->dev, "Failed to set DMIC clock\n");
	else
		regmap_update_bits(rt5514->regmap, RT5514_CLK_CTRL1,
			RT5514_CLK_DMIC_OUT_SEL_MASK,
			idx << RT5514_CLK_DMIC_OUT_SEL_SFT);

	if (rt5514->pdata.dmic_init_delay)
		msleep(rt5514->pdata.dmic_init_delay);

	return idx;
}

static int rt5514_is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(source->dapm);
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);

	if (rt5514->sysclk_src == RT5514_SCLK_S_PLL1)
		return 1;
	else
		return 0;
}

static int rt5514_i2s_use_asrc(struct snd_soc_dapm_widget *source,
	struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(source->dapm);
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);

	return (rt5514->sysclk > rt5514->lrck * 384);
}

static const struct snd_soc_dapm_widget rt5514_dapm_widgets[] = {
	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC1L"),
	SND_SOC_DAPM_INPUT("DMIC1R"),
	SND_SOC_DAPM_INPUT("DMIC2L"),
	SND_SOC_DAPM_INPUT("DMIC2R"),

	SND_SOC_DAPM_INPUT("AMICL"),
	SND_SOC_DAPM_INPUT("AMICR"),

	SND_SOC_DAPM_PGA("DMIC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC2", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("DMIC CLK", 1, SND_SOC_NOPM, 0, 0,
		rt5514_set_dmic_clk, SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_SUPPLY("ADC CLK", RT5514_CLK_CTRL1,
		RT5514_CLK_AD_ANA1_EN_BIT, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("LDO18 IN", RT5514_PWR_ANA1,
		RT5514_POW_LDO18_IN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LDO18 ADC", RT5514_PWR_ANA1,
		RT5514_POW_LDO18_ADC_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LDO21", RT5514_PWR_ANA1, RT5514_POW_LDO21_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("BG LDO18 IN", RT5514_PWR_ANA1,
		RT5514_POW_BG_LDO18_IN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BG LDO21", RT5514_PWR_ANA1,
		RT5514_POW_BG_LDO21_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BG MBIAS", RT5514_PWR_ANA2,
		RT5514_POW_BG_MBIAS_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MBIAS", RT5514_PWR_ANA2, RT5514_POW_MBIAS_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("VREF2", RT5514_PWR_ANA2, RT5514_POW_VREF2_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("VREF1", RT5514_PWR_ANA2, RT5514_POW_VREF1_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Power", SND_SOC_NOPM, 0, 0, NULL, 0),


	SND_SOC_DAPM_SUPPLY("LDO16L", RT5514_PWR_ANA2, RT5514_POWL_LDO16_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1L", RT5514_PWR_ANA2, RT5514_POW_ADC1_L_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("BSTL2", RT5514_PWR_ANA2, RT5514_POW2_BSTL_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("BSTL", RT5514_PWR_ANA2, RT5514_POW_BSTL_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADCFEDL", RT5514_PWR_ANA2, RT5514_POW_ADCFEDL_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADCL Power", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("LDO16R", RT5514_PWR_ANA2, RT5514_POWR_LDO16_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1R", RT5514_PWR_ANA2, RT5514_POW_ADC1_R_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("BSTR2", RT5514_PWR_ANA2, RT5514_POW2_BSTR_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("BSTR", RT5514_PWR_ANA2, RT5514_POW_BSTR_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADCFEDR", RT5514_PWR_ANA2, RT5514_POW_ADCFEDR_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADCR Power", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("PLL1 LDO ENABLE", RT5514_ANA_CTRL_PLL1_2,
		RT5514_EN_LDO_PLL1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL1 LDO", RT5514_PWR_ANA2,
		RT5514_POW_PLL1_LDO_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL1", RT5514_PWR_ANA2, RT5514_POW_PLL1_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ASRC AD1", 1, RT5514_CLK_CTRL2,
		RT5514_CLK_AD0_ASRC_EN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ASRC AD2", 1, RT5514_CLK_CTRL2,
		RT5514_CLK_AD1_ASRC_EN_BIT, 0, NULL, 0),

	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo1 DMIC Mux", SND_SOC_NOPM, 0, 0,
				&rt5514_sto1_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo2 DMIC Mux", SND_SOC_NOPM, 0, 0,
				&rt5514_sto2_dmic_mux),

	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("adc stereo1 filter", RT5514_CLK_CTRL1,
		RT5514_CLK_AD0_EN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("adc stereo2 filter", RT5514_CLK_CTRL1,
		RT5514_CLK_AD1_EN_BIT, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("Sto1 ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5514_sto1_adc_l_mix, ARRAY_SIZE(rt5514_sto1_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Sto1 ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5514_sto1_adc_r_mix, ARRAY_SIZE(rt5514_sto1_adc_r_mix)),
	SND_SOC_DAPM_MIXER("Sto2 ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5514_sto2_adc_l_mix, ARRAY_SIZE(rt5514_sto2_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Sto2 ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5514_sto2_adc_r_mix, ARRAY_SIZE(rt5514_sto2_adc_r_mix)),

	SND_SOC_DAPM_ADC("Stereo1 ADC MIXL", NULL, RT5514_DOWNFILTER0_CTRL1,
		RT5514_AD_AD_MUTE_BIT, 1),
	SND_SOC_DAPM_ADC("Stereo1 ADC MIXR", NULL, RT5514_DOWNFILTER0_CTRL2,
		RT5514_AD_AD_MUTE_BIT, 1),
	SND_SOC_DAPM_ADC("Stereo2 ADC MIXL", NULL, RT5514_DOWNFILTER1_CTRL1,
		RT5514_AD_AD_MUTE_BIT, 1),
	SND_SOC_DAPM_ADC("Stereo2 ADC MIXR", NULL, RT5514_DOWNFILTER1_CTRL2,
		RT5514_AD_AD_MUTE_BIT, 1),

	/* ADC PGA */
	SND_SOC_DAPM_PGA("Stereo1 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo2 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt5514_dapm_routes[] = {
	{ "DMIC1", NULL, "DMIC1L" },
	{ "DMIC1", NULL, "DMIC1R" },
	{ "DMIC2", NULL, "DMIC2L" },
	{ "DMIC2", NULL, "DMIC2R" },

	{ "DMIC1L", NULL, "DMIC CLK" },
	{ "DMIC1R", NULL, "DMIC CLK" },
	{ "DMIC2L", NULL, "DMIC CLK" },
	{ "DMIC2R", NULL, "DMIC CLK" },

	{ "Stereo1 DMIC Mux", "DMIC1", "DMIC1" },
	{ "Stereo1 DMIC Mux", "DMIC2", "DMIC2" },

	{ "Sto1 ADC MIXL", "DMIC Switch", "Stereo1 DMIC Mux" },
	{ "Sto1 ADC MIXL", "ADC Switch", "AMICL" },
	{ "Sto1 ADC MIXR", "DMIC Switch", "Stereo1 DMIC Mux" },
	{ "Sto1 ADC MIXR", "ADC Switch", "AMICR" },

	{ "ADC Power", NULL, "LDO18 IN" },
	{ "ADC Power", NULL, "LDO18 ADC" },
	{ "ADC Power", NULL, "LDO21" },
	{ "ADC Power", NULL, "BG LDO18 IN" },
	{ "ADC Power", NULL, "BG LDO21" },
	{ "ADC Power", NULL, "BG MBIAS" },
	{ "ADC Power", NULL, "MBIAS" },
	{ "ADC Power", NULL, "VREF2" },
	{ "ADC Power", NULL, "VREF1" },

	{ "ADCL Power", NULL, "LDO16L" },
	{ "ADCL Power", NULL, "ADC1L" },
	{ "ADCL Power", NULL, "BSTL2" },
	{ "ADCL Power", NULL, "BSTL" },
	{ "ADCL Power", NULL, "ADCFEDL" },

	{ "ADCR Power", NULL, "LDO16R" },
	{ "ADCR Power", NULL, "ADC1R" },
	{ "ADCR Power", NULL, "BSTR2" },
	{ "ADCR Power", NULL, "BSTR" },
	{ "ADCR Power", NULL, "ADCFEDR" },

	{ "AMICL", NULL, "ADC CLK" },
	{ "AMICL", NULL, "ADC Power" },
	{ "AMICL", NULL, "ADCL Power" },
	{ "AMICR", NULL, "ADC CLK" },
	{ "AMICR", NULL, "ADC Power" },
	{ "AMICR", NULL, "ADCR Power" },

	{ "PLL1 LDO", NULL, "PLL1 LDO ENABLE" },
	{ "PLL1", NULL, "PLL1 LDO" },

	{ "Stereo1 ADC MIXL", NULL, "Sto1 ADC MIXL" },
	{ "Stereo1 ADC MIXR", NULL, "Sto1 ADC MIXR" },

	{ "Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXL" },
	{ "Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXR" },
	{ "Stereo1 ADC MIX", NULL, "adc stereo1 filter" },
	{ "adc stereo1 filter", NULL, "PLL1", rt5514_is_sys_clk_from_pll },
	{ "adc stereo1 filter", NULL, "ASRC AD1", rt5514_i2s_use_asrc },

	{ "Stereo2 DMIC Mux", "DMIC1", "DMIC1" },
	{ "Stereo2 DMIC Mux", "DMIC2", "DMIC2" },

	{ "Sto2 ADC MIXL", "DMIC Switch", "Stereo2 DMIC Mux" },
	{ "Sto2 ADC MIXL", "ADC Switch", "AMICL" },
	{ "Sto2 ADC MIXR", "DMIC Switch", "Stereo2 DMIC Mux" },
	{ "Sto2 ADC MIXR", "ADC Switch", "AMICR" },

	{ "Stereo2 ADC MIXL", NULL, "Sto2 ADC MIXL" },
	{ "Stereo2 ADC MIXR", NULL, "Sto2 ADC MIXR" },

	{ "Stereo2 ADC MIX", NULL, "Stereo2 ADC MIXL" },
	{ "Stereo2 ADC MIX", NULL, "Stereo2 ADC MIXR" },
	{ "Stereo2 ADC MIX", NULL, "adc stereo2 filter" },
	{ "adc stereo2 filter", NULL, "PLL1", rt5514_is_sys_clk_from_pll },
	{ "adc stereo2 filter", NULL, "ASRC AD2", rt5514_i2s_use_asrc },

	{ "AIF1TX", NULL, "Stereo1 ADC MIX"},
	{ "AIF1TX", NULL, "Stereo2 ADC MIX"},
};

static int rt5514_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);
	int pre_div, bclk_ms, frame_size;
	unsigned int val_len = 0;

	rt5514->lrck = params_rate(params);
	pre_div = rl6231_get_clk_info(rt5514->sysclk, rt5514->lrck);
	if (pre_div < 0) {
		dev_err(component->dev, "Unsupported clock setting\n");
		return -EINVAL;
	}

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}

	bclk_ms = frame_size > 32;
	rt5514->bclk = rt5514->lrck * (32 << bclk_ms);

	dev_dbg(dai->dev, "bclk is %dHz and lrck is %dHz\n",
		rt5514->bclk, rt5514->lrck);
	dev_dbg(dai->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val_len = RT5514_I2S_DL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val_len = RT5514_I2S_DL_24;
		break;
	case SNDRV_PCM_FORMAT_S8:
		val_len = RT5514_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(rt5514->regmap, RT5514_I2S_CTRL1, RT5514_I2S_DL_MASK,
		val_len);
	regmap_update_bits(rt5514->regmap, RT5514_CLK_CTRL1,
		RT5514_CLK_AD_ANA1_SEL_MASK,
		(pre_div + 1) << RT5514_CLK_AD_ANA1_SEL_SFT);
	regmap_update_bits(rt5514->regmap, RT5514_CLK_CTRL2,
		RT5514_CLK_SYS_DIV_OUT_MASK | RT5514_SEL_ADC_OSR_MASK,
		pre_div << RT5514_CLK_SYS_DIV_OUT_SFT |
		pre_div << RT5514_SEL_ADC_OSR_SFT);

	return 0;
}

static int rt5514_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;

	case SND_SOC_DAIFMT_NB_IF:
		reg_val |= RT5514_I2S_LR_INV;
		break;

	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5514_I2S_BP_INV;
		break;

	case SND_SOC_DAIFMT_IB_IF:
		reg_val |= RT5514_I2S_BP_INV | RT5514_I2S_LR_INV;
		break;

	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5514_I2S_DF_LEFT;
		break;

	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5514_I2S_DF_PCM_A;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5514_I2S_DF_PCM_B;
		break;

	default:
		return -EINVAL;
	}

	regmap_update_bits(rt5514->regmap, RT5514_I2S_CTRL1,
		RT5514_I2S_DF_MASK | RT5514_I2S_BP_MASK | RT5514_I2S_LR_MASK,
		reg_val);

	return 0;
}

static int rt5514_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	if (freq == rt5514->sysclk && clk_id == rt5514->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5514_SCLK_S_MCLK:
		reg_val |= RT5514_CLK_SYS_PRE_SEL_MCLK;
		break;

	case RT5514_SCLK_S_PLL1:
		reg_val |= RT5514_CLK_SYS_PRE_SEL_PLL;
		break;

	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	regmap_update_bits(rt5514->regmap, RT5514_CLK_CTRL2,
		RT5514_CLK_SYS_PRE_SEL_MASK, reg_val);

	rt5514->sysclk = freq;
	rt5514->sysclk_src = clk_id;

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);

	return 0;
}

static int rt5514_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_component *component = dai->component;
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code;
	int ret;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt5514->pll_in = 0;
		rt5514->pll_out = 0;
		regmap_update_bits(rt5514->regmap, RT5514_CLK_CTRL2,
			RT5514_CLK_SYS_PRE_SEL_MASK,
			RT5514_CLK_SYS_PRE_SEL_MCLK);

		return 0;
	}

	if (source == rt5514->pll_src && freq_in == rt5514->pll_in &&
	    freq_out == rt5514->pll_out)
		return 0;

	switch (source) {
	case RT5514_PLL1_S_MCLK:
		regmap_update_bits(rt5514->regmap, RT5514_PLL_SOURCE_CTRL,
			RT5514_PLL_1_SEL_MASK, RT5514_PLL_1_SEL_MCLK);
		break;

	case RT5514_PLL1_S_BCLK:
		regmap_update_bits(rt5514->regmap, RT5514_PLL_SOURCE_CTRL,
			RT5514_PLL_1_SEL_MASK, RT5514_PLL_1_SEL_SCLK);
		break;

	default:
		dev_err(component->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(component->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(component->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	regmap_write(rt5514->regmap, RT5514_ANA_CTRL_PLL1_1,
		pll_code.k_code << RT5514_PLL_K_SFT |
		pll_code.n_code << RT5514_PLL_N_SFT |
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT5514_PLL_M_SFT);
	regmap_update_bits(rt5514->regmap, RT5514_ANA_CTRL_PLL1_2,
		RT5514_PLL_M_BP, pll_code.m_bp << RT5514_PLL_M_BP_SFT);

	rt5514->pll_in = freq_in;
	rt5514->pll_out = freq_out;
	rt5514->pll_src = source;

	return 0;
}

static int rt5514_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);
	unsigned int val = 0, val2 = 0;

	if (rx_mask || tx_mask)
		val |= RT5514_TDM_MODE;

	switch (tx_mask) {
	case 0x3:
		val2 |= RT5514_TDM_DOCKING_MODE | RT5514_TDM_DOCKING_VALID_CH2 |
			RT5514_TDM_DOCKING_START_SLOT0;
		break;

	case 0x30:
		val2 |= RT5514_TDM_DOCKING_MODE | RT5514_TDM_DOCKING_VALID_CH2 |
			RT5514_TDM_DOCKING_START_SLOT4;
		break;

	case 0xf:
		val2 |= RT5514_TDM_DOCKING_MODE | RT5514_TDM_DOCKING_VALID_CH4 |
			RT5514_TDM_DOCKING_START_SLOT0;
		break;

	case 0xf0:
		val2 |= RT5514_TDM_DOCKING_MODE | RT5514_TDM_DOCKING_VALID_CH4 |
			RT5514_TDM_DOCKING_START_SLOT4;
		break;

	default:
		break;
	}



	switch (slots) {
	case 4:
		val |= RT5514_TDMSLOT_SEL_RX_4CH | RT5514_TDMSLOT_SEL_TX_4CH;
		break;

	case 6:
		val |= RT5514_TDMSLOT_SEL_RX_6CH | RT5514_TDMSLOT_SEL_TX_6CH;
		break;

	case 8:
		val |= RT5514_TDMSLOT_SEL_RX_8CH | RT5514_TDMSLOT_SEL_TX_8CH;
		break;

	case 2:
	default:
		break;
	}

	switch (slot_width) {
	case 20:
		val |= RT5514_CH_LEN_RX_20 | RT5514_CH_LEN_TX_20;
		break;

	case 24:
		val |= RT5514_CH_LEN_RX_24 | RT5514_CH_LEN_TX_24;
		break;

	case 25:
		val |= RT5514_TDM_MODE2;
		break;

	case 32:
		val |= RT5514_CH_LEN_RX_32 | RT5514_CH_LEN_TX_32;
		break;

	case 16:
	default:
		break;
	}

	regmap_update_bits(rt5514->regmap, RT5514_I2S_CTRL1, RT5514_TDM_MODE |
		RT5514_TDMSLOT_SEL_RX_MASK | RT5514_TDMSLOT_SEL_TX_MASK |
		RT5514_CH_LEN_RX_MASK | RT5514_CH_LEN_TX_MASK |
		RT5514_TDM_MODE2, val);

	regmap_update_bits(rt5514->regmap, RT5514_I2S_CTRL2,
		RT5514_TDM_DOCKING_MODE | RT5514_TDM_DOCKING_VALID_CH_MASK |
		RT5514_TDM_DOCKING_START_MASK, val2);

	return 0;
}

static int rt5514_set_bias_level(struct snd_soc_component *component,
			enum snd_soc_bias_level level)
{
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (IS_ERR(rt5514->mclk))
			break;

		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_ON) {
			clk_disable_unprepare(rt5514->mclk);
		} else {
			ret = clk_prepare_enable(rt5514->mclk);
			if (ret)
				return ret;
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			/*
			 * If the DSP is enabled in start of recording, the DSP
			 * should be disabled, and sync back to normal recording
			 * settings to make sure recording properly.
			 */
			if (rt5514->dsp_enabled) {
				rt5514->dsp_enabled = 0;
				regmap_multi_reg_write(rt5514->i2c_regmap,
					rt5514_i2c_patch,
					ARRAY_SIZE(rt5514_i2c_patch));
				regcache_mark_dirty(rt5514->regmap);
				regcache_sync(rt5514->regmap);
			}
		}
		break;

	default:
		break;
	}

	return 0;
}

static int rt5514_probe(struct snd_soc_component *component)
{
	struct rt5514_priv *rt5514 = snd_soc_component_get_drvdata(component);
	struct platform_device *pdev = container_of(component->dev,
						   struct platform_device, dev);

	rt5514->mclk = devm_clk_get(component->dev, "mclk");
	if (PTR_ERR(rt5514->mclk) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (rt5514->pdata.dsp_calib_clk_name) {
		rt5514->dsp_calib_clk = devm_clk_get(&pdev->dev,
				rt5514->pdata.dsp_calib_clk_name);
		if (PTR_ERR(rt5514->dsp_calib_clk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	}

	rt5514->component = component;
	rt5514->pll3_cal_value = 0x0078b000;

	return 0;
}

static int rt5514_i2c_read(void *context, unsigned int reg, unsigned int *val)
{
	struct i2c_client *client = context;
	struct rt5514_priv *rt5514 = i2c_get_clientdata(client);

	regmap_read(rt5514->i2c_regmap, reg | RT5514_DSP_MAPPING, val);

	return 0;
}

static int rt5514_i2c_write(void *context, unsigned int reg, unsigned int val)
{
	struct i2c_client *client = context;
	struct rt5514_priv *rt5514 = i2c_get_clientdata(client);

	regmap_write(rt5514->i2c_regmap, reg | RT5514_DSP_MAPPING, val);

	return 0;
}

#define RT5514_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT5514_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt5514_aif_dai_ops = {
	.hw_params = rt5514_hw_params,
	.set_fmt = rt5514_set_dai_fmt,
	.set_sysclk = rt5514_set_dai_sysclk,
	.set_pll = rt5514_set_dai_pll,
	.set_tdm_slot = rt5514_set_tdm_slot,
};

static struct snd_soc_dai_driver rt5514_dai[] = {
	{
		.name = "rt5514-aif1",
		.id = 0,
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = RT5514_STEREO_RATES,
			.formats = RT5514_FORMATS,
		},
		.ops = &rt5514_aif_dai_ops,
	}
};

static const struct snd_soc_component_driver soc_component_dev_rt5514 = {
	.probe			= rt5514_probe,
	.set_bias_level		= rt5514_set_bias_level,
	.controls		= rt5514_snd_controls,
	.num_controls		= ARRAY_SIZE(rt5514_snd_controls),
	.dapm_widgets		= rt5514_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(rt5514_dapm_widgets),
	.dapm_routes		= rt5514_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(rt5514_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config rt5514_i2c_regmap = {
	.name = "i2c",
	.reg_bits = 32,
	.val_bits = 32,

	.readable_reg = rt5514_i2c_readable_register,

	.cache_type = REGCACHE_NONE,
};

static const struct regmap_config rt5514_regmap = {
	.reg_bits = 16,
	.val_bits = 32,

	.max_register = RT5514_VENDOR_ID2,
	.volatile_reg = rt5514_volatile_register,
	.readable_reg = rt5514_readable_register,
	.reg_read = rt5514_i2c_read,
	.reg_write = rt5514_i2c_write,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5514_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5514_reg),
	.use_single_read = true,
	.use_single_write = true,
};

static const struct i2c_device_id rt5514_i2c_id[] = {
	{ "rt5514", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5514_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id rt5514_of_match[] = {
	{ .compatible = "realtek,rt5514", },
	{},
};
MODULE_DEVICE_TABLE(of, rt5514_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt5514_acpi_match[] = {
	{ "10EC5514", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, rt5514_acpi_match);
#endif

static int rt5514_parse_dp(struct rt5514_priv *rt5514, struct device *dev)
{
	device_property_read_u32(dev, "realtek,dmic-init-delay-ms",
		&rt5514->pdata.dmic_init_delay);
	device_property_read_string(dev, "realtek,dsp-calib-clk-name",
		&rt5514->pdata.dsp_calib_clk_name);
	device_property_read_u32(dev, "realtek,dsp-calib-clk-rate",
		&rt5514->pdata.dsp_calib_clk_rate);

	return 0;
}

static __maybe_unused int rt5514_i2c_resume(struct device *dev)
{
	struct rt5514_priv *rt5514 = dev_get_drvdata(dev);
	unsigned int val;

	/*
	 * Add a bogus read to avoid rt5514's confusion after s2r in case it
	 * saw glitches on the i2c lines and thought the other side sent a
	 * start bit.
	 */
	regmap_read(rt5514->regmap, RT5514_VENDOR_ID2, &val);

	return 0;
}

static int rt5514_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5514_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt5514_priv *rt5514;
	int ret;
	unsigned int val = ~0;

	rt5514 = devm_kzalloc(&i2c->dev, sizeof(struct rt5514_priv),
				GFP_KERNEL);
	if (rt5514 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5514);

	if (pdata)
		rt5514->pdata = *pdata;
	else
		rt5514_parse_dp(rt5514, &i2c->dev);

	rt5514->i2c_regmap = devm_regmap_init_i2c(i2c, &rt5514_i2c_regmap);
	if (IS_ERR(rt5514->i2c_regmap)) {
		ret = PTR_ERR(rt5514->i2c_regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	rt5514->regmap = devm_regmap_init(&i2c->dev, NULL, i2c, &rt5514_regmap);
	if (IS_ERR(rt5514->regmap)) {
		ret = PTR_ERR(rt5514->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	/*
	 * The rt5514 can get confused if the i2c lines glitch together, as
	 * can happen at bootup as regulators are turned off and on.  If it's
	 * in this glitched state the first i2c read will fail, so we'll give
	 * it one change to retry.
	 */
	ret = regmap_read(rt5514->regmap, RT5514_VENDOR_ID2, &val);
	if (ret || val != RT5514_DEVICE_ID)
		ret = regmap_read(rt5514->regmap, RT5514_VENDOR_ID2, &val);
	if (ret || val != RT5514_DEVICE_ID) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt5514\n", val);
		return -ENODEV;
	}

	ret = regmap_multi_reg_write(rt5514->i2c_regmap, rt5514_i2c_patch,
				    ARRAY_SIZE(rt5514_i2c_patch));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply i2c_regmap patch: %d\n",
			ret);

	ret = regmap_register_patch(rt5514->regmap, rt5514_patch,
				    ARRAY_SIZE(rt5514_patch));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply regmap patch: %d\n", ret);

	return devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_rt5514,
			rt5514_dai, ARRAY_SIZE(rt5514_dai));
}

static const struct dev_pm_ops rt5514_i2_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, rt5514_i2c_resume)
};

static struct i2c_driver rt5514_i2c_driver = {
	.driver = {
		.name = "rt5514",
		.acpi_match_table = ACPI_PTR(rt5514_acpi_match),
		.of_match_table = of_match_ptr(rt5514_of_match),
		.pm = &rt5514_i2_pm_ops,
	},
	.probe = rt5514_i2c_probe,
	.id_table = rt5514_i2c_id,
};
module_i2c_driver(rt5514_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5514 driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
