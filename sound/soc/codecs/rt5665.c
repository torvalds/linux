/*
 * rt5665.c  --  RT5665/RT5658 ALSA SoC audio codec driver
 *
 * Copyright 2016 Realtek Semiconductor Corp.
 * Author: Bard Liao <bardliao@realtek.com>
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
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/rt5665.h>

#include "rl6231.h"
#include "rt5665.h"

#define RT5665_NUM_SUPPLIES 3

static const char *rt5665_supply_names[RT5665_NUM_SUPPLIES] = {
	"AVDD",
	"MICVDD",
	"VBAT",
};

struct rt5665_priv {
	struct snd_soc_codec *codec;
	struct rt5665_platform_data pdata;
	struct regmap *regmap;
	struct gpio_desc *gpiod_ldo1_en;
	struct gpio_desc *gpiod_reset;
	struct snd_soc_jack *hs_jack;
	struct regulator_bulk_data supplies[RT5665_NUM_SUPPLIES];
	struct delayed_work jack_detect_work;
	struct delayed_work calibrate_work;
	struct delayed_work jd_check_work;
	struct mutex calibrate_mutex;

	int sysclk;
	int sysclk_src;
	int lrck[RT5665_AIFS];
	int bclk[RT5665_AIFS];
	int master[RT5665_AIFS];
	int id;

	int pll_src;
	int pll_in;
	int pll_out;

	int jack_type;
	int irq_work_delay_time;
	unsigned int sar_adc_value;
	bool calibration_done;
};

static const struct reg_default rt5665_reg[] = {
	{0x0000, 0x0000},
	{0x0001, 0xc8c8},
	{0x0002, 0x8080},
	{0x0003, 0x8000},
	{0x0004, 0xc80a},
	{0x0005, 0x0000},
	{0x0006, 0x0000},
	{0x0007, 0x0000},
	{0x000a, 0x0000},
	{0x000b, 0x0000},
	{0x000c, 0x0000},
	{0x000d, 0x0000},
	{0x000f, 0x0808},
	{0x0010, 0x4040},
	{0x0011, 0x0000},
	{0x0012, 0x1404},
	{0x0013, 0x1000},
	{0x0014, 0xa00a},
	{0x0015, 0x0404},
	{0x0016, 0x0404},
	{0x0017, 0x0011},
	{0x0018, 0xafaf},
	{0x0019, 0xafaf},
	{0x001a, 0xafaf},
	{0x001b, 0x0011},
	{0x001c, 0x2f2f},
	{0x001d, 0x2f2f},
	{0x001e, 0x2f2f},
	{0x001f, 0x0000},
	{0x0020, 0x0000},
	{0x0021, 0x0000},
	{0x0022, 0x5757},
	{0x0023, 0x0039},
	{0x0026, 0xc0c0},
	{0x0027, 0xc0c0},
	{0x0028, 0xc0c0},
	{0x0029, 0x8080},
	{0x002a, 0xaaaa},
	{0x002b, 0xaaaa},
	{0x002c, 0xaba8},
	{0x002d, 0x0000},
	{0x002e, 0x0000},
	{0x002f, 0x0000},
	{0x0030, 0x0000},
	{0x0031, 0x5000},
	{0x0032, 0x0000},
	{0x0033, 0x0000},
	{0x0034, 0x0000},
	{0x0035, 0x0000},
	{0x003a, 0x0000},
	{0x003b, 0x0000},
	{0x003c, 0x00ff},
	{0x003d, 0x0000},
	{0x003e, 0x00ff},
	{0x003f, 0x0000},
	{0x0040, 0x0000},
	{0x0041, 0x00ff},
	{0x0042, 0x0000},
	{0x0043, 0x00ff},
	{0x0044, 0x0c0c},
	{0x0049, 0xc00b},
	{0x004a, 0x0000},
	{0x004b, 0x031f},
	{0x004d, 0x0000},
	{0x004e, 0x001f},
	{0x004f, 0x0000},
	{0x0050, 0x001f},
	{0x0052, 0xf000},
	{0x0061, 0x0000},
	{0x0062, 0x0000},
	{0x0063, 0x003e},
	{0x0064, 0x0000},
	{0x0065, 0x0000},
	{0x0066, 0x003f},
	{0x0067, 0x0000},
	{0x006b, 0x0000},
	{0x006d, 0xff00},
	{0x006e, 0x2808},
	{0x006f, 0x000a},
	{0x0070, 0x8000},
	{0x0071, 0x8000},
	{0x0072, 0x8000},
	{0x0073, 0x7000},
	{0x0074, 0x7770},
	{0x0075, 0x0002},
	{0x0076, 0x0001},
	{0x0078, 0x00f0},
	{0x0079, 0x0000},
	{0x007a, 0x0000},
	{0x007b, 0x0000},
	{0x007c, 0x0000},
	{0x007d, 0x0123},
	{0x007e, 0x4500},
	{0x007f, 0x8003},
	{0x0080, 0x0000},
	{0x0081, 0x0000},
	{0x0082, 0x0000},
	{0x0083, 0x0000},
	{0x0084, 0x0000},
	{0x0085, 0x0000},
	{0x0086, 0x0008},
	{0x0087, 0x0000},
	{0x0088, 0x0000},
	{0x0089, 0x0000},
	{0x008a, 0x0000},
	{0x008b, 0x0000},
	{0x008c, 0x0003},
	{0x008e, 0x0060},
	{0x008f, 0x1000},
	{0x0091, 0x0c26},
	{0x0092, 0x0073},
	{0x0093, 0x0000},
	{0x0094, 0x0080},
	{0x0098, 0x0000},
	{0x0099, 0x0000},
	{0x009a, 0x0007},
	{0x009f, 0x0000},
	{0x00a0, 0x0000},
	{0x00a1, 0x0002},
	{0x00a2, 0x0001},
	{0x00a3, 0x0002},
	{0x00a4, 0x0001},
	{0x00ae, 0x2040},
	{0x00af, 0x0000},
	{0x00b6, 0x0000},
	{0x00b7, 0x0000},
	{0x00b8, 0x0000},
	{0x00b9, 0x0000},
	{0x00ba, 0x0002},
	{0x00bb, 0x0000},
	{0x00be, 0x0000},
	{0x00c0, 0x0000},
	{0x00c1, 0x0aaa},
	{0x00c2, 0xaa80},
	{0x00c3, 0x0003},
	{0x00c4, 0x0000},
	{0x00d0, 0x0000},
	{0x00d1, 0x2244},
	{0x00d3, 0x3300},
	{0x00d4, 0x2200},
	{0x00d9, 0x0809},
	{0x00da, 0x0000},
	{0x00db, 0x0008},
	{0x00dc, 0x00c0},
	{0x00dd, 0x6724},
	{0x00de, 0x3131},
	{0x00df, 0x0008},
	{0x00e0, 0x4000},
	{0x00e1, 0x3131},
	{0x00e2, 0x600c},
	{0x00ea, 0xb320},
	{0x00eb, 0x0000},
	{0x00ec, 0xb300},
	{0x00ed, 0x0000},
	{0x00ee, 0xb320},
	{0x00ef, 0x0000},
	{0x00f0, 0x0201},
	{0x00f1, 0x0ddd},
	{0x00f2, 0x0ddd},
	{0x00f6, 0x0000},
	{0x00f7, 0x0000},
	{0x00f8, 0x0000},
	{0x00fa, 0x0000},
	{0x00fb, 0x0000},
	{0x00fc, 0x0000},
	{0x00fd, 0x0000},
	{0x00fe, 0x10ec},
	{0x00ff, 0x6451},
	{0x0100, 0xaaaa},
	{0x0101, 0x000a},
	{0x010a, 0xaaaa},
	{0x010b, 0xa0a0},
	{0x010c, 0xaeae},
	{0x010d, 0xaaaa},
	{0x010e, 0xaaaa},
	{0x010f, 0xaaaa},
	{0x0110, 0xe002},
	{0x0111, 0xa402},
	{0x0112, 0xaaaa},
	{0x0113, 0x2000},
	{0x0117, 0x0f00},
	{0x0125, 0x0410},
	{0x0132, 0x0000},
	{0x0133, 0x0000},
	{0x0137, 0x5540},
	{0x0138, 0x3700},
	{0x0139, 0x79a1},
	{0x013a, 0x2020},
	{0x013b, 0x2020},
	{0x013c, 0x2005},
	{0x013f, 0x0000},
	{0x0145, 0x0002},
	{0x0146, 0x0000},
	{0x0147, 0x0000},
	{0x0148, 0x0000},
	{0x0150, 0x0000},
	{0x0160, 0x4eff},
	{0x0161, 0x0080},
	{0x0162, 0x0200},
	{0x0163, 0x0800},
	{0x0164, 0x0000},
	{0x0165, 0x0000},
	{0x0166, 0x0000},
	{0x0167, 0x000f},
	{0x0170, 0x4e87},
	{0x0171, 0x0080},
	{0x0172, 0x0200},
	{0x0173, 0x0800},
	{0x0174, 0x00ff},
	{0x0175, 0x0000},
	{0x0190, 0x413d},
	{0x0191, 0x4139},
	{0x0192, 0x4135},
	{0x0193, 0x413d},
	{0x0194, 0x0000},
	{0x0195, 0x0000},
	{0x0196, 0x0000},
	{0x0197, 0x0000},
	{0x0198, 0x0000},
	{0x0199, 0x0000},
	{0x01a0, 0x1e64},
	{0x01a1, 0x06a3},
	{0x01a2, 0x0000},
	{0x01a3, 0x0000},
	{0x01a4, 0x0000},
	{0x01a5, 0x0000},
	{0x01a6, 0x0000},
	{0x01a7, 0x8000},
	{0x01a8, 0x0000},
	{0x01a9, 0x0000},
	{0x01aa, 0x0000},
	{0x01ab, 0x0000},
	{0x01b5, 0x0000},
	{0x01b6, 0x01c3},
	{0x01b7, 0x02a0},
	{0x01b8, 0x03e9},
	{0x01b9, 0x1389},
	{0x01ba, 0xc351},
	{0x01bb, 0x0009},
	{0x01bc, 0x0018},
	{0x01bd, 0x002a},
	{0x01be, 0x004c},
	{0x01bf, 0x0097},
	{0x01c0, 0x433d},
	{0x01c1, 0x0000},
	{0x01c2, 0x0000},
	{0x01c3, 0x0000},
	{0x01c4, 0x0000},
	{0x01c5, 0x0000},
	{0x01c6, 0x0000},
	{0x01c7, 0x0000},
	{0x01c8, 0x40af},
	{0x01c9, 0x0702},
	{0x01ca, 0x0000},
	{0x01cb, 0x0000},
	{0x01cc, 0x5757},
	{0x01cd, 0x5757},
	{0x01ce, 0x5757},
	{0x01cf, 0x5757},
	{0x01d0, 0x5757},
	{0x01d1, 0x5757},
	{0x01d2, 0x5757},
	{0x01d3, 0x5757},
	{0x01d4, 0x5757},
	{0x01d5, 0x5757},
	{0x01d6, 0x003c},
	{0x01da, 0x0000},
	{0x01db, 0x0000},
	{0x01dc, 0x0000},
	{0x01de, 0x7c00},
	{0x01df, 0x0320},
	{0x01e0, 0x06a1},
	{0x01e1, 0x0000},
	{0x01e2, 0x0000},
	{0x01e3, 0x0000},
	{0x01e4, 0x0000},
	{0x01e6, 0x0001},
	{0x01e7, 0x0000},
	{0x01e8, 0x0000},
	{0x01ea, 0xbf3f},
	{0x01eb, 0x0000},
	{0x01ec, 0x0000},
	{0x01ed, 0x0000},
	{0x01ee, 0x0000},
	{0x01ef, 0x0000},
	{0x01f0, 0x0000},
	{0x01f1, 0x0000},
	{0x01f2, 0x0000},
	{0x01f3, 0x0000},
	{0x01f4, 0x0000},
	{0x0200, 0x0000},
	{0x0201, 0x0000},
	{0x0202, 0x0000},
	{0x0203, 0x0000},
	{0x0204, 0x0000},
	{0x0205, 0x0000},
	{0x0206, 0x0000},
	{0x0207, 0x0000},
	{0x0208, 0x0000},
	{0x0210, 0x60b1},
	{0x0211, 0xa005},
	{0x0212, 0x024c},
	{0x0213, 0xf7ff},
	{0x0214, 0x024c},
	{0x0215, 0x0102},
	{0x0216, 0x00a3},
	{0x0217, 0x0048},
	{0x0218, 0xa2c0},
	{0x0219, 0x0400},
	{0x021a, 0x00c8},
	{0x021b, 0x00c0},
	{0x02ff, 0x0110},
	{0x0300, 0x001f},
	{0x0301, 0x032c},
	{0x0302, 0x5f21},
	{0x0303, 0x4000},
	{0x0304, 0x4000},
	{0x0305, 0x06d5},
	{0x0306, 0x8000},
	{0x0307, 0x0700},
	{0x0310, 0x4560},
	{0x0311, 0xa4a8},
	{0x0312, 0x7418},
	{0x0313, 0x0000},
	{0x0314, 0x0006},
	{0x0315, 0xffff},
	{0x0316, 0xc400},
	{0x0317, 0x0000},
	{0x0330, 0x00a6},
	{0x0331, 0x04c3},
	{0x0332, 0x27c8},
	{0x0333, 0xbf50},
	{0x0334, 0x0045},
	{0x0335, 0x0007},
	{0x0336, 0x7418},
	{0x0337, 0x0501},
	{0x0338, 0x0000},
	{0x0339, 0x0010},
	{0x033a, 0x1010},
	{0x03c0, 0x7e00},
	{0x03c1, 0x8000},
	{0x03c2, 0x8000},
	{0x03c3, 0x8000},
	{0x03c4, 0x8000},
	{0x03c5, 0x8000},
	{0x03c6, 0x8000},
	{0x03c7, 0x8000},
	{0x03c8, 0x8000},
	{0x03c9, 0x8000},
	{0x03ca, 0x8000},
	{0x03cb, 0x8000},
	{0x03cc, 0x8000},
	{0x03d0, 0x0000},
	{0x03d1, 0x0000},
	{0x03d2, 0x0000},
	{0x03d3, 0x0000},
	{0x03d4, 0x2000},
	{0x03d5, 0x2000},
	{0x03d6, 0x0000},
	{0x03d7, 0x0000},
	{0x03d8, 0x2000},
	{0x03d9, 0x2000},
	{0x03da, 0x2000},
	{0x03db, 0x2000},
	{0x03dc, 0x0000},
	{0x03dd, 0x0000},
	{0x03de, 0x0000},
	{0x03df, 0x2000},
	{0x03e0, 0x0000},
	{0x03e1, 0x0000},
	{0x03e2, 0x0000},
	{0x03e3, 0x0000},
	{0x03e4, 0x0000},
	{0x03e5, 0x0000},
	{0x03e6, 0x0000},
	{0x03e7, 0x0000},
	{0x03e8, 0x0000},
	{0x03e9, 0x0000},
	{0x03ea, 0x0000},
	{0x03eb, 0x0000},
	{0x03ec, 0x0000},
	{0x03ed, 0x0000},
	{0x03ee, 0x0000},
	{0x03ef, 0x0000},
	{0x03f0, 0x0800},
	{0x03f1, 0x0800},
	{0x03f2, 0x0800},
	{0x03f3, 0x0800},
};

static bool rt5665_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5665_RESET:
	case RT5665_EJD_CTRL_2:
	case RT5665_GPIO_STA:
	case RT5665_INT_ST_1:
	case RT5665_IL_CMD_1:
	case RT5665_4BTN_IL_CMD_1:
	case RT5665_PSV_IL_CMD_1:
	case RT5665_AJD1_CTRL:
	case RT5665_JD_CTRL_3:
	case RT5665_STO_NG2_CTRL_1:
	case RT5665_SAR_IL_CMD_4:
	case RT5665_DEVICE_ID:
	case RT5665_STO1_DAC_SIL_DET ... RT5665_STO2_DAC_SIL_DET:
	case RT5665_MONO_AMP_CALIB_STA1 ... RT5665_MONO_AMP_CALIB_STA6:
	case RT5665_HP_IMP_SENS_CTRL_12 ... RT5665_HP_IMP_SENS_CTRL_15:
	case RT5665_HP_CALIB_STA_1 ... RT5665_HP_CALIB_STA_11:
		return true;
	default:
		return false;
	}
}

static bool rt5665_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5665_RESET:
	case RT5665_VENDOR_ID:
	case RT5665_VENDOR_ID_1:
	case RT5665_DEVICE_ID:
	case RT5665_LOUT:
	case RT5665_HP_CTRL_1:
	case RT5665_HP_CTRL_2:
	case RT5665_MONO_OUT:
	case RT5665_HPL_GAIN:
	case RT5665_HPR_GAIN:
	case RT5665_MONO_GAIN:
	case RT5665_CAL_BST_CTRL:
	case RT5665_CBJ_BST_CTRL:
	case RT5665_IN1_IN2:
	case RT5665_IN3_IN4:
	case RT5665_INL1_INR1_VOL:
	case RT5665_EJD_CTRL_1:
	case RT5665_EJD_CTRL_2:
	case RT5665_EJD_CTRL_3:
	case RT5665_EJD_CTRL_4:
	case RT5665_EJD_CTRL_5:
	case RT5665_EJD_CTRL_6:
	case RT5665_EJD_CTRL_7:
	case RT5665_DAC2_CTRL:
	case RT5665_DAC2_DIG_VOL:
	case RT5665_DAC1_DIG_VOL:
	case RT5665_DAC3_DIG_VOL:
	case RT5665_DAC3_CTRL:
	case RT5665_STO1_ADC_DIG_VOL:
	case RT5665_MONO_ADC_DIG_VOL:
	case RT5665_STO2_ADC_DIG_VOL:
	case RT5665_STO1_ADC_BOOST:
	case RT5665_MONO_ADC_BOOST:
	case RT5665_STO2_ADC_BOOST:
	case RT5665_HP_IMP_GAIN_1:
	case RT5665_HP_IMP_GAIN_2:
	case RT5665_STO1_ADC_MIXER:
	case RT5665_MONO_ADC_MIXER:
	case RT5665_STO2_ADC_MIXER:
	case RT5665_AD_DA_MIXER:
	case RT5665_STO1_DAC_MIXER:
	case RT5665_MONO_DAC_MIXER:
	case RT5665_STO2_DAC_MIXER:
	case RT5665_A_DAC1_MUX:
	case RT5665_A_DAC2_MUX:
	case RT5665_DIG_INF2_DATA:
	case RT5665_DIG_INF3_DATA:
	case RT5665_PDM_OUT_CTRL:
	case RT5665_PDM_DATA_CTRL_1:
	case RT5665_PDM_DATA_CTRL_2:
	case RT5665_PDM_DATA_CTRL_3:
	case RT5665_PDM_DATA_CTRL_4:
	case RT5665_REC1_GAIN:
	case RT5665_REC1_L1_MIXER:
	case RT5665_REC1_L2_MIXER:
	case RT5665_REC1_R1_MIXER:
	case RT5665_REC1_R2_MIXER:
	case RT5665_REC2_GAIN:
	case RT5665_REC2_L1_MIXER:
	case RT5665_REC2_L2_MIXER:
	case RT5665_REC2_R1_MIXER:
	case RT5665_REC2_R2_MIXER:
	case RT5665_CAL_REC:
	case RT5665_ALC_BACK_GAIN:
	case RT5665_MONOMIX_GAIN:
	case RT5665_MONOMIX_IN_GAIN:
	case RT5665_OUT_L_GAIN:
	case RT5665_OUT_L_MIXER:
	case RT5665_OUT_R_GAIN:
	case RT5665_OUT_R_MIXER:
	case RT5665_LOUT_MIXER:
	case RT5665_PWR_DIG_1:
	case RT5665_PWR_DIG_2:
	case RT5665_PWR_ANLG_1:
	case RT5665_PWR_ANLG_2:
	case RT5665_PWR_ANLG_3:
	case RT5665_PWR_MIXER:
	case RT5665_PWR_VOL:
	case RT5665_CLK_DET:
	case RT5665_HPF_CTRL1:
	case RT5665_DMIC_CTRL_1:
	case RT5665_DMIC_CTRL_2:
	case RT5665_I2S1_SDP:
	case RT5665_I2S2_SDP:
	case RT5665_I2S3_SDP:
	case RT5665_ADDA_CLK_1:
	case RT5665_ADDA_CLK_2:
	case RT5665_I2S1_F_DIV_CTRL_1:
	case RT5665_I2S1_F_DIV_CTRL_2:
	case RT5665_TDM_CTRL_1:
	case RT5665_TDM_CTRL_2:
	case RT5665_TDM_CTRL_3:
	case RT5665_TDM_CTRL_4:
	case RT5665_TDM_CTRL_5:
	case RT5665_TDM_CTRL_6:
	case RT5665_TDM_CTRL_7:
	case RT5665_TDM_CTRL_8:
	case RT5665_GLB_CLK:
	case RT5665_PLL_CTRL_1:
	case RT5665_PLL_CTRL_2:
	case RT5665_ASRC_1:
	case RT5665_ASRC_2:
	case RT5665_ASRC_3:
	case RT5665_ASRC_4:
	case RT5665_ASRC_5:
	case RT5665_ASRC_6:
	case RT5665_ASRC_7:
	case RT5665_ASRC_8:
	case RT5665_ASRC_9:
	case RT5665_ASRC_10:
	case RT5665_DEPOP_1:
	case RT5665_DEPOP_2:
	case RT5665_HP_CHARGE_PUMP_1:
	case RT5665_HP_CHARGE_PUMP_2:
	case RT5665_MICBIAS_1:
	case RT5665_MICBIAS_2:
	case RT5665_ASRC_12:
	case RT5665_ASRC_13:
	case RT5665_ASRC_14:
	case RT5665_RC_CLK_CTRL:
	case RT5665_I2S_M_CLK_CTRL_1:
	case RT5665_I2S2_F_DIV_CTRL_1:
	case RT5665_I2S2_F_DIV_CTRL_2:
	case RT5665_I2S3_F_DIV_CTRL_1:
	case RT5665_I2S3_F_DIV_CTRL_2:
	case RT5665_EQ_CTRL_1:
	case RT5665_EQ_CTRL_2:
	case RT5665_IRQ_CTRL_1:
	case RT5665_IRQ_CTRL_2:
	case RT5665_IRQ_CTRL_3:
	case RT5665_IRQ_CTRL_4:
	case RT5665_IRQ_CTRL_5:
	case RT5665_IRQ_CTRL_6:
	case RT5665_INT_ST_1:
	case RT5665_GPIO_CTRL_1:
	case RT5665_GPIO_CTRL_2:
	case RT5665_GPIO_CTRL_3:
	case RT5665_GPIO_CTRL_4:
	case RT5665_GPIO_STA:
	case RT5665_HP_AMP_DET_CTRL_1:
	case RT5665_HP_AMP_DET_CTRL_2:
	case RT5665_MID_HP_AMP_DET:
	case RT5665_LOW_HP_AMP_DET:
	case RT5665_SV_ZCD_1:
	case RT5665_SV_ZCD_2:
	case RT5665_IL_CMD_1:
	case RT5665_IL_CMD_2:
	case RT5665_IL_CMD_3:
	case RT5665_IL_CMD_4:
	case RT5665_4BTN_IL_CMD_1:
	case RT5665_4BTN_IL_CMD_2:
	case RT5665_4BTN_IL_CMD_3:
	case RT5665_PSV_IL_CMD_1:
	case RT5665_ADC_STO1_HP_CTRL_1:
	case RT5665_ADC_STO1_HP_CTRL_2:
	case RT5665_ADC_MONO_HP_CTRL_1:
	case RT5665_ADC_MONO_HP_CTRL_2:
	case RT5665_ADC_STO2_HP_CTRL_1:
	case RT5665_ADC_STO2_HP_CTRL_2:
	case RT5665_AJD1_CTRL:
	case RT5665_JD1_THD:
	case RT5665_JD2_THD:
	case RT5665_JD_CTRL_1:
	case RT5665_JD_CTRL_2:
	case RT5665_JD_CTRL_3:
	case RT5665_DIG_MISC:
	case RT5665_DUMMY_2:
	case RT5665_DUMMY_3:
	case RT5665_DAC_ADC_DIG_VOL1:
	case RT5665_DAC_ADC_DIG_VOL2:
	case RT5665_BIAS_CUR_CTRL_1:
	case RT5665_BIAS_CUR_CTRL_2:
	case RT5665_BIAS_CUR_CTRL_3:
	case RT5665_BIAS_CUR_CTRL_4:
	case RT5665_BIAS_CUR_CTRL_5:
	case RT5665_BIAS_CUR_CTRL_6:
	case RT5665_BIAS_CUR_CTRL_7:
	case RT5665_BIAS_CUR_CTRL_8:
	case RT5665_BIAS_CUR_CTRL_9:
	case RT5665_BIAS_CUR_CTRL_10:
	case RT5665_VREF_REC_OP_FB_CAP_CTRL:
	case RT5665_CHARGE_PUMP_1:
	case RT5665_DIG_IN_CTRL_1:
	case RT5665_DIG_IN_CTRL_2:
	case RT5665_PAD_DRIVING_CTRL:
	case RT5665_SOFT_RAMP_DEPOP:
	case RT5665_PLL:
	case RT5665_CHOP_DAC:
	case RT5665_CHOP_ADC:
	case RT5665_CALIB_ADC_CTRL:
	case RT5665_VOL_TEST:
	case RT5665_TEST_MODE_CTRL_1:
	case RT5665_TEST_MODE_CTRL_2:
	case RT5665_TEST_MODE_CTRL_3:
	case RT5665_TEST_MODE_CTRL_4:
	case RT5665_BASSBACK_CTRL:
	case RT5665_STO_NG2_CTRL_1:
	case RT5665_STO_NG2_CTRL_2:
	case RT5665_STO_NG2_CTRL_3:
	case RT5665_STO_NG2_CTRL_4:
	case RT5665_STO_NG2_CTRL_5:
	case RT5665_STO_NG2_CTRL_6:
	case RT5665_STO_NG2_CTRL_7:
	case RT5665_STO_NG2_CTRL_8:
	case RT5665_MONO_NG2_CTRL_1:
	case RT5665_MONO_NG2_CTRL_2:
	case RT5665_MONO_NG2_CTRL_3:
	case RT5665_MONO_NG2_CTRL_4:
	case RT5665_MONO_NG2_CTRL_5:
	case RT5665_MONO_NG2_CTRL_6:
	case RT5665_STO1_DAC_SIL_DET:
	case RT5665_MONOL_DAC_SIL_DET:
	case RT5665_MONOR_DAC_SIL_DET:
	case RT5665_STO2_DAC_SIL_DET:
	case RT5665_SIL_PSV_CTRL1:
	case RT5665_SIL_PSV_CTRL2:
	case RT5665_SIL_PSV_CTRL3:
	case RT5665_SIL_PSV_CTRL4:
	case RT5665_SIL_PSV_CTRL5:
	case RT5665_SIL_PSV_CTRL6:
	case RT5665_MONO_AMP_CALIB_CTRL_1:
	case RT5665_MONO_AMP_CALIB_CTRL_2:
	case RT5665_MONO_AMP_CALIB_CTRL_3:
	case RT5665_MONO_AMP_CALIB_CTRL_4:
	case RT5665_MONO_AMP_CALIB_CTRL_5:
	case RT5665_MONO_AMP_CALIB_CTRL_6:
	case RT5665_MONO_AMP_CALIB_CTRL_7:
	case RT5665_MONO_AMP_CALIB_STA1:
	case RT5665_MONO_AMP_CALIB_STA2:
	case RT5665_MONO_AMP_CALIB_STA3:
	case RT5665_MONO_AMP_CALIB_STA4:
	case RT5665_MONO_AMP_CALIB_STA6:
	case RT5665_HP_IMP_SENS_CTRL_01:
	case RT5665_HP_IMP_SENS_CTRL_02:
	case RT5665_HP_IMP_SENS_CTRL_03:
	case RT5665_HP_IMP_SENS_CTRL_04:
	case RT5665_HP_IMP_SENS_CTRL_05:
	case RT5665_HP_IMP_SENS_CTRL_06:
	case RT5665_HP_IMP_SENS_CTRL_07:
	case RT5665_HP_IMP_SENS_CTRL_08:
	case RT5665_HP_IMP_SENS_CTRL_09:
	case RT5665_HP_IMP_SENS_CTRL_10:
	case RT5665_HP_IMP_SENS_CTRL_11:
	case RT5665_HP_IMP_SENS_CTRL_12:
	case RT5665_HP_IMP_SENS_CTRL_13:
	case RT5665_HP_IMP_SENS_CTRL_14:
	case RT5665_HP_IMP_SENS_CTRL_15:
	case RT5665_HP_IMP_SENS_CTRL_16:
	case RT5665_HP_IMP_SENS_CTRL_17:
	case RT5665_HP_IMP_SENS_CTRL_18:
	case RT5665_HP_IMP_SENS_CTRL_19:
	case RT5665_HP_IMP_SENS_CTRL_20:
	case RT5665_HP_IMP_SENS_CTRL_21:
	case RT5665_HP_IMP_SENS_CTRL_22:
	case RT5665_HP_IMP_SENS_CTRL_23:
	case RT5665_HP_IMP_SENS_CTRL_24:
	case RT5665_HP_IMP_SENS_CTRL_25:
	case RT5665_HP_IMP_SENS_CTRL_26:
	case RT5665_HP_IMP_SENS_CTRL_27:
	case RT5665_HP_IMP_SENS_CTRL_28:
	case RT5665_HP_IMP_SENS_CTRL_29:
	case RT5665_HP_IMP_SENS_CTRL_30:
	case RT5665_HP_IMP_SENS_CTRL_31:
	case RT5665_HP_IMP_SENS_CTRL_32:
	case RT5665_HP_IMP_SENS_CTRL_33:
	case RT5665_HP_IMP_SENS_CTRL_34:
	case RT5665_HP_LOGIC_CTRL_1:
	case RT5665_HP_LOGIC_CTRL_2:
	case RT5665_HP_LOGIC_CTRL_3:
	case RT5665_HP_CALIB_CTRL_1:
	case RT5665_HP_CALIB_CTRL_2:
	case RT5665_HP_CALIB_CTRL_3:
	case RT5665_HP_CALIB_CTRL_4:
	case RT5665_HP_CALIB_CTRL_5:
	case RT5665_HP_CALIB_CTRL_6:
	case RT5665_HP_CALIB_CTRL_7:
	case RT5665_HP_CALIB_CTRL_9:
	case RT5665_HP_CALIB_CTRL_10:
	case RT5665_HP_CALIB_CTRL_11:
	case RT5665_HP_CALIB_STA_1:
	case RT5665_HP_CALIB_STA_2:
	case RT5665_HP_CALIB_STA_3:
	case RT5665_HP_CALIB_STA_4:
	case RT5665_HP_CALIB_STA_5:
	case RT5665_HP_CALIB_STA_6:
	case RT5665_HP_CALIB_STA_7:
	case RT5665_HP_CALIB_STA_8:
	case RT5665_HP_CALIB_STA_9:
	case RT5665_HP_CALIB_STA_10:
	case RT5665_HP_CALIB_STA_11:
	case RT5665_PGM_TAB_CTRL1:
	case RT5665_PGM_TAB_CTRL2:
	case RT5665_PGM_TAB_CTRL3:
	case RT5665_PGM_TAB_CTRL4:
	case RT5665_PGM_TAB_CTRL5:
	case RT5665_PGM_TAB_CTRL6:
	case RT5665_PGM_TAB_CTRL7:
	case RT5665_PGM_TAB_CTRL8:
	case RT5665_PGM_TAB_CTRL9:
	case RT5665_SAR_IL_CMD_1:
	case RT5665_SAR_IL_CMD_2:
	case RT5665_SAR_IL_CMD_3:
	case RT5665_SAR_IL_CMD_4:
	case RT5665_SAR_IL_CMD_5:
	case RT5665_SAR_IL_CMD_6:
	case RT5665_SAR_IL_CMD_7:
	case RT5665_SAR_IL_CMD_8:
	case RT5665_SAR_IL_CMD_9:
	case RT5665_SAR_IL_CMD_10:
	case RT5665_SAR_IL_CMD_11:
	case RT5665_SAR_IL_CMD_12:
	case RT5665_DRC1_CTRL_0:
	case RT5665_DRC1_CTRL_1:
	case RT5665_DRC1_CTRL_2:
	case RT5665_DRC1_CTRL_3:
	case RT5665_DRC1_CTRL_4:
	case RT5665_DRC1_CTRL_5:
	case RT5665_DRC1_CTRL_6:
	case RT5665_DRC1_HARD_LMT_CTRL_1:
	case RT5665_DRC1_HARD_LMT_CTRL_2:
	case RT5665_DRC1_PRIV_1:
	case RT5665_DRC1_PRIV_2:
	case RT5665_DRC1_PRIV_3:
	case RT5665_DRC1_PRIV_4:
	case RT5665_DRC1_PRIV_5:
	case RT5665_DRC1_PRIV_6:
	case RT5665_DRC1_PRIV_7:
	case RT5665_DRC1_PRIV_8:
	case RT5665_ALC_PGA_CTRL_1:
	case RT5665_ALC_PGA_CTRL_2:
	case RT5665_ALC_PGA_CTRL_3:
	case RT5665_ALC_PGA_CTRL_4:
	case RT5665_ALC_PGA_CTRL_5:
	case RT5665_ALC_PGA_CTRL_6:
	case RT5665_ALC_PGA_CTRL_7:
	case RT5665_ALC_PGA_CTRL_8:
	case RT5665_ALC_PGA_STA_1:
	case RT5665_ALC_PGA_STA_2:
	case RT5665_ALC_PGA_STA_3:
	case RT5665_EQ_AUTO_RCV_CTRL1:
	case RT5665_EQ_AUTO_RCV_CTRL2:
	case RT5665_EQ_AUTO_RCV_CTRL3:
	case RT5665_EQ_AUTO_RCV_CTRL4:
	case RT5665_EQ_AUTO_RCV_CTRL5:
	case RT5665_EQ_AUTO_RCV_CTRL6:
	case RT5665_EQ_AUTO_RCV_CTRL7:
	case RT5665_EQ_AUTO_RCV_CTRL8:
	case RT5665_EQ_AUTO_RCV_CTRL9:
	case RT5665_EQ_AUTO_RCV_CTRL10:
	case RT5665_EQ_AUTO_RCV_CTRL11:
	case RT5665_EQ_AUTO_RCV_CTRL12:
	case RT5665_EQ_AUTO_RCV_CTRL13:
	case RT5665_ADC_L_EQ_LPF1_A1:
	case RT5665_R_EQ_LPF1_A1:
	case RT5665_L_EQ_LPF1_H0:
	case RT5665_R_EQ_LPF1_H0:
	case RT5665_L_EQ_BPF1_A1:
	case RT5665_R_EQ_BPF1_A1:
	case RT5665_L_EQ_BPF1_A2:
	case RT5665_R_EQ_BPF1_A2:
	case RT5665_L_EQ_BPF1_H0:
	case RT5665_R_EQ_BPF1_H0:
	case RT5665_L_EQ_BPF2_A1:
	case RT5665_R_EQ_BPF2_A1:
	case RT5665_L_EQ_BPF2_A2:
	case RT5665_R_EQ_BPF2_A2:
	case RT5665_L_EQ_BPF2_H0:
	case RT5665_R_EQ_BPF2_H0:
	case RT5665_L_EQ_BPF3_A1:
	case RT5665_R_EQ_BPF3_A1:
	case RT5665_L_EQ_BPF3_A2:
	case RT5665_R_EQ_BPF3_A2:
	case RT5665_L_EQ_BPF3_H0:
	case RT5665_R_EQ_BPF3_H0:
	case RT5665_L_EQ_BPF4_A1:
	case RT5665_R_EQ_BPF4_A1:
	case RT5665_L_EQ_BPF4_A2:
	case RT5665_R_EQ_BPF4_A2:
	case RT5665_L_EQ_BPF4_H0:
	case RT5665_R_EQ_BPF4_H0:
	case RT5665_L_EQ_HPF1_A1:
	case RT5665_R_EQ_HPF1_A1:
	case RT5665_L_EQ_HPF1_H0:
	case RT5665_R_EQ_HPF1_H0:
	case RT5665_L_EQ_PRE_VOL:
	case RT5665_R_EQ_PRE_VOL:
	case RT5665_L_EQ_POST_VOL:
	case RT5665_R_EQ_POST_VOL:
	case RT5665_SCAN_MODE_CTRL:
	case RT5665_I2C_MODE:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(hp_vol_tlv, -2250, 150, 0);
static const DECLARE_TLV_DB_SCALE(mono_vol_tlv, -1400, 150, 0);
static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -65625, 375, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -17625, 375, 0);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);
static const DECLARE_TLV_DB_SCALE(in_bst_tlv, -1200, 75, 0);

/* {0, +20, +24, +30, +35, +40, +44, +50, +52} dB */
static const DECLARE_TLV_DB_RANGE(bst_tlv,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0)
);

/* Interface data select */
static const char * const rt5665_data_select[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static SOC_ENUM_SINGLE_DECL(rt5665_if1_1_01_adc_enum,
	RT5665_TDM_CTRL_2, RT5665_I2S1_1_DS_ADC_SLOT01_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if1_1_23_adc_enum,
	RT5665_TDM_CTRL_2, RT5665_I2S1_1_DS_ADC_SLOT23_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if1_1_45_adc_enum,
	RT5665_TDM_CTRL_2, RT5665_I2S1_1_DS_ADC_SLOT45_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if1_1_67_adc_enum,
	RT5665_TDM_CTRL_2, RT5665_I2S1_1_DS_ADC_SLOT67_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if1_2_01_adc_enum,
	RT5665_TDM_CTRL_2, RT5665_I2S1_2_DS_ADC_SLOT01_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if1_2_23_adc_enum,
	RT5665_TDM_CTRL_2, RT5665_I2S1_2_DS_ADC_SLOT23_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if1_2_45_adc_enum,
	RT5665_TDM_CTRL_2, RT5665_I2S1_2_DS_ADC_SLOT45_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if1_2_67_adc_enum,
	RT5665_TDM_CTRL_2, RT5665_I2S1_2_DS_ADC_SLOT67_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if2_1_dac_enum,
	RT5665_DIG_INF2_DATA, RT5665_IF2_1_DAC_SEL_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if2_1_adc_enum,
	RT5665_DIG_INF2_DATA, RT5665_IF2_1_ADC_SEL_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if2_2_dac_enum,
	RT5665_DIG_INF2_DATA, RT5665_IF2_2_DAC_SEL_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if2_2_adc_enum,
	RT5665_DIG_INF2_DATA, RT5665_IF2_2_ADC_SEL_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if3_dac_enum,
	RT5665_DIG_INF3_DATA, RT5665_IF3_DAC_SEL_SFT, rt5665_data_select);

static SOC_ENUM_SINGLE_DECL(rt5665_if3_adc_enum,
	RT5665_DIG_INF3_DATA, RT5665_IF3_ADC_SEL_SFT, rt5665_data_select);

static const struct snd_kcontrol_new rt5665_if1_1_01_adc_swap_mux =
	SOC_DAPM_ENUM("IF1_1 01 ADC Swap Mux", rt5665_if1_1_01_adc_enum);

static const struct snd_kcontrol_new rt5665_if1_1_23_adc_swap_mux =
	SOC_DAPM_ENUM("IF1_1 23 ADC Swap Mux", rt5665_if1_1_23_adc_enum);

static const struct snd_kcontrol_new rt5665_if1_1_45_adc_swap_mux =
	SOC_DAPM_ENUM("IF1_1 45 ADC Swap Mux", rt5665_if1_1_45_adc_enum);

static const struct snd_kcontrol_new rt5665_if1_1_67_adc_swap_mux =
	SOC_DAPM_ENUM("IF1_1 67 ADC Swap Mux", rt5665_if1_1_67_adc_enum);

static const struct snd_kcontrol_new rt5665_if1_2_01_adc_swap_mux =
	SOC_DAPM_ENUM("IF1_2 01 ADC Swap Mux", rt5665_if1_2_01_adc_enum);

static const struct snd_kcontrol_new rt5665_if1_2_23_adc_swap_mux =
	SOC_DAPM_ENUM("IF1_2 23 ADC1 Swap Mux", rt5665_if1_2_23_adc_enum);

static const struct snd_kcontrol_new rt5665_if1_2_45_adc_swap_mux =
	SOC_DAPM_ENUM("IF1_2 45 ADC1 Swap Mux", rt5665_if1_2_45_adc_enum);

static const struct snd_kcontrol_new rt5665_if1_2_67_adc_swap_mux =
	SOC_DAPM_ENUM("IF1_2 67 ADC1 Swap Mux", rt5665_if1_2_67_adc_enum);

static const struct snd_kcontrol_new rt5665_if2_1_dac_swap_mux =
	SOC_DAPM_ENUM("IF2_1 DAC Swap Source", rt5665_if2_1_dac_enum);

static const struct snd_kcontrol_new rt5665_if2_1_adc_swap_mux =
	SOC_DAPM_ENUM("IF2_1 ADC Swap Source", rt5665_if2_1_adc_enum);

static const struct snd_kcontrol_new rt5665_if2_2_dac_swap_mux =
	SOC_DAPM_ENUM("IF2_2 DAC Swap Source", rt5665_if2_2_dac_enum);

static const struct snd_kcontrol_new rt5665_if2_2_adc_swap_mux =
	SOC_DAPM_ENUM("IF2_2 ADC Swap Source", rt5665_if2_2_adc_enum);

static const struct snd_kcontrol_new rt5665_if3_dac_swap_mux =
	SOC_DAPM_ENUM("IF3 DAC Swap Source", rt5665_if3_dac_enum);

static const struct snd_kcontrol_new rt5665_if3_adc_swap_mux =
	SOC_DAPM_ENUM("IF3 ADC Swap Source", rt5665_if3_adc_enum);

static int rt5665_hp_vol_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	int ret = snd_soc_put_volsw(kcontrol, ucontrol);

	if (snd_soc_read(codec, RT5665_STO_NG2_CTRL_1) & RT5665_NG2_EN) {
		snd_soc_update_bits(codec, RT5665_STO_NG2_CTRL_1,
			RT5665_NG2_EN_MASK, RT5665_NG2_DIS);
		snd_soc_update_bits(codec, RT5665_STO_NG2_CTRL_1,
			RT5665_NG2_EN_MASK, RT5665_NG2_EN);
	}

	return ret;
}

static int rt5665_mono_vol_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	int ret = snd_soc_put_volsw(kcontrol, ucontrol);

	if (snd_soc_read(codec, RT5665_MONO_NG2_CTRL_1) & RT5665_NG2_EN) {
		snd_soc_update_bits(codec, RT5665_MONO_NG2_CTRL_1,
			RT5665_NG2_EN_MASK, RT5665_NG2_DIS);
		snd_soc_update_bits(codec, RT5665_MONO_NG2_CTRL_1,
			RT5665_NG2_EN_MASK, RT5665_NG2_EN);
	}

	return ret;
}

/**
 * rt5665_sel_asrc_clk_src - select ASRC clock source for a set of filters
 * @codec: SoC audio codec device.
 * @filter_mask: mask of filters.
 * @clk_src: clock source
 *
 * The ASRC function is for asynchronous MCLK and LRCK. Also, since RT5665 can
 * only support standard 32fs or 64fs i2s format, ASRC should be enabled to
 * support special i2s clock format such as Intel's 100fs(100 * sampling rate).
 * ASRC function will track i2s clock and generate a corresponding system clock
 * for codec. This function provides an API to select the clock source for a
 * set of filters specified by the mask. And the codec driver will turn on ASRC
 * for these filters if ASRC is selected as their clock source.
 */
int rt5665_sel_asrc_clk_src(struct snd_soc_codec *codec,
		unsigned int filter_mask, unsigned int clk_src)
{
	unsigned int asrc2_mask = 0;
	unsigned int asrc2_value = 0;
	unsigned int asrc3_mask = 0;
	unsigned int asrc3_value = 0;

	switch (clk_src) {
	case RT5665_CLK_SEL_SYS:
	case RT5665_CLK_SEL_I2S1_ASRC:
	case RT5665_CLK_SEL_I2S2_ASRC:
	case RT5665_CLK_SEL_I2S3_ASRC:
	case RT5665_CLK_SEL_SYS2:
	case RT5665_CLK_SEL_SYS3:
	case RT5665_CLK_SEL_SYS4:
		break;

	default:
		return -EINVAL;
	}

	if (filter_mask & RT5665_DA_STEREO1_FILTER) {
		asrc2_mask |= RT5665_DA_STO1_CLK_SEL_MASK;
		asrc2_value = (asrc2_value & ~RT5665_DA_STO1_CLK_SEL_MASK)
			| (clk_src << RT5665_DA_STO1_CLK_SEL_SFT);
	}

	if (filter_mask & RT5665_DA_STEREO2_FILTER) {
		asrc2_mask |= RT5665_DA_STO2_CLK_SEL_MASK;
		asrc2_value = (asrc2_value & ~RT5665_DA_STO2_CLK_SEL_MASK)
			| (clk_src << RT5665_DA_STO2_CLK_SEL_SFT);
	}

	if (filter_mask & RT5665_DA_MONO_L_FILTER) {
		asrc2_mask |= RT5665_DA_MONOL_CLK_SEL_MASK;
		asrc2_value = (asrc2_value & ~RT5665_DA_MONOL_CLK_SEL_MASK)
			| (clk_src << RT5665_DA_MONOL_CLK_SEL_SFT);
	}

	if (filter_mask & RT5665_DA_MONO_R_FILTER) {
		asrc2_mask |= RT5665_DA_MONOR_CLK_SEL_MASK;
		asrc2_value = (asrc2_value & ~RT5665_DA_MONOR_CLK_SEL_MASK)
			| (clk_src << RT5665_DA_MONOR_CLK_SEL_SFT);
	}

	if (filter_mask & RT5665_AD_STEREO1_FILTER) {
		asrc3_mask |= RT5665_AD_STO1_CLK_SEL_MASK;
		asrc3_value = (asrc2_value & ~RT5665_AD_STO1_CLK_SEL_MASK)
			| (clk_src << RT5665_AD_STO1_CLK_SEL_SFT);
	}

	if (filter_mask & RT5665_AD_STEREO2_FILTER) {
		asrc3_mask |= RT5665_AD_STO2_CLK_SEL_MASK;
		asrc3_value = (asrc2_value & ~RT5665_AD_STO2_CLK_SEL_MASK)
			| (clk_src << RT5665_AD_STO2_CLK_SEL_SFT);
	}

	if (filter_mask & RT5665_AD_MONO_L_FILTER) {
		asrc3_mask |= RT5665_AD_MONOL_CLK_SEL_MASK;
		asrc3_value = (asrc3_value & ~RT5665_AD_MONOL_CLK_SEL_MASK)
			| (clk_src << RT5665_AD_MONOL_CLK_SEL_SFT);
	}

	if (filter_mask & RT5665_AD_MONO_R_FILTER)  {
		asrc3_mask |= RT5665_AD_MONOR_CLK_SEL_MASK;
		asrc3_value = (asrc3_value & ~RT5665_AD_MONOR_CLK_SEL_MASK)
			| (clk_src << RT5665_AD_MONOR_CLK_SEL_SFT);
	}

	if (asrc2_mask)
		snd_soc_update_bits(codec, RT5665_ASRC_2,
			asrc2_mask, asrc2_value);

	if (asrc3_mask)
		snd_soc_update_bits(codec, RT5665_ASRC_3,
			asrc3_mask, asrc3_value);

	return 0;
}
EXPORT_SYMBOL_GPL(rt5665_sel_asrc_clk_src);

static int rt5665_button_detect(struct snd_soc_codec *codec)
{
	int btn_type, val;

	val = snd_soc_read(codec, RT5665_4BTN_IL_CMD_1);
	btn_type = val & 0xfff0;
	snd_soc_write(codec, RT5665_4BTN_IL_CMD_1, val);

	return btn_type;
}

static void rt5665_enable_push_button_irq(struct snd_soc_codec *codec,
	bool enable)
{
	if (enable) {
		snd_soc_write(codec, RT5665_4BTN_IL_CMD_1, 0x0003);
		snd_soc_update_bits(codec, RT5665_SAR_IL_CMD_9, 0x1, 0x1);
		snd_soc_write(codec, RT5665_IL_CMD_1, 0x0048);
		snd_soc_update_bits(codec, RT5665_4BTN_IL_CMD_2,
				RT5665_4BTN_IL_MASK | RT5665_4BTN_IL_RST_MASK,
				RT5665_4BTN_IL_EN | RT5665_4BTN_IL_NOR);
		snd_soc_update_bits(codec, RT5665_IRQ_CTRL_3,
				RT5665_IL_IRQ_MASK, RT5665_IL_IRQ_EN);
	} else {
		snd_soc_update_bits(codec, RT5665_IRQ_CTRL_3,
				RT5665_IL_IRQ_MASK, RT5665_IL_IRQ_DIS);
		snd_soc_update_bits(codec, RT5665_4BTN_IL_CMD_2,
				RT5665_4BTN_IL_MASK, RT5665_4BTN_IL_DIS);
		snd_soc_update_bits(codec, RT5665_4BTN_IL_CMD_2,
				RT5665_4BTN_IL_RST_MASK, RT5665_4BTN_IL_RST);
	}
}

/**
 * rt5665_headset_detect - Detect headset.
 * @codec: SoC audio codec device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */
static int rt5665_headset_detect(struct snd_soc_codec *codec, int jack_insert)
{
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	unsigned int sar_hs_type, val;

	if (jack_insert) {
		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS1");
		snd_soc_dapm_sync(dapm);

		regmap_update_bits(rt5665->regmap, RT5665_MICBIAS_2, 0x100,
			0x100);

		regmap_read(rt5665->regmap, RT5665_GPIO_STA, &val);
		if (val & 0x4) {
			regmap_update_bits(rt5665->regmap, RT5665_EJD_CTRL_1,
				0x100, 0);

			regmap_read(rt5665->regmap, RT5665_GPIO_STA, &val);
			while (val & 0x4) {
				usleep_range(10000, 15000);
				regmap_read(rt5665->regmap, RT5665_GPIO_STA,
					&val);
			}
		}

		regmap_update_bits(rt5665->regmap, RT5665_EJD_CTRL_1,
			0x1a0, 0x120);
		regmap_write(rt5665->regmap, RT5665_EJD_CTRL_3, 0x3424);
		regmap_write(rt5665->regmap, RT5665_IL_CMD_1, 0x0048);
		regmap_write(rt5665->regmap, RT5665_SAR_IL_CMD_1, 0xa291);

		usleep_range(10000, 15000);

		rt5665->sar_adc_value = snd_soc_read(rt5665->codec,
			RT5665_SAR_IL_CMD_4) & 0x7ff;

		sar_hs_type = rt5665->pdata.sar_hs_type ?
			rt5665->pdata.sar_hs_type : 729;

		if (rt5665->sar_adc_value > sar_hs_type) {
			rt5665->jack_type = SND_JACK_HEADSET;
			rt5665_enable_push_button_irq(codec, true);
			} else {
			rt5665->jack_type = SND_JACK_HEADPHONE;
			regmap_write(rt5665->regmap, RT5665_SAR_IL_CMD_1,
				0x2291);
			regmap_update_bits(rt5665->regmap, RT5665_MICBIAS_2,
				0x100, 0);
			snd_soc_dapm_disable_pin(dapm, "MICBIAS1");
			snd_soc_dapm_sync(dapm);
		}
	} else {
		regmap_write(rt5665->regmap, RT5665_SAR_IL_CMD_1, 0x2291);
		regmap_update_bits(rt5665->regmap, RT5665_MICBIAS_2, 0x100, 0);
		snd_soc_dapm_disable_pin(dapm, "MICBIAS1");
		snd_soc_dapm_sync(dapm);
		if (rt5665->jack_type == SND_JACK_HEADSET)
			rt5665_enable_push_button_irq(codec, false);
		rt5665->jack_type = 0;
	}

	dev_dbg(codec->dev, "jack_type = %d\n", rt5665->jack_type);
	return rt5665->jack_type;
}

static irqreturn_t rt5665_irq(int irq, void *data)
{
	struct rt5665_priv *rt5665 = data;

	mod_delayed_work(system_power_efficient_wq,
			   &rt5665->jack_detect_work, msecs_to_jiffies(250));

	return IRQ_HANDLED;
}

static void rt5665_jd_check_handler(struct work_struct *work)
{
	struct rt5665_priv *rt5665 = container_of(work, struct rt5665_priv,
		jd_check_work.work);

	if (snd_soc_read(rt5665->codec, RT5665_AJD1_CTRL) & 0x0010) {
		/* jack out */
		rt5665->jack_type = rt5665_headset_detect(rt5665->codec, 0);

		snd_soc_jack_report(rt5665->hs_jack, rt5665->jack_type,
				SND_JACK_HEADSET |
				SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				SND_JACK_BTN_2 | SND_JACK_BTN_3);
	} else {
		schedule_delayed_work(&rt5665->jd_check_work, 500);
	}
}

static int rt5665_set_jack_detect(struct snd_soc_codec *codec,
	struct snd_soc_jack *hs_jack, void *data)
{
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);

	switch (rt5665->pdata.jd_src) {
	case RT5665_JD1:
		regmap_update_bits(rt5665->regmap, RT5665_GPIO_CTRL_1,
			RT5665_GP1_PIN_MASK, RT5665_GP1_PIN_IRQ);
		regmap_update_bits(rt5665->regmap, RT5665_RC_CLK_CTRL,
				0xc000, 0xc000);
		regmap_update_bits(rt5665->regmap, RT5665_PWR_ANLG_2,
			RT5665_PWR_JD1, RT5665_PWR_JD1);
		regmap_update_bits(rt5665->regmap, RT5665_IRQ_CTRL_1, 0x8, 0x8);
		break;

	case RT5665_JD_NULL:
		break;

	default:
		dev_warn(codec->dev, "Wrong JD source\n");
		break;
	}

	rt5665->hs_jack = hs_jack;

	return 0;
}

static void rt5665_jack_detect_handler(struct work_struct *work)
{
	struct rt5665_priv *rt5665 =
		container_of(work, struct rt5665_priv, jack_detect_work.work);
	int val, btn_type;

	while (!rt5665->codec) {
		pr_debug("%s codec = null\n", __func__);
		usleep_range(10000, 15000);
	}

	while (!rt5665->codec->component.card->instantiated) {
		pr_debug("%s\n", __func__);
		usleep_range(10000, 15000);
	}

	while (!rt5665->calibration_done) {
		pr_debug("%s calibration not ready\n", __func__);
		usleep_range(10000, 15000);
	}

	mutex_lock(&rt5665->calibrate_mutex);

	val = snd_soc_read(rt5665->codec, RT5665_AJD1_CTRL) & 0x0010;
	if (!val) {
		/* jack in */
		if (rt5665->jack_type == 0) {
			/* jack was out, report jack type */
			rt5665->jack_type =
				rt5665_headset_detect(rt5665->codec, 1);
		} else {
			/* jack is already in, report button event */
			rt5665->jack_type = SND_JACK_HEADSET;
			btn_type = rt5665_button_detect(rt5665->codec);
			/**
			 * rt5665 can report three kinds of button behavior,
			 * one click, double click and hold. However,
			 * currently we will report button pressed/released
			 * event. So all the three button behaviors are
			 * treated as button pressed.
			 */
			switch (btn_type) {
			case 0x8000:
			case 0x4000:
			case 0x2000:
				rt5665->jack_type |= SND_JACK_BTN_0;
				break;
			case 0x1000:
			case 0x0800:
			case 0x0400:
				rt5665->jack_type |= SND_JACK_BTN_1;
				break;
			case 0x0200:
			case 0x0100:
			case 0x0080:
				rt5665->jack_type |= SND_JACK_BTN_2;
				break;
			case 0x0040:
			case 0x0020:
			case 0x0010:
				rt5665->jack_type |= SND_JACK_BTN_3;
				break;
			case 0x0000: /* unpressed */
				break;
			default:
				btn_type = 0;
				dev_err(rt5665->codec->dev,
					"Unexpected button code 0x%04x\n",
					btn_type);
				break;
			}
		}
	} else {
		/* jack out */
		rt5665->jack_type = rt5665_headset_detect(rt5665->codec, 0);
	}

	snd_soc_jack_report(rt5665->hs_jack, rt5665->jack_type,
			SND_JACK_HEADSET |
			    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			    SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (rt5665->jack_type & (SND_JACK_BTN_0 | SND_JACK_BTN_1 |
		SND_JACK_BTN_2 | SND_JACK_BTN_3))
		schedule_delayed_work(&rt5665->jd_check_work, 0);
	else
		cancel_delayed_work_sync(&rt5665->jd_check_work);

	mutex_unlock(&rt5665->calibrate_mutex);
}

static const char * const rt5665_clk_sync[] = {
	"I2S1_1", "I2S1_2", "I2S2", "I2S3", "IF2 Slave", "IF3 Slave"
};

static const struct soc_enum rt5665_enum[] = {
	SOC_ENUM_SINGLE(RT5665_I2S1_SDP, 11, 5, rt5665_clk_sync),
	SOC_ENUM_SINGLE(RT5665_I2S2_SDP, 11, 5, rt5665_clk_sync),
	SOC_ENUM_SINGLE(RT5665_I2S3_SDP, 11, 5, rt5665_clk_sync),
};

static const struct snd_kcontrol_new rt5665_snd_controls[] = {
	/* Headphone Output Volume */
	SOC_DOUBLE_R_EXT_TLV("Headphone Playback Volume", RT5665_HPL_GAIN,
		RT5665_HPR_GAIN, RT5665_G_HP_SFT, 15, 1, snd_soc_get_volsw,
		rt5665_hp_vol_put, hp_vol_tlv),

	/* Mono Output Volume */
	SOC_SINGLE_EXT_TLV("Mono Playback Volume", RT5665_MONO_GAIN,
		RT5665_L_VOL_SFT, 15, 1, snd_soc_get_volsw,
		rt5665_mono_vol_put, mono_vol_tlv),

	SOC_SINGLE_TLV("MONOVOL Playback Volume", RT5665_MONO_OUT,
		RT5665_L_VOL_SFT, 39, 1, out_vol_tlv),

	/* Output Volume */
	SOC_DOUBLE_TLV("OUT Playback Volume", RT5665_LOUT, RT5665_L_VOL_SFT,
		RT5665_R_VOL_SFT, 39, 1, out_vol_tlv),

	/* DAC Digital Volume */
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5665_DAC1_DIG_VOL,
		RT5665_L_VOL_SFT, RT5665_R_VOL_SFT, 175, 0, dac_vol_tlv),
	SOC_DOUBLE_TLV("DAC2 Playback Volume", RT5665_DAC2_DIG_VOL,
		RT5665_L_VOL_SFT, RT5665_R_VOL_SFT, 175, 0, dac_vol_tlv),
	SOC_DOUBLE("DAC2 Playback Switch", RT5665_DAC2_CTRL,
		RT5665_M_DAC2_L_VOL_SFT, RT5665_M_DAC2_R_VOL_SFT, 1, 1),

	/* IN1/IN2/IN3/IN4 Volume */
	SOC_SINGLE_TLV("IN1 Boost Volume", RT5665_IN1_IN2,
		RT5665_BST1_SFT, 69, 0, in_bst_tlv),
	SOC_SINGLE_TLV("IN2 Boost Volume", RT5665_IN1_IN2,
		RT5665_BST2_SFT, 69, 0, in_bst_tlv),
	SOC_SINGLE_TLV("IN3 Boost Volume", RT5665_IN3_IN4,
		RT5665_BST3_SFT, 69, 0, in_bst_tlv),
	SOC_SINGLE_TLV("IN4 Boost Volume", RT5665_IN3_IN4,
		RT5665_BST4_SFT, 69, 0, in_bst_tlv),
	SOC_SINGLE_TLV("CBJ Boost Volume", RT5665_CBJ_BST_CTRL,
		RT5665_BST_CBJ_SFT, 8, 0, bst_tlv),

	/* INL/INR Volume Control */
	SOC_DOUBLE_TLV("IN Capture Volume", RT5665_INL1_INR1_VOL,
		RT5665_INL_VOL_SFT, RT5665_INR_VOL_SFT, 31, 1, in_vol_tlv),

	/* ADC Digital Volume Control */
	SOC_DOUBLE("STO1 ADC Capture Switch", RT5665_STO1_ADC_DIG_VOL,
		RT5665_L_MUTE_SFT, RT5665_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("STO1 ADC Capture Volume", RT5665_STO1_ADC_DIG_VOL,
		RT5665_L_VOL_SFT, RT5665_R_VOL_SFT, 127, 0, adc_vol_tlv),
	SOC_DOUBLE("Mono ADC Capture Switch", RT5665_MONO_ADC_DIG_VOL,
		RT5665_L_MUTE_SFT, RT5665_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("Mono ADC Capture Volume", RT5665_MONO_ADC_DIG_VOL,
		RT5665_L_VOL_SFT, RT5665_R_VOL_SFT, 127, 0, adc_vol_tlv),
	SOC_DOUBLE("STO2 ADC Capture Switch", RT5665_STO2_ADC_DIG_VOL,
		RT5665_L_MUTE_SFT, RT5665_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("STO2 ADC Capture Volume", RT5665_STO2_ADC_DIG_VOL,
		RT5665_L_VOL_SFT, RT5665_R_VOL_SFT, 127, 0, adc_vol_tlv),

	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("STO1 ADC Boost Gain Volume", RT5665_STO1_ADC_BOOST,
		RT5665_STO1_ADC_L_BST_SFT, RT5665_STO1_ADC_R_BST_SFT,
		3, 0, adc_bst_tlv),

	SOC_DOUBLE_TLV("Mono ADC Boost Gain Volume", RT5665_MONO_ADC_BOOST,
		RT5665_MONO_ADC_L_BST_SFT, RT5665_MONO_ADC_R_BST_SFT,
		3, 0, adc_bst_tlv),

	SOC_DOUBLE_TLV("STO2 ADC Boost Gain Volume", RT5665_STO2_ADC_BOOST,
		RT5665_STO2_ADC_L_BST_SFT, RT5665_STO2_ADC_R_BST_SFT,
		3, 0, adc_bst_tlv),

	/* I2S3 CLK Source */
	SOC_ENUM("I2S1 Master Clk Sel", rt5665_enum[0]),
	SOC_ENUM("I2S2 Master Clk Sel", rt5665_enum[1]),
	SOC_ENUM("I2S3 Master Clk Sel", rt5665_enum[2]),
};

/**
 * set_dmic_clk - Set parameter of dmic.
 *
 * @w: DAPM widget.
 * @kcontrol: The kcontrol of this widget.
 * @event: Event id.
 *
 * Choose dmic clock between 1MHz and 3MHz.
 * It is better for clock to approximate 3MHz.
 */
static int set_dmic_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);
	int pd, idx = -EINVAL;

	pd = rl6231_get_pre_div(rt5665->regmap,
		RT5665_ADDA_CLK_1, RT5665_I2S_PD1_SFT);
	idx = rl6231_calc_dmic_clk(rt5665->sysclk / pd);

	if (idx < 0)
		dev_err(codec->dev, "Failed to set DMIC clock\n");
	else {
		snd_soc_update_bits(codec, RT5665_DMIC_CTRL_1,
			RT5665_DMIC_CLK_MASK, idx << RT5665_DMIC_CLK_SFT);
	}
	return idx;
}

static int rt5665_charge_pump_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(codec, RT5665_HP_CHARGE_PUMP_1,
			RT5665_PM_HP_MASK | RT5665_OSW_L_MASK,
			RT5665_PM_HP_HV | RT5665_OSW_L_EN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RT5665_HP_CHARGE_PUMP_1,
			RT5665_PM_HP_MASK | RT5665_OSW_L_MASK,
			RT5665_PM_HP_LV | RT5665_OSW_L_DIS);
		break;
	default:
		return 0;
	}

	return 0;
}

static int is_sys_clk_from_pll(struct snd_soc_dapm_widget *w,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int val;
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	val = snd_soc_read(codec, RT5665_GLB_CLK);
	val &= RT5665_SCLK_SRC_MASK;
	if (val == RT5665_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

static int is_using_asrc(struct snd_soc_dapm_widget *w,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg, shift, val;
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (w->shift) {
	case RT5665_ADC_MONO_R_ASRC_SFT:
		reg = RT5665_ASRC_3;
		shift = RT5665_AD_MONOR_CLK_SEL_SFT;
		break;
	case RT5665_ADC_MONO_L_ASRC_SFT:
		reg = RT5665_ASRC_3;
		shift = RT5665_AD_MONOL_CLK_SEL_SFT;
		break;
	case RT5665_ADC_STO1_ASRC_SFT:
		reg = RT5665_ASRC_3;
		shift = RT5665_AD_STO1_CLK_SEL_SFT;
		break;
	case RT5665_ADC_STO2_ASRC_SFT:
		reg = RT5665_ASRC_3;
		shift = RT5665_AD_STO2_CLK_SEL_SFT;
		break;
	case RT5665_DAC_MONO_R_ASRC_SFT:
		reg = RT5665_ASRC_2;
		shift = RT5665_DA_MONOR_CLK_SEL_SFT;
		break;
	case RT5665_DAC_MONO_L_ASRC_SFT:
		reg = RT5665_ASRC_2;
		shift = RT5665_DA_MONOL_CLK_SEL_SFT;
		break;
	case RT5665_DAC_STO1_ASRC_SFT:
		reg = RT5665_ASRC_2;
		shift = RT5665_DA_STO1_CLK_SEL_SFT;
		break;
	case RT5665_DAC_STO2_ASRC_SFT:
		reg = RT5665_ASRC_2;
		shift = RT5665_DA_STO2_CLK_SEL_SFT;
		break;
	default:
		return 0;
	}

	val = (snd_soc_read(codec, reg) >> shift) & 0xf;
	switch (val) {
	case RT5665_CLK_SEL_I2S1_ASRC:
	case RT5665_CLK_SEL_I2S2_ASRC:
	case RT5665_CLK_SEL_I2S3_ASRC:
		/* I2S_Pre_Div1 should be 1 in asrc mode */
		snd_soc_update_bits(codec, RT5665_ADDA_CLK_1,
			RT5665_I2S_PD1_MASK, RT5665_I2S_PD1_2);
		return 1;
	default:
		return 0;
	}

}

/* Digital Mixer */
static const struct snd_kcontrol_new rt5665_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5665_STO1_ADC_MIXER,
			RT5665_M_STO1_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5665_STO1_ADC_MIXER,
			RT5665_M_STO1_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5665_STO1_ADC_MIXER,
			RT5665_M_STO1_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5665_STO1_ADC_MIXER,
			RT5665_M_STO1_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_sto2_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5665_STO2_ADC_MIXER,
			RT5665_M_STO2_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5665_STO2_ADC_MIXER,
			RT5665_M_STO2_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_sto2_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5665_STO2_ADC_MIXER,
			RT5665_M_STO2_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5665_STO2_ADC_MIXER,
			RT5665_M_STO2_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_mono_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5665_MONO_ADC_MIXER,
			RT5665_M_MONO_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5665_MONO_ADC_MIXER,
			RT5665_M_MONO_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_mono_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5665_MONO_ADC_MIXER,
			RT5665_M_MONO_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5665_MONO_ADC_MIXER,
			RT5665_M_MONO_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5665_AD_DA_MIXER,
			RT5665_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5665_AD_DA_MIXER,
			RT5665_M_DAC1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5665_AD_DA_MIXER,
			RT5665_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5665_AD_DA_MIXER,
			RT5665_M_DAC1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_sto1_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5665_STO1_DAC_MIXER,
			RT5665_M_DAC_L1_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5665_STO1_DAC_MIXER,
			RT5665_M_DAC_R1_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5665_STO1_DAC_MIXER,
			RT5665_M_DAC_L2_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5665_STO1_DAC_MIXER,
			RT5665_M_DAC_R2_STO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_sto1_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5665_STO1_DAC_MIXER,
			RT5665_M_DAC_L1_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5665_STO1_DAC_MIXER,
			RT5665_M_DAC_R1_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5665_STO1_DAC_MIXER,
			RT5665_M_DAC_L2_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5665_STO1_DAC_MIXER,
			RT5665_M_DAC_R2_STO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_sto2_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5665_STO2_DAC_MIXER,
			RT5665_M_DAC_L1_STO2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5665_STO2_DAC_MIXER,
			RT5665_M_DAC_L2_STO2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L3 Switch", RT5665_STO2_DAC_MIXER,
			RT5665_M_DAC_L3_STO2_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_sto2_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5665_STO2_DAC_MIXER,
			RT5665_M_DAC_R1_STO2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5665_STO2_DAC_MIXER,
			RT5665_M_DAC_R2_STO2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R3 Switch", RT5665_STO2_DAC_MIXER,
			RT5665_M_DAC_R3_STO2_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_mono_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5665_MONO_DAC_MIXER,
			RT5665_M_DAC_L1_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5665_MONO_DAC_MIXER,
			RT5665_M_DAC_R1_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5665_MONO_DAC_MIXER,
			RT5665_M_DAC_L2_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5665_MONO_DAC_MIXER,
			RT5665_M_DAC_R2_MONO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_mono_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5665_MONO_DAC_MIXER,
			RT5665_M_DAC_L1_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5665_MONO_DAC_MIXER,
			RT5665_M_DAC_R1_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5665_MONO_DAC_MIXER,
			RT5665_M_DAC_L2_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5665_MONO_DAC_MIXER,
			RT5665_M_DAC_R2_MONO_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5665_rec1_l_mix[] = {
	SOC_DAPM_SINGLE("CBJ Switch", RT5665_REC1_L2_MIXER,
			RT5665_M_CBJ_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5665_REC1_L2_MIXER,
			RT5665_M_INL_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5665_REC1_L2_MIXER,
			RT5665_M_INR_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5665_REC1_L2_MIXER,
			RT5665_M_BST4_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5665_REC1_L2_MIXER,
			RT5665_M_BST3_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5665_REC1_L2_MIXER,
			RT5665_M_BST2_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5665_REC1_L2_MIXER,
			RT5665_M_BST1_RM1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_rec1_r_mix[] = {
	SOC_DAPM_SINGLE("MONOVOL Switch", RT5665_REC1_R2_MIXER,
			RT5665_M_AEC_REF_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5665_REC1_R2_MIXER,
			RT5665_M_INR_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5665_REC1_R2_MIXER,
			RT5665_M_BST4_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5665_REC1_R2_MIXER,
			RT5665_M_BST3_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5665_REC1_R2_MIXER,
			RT5665_M_BST2_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5665_REC1_R2_MIXER,
			RT5665_M_BST1_RM1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_rec2_l_mix[] = {
	SOC_DAPM_SINGLE("INL Switch", RT5665_REC2_L2_MIXER,
			RT5665_M_INL_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5665_REC2_L2_MIXER,
			RT5665_M_INR_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("CBJ Switch", RT5665_REC2_L2_MIXER,
			RT5665_M_CBJ_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5665_REC2_L2_MIXER,
			RT5665_M_BST4_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5665_REC2_L2_MIXER,
			RT5665_M_BST3_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5665_REC2_L2_MIXER,
			RT5665_M_BST2_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5665_REC2_L2_MIXER,
			RT5665_M_BST1_RM2_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_rec2_r_mix[] = {
	SOC_DAPM_SINGLE("MONOVOL Switch", RT5665_REC2_R2_MIXER,
			RT5665_M_MONOVOL_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5665_REC2_R2_MIXER,
			RT5665_M_INL_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5665_REC2_R2_MIXER,
			RT5665_M_INR_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5665_REC2_R2_MIXER,
			RT5665_M_BST4_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5665_REC2_R2_MIXER,
			RT5665_M_BST3_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5665_REC2_R2_MIXER,
			RT5665_M_BST2_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5665_REC2_R2_MIXER,
			RT5665_M_BST1_RM2_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_monovol_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5665_MONOMIX_IN_GAIN,
			RT5665_M_DAC_L2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("RECMIX2L Switch", RT5665_MONOMIX_IN_GAIN,
			RT5665_M_RECMIC2L_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5665_MONOMIX_IN_GAIN,
			RT5665_M_BST1_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5665_MONOMIX_IN_GAIN,
			RT5665_M_BST2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5665_MONOMIX_IN_GAIN,
			RT5665_M_BST3_MM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_out_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5665_OUT_L_MIXER,
			RT5665_M_DAC_L2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5665_OUT_L_MIXER,
			RT5665_M_IN_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5665_OUT_L_MIXER,
			RT5665_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5665_OUT_L_MIXER,
			RT5665_M_BST2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5665_OUT_L_MIXER,
			RT5665_M_BST3_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_out_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5665_OUT_R_MIXER,
			RT5665_M_DAC_R2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5665_OUT_R_MIXER,
			RT5665_M_IN_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5665_OUT_R_MIXER,
			RT5665_M_BST2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5665_OUT_R_MIXER,
			RT5665_M_BST3_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5665_OUT_R_MIXER,
			RT5665_M_BST4_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_mono_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5665_MONOMIX_IN_GAIN,
			RT5665_M_DAC_L2_MA_SFT, 1, 1),
	SOC_DAPM_SINGLE("MONOVOL Switch", RT5665_MONOMIX_IN_GAIN,
			RT5665_M_MONOVOL_MA_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_lout_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5665_LOUT_MIXER,
			RT5665_M_DAC_L2_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL L Switch", RT5665_LOUT_MIXER,
			RT5665_M_OV_L_LM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5665_lout_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5665_LOUT_MIXER,
			RT5665_M_DAC_R2_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL R Switch", RT5665_LOUT_MIXER,
			RT5665_M_OV_R_LM_SFT, 1, 1),
};

/*DAC L2, DAC R2*/
/*MX-17 [6:4], MX-17 [2:0]*/
static const char * const rt5665_dac2_src[] = {
	"IF1 DAC2", "IF2_1 DAC", "IF2_2 DAC", "IF3 DAC", "Mono ADC MIX"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_dac_l2_enum, RT5665_DAC2_CTRL,
	RT5665_DAC_L2_SEL_SFT, rt5665_dac2_src);

static const struct snd_kcontrol_new rt5665_dac_l2_mux =
	SOC_DAPM_ENUM("Digital DAC L2 Source", rt5665_dac_l2_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_dac_r2_enum, RT5665_DAC2_CTRL,
	RT5665_DAC_R2_SEL_SFT, rt5665_dac2_src);

static const struct snd_kcontrol_new rt5665_dac_r2_mux =
	SOC_DAPM_ENUM("Digital DAC R2 Source", rt5665_dac_r2_enum);

/*DAC L3, DAC R3*/
/*MX-1B [6:4], MX-1B [2:0]*/
static const char * const rt5665_dac3_src[] = {
	"IF1 DAC2", "IF2_1 DAC", "IF2_2 DAC", "IF3 DAC", "STO2 ADC MIX"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_dac_l3_enum, RT5665_DAC3_CTRL,
	RT5665_DAC_L3_SEL_SFT, rt5665_dac3_src);

static const struct snd_kcontrol_new rt5665_dac_l3_mux =
	SOC_DAPM_ENUM("Digital DAC L3 Source", rt5665_dac_l3_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_dac_r3_enum, RT5665_DAC3_CTRL,
	RT5665_DAC_R3_SEL_SFT, rt5665_dac3_src);

static const struct snd_kcontrol_new rt5665_dac_r3_mux =
	SOC_DAPM_ENUM("Digital DAC R3 Source", rt5665_dac_r3_enum);

/* STO1 ADC1 Source */
/* MX-26 [13] [5] */
static const char * const rt5665_sto1_adc1_src[] = {
	"DD Mux", "ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto1_adc1l_enum, RT5665_STO1_ADC_MIXER,
	RT5665_STO1_ADC1L_SRC_SFT, rt5665_sto1_adc1_src);

static const struct snd_kcontrol_new rt5665_sto1_adc1l_mux =
	SOC_DAPM_ENUM("Stereo1 ADC1L Source", rt5665_sto1_adc1l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto1_adc1r_enum, RT5665_STO1_ADC_MIXER,
	RT5665_STO1_ADC1R_SRC_SFT, rt5665_sto1_adc1_src);

static const struct snd_kcontrol_new rt5665_sto1_adc1r_mux =
	SOC_DAPM_ENUM("Stereo1 ADC1L Source", rt5665_sto1_adc1r_enum);

/* STO1 ADC Source */
/* MX-26 [11:10] [3:2] */
static const char * const rt5665_sto1_adc_src[] = {
	"ADC1 L", "ADC1 R", "ADC2 L", "ADC2 R"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto1_adcl_enum, RT5665_STO1_ADC_MIXER,
	RT5665_STO1_ADCL_SRC_SFT, rt5665_sto1_adc_src);

static const struct snd_kcontrol_new rt5665_sto1_adcl_mux =
	SOC_DAPM_ENUM("Stereo1 ADCL Source", rt5665_sto1_adcl_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto1_adcr_enum, RT5665_STO1_ADC_MIXER,
	RT5665_STO1_ADCR_SRC_SFT, rt5665_sto1_adc_src);

static const struct snd_kcontrol_new rt5665_sto1_adcr_mux =
	SOC_DAPM_ENUM("Stereo1 ADCR Source", rt5665_sto1_adcr_enum);

/* STO1 ADC2 Source */
/* MX-26 [12] [4] */
static const char * const rt5665_sto1_adc2_src[] = {
	"DAC MIX", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto1_adc2l_enum, RT5665_STO1_ADC_MIXER,
	RT5665_STO1_ADC2L_SRC_SFT, rt5665_sto1_adc2_src);

static const struct snd_kcontrol_new rt5665_sto1_adc2l_mux =
	SOC_DAPM_ENUM("Stereo1 ADC2L Source", rt5665_sto1_adc2l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto1_adc2r_enum, RT5665_STO1_ADC_MIXER,
	RT5665_STO1_ADC2R_SRC_SFT, rt5665_sto1_adc2_src);

static const struct snd_kcontrol_new rt5665_sto1_adc2r_mux =
	SOC_DAPM_ENUM("Stereo1 ADC2R Source", rt5665_sto1_adc2r_enum);

/* STO1 DMIC Source */
/* MX-26 [8] */
static const char * const rt5665_sto1_dmic_src[] = {
	"DMIC1", "DMIC2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto1_dmic_enum, RT5665_STO1_ADC_MIXER,
	RT5665_STO1_DMIC_SRC_SFT, rt5665_sto1_dmic_src);

static const struct snd_kcontrol_new rt5665_sto1_dmic_mux =
	SOC_DAPM_ENUM("Stereo1 DMIC Mux", rt5665_sto1_dmic_enum);

/* MX-26 [9] */
static const char * const rt5665_sto1_dd_l_src[] = {
	"STO2 DAC", "MONO DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto1_dd_l_enum, RT5665_STO1_ADC_MIXER,
	RT5665_STO1_DD_L_SRC_SFT, rt5665_sto1_dd_l_src);

static const struct snd_kcontrol_new rt5665_sto1_dd_l_mux =
	SOC_DAPM_ENUM("Stereo1 DD L Source", rt5665_sto1_dd_l_enum);

/* MX-26 [1:0] */
static const char * const rt5665_sto1_dd_r_src[] = {
	"STO2 DAC", "MONO DAC", "AEC REF"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto1_dd_r_enum, RT5665_STO1_ADC_MIXER,
	RT5665_STO1_DD_R_SRC_SFT, rt5665_sto1_dd_r_src);

static const struct snd_kcontrol_new rt5665_sto1_dd_r_mux =
	SOC_DAPM_ENUM("Stereo1 DD R Source", rt5665_sto1_dd_r_enum);

/* MONO ADC L2 Source */
/* MX-27 [12] */
static const char * const rt5665_mono_adc_l2_src[] = {
	"DAC MIXL", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_adc_l2_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_ADC_L2_SRC_SFT, rt5665_mono_adc_l2_src);

static const struct snd_kcontrol_new rt5665_mono_adc_l2_mux =
	SOC_DAPM_ENUM("Mono ADC L2 Source", rt5665_mono_adc_l2_enum);


/* MONO ADC L1 Source */
/* MX-27 [13] */
static const char * const rt5665_mono_adc_l1_src[] = {
	"DD Mux", "ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_adc_l1_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_ADC_L1_SRC_SFT, rt5665_mono_adc_l1_src);

static const struct snd_kcontrol_new rt5665_mono_adc_l1_mux =
	SOC_DAPM_ENUM("Mono ADC L1 Source", rt5665_mono_adc_l1_enum);

/* MX-27 [9][1]*/
static const char * const rt5665_mono_dd_src[] = {
	"STO2 DAC", "MONO DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_dd_l_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_DD_L_SRC_SFT, rt5665_mono_dd_src);

static const struct snd_kcontrol_new rt5665_mono_dd_l_mux =
	SOC_DAPM_ENUM("Mono DD L Source", rt5665_mono_dd_l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_dd_r_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_DD_R_SRC_SFT, rt5665_mono_dd_src);

static const struct snd_kcontrol_new rt5665_mono_dd_r_mux =
	SOC_DAPM_ENUM("Mono DD R Source", rt5665_mono_dd_r_enum);

/* MONO ADC L Source, MONO ADC R Source*/
/* MX-27 [11:10], MX-27 [3:2] */
static const char * const rt5665_mono_adc_src[] = {
	"ADC1 L", "ADC1 R", "ADC2 L", "ADC2 R"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_adc_l_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_ADC_L_SRC_SFT, rt5665_mono_adc_src);

static const struct snd_kcontrol_new rt5665_mono_adc_l_mux =
	SOC_DAPM_ENUM("Mono ADC L Source", rt5665_mono_adc_l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_adcr_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_ADC_R_SRC_SFT, rt5665_mono_adc_src);

static const struct snd_kcontrol_new rt5665_mono_adc_r_mux =
	SOC_DAPM_ENUM("Mono ADC R Source", rt5665_mono_adcr_enum);

/* MONO DMIC L Source */
/* MX-27 [8] */
static const char * const rt5665_mono_dmic_l_src[] = {
	"DMIC1 L", "DMIC2 L"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_dmic_l_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_DMIC_L_SRC_SFT, rt5665_mono_dmic_l_src);

static const struct snd_kcontrol_new rt5665_mono_dmic_l_mux =
	SOC_DAPM_ENUM("Mono DMIC L Source", rt5665_mono_dmic_l_enum);

/* MONO ADC R2 Source */
/* MX-27 [4] */
static const char * const rt5665_mono_adc_r2_src[] = {
	"DAC MIXR", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_adc_r2_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_ADC_R2_SRC_SFT, rt5665_mono_adc_r2_src);

static const struct snd_kcontrol_new rt5665_mono_adc_r2_mux =
	SOC_DAPM_ENUM("Mono ADC R2 Source", rt5665_mono_adc_r2_enum);

/* MONO ADC R1 Source */
/* MX-27 [5] */
static const char * const rt5665_mono_adc_r1_src[] = {
	"DD Mux", "ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_adc_r1_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_ADC_R1_SRC_SFT, rt5665_mono_adc_r1_src);

static const struct snd_kcontrol_new rt5665_mono_adc_r1_mux =
	SOC_DAPM_ENUM("Mono ADC R1 Source", rt5665_mono_adc_r1_enum);

/* MONO DMIC R Source */
/* MX-27 [0] */
static const char * const rt5665_mono_dmic_r_src[] = {
	"DMIC1 R", "DMIC2 R"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_mono_dmic_r_enum, RT5665_MONO_ADC_MIXER,
	RT5665_MONO_DMIC_R_SRC_SFT, rt5665_mono_dmic_r_src);

static const struct snd_kcontrol_new rt5665_mono_dmic_r_mux =
	SOC_DAPM_ENUM("Mono DMIC R Source", rt5665_mono_dmic_r_enum);


/* STO2 ADC1 Source */
/* MX-28 [13] [5] */
static const char * const rt5665_sto2_adc1_src[] = {
	"DD Mux", "ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto2_adc1l_enum, RT5665_STO2_ADC_MIXER,
	RT5665_STO2_ADC1L_SRC_SFT, rt5665_sto2_adc1_src);

static const struct snd_kcontrol_new rt5665_sto2_adc1l_mux =
	SOC_DAPM_ENUM("Stereo2 ADC1L Source", rt5665_sto2_adc1l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto2_adc1r_enum, RT5665_STO2_ADC_MIXER,
	RT5665_STO2_ADC1R_SRC_SFT, rt5665_sto2_adc1_src);

static const struct snd_kcontrol_new rt5665_sto2_adc1r_mux =
	SOC_DAPM_ENUM("Stereo2 ADC1L Source", rt5665_sto2_adc1r_enum);

/* STO2 ADC Source */
/* MX-28 [11:10] [3:2] */
static const char * const rt5665_sto2_adc_src[] = {
	"ADC1 L", "ADC1 R", "ADC2 L"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto2_adcl_enum, RT5665_STO2_ADC_MIXER,
	RT5665_STO2_ADCL_SRC_SFT, rt5665_sto2_adc_src);

static const struct snd_kcontrol_new rt5665_sto2_adcl_mux =
	SOC_DAPM_ENUM("Stereo2 ADCL Source", rt5665_sto2_adcl_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto2_adcr_enum, RT5665_STO2_ADC_MIXER,
	RT5665_STO2_ADCR_SRC_SFT, rt5665_sto2_adc_src);

static const struct snd_kcontrol_new rt5665_sto2_adcr_mux =
	SOC_DAPM_ENUM("Stereo2 ADCR Source", rt5665_sto2_adcr_enum);

/* STO2 ADC2 Source */
/* MX-28 [12] [4] */
static const char * const rt5665_sto2_adc2_src[] = {
	"DAC MIX", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto2_adc2l_enum, RT5665_STO2_ADC_MIXER,
	RT5665_STO2_ADC2L_SRC_SFT, rt5665_sto2_adc2_src);

static const struct snd_kcontrol_new rt5665_sto2_adc2l_mux =
	SOC_DAPM_ENUM("Stereo2 ADC2L Source", rt5665_sto2_adc2l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto2_adc2r_enum, RT5665_STO2_ADC_MIXER,
	RT5665_STO2_ADC2R_SRC_SFT, rt5665_sto2_adc2_src);

static const struct snd_kcontrol_new rt5665_sto2_adc2r_mux =
	SOC_DAPM_ENUM("Stereo2 ADC2R Source", rt5665_sto2_adc2r_enum);

/* STO2 DMIC Source */
/* MX-28 [8] */
static const char * const rt5665_sto2_dmic_src[] = {
	"DMIC1", "DMIC2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto2_dmic_enum, RT5665_STO2_ADC_MIXER,
	RT5665_STO2_DMIC_SRC_SFT, rt5665_sto2_dmic_src);

static const struct snd_kcontrol_new rt5665_sto2_dmic_mux =
	SOC_DAPM_ENUM("Stereo2 DMIC Source", rt5665_sto2_dmic_enum);

/* MX-28 [9] */
static const char * const rt5665_sto2_dd_l_src[] = {
	"STO2 DAC", "MONO DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto2_dd_l_enum, RT5665_STO2_ADC_MIXER,
	RT5665_STO2_DD_L_SRC_SFT, rt5665_sto2_dd_l_src);

static const struct snd_kcontrol_new rt5665_sto2_dd_l_mux =
	SOC_DAPM_ENUM("Stereo2 DD L Source", rt5665_sto2_dd_l_enum);

/* MX-28 [1] */
static const char * const rt5665_sto2_dd_r_src[] = {
	"STO2 DAC", "MONO DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_sto2_dd_r_enum, RT5665_STO2_ADC_MIXER,
	RT5665_STO2_DD_R_SRC_SFT, rt5665_sto2_dd_r_src);

static const struct snd_kcontrol_new rt5665_sto2_dd_r_mux =
	SOC_DAPM_ENUM("Stereo2 DD R Source", rt5665_sto2_dd_r_enum);

/* DAC R1 Source, DAC L1 Source*/
/* MX-29 [11:10], MX-29 [9:8]*/
static const char * const rt5665_dac1_src[] = {
	"IF1 DAC1", "IF2_1 DAC", "IF2_2 DAC", "IF3 DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_dac_r1_enum, RT5665_AD_DA_MIXER,
	RT5665_DAC1_R_SEL_SFT, rt5665_dac1_src);

static const struct snd_kcontrol_new rt5665_dac_r1_mux =
	SOC_DAPM_ENUM("DAC R1 Source", rt5665_dac_r1_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_dac_l1_enum, RT5665_AD_DA_MIXER,
	RT5665_DAC1_L_SEL_SFT, rt5665_dac1_src);

static const struct snd_kcontrol_new rt5665_dac_l1_mux =
	SOC_DAPM_ENUM("DAC L1 Source", rt5665_dac_l1_enum);

/* DAC Digital Mixer L Source, DAC Digital Mixer R Source*/
/* MX-2D [13:12], MX-2D [9:8]*/
static const char * const rt5665_dig_dac_mix_src[] = {
	"Stereo1 DAC Mixer", "Stereo2 DAC Mixer", "Mono DAC Mixer"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_dig_dac_mixl_enum, RT5665_A_DAC1_MUX,
	RT5665_DAC_MIX_L_SFT, rt5665_dig_dac_mix_src);

static const struct snd_kcontrol_new rt5665_dig_dac_mixl_mux =
	SOC_DAPM_ENUM("DAC Digital Mixer L Source", rt5665_dig_dac_mixl_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_dig_dac_mixr_enum, RT5665_A_DAC1_MUX,
	RT5665_DAC_MIX_R_SFT, rt5665_dig_dac_mix_src);

static const struct snd_kcontrol_new rt5665_dig_dac_mixr_mux =
	SOC_DAPM_ENUM("DAC Digital Mixer R Source", rt5665_dig_dac_mixr_enum);

/* Analog DAC L1 Source, Analog DAC R1 Source*/
/* MX-2D [5:4], MX-2D [1:0]*/
static const char * const rt5665_alg_dac1_src[] = {
	"Stereo1 DAC Mixer", "DAC1", "DMIC1"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_alg_dac_l1_enum, RT5665_A_DAC1_MUX,
	RT5665_A_DACL1_SFT, rt5665_alg_dac1_src);

static const struct snd_kcontrol_new rt5665_alg_dac_l1_mux =
	SOC_DAPM_ENUM("Analog DAC L1 Source", rt5665_alg_dac_l1_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_alg_dac_r1_enum, RT5665_A_DAC1_MUX,
	RT5665_A_DACR1_SFT, rt5665_alg_dac1_src);

static const struct snd_kcontrol_new rt5665_alg_dac_r1_mux =
	SOC_DAPM_ENUM("Analog DAC R1 Source", rt5665_alg_dac_r1_enum);

/* Analog DAC LR Source, Analog DAC R2 Source*/
/* MX-2E [5:4], MX-2E [0]*/
static const char * const rt5665_alg_dac2_src[] = {
	"Mono DAC Mixer", "DAC2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_alg_dac_l2_enum, RT5665_A_DAC2_MUX,
	RT5665_A_DACL2_SFT, rt5665_alg_dac2_src);

static const struct snd_kcontrol_new rt5665_alg_dac_l2_mux =
	SOC_DAPM_ENUM("Analog DAC L2 Source", rt5665_alg_dac_l2_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_alg_dac_r2_enum, RT5665_A_DAC2_MUX,
	RT5665_A_DACR2_SFT, rt5665_alg_dac2_src);

static const struct snd_kcontrol_new rt5665_alg_dac_r2_mux =
	SOC_DAPM_ENUM("Analog DAC R2 Source", rt5665_alg_dac_r2_enum);

/* Interface2 ADC Data Input*/
/* MX-2F [14:12] */
static const char * const rt5665_if2_1_adc_in_src[] = {
	"STO1 ADC", "STO2 ADC", "MONO ADC", "IF1 DAC1",
	"IF1 DAC2", "IF2_2 DAC", "IF3 DAC", "DAC1 MIX"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if2_1_adc_in_enum, RT5665_DIG_INF2_DATA,
	RT5665_IF2_1_ADC_IN_SFT, rt5665_if2_1_adc_in_src);

static const struct snd_kcontrol_new rt5665_if2_1_adc_in_mux =
	SOC_DAPM_ENUM("IF2_1 ADC IN Source", rt5665_if2_1_adc_in_enum);

/* MX-2F [6:4] */
static const char * const rt5665_if2_2_adc_in_src[] = {
	"STO1 ADC", "STO2 ADC", "MONO ADC", "IF1 DAC1",
	"IF1 DAC2", "IF2_1 DAC", "IF3 DAC", "DAC1 MIX"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if2_2_adc_in_enum, RT5665_DIG_INF2_DATA,
	RT5665_IF2_2_ADC_IN_SFT, rt5665_if2_2_adc_in_src);

static const struct snd_kcontrol_new rt5665_if2_2_adc_in_mux =
	SOC_DAPM_ENUM("IF2_1 ADC IN Source", rt5665_if2_2_adc_in_enum);

/* Interface3 ADC Data Input*/
/* MX-30 [6:4] */
static const char * const rt5665_if3_adc_in_src[] = {
	"STO1 ADC", "STO2 ADC", "MONO ADC", "IF1 DAC1",
	"IF1 DAC2", "IF2_1 DAC", "IF2_2 DAC", "DAC1 MIX"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if3_adc_in_enum, RT5665_DIG_INF3_DATA,
	RT5665_IF3_ADC_IN_SFT, rt5665_if3_adc_in_src);

static const struct snd_kcontrol_new rt5665_if3_adc_in_mux =
	SOC_DAPM_ENUM("IF3 ADC IN Source", rt5665_if3_adc_in_enum);

/* PDM 1 L/R*/
/* MX-31 [11:10] [9:8] */
static const char * const rt5665_pdm_src[] = {
	"Stereo1 DAC", "Stereo2 DAC", "Mono DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_pdm_l_enum, RT5665_PDM_OUT_CTRL,
	RT5665_PDM1_L_SFT, rt5665_pdm_src);

static const struct snd_kcontrol_new rt5665_pdm_l_mux =
	SOC_DAPM_ENUM("PDM L Source", rt5665_pdm_l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_pdm_r_enum, RT5665_PDM_OUT_CTRL,
	RT5665_PDM1_R_SFT, rt5665_pdm_src);

static const struct snd_kcontrol_new rt5665_pdm_r_mux =
	SOC_DAPM_ENUM("PDM R Source", rt5665_pdm_r_enum);


/* I2S1 TDM ADCDAT Source */
/* MX-7a[10] */
static const char * const rt5665_if1_1_adc1_data_src[] = {
	"STO1 ADC", "IF2_1 DAC",
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if1_1_adc1_data_enum, RT5665_TDM_CTRL_3,
	RT5665_IF1_ADC1_SEL_SFT, rt5665_if1_1_adc1_data_src);

static const struct snd_kcontrol_new rt5665_if1_1_adc1_mux =
	SOC_DAPM_ENUM("IF1_1 ADC1 Source", rt5665_if1_1_adc1_data_enum);

/* MX-7a[9] */
static const char * const rt5665_if1_1_adc2_data_src[] = {
	"STO2 ADC", "IF2_2 DAC",
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if1_1_adc2_data_enum, RT5665_TDM_CTRL_3,
	RT5665_IF1_ADC2_SEL_SFT, rt5665_if1_1_adc2_data_src);

static const struct snd_kcontrol_new rt5665_if1_1_adc2_mux =
	SOC_DAPM_ENUM("IF1_1 ADC2 Source", rt5665_if1_1_adc2_data_enum);

/* MX-7a[8] */
static const char * const rt5665_if1_1_adc3_data_src[] = {
	"MONO ADC", "IF3 DAC",
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if1_1_adc3_data_enum, RT5665_TDM_CTRL_3,
	RT5665_IF1_ADC3_SEL_SFT, rt5665_if1_1_adc3_data_src);

static const struct snd_kcontrol_new rt5665_if1_1_adc3_mux =
	SOC_DAPM_ENUM("IF1_1 ADC3 Source", rt5665_if1_1_adc3_data_enum);

/* MX-7b[10] */
static const char * const rt5665_if1_2_adc1_data_src[] = {
	"STO1 ADC", "IF1 DAC",
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if1_2_adc1_data_enum, RT5665_TDM_CTRL_4,
	RT5665_IF1_ADC1_SEL_SFT, rt5665_if1_2_adc1_data_src);

static const struct snd_kcontrol_new rt5665_if1_2_adc1_mux =
	SOC_DAPM_ENUM("IF1_2 ADC1 Source", rt5665_if1_2_adc1_data_enum);

/* MX-7b[9] */
static const char * const rt5665_if1_2_adc2_data_src[] = {
	"STO2 ADC", "IF2_1 DAC",
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if1_2_adc2_data_enum, RT5665_TDM_CTRL_4,
	RT5665_IF1_ADC2_SEL_SFT, rt5665_if1_2_adc2_data_src);

static const struct snd_kcontrol_new rt5665_if1_2_adc2_mux =
	SOC_DAPM_ENUM("IF1_2 ADC2 Source", rt5665_if1_2_adc2_data_enum);

/* MX-7b[8] */
static const char * const rt5665_if1_2_adc3_data_src[] = {
	"MONO ADC", "IF2_2 DAC",
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if1_2_adc3_data_enum, RT5665_TDM_CTRL_4,
	RT5665_IF1_ADC3_SEL_SFT, rt5665_if1_2_adc3_data_src);

static const struct snd_kcontrol_new rt5665_if1_2_adc3_mux =
	SOC_DAPM_ENUM("IF1_2 ADC3 Source", rt5665_if1_2_adc3_data_enum);

/* MX-7b[7] */
static const char * const rt5665_if1_2_adc4_data_src[] = {
	"DAC1", "IF3 DAC",
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_if1_2_adc4_data_enum, RT5665_TDM_CTRL_4,
	RT5665_IF1_ADC4_SEL_SFT, rt5665_if1_2_adc4_data_src);

static const struct snd_kcontrol_new rt5665_if1_2_adc4_mux =
	SOC_DAPM_ENUM("IF1_2 ADC4 Source", rt5665_if1_2_adc4_data_enum);

/* MX-7a[4:0] MX-7b[4:0] */
static const char * const rt5665_tdm_adc_data_src[] = {
	"1234", "1243", "1324",	"1342", "1432", "1423",
	"2134", "2143", "2314",	"2341", "2431", "2413",
	"3124", "3142", "3214", "3241", "3412", "3421",
	"4123", "4132", "4213", "4231", "4312", "4321"
};

static SOC_ENUM_SINGLE_DECL(
	rt5665_tdm1_adc_data_enum, RT5665_TDM_CTRL_3,
	RT5665_TDM_ADC_SEL_SFT, rt5665_tdm_adc_data_src);

static const struct snd_kcontrol_new rt5665_tdm1_adc_mux =
	SOC_DAPM_ENUM("TDM1 ADC Mux", rt5665_tdm1_adc_data_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5665_tdm2_adc_data_enum, RT5665_TDM_CTRL_4,
	RT5665_TDM_ADC_SEL_SFT, rt5665_tdm_adc_data_src);

static const struct snd_kcontrol_new rt5665_tdm2_adc_mux =
	SOC_DAPM_ENUM("TDM2 ADCDAT Source", rt5665_tdm2_adc_data_enum);

/* Out Volume Switch */
static const struct snd_kcontrol_new monovol_switch =
	SOC_DAPM_SINGLE("Switch", RT5665_MONO_OUT, RT5665_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new outvol_l_switch =
	SOC_DAPM_SINGLE("Switch", RT5665_LOUT, RT5665_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new outvol_r_switch =
	SOC_DAPM_SINGLE("Switch", RT5665_LOUT, RT5665_VOL_R_SFT, 1, 1);

/* Out Switch */
static const struct snd_kcontrol_new mono_switch =
	SOC_DAPM_SINGLE("Switch", RT5665_MONO_OUT, RT5665_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new hpo_switch =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5665_HP_CTRL_2,
					RT5665_VOL_L_SFT, 1, 0);

static const struct snd_kcontrol_new lout_l_switch =
	SOC_DAPM_SINGLE("Switch", RT5665_LOUT, RT5665_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new lout_r_switch =
	SOC_DAPM_SINGLE("Switch", RT5665_LOUT, RT5665_R_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new pdm_l_switch =
	SOC_DAPM_SINGLE("Switch", RT5665_PDM_OUT_CTRL,
			RT5665_M_PDM1_L_SFT, 1,	1);

static const struct snd_kcontrol_new pdm_r_switch =
	SOC_DAPM_SINGLE("Switch", RT5665_PDM_OUT_CTRL,
			RT5665_M_PDM1_R_SFT, 1,	1);

static int rt5665_mono_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(codec, RT5665_MONO_NG2_CTRL_1,
			RT5665_NG2_EN_MASK, RT5665_NG2_EN);
		snd_soc_update_bits(codec, RT5665_MONO_AMP_CALIB_CTRL_1, 0x40,
			0x0);
		snd_soc_update_bits(codec, RT5665_MONO_OUT, 0x10, 0x10);
		snd_soc_update_bits(codec, RT5665_MONO_OUT, 0x20, 0x20);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RT5665_MONO_OUT, 0x20, 0);
		snd_soc_update_bits(codec, RT5665_MONO_OUT, 0x10, 0);
		snd_soc_update_bits(codec, RT5665_MONO_AMP_CALIB_CTRL_1, 0x40,
			0x40);
		snd_soc_update_bits(codec, RT5665_MONO_NG2_CTRL_1,
			RT5665_NG2_EN_MASK, RT5665_NG2_DIS);
		break;

	default:
		return 0;
	}

	return 0;

}

static int rt5665_hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(codec, RT5665_STO_NG2_CTRL_1,
			RT5665_NG2_EN_MASK, RT5665_NG2_EN);
		snd_soc_write(codec, RT5665_HP_LOGIC_CTRL_2, 0x0003);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_write(codec, RT5665_HP_LOGIC_CTRL_2, 0x0002);
		snd_soc_update_bits(codec, RT5665_STO_NG2_CTRL_1,
			RT5665_NG2_EN_MASK, RT5665_NG2_DIS);
		break;

	default:
		return 0;
	}

	return 0;

}

static int rt5665_lout_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5665_DEPOP_1,
			RT5665_PUMP_EN, RT5665_PUMP_EN);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5665_DEPOP_1,
			RT5665_PUMP_EN, 0);
		break;

	default:
		return 0;
	}

	return 0;

}

static int set_dmic_power(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/*Add delay to avoid pop noise*/
		msleep(150);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5655_set_verf(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (w->shift) {
		case RT5665_PWR_VREF1_BIT:
			snd_soc_update_bits(codec, RT5665_PWR_ANLG_1,
				RT5665_PWR_FV1, 0);
			break;

		case RT5665_PWR_VREF2_BIT:
			snd_soc_update_bits(codec, RT5665_PWR_ANLG_1,
				RT5665_PWR_FV2, 0);
			break;

		case RT5665_PWR_VREF3_BIT:
			snd_soc_update_bits(codec, RT5665_PWR_ANLG_1,
				RT5665_PWR_FV3, 0);
			break;

		default:
			break;
		}
		break;

	case SND_SOC_DAPM_POST_PMU:
		usleep_range(15000, 20000);
		switch (w->shift) {
		case RT5665_PWR_VREF1_BIT:
			snd_soc_update_bits(codec, RT5665_PWR_ANLG_1,
				RT5665_PWR_FV1, RT5665_PWR_FV1);
			break;

		case RT5665_PWR_VREF2_BIT:
			snd_soc_update_bits(codec, RT5665_PWR_ANLG_1,
				RT5665_PWR_FV2, RT5665_PWR_FV2);
			break;

		case RT5665_PWR_VREF3_BIT:
			snd_soc_update_bits(codec, RT5665_PWR_ANLG_1,
				RT5665_PWR_FV3, RT5665_PWR_FV3);
			break;

		default:
			break;
		}
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5665_i2s_pin_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	unsigned int val1, val2, mask1 = 0, mask2 = 0;

	switch (w->shift) {
	case RT5665_PWR_I2S2_1_BIT:
		mask1 = RT5665_GP2_PIN_MASK | RT5665_GP3_PIN_MASK |
			RT5665_GP4_PIN_MASK | RT5665_GP5_PIN_MASK;
		val1 = RT5665_GP2_PIN_BCLK2 | RT5665_GP3_PIN_LRCK2 |
			RT5665_GP4_PIN_DACDAT2_1 | RT5665_GP5_PIN_ADCDAT2_1;
		break;
	case RT5665_PWR_I2S2_2_BIT:
		mask1 = RT5665_GP2_PIN_MASK | RT5665_GP3_PIN_MASK |
			RT5665_GP8_PIN_MASK;
		val1 = RT5665_GP2_PIN_BCLK2 | RT5665_GP3_PIN_LRCK2 |
			RT5665_GP8_PIN_DACDAT2_2;
		mask2 = RT5665_GP9_PIN_MASK;
		val2 = RT5665_GP9_PIN_ADCDAT2_2;
		break;
	case RT5665_PWR_I2S3_BIT:
		mask1 = RT5665_GP6_PIN_MASK | RT5665_GP7_PIN_MASK |
			RT5665_GP8_PIN_MASK;
		val1 = RT5665_GP6_PIN_BCLK3 | RT5665_GP7_PIN_LRCK3 |
			RT5665_GP8_PIN_DACDAT3;
		mask2 = RT5665_GP9_PIN_MASK;
		val2 = RT5665_GP9_PIN_ADCDAT3;
		break;
	}
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (mask1)
			snd_soc_update_bits(codec, RT5665_GPIO_CTRL_1,
					    mask1, val1);
		if (mask2)
			snd_soc_update_bits(codec, RT5665_GPIO_CTRL_2,
					    mask2, val2);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (mask1)
			snd_soc_update_bits(codec, RT5665_GPIO_CTRL_1,
					    mask1, 0);
		if (mask2)
			snd_soc_update_bits(codec, RT5665_GPIO_CTRL_2,
					    mask2, 0);
		break;
	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt5665_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("LDO2", RT5665_PWR_ANLG_3, RT5665_PWR_LDO2_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL", RT5665_PWR_ANLG_3, RT5665_PWR_PLL_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Det Power", RT5665_PWR_VOL,
		RT5665_PWR_MIC_DET_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Vref1", RT5665_PWR_ANLG_1, RT5665_PWR_VREF1_BIT, 0,
		rt5655_set_verf, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("Vref2", RT5665_PWR_ANLG_1, RT5665_PWR_VREF2_BIT, 0,
		rt5655_set_verf, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("Vref3", RT5665_PWR_ANLG_1, RT5665_PWR_VREF3_BIT, 0,
		rt5655_set_verf, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),

	/* ASRC */
	SND_SOC_DAPM_SUPPLY_S("I2S1 ASRC", 1, RT5665_ASRC_1,
		RT5665_I2S1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S2 ASRC", 1, RT5665_ASRC_1,
		RT5665_I2S2_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S3 ASRC", 1, RT5665_ASRC_1,
		RT5665_I2S3_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC STO1 ASRC", 1, RT5665_ASRC_1,
		RT5665_DAC_STO1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC STO2 ASRC", 1, RT5665_ASRC_1,
		RT5665_DAC_STO2_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC Mono L ASRC", 1, RT5665_ASRC_1,
		RT5665_DAC_MONO_L_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC Mono R ASRC", 1, RT5665_ASRC_1,
		RT5665_DAC_MONO_R_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO1 ASRC", 1, RT5665_ASRC_1,
		RT5665_ADC_STO1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO2 ASRC", 1, RT5665_ASRC_1,
		RT5665_ADC_STO2_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC Mono L ASRC", 1, RT5665_ASRC_1,
		RT5665_ADC_MONO_L_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC Mono R ASRC", 1, RT5665_ASRC_1,
		RT5665_ADC_MONO_R_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC STO1 ASRC", 1, RT5665_ASRC_1,
		RT5665_DMIC_STO1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC STO2 ASRC", 1, RT5665_ASRC_1,
		RT5665_DMIC_STO2_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC MONO L ASRC", 1, RT5665_ASRC_1,
		RT5665_DMIC_MONO_L_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC MONO R ASRC", 1, RT5665_ASRC_1,
		RT5665_DMIC_MONO_R_ASRC_SFT, 0, NULL, 0),

	/* Input Side */
	SND_SOC_DAPM_SUPPLY("MICBIAS1", RT5665_PWR_ANLG_2, RT5665_PWR_MB1_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS2", RT5665_PWR_ANLG_2, RT5665_PWR_MB2_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS3", RT5665_PWR_ANLG_2, RT5665_PWR_MB3_BIT,
		0, NULL, 0),

	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),
	SND_SOC_DAPM_INPUT("DMIC L2"),
	SND_SOC_DAPM_INPUT("DMIC R2"),

	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),
	SND_SOC_DAPM_INPUT("IN3P"),
	SND_SOC_DAPM_INPUT("IN3N"),
	SND_SOC_DAPM_INPUT("IN4P"),
	SND_SOC_DAPM_INPUT("IN4N"),

	SND_SOC_DAPM_PGA("DMIC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC2", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
		set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC1 Power", RT5665_DMIC_CTRL_1,
		RT5665_DMIC_1_EN_SFT, 0, set_dmic_power, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC2 Power", RT5665_DMIC_CTRL_1,
		RT5665_DMIC_2_EN_SFT, 0, set_dmic_power, SND_SOC_DAPM_POST_PMU),

	/* Boost */
	SND_SOC_DAPM_PGA("BST1", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST2", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST3", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST4", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST1 CBJ", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST1 Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_BST1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST2 Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_BST2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST3 Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_BST3_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST4 Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_BST4_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST1P Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_BST1_P_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST2P Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_BST2_P_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST3P Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_BST3_P_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST4P Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_BST4_P_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CBJ Power", RT5665_PWR_ANLG_3,
		RT5665_PWR_CBJ_BIT, 0, NULL, 0),


	/* Input Volume */
	SND_SOC_DAPM_PGA("INL VOL", RT5665_PWR_VOL, RT5665_PWR_IN_L_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_PGA("INR VOL", RT5665_PWR_VOL, RT5665_PWR_IN_R_BIT,
		0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIX1L", SND_SOC_NOPM, 0, 0, rt5665_rec1_l_mix,
		ARRAY_SIZE(rt5665_rec1_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIX1R", SND_SOC_NOPM, 0, 0, rt5665_rec1_r_mix,
		ARRAY_SIZE(rt5665_rec1_r_mix)),
	SND_SOC_DAPM_MIXER("RECMIX2L", SND_SOC_NOPM, 0, 0, rt5665_rec2_l_mix,
		ARRAY_SIZE(rt5665_rec2_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIX2R", SND_SOC_NOPM, 0, 0, rt5665_rec2_r_mix,
		ARRAY_SIZE(rt5665_rec2_r_mix)),
	SND_SOC_DAPM_SUPPLY("RECMIX1L Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_RM1_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RECMIX1R Power", RT5665_PWR_ANLG_2,
		RT5665_PWR_RM1_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RECMIX2L Power", RT5665_PWR_MIXER,
		RT5665_PWR_RM2_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RECMIX2R Power", RT5665_PWR_MIXER,
		RT5665_PWR_RM2_R_BIT, 0, NULL, 0),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC1 L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC1 R", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC2 L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC2 R", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY("ADC1 L Power", RT5665_PWR_DIG_1,
		RT5665_PWR_ADC_L1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1 R Power", RT5665_PWR_DIG_1,
		RT5665_PWR_ADC_R1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC2 L Power", RT5665_PWR_DIG_1,
		RT5665_PWR_ADC_L2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC2 R Power", RT5665_PWR_DIG_1,
		RT5665_PWR_ADC_R2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1 clock", RT5665_CHOP_ADC,
		RT5665_CKGEN_ADC1_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC2 clock", RT5665_CHOP_ADC,
		RT5665_CKGEN_ADC2_SFT, 0, NULL, 0),

	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo1 DMIC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo1 DMIC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_adc1l_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_adc1r_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_adc2l_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_adc2r_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_adcl_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_adcr_mux),
	SND_SOC_DAPM_MUX("Stereo1 DD L Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_dd_l_mux),
	SND_SOC_DAPM_MUX("Stereo1 DD R Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto1_dd_r_mux),
	SND_SOC_DAPM_MUX("Mono ADC L2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_adc_l2_mux),
	SND_SOC_DAPM_MUX("Mono ADC R2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_adc_r2_mux),
	SND_SOC_DAPM_MUX("Mono ADC L1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_adc_l1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_adc_r1_mux),
	SND_SOC_DAPM_MUX("Mono DMIC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_dmic_l_mux),
	SND_SOC_DAPM_MUX("Mono DMIC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_dmic_r_mux),
	SND_SOC_DAPM_MUX("Mono ADC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_adc_l_mux),
	SND_SOC_DAPM_MUX("Mono ADC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_adc_r_mux),
	SND_SOC_DAPM_MUX("Mono DD L Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_dd_l_mux),
	SND_SOC_DAPM_MUX("Mono DD R Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_mono_dd_r_mux),
	SND_SOC_DAPM_MUX("Stereo2 DMIC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo2 DMIC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_adc1l_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_adc1r_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_adc2l_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_adc2r_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_adcl_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_adcr_mux),
	SND_SOC_DAPM_MUX("Stereo2 DD L Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_dd_l_mux),
	SND_SOC_DAPM_MUX("Stereo2 DD R Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_sto2_dd_r_mux),
	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("ADC Stereo1 Filter", RT5665_PWR_DIG_2,
		RT5665_PWR_ADC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Stereo2 Filter", RT5665_PWR_DIG_2,
		RT5665_PWR_ADC_S2F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXL", RT5665_STO1_ADC_DIG_VOL,
		RT5665_L_MUTE_SFT, 1, rt5665_sto1_adc_l_mix,
		ARRAY_SIZE(rt5665_sto1_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXR", RT5665_STO1_ADC_DIG_VOL,
		RT5665_R_MUTE_SFT, 1, rt5665_sto1_adc_r_mix,
		ARRAY_SIZE(rt5665_sto1_adc_r_mix)),
	SND_SOC_DAPM_MIXER("Stereo2 ADC MIXL", RT5665_STO2_ADC_DIG_VOL,
		RT5665_L_MUTE_SFT, 1, rt5665_sto2_adc_l_mix,
		ARRAY_SIZE(rt5665_sto2_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo2 ADC MIXR", RT5665_STO2_ADC_DIG_VOL,
		RT5665_R_MUTE_SFT, 1, rt5665_sto2_adc_r_mix,
		ARRAY_SIZE(rt5665_sto2_adc_r_mix)),
	SND_SOC_DAPM_SUPPLY("ADC Mono Left Filter", RT5665_PWR_DIG_2,
		RT5665_PWR_ADC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXL", RT5665_MONO_ADC_DIG_VOL,
		RT5665_L_MUTE_SFT, 1, rt5665_mono_adc_l_mix,
		ARRAY_SIZE(rt5665_mono_adc_l_mix)),
	SND_SOC_DAPM_SUPPLY("ADC Mono Right Filter", RT5665_PWR_DIG_2,
		RT5665_PWR_ADC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXR", RT5665_MONO_ADC_DIG_VOL,
		RT5665_R_MUTE_SFT, 1, rt5665_mono_adc_r_mix,
		ARRAY_SIZE(rt5665_mono_adc_r_mix)),

	/* ADC PGA */
	SND_SOC_DAPM_PGA("Stereo1 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo2 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mono ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1_1", RT5665_PWR_DIG_1, RT5665_PWR_I2S1_1_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S1_2", RT5665_PWR_DIG_1, RT5665_PWR_I2S1_2_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S2_1", RT5665_PWR_DIG_1, RT5665_PWR_I2S2_1_BIT,
		0, rt5665_i2s_pin_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("I2S2_2", RT5665_PWR_DIG_1, RT5665_PWR_I2S2_2_BIT,
		0, rt5665_i2s_pin_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("I2S3", RT5665_PWR_DIG_1, RT5665_PWR_I2S3_BIT,
		0, rt5665_i2s_pin_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA("IF1 DAC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC3 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC3 R", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA("IF2_1 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2_2 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2_1 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2_1 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2_2 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2_2 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2_1 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2_2 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA("IF3 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface Select */
	SND_SOC_DAPM_MUX("IF1_1_ADC1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if1_1_adc1_mux),
	SND_SOC_DAPM_MUX("IF1_1_ADC2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if1_1_adc2_mux),
	SND_SOC_DAPM_MUX("IF1_1_ADC3 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if1_1_adc3_mux),
	SND_SOC_DAPM_PGA("IF1_1_ADC4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MUX("IF1_2_ADC1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if1_2_adc1_mux),
	SND_SOC_DAPM_MUX("IF1_2_ADC2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if1_2_adc2_mux),
	SND_SOC_DAPM_MUX("IF1_2_ADC3 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if1_2_adc3_mux),
	SND_SOC_DAPM_MUX("IF1_2_ADC4 Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if1_2_adc4_mux),
	SND_SOC_DAPM_MUX("TDM1 slot 01 Data Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_tdm1_adc_mux),
	SND_SOC_DAPM_MUX("TDM1 slot 23 Data Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_tdm1_adc_mux),
	SND_SOC_DAPM_MUX("TDM1 slot 45 Data Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_tdm1_adc_mux),
	SND_SOC_DAPM_MUX("TDM1 slot 67 Data Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_tdm1_adc_mux),
	SND_SOC_DAPM_MUX("TDM2 slot 01 Data Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_tdm2_adc_mux),
	SND_SOC_DAPM_MUX("TDM2 slot 23 Data Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_tdm2_adc_mux),
	SND_SOC_DAPM_MUX("TDM2 slot 45 Data Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_tdm2_adc_mux),
	SND_SOC_DAPM_MUX("TDM2 slot 67 Data Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_tdm2_adc_mux),
	SND_SOC_DAPM_MUX("IF2_1 ADC Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if2_1_adc_in_mux),
	SND_SOC_DAPM_MUX("IF2_2 ADC Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if2_2_adc_in_mux),
	SND_SOC_DAPM_MUX("IF3 ADC Mux", SND_SOC_NOPM, 0, 0,
		&rt5665_if3_adc_in_mux),
	SND_SOC_DAPM_MUX("IF1_1 0 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_1_01_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_1 1 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_1_01_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_1 2 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_1_23_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_1 3 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_1_23_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_1 4 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_1_45_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_1 5 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_1_45_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_1 6 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_1_67_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_1 7 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_1_67_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_2 0 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_2_01_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_2 1 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_2_01_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_2 2 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_2_23_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_2 3 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_2_23_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_2 4 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_2_45_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_2 5 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_2_45_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_2 6 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_2_67_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1_2 7 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if1_2_67_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF2_1 DAC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if2_1_dac_swap_mux),
	SND_SOC_DAPM_MUX("IF2_1 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if2_1_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF2_2 DAC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if2_2_dac_swap_mux),
	SND_SOC_DAPM_MUX("IF2_2 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if2_2_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF3 DAC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if3_dac_swap_mux),
	SND_SOC_DAPM_MUX("IF3 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5665_if3_adc_swap_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_OUT("AIF1_1TX slot 0", "AIF1_1 Capture",
				0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_1TX slot 1", "AIF1_1 Capture",
				1, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_1TX slot 2", "AIF1_1 Capture",
				2, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_1TX slot 3", "AIF1_1 Capture",
				3, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_1TX slot 4", "AIF1_1 Capture",
				4, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_1TX slot 5", "AIF1_1 Capture",
				5, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_1TX slot 6", "AIF1_1 Capture",
				6, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_1TX slot 7", "AIF1_1 Capture",
				7, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_2TX slot 0", "AIF1_2 Capture",
				0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_2TX slot 1", "AIF1_2 Capture",
				1, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_2TX slot 2", "AIF1_2 Capture",
				2, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_2TX slot 3", "AIF1_2 Capture",
				3, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_2TX slot 4", "AIF1_2 Capture",
				4, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_2TX slot 5", "AIF1_2 Capture",
				5, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_2TX slot 6", "AIF1_2 Capture",
				6, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1_2TX slot 7", "AIF1_2 Capture",
				7, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2_1TX", "AIF2_1 Capture",
				0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2_2TX", "AIF2_2 Capture",
				0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF3TX", "AIF3 Capture",
				0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback",
				0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2_1RX", "AIF2_1 Playback",
				0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2_2RX", "AIF2_2 Playback",
				0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF3RX", "AIF3 Playback",
				0, SND_SOC_NOPM, 0, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC1 MIXL", SND_SOC_NOPM, 0, 0,
		rt5665_dac_l_mix, ARRAY_SIZE(rt5665_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC1 MIXR", SND_SOC_NOPM, 0, 0,
		rt5665_dac_r_mix, ARRAY_SIZE(rt5665_dac_r_mix)),

	/* DAC channel Mux */
	SND_SOC_DAPM_MUX("DAC L1 Mux", SND_SOC_NOPM, 0, 0, &rt5665_dac_l1_mux),
	SND_SOC_DAPM_MUX("DAC R1 Mux", SND_SOC_NOPM, 0, 0, &rt5665_dac_r1_mux),
	SND_SOC_DAPM_MUX("DAC L2 Mux", SND_SOC_NOPM, 0, 0, &rt5665_dac_l2_mux),
	SND_SOC_DAPM_MUX("DAC R2 Mux", SND_SOC_NOPM, 0, 0, &rt5665_dac_r2_mux),
	SND_SOC_DAPM_MUX("DAC L3 Mux", SND_SOC_NOPM, 0, 0, &rt5665_dac_l3_mux),
	SND_SOC_DAPM_MUX("DAC R3 Mux", SND_SOC_NOPM, 0, 0, &rt5665_dac_r3_mux),

	SND_SOC_DAPM_MUX("DAC L1 Source", SND_SOC_NOPM, 0, 0,
		&rt5665_alg_dac_l1_mux),
	SND_SOC_DAPM_MUX("DAC R1 Source", SND_SOC_NOPM, 0, 0,
		&rt5665_alg_dac_r1_mux),
	SND_SOC_DAPM_MUX("DAC L2 Source", SND_SOC_NOPM, 0, 0,
		&rt5665_alg_dac_l2_mux),
	SND_SOC_DAPM_MUX("DAC R2 Source", SND_SOC_NOPM, 0, 0,
		&rt5665_alg_dac_r2_mux),

	/* DAC Mixer */
	SND_SOC_DAPM_SUPPLY("DAC Stereo1 Filter", RT5665_PWR_DIG_2,
		RT5665_PWR_DAC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Stereo2 Filter", RT5665_PWR_DIG_2,
		RT5665_PWR_DAC_S2F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Mono Left Filter", RT5665_PWR_DIG_2,
		RT5665_PWR_DAC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Mono Right Filter", RT5665_PWR_DIG_2,
		RT5665_PWR_DAC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo1 DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5665_sto1_dac_l_mix, ARRAY_SIZE(rt5665_sto1_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo1 DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5665_sto1_dac_r_mix, ARRAY_SIZE(rt5665_sto1_dac_r_mix)),
	SND_SOC_DAPM_MIXER("Stereo2 DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5665_sto2_dac_l_mix, ARRAY_SIZE(rt5665_sto2_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo2 DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5665_sto2_dac_r_mix, ARRAY_SIZE(rt5665_sto2_dac_r_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5665_mono_dac_l_mix, ARRAY_SIZE(rt5665_mono_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5665_mono_dac_r_mix, ARRAY_SIZE(rt5665_mono_dac_r_mix)),
	SND_SOC_DAPM_MUX("DAC MIXL", SND_SOC_NOPM, 0, 0,
		&rt5665_dig_dac_mixl_mux),
	SND_SOC_DAPM_MUX("DAC MIXR", SND_SOC_NOPM, 0, 0,
		&rt5665_dig_dac_mixr_mux),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC L1", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY("DAC L2 Power", RT5665_PWR_DIG_1,
		RT5665_PWR_DAC_L2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC R2 Power", RT5665_PWR_DIG_1,
		RT5665_PWR_DAC_R2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DAC L2", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC R2", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_PGA("DAC1 MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("DAC 1 Clock", 1, RT5665_CHOP_DAC,
		RT5665_CKGEN_DAC1_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC 2 Clock", 1, RT5665_CHOP_DAC,
		RT5665_CKGEN_DAC2_SFT, 0, NULL, 0),

	/* OUT Mixer */
	SND_SOC_DAPM_MIXER("MONOVOL MIX", RT5665_PWR_MIXER, RT5665_PWR_MM_BIT,
		0, rt5665_monovol_mix, ARRAY_SIZE(rt5665_monovol_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXL", RT5665_PWR_MIXER, RT5665_PWR_OM_L_BIT,
		0, rt5665_out_l_mix, ARRAY_SIZE(rt5665_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT5665_PWR_MIXER, RT5665_PWR_OM_R_BIT,
		0, rt5665_out_r_mix, ARRAY_SIZE(rt5665_out_r_mix)),

	/* Output Volume */
	SND_SOC_DAPM_SWITCH("MONOVOL", RT5665_PWR_VOL, RT5665_PWR_MV_BIT, 0,
		&monovol_switch),
	SND_SOC_DAPM_SWITCH("OUTVOL L", RT5665_PWR_VOL, RT5665_PWR_OV_L_BIT, 0,
		&outvol_l_switch),
	SND_SOC_DAPM_SWITCH("OUTVOL R", RT5665_PWR_VOL, RT5665_PWR_OV_R_BIT, 0,
		&outvol_r_switch),

	/* MONO/HPO/LOUT */
	SND_SOC_DAPM_MIXER("Mono MIX", SND_SOC_NOPM, 0,	0, rt5665_mono_mix,
		ARRAY_SIZE(rt5665_mono_mix)),
	SND_SOC_DAPM_MIXER("LOUT L MIX", SND_SOC_NOPM, 0, 0, rt5665_lout_l_mix,
		ARRAY_SIZE(rt5665_lout_l_mix)),
	SND_SOC_DAPM_MIXER("LOUT R MIX", SND_SOC_NOPM, 0, 0, rt5665_lout_r_mix,
		ARRAY_SIZE(rt5665_lout_r_mix)),
	SND_SOC_DAPM_PGA_S("Mono Amp", 1, RT5665_PWR_ANLG_1, RT5665_PWR_MA_BIT,
		0, rt5665_mono_event, SND_SOC_DAPM_POST_PMD |
		SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_PGA_S("HP Amp", 1, SND_SOC_NOPM, 0, 0, rt5665_hp_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_PGA_S("LOUT Amp", 1, RT5665_PWR_ANLG_1,
		RT5665_PWR_LM_BIT, 0, rt5665_lout_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_SUPPLY("Charge Pump", SND_SOC_NOPM, 0, 0,
		rt5665_charge_pump_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SWITCH("Mono Playback", SND_SOC_NOPM, 0, 0,
		&mono_switch),
	SND_SOC_DAPM_SWITCH("HPO Playback", SND_SOC_NOPM, 0, 0,
		&hpo_switch),
	SND_SOC_DAPM_SWITCH("LOUT L Playback", SND_SOC_NOPM, 0, 0,
		&lout_l_switch),
	SND_SOC_DAPM_SWITCH("LOUT R Playback", SND_SOC_NOPM, 0, 0,
		&lout_r_switch),
	SND_SOC_DAPM_SWITCH("PDM L Playback", SND_SOC_NOPM, 0, 0,
		&pdm_l_switch),
	SND_SOC_DAPM_SWITCH("PDM R Playback", SND_SOC_NOPM, 0, 0,
		&pdm_r_switch),

	/* PDM */
	SND_SOC_DAPM_SUPPLY("PDM Power", RT5665_PWR_DIG_2,
		RT5665_PWR_PDM1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MUX("PDM L Mux", SND_SOC_NOPM,
		0, 1, &rt5665_pdm_l_mux),
	SND_SOC_DAPM_MUX("PDM R Mux", SND_SOC_NOPM,
		0, 1, &rt5665_pdm_r_mux),

	/* CLK DET */
	SND_SOC_DAPM_SUPPLY("CLKDET SYS", RT5665_CLK_DET, RT5665_SYS_CLK_DET,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKDET HP", RT5665_CLK_DET, RT5665_HP_CLK_DET,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKDET MONO", RT5665_CLK_DET, RT5665_MONO_CLK_DET,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKDET LOUT", RT5665_CLK_DET, RT5665_LOUT_CLK_DET,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKDET", RT5665_CLK_DET, RT5665_POW_CLK_DET,
		0, NULL, 0),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
	SND_SOC_DAPM_OUTPUT("MONOOUT"),
	SND_SOC_DAPM_OUTPUT("PDML"),
	SND_SOC_DAPM_OUTPUT("PDMR"),
};

static const struct snd_soc_dapm_route rt5665_dapm_routes[] = {
	/*PLL*/
	{"ADC Stereo1 Filter", NULL, "PLL", is_sys_clk_from_pll},
	{"ADC Stereo2 Filter", NULL, "PLL", is_sys_clk_from_pll},
	{"ADC Mono Left Filter", NULL, "PLL", is_sys_clk_from_pll},
	{"ADC Mono Right Filter", NULL, "PLL", is_sys_clk_from_pll},
	{"DAC Stereo1 Filter", NULL, "PLL", is_sys_clk_from_pll},
	{"DAC Stereo2 Filter", NULL, "PLL", is_sys_clk_from_pll},
	{"DAC Mono Left Filter", NULL, "PLL", is_sys_clk_from_pll},
	{"DAC Mono Right Filter", NULL, "PLL", is_sys_clk_from_pll},

	/*ASRC*/
	{"ADC Stereo1 Filter", NULL, "ADC STO1 ASRC", is_using_asrc},
	{"ADC Stereo2 Filter", NULL, "ADC STO2 ASRC", is_using_asrc},
	{"ADC Mono Left Filter", NULL, "ADC Mono L ASRC", is_using_asrc},
	{"ADC Mono Right Filter", NULL, "ADC Mono R ASRC", is_using_asrc},
	{"DAC Mono Left Filter", NULL, "DAC Mono L ASRC", is_using_asrc},
	{"DAC Mono Right Filter", NULL, "DAC Mono R ASRC", is_using_asrc},
	{"DAC Stereo1 Filter", NULL, "DAC STO1 ASRC", is_using_asrc},
	{"DAC Stereo2 Filter", NULL, "DAC STO2 ASRC", is_using_asrc},
	{"I2S1 ASRC", NULL, "CLKDET"},
	{"I2S2 ASRC", NULL, "CLKDET"},
	{"I2S3 ASRC", NULL, "CLKDET"},

	/*Vref*/
	{"Mic Det Power", NULL, "Vref2"},
	{"MICBIAS1", NULL, "Vref1"},
	{"MICBIAS1", NULL, "Vref2"},
	{"MICBIAS2", NULL, "Vref1"},
	{"MICBIAS2", NULL, "Vref2"},
	{"MICBIAS3", NULL, "Vref1"},
	{"MICBIAS3", NULL, "Vref2"},

	{"Stereo1 DMIC L Mux", NULL, "DMIC STO1 ASRC"},
	{"Stereo1 DMIC R Mux", NULL, "DMIC STO1 ASRC"},
	{"Stereo2 DMIC L Mux", NULL, "DMIC STO2 ASRC"},
	{"Stereo2 DMIC R Mux", NULL, "DMIC STO2 ASRC"},
	{"Mono DMIC L Mux", NULL, "DMIC MONO L ASRC"},
	{"Mono DMIC R Mux", NULL, "DMIC MONO R ASRC"},

	{"I2S1_1", NULL, "I2S1 ASRC"},
	{"I2S1_2", NULL, "I2S1 ASRC"},
	{"I2S2_1", NULL, "I2S2 ASRC"},
	{"I2S2_2", NULL, "I2S2 ASRC"},
	{"I2S3", NULL, "I2S3 ASRC"},

	{"CLKDET SYS", NULL, "CLKDET"},
	{"CLKDET HP", NULL, "CLKDET"},
	{"CLKDET MONO", NULL, "CLKDET"},
	{"CLKDET LOUT", NULL, "CLKDET"},

	{"IN1P", NULL, "LDO2"},
	{"IN2P", NULL, "LDO2"},
	{"IN3P", NULL, "LDO2"},
	{"IN4P", NULL, "LDO2"},

	{"DMIC1", NULL, "DMIC L1"},
	{"DMIC1", NULL, "DMIC R1"},
	{"DMIC2", NULL, "DMIC L2"},
	{"DMIC2", NULL, "DMIC R2"},

	{"BST1", NULL, "IN1P"},
	{"BST1", NULL, "IN1N"},
	{"BST1", NULL, "BST1 Power"},
	{"BST1", NULL, "BST1P Power"},
	{"BST2", NULL, "IN2P"},
	{"BST2", NULL, "IN2N"},
	{"BST2", NULL, "BST2 Power"},
	{"BST2", NULL, "BST2P Power"},
	{"BST3", NULL, "IN3P"},
	{"BST3", NULL, "IN3N"},
	{"BST3", NULL, "BST3 Power"},
	{"BST3", NULL, "BST3P Power"},
	{"BST4", NULL, "IN4P"},
	{"BST4", NULL, "IN4N"},
	{"BST4", NULL, "BST4 Power"},
	{"BST4", NULL, "BST4P Power"},
	{"BST1 CBJ", NULL, "IN1P"},
	{"BST1 CBJ", NULL, "IN1N"},
	{"BST1 CBJ", NULL, "CBJ Power"},
	{"CBJ Power", NULL, "Vref2"},

	{"INL VOL", NULL, "IN3P"},
	{"INR VOL", NULL, "IN3N"},

	{"RECMIX1L", "CBJ Switch", "BST1 CBJ"},
	{"RECMIX1L", "INL Switch", "INL VOL"},
	{"RECMIX1L", "INR Switch", "INR VOL"},
	{"RECMIX1L", "BST4 Switch", "BST4"},
	{"RECMIX1L", "BST3 Switch", "BST3"},
	{"RECMIX1L", "BST2 Switch", "BST2"},
	{"RECMIX1L", "BST1 Switch", "BST1"},
	{"RECMIX1L", NULL, "RECMIX1L Power"},

	{"RECMIX1R", "MONOVOL Switch", "MONOVOL"},
	{"RECMIX1R", "INR Switch", "INR VOL"},
	{"RECMIX1R", "BST4 Switch", "BST4"},
	{"RECMIX1R", "BST3 Switch", "BST3"},
	{"RECMIX1R", "BST2 Switch", "BST2"},
	{"RECMIX1R", "BST1 Switch", "BST1"},
	{"RECMIX1R", NULL, "RECMIX1R Power"},

	{"RECMIX2L", "CBJ Switch", "BST1 CBJ"},
	{"RECMIX2L", "INL Switch", "INL VOL"},
	{"RECMIX2L", "INR Switch", "INR VOL"},
	{"RECMIX2L", "BST4 Switch", "BST4"},
	{"RECMIX2L", "BST3 Switch", "BST3"},
	{"RECMIX2L", "BST2 Switch", "BST2"},
	{"RECMIX2L", "BST1 Switch", "BST1"},
	{"RECMIX2L", NULL, "RECMIX2L Power"},

	{"RECMIX2R", "MONOVOL Switch", "MONOVOL"},
	{"RECMIX2R", "INL Switch", "INL VOL"},
	{"RECMIX2R", "INR Switch", "INR VOL"},
	{"RECMIX2R", "BST4 Switch", "BST4"},
	{"RECMIX2R", "BST3 Switch", "BST3"},
	{"RECMIX2R", "BST2 Switch", "BST2"},
	{"RECMIX2R", "BST1 Switch", "BST1"},
	{"RECMIX2R", NULL, "RECMIX2R Power"},

	{"ADC1 L", NULL, "RECMIX1L"},
	{"ADC1 L", NULL, "ADC1 L Power"},
	{"ADC1 L", NULL, "ADC1 clock"},
	{"ADC1 R", NULL, "RECMIX1R"},
	{"ADC1 R", NULL, "ADC1 R Power"},
	{"ADC1 R", NULL, "ADC1 clock"},

	{"ADC2 L", NULL, "RECMIX2L"},
	{"ADC2 L", NULL, "ADC2 L Power"},
	{"ADC2 L", NULL, "ADC2 clock"},
	{"ADC2 R", NULL, "RECMIX2R"},
	{"ADC2 R", NULL, "ADC2 R Power"},
	{"ADC2 R", NULL, "ADC2 clock"},

	{"DMIC L1", NULL, "DMIC CLK"},
	{"DMIC L1", NULL, "DMIC1 Power"},
	{"DMIC R1", NULL, "DMIC CLK"},
	{"DMIC R1", NULL, "DMIC1 Power"},
	{"DMIC L2", NULL, "DMIC CLK"},
	{"DMIC L2", NULL, "DMIC2 Power"},
	{"DMIC R2", NULL, "DMIC CLK"},
	{"DMIC R2", NULL, "DMIC2 Power"},

	{"Stereo1 DMIC L Mux", "DMIC1", "DMIC L1"},
	{"Stereo1 DMIC L Mux", "DMIC2", "DMIC L2"},

	{"Stereo1 DMIC R Mux", "DMIC1", "DMIC R1"},
	{"Stereo1 DMIC R Mux", "DMIC2", "DMIC R2"},

	{"Mono DMIC L Mux", "DMIC1 L", "DMIC L1"},
	{"Mono DMIC L Mux", "DMIC2 L", "DMIC L2"},

	{"Mono DMIC R Mux", "DMIC1 R", "DMIC R1"},
	{"Mono DMIC R Mux", "DMIC2 R", "DMIC R2"},

	{"Stereo2 DMIC L Mux", "DMIC1", "DMIC L1"},
	{"Stereo2 DMIC L Mux", "DMIC2", "DMIC L2"},

	{"Stereo2 DMIC R Mux", "DMIC1", "DMIC R1"},
	{"Stereo2 DMIC R Mux", "DMIC2", "DMIC R2"},

	{"Stereo1 ADC L Mux", "ADC1 L", "ADC1 L"},
	{"Stereo1 ADC L Mux", "ADC1 R", "ADC1 R"},
	{"Stereo1 ADC L Mux", "ADC2 L", "ADC2 L"},
	{"Stereo1 ADC L Mux", "ADC2 R", "ADC2 R"},
	{"Stereo1 ADC R Mux", "ADC1 L", "ADC1 L"},
	{"Stereo1 ADC R Mux", "ADC1 R", "ADC1 R"},
	{"Stereo1 ADC R Mux", "ADC2 L", "ADC2 L"},
	{"Stereo1 ADC R Mux", "ADC2 R", "ADC2 R"},

	{"Stereo1 DD L Mux", "STO2 DAC", "Stereo2 DAC MIXL"},
	{"Stereo1 DD L Mux", "MONO DAC", "Mono DAC MIXL"},

	{"Stereo1 DD R Mux", "STO2 DAC", "Stereo2 DAC MIXR"},
	{"Stereo1 DD R Mux", "MONO DAC", "Mono DAC MIXR"},

	{"Stereo1 ADC L1 Mux", "ADC", "Stereo1 ADC L Mux"},
	{"Stereo1 ADC L1 Mux", "DD Mux", "Stereo1 DD L Mux"},
	{"Stereo1 ADC L2 Mux", "DMIC", "Stereo1 DMIC L Mux"},
	{"Stereo1 ADC L2 Mux", "DAC MIX", "DAC MIXL"},

	{"Stereo1 ADC R1 Mux", "ADC", "Stereo1 ADC R Mux"},
	{"Stereo1 ADC R1 Mux", "DD Mux", "Stereo1 DD R Mux"},
	{"Stereo1 ADC R2 Mux", "DMIC", "Stereo1 DMIC R Mux"},
	{"Stereo1 ADC R2 Mux", "DAC MIX", "DAC MIXR"},

	{"Mono ADC L Mux", "ADC1 L", "ADC1 L"},
	{"Mono ADC L Mux", "ADC1 R", "ADC1 R"},
	{"Mono ADC L Mux", "ADC2 L", "ADC2 L"},
	{"Mono ADC L Mux", "ADC2 R", "ADC2 R"},

	{"Mono ADC R Mux", "ADC1 L", "ADC1 L"},
	{"Mono ADC R Mux", "ADC1 R", "ADC1 R"},
	{"Mono ADC R Mux", "ADC2 L", "ADC2 L"},
	{"Mono ADC R Mux", "ADC2 R", "ADC2 R"},

	{"Mono DD L Mux", "STO2 DAC", "Stereo2 DAC MIXL"},
	{"Mono DD L Mux", "MONO DAC", "Mono DAC MIXL"},

	{"Mono DD R Mux", "STO2 DAC", "Stereo2 DAC MIXR"},
	{"Mono DD R Mux", "MONO DAC", "Mono DAC MIXR"},

	{"Mono ADC L2 Mux", "DMIC", "Mono DMIC L Mux"},
	{"Mono ADC L2 Mux", "DAC MIXL", "DAC MIXL"},
	{"Mono ADC L1 Mux", "DD Mux", "Mono DD L Mux"},
	{"Mono ADC L1 Mux", "ADC",  "Mono ADC L Mux"},

	{"Mono ADC R1 Mux", "DD Mux", "Mono DD R Mux"},
	{"Mono ADC R1 Mux", "ADC", "Mono ADC R Mux"},
	{"Mono ADC R2 Mux", "DMIC", "Mono DMIC R Mux"},
	{"Mono ADC R2 Mux", "DAC MIXR", "DAC MIXR"},

	{"Stereo2 ADC L Mux", "ADC1 L", "ADC1 L"},
	{"Stereo2 ADC L Mux", "ADC2 L", "ADC2 L"},
	{"Stereo2 ADC L Mux", "ADC1 R", "ADC1 R"},
	{"Stereo2 ADC R Mux", "ADC1 L", "ADC1 L"},
	{"Stereo2 ADC R Mux", "ADC2 L", "ADC2 L"},
	{"Stereo2 ADC R Mux", "ADC1 R", "ADC1 R"},

	{"Stereo2 DD L Mux", "STO2 DAC", "Stereo2 DAC MIXL"},
	{"Stereo2 DD L Mux", "MONO DAC", "Mono DAC MIXL"},

	{"Stereo2 DD R Mux", "STO2 DAC", "Stereo2 DAC MIXR"},
	{"Stereo2 DD R Mux", "MONO DAC", "Mono DAC MIXR"},

	{"Stereo2 ADC L1 Mux", "ADC", "Stereo2 ADC L Mux"},
	{"Stereo2 ADC L1 Mux", "DD Mux", "Stereo2 DD L Mux"},
	{"Stereo2 ADC L2 Mux", "DMIC", "Stereo2 DMIC L Mux"},
	{"Stereo2 ADC L2 Mux", "DAC MIX", "DAC MIXL"},

	{"Stereo2 ADC R1 Mux", "ADC", "Stereo2 ADC R Mux"},
	{"Stereo2 ADC R1 Mux", "DD Mux", "Stereo2 DD R Mux"},
	{"Stereo2 ADC R2 Mux", "DMIC", "Stereo2 DMIC R Mux"},
	{"Stereo2 ADC R2 Mux", "DAC MIX", "DAC MIXR"},

	{"Stereo1 ADC MIXL", "ADC1 Switch", "Stereo1 ADC L1 Mux"},
	{"Stereo1 ADC MIXL", "ADC2 Switch", "Stereo1 ADC L2 Mux"},
	{"Stereo1 ADC MIXL", NULL, "ADC Stereo1 Filter"},

	{"Stereo1 ADC MIXR", "ADC1 Switch", "Stereo1 ADC R1 Mux"},
	{"Stereo1 ADC MIXR", "ADC2 Switch", "Stereo1 ADC R2 Mux"},
	{"Stereo1 ADC MIXR", NULL, "ADC Stereo1 Filter"},

	{"Mono ADC MIXL", "ADC1 Switch", "Mono ADC L1 Mux"},
	{"Mono ADC MIXL", "ADC2 Switch", "Mono ADC L2 Mux"},
	{"Mono ADC MIXL", NULL, "ADC Mono Left Filter"},

	{"Mono ADC MIXR", "ADC1 Switch", "Mono ADC R1 Mux"},
	{"Mono ADC MIXR", "ADC2 Switch", "Mono ADC R2 Mux"},
	{"Mono ADC MIXR", NULL, "ADC Mono Right Filter"},

	{"Stereo2 ADC MIXL", "ADC1 Switch", "Stereo2 ADC L1 Mux"},
	{"Stereo2 ADC MIXL", "ADC2 Switch", "Stereo2 ADC L2 Mux"},
	{"Stereo2 ADC MIXL", NULL, "ADC Stereo2 Filter"},

	{"Stereo2 ADC MIXR", "ADC1 Switch", "Stereo2 ADC R1 Mux"},
	{"Stereo2 ADC MIXR", "ADC2 Switch", "Stereo2 ADC R2 Mux"},
	{"Stereo2 ADC MIXR", NULL, "ADC Stereo2 Filter"},

	{"Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXL"},
	{"Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXR"},
	{"Stereo2 ADC MIX", NULL, "Stereo2 ADC MIXL"},
	{"Stereo2 ADC MIX", NULL, "Stereo2 ADC MIXR"},
	{"Mono ADC MIX", NULL, "Mono ADC MIXL"},
	{"Mono ADC MIX", NULL, "Mono ADC MIXR"},

	{"IF1_1_ADC1 Mux", "STO1 ADC", "Stereo1 ADC MIX"},
	{"IF1_1_ADC1 Mux", "IF2_1 DAC", "IF2_1 DAC"},
	{"IF1_1_ADC2 Mux", "STO2 ADC", "Stereo2 ADC MIX"},
	{"IF1_1_ADC2 Mux", "IF2_2 DAC", "IF2_2 DAC"},
	{"IF1_1_ADC3 Mux", "MONO ADC", "Mono ADC MIX"},
	{"IF1_1_ADC3 Mux", "IF3 DAC", "IF3 DAC"},
	{"IF1_1_ADC4", NULL, "DAC1 MIX"},

	{"IF1_2_ADC1 Mux", "STO1 ADC", "Stereo1 ADC MIX"},
	{"IF1_2_ADC1 Mux", "IF1 DAC", "IF1 DAC1"},
	{"IF1_2_ADC2 Mux", "STO2 ADC", "Stereo2 ADC MIX"},
	{"IF1_2_ADC2 Mux", "IF2_1 DAC", "IF2_1 DAC"},
	{"IF1_2_ADC3 Mux", "MONO ADC", "Mono ADC MIX"},
	{"IF1_2_ADC3 Mux", "IF2_2 DAC", "IF2_2 DAC"},
	{"IF1_2_ADC4 Mux", "DAC1", "DAC1 MIX"},
	{"IF1_2_ADC4 Mux", "IF3 DAC", "IF3 DAC"},

	{"TDM1 slot 01 Data Mux", "1234", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 01 Data Mux", "1243", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 01 Data Mux", "1324", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 01 Data Mux", "1342", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 01 Data Mux", "1432", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 01 Data Mux", "1423", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 01 Data Mux", "2134", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 01 Data Mux", "2143", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 01 Data Mux", "2314", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 01 Data Mux", "2341", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 01 Data Mux", "2431", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 01 Data Mux", "2413", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 01 Data Mux", "3124", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 01 Data Mux", "3142", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 01 Data Mux", "3214", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 01 Data Mux", "3241", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 01 Data Mux", "3412", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 01 Data Mux", "3421", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 01 Data Mux", "4123", "IF1_1_ADC4"},
	{"TDM1 slot 01 Data Mux", "4132", "IF1_1_ADC4"},
	{"TDM1 slot 01 Data Mux", "4213", "IF1_1_ADC4"},
	{"TDM1 slot 01 Data Mux", "4231", "IF1_1_ADC4"},
	{"TDM1 slot 01 Data Mux", "4312", "IF1_1_ADC4"},
	{"TDM1 slot 01 Data Mux", "4321", "IF1_1_ADC4"},
	{"TDM1 slot 01 Data Mux", NULL, "I2S1_1"},

	{"TDM1 slot 23 Data Mux", "1234", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 23 Data Mux", "1243", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 23 Data Mux", "1324", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 23 Data Mux", "1342", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 23 Data Mux", "1432", "IF1_1_ADC4"},
	{"TDM1 slot 23 Data Mux", "1423", "IF1_1_ADC4"},
	{"TDM1 slot 23 Data Mux", "2134", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 23 Data Mux", "2143", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 23 Data Mux", "2314", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 23 Data Mux", "2341", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 23 Data Mux", "2431", "IF1_1_ADC4"},
	{"TDM1 slot 23 Data Mux", "2413", "IF1_1_ADC4"},
	{"TDM1 slot 23 Data Mux", "3124", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 23 Data Mux", "3142", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 23 Data Mux", "3214", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 23 Data Mux", "3241", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 23 Data Mux", "3412", "IF1_1_ADC4"},
	{"TDM1 slot 23 Data Mux", "3421", "IF1_1_ADC4"},
	{"TDM1 slot 23 Data Mux", "4123", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 23 Data Mux", "4132", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 23 Data Mux", "4213", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 23 Data Mux", "4231", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 23 Data Mux", "4312", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 23 Data Mux", "4321", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 23 Data Mux", NULL, "I2S1_1"},

	{"TDM1 slot 45 Data Mux", "1234", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 45 Data Mux", "1243", "IF1_1_ADC4"},
	{"TDM1 slot 45 Data Mux", "1324", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 45 Data Mux", "1342", "IF1_1_ADC4"},
	{"TDM1 slot 45 Data Mux", "1432", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 45 Data Mux", "1423", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 45 Data Mux", "2134", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 45 Data Mux", "2143", "IF1_1_ADC4"},
	{"TDM1 slot 45 Data Mux", "2314", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 45 Data Mux", "2341", "IF1_1_ADC4"},
	{"TDM1 slot 45 Data Mux", "2431", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 45 Data Mux", "2413", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 45 Data Mux", "3124", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 45 Data Mux", "3142", "IF1_1_ADC4"},
	{"TDM1 slot 45 Data Mux", "3214", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 45 Data Mux", "3241", "IF1_1_ADC4"},
	{"TDM1 slot 45 Data Mux", "3412", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 45 Data Mux", "3421", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 45 Data Mux", "4123", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 45 Data Mux", "4132", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 45 Data Mux", "4213", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 45 Data Mux", "4231", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 45 Data Mux", "4312", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 45 Data Mux", "4321", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 45 Data Mux", NULL, "I2S1_1"},

	{"TDM1 slot 67 Data Mux", "1234", "IF1_1_ADC4"},
	{"TDM1 slot 67 Data Mux", "1243", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 67 Data Mux", "1324", "IF1_1_ADC4"},
	{"TDM1 slot 67 Data Mux", "1342", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 67 Data Mux", "1432", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 67 Data Mux", "1423", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 67 Data Mux", "2134", "IF1_1_ADC4"},
	{"TDM1 slot 67 Data Mux", "2143", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 67 Data Mux", "2314", "IF1_1_ADC4"},
	{"TDM1 slot 67 Data Mux", "2341", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 67 Data Mux", "2431", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 67 Data Mux", "2413", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 67 Data Mux", "3124", "IF1_1_ADC4"},
	{"TDM1 slot 67 Data Mux", "3142", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 67 Data Mux", "3214", "IF1_1_ADC4"},
	{"TDM1 slot 67 Data Mux", "3241", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 67 Data Mux", "3412", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 67 Data Mux", "3421", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 67 Data Mux", "4123", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 67 Data Mux", "4132", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 67 Data Mux", "4213", "IF1_1_ADC3 Mux"},
	{"TDM1 slot 67 Data Mux", "4231", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 67 Data Mux", "4312", "IF1_1_ADC2 Mux"},
	{"TDM1 slot 67 Data Mux", "4321", "IF1_1_ADC1 Mux"},
	{"TDM1 slot 67 Data Mux", NULL, "I2S1_1"},


	{"TDM2 slot 01 Data Mux", "1234", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 01 Data Mux", "1243", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 01 Data Mux", "1324", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 01 Data Mux", "1342", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 01 Data Mux", "1432", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 01 Data Mux", "1423", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 01 Data Mux", "2134", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 01 Data Mux", "2143", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 01 Data Mux", "2314", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 01 Data Mux", "2341", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 01 Data Mux", "2431", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 01 Data Mux", "2413", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 01 Data Mux", "3124", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 01 Data Mux", "3142", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 01 Data Mux", "3214", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 01 Data Mux", "3241", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 01 Data Mux", "3412", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 01 Data Mux", "3421", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 01 Data Mux", "4123", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 01 Data Mux", "4132", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 01 Data Mux", "4213", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 01 Data Mux", "4231", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 01 Data Mux", "4312", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 01 Data Mux", "4321", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 01 Data Mux", NULL, "I2S1_2"},

	{"TDM2 slot 23 Data Mux", "1234", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 23 Data Mux", "1243", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 23 Data Mux", "1324", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 23 Data Mux", "1342", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 23 Data Mux", "1432", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 23 Data Mux", "1423", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 23 Data Mux", "2134", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 23 Data Mux", "2143", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 23 Data Mux", "2314", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 23 Data Mux", "2341", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 23 Data Mux", "2431", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 23 Data Mux", "2413", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 23 Data Mux", "3124", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 23 Data Mux", "3142", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 23 Data Mux", "3214", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 23 Data Mux", "3241", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 23 Data Mux", "3412", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 23 Data Mux", "3421", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 23 Data Mux", "4123", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 23 Data Mux", "4132", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 23 Data Mux", "4213", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 23 Data Mux", "4231", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 23 Data Mux", "4312", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 23 Data Mux", "4321", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 23 Data Mux", NULL, "I2S1_2"},

	{"TDM2 slot 45 Data Mux", "1234", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 45 Data Mux", "1243", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 45 Data Mux", "1324", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 45 Data Mux", "1342", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 45 Data Mux", "1432", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 45 Data Mux", "1423", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 45 Data Mux", "2134", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 45 Data Mux", "2143", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 45 Data Mux", "2314", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 45 Data Mux", "2341", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 45 Data Mux", "2431", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 45 Data Mux", "2413", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 45 Data Mux", "3124", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 45 Data Mux", "3142", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 45 Data Mux", "3214", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 45 Data Mux", "3241", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 45 Data Mux", "3412", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 45 Data Mux", "3421", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 45 Data Mux", "4123", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 45 Data Mux", "4132", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 45 Data Mux", "4213", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 45 Data Mux", "4231", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 45 Data Mux", "4312", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 45 Data Mux", "4321", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 45 Data Mux", NULL, "I2S1_2"},

	{"TDM2 slot 67 Data Mux", "1234", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 67 Data Mux", "1243", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 67 Data Mux", "1324", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 67 Data Mux", "1342", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 67 Data Mux", "1432", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 67 Data Mux", "1423", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 67 Data Mux", "2134", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 67 Data Mux", "2143", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 67 Data Mux", "2314", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 67 Data Mux", "2341", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 67 Data Mux", "2431", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 67 Data Mux", "2413", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 67 Data Mux", "3124", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 67 Data Mux", "3142", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 67 Data Mux", "3214", "IF1_2_ADC4 Mux"},
	{"TDM2 slot 67 Data Mux", "3241", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 67 Data Mux", "3412", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 67 Data Mux", "3421", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 67 Data Mux", "4123", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 67 Data Mux", "4132", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 67 Data Mux", "4213", "IF1_2_ADC3 Mux"},
	{"TDM2 slot 67 Data Mux", "4231", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 67 Data Mux", "4312", "IF1_2_ADC2 Mux"},
	{"TDM2 slot 67 Data Mux", "4321", "IF1_2_ADC1 Mux"},
	{"TDM2 slot 67 Data Mux", NULL, "I2S1_2"},

	{"IF1_1 0 ADC Swap Mux", "L/R", "TDM1 slot 01 Data Mux"},
	{"IF1_1 0 ADC Swap Mux", "L/L", "TDM1 slot 01 Data Mux"},
	{"IF1_1 1 ADC Swap Mux", "R/L", "TDM1 slot 01 Data Mux"},
	{"IF1_1 1 ADC Swap Mux", "R/R", "TDM1 slot 01 Data Mux"},
	{"IF1_1 2 ADC Swap Mux", "L/R", "TDM1 slot 23 Data Mux"},
	{"IF1_1 2 ADC Swap Mux", "R/L", "TDM1 slot 23 Data Mux"},
	{"IF1_1 3 ADC Swap Mux", "L/L", "TDM1 slot 23 Data Mux"},
	{"IF1_1 3 ADC Swap Mux", "R/R", "TDM1 slot 23 Data Mux"},
	{"IF1_1 4 ADC Swap Mux", "L/R", "TDM1 slot 45 Data Mux"},
	{"IF1_1 4 ADC Swap Mux", "R/L", "TDM1 slot 45 Data Mux"},
	{"IF1_1 5 ADC Swap Mux", "L/L", "TDM1 slot 45 Data Mux"},
	{"IF1_1 5 ADC Swap Mux", "R/R", "TDM1 slot 45 Data Mux"},
	{"IF1_1 6 ADC Swap Mux", "L/R", "TDM1 slot 67 Data Mux"},
	{"IF1_1 6 ADC Swap Mux", "R/L", "TDM1 slot 67 Data Mux"},
	{"IF1_1 7 ADC Swap Mux", "L/L", "TDM1 slot 67 Data Mux"},
	{"IF1_1 7 ADC Swap Mux", "R/R", "TDM1 slot 67 Data Mux"},
	{"IF1_2 0 ADC Swap Mux", "L/R", "TDM2 slot 01 Data Mux"},
	{"IF1_2 0 ADC Swap Mux", "R/L", "TDM2 slot 01 Data Mux"},
	{"IF1_2 1 ADC Swap Mux", "L/L", "TDM2 slot 01 Data Mux"},
	{"IF1_2 1 ADC Swap Mux", "R/R", "TDM2 slot 01 Data Mux"},
	{"IF1_2 2 ADC Swap Mux", "L/R", "TDM2 slot 23 Data Mux"},
	{"IF1_2 2 ADC Swap Mux", "R/L", "TDM2 slot 23 Data Mux"},
	{"IF1_2 3 ADC Swap Mux", "L/L", "TDM2 slot 23 Data Mux"},
	{"IF1_2 3 ADC Swap Mux", "R/R", "TDM2 slot 23 Data Mux"},
	{"IF1_2 4 ADC Swap Mux", "L/R", "TDM2 slot 45 Data Mux"},
	{"IF1_2 4 ADC Swap Mux", "R/L", "TDM2 slot 45 Data Mux"},
	{"IF1_2 5 ADC Swap Mux", "L/L", "TDM2 slot 45 Data Mux"},
	{"IF1_2 5 ADC Swap Mux", "R/R", "TDM2 slot 45 Data Mux"},
	{"IF1_2 6 ADC Swap Mux", "L/R", "TDM2 slot 67 Data Mux"},
	{"IF1_2 6 ADC Swap Mux", "R/L", "TDM2 slot 67 Data Mux"},
	{"IF1_2 7 ADC Swap Mux", "L/L", "TDM2 slot 67 Data Mux"},
	{"IF1_2 7 ADC Swap Mux", "R/R", "TDM2 slot 67 Data Mux"},

	{"IF2_1 ADC Mux", "STO1 ADC", "Stereo1 ADC MIX"},
	{"IF2_1 ADC Mux", "STO2 ADC", "Stereo2 ADC MIX"},
	{"IF2_1 ADC Mux", "MONO ADC", "Mono ADC MIX"},
	{"IF2_1 ADC Mux", "IF1 DAC1", "IF1 DAC1"},
	{"IF2_1 ADC Mux", "IF1 DAC2", "IF1 DAC2"},
	{"IF2_1 ADC Mux", "IF2_2 DAC", "IF2_2 DAC"},
	{"IF2_1 ADC Mux", "IF3 DAC", "IF3 DAC"},
	{"IF2_1 ADC Mux", "DAC1 MIX", "DAC1 MIX"},
	{"IF2_1 ADC", NULL, "IF2_1 ADC Mux"},
	{"IF2_1 ADC", NULL, "I2S2_1"},

	{"IF2_2 ADC Mux", "STO1 ADC", "Stereo1 ADC MIX"},
	{"IF2_2 ADC Mux", "STO2 ADC", "Stereo2 ADC MIX"},
	{"IF2_2 ADC Mux", "MONO ADC", "Mono ADC MIX"},
	{"IF2_2 ADC Mux", "IF1 DAC1", "IF1 DAC1"},
	{"IF2_2 ADC Mux", "IF1 DAC2", "IF1 DAC2"},
	{"IF2_2 ADC Mux", "IF2_1 DAC", "IF2_1 DAC"},
	{"IF2_2 ADC Mux", "IF3 DAC", "IF3 DAC"},
	{"IF2_2 ADC Mux", "DAC1 MIX", "DAC1 MIX"},
	{"IF2_2 ADC", NULL, "IF2_2 ADC Mux"},
	{"IF2_2 ADC", NULL, "I2S2_2"},

	{"IF3 ADC Mux", "STO1 ADC", "Stereo1 ADC MIX"},
	{"IF3 ADC Mux", "STO2 ADC", "Stereo2 ADC MIX"},
	{"IF3 ADC Mux", "MONO ADC", "Mono ADC MIX"},
	{"IF3 ADC Mux", "IF1 DAC1", "IF1 DAC1"},
	{"IF3 ADC Mux", "IF1 DAC2", "IF1 DAC2"},
	{"IF3 ADC Mux", "IF2_1 DAC", "IF2_1 DAC"},
	{"IF3 ADC Mux", "IF2_2 DAC", "IF2_2 DAC"},
	{"IF3 ADC Mux", "DAC1 MIX", "DAC1 MIX"},
	{"IF3 ADC", NULL, "IF3 ADC Mux"},
	{"IF3 ADC", NULL, "I2S3"},

	{"AIF1_1TX slot 0", NULL, "IF1_1 0 ADC Swap Mux"},
	{"AIF1_1TX slot 1", NULL, "IF1_1 1 ADC Swap Mux"},
	{"AIF1_1TX slot 2", NULL, "IF1_1 2 ADC Swap Mux"},
	{"AIF1_1TX slot 3", NULL, "IF1_1 3 ADC Swap Mux"},
	{"AIF1_1TX slot 4", NULL, "IF1_1 4 ADC Swap Mux"},
	{"AIF1_1TX slot 5", NULL, "IF1_1 5 ADC Swap Mux"},
	{"AIF1_1TX slot 6", NULL, "IF1_1 6 ADC Swap Mux"},
	{"AIF1_1TX slot 7", NULL, "IF1_1 7 ADC Swap Mux"},
	{"AIF1_2TX slot 0", NULL, "IF1_2 0 ADC Swap Mux"},
	{"AIF1_2TX slot 1", NULL, "IF1_2 1 ADC Swap Mux"},
	{"AIF1_2TX slot 2", NULL, "IF1_2 2 ADC Swap Mux"},
	{"AIF1_2TX slot 3", NULL, "IF1_2 3 ADC Swap Mux"},
	{"AIF1_2TX slot 4", NULL, "IF1_2 4 ADC Swap Mux"},
	{"AIF1_2TX slot 5", NULL, "IF1_2 5 ADC Swap Mux"},
	{"AIF1_2TX slot 6", NULL, "IF1_2 6 ADC Swap Mux"},
	{"AIF1_2TX slot 7", NULL, "IF1_2 7 ADC Swap Mux"},
	{"IF2_1 ADC Swap Mux", "L/R", "IF2_1 ADC"},
	{"IF2_1 ADC Swap Mux", "R/L", "IF2_1 ADC"},
	{"IF2_1 ADC Swap Mux", "L/L", "IF2_1 ADC"},
	{"IF2_1 ADC Swap Mux", "R/R", "IF2_1 ADC"},
	{"AIF2_1TX", NULL, "IF2_1 ADC Swap Mux"},
	{"IF2_2 ADC Swap Mux", "L/R", "IF2_2 ADC"},
	{"IF2_2 ADC Swap Mux", "R/L", "IF2_2 ADC"},
	{"IF2_2 ADC Swap Mux", "L/L", "IF2_2 ADC"},
	{"IF2_2 ADC Swap Mux", "R/R", "IF2_2 ADC"},
	{"AIF2_2TX", NULL, "IF2_2 ADC Swap Mux"},
	{"IF3 ADC Swap Mux", "L/R", "IF3 ADC"},
	{"IF3 ADC Swap Mux", "R/L", "IF3 ADC"},
	{"IF3 ADC Swap Mux", "L/L", "IF3 ADC"},
	{"IF3 ADC Swap Mux", "R/R", "IF3 ADC"},
	{"AIF3TX", NULL, "IF3 ADC Swap Mux"},

	{"IF1 DAC1", NULL, "AIF1RX"},
	{"IF1 DAC2", NULL, "AIF1RX"},
	{"IF1 DAC3", NULL, "AIF1RX"},
	{"IF2_1 DAC Swap Mux", "L/R", "AIF2_1RX"},
	{"IF2_1 DAC Swap Mux", "R/L", "AIF2_1RX"},
	{"IF2_1 DAC Swap Mux", "L/L", "AIF2_1RX"},
	{"IF2_1 DAC Swap Mux", "R/R", "AIF2_1RX"},
	{"IF2_2 DAC Swap Mux", "L/R", "AIF2_2RX"},
	{"IF2_2 DAC Swap Mux", "R/L", "AIF2_2RX"},
	{"IF2_2 DAC Swap Mux", "L/L", "AIF2_2RX"},
	{"IF2_2 DAC Swap Mux", "R/R", "AIF2_2RX"},
	{"IF2_1 DAC", NULL, "IF2_1 DAC Swap Mux"},
	{"IF2_2 DAC", NULL, "IF2_2 DAC Swap Mux"},
	{"IF3 DAC Swap Mux", "L/R", "AIF3RX"},
	{"IF3 DAC Swap Mux", "R/L", "AIF3RX"},
	{"IF3 DAC Swap Mux", "L/L", "AIF3RX"},
	{"IF3 DAC Swap Mux", "R/R", "AIF3RX"},
	{"IF3 DAC", NULL, "IF3 DAC Swap Mux"},

	{"IF1 DAC1", NULL, "I2S1_1"},
	{"IF1 DAC2", NULL, "I2S1_1"},
	{"IF1 DAC3", NULL, "I2S1_1"},
	{"IF2_1 DAC", NULL, "I2S2_1"},
	{"IF2_2 DAC", NULL, "I2S2_2"},
	{"IF3 DAC", NULL, "I2S3"},

	{"IF1 DAC1 L", NULL, "IF1 DAC1"},
	{"IF1 DAC1 R", NULL, "IF1 DAC1"},
	{"IF1 DAC2 L", NULL, "IF1 DAC2"},
	{"IF1 DAC2 R", NULL, "IF1 DAC2"},
	{"IF1 DAC3 L", NULL, "IF1 DAC3"},
	{"IF1 DAC3 R", NULL, "IF1 DAC3"},
	{"IF2_1 DAC L", NULL, "IF2_1 DAC"},
	{"IF2_1 DAC R", NULL, "IF2_1 DAC"},
	{"IF2_2 DAC L", NULL, "IF2_2 DAC"},
	{"IF2_2 DAC R", NULL, "IF2_2 DAC"},
	{"IF3 DAC L", NULL, "IF3 DAC"},
	{"IF3 DAC R", NULL, "IF3 DAC"},

	{"DAC L1 Mux", "IF1 DAC1", "IF1 DAC1 L"},
	{"DAC L1 Mux", "IF2_1 DAC", "IF2_1 DAC L"},
	{"DAC L1 Mux", "IF2_2 DAC", "IF2_2 DAC L"},
	{"DAC L1 Mux", "IF3 DAC", "IF3 DAC L"},
	{"DAC L1 Mux", NULL, "DAC Stereo1 Filter"},

	{"DAC R1 Mux", "IF1 DAC1", "IF1 DAC1 R"},
	{"DAC R1 Mux", "IF2_1 DAC", "IF2_1 DAC R"},
	{"DAC R1 Mux", "IF2_2 DAC", "IF2_2 DAC R"},
	{"DAC R1 Mux", "IF3 DAC", "IF3 DAC R"},
	{"DAC R1 Mux", NULL, "DAC Stereo1 Filter"},

	{"DAC1 MIXL", "Stereo ADC Switch", "Stereo1 ADC MIXL"},
	{"DAC1 MIXL", "DAC1 Switch", "DAC L1 Mux"},
	{"DAC1 MIXR", "Stereo ADC Switch", "Stereo1 ADC MIXR"},
	{"DAC1 MIXR", "DAC1 Switch", "DAC R1 Mux"},

	{"DAC1 MIX", NULL, "DAC1 MIXL"},
	{"DAC1 MIX", NULL, "DAC1 MIXR"},

	{"DAC L2 Mux", "IF1 DAC2", "IF1 DAC2 L"},
	{"DAC L2 Mux", "IF2_1 DAC", "IF2_1 DAC L"},
	{"DAC L2 Mux", "IF2_2 DAC", "IF2_2 DAC L"},
	{"DAC L2 Mux", "IF3 DAC", "IF3 DAC L"},
	{"DAC L2 Mux", "Mono ADC MIX", "Mono ADC MIXL"},
	{"DAC L2 Mux", NULL, "DAC Mono Left Filter"},

	{"DAC R2 Mux", "IF1 DAC2", "IF1 DAC2 R"},
	{"DAC R2 Mux", "IF2_1 DAC", "IF2_1 DAC R"},
	{"DAC R2 Mux", "IF2_2 DAC", "IF2_2 DAC R"},
	{"DAC R2 Mux", "IF3 DAC", "IF3 DAC R"},
	{"DAC R2 Mux", "Mono ADC MIX", "Mono ADC MIXR"},
	{"DAC R2 Mux", NULL, "DAC Mono Right Filter"},

	{"DAC L3 Mux", "IF1 DAC2", "IF1 DAC2 L"},
	{"DAC L3 Mux", "IF2_1 DAC", "IF2_1 DAC L"},
	{"DAC L3 Mux", "IF2_2 DAC", "IF2_2 DAC L"},
	{"DAC L3 Mux", "IF3 DAC", "IF3 DAC L"},
	{"DAC L3 Mux", "STO2 ADC MIX", "Stereo2 ADC MIXL"},
	{"DAC L3 Mux", NULL, "DAC Stereo2 Filter"},

	{"DAC R3 Mux", "IF1 DAC2", "IF1 DAC2 R"},
	{"DAC R3 Mux", "IF2_1 DAC", "IF2_1 DAC R"},
	{"DAC R3 Mux", "IF2_2 DAC", "IF2_2 DAC R"},
	{"DAC R3 Mux", "IF3 DAC", "IF3 DAC R"},
	{"DAC R3 Mux", "STO2 ADC MIX", "Stereo2 ADC MIXR"},
	{"DAC R3 Mux", NULL, "DAC Stereo2 Filter"},

	{"Stereo1 DAC MIXL", "DAC L1 Switch", "DAC1 MIXL"},
	{"Stereo1 DAC MIXL", "DAC R1 Switch", "DAC1 MIXR"},
	{"Stereo1 DAC MIXL", "DAC L2 Switch", "DAC L2 Mux"},
	{"Stereo1 DAC MIXL", "DAC R2 Switch", "DAC R2 Mux"},

	{"Stereo1 DAC MIXR", "DAC R1 Switch", "DAC1 MIXR"},
	{"Stereo1 DAC MIXR", "DAC L1 Switch", "DAC1 MIXL"},
	{"Stereo1 DAC MIXR", "DAC L2 Switch", "DAC L2 Mux"},
	{"Stereo1 DAC MIXR", "DAC R2 Switch", "DAC R2 Mux"},

	{"Stereo2 DAC MIXL", "DAC L1 Switch", "DAC1 MIXL"},
	{"Stereo2 DAC MIXL", "DAC L2 Switch", "DAC L2 Mux"},
	{"Stereo2 DAC MIXL", "DAC L3 Switch", "DAC L3 Mux"},

	{"Stereo2 DAC MIXR", "DAC R1 Switch", "DAC1 MIXR"},
	{"Stereo2 DAC MIXR", "DAC R2 Switch", "DAC R2 Mux"},
	{"Stereo2 DAC MIXR", "DAC R3 Switch", "DAC R3 Mux"},

	{"Mono DAC MIXL", "DAC L1 Switch", "DAC1 MIXL"},
	{"Mono DAC MIXL", "DAC R1 Switch", "DAC1 MIXR"},
	{"Mono DAC MIXL", "DAC L2 Switch", "DAC L2 Mux"},
	{"Mono DAC MIXL", "DAC R2 Switch", "DAC R2 Mux"},
	{"Mono DAC MIXR", "DAC L1 Switch", "DAC1 MIXL"},
	{"Mono DAC MIXR", "DAC R1 Switch", "DAC1 MIXR"},
	{"Mono DAC MIXR", "DAC L2 Switch", "DAC L2 Mux"},
	{"Mono DAC MIXR", "DAC R2 Switch", "DAC R2 Mux"},

	{"DAC MIXL", "Stereo1 DAC Mixer", "Stereo1 DAC MIXL"},
	{"DAC MIXL", "Stereo2 DAC Mixer", "Stereo2 DAC MIXL"},
	{"DAC MIXL", "Mono DAC Mixer", "Mono DAC MIXL"},
	{"DAC MIXR", "Stereo1 DAC Mixer", "Stereo1 DAC MIXR"},
	{"DAC MIXR", "Stereo2 DAC Mixer", "Stereo2 DAC MIXR"},
	{"DAC MIXR", "Mono DAC Mixer", "Mono DAC MIXR"},

	{"DAC L1 Source", "DAC1", "DAC1 MIXL"},
	{"DAC L1 Source", "Stereo1 DAC Mixer", "Stereo1 DAC MIXL"},
	{"DAC L1 Source", "DMIC1", "DMIC L1"},
	{"DAC R1 Source", "DAC1", "DAC1 MIXR"},
	{"DAC R1 Source", "Stereo1 DAC Mixer", "Stereo1 DAC MIXR"},
	{"DAC R1 Source", "DMIC1", "DMIC R1"},

	{"DAC L2 Source", "DAC2", "DAC L2 Mux"},
	{"DAC L2 Source", "Mono DAC Mixer", "Mono DAC MIXL"},
	{"DAC L2 Source", NULL, "DAC L2 Power"},
	{"DAC R2 Source", "DAC2", "DAC R2 Mux"},
	{"DAC R2 Source", "Mono DAC Mixer", "Mono DAC MIXR"},
	{"DAC R2 Source", NULL, "DAC R2 Power"},

	{"DAC L1", NULL, "DAC L1 Source"},
	{"DAC R1", NULL, "DAC R1 Source"},
	{"DAC L2", NULL, "DAC L2 Source"},
	{"DAC R2", NULL, "DAC R2 Source"},

	{"DAC L1", NULL, "DAC 1 Clock"},
	{"DAC R1", NULL, "DAC 1 Clock"},
	{"DAC L2", NULL, "DAC 2 Clock"},
	{"DAC R2", NULL, "DAC 2 Clock"},

	{"MONOVOL MIX", "DAC L2 Switch", "DAC L2"},
	{"MONOVOL MIX", "RECMIX2L Switch", "RECMIX2L"},
	{"MONOVOL MIX", "BST1 Switch", "BST1"},
	{"MONOVOL MIX", "BST2 Switch", "BST2"},
	{"MONOVOL MIX", "BST3 Switch", "BST3"},

	{"OUT MIXL", "DAC L2 Switch", "DAC L2"},
	{"OUT MIXL", "INL Switch", "INL VOL"},
	{"OUT MIXL", "BST1 Switch", "BST1"},
	{"OUT MIXL", "BST2 Switch", "BST2"},
	{"OUT MIXL", "BST3 Switch", "BST3"},
	{"OUT MIXR", "DAC R2 Switch", "DAC R2"},
	{"OUT MIXR", "INR Switch", "INR VOL"},
	{"OUT MIXR", "BST2 Switch", "BST2"},
	{"OUT MIXR", "BST3 Switch", "BST3"},
	{"OUT MIXR", "BST4 Switch", "BST4"},

	{"MONOVOL", "Switch", "MONOVOL MIX"},
	{"Mono MIX", "DAC L2 Switch", "DAC L2"},
	{"Mono MIX", "MONOVOL Switch", "MONOVOL"},
	{"Mono Amp", NULL, "Mono MIX"},
	{"Mono Amp", NULL, "Vref2"},
	{"Mono Amp", NULL, "Vref3"},
	{"Mono Amp", NULL, "CLKDET SYS"},
	{"Mono Amp", NULL, "CLKDET MONO"},
	{"Mono Playback", "Switch", "Mono Amp"},
	{"MONOOUT", NULL, "Mono Playback"},

	{"HP Amp", NULL, "DAC L1"},
	{"HP Amp", NULL, "DAC R1"},
	{"HP Amp", NULL, "Charge Pump"},
	{"HP Amp", NULL, "CLKDET SYS"},
	{"HP Amp", NULL, "CLKDET HP"},
	{"HP Amp", NULL, "CBJ Power"},
	{"HP Amp", NULL, "Vref2"},
	{"HPO Playback", "Switch", "HP Amp"},
	{"HPOL", NULL, "HPO Playback"},
	{"HPOR", NULL, "HPO Playback"},

	{"OUTVOL L", "Switch", "OUT MIXL"},
	{"OUTVOL R", "Switch", "OUT MIXR"},
	{"LOUT L MIX", "DAC L2 Switch", "DAC L2"},
	{"LOUT L MIX", "OUTVOL L Switch", "OUTVOL L"},
	{"LOUT R MIX", "DAC R2 Switch", "DAC R2"},
	{"LOUT R MIX", "OUTVOL R Switch", "OUTVOL R"},
	{"LOUT Amp", NULL, "LOUT L MIX"},
	{"LOUT Amp", NULL, "LOUT R MIX"},
	{"LOUT Amp", NULL, "Vref1"},
	{"LOUT Amp", NULL, "Vref2"},
	{"LOUT Amp", NULL, "CLKDET SYS"},
	{"LOUT Amp", NULL, "CLKDET LOUT"},
	{"LOUT L Playback", "Switch", "LOUT Amp"},
	{"LOUT R Playback", "Switch", "LOUT Amp"},
	{"LOUTL", NULL, "LOUT L Playback"},
	{"LOUTR", NULL, "LOUT R Playback"},

	{"PDM L Mux", "Mono DAC", "Mono DAC MIXL"},
	{"PDM L Mux", "Stereo1 DAC", "Stereo1 DAC MIXL"},
	{"PDM L Mux", "Stereo2 DAC", "Stereo2 DAC MIXL"},
	{"PDM L Mux", NULL, "PDM Power"},
	{"PDM R Mux", "Mono DAC", "Mono DAC MIXR"},
	{"PDM R Mux", "Stereo1 DAC", "Stereo1 DAC MIXR"},
	{"PDM R Mux", "Stereo2 DAC", "Stereo2 DAC MIXR"},
	{"PDM R Mux", NULL, "PDM Power"},
	{"PDM L Playback", "Switch", "PDM L Mux"},
	{"PDM R Playback", "Switch", "PDM R Mux"},
	{"PDML", NULL, "PDM L Playback"},
	{"PDMR", NULL, "PDM R Playback"},
};

static int rt5665_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int val = 0;

	if (rx_mask || tx_mask)
		val |= RT5665_I2S1_MODE_TDM;

	switch (slots) {
	case 4:
		val |= RT5665_TDM_IN_CH_4;
		val |= RT5665_TDM_OUT_CH_4;
		break;
	case 6:
		val |= RT5665_TDM_IN_CH_6;
		val |= RT5665_TDM_OUT_CH_6;
		break;
	case 8:
		val |= RT5665_TDM_IN_CH_8;
		val |= RT5665_TDM_OUT_CH_8;
		break;
	case 2:
		break;
	default:
		return -EINVAL;
	}

	switch (slot_width) {
	case 20:
		val |= RT5665_TDM_IN_LEN_20;
		val |= RT5665_TDM_OUT_LEN_20;
		break;
	case 24:
		val |= RT5665_TDM_IN_LEN_24;
		val |= RT5665_TDM_OUT_LEN_24;
		break;
	case 32:
		val |= RT5665_TDM_IN_LEN_32;
		val |= RT5665_TDM_OUT_LEN_32;
		break;
	case 16:
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, RT5665_TDM_CTRL_1,
		RT5665_I2S1_MODE_MASK | RT5665_TDM_IN_CH_MASK |
		RT5665_TDM_OUT_CH_MASK | RT5665_TDM_IN_LEN_MASK |
		RT5665_TDM_OUT_LEN_MASK, val);

	return 0;
}


static int rt5665_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0, val_clk, reg_clk, mask_clk, val_bits = 0x0100;
	int pre_div, frame_size;

	rt5665->lrck[dai->id] = params_rate(params);
	pre_div = rl6231_get_clk_info(rt5665->sysclk, rt5665->lrck[dai->id]);
	if (pre_div < 0) {
		dev_warn(codec->dev, "Force using PLL");
		snd_soc_codec_set_pll(codec, 0, RT5665_PLL1_S_MCLK,
			rt5665->sysclk,	rt5665->lrck[dai->id] * 512);
		snd_soc_codec_set_sysclk(codec, RT5665_SCLK_S_PLL1, 0,
			rt5665->lrck[dai->id] * 512, 0);
		pre_div = 1;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(codec->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}

	dev_dbg(dai->dev, "lrck is %dHz and pre_div is %d for iis %d\n",
				rt5665->lrck[dai->id], pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		val_bits = 0x0100;
		break;
	case 20:
		val_len |= RT5665_I2S_DL_20;
		val_bits = 0x1300;
		break;
	case 24:
		val_len |= RT5665_I2S_DL_24;
		val_bits = 0x2500;
		break;
	case 8:
		val_len |= RT5665_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5665_AIF1_1:
	case RT5665_AIF1_2:
		if (params_channels(params) > 2)
			rt5665_set_tdm_slot(dai, 0xf, 0xf,
				params_channels(params), params_width(params));
		reg_clk = RT5665_ADDA_CLK_1;
		mask_clk = RT5665_I2S_PD1_MASK;
		val_clk = pre_div << RT5665_I2S_PD1_SFT;
		snd_soc_update_bits(codec, RT5665_I2S1_SDP,
			RT5665_I2S_DL_MASK, val_len);
		break;
	case RT5665_AIF2_1:
	case RT5665_AIF2_2:
		reg_clk = RT5665_ADDA_CLK_2;
		mask_clk = RT5665_I2S_PD2_MASK;
		val_clk = pre_div << RT5665_I2S_PD2_SFT;
		snd_soc_update_bits(codec, RT5665_I2S2_SDP,
			RT5665_I2S_DL_MASK, val_len);
		break;
	case RT5665_AIF3:
		reg_clk = RT5665_ADDA_CLK_2;
		mask_clk = RT5665_I2S_PD3_MASK;
		val_clk = pre_div << RT5665_I2S_PD3_SFT;
		snd_soc_update_bits(codec, RT5665_I2S3_SDP,
			RT5665_I2S_DL_MASK, val_len);
		break;
	default:
		dev_err(codec->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, reg_clk, mask_clk, val_clk);
	snd_soc_update_bits(codec, RT5665_STO1_DAC_SIL_DET, 0x3700, val_bits);

	switch (rt5665->lrck[dai->id]) {
	case 192000:
		snd_soc_update_bits(codec, RT5665_ADDA_CLK_1,
			RT5665_DAC_OSR_MASK | RT5665_ADC_OSR_MASK,
			RT5665_DAC_OSR_32 | RT5665_ADC_OSR_32);
		break;
	case 96000:
		snd_soc_update_bits(codec, RT5665_ADDA_CLK_1,
			RT5665_DAC_OSR_MASK | RT5665_ADC_OSR_MASK,
			RT5665_DAC_OSR_64 | RT5665_ADC_OSR_64);
		break;
	default:
		snd_soc_update_bits(codec, RT5665_ADDA_CLK_1,
			RT5665_DAC_OSR_MASK | RT5665_ADC_OSR_MASK,
			RT5665_DAC_OSR_128 | RT5665_ADC_OSR_128);
		break;
	}

	if (rt5665->master[RT5665_AIF2_1] || rt5665->master[RT5665_AIF2_2]) {
		snd_soc_update_bits(codec, RT5665_I2S_M_CLK_CTRL_1,
			RT5665_I2S2_M_PD_MASK, pre_div << RT5665_I2S2_M_PD_SFT);
	}
	if (rt5665->master[RT5665_AIF3]) {
		snd_soc_update_bits(codec, RT5665_I2S_M_CLK_CTRL_1,
			RT5665_I2S3_M_PD_MASK, pre_div << RT5665_I2S3_M_PD_SFT);
	}

	return 0;
}

static int rt5665_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5665->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5665_I2S_MS_S;
		rt5665->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5665_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5665_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5665_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5665_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5665_AIF1_1:
	case RT5665_AIF1_2:
		snd_soc_update_bits(codec, RT5665_I2S1_SDP,
			RT5665_I2S_MS_MASK | RT5665_I2S_BP_MASK |
			RT5665_I2S_DF_MASK, reg_val);
		break;
	case RT5665_AIF2_1:
	case RT5665_AIF2_2:
		snd_soc_update_bits(codec, RT5665_I2S2_SDP,
			RT5665_I2S_MS_MASK | RT5665_I2S_BP_MASK |
			RT5665_I2S_DF_MASK, reg_val);
		break;
	case RT5665_AIF3:
		snd_soc_update_bits(codec, RT5665_I2S3_SDP,
			RT5665_I2S_MS_MASK | RT5665_I2S_BP_MASK |
			RT5665_I2S_DF_MASK, reg_val);
		break;
	default:
		dev_err(codec->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}
	return 0;
}

static int rt5665_set_codec_sysclk(struct snd_soc_codec *codec, int clk_id,
				   int source, unsigned int freq, int dir)
{
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0, src = 0;

	if (freq == rt5665->sysclk && clk_id == rt5665->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5665_SCLK_S_MCLK:
		reg_val |= RT5665_SCLK_SRC_MCLK;
		src = RT5665_CLK_SRC_MCLK;
		break;
	case RT5665_SCLK_S_PLL1:
		reg_val |= RT5665_SCLK_SRC_PLL1;
		src = RT5665_CLK_SRC_PLL1;
		break;
	case RT5665_SCLK_S_RCCLK:
		reg_val |= RT5665_SCLK_SRC_RCCLK;
		src = RT5665_CLK_SRC_RCCLK;
		break;
	default:
		dev_err(codec->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_update_bits(codec, RT5665_GLB_CLK,
		RT5665_SCLK_SRC_MASK, reg_val);

	if (rt5665->master[RT5665_AIF2_1] || rt5665->master[RT5665_AIF2_2]) {
		snd_soc_update_bits(codec, RT5665_I2S_M_CLK_CTRL_1,
			RT5665_I2S2_SRC_MASK, src << RT5665_I2S2_SRC_SFT);
	}
	if (rt5665->master[RT5665_AIF3]) {
		snd_soc_update_bits(codec, RT5665_I2S_M_CLK_CTRL_1,
			RT5665_I2S3_SRC_MASK, src << RT5665_I2S3_SRC_SFT);
	}

	rt5665->sysclk = freq;
	rt5665->sysclk_src = clk_id;

	dev_dbg(codec->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);

	return 0;
}

static int rt5665_set_codec_pll(struct snd_soc_codec *codec, int pll_id,
				int source, unsigned int freq_in,
				unsigned int freq_out)
{
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt5665->pll_src && freq_in == rt5665->pll_in &&
	    freq_out == rt5665->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(codec->dev, "PLL disabled\n");

		rt5665->pll_in = 0;
		rt5665->pll_out = 0;
		snd_soc_update_bits(codec, RT5665_GLB_CLK,
			RT5665_SCLK_SRC_MASK, RT5665_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT5665_PLL1_S_MCLK:
		snd_soc_update_bits(codec, RT5665_GLB_CLK,
			RT5665_PLL1_SRC_MASK, RT5665_PLL1_SRC_MCLK);
		break;
	case RT5665_PLL1_S_BCLK1:
		snd_soc_update_bits(codec, RT5665_GLB_CLK,
				RT5665_PLL1_SRC_MASK, RT5665_PLL1_SRC_BCLK1);
		break;
	case RT5665_PLL1_S_BCLK2:
		snd_soc_update_bits(codec, RT5665_GLB_CLK,
				RT5665_PLL1_SRC_MASK, RT5665_PLL1_SRC_BCLK2);
		break;
	case RT5665_PLL1_S_BCLK3:
		snd_soc_update_bits(codec, RT5665_GLB_CLK,
				RT5665_PLL1_SRC_MASK, RT5665_PLL1_SRC_BCLK3);
		break;
	default:
		dev_err(codec->dev, "Unknown PLL Source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(codec->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(codec->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_write(codec, RT5665_PLL_CTRL_1,
		pll_code.n_code << RT5665_PLL_N_SFT | pll_code.k_code);
	snd_soc_write(codec, RT5665_PLL_CTRL_2,
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT5665_PLL_M_SFT |
		pll_code.m_bp << RT5665_PLL_M_BP_SFT);

	rt5665->pll_in = freq_in;
	rt5665->pll_out = freq_out;
	rt5665->pll_src = source;

	return 0;
}

static int rt5665_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s ratio=%d\n", __func__, ratio);

	rt5665->bclk[dai->id] = ratio;

	if (ratio == 64) {
		switch (dai->id) {
		case RT5665_AIF2_1:
		case RT5665_AIF2_2:
			snd_soc_update_bits(codec, RT5665_ADDA_CLK_1,
				RT5665_I2S_BCLK_MS2_MASK,
				RT5665_I2S_BCLK_MS2_64);
			break;
		case RT5665_AIF3:
			snd_soc_update_bits(codec, RT5665_ADDA_CLK_1,
				RT5665_I2S_BCLK_MS3_MASK,
				RT5665_I2S_BCLK_MS3_64);
			break;
		}
	}

	return 0;
}

static int rt5665_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		regmap_update_bits(rt5665->regmap, RT5665_DIG_MISC,
			RT5665_DIG_GATE_CTRL, RT5665_DIG_GATE_CTRL);
		break;

	case SND_SOC_BIAS_STANDBY:
		regmap_update_bits(rt5665->regmap, RT5665_PWR_DIG_1,
			RT5665_PWR_LDO,	RT5665_PWR_LDO);
		regmap_update_bits(rt5665->regmap, RT5665_PWR_ANLG_1,
			RT5665_PWR_MB, RT5665_PWR_MB);
		regmap_update_bits(rt5665->regmap, RT5665_DIG_MISC,
			RT5665_DIG_GATE_CTRL, 0);
		break;
	case SND_SOC_BIAS_OFF:
		regmap_update_bits(rt5665->regmap, RT5665_PWR_DIG_1,
			RT5665_PWR_LDO, 0);
		regmap_update_bits(rt5665->regmap, RT5665_PWR_ANLG_1,
			RT5665_PWR_MB, 0);
		break;

	default:
		break;
	}

	return 0;
}

static int rt5665_probe(struct snd_soc_codec *codec)
{
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);

	rt5665->codec = codec;

	schedule_delayed_work(&rt5665->calibrate_work, msecs_to_jiffies(100));

	return 0;
}

static int rt5665_remove(struct snd_soc_codec *codec)
{
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);

	regmap_write(rt5665->regmap, RT5665_RESET, 0);

	return 0;
}

#ifdef CONFIG_PM
static int rt5665_suspend(struct snd_soc_codec *codec)
{
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt5665->regmap, true);
	regcache_mark_dirty(rt5665->regmap);
	return 0;
}

static int rt5665_resume(struct snd_soc_codec *codec)
{
	struct rt5665_priv *rt5665 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt5665->regmap, false);
	regcache_sync(rt5665->regmap);

	return 0;
}
#else
#define rt5665_suspend NULL
#define rt5665_resume NULL
#endif

#define RT5665_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT5665_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt5665_aif_dai_ops = {
	.hw_params = rt5665_hw_params,
	.set_fmt = rt5665_set_dai_fmt,
	.set_tdm_slot = rt5665_set_tdm_slot,
	.set_bclk_ratio = rt5665_set_bclk_ratio,
};

static struct snd_soc_dai_driver rt5665_dai[] = {
	{
		.name = "rt5665-aif1_1",
		.id = RT5665_AIF1_1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5665_STEREO_RATES,
			.formats = RT5665_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1_1 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5665_STEREO_RATES,
			.formats = RT5665_FORMATS,
		},
		.ops = &rt5665_aif_dai_ops,
	},
	{
		.name = "rt5665-aif1_2",
		.id = RT5665_AIF1_2,
		.capture = {
			.stream_name = "AIF1_2 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5665_STEREO_RATES,
			.formats = RT5665_FORMATS,
		},
		.ops = &rt5665_aif_dai_ops,
	},
	{
		.name = "rt5665-aif2_1",
		.id = RT5665_AIF2_1,
		.playback = {
			.stream_name = "AIF2_1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5665_STEREO_RATES,
			.formats = RT5665_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2_1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5665_STEREO_RATES,
			.formats = RT5665_FORMATS,
		},
		.ops = &rt5665_aif_dai_ops,
	},
	{
		.name = "rt5665-aif2_2",
		.id = RT5665_AIF2_2,
		.playback = {
			.stream_name = "AIF2_2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5665_STEREO_RATES,
			.formats = RT5665_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2_2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5665_STEREO_RATES,
			.formats = RT5665_FORMATS,
		},
		.ops = &rt5665_aif_dai_ops,
	},
	{
		.name = "rt5665-aif3",
		.id = RT5665_AIF3,
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5665_STEREO_RATES,
			.formats = RT5665_FORMATS,
		},
		.capture = {
			.stream_name = "AIF3 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5665_STEREO_RATES,
			.formats = RT5665_FORMATS,
		},
		.ops = &rt5665_aif_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt5665 = {
	.probe = rt5665_probe,
	.remove = rt5665_remove,
	.suspend = rt5665_suspend,
	.resume = rt5665_resume,
	.set_bias_level = rt5665_set_bias_level,
	.idle_bias_off = true,
	.component_driver = {
		.controls = rt5665_snd_controls,
		.num_controls = ARRAY_SIZE(rt5665_snd_controls),
		.dapm_widgets = rt5665_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(rt5665_dapm_widgets),
		.dapm_routes = rt5665_dapm_routes,
		.num_dapm_routes = ARRAY_SIZE(rt5665_dapm_routes),
	},
	.set_sysclk = rt5665_set_codec_sysclk,
	.set_pll = rt5665_set_codec_pll,
	.set_jack = rt5665_set_jack_detect,
};


static const struct regmap_config rt5665_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.max_register = 0x0400,
	.volatile_reg = rt5665_volatile_register,
	.readable_reg = rt5665_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5665_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5665_reg),
	.use_single_rw = true,
};

static const struct i2c_device_id rt5665_i2c_id[] = {
	{"rt5665", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rt5665_i2c_id);

static int rt5665_parse_dt(struct rt5665_priv *rt5665, struct device *dev)
{
	rt5665->pdata.in1_diff = of_property_read_bool(dev->of_node,
					"realtek,in1-differential");
	rt5665->pdata.in2_diff = of_property_read_bool(dev->of_node,
					"realtek,in2-differential");
	rt5665->pdata.in3_diff = of_property_read_bool(dev->of_node,
					"realtek,in3-differential");
	rt5665->pdata.in4_diff = of_property_read_bool(dev->of_node,
					"realtek,in4-differential");

	of_property_read_u32(dev->of_node, "realtek,dmic1-data-pin",
		&rt5665->pdata.dmic1_data_pin);
	of_property_read_u32(dev->of_node, "realtek,dmic2-data-pin",
		&rt5665->pdata.dmic2_data_pin);
	of_property_read_u32(dev->of_node, "realtek,jd-src",
		&rt5665->pdata.jd_src);

	rt5665->pdata.ldo1_en = of_get_named_gpio(dev->of_node,
		"realtek,ldo1-en-gpios", 0);

	return 0;
}

static void rt5665_calibrate(struct rt5665_priv *rt5665)
{
	int value, count;

	mutex_lock(&rt5665->calibrate_mutex);

	regcache_cache_bypass(rt5665->regmap, true);

	regmap_write(rt5665->regmap, RT5665_RESET, 0);
	regmap_write(rt5665->regmap, RT5665_BIAS_CUR_CTRL_8, 0xa602);
	regmap_write(rt5665->regmap, RT5665_HP_CHARGE_PUMP_1, 0x0c26);
	regmap_write(rt5665->regmap, RT5665_MONOMIX_IN_GAIN, 0x021f);
	regmap_write(rt5665->regmap, RT5665_MONO_OUT, 0x480a);
	regmap_write(rt5665->regmap, RT5665_PWR_MIXER, 0x083f);
	regmap_write(rt5665->regmap, RT5665_PWR_DIG_1, 0x0180);
	regmap_write(rt5665->regmap, RT5665_EJD_CTRL_1, 0x4040);
	regmap_write(rt5665->regmap, RT5665_HP_LOGIC_CTRL_2, 0x0000);
	regmap_write(rt5665->regmap, RT5665_DIG_MISC, 0x0001);
	regmap_write(rt5665->regmap, RT5665_MICBIAS_2, 0x0380);
	regmap_write(rt5665->regmap, RT5665_GLB_CLK, 0x8000);
	regmap_write(rt5665->regmap, RT5665_ADDA_CLK_1, 0x1000);
	regmap_write(rt5665->regmap, RT5665_CHOP_DAC, 0x3030);
	regmap_write(rt5665->regmap, RT5665_CALIB_ADC_CTRL, 0x3c05);
	regmap_write(rt5665->regmap, RT5665_PWR_ANLG_1, 0xaa3e);
	usleep_range(15000, 20000);
	regmap_write(rt5665->regmap, RT5665_PWR_ANLG_1, 0xfe7e);
	regmap_write(rt5665->regmap, RT5665_HP_CALIB_CTRL_2, 0x0321);

	regmap_write(rt5665->regmap, RT5665_HP_CALIB_CTRL_1, 0xfc00);
	count = 0;
	while (true) {
		regmap_read(rt5665->regmap, RT5665_HP_CALIB_STA_1, &value);
		if (value & 0x8000)
			usleep_range(10000, 10005);
		else
			break;

		if (count > 60) {
			pr_err("HP Calibration Failure\n");
			regmap_write(rt5665->regmap, RT5665_RESET, 0);
			regcache_cache_bypass(rt5665->regmap, false);
			goto out_unlock;
		}

		count++;
	}

	regmap_write(rt5665->regmap, RT5665_MONO_AMP_CALIB_CTRL_1, 0x9e24);
	count = 0;
	while (true) {
		regmap_read(rt5665->regmap, RT5665_MONO_AMP_CALIB_STA1, &value);
		if (value & 0x8000)
			usleep_range(10000, 10005);
		else
			break;

		if (count > 60) {
			pr_err("MONO Calibration Failure\n");
			regmap_write(rt5665->regmap, RT5665_RESET, 0);
			regcache_cache_bypass(rt5665->regmap, false);
			goto out_unlock;
		}

		count++;
	}

	regmap_write(rt5665->regmap, RT5665_RESET, 0);
	regcache_cache_bypass(rt5665->regmap, false);

	regcache_mark_dirty(rt5665->regmap);
	regcache_sync(rt5665->regmap);

	regmap_write(rt5665->regmap, RT5665_BIAS_CUR_CTRL_8, 0xa602);
	regmap_write(rt5665->regmap, RT5665_ASRC_8, 0x0120);

out_unlock:
	rt5665->calibration_done = true;
	mutex_unlock(&rt5665->calibrate_mutex);
}

static void rt5665_calibrate_handler(struct work_struct *work)
{
	struct rt5665_priv *rt5665 = container_of(work, struct rt5665_priv,
		calibrate_work.work);

	while (!rt5665->codec->component.card->instantiated) {
		pr_debug("%s\n", __func__);
		usleep_range(10000, 15000);
	}

	rt5665_calibrate(rt5665);
}

static int rt5665_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5665_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt5665_priv *rt5665;
	int i, ret;
	unsigned int val;

	rt5665 = devm_kzalloc(&i2c->dev, sizeof(struct rt5665_priv),
		GFP_KERNEL);

	if (rt5665 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5665);

	if (pdata)
		rt5665->pdata = *pdata;
	else
		rt5665_parse_dt(rt5665, &i2c->dev);

	for (i = 0; i < ARRAY_SIZE(rt5665->supplies); i++)
		rt5665->supplies[i].supply = rt5665_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(rt5665->supplies),
				      rt5665->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(rt5665->supplies),
				    rt5665->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	if (gpio_is_valid(rt5665->pdata.ldo1_en)) {
		if (devm_gpio_request_one(&i2c->dev, rt5665->pdata.ldo1_en,
					  GPIOF_OUT_INIT_HIGH, "rt5665"))
			dev_err(&i2c->dev, "Fail gpio_request gpio_ldo\n");
	}

	/* Sleep for 300 ms miniumum */
	usleep_range(300000, 350000);

	rt5665->regmap = devm_regmap_init_i2c(i2c, &rt5665_regmap);
	if (IS_ERR(rt5665->regmap)) {
		ret = PTR_ERR(rt5665->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt5665->regmap, RT5665_DEVICE_ID, &val);
	if (val != DEVICE_ID) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt5665\n", val);
		return -ENODEV;
	}

	regmap_read(rt5665->regmap, RT5665_RESET, &val);
	switch (val) {
	case 0x0:
		rt5665->id = CODEC_5666;
		break;
	case 0x6:
		rt5665->id = CODEC_5668;
		break;
	case 0x3:
	default:
		rt5665->id = CODEC_5665;
		break;
	}

	regmap_write(rt5665->regmap, RT5665_RESET, 0);

	/* line in diff mode*/
	if (rt5665->pdata.in1_diff)
		regmap_update_bits(rt5665->regmap, RT5665_IN1_IN2,
			RT5665_IN1_DF_MASK, RT5665_IN1_DF_MASK);
	if (rt5665->pdata.in2_diff)
		regmap_update_bits(rt5665->regmap, RT5665_IN1_IN2,
			RT5665_IN2_DF_MASK, RT5665_IN2_DF_MASK);
	if (rt5665->pdata.in3_diff)
		regmap_update_bits(rt5665->regmap, RT5665_IN3_IN4,
			RT5665_IN3_DF_MASK, RT5665_IN3_DF_MASK);
	if (rt5665->pdata.in4_diff)
		regmap_update_bits(rt5665->regmap, RT5665_IN3_IN4,
			RT5665_IN4_DF_MASK, RT5665_IN4_DF_MASK);

	/* DMIC pin*/
	if (rt5665->pdata.dmic1_data_pin != RT5665_DMIC1_NULL ||
		rt5665->pdata.dmic2_data_pin != RT5665_DMIC2_NULL) {
		regmap_update_bits(rt5665->regmap, RT5665_GPIO_CTRL_2,
			RT5665_GP9_PIN_MASK, RT5665_GP9_PIN_DMIC1_SCL);
		regmap_update_bits(rt5665->regmap, RT5665_GPIO_CTRL_1,
				RT5665_GP8_PIN_MASK, RT5665_GP8_PIN_DMIC2_SCL);
		switch (rt5665->pdata.dmic1_data_pin) {
		case RT5665_DMIC1_DATA_IN2N:
			regmap_update_bits(rt5665->regmap, RT5665_DMIC_CTRL_1,
				RT5665_DMIC_1_DP_MASK, RT5665_DMIC_1_DP_IN2N);
			break;

		case RT5665_DMIC1_DATA_GPIO4:
			regmap_update_bits(rt5665->regmap, RT5665_DMIC_CTRL_1,
				RT5665_DMIC_1_DP_MASK, RT5665_DMIC_1_DP_GPIO4);
			regmap_update_bits(rt5665->regmap, RT5665_GPIO_CTRL_1,
				RT5665_GP4_PIN_MASK, RT5665_GP4_PIN_DMIC1_SDA);
			break;

		default:
			dev_dbg(&i2c->dev, "no DMIC1\n");
			break;
		}

		switch (rt5665->pdata.dmic2_data_pin) {
		case RT5665_DMIC2_DATA_IN2P:
			regmap_update_bits(rt5665->regmap, RT5665_DMIC_CTRL_1,
				RT5665_DMIC_2_DP_MASK, RT5665_DMIC_2_DP_IN2P);
			break;

		case RT5665_DMIC2_DATA_GPIO5:
			regmap_update_bits(rt5665->regmap,
				RT5665_DMIC_CTRL_1,
				RT5665_DMIC_2_DP_MASK,
				RT5665_DMIC_2_DP_GPIO5);
			regmap_update_bits(rt5665->regmap, RT5665_GPIO_CTRL_1,
				RT5665_GP5_PIN_MASK, RT5665_GP5_PIN_DMIC2_SDA);
			break;

		default:
			dev_dbg(&i2c->dev, "no DMIC2\n");
			break;

		}
	}

	regmap_write(rt5665->regmap, RT5665_HP_LOGIC_CTRL_2, 0x0002);
	regmap_update_bits(rt5665->regmap, RT5665_EJD_CTRL_1,
		0xf000 | RT5665_VREF_POW_MASK, 0xe000 | RT5665_VREF_POW_REG);
	/* Work around for pow_pump */
	regmap_update_bits(rt5665->regmap, RT5665_STO1_DAC_SIL_DET,
		RT5665_DEB_STO_DAC_MASK, RT5665_DEB_80_MS);

	regmap_update_bits(rt5665->regmap, RT5665_HP_CHARGE_PUMP_1,
		RT5665_PM_HP_MASK, RT5665_PM_HP_HV);

	/* Set GPIO4,8 as input for combo jack */
	if (rt5665->id == CODEC_5666) {
		regmap_update_bits(rt5665->regmap, RT5665_GPIO_CTRL_2,
			RT5665_GP4_PF_MASK, RT5665_GP4_PF_IN);
		regmap_update_bits(rt5665->regmap, RT5665_GPIO_CTRL_3,
			RT5665_GP8_PF_MASK, RT5665_GP8_PF_IN);
	}

	/* Enhance performance*/
	regmap_update_bits(rt5665->regmap, RT5665_PWR_ANLG_1,
		RT5665_HP_DRIVER_MASK | RT5665_LDO1_DVO_MASK,
		RT5665_HP_DRIVER_5X | RT5665_LDO1_DVO_12);

	INIT_DELAYED_WORK(&rt5665->jack_detect_work,
				rt5665_jack_detect_handler);
	INIT_DELAYED_WORK(&rt5665->calibrate_work,
				rt5665_calibrate_handler);
	INIT_DELAYED_WORK(&rt5665->jd_check_work,
				rt5665_jd_check_handler);

	mutex_init(&rt5665->calibrate_mutex);

	if (i2c->irq) {
		ret = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL,
			rt5665_irq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT, "rt5665", rt5665);
		if (ret)
			dev_err(&i2c->dev, "Failed to reguest IRQ: %d\n", ret);

	}

	return snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt5665,
			rt5665_dai, ARRAY_SIZE(rt5665_dai));
}

static int rt5665_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);

	return 0;
}

static void rt5665_i2c_shutdown(struct i2c_client *client)
{
	struct rt5665_priv *rt5665 = i2c_get_clientdata(client);

	regmap_write(rt5665->regmap, RT5665_RESET, 0);
}

#ifdef CONFIG_OF
static const struct of_device_id rt5665_of_match[] = {
	{.compatible = "realtek,rt5665"},
	{.compatible = "realtek,rt5666"},
	{.compatible = "realtek,rt5668"},
	{},
};
MODULE_DEVICE_TABLE(of, rt5665_of_match);
#endif

#ifdef CONFIG_ACPI
static struct acpi_device_id rt5665_acpi_match[] = {
	{"10EC5665", 0,},
	{"10EC5666", 0,},
	{"10EC5668", 0,},
	{},
};
MODULE_DEVICE_TABLE(acpi, rt5665_acpi_match);
#endif

static struct i2c_driver rt5665_i2c_driver = {
	.driver = {
		.name = "rt5665",
		.of_match_table = of_match_ptr(rt5665_of_match),
		.acpi_match_table = ACPI_PTR(rt5665_acpi_match),
	},
	.probe = rt5665_i2c_probe,
	.remove = rt5665_i2c_remove,
	.shutdown = rt5665_i2c_shutdown,
	.id_table = rt5665_i2c_id,
};
module_i2c_driver(rt5665_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5665 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL v2");
