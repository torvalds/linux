// SPDX-License-Identifier: GPL-2.0-only
//
// rt5682.c  --  RT5682 ALSA SoC audio component driver
//
// Copyright 2018 Realtek Semiconductor Corp.
// Author: Bard Liao <bardliao@realtek.com>
//

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/rt5682.h>

#include "rl6231.h"
#include "rt5682.h"

const char *rt5682_supply_names[RT5682_NUM_SUPPLIES] = {
	"AVDD",
	"MICVDD",
	"VBAT",
};
EXPORT_SYMBOL_GPL(rt5682_supply_names);

static const struct reg_sequence patch_list[] = {
	{RT5682_HP_IMP_SENS_CTRL_19, 0x1000},
	{RT5682_DAC_ADC_DIG_VOL1, 0xa020},
	{RT5682_I2C_CTRL, 0x000f},
	{RT5682_PLL2_INTERNAL, 0x8266},
	{RT5682_SAR_IL_CMD_1, 0x22b7},
	{RT5682_SAR_IL_CMD_3, 0x0365},
	{RT5682_SAR_IL_CMD_6, 0x0110},
	{RT5682_CHARGE_PUMP_1, 0x0210},
	{RT5682_HP_LOGIC_CTRL_2, 0x0007},
};

void rt5682_apply_patch_list(struct rt5682_priv *rt5682, struct device *dev)
{
	int ret;

	ret = regmap_multi_reg_write(rt5682->regmap, patch_list,
				     ARRAY_SIZE(patch_list));
	if (ret)
		dev_warn(dev, "Failed to apply regmap patch: %d\n", ret);
}
EXPORT_SYMBOL_GPL(rt5682_apply_patch_list);

const struct reg_default rt5682_reg[RT5682_REG_NUM] = {
	{0x0002, 0x8080},
	{0x0003, 0x8000},
	{0x0005, 0x0000},
	{0x0006, 0x0000},
	{0x0008, 0x800f},
	{0x000b, 0x0000},
	{0x0010, 0x4040},
	{0x0011, 0x0000},
	{0x0012, 0x1404},
	{0x0013, 0x1000},
	{0x0014, 0xa00a},
	{0x0015, 0x0404},
	{0x0016, 0x0404},
	{0x0019, 0xafaf},
	{0x001c, 0x2f2f},
	{0x001f, 0x0000},
	{0x0022, 0x5757},
	{0x0023, 0x0039},
	{0x0024, 0x000b},
	{0x0026, 0xc0c4},
	{0x0029, 0x8080},
	{0x002a, 0xa0a0},
	{0x002b, 0x0300},
	{0x0030, 0x0000},
	{0x003c, 0x0080},
	{0x0044, 0x0c0c},
	{0x0049, 0x0000},
	{0x0061, 0x0000},
	{0x0062, 0x0000},
	{0x0063, 0x003f},
	{0x0064, 0x0000},
	{0x0065, 0x0000},
	{0x0066, 0x0030},
	{0x0067, 0x0000},
	{0x006b, 0x0000},
	{0x006c, 0x0000},
	{0x006d, 0x2200},
	{0x006e, 0x0a10},
	{0x0070, 0x8000},
	{0x0071, 0x8000},
	{0x0073, 0x0000},
	{0x0074, 0x0000},
	{0x0075, 0x0002},
	{0x0076, 0x0001},
	{0x0079, 0x0000},
	{0x007a, 0x0000},
	{0x007b, 0x0000},
	{0x007c, 0x0100},
	{0x007e, 0x0000},
	{0x0080, 0x0000},
	{0x0081, 0x0000},
	{0x0082, 0x0000},
	{0x0083, 0x0000},
	{0x0084, 0x0000},
	{0x0085, 0x0000},
	{0x0086, 0x0005},
	{0x0087, 0x0000},
	{0x0088, 0x0000},
	{0x008c, 0x0003},
	{0x008d, 0x0000},
	{0x008e, 0x0060},
	{0x008f, 0x1000},
	{0x0091, 0x0c26},
	{0x0092, 0x0073},
	{0x0093, 0x0000},
	{0x0094, 0x0080},
	{0x0098, 0x0000},
	{0x009a, 0x0000},
	{0x009b, 0x0000},
	{0x009c, 0x0000},
	{0x009d, 0x0000},
	{0x009e, 0x100c},
	{0x009f, 0x0000},
	{0x00a0, 0x0000},
	{0x00a3, 0x0002},
	{0x00a4, 0x0001},
	{0x00ae, 0x2040},
	{0x00af, 0x0000},
	{0x00b6, 0x0000},
	{0x00b7, 0x0000},
	{0x00b8, 0x0000},
	{0x00b9, 0x0002},
	{0x00be, 0x0000},
	{0x00c0, 0x0160},
	{0x00c1, 0x82a0},
	{0x00c2, 0x0000},
	{0x00d0, 0x0000},
	{0x00d1, 0x2244},
	{0x00d2, 0x3300},
	{0x00d3, 0x2200},
	{0x00d4, 0x0000},
	{0x00d9, 0x0009},
	{0x00da, 0x0000},
	{0x00db, 0x0000},
	{0x00dc, 0x00c0},
	{0x00dd, 0x2220},
	{0x00de, 0x3131},
	{0x00df, 0x3131},
	{0x00e0, 0x3131},
	{0x00e2, 0x0000},
	{0x00e3, 0x4000},
	{0x00e4, 0x0aa0},
	{0x00e5, 0x3131},
	{0x00e6, 0x3131},
	{0x00e7, 0x3131},
	{0x00e8, 0x3131},
	{0x00ea, 0xb320},
	{0x00eb, 0x0000},
	{0x00f0, 0x0000},
	{0x00f1, 0x00d0},
	{0x00f2, 0x00d0},
	{0x00f6, 0x0000},
	{0x00fa, 0x0000},
	{0x00fb, 0x0000},
	{0x00fc, 0x0000},
	{0x00fd, 0x0000},
	{0x00fe, 0x10ec},
	{0x00ff, 0x6530},
	{0x0100, 0xa0a0},
	{0x010b, 0x0000},
	{0x010c, 0xae00},
	{0x010d, 0xaaa0},
	{0x010e, 0x8aa2},
	{0x010f, 0x02a2},
	{0x0110, 0xc000},
	{0x0111, 0x04a2},
	{0x0112, 0x2800},
	{0x0113, 0x0000},
	{0x0117, 0x0100},
	{0x0125, 0x0410},
	{0x0132, 0x6026},
	{0x0136, 0x5555},
	{0x0138, 0x3700},
	{0x013a, 0x2000},
	{0x013b, 0x2000},
	{0x013c, 0x2005},
	{0x013f, 0x0000},
	{0x0142, 0x0000},
	{0x0145, 0x0002},
	{0x0146, 0x0000},
	{0x0147, 0x0000},
	{0x0148, 0x0000},
	{0x0149, 0x0000},
	{0x0150, 0x79a1},
	{0x0156, 0xaaaa},
	{0x0160, 0x4ec0},
	{0x0161, 0x0080},
	{0x0162, 0x0200},
	{0x0163, 0x0800},
	{0x0164, 0x0000},
	{0x0165, 0x0000},
	{0x0166, 0x0000},
	{0x0167, 0x000f},
	{0x0168, 0x000f},
	{0x0169, 0x0021},
	{0x0190, 0x413d},
	{0x0194, 0x0000},
	{0x0195, 0x0000},
	{0x0197, 0x0022},
	{0x0198, 0x0000},
	{0x0199, 0x0000},
	{0x01af, 0x0000},
	{0x01b0, 0x0400},
	{0x01b1, 0x0000},
	{0x01b2, 0x0000},
	{0x01b3, 0x0000},
	{0x01b4, 0x0000},
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
	{0x01d6, 0x0000},
	{0x01d7, 0x0008},
	{0x01d8, 0x0029},
	{0x01d9, 0x3333},
	{0x01da, 0x0000},
	{0x01db, 0x0004},
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
	{0x01ea, 0x0000},
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
	{0x0210, 0x6297},
	{0x0211, 0xa005},
	{0x0212, 0x824c},
	{0x0213, 0xf7ff},
	{0x0214, 0xf24c},
	{0x0215, 0x0102},
	{0x0216, 0x00a3},
	{0x0217, 0x0048},
	{0x0218, 0xa2c0},
	{0x0219, 0x0400},
	{0x021a, 0x00c8},
	{0x021b, 0x00c0},
	{0x021c, 0x0000},
	{0x0250, 0x4500},
	{0x0251, 0x40b3},
	{0x0252, 0x0000},
	{0x0253, 0x0000},
	{0x0254, 0x0000},
	{0x0255, 0x0000},
	{0x0256, 0x0000},
	{0x0257, 0x0000},
	{0x0258, 0x0000},
	{0x0259, 0x0000},
	{0x025a, 0x0005},
	{0x0270, 0x0000},
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
EXPORT_SYMBOL_GPL(rt5682_reg);

bool rt5682_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5682_RESET:
	case RT5682_CBJ_CTRL_2:
	case RT5682_INT_ST_1:
	case RT5682_4BTN_IL_CMD_1:
	case RT5682_AJD1_CTRL:
	case RT5682_HP_CALIB_CTRL_1:
	case RT5682_DEVICE_ID:
	case RT5682_I2C_MODE:
	case RT5682_HP_CALIB_CTRL_10:
	case RT5682_EFUSE_CTRL_2:
	case RT5682_JD_TOP_VC_VTRL:
	case RT5682_HP_IMP_SENS_CTRL_19:
	case RT5682_IL_CMD_1:
	case RT5682_SAR_IL_CMD_2:
	case RT5682_SAR_IL_CMD_4:
	case RT5682_SAR_IL_CMD_10:
	case RT5682_SAR_IL_CMD_11:
	case RT5682_EFUSE_CTRL_6...RT5682_EFUSE_CTRL_11:
	case RT5682_HP_CALIB_STA_1...RT5682_HP_CALIB_STA_11:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(rt5682_volatile_register);

bool rt5682_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5682_RESET:
	case RT5682_VERSION_ID:
	case RT5682_VENDOR_ID:
	case RT5682_DEVICE_ID:
	case RT5682_HP_CTRL_1:
	case RT5682_HP_CTRL_2:
	case RT5682_HPL_GAIN:
	case RT5682_HPR_GAIN:
	case RT5682_I2C_CTRL:
	case RT5682_CBJ_BST_CTRL:
	case RT5682_CBJ_CTRL_1:
	case RT5682_CBJ_CTRL_2:
	case RT5682_CBJ_CTRL_3:
	case RT5682_CBJ_CTRL_4:
	case RT5682_CBJ_CTRL_5:
	case RT5682_CBJ_CTRL_6:
	case RT5682_CBJ_CTRL_7:
	case RT5682_DAC1_DIG_VOL:
	case RT5682_STO1_ADC_DIG_VOL:
	case RT5682_STO1_ADC_BOOST:
	case RT5682_HP_IMP_GAIN_1:
	case RT5682_HP_IMP_GAIN_2:
	case RT5682_SIDETONE_CTRL:
	case RT5682_STO1_ADC_MIXER:
	case RT5682_AD_DA_MIXER:
	case RT5682_STO1_DAC_MIXER:
	case RT5682_A_DAC1_MUX:
	case RT5682_DIG_INF2_DATA:
	case RT5682_REC_MIXER:
	case RT5682_CAL_REC:
	case RT5682_ALC_BACK_GAIN:
	case RT5682_PWR_DIG_1:
	case RT5682_PWR_DIG_2:
	case RT5682_PWR_ANLG_1:
	case RT5682_PWR_ANLG_2:
	case RT5682_PWR_ANLG_3:
	case RT5682_PWR_MIXER:
	case RT5682_PWR_VOL:
	case RT5682_CLK_DET:
	case RT5682_RESET_LPF_CTRL:
	case RT5682_RESET_HPF_CTRL:
	case RT5682_DMIC_CTRL_1:
	case RT5682_I2S1_SDP:
	case RT5682_I2S2_SDP:
	case RT5682_ADDA_CLK_1:
	case RT5682_ADDA_CLK_2:
	case RT5682_I2S1_F_DIV_CTRL_1:
	case RT5682_I2S1_F_DIV_CTRL_2:
	case RT5682_TDM_CTRL:
	case RT5682_TDM_ADDA_CTRL_1:
	case RT5682_TDM_ADDA_CTRL_2:
	case RT5682_DATA_SEL_CTRL_1:
	case RT5682_TDM_TCON_CTRL:
	case RT5682_GLB_CLK:
	case RT5682_PLL_CTRL_1:
	case RT5682_PLL_CTRL_2:
	case RT5682_PLL_TRACK_1:
	case RT5682_PLL_TRACK_2:
	case RT5682_PLL_TRACK_3:
	case RT5682_PLL_TRACK_4:
	case RT5682_PLL_TRACK_5:
	case RT5682_PLL_TRACK_6:
	case RT5682_PLL_TRACK_11:
	case RT5682_SDW_REF_CLK:
	case RT5682_DEPOP_1:
	case RT5682_DEPOP_2:
	case RT5682_HP_CHARGE_PUMP_1:
	case RT5682_HP_CHARGE_PUMP_2:
	case RT5682_MICBIAS_1:
	case RT5682_MICBIAS_2:
	case RT5682_PLL_TRACK_12:
	case RT5682_PLL_TRACK_14:
	case RT5682_PLL2_CTRL_1:
	case RT5682_PLL2_CTRL_2:
	case RT5682_PLL2_CTRL_3:
	case RT5682_PLL2_CTRL_4:
	case RT5682_RC_CLK_CTRL:
	case RT5682_I2S_M_CLK_CTRL_1:
	case RT5682_I2S2_F_DIV_CTRL_1:
	case RT5682_I2S2_F_DIV_CTRL_2:
	case RT5682_EQ_CTRL_1:
	case RT5682_EQ_CTRL_2:
	case RT5682_IRQ_CTRL_1:
	case RT5682_IRQ_CTRL_2:
	case RT5682_IRQ_CTRL_3:
	case RT5682_IRQ_CTRL_4:
	case RT5682_INT_ST_1:
	case RT5682_GPIO_CTRL_1:
	case RT5682_GPIO_CTRL_2:
	case RT5682_GPIO_CTRL_3:
	case RT5682_HP_AMP_DET_CTRL_1:
	case RT5682_HP_AMP_DET_CTRL_2:
	case RT5682_MID_HP_AMP_DET:
	case RT5682_LOW_HP_AMP_DET:
	case RT5682_DELAY_BUF_CTRL:
	case RT5682_SV_ZCD_1:
	case RT5682_SV_ZCD_2:
	case RT5682_IL_CMD_1:
	case RT5682_IL_CMD_2:
	case RT5682_IL_CMD_3:
	case RT5682_IL_CMD_4:
	case RT5682_IL_CMD_5:
	case RT5682_IL_CMD_6:
	case RT5682_4BTN_IL_CMD_1:
	case RT5682_4BTN_IL_CMD_2:
	case RT5682_4BTN_IL_CMD_3:
	case RT5682_4BTN_IL_CMD_4:
	case RT5682_4BTN_IL_CMD_5:
	case RT5682_4BTN_IL_CMD_6:
	case RT5682_4BTN_IL_CMD_7:
	case RT5682_ADC_STO1_HP_CTRL_1:
	case RT5682_ADC_STO1_HP_CTRL_2:
	case RT5682_AJD1_CTRL:
	case RT5682_JD1_THD:
	case RT5682_JD2_THD:
	case RT5682_JD_CTRL_1:
	case RT5682_DUMMY_1:
	case RT5682_DUMMY_2:
	case RT5682_DUMMY_3:
	case RT5682_DAC_ADC_DIG_VOL1:
	case RT5682_BIAS_CUR_CTRL_2:
	case RT5682_BIAS_CUR_CTRL_3:
	case RT5682_BIAS_CUR_CTRL_4:
	case RT5682_BIAS_CUR_CTRL_5:
	case RT5682_BIAS_CUR_CTRL_6:
	case RT5682_BIAS_CUR_CTRL_7:
	case RT5682_BIAS_CUR_CTRL_8:
	case RT5682_BIAS_CUR_CTRL_9:
	case RT5682_BIAS_CUR_CTRL_10:
	case RT5682_VREF_REC_OP_FB_CAP_CTRL:
	case RT5682_CHARGE_PUMP_1:
	case RT5682_DIG_IN_CTRL_1:
	case RT5682_PAD_DRIVING_CTRL:
	case RT5682_SOFT_RAMP_DEPOP:
	case RT5682_CHOP_DAC:
	case RT5682_CHOP_ADC:
	case RT5682_CALIB_ADC_CTRL:
	case RT5682_VOL_TEST:
	case RT5682_SPKVDD_DET_STA:
	case RT5682_TEST_MODE_CTRL_1:
	case RT5682_TEST_MODE_CTRL_2:
	case RT5682_TEST_MODE_CTRL_3:
	case RT5682_TEST_MODE_CTRL_4:
	case RT5682_TEST_MODE_CTRL_5:
	case RT5682_PLL1_INTERNAL:
	case RT5682_PLL2_INTERNAL:
	case RT5682_STO_NG2_CTRL_1:
	case RT5682_STO_NG2_CTRL_2:
	case RT5682_STO_NG2_CTRL_3:
	case RT5682_STO_NG2_CTRL_4:
	case RT5682_STO_NG2_CTRL_5:
	case RT5682_STO_NG2_CTRL_6:
	case RT5682_STO_NG2_CTRL_7:
	case RT5682_STO_NG2_CTRL_8:
	case RT5682_STO_NG2_CTRL_9:
	case RT5682_STO_NG2_CTRL_10:
	case RT5682_STO1_DAC_SIL_DET:
	case RT5682_SIL_PSV_CTRL1:
	case RT5682_SIL_PSV_CTRL2:
	case RT5682_SIL_PSV_CTRL3:
	case RT5682_SIL_PSV_CTRL4:
	case RT5682_SIL_PSV_CTRL5:
	case RT5682_HP_IMP_SENS_CTRL_01:
	case RT5682_HP_IMP_SENS_CTRL_02:
	case RT5682_HP_IMP_SENS_CTRL_03:
	case RT5682_HP_IMP_SENS_CTRL_04:
	case RT5682_HP_IMP_SENS_CTRL_05:
	case RT5682_HP_IMP_SENS_CTRL_06:
	case RT5682_HP_IMP_SENS_CTRL_07:
	case RT5682_HP_IMP_SENS_CTRL_08:
	case RT5682_HP_IMP_SENS_CTRL_09:
	case RT5682_HP_IMP_SENS_CTRL_10:
	case RT5682_HP_IMP_SENS_CTRL_11:
	case RT5682_HP_IMP_SENS_CTRL_12:
	case RT5682_HP_IMP_SENS_CTRL_13:
	case RT5682_HP_IMP_SENS_CTRL_14:
	case RT5682_HP_IMP_SENS_CTRL_15:
	case RT5682_HP_IMP_SENS_CTRL_16:
	case RT5682_HP_IMP_SENS_CTRL_17:
	case RT5682_HP_IMP_SENS_CTRL_18:
	case RT5682_HP_IMP_SENS_CTRL_19:
	case RT5682_HP_IMP_SENS_CTRL_20:
	case RT5682_HP_IMP_SENS_CTRL_21:
	case RT5682_HP_IMP_SENS_CTRL_22:
	case RT5682_HP_IMP_SENS_CTRL_23:
	case RT5682_HP_IMP_SENS_CTRL_24:
	case RT5682_HP_IMP_SENS_CTRL_25:
	case RT5682_HP_IMP_SENS_CTRL_26:
	case RT5682_HP_IMP_SENS_CTRL_27:
	case RT5682_HP_IMP_SENS_CTRL_28:
	case RT5682_HP_IMP_SENS_CTRL_29:
	case RT5682_HP_IMP_SENS_CTRL_30:
	case RT5682_HP_IMP_SENS_CTRL_31:
	case RT5682_HP_IMP_SENS_CTRL_32:
	case RT5682_HP_IMP_SENS_CTRL_33:
	case RT5682_HP_IMP_SENS_CTRL_34:
	case RT5682_HP_IMP_SENS_CTRL_35:
	case RT5682_HP_IMP_SENS_CTRL_36:
	case RT5682_HP_IMP_SENS_CTRL_37:
	case RT5682_HP_IMP_SENS_CTRL_38:
	case RT5682_HP_IMP_SENS_CTRL_39:
	case RT5682_HP_IMP_SENS_CTRL_40:
	case RT5682_HP_IMP_SENS_CTRL_41:
	case RT5682_HP_IMP_SENS_CTRL_42:
	case RT5682_HP_IMP_SENS_CTRL_43:
	case RT5682_HP_LOGIC_CTRL_1:
	case RT5682_HP_LOGIC_CTRL_2:
	case RT5682_HP_LOGIC_CTRL_3:
	case RT5682_HP_CALIB_CTRL_1:
	case RT5682_HP_CALIB_CTRL_2:
	case RT5682_HP_CALIB_CTRL_3:
	case RT5682_HP_CALIB_CTRL_4:
	case RT5682_HP_CALIB_CTRL_5:
	case RT5682_HP_CALIB_CTRL_6:
	case RT5682_HP_CALIB_CTRL_7:
	case RT5682_HP_CALIB_CTRL_9:
	case RT5682_HP_CALIB_CTRL_10:
	case RT5682_HP_CALIB_CTRL_11:
	case RT5682_HP_CALIB_STA_1:
	case RT5682_HP_CALIB_STA_2:
	case RT5682_HP_CALIB_STA_3:
	case RT5682_HP_CALIB_STA_4:
	case RT5682_HP_CALIB_STA_5:
	case RT5682_HP_CALIB_STA_6:
	case RT5682_HP_CALIB_STA_7:
	case RT5682_HP_CALIB_STA_8:
	case RT5682_HP_CALIB_STA_9:
	case RT5682_HP_CALIB_STA_10:
	case RT5682_HP_CALIB_STA_11:
	case RT5682_SAR_IL_CMD_1:
	case RT5682_SAR_IL_CMD_2:
	case RT5682_SAR_IL_CMD_3:
	case RT5682_SAR_IL_CMD_4:
	case RT5682_SAR_IL_CMD_5:
	case RT5682_SAR_IL_CMD_6:
	case RT5682_SAR_IL_CMD_7:
	case RT5682_SAR_IL_CMD_8:
	case RT5682_SAR_IL_CMD_9:
	case RT5682_SAR_IL_CMD_10:
	case RT5682_SAR_IL_CMD_11:
	case RT5682_SAR_IL_CMD_12:
	case RT5682_SAR_IL_CMD_13:
	case RT5682_EFUSE_CTRL_1:
	case RT5682_EFUSE_CTRL_2:
	case RT5682_EFUSE_CTRL_3:
	case RT5682_EFUSE_CTRL_4:
	case RT5682_EFUSE_CTRL_5:
	case RT5682_EFUSE_CTRL_6:
	case RT5682_EFUSE_CTRL_7:
	case RT5682_EFUSE_CTRL_8:
	case RT5682_EFUSE_CTRL_9:
	case RT5682_EFUSE_CTRL_10:
	case RT5682_EFUSE_CTRL_11:
	case RT5682_JD_TOP_VC_VTRL:
	case RT5682_DRC1_CTRL_0:
	case RT5682_DRC1_CTRL_1:
	case RT5682_DRC1_CTRL_2:
	case RT5682_DRC1_CTRL_3:
	case RT5682_DRC1_CTRL_4:
	case RT5682_DRC1_CTRL_5:
	case RT5682_DRC1_CTRL_6:
	case RT5682_DRC1_HARD_LMT_CTRL_1:
	case RT5682_DRC1_HARD_LMT_CTRL_2:
	case RT5682_DRC1_PRIV_1:
	case RT5682_DRC1_PRIV_2:
	case RT5682_DRC1_PRIV_3:
	case RT5682_DRC1_PRIV_4:
	case RT5682_DRC1_PRIV_5:
	case RT5682_DRC1_PRIV_6:
	case RT5682_DRC1_PRIV_7:
	case RT5682_DRC1_PRIV_8:
	case RT5682_EQ_AUTO_RCV_CTRL1:
	case RT5682_EQ_AUTO_RCV_CTRL2:
	case RT5682_EQ_AUTO_RCV_CTRL3:
	case RT5682_EQ_AUTO_RCV_CTRL4:
	case RT5682_EQ_AUTO_RCV_CTRL5:
	case RT5682_EQ_AUTO_RCV_CTRL6:
	case RT5682_EQ_AUTO_RCV_CTRL7:
	case RT5682_EQ_AUTO_RCV_CTRL8:
	case RT5682_EQ_AUTO_RCV_CTRL9:
	case RT5682_EQ_AUTO_RCV_CTRL10:
	case RT5682_EQ_AUTO_RCV_CTRL11:
	case RT5682_EQ_AUTO_RCV_CTRL12:
	case RT5682_EQ_AUTO_RCV_CTRL13:
	case RT5682_ADC_L_EQ_LPF1_A1:
	case RT5682_R_EQ_LPF1_A1:
	case RT5682_L_EQ_LPF1_H0:
	case RT5682_R_EQ_LPF1_H0:
	case RT5682_L_EQ_BPF1_A1:
	case RT5682_R_EQ_BPF1_A1:
	case RT5682_L_EQ_BPF1_A2:
	case RT5682_R_EQ_BPF1_A2:
	case RT5682_L_EQ_BPF1_H0:
	case RT5682_R_EQ_BPF1_H0:
	case RT5682_L_EQ_BPF2_A1:
	case RT5682_R_EQ_BPF2_A1:
	case RT5682_L_EQ_BPF2_A2:
	case RT5682_R_EQ_BPF2_A2:
	case RT5682_L_EQ_BPF2_H0:
	case RT5682_R_EQ_BPF2_H0:
	case RT5682_L_EQ_BPF3_A1:
	case RT5682_R_EQ_BPF3_A1:
	case RT5682_L_EQ_BPF3_A2:
	case RT5682_R_EQ_BPF3_A2:
	case RT5682_L_EQ_BPF3_H0:
	case RT5682_R_EQ_BPF3_H0:
	case RT5682_L_EQ_BPF4_A1:
	case RT5682_R_EQ_BPF4_A1:
	case RT5682_L_EQ_BPF4_A2:
	case RT5682_R_EQ_BPF4_A2:
	case RT5682_L_EQ_BPF4_H0:
	case RT5682_R_EQ_BPF4_H0:
	case RT5682_L_EQ_HPF1_A1:
	case RT5682_R_EQ_HPF1_A1:
	case RT5682_L_EQ_HPF1_H0:
	case RT5682_R_EQ_HPF1_H0:
	case RT5682_L_EQ_PRE_VOL:
	case RT5682_R_EQ_PRE_VOL:
	case RT5682_L_EQ_POST_VOL:
	case RT5682_R_EQ_POST_VOL:
	case RT5682_I2C_MODE:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(rt5682_readable_register);

static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);

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
static const char * const rt5682_data_select[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static SOC_ENUM_SINGLE_DECL(rt5682_if2_adc_enum,
	RT5682_DIG_INF2_DATA, RT5682_IF2_ADC_SEL_SFT, rt5682_data_select);

static SOC_ENUM_SINGLE_DECL(rt5682_if1_01_adc_enum,
	RT5682_TDM_ADDA_CTRL_1, RT5682_IF1_ADC1_SEL_SFT, rt5682_data_select);

static SOC_ENUM_SINGLE_DECL(rt5682_if1_23_adc_enum,
	RT5682_TDM_ADDA_CTRL_1, RT5682_IF1_ADC2_SEL_SFT, rt5682_data_select);

static SOC_ENUM_SINGLE_DECL(rt5682_if1_45_adc_enum,
	RT5682_TDM_ADDA_CTRL_1, RT5682_IF1_ADC3_SEL_SFT, rt5682_data_select);

static SOC_ENUM_SINGLE_DECL(rt5682_if1_67_adc_enum,
	RT5682_TDM_ADDA_CTRL_1, RT5682_IF1_ADC4_SEL_SFT, rt5682_data_select);

static const struct snd_kcontrol_new rt5682_if2_adc_swap_mux =
	SOC_DAPM_ENUM("IF2 ADC Swap Mux", rt5682_if2_adc_enum);

static const struct snd_kcontrol_new rt5682_if1_01_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 01 ADC Swap Mux", rt5682_if1_01_adc_enum);

static const struct snd_kcontrol_new rt5682_if1_23_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 23 ADC Swap Mux", rt5682_if1_23_adc_enum);

static const struct snd_kcontrol_new rt5682_if1_45_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 45 ADC Swap Mux", rt5682_if1_45_adc_enum);

static const struct snd_kcontrol_new rt5682_if1_67_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 67 ADC Swap Mux", rt5682_if1_67_adc_enum);

static const char * const rt5682_dac_select[] = {
	"IF1", "SOUND"
};

static SOC_ENUM_SINGLE_DECL(rt5682_dacl_enum,
	RT5682_AD_DA_MIXER, RT5682_DAC1_L_SEL_SFT, rt5682_dac_select);

static const struct snd_kcontrol_new rt5682_dac_l_mux =
	SOC_DAPM_ENUM("DAC L Mux", rt5682_dacl_enum);

static SOC_ENUM_SINGLE_DECL(rt5682_dacr_enum,
	RT5682_AD_DA_MIXER, RT5682_DAC1_R_SEL_SFT, rt5682_dac_select);

static const struct snd_kcontrol_new rt5682_dac_r_mux =
	SOC_DAPM_ENUM("DAC R Mux", rt5682_dacr_enum);

void rt5682_reset(struct rt5682_priv *rt5682)
{
	regmap_write(rt5682->regmap, RT5682_RESET, 0);
	if (!rt5682->is_sdw)
		regmap_write(rt5682->regmap, RT5682_I2C_MODE, 1);
}
EXPORT_SYMBOL_GPL(rt5682_reset);

/**
 * rt5682_sel_asrc_clk_src - select ASRC clock source for a set of filters
 * @component: SoC audio component device.
 * @filter_mask: mask of filters.
 * @clk_src: clock source
 *
 * The ASRC function is for asynchronous MCLK and LRCK. Also, since RT5682 can
 * only support standard 32fs or 64fs i2s format, ASRC should be enabled to
 * support special i2s clock format such as Intel's 100fs(100 * sampling rate).
 * ASRC function will track i2s clock and generate a corresponding system clock
 * for codec. This function provides an API to select the clock source for a
 * set of filters specified by the mask. And the component driver will turn on
 * ASRC for these filters if ASRC is selected as their clock source.
 */
int rt5682_sel_asrc_clk_src(struct snd_soc_component *component,
		unsigned int filter_mask, unsigned int clk_src)
{
	switch (clk_src) {
	case RT5682_CLK_SEL_SYS:
	case RT5682_CLK_SEL_I2S1_ASRC:
	case RT5682_CLK_SEL_I2S2_ASRC:
		break;

	default:
		return -EINVAL;
	}

	if (filter_mask & RT5682_DA_STEREO1_FILTER) {
		snd_soc_component_update_bits(component, RT5682_PLL_TRACK_2,
			RT5682_FILTER_CLK_SEL_MASK,
			clk_src << RT5682_FILTER_CLK_SEL_SFT);
	}

	if (filter_mask & RT5682_AD_STEREO1_FILTER) {
		snd_soc_component_update_bits(component, RT5682_PLL_TRACK_3,
			RT5682_FILTER_CLK_SEL_MASK,
			clk_src << RT5682_FILTER_CLK_SEL_SFT);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rt5682_sel_asrc_clk_src);

static int rt5682_button_detect(struct snd_soc_component *component)
{
	int btn_type, val;

	val = snd_soc_component_read(component, RT5682_4BTN_IL_CMD_1);
	btn_type = val & 0xfff0;
	snd_soc_component_write(component, RT5682_4BTN_IL_CMD_1, val);
	dev_dbg(component->dev, "%s btn_type=%x\n", __func__, btn_type);
	snd_soc_component_update_bits(component,
		RT5682_SAR_IL_CMD_2, 0x10, 0x10);

	return btn_type;
}

static void rt5682_enable_push_button_irq(struct snd_soc_component *component,
		bool enable)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);

	if (enable) {
		snd_soc_component_update_bits(component, RT5682_SAR_IL_CMD_1,
			RT5682_SAR_BUTT_DET_MASK, RT5682_SAR_BUTT_DET_EN);
		snd_soc_component_update_bits(component, RT5682_SAR_IL_CMD_13,
			RT5682_SAR_SOUR_MASK, RT5682_SAR_SOUR_BTN);
		snd_soc_component_write(component, RT5682_IL_CMD_1, 0x0040);
		snd_soc_component_update_bits(component, RT5682_4BTN_IL_CMD_2,
			RT5682_4BTN_IL_MASK | RT5682_4BTN_IL_RST_MASK,
			RT5682_4BTN_IL_EN | RT5682_4BTN_IL_NOR);
		if (rt5682->is_sdw)
			snd_soc_component_update_bits(component,
				RT5682_IRQ_CTRL_3,
				RT5682_IL_IRQ_MASK | RT5682_IL_IRQ_TYPE_MASK,
				RT5682_IL_IRQ_EN | RT5682_IL_IRQ_PUL);
		else
			snd_soc_component_update_bits(component,
				RT5682_IRQ_CTRL_3, RT5682_IL_IRQ_MASK,
				RT5682_IL_IRQ_EN);
	} else {
		snd_soc_component_update_bits(component, RT5682_IRQ_CTRL_3,
			RT5682_IL_IRQ_MASK, RT5682_IL_IRQ_DIS);
		snd_soc_component_update_bits(component, RT5682_SAR_IL_CMD_1,
			RT5682_SAR_BUTT_DET_MASK, RT5682_SAR_BUTT_DET_DIS);
		snd_soc_component_update_bits(component, RT5682_4BTN_IL_CMD_2,
			RT5682_4BTN_IL_MASK, RT5682_4BTN_IL_DIS);
		snd_soc_component_update_bits(component, RT5682_4BTN_IL_CMD_2,
			RT5682_4BTN_IL_RST_MASK, RT5682_4BTN_IL_RST);
		snd_soc_component_update_bits(component, RT5682_SAR_IL_CMD_13,
			RT5682_SAR_SOUR_MASK, RT5682_SAR_SOUR_TYPE);
	}
}

/**
 * rt5682_headset_detect - Detect headset.
 * @component: SoC audio component device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */
int rt5682_headset_detect(struct snd_soc_component *component, int jack_insert)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = &component->dapm;
	unsigned int val, count;

	if (jack_insert) {
		snd_soc_component_update_bits(component, RT5682_PWR_ANLG_1,
			RT5682_PWR_VREF2 | RT5682_PWR_MB,
			RT5682_PWR_VREF2 | RT5682_PWR_MB);
		snd_soc_component_update_bits(component,
			RT5682_PWR_ANLG_1, RT5682_PWR_FV2, 0);
		usleep_range(15000, 20000);
		snd_soc_component_update_bits(component,
			RT5682_PWR_ANLG_1, RT5682_PWR_FV2, RT5682_PWR_FV2);
		snd_soc_component_update_bits(component, RT5682_PWR_ANLG_3,
			RT5682_PWR_CBJ, RT5682_PWR_CBJ);
		snd_soc_component_update_bits(component,
			RT5682_HP_CHARGE_PUMP_1,
			RT5682_OSW_L_MASK | RT5682_OSW_R_MASK, 0);
		snd_soc_component_update_bits(component, RT5682_CBJ_CTRL_1,
			RT5682_TRIG_JD_MASK, RT5682_TRIG_JD_HIGH);

		count = 0;
		val = snd_soc_component_read(component, RT5682_CBJ_CTRL_2)
			& RT5682_JACK_TYPE_MASK;
		while (val == 0 && count < 50) {
			usleep_range(10000, 15000);
			val = snd_soc_component_read(component,
				RT5682_CBJ_CTRL_2) & RT5682_JACK_TYPE_MASK;
			count++;
		}

		switch (val) {
		case 0x1:
		case 0x2:
			rt5682->jack_type = SND_JACK_HEADSET;
			snd_soc_component_update_bits(component, RT5682_CBJ_CTRL_1,
				RT5682_FAST_OFF_MASK, RT5682_FAST_OFF_EN);
			rt5682_enable_push_button_irq(component, true);
			break;
		default:
			rt5682->jack_type = SND_JACK_HEADPHONE;
			break;
		}

		snd_soc_component_update_bits(component,
			RT5682_HP_CHARGE_PUMP_1,
			RT5682_OSW_L_MASK | RT5682_OSW_R_MASK,
			RT5682_OSW_L_EN | RT5682_OSW_R_EN);
		snd_soc_component_update_bits(component, RT5682_MICBIAS_2,
			RT5682_PWR_CLK25M_MASK | RT5682_PWR_CLK1M_MASK,
			RT5682_PWR_CLK25M_PU | RT5682_PWR_CLK1M_PU);
	} else {
		rt5682_enable_push_button_irq(component, false);
		snd_soc_component_update_bits(component, RT5682_CBJ_CTRL_1,
			RT5682_TRIG_JD_MASK, RT5682_TRIG_JD_LOW);
		if (!snd_soc_dapm_get_pin_status(dapm, "MICBIAS") &&
			!snd_soc_dapm_get_pin_status(dapm, "PLL1") &&
			!snd_soc_dapm_get_pin_status(dapm, "PLL2B"))
			snd_soc_component_update_bits(component,
				RT5682_PWR_ANLG_1, RT5682_PWR_MB, 0);
		if (!snd_soc_dapm_get_pin_status(dapm, "Vref2") &&
			!snd_soc_dapm_get_pin_status(dapm, "PLL1") &&
			!snd_soc_dapm_get_pin_status(dapm, "PLL2B"))
			snd_soc_component_update_bits(component,
				RT5682_PWR_ANLG_1, RT5682_PWR_VREF2, 0);
		snd_soc_component_update_bits(component, RT5682_PWR_ANLG_3,
			RT5682_PWR_CBJ, 0);
		snd_soc_component_update_bits(component, RT5682_MICBIAS_2,
			RT5682_PWR_CLK25M_MASK | RT5682_PWR_CLK1M_MASK,
			RT5682_PWR_CLK25M_PD | RT5682_PWR_CLK1M_PD);
		snd_soc_component_update_bits(component, RT5682_CBJ_CTRL_1,
			RT5682_FAST_OFF_MASK, RT5682_FAST_OFF_DIS);

		rt5682->jack_type = 0;
	}

	dev_dbg(component->dev, "jack_type = %d\n", rt5682->jack_type);
	return rt5682->jack_type;
}
EXPORT_SYMBOL_GPL(rt5682_headset_detect);

static int rt5682_set_jack_detect(struct snd_soc_component *component,
		struct snd_soc_jack *hs_jack, void *data)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);

	rt5682->hs_jack = hs_jack;

	if (!hs_jack) {
		regmap_update_bits(rt5682->regmap, RT5682_IRQ_CTRL_2,
			RT5682_JD1_EN_MASK, RT5682_JD1_DIS);
		regmap_update_bits(rt5682->regmap, RT5682_RC_CLK_CTRL,
			RT5682_POW_JDH | RT5682_POW_JDL, 0);
		cancel_delayed_work_sync(&rt5682->jack_detect_work);

		return 0;
	}

	if (!rt5682->is_sdw) {
		switch (rt5682->pdata.jd_src) {
		case RT5682_JD1:
			snd_soc_component_update_bits(component,
				RT5682_CBJ_CTRL_5, 0x0700, 0x0600);
			snd_soc_component_update_bits(component,
				RT5682_CBJ_CTRL_2, RT5682_EXT_JD_SRC,
				RT5682_EXT_JD_SRC_MANUAL);
			snd_soc_component_write(component, RT5682_CBJ_CTRL_1,
				0xd142);
			snd_soc_component_update_bits(component,
				RT5682_CBJ_CTRL_3, RT5682_CBJ_IN_BUF_EN,
				RT5682_CBJ_IN_BUF_EN);
			snd_soc_component_update_bits(component,
				RT5682_SAR_IL_CMD_1, RT5682_SAR_POW_MASK,
				RT5682_SAR_POW_EN);
			regmap_update_bits(rt5682->regmap, RT5682_GPIO_CTRL_1,
				RT5682_GP1_PIN_MASK, RT5682_GP1_PIN_IRQ);
			regmap_update_bits(rt5682->regmap, RT5682_RC_CLK_CTRL,
				RT5682_POW_IRQ | RT5682_POW_JDH |
				RT5682_POW_ANA, RT5682_POW_IRQ |
				RT5682_POW_JDH | RT5682_POW_ANA);
			regmap_update_bits(rt5682->regmap, RT5682_PWR_ANLG_2,
				RT5682_PWR_JDH, RT5682_PWR_JDH);
			regmap_update_bits(rt5682->regmap, RT5682_IRQ_CTRL_2,
				RT5682_JD1_EN_MASK | RT5682_JD1_POL_MASK,
				RT5682_JD1_EN | RT5682_JD1_POL_NOR);
			regmap_update_bits(rt5682->regmap, RT5682_4BTN_IL_CMD_4,
				0x7f7f, (rt5682->pdata.btndet_delay << 8 |
				rt5682->pdata.btndet_delay));
			regmap_update_bits(rt5682->regmap, RT5682_4BTN_IL_CMD_5,
				0x7f7f, (rt5682->pdata.btndet_delay << 8 |
				rt5682->pdata.btndet_delay));
			regmap_update_bits(rt5682->regmap, RT5682_4BTN_IL_CMD_6,
				0x7f7f, (rt5682->pdata.btndet_delay << 8 |
				rt5682->pdata.btndet_delay));
			regmap_update_bits(rt5682->regmap, RT5682_4BTN_IL_CMD_7,
				0x7f7f, (rt5682->pdata.btndet_delay << 8 |
				rt5682->pdata.btndet_delay));
			mod_delayed_work(system_power_efficient_wq,
				&rt5682->jack_detect_work,
				msecs_to_jiffies(250));
			break;

		case RT5682_JD_NULL:
			regmap_update_bits(rt5682->regmap, RT5682_IRQ_CTRL_2,
				RT5682_JD1_EN_MASK, RT5682_JD1_DIS);
			regmap_update_bits(rt5682->regmap, RT5682_RC_CLK_CTRL,
				RT5682_POW_JDH | RT5682_POW_JDL, 0);
			break;

		default:
			dev_warn(component->dev, "Wrong JD source\n");
			break;
		}
	}

	return 0;
}

void rt5682_jack_detect_handler(struct work_struct *work)
{
	struct rt5682_priv *rt5682 =
		container_of(work, struct rt5682_priv, jack_detect_work.work);
	int val, btn_type;

	while (!rt5682->component)
		usleep_range(10000, 15000);

	while (!rt5682->component->card->instantiated)
		usleep_range(10000, 15000);

	mutex_lock(&rt5682->calibrate_mutex);

	val = snd_soc_component_read(rt5682->component, RT5682_AJD1_CTRL)
		& RT5682_JDH_RS_MASK;
	if (!val) {
		/* jack in */
		if (rt5682->jack_type == 0) {
			/* jack was out, report jack type */
			rt5682->jack_type =
				rt5682_headset_detect(rt5682->component, 1);
			rt5682->irq_work_delay_time = 0;
		} else if ((rt5682->jack_type & SND_JACK_HEADSET) ==
			SND_JACK_HEADSET) {
			/* jack is already in, report button event */
			rt5682->jack_type = SND_JACK_HEADSET;
			btn_type = rt5682_button_detect(rt5682->component);
			/**
			 * rt5682 can report three kinds of button behavior,
			 * one click, double click and hold. However,
			 * currently we will report button pressed/released
			 * event. So all the three button behaviors are
			 * treated as button pressed.
			 */
			switch (btn_type) {
			case 0x8000:
			case 0x4000:
			case 0x2000:
				rt5682->jack_type |= SND_JACK_BTN_0;
				break;
			case 0x1000:
			case 0x0800:
			case 0x0400:
				rt5682->jack_type |= SND_JACK_BTN_1;
				break;
			case 0x0200:
			case 0x0100:
			case 0x0080:
				rt5682->jack_type |= SND_JACK_BTN_2;
				break;
			case 0x0040:
			case 0x0020:
			case 0x0010:
				rt5682->jack_type |= SND_JACK_BTN_3;
				break;
			case 0x0000: /* unpressed */
				break;
			default:
				dev_err(rt5682->component->dev,
					"Unexpected button code 0x%04x\n",
					btn_type);
				break;
			}
		}
	} else {
		/* jack out */
		rt5682->jack_type = rt5682_headset_detect(rt5682->component, 0);
		rt5682->irq_work_delay_time = 50;
	}

	snd_soc_jack_report(rt5682->hs_jack, rt5682->jack_type,
		SND_JACK_HEADSET |
		SND_JACK_BTN_0 | SND_JACK_BTN_1 |
		SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (!rt5682->is_sdw) {
		if (rt5682->jack_type & (SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3))
			schedule_delayed_work(&rt5682->jd_check_work, 0);
		else
			cancel_delayed_work_sync(&rt5682->jd_check_work);
	}

	mutex_unlock(&rt5682->calibrate_mutex);
}
EXPORT_SYMBOL_GPL(rt5682_jack_detect_handler);

static const struct snd_kcontrol_new rt5682_snd_controls[] = {
	/* DAC Digital Volume */
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5682_DAC1_DIG_VOL,
		RT5682_L_VOL_SFT + 1, RT5682_R_VOL_SFT + 1, 87, 0, dac_vol_tlv),

	/* IN Boost Volume */
	SOC_SINGLE_TLV("CBJ Boost Volume", RT5682_CBJ_BST_CTRL,
		RT5682_BST_CBJ_SFT, 8, 0, bst_tlv),

	/* ADC Digital Volume Control */
	SOC_DOUBLE("STO1 ADC Capture Switch", RT5682_STO1_ADC_DIG_VOL,
		RT5682_L_MUTE_SFT, RT5682_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("STO1 ADC Capture Volume", RT5682_STO1_ADC_DIG_VOL,
		RT5682_L_VOL_SFT + 1, RT5682_R_VOL_SFT + 1, 63, 0, adc_vol_tlv),

	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("STO1 ADC Boost Gain Volume", RT5682_STO1_ADC_BOOST,
		RT5682_STO1_ADC_L_BST_SFT, RT5682_STO1_ADC_R_BST_SFT,
		3, 0, adc_bst_tlv),
};

static int rt5682_div_sel(struct rt5682_priv *rt5682,
		int target, const int div[], int size)
{
	int i;

	if (rt5682->sysclk < target) {
		dev_err(rt5682->component->dev,
			"sysclk rate %d is too low\n", rt5682->sysclk);
		return 0;
	}

	for (i = 0; i < size - 1; i++) {
		dev_dbg(rt5682->component->dev, "div[%d]=%d\n", i, div[i]);
		if (target * div[i] == rt5682->sysclk)
			return i;
		if (target * div[i + 1] > rt5682->sysclk) {
			dev_dbg(rt5682->component->dev,
				"can't find div for sysclk %d\n",
				rt5682->sysclk);
			return i;
		}
	}

	if (target * div[i] < rt5682->sysclk)
		dev_err(rt5682->component->dev,
			"sysclk rate %d is too high\n", rt5682->sysclk);

	return size - 1;
}

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
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	int idx, dmic_clk_rate = 3072000;
	static const int div[] = {2, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128};

	if (rt5682->pdata.dmic_clk_rate)
		dmic_clk_rate = rt5682->pdata.dmic_clk_rate;

	idx = rt5682_div_sel(rt5682, dmic_clk_rate, div, ARRAY_SIZE(div));

	snd_soc_component_update_bits(component, RT5682_DMIC_CTRL_1,
		RT5682_DMIC_CLK_MASK, idx << RT5682_DMIC_CLK_SFT);

	return 0;
}

static int set_filter_clk(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	int ref, val, reg, idx;
	static const int div_f[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48};
	static const int div_o[] = {1, 2, 4, 6, 8, 12, 16, 24, 32, 48};

	if (rt5682->is_sdw)
		return 0;

	val = snd_soc_component_read(component, RT5682_GPIO_CTRL_1) &
		RT5682_GP4_PIN_MASK;
	if (w->shift == RT5682_PWR_ADC_S1F_BIT &&
		val == RT5682_GP4_PIN_ADCDAT2)
		ref = 256 * rt5682->lrck[RT5682_AIF2];
	else
		ref = 256 * rt5682->lrck[RT5682_AIF1];

	idx = rt5682_div_sel(rt5682, ref, div_f, ARRAY_SIZE(div_f));

	if (w->shift == RT5682_PWR_ADC_S1F_BIT)
		reg = RT5682_PLL_TRACK_3;
	else
		reg = RT5682_PLL_TRACK_2;

	snd_soc_component_update_bits(component, reg,
		RT5682_FILTER_CLK_DIV_MASK, idx << RT5682_FILTER_CLK_DIV_SFT);

	/* select over sample rate */
	for (idx = 0; idx < ARRAY_SIZE(div_o); idx++) {
		if (rt5682->sysclk <= 12288000 * div_o[idx])
			break;
	}

	snd_soc_component_update_bits(component, RT5682_ADDA_CLK_1,
		RT5682_ADC_OSR_MASK | RT5682_DAC_OSR_MASK,
		(idx << RT5682_ADC_OSR_SFT) | (idx << RT5682_DAC_OSR_SFT));

	return 0;
}

static int is_sys_clk_from_pll1(struct snd_soc_dapm_widget *w,
		struct snd_soc_dapm_widget *sink)
{
	unsigned int val;
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	val = snd_soc_component_read(component, RT5682_GLB_CLK);
	val &= RT5682_SCLK_SRC_MASK;
	if (val == RT5682_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

static int is_sys_clk_from_pll2(struct snd_soc_dapm_widget *w,
		struct snd_soc_dapm_widget *sink)
{
	unsigned int val;
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	val = snd_soc_component_read(component, RT5682_GLB_CLK);
	val &= RT5682_SCLK_SRC_MASK;
	if (val == RT5682_SCLK_SRC_PLL2)
		return 1;
	else
		return 0;
}

static int is_using_asrc(struct snd_soc_dapm_widget *w,
		struct snd_soc_dapm_widget *sink)
{
	unsigned int reg, shift, val;
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	switch (w->shift) {
	case RT5682_ADC_STO1_ASRC_SFT:
		reg = RT5682_PLL_TRACK_3;
		shift = RT5682_FILTER_CLK_SEL_SFT;
		break;
	case RT5682_DAC_STO1_ASRC_SFT:
		reg = RT5682_PLL_TRACK_2;
		shift = RT5682_FILTER_CLK_SEL_SFT;
		break;
	default:
		return 0;
	}

	val = (snd_soc_component_read(component, reg) >> shift) & 0xf;
	switch (val) {
	case RT5682_CLK_SEL_I2S1_ASRC:
	case RT5682_CLK_SEL_I2S2_ASRC:
		return 1;
	default:
		return 0;
	}
}

/* Digital Mixer */
static const struct snd_kcontrol_new rt5682_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5682_STO1_ADC_MIXER,
			RT5682_M_STO1_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5682_STO1_ADC_MIXER,
			RT5682_M_STO1_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5682_STO1_ADC_MIXER,
			RT5682_M_STO1_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5682_STO1_ADC_MIXER,
			RT5682_M_STO1_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5682_AD_DA_MIXER,
			RT5682_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5682_AD_DA_MIXER,
			RT5682_M_DAC1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5682_AD_DA_MIXER,
			RT5682_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5682_AD_DA_MIXER,
			RT5682_M_DAC1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682_sto1_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5682_STO1_DAC_MIXER,
			RT5682_M_DAC_L1_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5682_STO1_DAC_MIXER,
			RT5682_M_DAC_R1_STO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682_sto1_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5682_STO1_DAC_MIXER,
			RT5682_M_DAC_L1_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5682_STO1_DAC_MIXER,
			RT5682_M_DAC_R1_STO_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5682_rec1_l_mix[] = {
	SOC_DAPM_SINGLE("CBJ Switch", RT5682_REC_MIXER,
			RT5682_M_CBJ_RM1_L_SFT, 1, 1),
};

/* STO1 ADC1 Source */
/* MX-26 [13] [5] */
static const char * const rt5682_sto1_adc1_src[] = {
	"DAC MIX", "ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5682_sto1_adc1l_enum, RT5682_STO1_ADC_MIXER,
	RT5682_STO1_ADC1L_SRC_SFT, rt5682_sto1_adc1_src);

static const struct snd_kcontrol_new rt5682_sto1_adc1l_mux =
	SOC_DAPM_ENUM("Stereo1 ADC1L Source", rt5682_sto1_adc1l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5682_sto1_adc1r_enum, RT5682_STO1_ADC_MIXER,
	RT5682_STO1_ADC1R_SRC_SFT, rt5682_sto1_adc1_src);

static const struct snd_kcontrol_new rt5682_sto1_adc1r_mux =
	SOC_DAPM_ENUM("Stereo1 ADC1L Source", rt5682_sto1_adc1r_enum);

/* STO1 ADC Source */
/* MX-26 [11:10] [3:2] */
static const char * const rt5682_sto1_adc_src[] = {
	"ADC1 L", "ADC1 R"
};

static SOC_ENUM_SINGLE_DECL(
	rt5682_sto1_adcl_enum, RT5682_STO1_ADC_MIXER,
	RT5682_STO1_ADCL_SRC_SFT, rt5682_sto1_adc_src);

static const struct snd_kcontrol_new rt5682_sto1_adcl_mux =
	SOC_DAPM_ENUM("Stereo1 ADCL Source", rt5682_sto1_adcl_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5682_sto1_adcr_enum, RT5682_STO1_ADC_MIXER,
	RT5682_STO1_ADCR_SRC_SFT, rt5682_sto1_adc_src);

static const struct snd_kcontrol_new rt5682_sto1_adcr_mux =
	SOC_DAPM_ENUM("Stereo1 ADCR Source", rt5682_sto1_adcr_enum);

/* STO1 ADC2 Source */
/* MX-26 [12] [4] */
static const char * const rt5682_sto1_adc2_src[] = {
	"DAC MIX", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5682_sto1_adc2l_enum, RT5682_STO1_ADC_MIXER,
	RT5682_STO1_ADC2L_SRC_SFT, rt5682_sto1_adc2_src);

static const struct snd_kcontrol_new rt5682_sto1_adc2l_mux =
	SOC_DAPM_ENUM("Stereo1 ADC2L Source", rt5682_sto1_adc2l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5682_sto1_adc2r_enum, RT5682_STO1_ADC_MIXER,
	RT5682_STO1_ADC2R_SRC_SFT, rt5682_sto1_adc2_src);

static const struct snd_kcontrol_new rt5682_sto1_adc2r_mux =
	SOC_DAPM_ENUM("Stereo1 ADC2R Source", rt5682_sto1_adc2r_enum);

/* MX-79 [6:4] I2S1 ADC data location */
static const unsigned int rt5682_if1_adc_slot_values[] = {
	0,
	2,
	4,
	6,
};

static const char * const rt5682_if1_adc_slot_src[] = {
	"Slot 0", "Slot 2", "Slot 4", "Slot 6"
};

static SOC_VALUE_ENUM_SINGLE_DECL(rt5682_if1_adc_slot_enum,
	RT5682_TDM_CTRL, RT5682_TDM_ADC_LCA_SFT, RT5682_TDM_ADC_LCA_MASK,
	rt5682_if1_adc_slot_src, rt5682_if1_adc_slot_values);

static const struct snd_kcontrol_new rt5682_if1_adc_slot_mux =
	SOC_DAPM_ENUM("IF1 ADC Slot location", rt5682_if1_adc_slot_enum);

/* Analog DAC L1 Source, Analog DAC R1 Source*/
/* MX-2B [4], MX-2B [0]*/
static const char * const rt5682_alg_dac1_src[] = {
	"Stereo1 DAC Mixer", "DAC1"
};

static SOC_ENUM_SINGLE_DECL(
	rt5682_alg_dac_l1_enum, RT5682_A_DAC1_MUX,
	RT5682_A_DACL1_SFT, rt5682_alg_dac1_src);

static const struct snd_kcontrol_new rt5682_alg_dac_l1_mux =
	SOC_DAPM_ENUM("Analog DAC L1 Source", rt5682_alg_dac_l1_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5682_alg_dac_r1_enum, RT5682_A_DAC1_MUX,
	RT5682_A_DACR1_SFT, rt5682_alg_dac1_src);

static const struct snd_kcontrol_new rt5682_alg_dac_r1_mux =
	SOC_DAPM_ENUM("Analog DAC R1 Source", rt5682_alg_dac_r1_enum);

/* Out Switch */
static const struct snd_kcontrol_new hpol_switch =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5682_HP_CTRL_1,
		RT5682_L_MUTE_SFT, 1, 1);
static const struct snd_kcontrol_new hpor_switch =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5682_HP_CTRL_1,
		RT5682_R_MUTE_SFT, 1, 1);

static int rt5682_hp_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component, RT5682_HP_CTRL_2,
			RT5682_HP_C2_DAC_AMP_MUTE, 0);
		snd_soc_component_update_bits(component, RT5682_HP_LOGIC_CTRL_2,
			RT5682_HP_LC2_SIG_SOUR2_MASK, RT5682_HP_LC2_SIG_SOUR2_REG);
		snd_soc_component_update_bits(component,
			RT5682_DEPOP_1, 0x60, 0x60);
		snd_soc_component_update_bits(component,
			RT5682_DAC_ADC_DIG_VOL1, 0x00c0, 0x0080);
		snd_soc_component_update_bits(component, RT5682_HP_CTRL_2,
			RT5682_HP_C2_DAC_L_EN | RT5682_HP_C2_DAC_R_EN,
			RT5682_HP_C2_DAC_L_EN | RT5682_HP_C2_DAC_R_EN);
		usleep_range(5000, 10000);
		snd_soc_component_update_bits(component, RT5682_CHARGE_PUMP_1,
			RT5682_CP_SW_SIZE_MASK, RT5682_CP_SW_SIZE_L);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, RT5682_HP_CTRL_2,
			RT5682_HP_C2_DAC_L_EN | RT5682_HP_C2_DAC_R_EN, 0);
		snd_soc_component_update_bits(component, RT5682_CHARGE_PUMP_1,
			RT5682_CP_SW_SIZE_MASK, RT5682_CP_SW_SIZE_M);
		snd_soc_component_update_bits(component,
			RT5682_DEPOP_1, 0x60, 0x0);
		snd_soc_component_update_bits(component,
			RT5682_DAC_ADC_DIG_VOL1, 0x00c0, 0x0000);
		break;
	}

	return 0;
}

static int set_dmic_power(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	unsigned int delay = 50, val;

	if (rt5682->pdata.dmic_delay)
		delay = rt5682->pdata.dmic_delay;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = snd_soc_component_read(component, RT5682_GLB_CLK);
		val &= RT5682_SCLK_SRC_MASK;
		if (val == RT5682_SCLK_SRC_PLL1 || val == RT5682_SCLK_SRC_PLL2)
			snd_soc_component_update_bits(component,
				RT5682_PWR_ANLG_1,
				RT5682_PWR_VREF2 | RT5682_PWR_MB,
				RT5682_PWR_VREF2 | RT5682_PWR_MB);

		/*Add delay to avoid pop noise*/
		msleep(delay);
		break;

	case SND_SOC_DAPM_POST_PMD:
		if (!rt5682->jack_type) {
			if (!snd_soc_dapm_get_pin_status(w->dapm, "MICBIAS"))
				snd_soc_component_update_bits(component,
					RT5682_PWR_ANLG_1, RT5682_PWR_MB, 0);
			if (!snd_soc_dapm_get_pin_status(w->dapm, "Vref2"))
				snd_soc_component_update_bits(component,
					RT5682_PWR_ANLG_1, RT5682_PWR_VREF2, 0);
		}
		break;
	}

	return 0;
}

static int rt5682_set_verf(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (w->shift) {
		case RT5682_PWR_VREF1_BIT:
			snd_soc_component_update_bits(component,
				RT5682_PWR_ANLG_1, RT5682_PWR_FV1, 0);
			break;

		case RT5682_PWR_VREF2_BIT:
			snd_soc_component_update_bits(component,
				RT5682_PWR_ANLG_1, RT5682_PWR_FV2, 0);
			break;
		}
		break;

	case SND_SOC_DAPM_POST_PMU:
		usleep_range(15000, 20000);
		switch (w->shift) {
		case RT5682_PWR_VREF1_BIT:
			snd_soc_component_update_bits(component,
				RT5682_PWR_ANLG_1, RT5682_PWR_FV1,
				RT5682_PWR_FV1);
			break;

		case RT5682_PWR_VREF2_BIT:
			snd_soc_component_update_bits(component,
				RT5682_PWR_ANLG_1, RT5682_PWR_FV2,
				RT5682_PWR_FV2);
			break;
		}
		break;
	}

	return 0;
}

static const unsigned int rt5682_adcdat_pin_values[] = {
	1,
	3,
};

static const char * const rt5682_adcdat_pin_select[] = {
	"ADCDAT1",
	"ADCDAT2",
};

static SOC_VALUE_ENUM_SINGLE_DECL(rt5682_adcdat_pin_enum,
	RT5682_GPIO_CTRL_1, RT5682_GP4_PIN_SFT, RT5682_GP4_PIN_MASK,
	rt5682_adcdat_pin_select, rt5682_adcdat_pin_values);

static const struct snd_kcontrol_new rt5682_adcdat_pin_ctrl =
	SOC_DAPM_ENUM("ADCDAT", rt5682_adcdat_pin_enum);

static const unsigned int rt5682_hpo_sig_out_values[] = {
	2,
	7,
};

static const char * const rt5682_hpo_sig_out_mode[] = {
	"Legacy",
	"OneBit",
};

static SOC_VALUE_ENUM_SINGLE_DECL(rt5682_hpo_sig_out_enum,
	RT5682_HP_LOGIC_CTRL_2, 0, RT5682_HP_LC2_SIG_SOUR1_MASK,
	rt5682_hpo_sig_out_mode, rt5682_hpo_sig_out_values);

static const struct snd_kcontrol_new rt5682_hpo_sig_demux =
	SOC_DAPM_ENUM("HPO Signal Demux", rt5682_hpo_sig_out_enum);

static const struct snd_soc_dapm_widget rt5682_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("LDO2", RT5682_PWR_ANLG_3, RT5682_PWR_LDO2_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL1", RT5682_PWR_ANLG_3, RT5682_PWR_PLL_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2B", RT5682_PWR_ANLG_3, RT5682_PWR_PLL2B_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2F", RT5682_PWR_ANLG_3, RT5682_PWR_PLL2F_BIT,
		0, set_filter_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("Vref1", RT5682_PWR_ANLG_1, RT5682_PWR_VREF1_BIT, 0,
		rt5682_set_verf, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("Vref2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* ASRC */
	SND_SOC_DAPM_SUPPLY_S("DAC STO1 ASRC", 1, RT5682_PLL_TRACK_1,
		RT5682_DAC_STO1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO1 ASRC", 1, RT5682_PLL_TRACK_1,
		RT5682_ADC_STO1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AD ASRC", 1, RT5682_PLL_TRACK_1,
		RT5682_AD_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DA ASRC", 1, RT5682_PLL_TRACK_1,
		RT5682_DA_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC ASRC", 1, RT5682_PLL_TRACK_1,
		RT5682_DMIC_ASRC_SFT, 0, NULL, 0),

	/* Input Side */
	SND_SOC_DAPM_SUPPLY("MICBIAS1", RT5682_PWR_ANLG_2, RT5682_PWR_MB1_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS2", RT5682_PWR_ANLG_2, RT5682_PWR_MB2_BIT,
		0, NULL, 0),

	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),

	SND_SOC_DAPM_INPUT("IN1P"),

	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
		set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC1 Power", RT5682_DMIC_CTRL_1,
		RT5682_DMIC_1_EN_SFT, 0, set_dmic_power,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	/* Boost */
	SND_SOC_DAPM_PGA("BST1 CBJ", SND_SOC_NOPM,
		0, 0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIX1L", SND_SOC_NOPM, 0, 0, rt5682_rec1_l_mix,
		ARRAY_SIZE(rt5682_rec1_l_mix)),
	SND_SOC_DAPM_SUPPLY("RECMIX1L Power", RT5682_PWR_ANLG_2,
		RT5682_PWR_RM1_L_BIT, 0, NULL, 0),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC1 L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC1 R", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY("ADC1 L Power", RT5682_PWR_DIG_1,
		RT5682_PWR_ADC_L1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1 R Power", RT5682_PWR_DIG_1,
		RT5682_PWR_ADC_R1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1 clock", RT5682_CHOP_ADC,
		RT5682_CKGEN_ADC1_SFT, 0, NULL, 0),

	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo1 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_sto1_adc1l_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_sto1_adc1r_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_sto1_adc2l_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_sto1_adc2r_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_sto1_adcl_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_sto1_adcr_mux),
	SND_SOC_DAPM_MUX("IF1_ADC Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_if1_adc_slot_mux),

	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("ADC Stereo1 Filter", RT5682_PWR_DIG_2,
		RT5682_PWR_ADC_S1F_BIT, 0, set_filter_clk,
		SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXL", RT5682_STO1_ADC_DIG_VOL,
		RT5682_L_MUTE_SFT, 1, rt5682_sto1_adc_l_mix,
		ARRAY_SIZE(rt5682_sto1_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXR", RT5682_STO1_ADC_DIG_VOL,
		RT5682_R_MUTE_SFT, 1, rt5682_sto1_adc_r_mix,
		ARRAY_SIZE(rt5682_sto1_adc_r_mix)),

	/* ADC PGA */
	SND_SOC_DAPM_PGA("Stereo1 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT5682_PWR_DIG_1, RT5682_PWR_I2S1_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S2", RT5682_PWR_DIG_1, RT5682_PWR_I2S2_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SOUND DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SOUND DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface Select */
	SND_SOC_DAPM_MUX("IF1 01 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_if1_01_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1 23 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_if1_23_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1 45 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_if1_45_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1 67 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_if1_67_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF2 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_if2_adc_swap_mux),

	SND_SOC_DAPM_MUX("ADCDAT Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_adcdat_pin_ctrl),

	SND_SOC_DAPM_MUX("DAC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_dac_l_mux),
	SND_SOC_DAPM_MUX("DAC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5682_dac_r_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0,
		RT5682_I2S1_SDP, RT5682_SEL_ADCDAT_SFT, 1),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0,
		RT5682_I2S2_SDP, RT5682_I2S2_PIN_CFG_SFT, 1),
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SDWRX", "SDW Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SDWTX", "SDW Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC1 MIXL", SND_SOC_NOPM, 0, 0,
		rt5682_dac_l_mix, ARRAY_SIZE(rt5682_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC1 MIXR", SND_SOC_NOPM, 0, 0,
		rt5682_dac_r_mix, ARRAY_SIZE(rt5682_dac_r_mix)),

	/* DAC channel Mux */
	SND_SOC_DAPM_MUX("DAC L1 Source", SND_SOC_NOPM, 0, 0,
		&rt5682_alg_dac_l1_mux),
	SND_SOC_DAPM_MUX("DAC R1 Source", SND_SOC_NOPM, 0, 0,
		&rt5682_alg_dac_r1_mux),

	/* DAC Mixer */
	SND_SOC_DAPM_SUPPLY("DAC Stereo1 Filter", RT5682_PWR_DIG_2,
		RT5682_PWR_DAC_S1F_BIT, 0, set_filter_clk,
		SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MIXER("Stereo1 DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5682_sto1_dac_l_mix, ARRAY_SIZE(rt5682_sto1_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo1 DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5682_sto1_dac_r_mix, ARRAY_SIZE(rt5682_sto1_dac_r_mix)),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC L1", NULL, RT5682_PWR_DIG_1,
		RT5682_PWR_DAC_L1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, RT5682_PWR_DIG_1,
		RT5682_PWR_DAC_R1_BIT, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC 1 Clock", 3, RT5682_CHOP_DAC,
		RT5682_CKGEN_DAC1_SFT, 0, NULL, 0),

	/* HPO */
	SND_SOC_DAPM_PGA_S("HP Amp", 1, SND_SOC_NOPM, 0, 0, rt5682_hp_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_SUPPLY("HP Amp L", RT5682_PWR_ANLG_1,
		RT5682_PWR_HA_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("HP Amp R", RT5682_PWR_ANLG_1,
		RT5682_PWR_HA_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("Charge Pump", 1, RT5682_DEPOP_1,
		RT5682_PUMP_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("Capless", 2, RT5682_DEPOP_1,
		RT5682_CAPLESS_EN_SFT, 0, NULL, 0),

	SND_SOC_DAPM_SWITCH("HPOL Playback", SND_SOC_NOPM, 0, 0,
		&hpol_switch),
	SND_SOC_DAPM_SWITCH("HPOR Playback", SND_SOC_NOPM, 0, 0,
		&hpor_switch),

	SND_SOC_DAPM_OUT_DRV("HPO Legacy", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("HPO OneBit", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_DEMUX("HPO Signal Demux", SND_SOC_NOPM, 0, 0, &rt5682_hpo_sig_demux),

	/* CLK DET */
	SND_SOC_DAPM_SUPPLY("CLKDET SYS", RT5682_CLK_DET,
		RT5682_SYS_CLK_DET_SFT,	0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKDET PLL1", RT5682_CLK_DET,
		RT5682_PLL1_CLK_DET_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKDET PLL2", RT5682_CLK_DET,
		RT5682_PLL2_CLK_DET_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKDET", RT5682_CLK_DET,
		RT5682_POW_CLK_DET_SFT, 0, NULL, 0),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_route rt5682_dapm_routes[] = {
	/*PLL*/
	{"ADC Stereo1 Filter", NULL, "PLL1", is_sys_clk_from_pll1},
	{"ADC Stereo1 Filter", NULL, "PLL2B", is_sys_clk_from_pll2},
	{"ADC Stereo1 Filter", NULL, "PLL2F", is_sys_clk_from_pll2},
	{"DAC Stereo1 Filter", NULL, "PLL1", is_sys_clk_from_pll1},
	{"DAC Stereo1 Filter", NULL, "PLL2B", is_sys_clk_from_pll2},
	{"DAC Stereo1 Filter", NULL, "PLL2F", is_sys_clk_from_pll2},

	/*ASRC*/
	{"ADC Stereo1 Filter", NULL, "ADC STO1 ASRC", is_using_asrc},
	{"DAC Stereo1 Filter", NULL, "DAC STO1 ASRC", is_using_asrc},
	{"ADC STO1 ASRC", NULL, "AD ASRC"},
	{"ADC STO1 ASRC", NULL, "DA ASRC"},
	{"ADC STO1 ASRC", NULL, "CLKDET"},
	{"DAC STO1 ASRC", NULL, "AD ASRC"},
	{"DAC STO1 ASRC", NULL, "DA ASRC"},
	{"DAC STO1 ASRC", NULL, "CLKDET"},

	/*Vref*/
	{"MICBIAS1", NULL, "Vref1"},
	{"MICBIAS2", NULL, "Vref1"},

	{"CLKDET SYS", NULL, "CLKDET"},

	{"BST1 CBJ", NULL, "IN1P"},

	{"RECMIX1L", "CBJ Switch", "BST1 CBJ"},
	{"RECMIX1L", NULL, "RECMIX1L Power"},

	{"ADC1 L", NULL, "RECMIX1L"},
	{"ADC1 L", NULL, "ADC1 L Power"},
	{"ADC1 L", NULL, "ADC1 clock"},

	{"DMIC L1", NULL, "DMIC CLK"},
	{"DMIC L1", NULL, "DMIC1 Power"},
	{"DMIC R1", NULL, "DMIC CLK"},
	{"DMIC R1", NULL, "DMIC1 Power"},
	{"DMIC CLK", NULL, "DMIC ASRC"},

	{"Stereo1 ADC L Mux", "ADC1 L", "ADC1 L"},
	{"Stereo1 ADC L Mux", "ADC1 R", "ADC1 R"},
	{"Stereo1 ADC R Mux", "ADC1 L", "ADC1 L"},
	{"Stereo1 ADC R Mux", "ADC1 R", "ADC1 R"},

	{"Stereo1 ADC L1 Mux", "ADC", "Stereo1 ADC L Mux"},
	{"Stereo1 ADC L1 Mux", "DAC MIX", "Stereo1 DAC MIXL"},
	{"Stereo1 ADC L2 Mux", "DMIC", "DMIC L1"},
	{"Stereo1 ADC L2 Mux", "DAC MIX", "Stereo1 DAC MIXL"},

	{"Stereo1 ADC R1 Mux", "ADC", "Stereo1 ADC R Mux"},
	{"Stereo1 ADC R1 Mux", "DAC MIX", "Stereo1 DAC MIXR"},
	{"Stereo1 ADC R2 Mux", "DMIC", "DMIC R1"},
	{"Stereo1 ADC R2 Mux", "DAC MIX", "Stereo1 DAC MIXR"},

	{"Stereo1 ADC MIXL", "ADC1 Switch", "Stereo1 ADC L1 Mux"},
	{"Stereo1 ADC MIXL", "ADC2 Switch", "Stereo1 ADC L2 Mux"},
	{"Stereo1 ADC MIXL", NULL, "ADC Stereo1 Filter"},

	{"Stereo1 ADC MIXR", "ADC1 Switch", "Stereo1 ADC R1 Mux"},
	{"Stereo1 ADC MIXR", "ADC2 Switch", "Stereo1 ADC R2 Mux"},
	{"Stereo1 ADC MIXR", NULL, "ADC Stereo1 Filter"},

	{"Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXL"},
	{"Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXR"},

	{"IF1 01 ADC Swap Mux", "L/R", "Stereo1 ADC MIX"},
	{"IF1 01 ADC Swap Mux", "L/L", "Stereo1 ADC MIX"},
	{"IF1 01 ADC Swap Mux", "R/L", "Stereo1 ADC MIX"},
	{"IF1 01 ADC Swap Mux", "R/R", "Stereo1 ADC MIX"},
	{"IF1 23 ADC Swap Mux", "L/R", "Stereo1 ADC MIX"},
	{"IF1 23 ADC Swap Mux", "R/L", "Stereo1 ADC MIX"},
	{"IF1 23 ADC Swap Mux", "L/L", "Stereo1 ADC MIX"},
	{"IF1 23 ADC Swap Mux", "R/R", "Stereo1 ADC MIX"},
	{"IF1 45 ADC Swap Mux", "L/R", "Stereo1 ADC MIX"},
	{"IF1 45 ADC Swap Mux", "R/L", "Stereo1 ADC MIX"},
	{"IF1 45 ADC Swap Mux", "L/L", "Stereo1 ADC MIX"},
	{"IF1 45 ADC Swap Mux", "R/R", "Stereo1 ADC MIX"},
	{"IF1 67 ADC Swap Mux", "L/R", "Stereo1 ADC MIX"},
	{"IF1 67 ADC Swap Mux", "R/L", "Stereo1 ADC MIX"},
	{"IF1 67 ADC Swap Mux", "L/L", "Stereo1 ADC MIX"},
	{"IF1 67 ADC Swap Mux", "R/R", "Stereo1 ADC MIX"},

	{"IF1_ADC Mux", "Slot 0", "IF1 01 ADC Swap Mux"},
	{"IF1_ADC Mux", "Slot 2", "IF1 23 ADC Swap Mux"},
	{"IF1_ADC Mux", "Slot 4", "IF1 45 ADC Swap Mux"},
	{"IF1_ADC Mux", "Slot 6", "IF1 67 ADC Swap Mux"},
	{"ADCDAT Mux", "ADCDAT1", "IF1_ADC Mux"},
	{"AIF1TX", NULL, "I2S1"},
	{"AIF1TX", NULL, "ADCDAT Mux"},
	{"IF2 ADC Swap Mux", "L/R", "Stereo1 ADC MIX"},
	{"IF2 ADC Swap Mux", "R/L", "Stereo1 ADC MIX"},
	{"IF2 ADC Swap Mux", "L/L", "Stereo1 ADC MIX"},
	{"IF2 ADC Swap Mux", "R/R", "Stereo1 ADC MIX"},
	{"ADCDAT Mux", "ADCDAT2", "IF2 ADC Swap Mux"},
	{"AIF2TX", NULL, "ADCDAT Mux"},

	{"SDWTX", NULL, "PLL2B"},
	{"SDWTX", NULL, "PLL2F"},
	{"SDWTX", NULL, "ADCDAT Mux"},

	{"IF1 DAC1 L", NULL, "AIF1RX"},
	{"IF1 DAC1 L", NULL, "I2S1"},
	{"IF1 DAC1 L", NULL, "DAC Stereo1 Filter"},
	{"IF1 DAC1 R", NULL, "AIF1RX"},
	{"IF1 DAC1 R", NULL, "I2S1"},
	{"IF1 DAC1 R", NULL, "DAC Stereo1 Filter"},

	{"SOUND DAC L", NULL, "SDWRX"},
	{"SOUND DAC L", NULL, "DAC Stereo1 Filter"},
	{"SOUND DAC L", NULL, "PLL2B"},
	{"SOUND DAC L", NULL, "PLL2F"},
	{"SOUND DAC R", NULL, "SDWRX"},
	{"SOUND DAC R", NULL, "DAC Stereo1 Filter"},
	{"SOUND DAC R", NULL, "PLL2B"},
	{"SOUND DAC R", NULL, "PLL2F"},

	{"DAC L Mux", "IF1", "IF1 DAC1 L"},
	{"DAC L Mux", "SOUND", "SOUND DAC L"},
	{"DAC R Mux", "IF1", "IF1 DAC1 R"},
	{"DAC R Mux", "SOUND", "SOUND DAC R"},

	{"DAC1 MIXL", "Stereo ADC Switch", "Stereo1 ADC MIXL"},
	{"DAC1 MIXL", "DAC1 Switch", "DAC L Mux"},
	{"DAC1 MIXR", "Stereo ADC Switch", "Stereo1 ADC MIXR"},
	{"DAC1 MIXR", "DAC1 Switch", "DAC R Mux"},

	{"Stereo1 DAC MIXL", "DAC L1 Switch", "DAC1 MIXL"},
	{"Stereo1 DAC MIXL", "DAC R1 Switch", "DAC1 MIXR"},

	{"Stereo1 DAC MIXR", "DAC R1 Switch", "DAC1 MIXR"},
	{"Stereo1 DAC MIXR", "DAC L1 Switch", "DAC1 MIXL"},

	{"DAC L1 Source", "DAC1", "DAC1 MIXL"},
	{"DAC L1 Source", "Stereo1 DAC Mixer", "Stereo1 DAC MIXL"},
	{"DAC R1 Source", "DAC1", "DAC1 MIXR"},
	{"DAC R1 Source", "Stereo1 DAC Mixer", "Stereo1 DAC MIXR"},

	{"DAC L1", NULL, "DAC L1 Source"},
	{"DAC R1", NULL, "DAC R1 Source"},

	{"DAC L1", NULL, "DAC 1 Clock"},
	{"DAC R1", NULL, "DAC 1 Clock"},

	{"HP Amp", NULL, "DAC L1"},
	{"HP Amp", NULL, "DAC R1"},
	{"HP Amp", NULL, "HP Amp L"},
	{"HP Amp", NULL, "HP Amp R"},
	{"HP Amp", NULL, "Capless"},
	{"HP Amp", NULL, "Charge Pump"},
	{"HP Amp", NULL, "CLKDET SYS"},
	{"HP Amp", NULL, "Vref1"},

	{"HPO Signal Demux", NULL, "HP Amp"},

	{"HPO Legacy", "Legacy", "HPO Signal Demux"},
	{"HPO OneBit", "OneBit", "HPO Signal Demux"},

	{"HPOL Playback", "Switch", "HPO Legacy"},
	{"HPOR Playback", "Switch", "HPO Legacy"},

	{"HPOL", NULL, "HPOL Playback"},
	{"HPOR", NULL, "HPOR Playback"},
	{"HPOL", NULL, "HPO OneBit"},
	{"HPOR", NULL, "HPO OneBit"},
};

static int rt5682_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
		unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	unsigned int cl, val = 0;

	if (tx_mask || rx_mask)
		snd_soc_component_update_bits(component, RT5682_TDM_ADDA_CTRL_2,
			RT5682_TDM_EN, RT5682_TDM_EN);
	else
		snd_soc_component_update_bits(component, RT5682_TDM_ADDA_CTRL_2,
			RT5682_TDM_EN, 0);

	switch (slots) {
	case 4:
		val |= RT5682_TDM_TX_CH_4;
		val |= RT5682_TDM_RX_CH_4;
		break;
	case 6:
		val |= RT5682_TDM_TX_CH_6;
		val |= RT5682_TDM_RX_CH_6;
		break;
	case 8:
		val |= RT5682_TDM_TX_CH_8;
		val |= RT5682_TDM_RX_CH_8;
		break;
	case 2:
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT5682_TDM_CTRL,
		RT5682_TDM_TX_CH_MASK | RT5682_TDM_RX_CH_MASK, val);

	switch (slot_width) {
	case 8:
		if (tx_mask || rx_mask)
			return -EINVAL;
		cl = RT5682_I2S1_TX_CHL_8 | RT5682_I2S1_RX_CHL_8;
		break;
	case 16:
		val = RT5682_TDM_CL_16;
		cl = RT5682_I2S1_TX_CHL_16 | RT5682_I2S1_RX_CHL_16;
		break;
	case 20:
		val = RT5682_TDM_CL_20;
		cl = RT5682_I2S1_TX_CHL_20 | RT5682_I2S1_RX_CHL_20;
		break;
	case 24:
		val = RT5682_TDM_CL_24;
		cl = RT5682_I2S1_TX_CHL_24 | RT5682_I2S1_RX_CHL_24;
		break;
	case 32:
		val = RT5682_TDM_CL_32;
		cl = RT5682_I2S1_TX_CHL_32 | RT5682_I2S1_RX_CHL_32;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT5682_TDM_TCON_CTRL,
		RT5682_TDM_CL_MASK, val);
	snd_soc_component_update_bits(component, RT5682_I2S1_SDP,
		RT5682_I2S1_TX_CHL_MASK | RT5682_I2S1_RX_CHL_MASK, cl);

	return 0;
}

static int rt5682_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	unsigned int len_1 = 0, len_2 = 0;
	int pre_div, frame_size;

	rt5682->lrck[dai->id] = params_rate(params);
	pre_div = rl6231_get_clk_info(rt5682->sysclk, rt5682->lrck[dai->id]);

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n",
			frame_size);
		return -EINVAL;
	}

	dev_dbg(dai->dev, "lrck is %dHz and pre_div is %d for iis %d\n",
		rt5682->lrck[dai->id], pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		len_1 |= RT5682_I2S1_DL_20;
		len_2 |= RT5682_I2S2_DL_20;
		break;
	case 24:
		len_1 |= RT5682_I2S1_DL_24;
		len_2 |= RT5682_I2S2_DL_24;
		break;
	case 32:
		len_1 |= RT5682_I2S1_DL_32;
		len_2 |= RT5682_I2S2_DL_24;
		break;
	case 8:
		len_1 |= RT5682_I2S2_DL_8;
		len_2 |= RT5682_I2S2_DL_8;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5682_AIF1:
		snd_soc_component_update_bits(component, RT5682_I2S1_SDP,
			RT5682_I2S1_DL_MASK, len_1);
		if (rt5682->master[RT5682_AIF1]) {
			snd_soc_component_update_bits(component,
				RT5682_ADDA_CLK_1, RT5682_I2S_M_DIV_MASK |
				RT5682_I2S_CLK_SRC_MASK,
				pre_div << RT5682_I2S_M_DIV_SFT |
				(rt5682->sysclk_src) << RT5682_I2S_CLK_SRC_SFT);
		}
		if (params_channels(params) == 1) /* mono mode */
			snd_soc_component_update_bits(component,
				RT5682_I2S1_SDP, RT5682_I2S1_MONO_MASK,
				RT5682_I2S1_MONO_EN);
		else
			snd_soc_component_update_bits(component,
				RT5682_I2S1_SDP, RT5682_I2S1_MONO_MASK,
				RT5682_I2S1_MONO_DIS);
		break;
	case RT5682_AIF2:
		snd_soc_component_update_bits(component, RT5682_I2S2_SDP,
			RT5682_I2S2_DL_MASK, len_2);
		if (rt5682->master[RT5682_AIF2]) {
			snd_soc_component_update_bits(component,
				RT5682_I2S_M_CLK_CTRL_1, RT5682_I2S2_M_PD_MASK,
				pre_div << RT5682_I2S2_M_PD_SFT);
		}
		if (params_channels(params) == 1) /* mono mode */
			snd_soc_component_update_bits(component,
				RT5682_I2S2_SDP, RT5682_I2S2_MONO_MASK,
				RT5682_I2S2_MONO_EN);
		else
			snd_soc_component_update_bits(component,
				RT5682_I2S2_SDP, RT5682_I2S2_MONO_MASK,
				RT5682_I2S2_MONO_DIS);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	return 0;
}

static int rt5682_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0, tdm_ctrl = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5682->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		rt5682->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5682_I2S_BP_INV;
		tdm_ctrl |= RT5682_TDM_S_BP_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		if (dai->id == RT5682_AIF1)
			tdm_ctrl |= RT5682_TDM_S_LP_INV | RT5682_TDM_M_BP_INV;
		else
			return -EINVAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		if (dai->id == RT5682_AIF1)
			tdm_ctrl |= RT5682_TDM_S_BP_INV | RT5682_TDM_S_LP_INV |
				    RT5682_TDM_M_BP_INV | RT5682_TDM_M_LP_INV;
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5682_I2S_DF_LEFT;
		tdm_ctrl |= RT5682_TDM_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5682_I2S_DF_PCM_A;
		tdm_ctrl |= RT5682_TDM_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5682_I2S_DF_PCM_B;
		tdm_ctrl |= RT5682_TDM_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5682_AIF1:
		snd_soc_component_update_bits(component, RT5682_I2S1_SDP,
			RT5682_I2S_DF_MASK, reg_val);
		snd_soc_component_update_bits(component, RT5682_TDM_TCON_CTRL,
			RT5682_TDM_MS_MASK | RT5682_TDM_S_BP_MASK |
			RT5682_TDM_DF_MASK | RT5682_TDM_M_BP_MASK |
			RT5682_TDM_M_LP_MASK | RT5682_TDM_S_LP_MASK,
			tdm_ctrl | rt5682->master[dai->id]);
		break;
	case RT5682_AIF2:
		if (rt5682->master[dai->id] == 0)
			reg_val |= RT5682_I2S2_MS_S;
		snd_soc_component_update_bits(component, RT5682_I2S2_SDP,
			RT5682_I2S2_MS_MASK | RT5682_I2S_BP_MASK |
			RT5682_I2S_DF_MASK, reg_val);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}
	return 0;
}

static int rt5682_set_component_sysclk(struct snd_soc_component *component,
		int clk_id, int source, unsigned int freq, int dir)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0, src = 0;

	if (freq == rt5682->sysclk && clk_id == rt5682->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5682_SCLK_S_MCLK:
		reg_val |= RT5682_SCLK_SRC_MCLK;
		src = RT5682_CLK_SRC_MCLK;
		break;
	case RT5682_SCLK_S_PLL1:
		reg_val |= RT5682_SCLK_SRC_PLL1;
		src = RT5682_CLK_SRC_PLL1;
		break;
	case RT5682_SCLK_S_PLL2:
		reg_val |= RT5682_SCLK_SRC_PLL2;
		src = RT5682_CLK_SRC_PLL2;
		break;
	case RT5682_SCLK_S_RCCLK:
		reg_val |= RT5682_SCLK_SRC_RCCLK;
		src = RT5682_CLK_SRC_RCCLK;
		break;
	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, RT5682_GLB_CLK,
		RT5682_SCLK_SRC_MASK, reg_val);

	if (rt5682->master[RT5682_AIF2]) {
		snd_soc_component_update_bits(component,
			RT5682_I2S_M_CLK_CTRL_1, RT5682_I2S2_SRC_MASK,
			src << RT5682_I2S2_SRC_SFT);
	}

	rt5682->sysclk = freq;
	rt5682->sysclk_src = clk_id;

	dev_dbg(component->dev, "Sysclk is %dHz and clock id is %d\n",
		freq, clk_id);

	return 0;
}

static int rt5682_set_component_pll(struct snd_soc_component *component,
		int pll_id, int source, unsigned int freq_in,
		unsigned int freq_out)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code, pll2f_code, pll2b_code;
	unsigned int pll2_fout1, pll2_ps_val;
	int ret;

	if (source == rt5682->pll_src[pll_id] &&
	    freq_in == rt5682->pll_in[pll_id] &&
	    freq_out == rt5682->pll_out[pll_id])
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt5682->pll_in[pll_id] = 0;
		rt5682->pll_out[pll_id] = 0;
		snd_soc_component_update_bits(component, RT5682_GLB_CLK,
			RT5682_SCLK_SRC_MASK, RT5682_SCLK_SRC_MCLK);
		return 0;
	}

	if (pll_id == RT5682_PLL2) {
		switch (source) {
		case RT5682_PLL2_S_MCLK:
			snd_soc_component_update_bits(component,
				RT5682_GLB_CLK, RT5682_PLL2_SRC_MASK,
				RT5682_PLL2_SRC_MCLK);
			break;
		default:
			dev_err(component->dev, "Unknown PLL2 Source %d\n",
				source);
			return -EINVAL;
		}

		/**
		 * PLL2 concatenates 2 PLL units.
		 * We suggest the Fout of the front PLL is 3.84MHz.
		 */
		pll2_fout1 = 3840000;
		ret = rl6231_pll_calc(freq_in, pll2_fout1, &pll2f_code);
		if (ret < 0) {
			dev_err(component->dev, "Unsupported input clock %d\n",
				freq_in);
			return ret;
		}
		dev_dbg(component->dev, "PLL2F: fin=%d fout=%d bypass=%d m=%d n=%d k=%d\n",
			freq_in, pll2_fout1,
			pll2f_code.m_bp,
			(pll2f_code.m_bp ? 0 : pll2f_code.m_code),
			pll2f_code.n_code, pll2f_code.k_code);

		ret = rl6231_pll_calc(pll2_fout1, freq_out, &pll2b_code);
		if (ret < 0) {
			dev_err(component->dev, "Unsupported input clock %d\n",
				pll2_fout1);
			return ret;
		}
		dev_dbg(component->dev, "PLL2B: fin=%d fout=%d bypass=%d m=%d n=%d k=%d\n",
			pll2_fout1, freq_out,
			pll2b_code.m_bp,
			(pll2b_code.m_bp ? 0 : pll2b_code.m_code),
			pll2b_code.n_code, pll2b_code.k_code);

		snd_soc_component_write(component, RT5682_PLL2_CTRL_1,
			pll2f_code.k_code << RT5682_PLL2F_K_SFT |
			pll2b_code.k_code << RT5682_PLL2B_K_SFT |
			pll2b_code.m_code);
		snd_soc_component_write(component, RT5682_PLL2_CTRL_2,
			pll2f_code.m_code << RT5682_PLL2F_M_SFT |
			pll2b_code.n_code);
		snd_soc_component_write(component, RT5682_PLL2_CTRL_3,
			pll2f_code.n_code << RT5682_PLL2F_N_SFT);

		if (freq_out == 22579200)
			pll2_ps_val = 1 << RT5682_PLL2B_SEL_PS_SFT;
		else
			pll2_ps_val = 1 << RT5682_PLL2B_PS_BYP_SFT;
		snd_soc_component_update_bits(component, RT5682_PLL2_CTRL_4,
			RT5682_PLL2B_SEL_PS_MASK | RT5682_PLL2B_PS_BYP_MASK |
			RT5682_PLL2B_M_BP_MASK | RT5682_PLL2F_M_BP_MASK | 0xf,
			pll2_ps_val |
			(pll2b_code.m_bp ? 1 : 0) << RT5682_PLL2B_M_BP_SFT |
			(pll2f_code.m_bp ? 1 : 0) << RT5682_PLL2F_M_BP_SFT |
			0xf);
	} else {
		switch (source) {
		case RT5682_PLL1_S_MCLK:
			snd_soc_component_update_bits(component,
				RT5682_GLB_CLK, RT5682_PLL1_SRC_MASK,
				RT5682_PLL1_SRC_MCLK);
			break;
		case RT5682_PLL1_S_BCLK1:
			snd_soc_component_update_bits(component,
				RT5682_GLB_CLK, RT5682_PLL1_SRC_MASK,
				RT5682_PLL1_SRC_BCLK1);
			break;
		default:
			dev_err(component->dev, "Unknown PLL1 Source %d\n",
				source);
			return -EINVAL;
		}

		ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
		if (ret < 0) {
			dev_err(component->dev, "Unsupported input clock %d\n",
				freq_in);
			return ret;
		}

		dev_dbg(component->dev, "bypass=%d m=%d n=%d k=%d\n",
			pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
			pll_code.n_code, pll_code.k_code);

		snd_soc_component_write(component, RT5682_PLL_CTRL_1,
			(pll_code.n_code << RT5682_PLL_N_SFT) | pll_code.k_code);
		snd_soc_component_write(component, RT5682_PLL_CTRL_2,
			((pll_code.m_bp ? 0 : pll_code.m_code) << RT5682_PLL_M_SFT) |
			((pll_code.m_bp << RT5682_PLL_M_BP_SFT) | RT5682_PLL_RST));
	}

	rt5682->pll_in[pll_id] = freq_in;
	rt5682->pll_out[pll_id] = freq_out;
	rt5682->pll_src[pll_id] = source;

	return 0;
}

static int rt5682_set_bclk1_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);

	rt5682->bclk[dai->id] = ratio;

	switch (ratio) {
	case 256:
		snd_soc_component_update_bits(component, RT5682_TDM_TCON_CTRL,
			RT5682_TDM_BCLK_MS1_MASK, RT5682_TDM_BCLK_MS1_256);
		break;
	case 128:
		snd_soc_component_update_bits(component, RT5682_TDM_TCON_CTRL,
			RT5682_TDM_BCLK_MS1_MASK, RT5682_TDM_BCLK_MS1_128);
		break;
	case 64:
		snd_soc_component_update_bits(component, RT5682_TDM_TCON_CTRL,
			RT5682_TDM_BCLK_MS1_MASK, RT5682_TDM_BCLK_MS1_64);
		break;
	case 32:
		snd_soc_component_update_bits(component, RT5682_TDM_TCON_CTRL,
			RT5682_TDM_BCLK_MS1_MASK, RT5682_TDM_BCLK_MS1_32);
		break;
	default:
		dev_err(dai->dev, "Invalid bclk1 ratio %d\n", ratio);
		return -EINVAL;
	}

	return 0;
}

static int rt5682_set_bclk2_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);

	rt5682->bclk[dai->id] = ratio;

	switch (ratio) {
	case 64:
		snd_soc_component_update_bits(component, RT5682_ADDA_CLK_2,
			RT5682_I2S2_BCLK_MS2_MASK,
			RT5682_I2S2_BCLK_MS2_64);
		break;
	case 32:
		snd_soc_component_update_bits(component, RT5682_ADDA_CLK_2,
			RT5682_I2S2_BCLK_MS2_MASK,
			RT5682_I2S2_BCLK_MS2_32);
		break;
	default:
		dev_err(dai->dev, "Invalid bclk2 ratio %d\n", ratio);
		return -EINVAL;
	}

	return 0;
}

static int rt5682_set_bias_level(struct snd_soc_component *component,
		enum snd_soc_bias_level level)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		regmap_update_bits(rt5682->regmap, RT5682_PWR_ANLG_1,
			RT5682_PWR_BG, RT5682_PWR_BG);
		regmap_update_bits(rt5682->regmap, RT5682_PWR_DIG_1,
			RT5682_DIG_GATE_CTRL | RT5682_PWR_LDO,
			RT5682_DIG_GATE_CTRL | RT5682_PWR_LDO);
		break;

	case SND_SOC_BIAS_STANDBY:
		regmap_update_bits(rt5682->regmap, RT5682_PWR_DIG_1,
			RT5682_DIG_GATE_CTRL, RT5682_DIG_GATE_CTRL);
		break;
	case SND_SOC_BIAS_OFF:
		regmap_update_bits(rt5682->regmap, RT5682_PWR_DIG_1,
			RT5682_DIG_GATE_CTRL | RT5682_PWR_LDO, 0);
		regmap_update_bits(rt5682->regmap, RT5682_PWR_ANLG_1,
			RT5682_PWR_BG, 0);
		break;
	case SND_SOC_BIAS_ON:
		break;
	}

	return 0;
}

#ifdef CONFIG_COMMON_CLK
#define CLK_PLL2_FIN 48000000
#define CLK_48 48000
#define CLK_44 44100

static bool rt5682_clk_check(struct rt5682_priv *rt5682)
{
	if (!rt5682->master[RT5682_AIF1]) {
		dev_dbg(rt5682->i2c_dev, "sysclk/dai not set correctly\n");
		return false;
	}
	return true;
}

static int rt5682_wclk_prepare(struct clk_hw *hw)
{
	struct rt5682_priv *rt5682 =
		container_of(hw, struct rt5682_priv,
			     dai_clks_hw[RT5682_DAI_WCLK_IDX]);
	struct snd_soc_component *component;
	struct snd_soc_dapm_context *dapm;

	if (!rt5682_clk_check(rt5682))
		return -EINVAL;

	component = rt5682->component;
	dapm = snd_soc_component_get_dapm(component);

	snd_soc_dapm_mutex_lock(dapm);

	snd_soc_dapm_force_enable_pin_unlocked(dapm, "MICBIAS");
	snd_soc_component_update_bits(component, RT5682_PWR_ANLG_1,
				RT5682_PWR_MB, RT5682_PWR_MB);

	snd_soc_dapm_force_enable_pin_unlocked(dapm, "Vref2");
	snd_soc_component_update_bits(component, RT5682_PWR_ANLG_1,
			RT5682_PWR_VREF2 | RT5682_PWR_FV2,
			RT5682_PWR_VREF2);
	usleep_range(55000, 60000);
	snd_soc_component_update_bits(component, RT5682_PWR_ANLG_1,
			RT5682_PWR_FV2, RT5682_PWR_FV2);

	snd_soc_dapm_force_enable_pin_unlocked(dapm, "I2S1");
	snd_soc_dapm_force_enable_pin_unlocked(dapm, "PLL2F");
	snd_soc_dapm_force_enable_pin_unlocked(dapm, "PLL2B");
	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);

	return 0;
}

static void rt5682_wclk_unprepare(struct clk_hw *hw)
{
	struct rt5682_priv *rt5682 =
		container_of(hw, struct rt5682_priv,
			     dai_clks_hw[RT5682_DAI_WCLK_IDX]);
	struct snd_soc_component *component;
	struct snd_soc_dapm_context *dapm;

	if (!rt5682_clk_check(rt5682))
		return;

	component = rt5682->component;
	dapm = snd_soc_component_get_dapm(component);

	snd_soc_dapm_mutex_lock(dapm);

	snd_soc_dapm_disable_pin_unlocked(dapm, "MICBIAS");
	snd_soc_dapm_disable_pin_unlocked(dapm, "Vref2");
	if (!rt5682->jack_type)
		snd_soc_component_update_bits(component, RT5682_PWR_ANLG_1,
				RT5682_PWR_VREF2 | RT5682_PWR_FV2 |
				RT5682_PWR_MB, 0);

	snd_soc_dapm_disable_pin_unlocked(dapm, "I2S1");
	snd_soc_dapm_disable_pin_unlocked(dapm, "PLL2F");
	snd_soc_dapm_disable_pin_unlocked(dapm, "PLL2B");
	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);
}

static unsigned long rt5682_wclk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct rt5682_priv *rt5682 =
		container_of(hw, struct rt5682_priv,
			     dai_clks_hw[RT5682_DAI_WCLK_IDX]);
	const char * const clk_name = clk_hw_get_name(hw);

	if (!rt5682_clk_check(rt5682))
		return 0;
	/*
	 * Only accept to set wclk rate to 44.1k or 48kHz.
	 */
	if (rt5682->lrck[RT5682_AIF1] != CLK_48 &&
	    rt5682->lrck[RT5682_AIF1] != CLK_44) {
		dev_warn(rt5682->i2c_dev, "%s: clk %s only support %d or %d Hz output\n",
			__func__, clk_name, CLK_44, CLK_48);
		return 0;
	}

	return rt5682->lrck[RT5682_AIF1];
}

static long rt5682_wclk_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct rt5682_priv *rt5682 =
		container_of(hw, struct rt5682_priv,
			     dai_clks_hw[RT5682_DAI_WCLK_IDX]);
	const char * const clk_name = clk_hw_get_name(hw);

	if (!rt5682_clk_check(rt5682))
		return -EINVAL;
	/*
	 * Only accept to set wclk rate to 44.1k or 48kHz.
	 * It will force to 48kHz if not both.
	 */
	if (rate != CLK_48 && rate != CLK_44) {
		dev_warn(rt5682->i2c_dev, "%s: clk %s only support %d or %d Hz output\n",
			__func__, clk_name, CLK_44, CLK_48);
		rate = CLK_48;
	}

	return rate;
}

static int rt5682_wclk_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct rt5682_priv *rt5682 =
		container_of(hw, struct rt5682_priv,
			     dai_clks_hw[RT5682_DAI_WCLK_IDX]);
	struct snd_soc_component *component;
	struct clk_hw *parent_hw;
	const char * const clk_name = clk_hw_get_name(hw);
	int pre_div;
	unsigned int clk_pll2_out;

	if (!rt5682_clk_check(rt5682))
		return -EINVAL;

	component = rt5682->component;

	/*
	 * Whether the wclk's parent clk (mclk) exists or not, please ensure
	 * it is fixed or set to 48MHz before setting wclk rate. It's a
	 * temporary limitation. Only accept 48MHz clk as the clk provider.
	 *
	 * It will set the codec anyway by assuming mclk is 48MHz.
	 */
	parent_hw = clk_hw_get_parent(hw);
	if (!parent_hw)
		dev_warn(rt5682->i2c_dev,
			"Parent mclk of wclk not acquired in driver. Please ensure mclk was provided as %d Hz.\n",
			CLK_PLL2_FIN);

	if (parent_rate != CLK_PLL2_FIN)
		dev_warn(rt5682->i2c_dev, "clk %s only support %d Hz input\n",
			clk_name, CLK_PLL2_FIN);

	/*
	 * To achieve the rate conversion from 48MHz to 44.1k or 48kHz,
	 * PLL2 is needed.
	 */
	clk_pll2_out = rate * 512;
	rt5682_set_component_pll(component, RT5682_PLL2, RT5682_PLL2_S_MCLK,
		CLK_PLL2_FIN, clk_pll2_out);

	rt5682_set_component_sysclk(component, RT5682_SCLK_S_PLL2, 0,
		clk_pll2_out, SND_SOC_CLOCK_IN);

	rt5682->lrck[RT5682_AIF1] = rate;

	pre_div = rl6231_get_clk_info(rt5682->sysclk, rate);

	snd_soc_component_update_bits(component, RT5682_ADDA_CLK_1,
		RT5682_I2S_M_DIV_MASK | RT5682_I2S_CLK_SRC_MASK,
		pre_div << RT5682_I2S_M_DIV_SFT |
		(rt5682->sysclk_src) << RT5682_I2S_CLK_SRC_SFT);

	return 0;
}

static unsigned long rt5682_bclk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct rt5682_priv *rt5682 =
		container_of(hw, struct rt5682_priv,
			     dai_clks_hw[RT5682_DAI_BCLK_IDX]);
	unsigned int bclks_per_wclk;

	regmap_read(rt5682->regmap, RT5682_TDM_TCON_CTRL, &bclks_per_wclk);

	switch (bclks_per_wclk & RT5682_TDM_BCLK_MS1_MASK) {
	case RT5682_TDM_BCLK_MS1_256:
		return parent_rate * 256;
	case RT5682_TDM_BCLK_MS1_128:
		return parent_rate * 128;
	case RT5682_TDM_BCLK_MS1_64:
		return parent_rate * 64;
	case RT5682_TDM_BCLK_MS1_32:
		return parent_rate * 32;
	default:
		return 0;
	}
}

static unsigned long rt5682_bclk_get_factor(unsigned long rate,
					    unsigned long parent_rate)
{
	unsigned long factor;

	factor = rate / parent_rate;
	if (factor < 64)
		return 32;
	else if (factor < 128)
		return 64;
	else if (factor < 256)
		return 128;
	else
		return 256;
}

static long rt5682_bclk_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct rt5682_priv *rt5682 =
		container_of(hw, struct rt5682_priv,
			     dai_clks_hw[RT5682_DAI_BCLK_IDX]);
	unsigned long factor;

	if (!*parent_rate || !rt5682_clk_check(rt5682))
		return -EINVAL;

	/*
	 * BCLK rates are set as a multiplier of WCLK in HW.
	 * We don't allow changing the parent WCLK. We just do
	 * some rounding down based on the parent WCLK rate
	 * and find the appropriate multiplier of BCLK to
	 * get the rounded down BCLK value.
	 */
	factor = rt5682_bclk_get_factor(rate, *parent_rate);

	return *parent_rate * factor;
}

static int rt5682_bclk_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct rt5682_priv *rt5682 =
		container_of(hw, struct rt5682_priv,
			     dai_clks_hw[RT5682_DAI_BCLK_IDX]);
	struct snd_soc_component *component;
	struct snd_soc_dai *dai;
	unsigned long factor;

	if (!rt5682_clk_check(rt5682))
		return -EINVAL;

	component = rt5682->component;

	factor = rt5682_bclk_get_factor(rate, parent_rate);

	for_each_component_dais(component, dai)
		if (dai->id == RT5682_AIF1)
			break;
	if (!dai) {
		dev_err(rt5682->i2c_dev, "dai %d not found in component\n",
			RT5682_AIF1);
		return -ENODEV;
	}

	return rt5682_set_bclk1_ratio(dai, factor);
}

static const struct clk_ops rt5682_dai_clk_ops[RT5682_DAI_NUM_CLKS] = {
	[RT5682_DAI_WCLK_IDX] = {
		.prepare = rt5682_wclk_prepare,
		.unprepare = rt5682_wclk_unprepare,
		.recalc_rate = rt5682_wclk_recalc_rate,
		.round_rate = rt5682_wclk_round_rate,
		.set_rate = rt5682_wclk_set_rate,
	},
	[RT5682_DAI_BCLK_IDX] = {
		.recalc_rate = rt5682_bclk_recalc_rate,
		.round_rate = rt5682_bclk_round_rate,
		.set_rate = rt5682_bclk_set_rate,
	},
};

int rt5682_register_dai_clks(struct rt5682_priv *rt5682)
{
	struct device *dev = rt5682->i2c_dev;
	struct rt5682_platform_data *pdata = &rt5682->pdata;
	struct clk_hw *dai_clk_hw;
	int i, ret;

	for (i = 0; i < RT5682_DAI_NUM_CLKS; ++i) {
		struct clk_init_data init = { };

		dai_clk_hw = &rt5682->dai_clks_hw[i];

		switch (i) {
		case RT5682_DAI_WCLK_IDX:
			/* Make MCLK the parent of WCLK */
			if (rt5682->mclk) {
				init.parent_data = &(struct clk_parent_data){
					.fw_name = "mclk",
				};
				init.num_parents = 1;
			}
			break;
		case RT5682_DAI_BCLK_IDX:
			/* Make WCLK the parent of BCLK */
			init.parent_hws = &(const struct clk_hw *){
				&rt5682->dai_clks_hw[RT5682_DAI_WCLK_IDX]
			};
			init.num_parents = 1;
			break;
		default:
			dev_err(dev, "Invalid clock index\n");
			return -EINVAL;
		}

		init.name = pdata->dai_clk_names[i];
		init.ops = &rt5682_dai_clk_ops[i];
		init.flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_GATE;
		dai_clk_hw->init = &init;

		ret = devm_clk_hw_register(dev, dai_clk_hw);
		if (ret) {
			dev_warn(dev, "Failed to register %s: %d\n",
				 init.name, ret);
			return ret;
		}

		if (dev->of_node) {
			devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
						    dai_clk_hw);
		} else {
			ret = devm_clk_hw_register_clkdev(dev, dai_clk_hw,
							  init.name,
							  dev_name(dev));
			if (ret)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rt5682_register_dai_clks);
#endif /* CONFIG_COMMON_CLK */

static int rt5682_probe(struct snd_soc_component *component)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	struct sdw_slave *slave;
	unsigned long time;
	struct snd_soc_dapm_context *dapm = &component->dapm;

	rt5682->component = component;

	if (rt5682->is_sdw) {
		slave = rt5682->slave;
		time = wait_for_completion_timeout(
			&slave->initialization_complete,
			msecs_to_jiffies(RT5682_PROBE_TIMEOUT));
		if (!time) {
			dev_err(&slave->dev, "Initialization not complete, timed out\n");
			return -ETIMEDOUT;
		}
	}

	snd_soc_dapm_disable_pin(dapm, "MICBIAS");
	snd_soc_dapm_disable_pin(dapm, "Vref2");
	snd_soc_dapm_sync(dapm);
	return 0;
}

static void rt5682_remove(struct snd_soc_component *component)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);

	rt5682_reset(rt5682);
}

#ifdef CONFIG_PM
static int rt5682_suspend(struct snd_soc_component *component)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	unsigned int val;

	if (rt5682->is_sdw)
		return 0;

	cancel_delayed_work_sync(&rt5682->jack_detect_work);
	cancel_delayed_work_sync(&rt5682->jd_check_work);
	if (rt5682->hs_jack && rt5682->jack_type == SND_JACK_HEADSET) {
		snd_soc_component_update_bits(component, RT5682_CBJ_CTRL_1,
			RT5682_MB1_PATH_MASK | RT5682_MB2_PATH_MASK,
			RT5682_CTRL_MB1_REG | RT5682_CTRL_MB2_REG);
		val = snd_soc_component_read(component,
				RT5682_CBJ_CTRL_2) & RT5682_JACK_TYPE_MASK;

		switch (val) {
		case 0x1:
			snd_soc_component_update_bits(component, RT5682_SAR_IL_CMD_1,
				RT5682_SAR_SEL_MB1_MASK | RT5682_SAR_SEL_MB2_MASK,
				RT5682_SAR_SEL_MB1_NOSEL | RT5682_SAR_SEL_MB2_SEL);
			break;
		case 0x2:
			snd_soc_component_update_bits(component, RT5682_SAR_IL_CMD_1,
				RT5682_SAR_SEL_MB1_MASK | RT5682_SAR_SEL_MB2_MASK,
				RT5682_SAR_SEL_MB1_SEL | RT5682_SAR_SEL_MB2_NOSEL);
			break;
		default:
			break;
		}

		/* enter SAR ADC power saving mode */
		snd_soc_component_update_bits(component, RT5682_SAR_IL_CMD_1,
			RT5682_SAR_BUTT_DET_MASK | RT5682_SAR_BUTDET_MODE_MASK |
			RT5682_SAR_BUTDET_RST_MASK | RT5682_SAR_SEL_MB1_MB2_MASK, 0);
		snd_soc_component_update_bits(component, RT5682_SAR_IL_CMD_1,
			RT5682_SAR_BUTT_DET_MASK | RT5682_SAR_BUTDET_MODE_MASK | RT5682_SAR_BUTDET_RST_MASK,
			RT5682_SAR_BUTT_DET_EN | RT5682_SAR_BUTDET_POW_SAV | RT5682_SAR_BUTDET_RST_NORMAL);
	}

	regcache_cache_only(rt5682->regmap, true);
	regcache_mark_dirty(rt5682->regmap);
	return 0;
}

static int rt5682_resume(struct snd_soc_component *component)
{
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);

	if (rt5682->is_sdw)
		return 0;

	regcache_cache_only(rt5682->regmap, false);
	regcache_sync(rt5682->regmap);

	if (rt5682->hs_jack && rt5682->jack_type == SND_JACK_HEADSET) {
		snd_soc_component_update_bits(component, RT5682_SAR_IL_CMD_1,
			RT5682_SAR_BUTDET_MODE_MASK | RT5682_SAR_SEL_MB1_MB2_MASK,
			RT5682_SAR_BUTDET_POW_NORM | RT5682_SAR_SEL_MB1_MB2_AUTO);
		snd_soc_component_update_bits(component, RT5682_CBJ_CTRL_1,
			RT5682_MB1_PATH_MASK | RT5682_MB2_PATH_MASK,
			RT5682_CTRL_MB1_FSM | RT5682_CTRL_MB2_FSM);
		snd_soc_component_update_bits(component, RT5682_PWR_ANLG_3,
			RT5682_PWR_CBJ, RT5682_PWR_CBJ);
	}

	mod_delayed_work(system_power_efficient_wq,
		&rt5682->jack_detect_work, msecs_to_jiffies(250));

	return 0;
}
#else
#define rt5682_suspend NULL
#define rt5682_resume NULL
#endif

const struct snd_soc_dai_ops rt5682_aif1_dai_ops = {
	.hw_params = rt5682_hw_params,
	.set_fmt = rt5682_set_dai_fmt,
	.set_tdm_slot = rt5682_set_tdm_slot,
	.set_bclk_ratio = rt5682_set_bclk1_ratio,
};
EXPORT_SYMBOL_GPL(rt5682_aif1_dai_ops);

const struct snd_soc_dai_ops rt5682_aif2_dai_ops = {
	.hw_params = rt5682_hw_params,
	.set_fmt = rt5682_set_dai_fmt,
	.set_bclk_ratio = rt5682_set_bclk2_ratio,
};
EXPORT_SYMBOL_GPL(rt5682_aif2_dai_ops);

const struct snd_soc_component_driver rt5682_soc_component_dev = {
	.probe = rt5682_probe,
	.remove = rt5682_remove,
	.suspend = rt5682_suspend,
	.resume = rt5682_resume,
	.set_bias_level = rt5682_set_bias_level,
	.controls = rt5682_snd_controls,
	.num_controls = ARRAY_SIZE(rt5682_snd_controls),
	.dapm_widgets = rt5682_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5682_dapm_widgets),
	.dapm_routes = rt5682_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5682_dapm_routes),
	.set_sysclk = rt5682_set_component_sysclk,
	.set_pll = rt5682_set_component_pll,
	.set_jack = rt5682_set_jack_detect,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};
EXPORT_SYMBOL_GPL(rt5682_soc_component_dev);

int rt5682_parse_dt(struct rt5682_priv *rt5682, struct device *dev)
{

	device_property_read_u32(dev, "realtek,dmic1-data-pin",
		&rt5682->pdata.dmic1_data_pin);
	device_property_read_u32(dev, "realtek,dmic1-clk-pin",
		&rt5682->pdata.dmic1_clk_pin);
	device_property_read_u32(dev, "realtek,jd-src",
		&rt5682->pdata.jd_src);
	device_property_read_u32(dev, "realtek,btndet-delay",
		&rt5682->pdata.btndet_delay);
	device_property_read_u32(dev, "realtek,dmic-clk-rate-hz",
		&rt5682->pdata.dmic_clk_rate);
	device_property_read_u32(dev, "realtek,dmic-delay-ms",
		&rt5682->pdata.dmic_delay);

	rt5682->pdata.ldo1_en = of_get_named_gpio(dev->of_node,
		"realtek,ldo1-en-gpios", 0);

	if (device_property_read_string_array(dev, "clock-output-names",
					      rt5682->pdata.dai_clk_names,
					      RT5682_DAI_NUM_CLKS) < 0)
		dev_warn(dev, "Using default DAI clk names: %s, %s\n",
			 rt5682->pdata.dai_clk_names[RT5682_DAI_WCLK_IDX],
			 rt5682->pdata.dai_clk_names[RT5682_DAI_BCLK_IDX]);

	rt5682->pdata.dmic_clk_driving_high = device_property_read_bool(dev,
		"realtek,dmic-clk-driving-high");

	return 0;
}
EXPORT_SYMBOL_GPL(rt5682_parse_dt);

void rt5682_calibrate(struct rt5682_priv *rt5682)
{
	int value, count;

	mutex_lock(&rt5682->calibrate_mutex);

	rt5682_reset(rt5682);
	regmap_write(rt5682->regmap, RT5682_I2C_CTRL, 0x000f);
	regmap_write(rt5682->regmap, RT5682_PWR_ANLG_1, 0xa2af);
	usleep_range(15000, 20000);
	regmap_write(rt5682->regmap, RT5682_PWR_ANLG_1, 0xf2af);
	regmap_write(rt5682->regmap, RT5682_MICBIAS_2, 0x0300);
	regmap_write(rt5682->regmap, RT5682_GLB_CLK, 0x8000);
	regmap_write(rt5682->regmap, RT5682_PWR_DIG_1, 0x0100);
	regmap_write(rt5682->regmap, RT5682_HP_IMP_SENS_CTRL_19, 0x3800);
	regmap_write(rt5682->regmap, RT5682_CHOP_DAC, 0x3000);
	regmap_write(rt5682->regmap, RT5682_CALIB_ADC_CTRL, 0x7005);
	regmap_write(rt5682->regmap, RT5682_STO1_ADC_MIXER, 0x686c);
	regmap_write(rt5682->regmap, RT5682_CAL_REC, 0x0d0d);
	regmap_write(rt5682->regmap, RT5682_HP_CALIB_CTRL_2, 0x0321);
	regmap_write(rt5682->regmap, RT5682_HP_LOGIC_CTRL_2, 0x0004);
	regmap_write(rt5682->regmap, RT5682_HP_CALIB_CTRL_1, 0x7c00);
	regmap_write(rt5682->regmap, RT5682_HP_CALIB_CTRL_3, 0x06a1);
	regmap_write(rt5682->regmap, RT5682_A_DAC1_MUX, 0x0311);
	regmap_write(rt5682->regmap, RT5682_HP_CALIB_CTRL_1, 0x7c00);

	regmap_write(rt5682->regmap, RT5682_HP_CALIB_CTRL_1, 0xfc00);

	for (count = 0; count < 60; count++) {
		regmap_read(rt5682->regmap, RT5682_HP_CALIB_STA_1, &value);
		if (!(value & 0x8000))
			break;

		usleep_range(10000, 10005);
	}

	if (count >= 60)
		dev_err(rt5682->component->dev, "HP Calibration Failure\n");

	/* restore settings */
	regmap_write(rt5682->regmap, RT5682_PWR_ANLG_1, 0x002f);
	regmap_write(rt5682->regmap, RT5682_MICBIAS_2, 0x0080);
	regmap_write(rt5682->regmap, RT5682_GLB_CLK, 0x0000);
	regmap_write(rt5682->regmap, RT5682_PWR_DIG_1, 0x0000);
	regmap_write(rt5682->regmap, RT5682_CHOP_DAC, 0x2000);
	regmap_write(rt5682->regmap, RT5682_CALIB_ADC_CTRL, 0x2005);
	regmap_write(rt5682->regmap, RT5682_STO1_ADC_MIXER, 0xc0c4);
	regmap_write(rt5682->regmap, RT5682_CAL_REC, 0x0c0c);

	mutex_unlock(&rt5682->calibrate_mutex);
}
EXPORT_SYMBOL_GPL(rt5682_calibrate);

MODULE_DESCRIPTION("ASoC RT5682 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL v2");
