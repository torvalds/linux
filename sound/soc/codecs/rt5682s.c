// SPDX-License-Identifier: GPL-2.0-only
//
// rt5682s.c  --  RT5682I-VS ALSA SoC audio component driver
//
// Copyright 2021 Realtek Semiconductor Corp.
// Author: Derek Fang <derek.fang@realtek.com>
//

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
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/rt5682s.h>

#include "rt5682s.h"

#define DEVICE_ID 0x6749

static const struct rt5682s_platform_data i2s_default_platform_data = {
	.dmic1_data_pin = RT5682S_DMIC1_DATA_GPIO2,
	.dmic1_clk_pin = RT5682S_DMIC1_CLK_GPIO3,
	.jd_src = RT5682S_JD1,
	.dai_clk_names[RT5682S_DAI_WCLK_IDX] = "rt5682-dai-wclk",
	.dai_clk_names[RT5682S_DAI_BCLK_IDX] = "rt5682-dai-bclk",
};

static const char *rt5682s_supply_names[RT5682S_NUM_SUPPLIES] = {
	[RT5682S_SUPPLY_AVDD] = "AVDD",
	[RT5682S_SUPPLY_MICVDD] = "MICVDD",
	[RT5682S_SUPPLY_DBVDD] = "DBVDD",
	[RT5682S_SUPPLY_LDO1_IN] = "LDO1-IN",
};

static const struct reg_sequence patch_list[] = {
	{RT5682S_I2C_CTRL,			0x0007},
	{RT5682S_DIG_IN_CTRL_1,			0x0000},
	{RT5682S_CHOP_DAC_2,			0x2020},
	{RT5682S_VREF_REC_OP_FB_CAP_CTRL_2,	0x0101},
	{RT5682S_VREF_REC_OP_FB_CAP_CTRL_1,	0x80c0},
	{RT5682S_HP_CALIB_CTRL_9,		0x0002},
	{RT5682S_DEPOP_1,			0x0000},
	{RT5682S_HP_CHARGE_PUMP_2,		0x3c15},
	{RT5682S_DAC1_DIG_VOL,			0xfefe},
	{RT5682S_SAR_IL_CMD_2,			0xac00},
	{RT5682S_SAR_IL_CMD_3,			0x024c},
	{RT5682S_CBJ_CTRL_6,			0x0804},
};

static void rt5682s_apply_patch_list(struct rt5682s_priv *rt5682s,
		struct device *dev)
{
	int ret;

	ret = regmap_multi_reg_write(rt5682s->regmap, patch_list, ARRAY_SIZE(patch_list));
	if (ret)
		dev_warn(dev, "Failed to apply regmap patch: %d\n", ret);
}

static const struct reg_default rt5682s_reg[] = {
	{0x0002, 0x8080},
	{0x0003, 0x0001},
	{0x0005, 0x0000},
	{0x0006, 0x0000},
	{0x0008, 0x8007},
	{0x000b, 0x0000},
	{0x000f, 0x4000},
	{0x0010, 0x4040},
	{0x0011, 0x0000},
	{0x0012, 0x0000},
	{0x0013, 0x1200},
	{0x0014, 0x200a},
	{0x0015, 0x0404},
	{0x0016, 0x0404},
	{0x0017, 0x05a4},
	{0x0019, 0xffff},
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
	{0x003c, 0x08c0},
	{0x0044, 0x1818},
	{0x004b, 0x00c0},
	{0x004c, 0x0000},
	{0x004d, 0x0000},
	{0x0061, 0x00c0},
	{0x0062, 0x008a},
	{0x0063, 0x0800},
	{0x0064, 0x0000},
	{0x0065, 0x0000},
	{0x0066, 0x0030},
	{0x0067, 0x000c},
	{0x0068, 0x0000},
	{0x0069, 0x0000},
	{0x006a, 0x0000},
	{0x006b, 0x0000},
	{0x006c, 0x0000},
	{0x006d, 0x2200},
	{0x006e, 0x0810},
	{0x006f, 0xe4de},
	{0x0070, 0x3320},
	{0x0071, 0x0000},
	{0x0073, 0x0000},
	{0x0074, 0x0000},
	{0x0075, 0x0002},
	{0x0076, 0x0001},
	{0x0079, 0x0000},
	{0x007a, 0x0000},
	{0x007b, 0x0000},
	{0x007c, 0x0100},
	{0x007e, 0x0000},
	{0x007f, 0x0000},
	{0x0080, 0x0000},
	{0x0083, 0x0000},
	{0x0084, 0x0000},
	{0x0085, 0x0000},
	{0x0086, 0x0005},
	{0x0087, 0x0000},
	{0x0088, 0x0000},
	{0x008c, 0x0003},
	{0x008e, 0x0060},
	{0x008f, 0x4da1},
	{0x0091, 0x1c15},
	{0x0092, 0x0425},
	{0x0093, 0x0000},
	{0x0094, 0x0080},
	{0x0095, 0x008f},
	{0x0096, 0x0000},
	{0x0097, 0x0000},
	{0x0098, 0x0000},
	{0x0099, 0x0000},
	{0x009a, 0x0000},
	{0x009b, 0x0000},
	{0x009c, 0x0000},
	{0x009d, 0x0000},
	{0x009e, 0x0000},
	{0x009f, 0x0009},
	{0x00a0, 0x0000},
	{0x00a3, 0x0002},
	{0x00a4, 0x0001},
	{0x00b6, 0x0000},
	{0x00b7, 0x0000},
	{0x00b8, 0x0000},
	{0x00b9, 0x0002},
	{0x00be, 0x0000},
	{0x00c0, 0x0160},
	{0x00c1, 0x82a0},
	{0x00c2, 0x0000},
	{0x00d0, 0x0000},
	{0x00d2, 0x3300},
	{0x00d3, 0x2200},
	{0x00d4, 0x0000},
	{0x00d9, 0x0000},
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
	{0x00f6, 0x0000},
	{0x00fa, 0x0000},
	{0x00fb, 0x0000},
	{0x00fc, 0x0000},
	{0x00fd, 0x0000},
	{0x00fe, 0x10ec},
	{0x00ff, 0x6749},
	{0x0100, 0xa000},
	{0x010b, 0x0066},
	{0x010c, 0x6666},
	{0x010d, 0x2202},
	{0x010e, 0x6666},
	{0x010f, 0xa800},
	{0x0110, 0x0006},
	{0x0111, 0x0460},
	{0x0112, 0x2000},
	{0x0113, 0x0200},
	{0x0117, 0x8000},
	{0x0118, 0x0303},
	{0x0125, 0x0020},
	{0x0132, 0x5026},
	{0x0136, 0x8000},
	{0x0139, 0x0005},
	{0x013a, 0x3030},
	{0x013b, 0xa000},
	{0x013c, 0x4110},
	{0x013f, 0x0000},
	{0x0145, 0x0022},
	{0x0146, 0x0000},
	{0x0147, 0x0000},
	{0x0148, 0x0000},
	{0x0156, 0x0022},
	{0x0157, 0x0303},
	{0x0158, 0x2222},
	{0x0159, 0x0000},
	{0x0160, 0x4ec0},
	{0x0161, 0x0080},
	{0x0162, 0x0200},
	{0x0163, 0x0800},
	{0x0164, 0x0000},
	{0x0165, 0x0000},
	{0x0166, 0x0000},
	{0x0167, 0x000f},
	{0x0168, 0x000f},
	{0x0169, 0x0001},
	{0x0190, 0x4131},
	{0x0194, 0x0000},
	{0x0195, 0x0000},
	{0x0197, 0x0022},
	{0x0198, 0x0000},
	{0x0199, 0x0000},
	{0x01ac, 0x0000},
	{0x01ad, 0x0000},
	{0x01ae, 0x0000},
	{0x01af, 0x2000},
	{0x01b0, 0x0000},
	{0x01b1, 0x0000},
	{0x01b2, 0x0000},
	{0x01b3, 0x0017},
	{0x01b4, 0x004b},
	{0x01b5, 0x0000},
	{0x01b6, 0x03e8},
	{0x01b7, 0x0000},
	{0x01b8, 0x0000},
	{0x01b9, 0x0400},
	{0x01ba, 0xb5b6},
	{0x01bb, 0x9124},
	{0x01bc, 0x4924},
	{0x01bd, 0x0009},
	{0x01be, 0x0018},
	{0x01bf, 0x002a},
	{0x01c0, 0x004c},
	{0x01c1, 0x0097},
	{0x01c2, 0x01c3},
	{0x01c3, 0x03e9},
	{0x01c4, 0x1389},
	{0x01c5, 0xc351},
	{0x01c6, 0x02a0},
	{0x01c7, 0x0b0f},
	{0x01c8, 0x402f},
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
	{0x01d7, 0x0000},
	{0x01d8, 0x0162},
	{0x01d9, 0x0007},
	{0x01da, 0x0000},
	{0x01db, 0x0004},
	{0x01dc, 0x0000},
	{0x01de, 0x7c00},
	{0x01df, 0x0020},
	{0x01e0, 0x04c1},
	{0x01e1, 0x0000},
	{0x01e2, 0x0000},
	{0x01e3, 0x0000},
	{0x01e4, 0x0000},
	{0x01e5, 0x0000},
	{0x01e6, 0x0001},
	{0x01e7, 0x0000},
	{0x01e8, 0x0000},
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
	{0x0211, 0xa004},
	{0x0212, 0x0365},
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
	{0x021d, 0x024c},
	{0x02fa, 0x0000},
	{0x02fb, 0x0000},
	{0x02fc, 0x0000},
	{0x03fe, 0x0000},
	{0x03ff, 0x0000},
	{0x0500, 0x0000},
	{0x0600, 0x0000},
	{0x0610, 0x6666},
	{0x0611, 0xa9aa},
	{0x0620, 0x6666},
	{0x0621, 0xa9aa},
	{0x0630, 0x6666},
	{0x0631, 0xa9aa},
	{0x0640, 0x6666},
	{0x0641, 0xa9aa},
	{0x07fa, 0x0000},
	{0x08fa, 0x0000},
	{0x08fb, 0x0000},
	{0x0d00, 0x0000},
	{0x1100, 0x0000},
	{0x1101, 0x0000},
	{0x1102, 0x0000},
	{0x1103, 0x0000},
	{0x1104, 0x0000},
	{0x1105, 0x0000},
	{0x1106, 0x0000},
	{0x1107, 0x0000},
	{0x1108, 0x0000},
	{0x1109, 0x0000},
	{0x110a, 0x0000},
	{0x110b, 0x0000},
	{0x110c, 0x0000},
	{0x1111, 0x0000},
	{0x1112, 0x0000},
	{0x1113, 0x0000},
	{0x1114, 0x0000},
	{0x1115, 0x0000},
	{0x1116, 0x0000},
	{0x1117, 0x0000},
	{0x1118, 0x0000},
	{0x1119, 0x0000},
	{0x111a, 0x0000},
	{0x111b, 0x0000},
	{0x111c, 0x0000},
	{0x1401, 0x0404},
	{0x1402, 0x0007},
	{0x1403, 0x0365},
	{0x1404, 0x0210},
	{0x1405, 0x0365},
	{0x1406, 0x0210},
	{0x1407, 0x0000},
	{0x1408, 0x0000},
	{0x1409, 0x0000},
	{0x140a, 0x0000},
	{0x140b, 0x0000},
	{0x140c, 0x0000},
	{0x140d, 0x0000},
	{0x140e, 0x0000},
	{0x140f, 0x0000},
	{0x1410, 0x0000},
	{0x1411, 0x0000},
	{0x1801, 0x0004},
	{0x1802, 0x0000},
	{0x1803, 0x0000},
	{0x1804, 0x0000},
	{0x1805, 0x00ff},
	{0x2c00, 0x0000},
	{0x3400, 0x0200},
	{0x3404, 0x0000},
	{0x3405, 0x0000},
	{0x3406, 0x0000},
	{0x3407, 0x0000},
	{0x3408, 0x0000},
	{0x3409, 0x0000},
	{0x340a, 0x0000},
	{0x340b, 0x0000},
	{0x340c, 0x0000},
	{0x340d, 0x0000},
	{0x340e, 0x0000},
	{0x340f, 0x0000},
	{0x3410, 0x0000},
	{0x3411, 0x0000},
	{0x3412, 0x0000},
	{0x3413, 0x0000},
	{0x3414, 0x0000},
	{0x3415, 0x0000},
	{0x3424, 0x0000},
	{0x3425, 0x0000},
	{0x3426, 0x0000},
	{0x3427, 0x0000},
	{0x3428, 0x0000},
	{0x3429, 0x0000},
	{0x342a, 0x0000},
	{0x342b, 0x0000},
	{0x342c, 0x0000},
	{0x342d, 0x0000},
	{0x342e, 0x0000},
	{0x342f, 0x0000},
	{0x3430, 0x0000},
	{0x3431, 0x0000},
	{0x3432, 0x0000},
	{0x3433, 0x0000},
	{0x3434, 0x0000},
	{0x3435, 0x0000},
	{0x3440, 0x6319},
	{0x3441, 0x3771},
	{0x3500, 0x0002},
	{0x3501, 0x5728},
	{0x3b00, 0x3010},
	{0x3b01, 0x3300},
	{0x3b02, 0x2200},
	{0x3b03, 0x0100},
};

static bool rt5682s_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5682S_RESET:
	case RT5682S_CBJ_CTRL_2:
	case RT5682S_I2S1_F_DIV_CTRL_2:
	case RT5682S_I2S2_F_DIV_CTRL_2:
	case RT5682S_INT_ST_1:
	case RT5682S_GPIO_ST:
	case RT5682S_IL_CMD_1:
	case RT5682S_4BTN_IL_CMD_1:
	case RT5682S_AJD1_CTRL:
	case RT5682S_VERSION_ID...RT5682S_DEVICE_ID:
	case RT5682S_STO_NG2_CTRL_1:
	case RT5682S_STO_NG2_CTRL_5...RT5682S_STO_NG2_CTRL_7:
	case RT5682S_STO1_DAC_SIL_DET:
	case RT5682S_HP_IMP_SENS_CTRL_1...RT5682S_HP_IMP_SENS_CTRL_4:
	case RT5682S_HP_IMP_SENS_CTRL_13:
	case RT5682S_HP_IMP_SENS_CTRL_14:
	case RT5682S_HP_IMP_SENS_CTRL_43...RT5682S_HP_IMP_SENS_CTRL_46:
	case RT5682S_HP_CALIB_CTRL_1:
	case RT5682S_HP_CALIB_CTRL_10:
	case RT5682S_HP_CALIB_ST_1...RT5682S_HP_CALIB_ST_11:
	case RT5682S_SAR_IL_CMD_2...RT5682S_SAR_IL_CMD_5:
	case RT5682S_SAR_IL_CMD_10:
	case RT5682S_SAR_IL_CMD_11:
	case RT5682S_VERSION_ID_HIDE:
	case RT5682S_VERSION_ID_CUS:
	case RT5682S_I2C_TRANS_CTRL:
	case RT5682S_DMIC_FLOAT_DET:
	case RT5682S_HA_CMP_OP_1:
	case RT5682S_NEW_CBJ_DET_CTL_10...RT5682S_NEW_CBJ_DET_CTL_16:
	case RT5682S_CLK_SW_TEST_1:
	case RT5682S_CLK_SW_TEST_2:
	case RT5682S_EFUSE_READ_1...RT5682S_EFUSE_READ_18:
	case RT5682S_PILOT_DIG_CTL_1:
		return true;
	default:
		return false;
	}
}

static bool rt5682s_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5682S_RESET:
	case RT5682S_VERSION_ID:
	case RT5682S_VENDOR_ID:
	case RT5682S_DEVICE_ID:
	case RT5682S_HP_CTRL_1:
	case RT5682S_HP_CTRL_2:
	case RT5682S_HPL_GAIN:
	case RT5682S_HPR_GAIN:
	case RT5682S_I2C_CTRL:
	case RT5682S_CBJ_BST_CTRL:
	case RT5682S_CBJ_DET_CTRL:
	case RT5682S_CBJ_CTRL_1...RT5682S_CBJ_CTRL_8:
	case RT5682S_DAC1_DIG_VOL:
	case RT5682S_STO1_ADC_DIG_VOL:
	case RT5682S_STO1_ADC_BOOST:
	case RT5682S_HP_IMP_GAIN_1:
	case RT5682S_HP_IMP_GAIN_2:
	case RT5682S_SIDETONE_CTRL:
	case RT5682S_STO1_ADC_MIXER:
	case RT5682S_AD_DA_MIXER:
	case RT5682S_STO1_DAC_MIXER:
	case RT5682S_A_DAC1_MUX:
	case RT5682S_DIG_INF2_DATA:
	case RT5682S_REC_MIXER:
	case RT5682S_CAL_REC:
	case RT5682S_HP_ANA_OST_CTRL_1...RT5682S_HP_ANA_OST_CTRL_3:
	case RT5682S_PWR_DIG_1...RT5682S_PWR_MIXER:
	case RT5682S_MB_CTRL:
	case RT5682S_CLK_GATE_TCON_1...RT5682S_CLK_GATE_TCON_3:
	case RT5682S_CLK_DET...RT5682S_LPF_AD_DMIC:
	case RT5682S_I2S1_SDP:
	case RT5682S_I2S2_SDP:
	case RT5682S_ADDA_CLK_1:
	case RT5682S_ADDA_CLK_2:
	case RT5682S_I2S1_F_DIV_CTRL_1:
	case RT5682S_I2S1_F_DIV_CTRL_2:
	case RT5682S_TDM_CTRL:
	case RT5682S_TDM_ADDA_CTRL_1:
	case RT5682S_TDM_ADDA_CTRL_2:
	case RT5682S_DATA_SEL_CTRL_1:
	case RT5682S_TDM_TCON_CTRL_1:
	case RT5682S_TDM_TCON_CTRL_2:
	case RT5682S_GLB_CLK:
	case RT5682S_PLL_TRACK_1...RT5682S_PLL_TRACK_6:
	case RT5682S_PLL_TRACK_11:
	case RT5682S_DEPOP_1:
	case RT5682S_HP_CHARGE_PUMP_1:
	case RT5682S_HP_CHARGE_PUMP_2:
	case RT5682S_HP_CHARGE_PUMP_3:
	case RT5682S_MICBIAS_1...RT5682S_MICBIAS_3:
	case RT5682S_PLL_TRACK_12...RT5682S_PLL_CTRL_7:
	case RT5682S_RC_CLK_CTRL:
	case RT5682S_I2S2_M_CLK_CTRL_1:
	case RT5682S_I2S2_F_DIV_CTRL_1:
	case RT5682S_I2S2_F_DIV_CTRL_2:
	case RT5682S_IRQ_CTRL_1...RT5682S_IRQ_CTRL_4:
	case RT5682S_INT_ST_1:
	case RT5682S_GPIO_CTRL_1:
	case RT5682S_GPIO_CTRL_2:
	case RT5682S_GPIO_ST:
	case RT5682S_HP_AMP_DET_CTRL_1:
	case RT5682S_MID_HP_AMP_DET:
	case RT5682S_LOW_HP_AMP_DET:
	case RT5682S_DELAY_BUF_CTRL:
	case RT5682S_SV_ZCD_1:
	case RT5682S_SV_ZCD_2:
	case RT5682S_IL_CMD_1...RT5682S_IL_CMD_6:
	case RT5682S_4BTN_IL_CMD_1...RT5682S_4BTN_IL_CMD_7:
	case RT5682S_ADC_STO1_HP_CTRL_1:
	case RT5682S_ADC_STO1_HP_CTRL_2:
	case RT5682S_AJD1_CTRL:
	case RT5682S_JD_CTRL_1:
	case RT5682S_DUMMY_1...RT5682S_DUMMY_3:
	case RT5682S_DAC_ADC_DIG_VOL1:
	case RT5682S_BIAS_CUR_CTRL_2...RT5682S_BIAS_CUR_CTRL_10:
	case RT5682S_VREF_REC_OP_FB_CAP_CTRL_1:
	case RT5682S_VREF_REC_OP_FB_CAP_CTRL_2:
	case RT5682S_CHARGE_PUMP_1:
	case RT5682S_DIG_IN_CTRL_1:
	case RT5682S_PAD_DRIVING_CTRL:
	case RT5682S_CHOP_DAC_1:
	case RT5682S_CHOP_DAC_2:
	case RT5682S_CHOP_ADC:
	case RT5682S_CALIB_ADC_CTRL:
	case RT5682S_VOL_TEST:
	case RT5682S_SPKVDD_DET_ST:
	case RT5682S_TEST_MODE_CTRL_1...RT5682S_TEST_MODE_CTRL_4:
	case RT5682S_PLL_INTERNAL_1...RT5682S_PLL_INTERNAL_4:
	case RT5682S_STO_NG2_CTRL_1...RT5682S_STO_NG2_CTRL_10:
	case RT5682S_STO1_DAC_SIL_DET:
	case RT5682S_SIL_PSV_CTRL1:
	case RT5682S_SIL_PSV_CTRL2:
	case RT5682S_SIL_PSV_CTRL3:
	case RT5682S_SIL_PSV_CTRL4:
	case RT5682S_SIL_PSV_CTRL5:
	case RT5682S_HP_IMP_SENS_CTRL_1...RT5682S_HP_IMP_SENS_CTRL_46:
	case RT5682S_HP_LOGIC_CTRL_1...RT5682S_HP_LOGIC_CTRL_3:
	case RT5682S_HP_CALIB_CTRL_1...RT5682S_HP_CALIB_CTRL_11:
	case RT5682S_HP_CALIB_ST_1...RT5682S_HP_CALIB_ST_11:
	case RT5682S_SAR_IL_CMD_1...RT5682S_SAR_IL_CMD_14:
	case RT5682S_DUMMY_4...RT5682S_DUMMY_6:
	case RT5682S_VERSION_ID_HIDE:
	case RT5682S_VERSION_ID_CUS:
	case RT5682S_SCAN_CTL:
	case RT5682S_HP_AMP_DET:
	case RT5682S_BIAS_CUR_CTRL_11:
	case RT5682S_BIAS_CUR_CTRL_12:
	case RT5682S_BIAS_CUR_CTRL_13:
	case RT5682S_BIAS_CUR_CTRL_14:
	case RT5682S_BIAS_CUR_CTRL_15:
	case RT5682S_BIAS_CUR_CTRL_16:
	case RT5682S_BIAS_CUR_CTRL_17:
	case RT5682S_BIAS_CUR_CTRL_18:
	case RT5682S_I2C_TRANS_CTRL:
	case RT5682S_DUMMY_7:
	case RT5682S_DUMMY_8:
	case RT5682S_DMIC_FLOAT_DET:
	case RT5682S_HA_CMP_OP_1...RT5682S_HA_CMP_OP_13:
	case RT5682S_HA_CMP_OP_14...RT5682S_HA_CMP_OP_25:
	case RT5682S_NEW_CBJ_DET_CTL_1...RT5682S_NEW_CBJ_DET_CTL_16:
	case RT5682S_DA_FILTER_1...RT5682S_DA_FILTER_5:
	case RT5682S_CLK_SW_TEST_1:
	case RT5682S_CLK_SW_TEST_2:
	case RT5682S_CLK_SW_TEST_3...RT5682S_CLK_SW_TEST_14:
	case RT5682S_EFUSE_MANU_WRITE_1...RT5682S_EFUSE_MANU_WRITE_6:
	case RT5682S_EFUSE_READ_1...RT5682S_EFUSE_READ_18:
	case RT5682S_EFUSE_TIMING_CTL_1:
	case RT5682S_EFUSE_TIMING_CTL_2:
	case RT5682S_PILOT_DIG_CTL_1:
	case RT5682S_PILOT_DIG_CTL_2:
	case RT5682S_HP_AMP_DET_CTL_1...RT5682S_HP_AMP_DET_CTL_4:
		return true;
	default:
		return false;
	}
}

static void rt5682s_reset(struct rt5682s_priv *rt5682s)
{
	regmap_write(rt5682s->regmap, RT5682S_RESET, 0);
}

static int rt5682s_button_detect(struct snd_soc_component *component)
{
	int btn_type, val;

	val = snd_soc_component_read(component, RT5682S_4BTN_IL_CMD_1);
	btn_type = val & 0xfff0;
	snd_soc_component_write(component, RT5682S_4BTN_IL_CMD_1, val);
	dev_dbg(component->dev, "%s btn_type=%x\n", __func__, btn_type);
	snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_2,
		RT5682S_SAR_ADC_PSV_MASK, RT5682S_SAR_ADC_PSV_ENTRY);

	return btn_type;
}

enum {
	SAR_PWR_OFF,
	SAR_PWR_NORMAL,
	SAR_PWR_SAVING,
};

static void rt5682s_sar_power_mode(struct snd_soc_component *component, int mode)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	mutex_lock(&rt5682s->sar_mutex);

	switch (mode) {
	case SAR_PWR_SAVING:
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_3,
			RT5682S_CBJ_IN_BUF_MASK, RT5682S_CBJ_IN_BUF_DIS);
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_1,
			RT5682S_MB1_PATH_MASK | RT5682S_MB2_PATH_MASK,
			RT5682S_CTRL_MB1_REG | RT5682S_CTRL_MB2_REG);
		snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_1,
			RT5682S_SAR_BUTDET_MASK | RT5682S_SAR_BUTDET_POW_MASK |
			RT5682S_SAR_SEL_MB1_2_CTL_MASK, RT5682S_SAR_BUTDET_DIS |
			RT5682S_SAR_BUTDET_POW_SAV | RT5682S_SAR_SEL_MB1_2_MANU);
		usleep_range(5000, 5500);
		snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_1,
			RT5682S_SAR_BUTDET_MASK, RT5682S_SAR_BUTDET_EN);
		usleep_range(5000, 5500);
		snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_2,
			RT5682S_SAR_ADC_PSV_MASK, RT5682S_SAR_ADC_PSV_ENTRY);
		break;
	case SAR_PWR_NORMAL:
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_3,
			RT5682S_CBJ_IN_BUF_MASK, RT5682S_CBJ_IN_BUF_EN);
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_1,
			RT5682S_MB1_PATH_MASK | RT5682S_MB2_PATH_MASK,
			RT5682S_CTRL_MB1_FSM | RT5682S_CTRL_MB2_FSM);
		snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_1,
			RT5682S_SAR_SEL_MB1_2_CTL_MASK, RT5682S_SAR_SEL_MB1_2_AUTO);
		usleep_range(5000, 5500);
		snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_1,
			RT5682S_SAR_BUTDET_MASK | RT5682S_SAR_BUTDET_POW_MASK,
			RT5682S_SAR_BUTDET_EN | RT5682S_SAR_BUTDET_POW_NORM);
		break;
	case SAR_PWR_OFF:
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_1,
			RT5682S_MB1_PATH_MASK | RT5682S_MB2_PATH_MASK,
			RT5682S_CTRL_MB1_FSM | RT5682S_CTRL_MB2_FSM);
		snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_1,
			RT5682S_SAR_BUTDET_MASK | RT5682S_SAR_BUTDET_POW_MASK |
			RT5682S_SAR_SEL_MB1_2_CTL_MASK, RT5682S_SAR_BUTDET_DIS |
			RT5682S_SAR_BUTDET_POW_SAV | RT5682S_SAR_SEL_MB1_2_MANU);
		break;
	default:
		dev_err(component->dev, "Invalid SAR Power mode: %d\n", mode);
		break;
	}

	mutex_unlock(&rt5682s->sar_mutex);
}

static void rt5682s_enable_push_button_irq(struct snd_soc_component *component)
{
	snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_13,
		RT5682S_SAR_SOUR_MASK, RT5682S_SAR_SOUR_BTN);
	snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_1,
		RT5682S_SAR_BUTDET_MASK | RT5682S_SAR_BUTDET_POW_MASK |
		RT5682S_SAR_SEL_MB1_2_CTL_MASK, RT5682S_SAR_BUTDET_EN |
		RT5682S_SAR_BUTDET_POW_NORM | RT5682S_SAR_SEL_MB1_2_AUTO);
	snd_soc_component_write(component, RT5682S_IL_CMD_1, 0x0040);
	snd_soc_component_update_bits(component, RT5682S_4BTN_IL_CMD_2,
		RT5682S_4BTN_IL_MASK | RT5682S_4BTN_IL_RST_MASK,
		RT5682S_4BTN_IL_EN | RT5682S_4BTN_IL_NOR);
	snd_soc_component_update_bits(component, RT5682S_IRQ_CTRL_3,
		RT5682S_IL_IRQ_MASK, RT5682S_IL_IRQ_EN);
}

static void rt5682s_disable_push_button_irq(struct snd_soc_component *component)
{
	snd_soc_component_update_bits(component, RT5682S_IRQ_CTRL_3,
		RT5682S_IL_IRQ_MASK, RT5682S_IL_IRQ_DIS);
	snd_soc_component_update_bits(component, RT5682S_4BTN_IL_CMD_2,
		RT5682S_4BTN_IL_MASK, RT5682S_4BTN_IL_DIS);
	snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_13,
		RT5682S_SAR_SOUR_MASK, RT5682S_SAR_SOUR_TYPE);
	snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_1,
		RT5682S_SAR_BUTDET_MASK | RT5682S_SAR_BUTDET_POW_MASK |
		RT5682S_SAR_SEL_MB1_2_CTL_MASK, RT5682S_SAR_BUTDET_DIS |
		RT5682S_SAR_BUTDET_POW_SAV | RT5682S_SAR_SEL_MB1_2_MANU);
}

/**
 * rt5682s_headset_detect - Detect headset.
 * @component: SoC audio component device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */
static int rt5682s_headset_detect(struct snd_soc_component *component, int jack_insert)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	unsigned int val, count;
	int jack_type = 0;

	if (jack_insert) {
		rt5682s_disable_push_button_irq(component);
		snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_1,
			RT5682S_PWR_VREF1 | RT5682S_PWR_VREF2 | RT5682S_PWR_MB,
			RT5682S_PWR_VREF1 | RT5682S_PWR_VREF2 | RT5682S_PWR_MB);
		snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_1,
			RT5682S_PWR_FV1 | RT5682S_PWR_FV2, 0);
		usleep_range(15000, 20000);
		snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_1,
			RT5682S_PWR_FV1 | RT5682S_PWR_FV2,
			RT5682S_PWR_FV1 | RT5682S_PWR_FV2);
		snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_3,
			RT5682S_PWR_CBJ, RT5682S_PWR_CBJ);
		snd_soc_component_write(component, RT5682S_SAR_IL_CMD_3, 0x0365);
		snd_soc_component_update_bits(component, RT5682S_HP_CHARGE_PUMP_2,
			RT5682S_OSW_L_MASK | RT5682S_OSW_R_MASK,
			RT5682S_OSW_L_DIS | RT5682S_OSW_R_DIS);
		snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_13,
			RT5682S_SAR_SOUR_MASK, RT5682S_SAR_SOUR_TYPE);
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_3,
			RT5682S_CBJ_IN_BUF_MASK, RT5682S_CBJ_IN_BUF_EN);
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_1,
			RT5682S_TRIG_JD_MASK, RT5682S_TRIG_JD_LOW);
		usleep_range(45000, 50000);
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_1,
			RT5682S_TRIG_JD_MASK, RT5682S_TRIG_JD_HIGH);

		count = 0;
		do {
			usleep_range(10000, 15000);
			val = snd_soc_component_read(component, RT5682S_CBJ_CTRL_2)
				& RT5682S_JACK_TYPE_MASK;
			count++;
		} while (val == 0 && count < 50);

		dev_dbg(component->dev, "%s, val=%d, count=%d\n", __func__, val, count);

		switch (val) {
		case 0x1:
		case 0x2:
			jack_type = SND_JACK_HEADSET;
			snd_soc_component_write(component, RT5682S_SAR_IL_CMD_3, 0x024c);
			snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_1,
				RT5682S_FAST_OFF_MASK, RT5682S_FAST_OFF_EN);
			snd_soc_component_update_bits(component, RT5682S_SAR_IL_CMD_1,
				RT5682S_SAR_SEL_MB1_2_MASK, val << RT5682S_SAR_SEL_MB1_2_SFT);
			rt5682s_enable_push_button_irq(component);
			rt5682s_sar_power_mode(component, SAR_PWR_SAVING);
			break;
		default:
			jack_type = SND_JACK_HEADPHONE;
			break;
		}
		snd_soc_component_update_bits(component, RT5682S_HP_CHARGE_PUMP_2,
			RT5682S_OSW_L_MASK | RT5682S_OSW_R_MASK,
			RT5682S_OSW_L_EN | RT5682S_OSW_R_EN);
		usleep_range(35000, 40000);
	} else {
		rt5682s_sar_power_mode(component, SAR_PWR_OFF);
		rt5682s_disable_push_button_irq(component);
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_1,
			RT5682S_TRIG_JD_MASK, RT5682S_TRIG_JD_LOW);

		if (!rt5682s->wclk_enabled) {
			snd_soc_component_update_bits(component,
				RT5682S_PWR_ANLG_1, RT5682S_PWR_VREF2 | RT5682S_PWR_MB, 0);
		}

		snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_3,
			RT5682S_PWR_CBJ, 0);
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_1,
			RT5682S_FAST_OFF_MASK, RT5682S_FAST_OFF_DIS);
		snd_soc_component_update_bits(component, RT5682S_CBJ_CTRL_3,
			RT5682S_CBJ_IN_BUF_MASK, RT5682S_CBJ_IN_BUF_DIS);
		jack_type = 0;
	}

	dev_dbg(component->dev, "jack_type = %d\n", jack_type);

	return jack_type;
}

static void rt5682s_jack_detect_handler(struct work_struct *work)
{
	struct rt5682s_priv *rt5682s =
		container_of(work, struct rt5682s_priv, jack_detect_work.work);
	struct snd_soc_dapm_context *dapm;
	int val, btn_type;

	if (!rt5682s->component ||
	    !snd_soc_card_is_instantiated(rt5682s->component->card)) {
		/* card not yet ready, try later */
		mod_delayed_work(system_power_efficient_wq,
				 &rt5682s->jack_detect_work, msecs_to_jiffies(15));
		return;
	}

	dapm = snd_soc_component_get_dapm(rt5682s->component);

	snd_soc_dapm_mutex_lock(dapm);
	mutex_lock(&rt5682s->calibrate_mutex);
	mutex_lock(&rt5682s->wclk_mutex);

	val = snd_soc_component_read(rt5682s->component, RT5682S_AJD1_CTRL)
		& RT5682S_JDH_RS_MASK;
	if (!val) {
		/* jack in */
		if (rt5682s->jack_type == 0) {
			/* jack was out, report jack type */
			rt5682s->jack_type = rt5682s_headset_detect(rt5682s->component, 1);
			rt5682s->irq_work_delay_time = 0;
		} else if ((rt5682s->jack_type & SND_JACK_HEADSET) == SND_JACK_HEADSET) {
			/* jack is already in, report button event */
			rt5682s->jack_type = SND_JACK_HEADSET;
			btn_type = rt5682s_button_detect(rt5682s->component);
			/**
			 * rt5682s can report three kinds of button behavior,
			 * one click, double click and hold. However,
			 * currently we will report button pressed/released
			 * event. So all the three button behaviors are
			 * treated as button pressed.
			 */
			switch (btn_type) {
			case 0x8000:
			case 0x4000:
			case 0x2000:
				rt5682s->jack_type |= SND_JACK_BTN_0;
				break;
			case 0x1000:
			case 0x0800:
			case 0x0400:
				rt5682s->jack_type |= SND_JACK_BTN_1;
				break;
			case 0x0200:
			case 0x0100:
			case 0x0080:
				rt5682s->jack_type |= SND_JACK_BTN_2;
				break;
			case 0x0040:
			case 0x0020:
			case 0x0010:
				rt5682s->jack_type |= SND_JACK_BTN_3;
				break;
			case 0x0000: /* unpressed */
				break;
			default:
				dev_err(rt5682s->component->dev,
					"Unexpected button code 0x%04x\n", btn_type);
				break;
			}
		}
	} else {
		/* jack out */
		rt5682s->jack_type = rt5682s_headset_detect(rt5682s->component, 0);
		rt5682s->irq_work_delay_time = 50;
	}

	mutex_unlock(&rt5682s->wclk_mutex);
	mutex_unlock(&rt5682s->calibrate_mutex);
	snd_soc_dapm_mutex_unlock(dapm);

	snd_soc_jack_report(rt5682s->hs_jack, rt5682s->jack_type,
		SND_JACK_HEADSET | SND_JACK_BTN_0 | SND_JACK_BTN_1 |
		SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (rt5682s->jack_type & (SND_JACK_BTN_0 | SND_JACK_BTN_1 |
		SND_JACK_BTN_2 | SND_JACK_BTN_3))
		schedule_delayed_work(&rt5682s->jd_check_work, 0);
	else
		cancel_delayed_work_sync(&rt5682s->jd_check_work);
}

static void rt5682s_jd_check_handler(struct work_struct *work)
{
	struct rt5682s_priv *rt5682s =
		container_of(work, struct rt5682s_priv, jd_check_work.work);

	if (snd_soc_component_read(rt5682s->component, RT5682S_AJD1_CTRL) & RT5682S_JDH_RS_MASK) {
		/* jack out */
		schedule_delayed_work(&rt5682s->jack_detect_work, 0);
	} else {
		schedule_delayed_work(&rt5682s->jd_check_work, 500);
	}
}

static irqreturn_t rt5682s_irq(int irq, void *data)
{
	struct rt5682s_priv *rt5682s = data;

	mod_delayed_work(system_power_efficient_wq, &rt5682s->jack_detect_work,
		msecs_to_jiffies(rt5682s->irq_work_delay_time));

	return IRQ_HANDLED;
}

static int rt5682s_set_jack_detect(struct snd_soc_component *component,
		struct snd_soc_jack *hs_jack, void *data)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	int btndet_delay = 16;

	rt5682s->hs_jack = hs_jack;

	if (!hs_jack) {
		regmap_update_bits(rt5682s->regmap, RT5682S_IRQ_CTRL_2,
			RT5682S_JD1_EN_MASK, RT5682S_JD1_DIS);
		regmap_update_bits(rt5682s->regmap, RT5682S_RC_CLK_CTRL,
			RT5682S_POW_JDH, 0);
		cancel_delayed_work_sync(&rt5682s->jack_detect_work);

		return 0;
	}

	switch (rt5682s->pdata.jd_src) {
	case RT5682S_JD1:
		regmap_update_bits(rt5682s->regmap, RT5682S_CBJ_CTRL_5,
			RT5682S_JD_FAST_OFF_SRC_MASK, RT5682S_JD_FAST_OFF_SRC_JDH);
		regmap_update_bits(rt5682s->regmap, RT5682S_CBJ_CTRL_2,
			RT5682S_EXT_JD_SRC, RT5682S_EXT_JD_SRC_MANUAL);
		regmap_update_bits(rt5682s->regmap, RT5682S_CBJ_CTRL_1,
			RT5682S_EMB_JD_MASK | RT5682S_DET_TYPE |
			RT5682S_POL_FAST_OFF_MASK | RT5682S_MIC_CAP_MASK,
			RT5682S_EMB_JD_EN | RT5682S_DET_TYPE |
			RT5682S_POL_FAST_OFF_HIGH | RT5682S_MIC_CAP_HS);
		regmap_update_bits(rt5682s->regmap, RT5682S_SAR_IL_CMD_1,
			RT5682S_SAR_POW_MASK, RT5682S_SAR_POW_EN);
		regmap_update_bits(rt5682s->regmap, RT5682S_GPIO_CTRL_1,
			RT5682S_GP1_PIN_MASK, RT5682S_GP1_PIN_IRQ);
		regmap_update_bits(rt5682s->regmap, RT5682S_PWR_ANLG_3,
			RT5682S_PWR_BGLDO, RT5682S_PWR_BGLDO);
		regmap_update_bits(rt5682s->regmap, RT5682S_PWR_ANLG_2,
			RT5682S_PWR_JD_MASK, RT5682S_PWR_JD_ENABLE);
		regmap_update_bits(rt5682s->regmap, RT5682S_RC_CLK_CTRL,
			RT5682S_POW_IRQ | RT5682S_POW_JDH, RT5682S_POW_IRQ | RT5682S_POW_JDH);
		regmap_update_bits(rt5682s->regmap, RT5682S_IRQ_CTRL_2,
			RT5682S_JD1_EN_MASK | RT5682S_JD1_POL_MASK,
			RT5682S_JD1_EN | RT5682S_JD1_POL_NOR);
		regmap_update_bits(rt5682s->regmap, RT5682S_4BTN_IL_CMD_4,
			RT5682S_4BTN_IL_HOLD_WIN_MASK | RT5682S_4BTN_IL_CLICK_WIN_MASK,
			(btndet_delay << RT5682S_4BTN_IL_HOLD_WIN_SFT | btndet_delay));
		regmap_update_bits(rt5682s->regmap, RT5682S_4BTN_IL_CMD_5,
			RT5682S_4BTN_IL_HOLD_WIN_MASK | RT5682S_4BTN_IL_CLICK_WIN_MASK,
			(btndet_delay << RT5682S_4BTN_IL_HOLD_WIN_SFT | btndet_delay));
		regmap_update_bits(rt5682s->regmap, RT5682S_4BTN_IL_CMD_6,
			RT5682S_4BTN_IL_HOLD_WIN_MASK | RT5682S_4BTN_IL_CLICK_WIN_MASK,
			(btndet_delay << RT5682S_4BTN_IL_HOLD_WIN_SFT | btndet_delay));
		regmap_update_bits(rt5682s->regmap, RT5682S_4BTN_IL_CMD_7,
			RT5682S_4BTN_IL_HOLD_WIN_MASK | RT5682S_4BTN_IL_CLICK_WIN_MASK,
			(btndet_delay << RT5682S_4BTN_IL_HOLD_WIN_SFT | btndet_delay));

		mod_delayed_work(system_power_efficient_wq,
			&rt5682s->jack_detect_work, msecs_to_jiffies(250));
		break;

	case RT5682S_JD_NULL:
		regmap_update_bits(rt5682s->regmap, RT5682S_IRQ_CTRL_2,
			RT5682S_JD1_EN_MASK, RT5682S_JD1_DIS);
		regmap_update_bits(rt5682s->regmap, RT5682S_RC_CLK_CTRL,
			RT5682S_POW_JDH, 0);
		break;

	default:
		dev_warn(component->dev, "Wrong JD source\n");
		break;
	}

	return 0;
}

static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -9562, 75, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);
static const DECLARE_TLV_DB_SCALE(cbj_bst_tlv, -1200, 150, 0);

static const struct snd_kcontrol_new rt5682s_snd_controls[] = {
	/* DAC Digital Volume */
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5682S_DAC1_DIG_VOL,
		RT5682S_L_VOL_SFT + 1, RT5682S_R_VOL_SFT + 1, 127, 0, dac_vol_tlv),

	/* CBJ Boost Volume */
	SOC_SINGLE_TLV("CBJ Boost Volume", RT5682S_REC_MIXER,
		RT5682S_BST_CBJ_SFT, 35, 0,  cbj_bst_tlv),

	/* ADC Digital Volume Control */
	SOC_DOUBLE("STO1 ADC Capture Switch", RT5682S_STO1_ADC_DIG_VOL,
		RT5682S_L_MUTE_SFT, RT5682S_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("STO1 ADC Capture Volume", RT5682S_STO1_ADC_DIG_VOL,
		RT5682S_L_VOL_SFT + 1, RT5682S_R_VOL_SFT + 1, 63, 0, adc_vol_tlv),

	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("STO1 ADC Boost Gain Volume", RT5682S_STO1_ADC_BOOST,
		RT5682S_STO1_ADC_L_BST_SFT, RT5682S_STO1_ADC_R_BST_SFT, 3, 0, adc_bst_tlv),
};

/**
 * rt5682s_sel_asrc_clk_src - select ASRC clock source for a set of filters
 * @component: SoC audio component device.
 * @filter_mask: mask of filters.
 * @clk_src: clock source
 *
 * The ASRC function is for asynchronous MCLK and LRCK. Also, since RT5682S can
 * only support standard 32fs or 64fs i2s format, ASRC should be enabled to
 * support special i2s clock format such as Intel's 100fs(100 * sampling rate).
 * ASRC function will track i2s clock and generate a corresponding system clock
 * for codec. This function provides an API to select the clock source for a
 * set of filters specified by the mask. And the component driver will turn on
 * ASRC for these filters if ASRC is selected as their clock source.
 */
int rt5682s_sel_asrc_clk_src(struct snd_soc_component *component,
		unsigned int filter_mask, unsigned int clk_src)
{
	switch (clk_src) {
	case RT5682S_CLK_SEL_SYS:
	case RT5682S_CLK_SEL_I2S1_ASRC:
	case RT5682S_CLK_SEL_I2S2_ASRC:
		break;

	default:
		return -EINVAL;
	}

	if (filter_mask & RT5682S_DA_STEREO1_FILTER) {
		snd_soc_component_update_bits(component, RT5682S_PLL_TRACK_2,
			RT5682S_FILTER_CLK_SEL_MASK, clk_src << RT5682S_FILTER_CLK_SEL_SFT);
	}

	if (filter_mask & RT5682S_AD_STEREO1_FILTER) {
		snd_soc_component_update_bits(component, RT5682S_PLL_TRACK_3,
			RT5682S_FILTER_CLK_SEL_MASK, clk_src << RT5682S_FILTER_CLK_SEL_SFT);
	}

	snd_soc_component_update_bits(component, RT5682S_PLL_TRACK_11,
		RT5682S_ASRCIN_AUTO_CLKOUT_MASK, RT5682S_ASRCIN_AUTO_CLKOUT_EN);

	return 0;
}
EXPORT_SYMBOL_GPL(rt5682s_sel_asrc_clk_src);

static int rt5682s_div_sel(struct rt5682s_priv *rt5682s,
		int target, const int div[], int size)
{
	int i;

	if (rt5682s->sysclk < target) {
		dev_err(rt5682s->component->dev,
			"sysclk rate %d is too low\n", rt5682s->sysclk);
		return 0;
	}

	for (i = 0; i < size - 1; i++) {
		dev_dbg(rt5682s->component->dev, "div[%d]=%d\n", i, div[i]);
		if (target * div[i] == rt5682s->sysclk)
			return i;
		if (target * div[i + 1] > rt5682s->sysclk) {
			dev_dbg(rt5682s->component->dev,
				"can't find div for sysclk %d\n", rt5682s->sysclk);
			return i;
		}
	}

	if (target * div[i] < rt5682s->sysclk)
		dev_err(rt5682s->component->dev,
			"sysclk rate %d is too high\n", rt5682s->sysclk);

	return size - 1;
}

static int get_clk_info(int sclk, int rate)
{
	int i;
	static const int pd[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48};

	if (sclk <= 0 || rate <= 0)
		return -EINVAL;

	rate = rate << 8;
	for (i = 0; i < ARRAY_SIZE(pd); i++)
		if (sclk == rate * pd[i])
			return i;

	return -EINVAL;
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
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	int idx, dmic_clk_rate = 3072000;
	static const int div[] = {2, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128};

	if (rt5682s->pdata.dmic_clk_rate)
		dmic_clk_rate = rt5682s->pdata.dmic_clk_rate;

	idx = rt5682s_div_sel(rt5682s, dmic_clk_rate, div, ARRAY_SIZE(div));

	snd_soc_component_update_bits(component, RT5682S_DMIC_CTRL_1,
		RT5682S_DMIC_CLK_MASK, idx << RT5682S_DMIC_CLK_SFT);

	return 0;
}


static int rt5682s_set_pllb_power(struct rt5682s_priv *rt5682s, int on)
{
	struct snd_soc_component *component = rt5682s->component;

	if (on) {
		snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_3,
			RT5682S_PWR_LDO_PLLB | RT5682S_PWR_BIAS_PLLB | RT5682S_PWR_PLLB,
			RT5682S_PWR_LDO_PLLB | RT5682S_PWR_BIAS_PLLB | RT5682S_PWR_PLLB);
		snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_3,
			RT5682S_RSTB_PLLB, RT5682S_RSTB_PLLB);
	} else {
		snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_3,
			RT5682S_PWR_LDO_PLLB | RT5682S_PWR_BIAS_PLLB |
			RT5682S_RSTB_PLLB | RT5682S_PWR_PLLB, 0);
	}

	return 0;
}

static int set_pllb_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	int on = 0;

	if (rt5682s->wclk_enabled)
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event))
		on = 1;

	rt5682s_set_pllb_power(rt5682s, on);

	return 0;
}

static void rt5682s_set_filter_clk(struct rt5682s_priv *rt5682s, int reg, int ref)
{
	struct snd_soc_component *component = rt5682s->component;
	int idx;
	static const int div_f[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48};
	static const int div_o[] = {1, 2, 4, 6, 8, 12, 16, 24, 32, 48};

	idx = rt5682s_div_sel(rt5682s, ref, div_f, ARRAY_SIZE(div_f));

	snd_soc_component_update_bits(component, reg,
		RT5682S_FILTER_CLK_DIV_MASK, idx << RT5682S_FILTER_CLK_DIV_SFT);

	/* select over sample rate */
	for (idx = 0; idx < ARRAY_SIZE(div_o); idx++) {
		if (rt5682s->sysclk <= 12288000 * div_o[idx])
			break;
	}

	snd_soc_component_update_bits(component, RT5682S_ADDA_CLK_1,
		RT5682S_ADC_OSR_MASK | RT5682S_DAC_OSR_MASK,
		(idx << RT5682S_ADC_OSR_SFT) | (idx << RT5682S_DAC_OSR_SFT));
}

static int set_filter_clk(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	int ref, reg, val;

	val = snd_soc_component_read(component, RT5682S_GPIO_CTRL_1)
			& RT5682S_GP4_PIN_MASK;

	if (w->shift == RT5682S_PWR_ADC_S1F_BIT && val == RT5682S_GP4_PIN_ADCDAT2)
		ref = 256 * rt5682s->lrck[RT5682S_AIF2];
	else
		ref = 256 * rt5682s->lrck[RT5682S_AIF1];

	if (w->shift == RT5682S_PWR_ADC_S1F_BIT)
		reg = RT5682S_PLL_TRACK_3;
	else
		reg = RT5682S_PLL_TRACK_2;

	rt5682s_set_filter_clk(rt5682s, reg, ref);

	return 0;
}

static int set_dmic_power(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	unsigned int delay = 50, val;

	if (rt5682s->pdata.dmic_delay)
		delay = rt5682s->pdata.dmic_delay;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = (snd_soc_component_read(component, RT5682S_GLB_CLK)
			& RT5682S_SCLK_SRC_MASK) >> RT5682S_SCLK_SRC_SFT;
		if (val == RT5682S_CLK_SRC_PLL1 || val == RT5682S_CLK_SRC_PLL2)
			snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_1,
				RT5682S_PWR_VREF2 | RT5682S_PWR_MB,
				RT5682S_PWR_VREF2 | RT5682S_PWR_MB);

		/*Add delay to avoid pop noise*/
		msleep(delay);
		break;

	case SND_SOC_DAPM_POST_PMD:
		if (!rt5682s->jack_type && !rt5682s->wclk_enabled) {
			snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_1,
				RT5682S_PWR_VREF2 | RT5682S_PWR_MB, 0);
		}
		break;
	}

	return 0;
}

static void rt5682s_set_i2s(struct rt5682s_priv *rt5682s, int id, int on)
{
	struct snd_soc_component *component = rt5682s->component;
	int pre_div;
	unsigned int p_reg, p_mask, p_sft;
	unsigned int c_reg, c_mask, c_sft;

	if (id == RT5682S_AIF1) {
		c_reg = RT5682S_ADDA_CLK_1;
		c_mask = RT5682S_I2S_M_D_MASK;
		c_sft = RT5682S_I2S_M_D_SFT;
		p_reg = RT5682S_PWR_DIG_1;
		p_mask = RT5682S_PWR_I2S1;
		p_sft = RT5682S_PWR_I2S1_BIT;
	} else {
		c_reg = RT5682S_I2S2_M_CLK_CTRL_1;
		c_mask = RT5682S_I2S2_M_D_MASK;
		c_sft = RT5682S_I2S2_M_D_SFT;
		p_reg = RT5682S_PWR_DIG_1;
		p_mask = RT5682S_PWR_I2S2;
		p_sft = RT5682S_PWR_I2S2_BIT;
	}

	if (on && rt5682s->master[id]) {
		pre_div = get_clk_info(rt5682s->sysclk, rt5682s->lrck[id]);
		if (pre_div < 0) {
			dev_err(component->dev, "get pre_div failed\n");
			return;
		}

		dev_dbg(component->dev, "lrck is %dHz and pre_div is %d for iis %d master\n",
			rt5682s->lrck[id], pre_div, id);
		snd_soc_component_update_bits(component, c_reg, c_mask, pre_div << c_sft);
	}

	snd_soc_component_update_bits(component, p_reg, p_mask, on << p_sft);
}

static int set_i2s_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	int on = 0;

	if (SND_SOC_DAPM_EVENT_ON(event))
		on = 1;

	if (!strcmp(w->name, "I2S1") && !rt5682s->wclk_enabled)
		rt5682s_set_i2s(rt5682s, RT5682S_AIF1, on);
	else if (!strcmp(w->name, "I2S2"))
		rt5682s_set_i2s(rt5682s, RT5682S_AIF2, on);

	return 0;
}

static int is_sys_clk_from_plla(struct snd_soc_dapm_widget *w,
		struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	if ((rt5682s->sysclk_src == RT5682S_CLK_SRC_PLL1) ||
	    (rt5682s->sysclk_src == RT5682S_CLK_SRC_PLL2 && rt5682s->pll_comb == USE_PLLAB))
		return 1;

	return 0;
}

static int is_sys_clk_from_pllb(struct snd_soc_dapm_widget *w,
		struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	if (rt5682s->sysclk_src == RT5682S_CLK_SRC_PLL2)
		return 1;

	return 0;
}

static int is_using_asrc(struct snd_soc_dapm_widget *w,
		struct snd_soc_dapm_widget *sink)
{
	unsigned int reg, sft, val;
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (w->shift) {
	case RT5682S_ADC_STO1_ASRC_SFT:
		reg = RT5682S_PLL_TRACK_3;
		sft = RT5682S_FILTER_CLK_SEL_SFT;
		break;
	case RT5682S_DAC_STO1_ASRC_SFT:
		reg = RT5682S_PLL_TRACK_2;
		sft = RT5682S_FILTER_CLK_SEL_SFT;
		break;
	default:
		return 0;
	}

	val = (snd_soc_component_read(component, reg) >> sft) & 0xf;
	switch (val) {
	case RT5682S_CLK_SEL_I2S1_ASRC:
	case RT5682S_CLK_SEL_I2S2_ASRC:
		return 1;
	default:
		return 0;
	}
}

static int rt5682s_hp_amp_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RT5682S_DEPOP_1,
			RT5682S_OUT_HP_L_EN | RT5682S_OUT_HP_R_EN,
			RT5682S_OUT_HP_L_EN | RT5682S_OUT_HP_R_EN);
		usleep_range(15000, 20000);
		snd_soc_component_update_bits(component, RT5682S_DEPOP_1,
			RT5682S_LDO_PUMP_EN | RT5682S_PUMP_EN |
			RT5682S_CAPLESS_L_EN | RT5682S_CAPLESS_R_EN,
			RT5682S_LDO_PUMP_EN | RT5682S_PUMP_EN |
			RT5682S_CAPLESS_L_EN | RT5682S_CAPLESS_R_EN);
		snd_soc_component_write(component, RT5682S_BIAS_CUR_CTRL_11, 0x6666);
		snd_soc_component_write(component, RT5682S_BIAS_CUR_CTRL_12, 0xa82a);

		snd_soc_component_update_bits(component, RT5682S_HP_CTRL_2,
			RT5682S_HPO_L_PATH_MASK | RT5682S_HPO_R_PATH_MASK |
			RT5682S_HPO_SEL_IP_EN_SW, RT5682S_HPO_L_PATH_EN |
			RT5682S_HPO_R_PATH_EN | RT5682S_HPO_IP_EN_GATING);
		usleep_range(5000, 10000);
		snd_soc_component_update_bits(component, RT5682S_HP_AMP_DET_CTL_1,
			RT5682S_CP_SW_SIZE_MASK, RT5682S_CP_SW_SIZE_L | RT5682S_CP_SW_SIZE_S);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, RT5682S_HP_CTRL_2,
			RT5682S_HPO_L_PATH_MASK | RT5682S_HPO_R_PATH_MASK |
			RT5682S_HPO_SEL_IP_EN_SW, 0);
		snd_soc_component_update_bits(component, RT5682S_HP_AMP_DET_CTL_1,
			RT5682S_CP_SW_SIZE_MASK, RT5682S_CP_SW_SIZE_M);
		snd_soc_component_update_bits(component, RT5682S_DEPOP_1,
			RT5682S_LDO_PUMP_EN | RT5682S_PUMP_EN |
			RT5682S_CAPLESS_L_EN | RT5682S_CAPLESS_R_EN, 0);
		snd_soc_component_update_bits(component, RT5682S_DEPOP_1,
			RT5682S_OUT_HP_L_EN | RT5682S_OUT_HP_R_EN, 0);
		break;
	}

	return 0;
}

static int rt5682s_stereo1_adc_mixl_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	unsigned int delay = 0;

	if (rt5682s->pdata.amic_delay)
		delay = rt5682s->pdata.amic_delay;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(delay);
		snd_soc_component_update_bits(component, RT5682S_STO1_ADC_DIG_VOL,
			RT5682S_L_MUTE, 0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT5682S_STO1_ADC_DIG_VOL,
			RT5682S_L_MUTE, RT5682S_L_MUTE);
		break;
	}

	return 0;
}

static int sar_power_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	if ((rt5682s->jack_type & SND_JACK_HEADSET) != SND_JACK_HEADSET)
		return 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		rt5682s_sar_power_mode(component, SAR_PWR_NORMAL);
		break;
	case SND_SOC_DAPM_POST_PMD:
		rt5682s_sar_power_mode(component, SAR_PWR_SAVING);
		break;
	}

	return 0;
}

/* Interface data select */
static const char * const rt5682s_data_select[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static SOC_ENUM_SINGLE_DECL(rt5682s_if2_adc_enum, RT5682S_DIG_INF2_DATA,
	RT5682S_IF2_ADC_SEL_SFT, rt5682s_data_select);

static SOC_ENUM_SINGLE_DECL(rt5682s_if1_01_adc_enum, RT5682S_TDM_ADDA_CTRL_1,
	RT5682S_IF1_ADC1_SEL_SFT, rt5682s_data_select);

static SOC_ENUM_SINGLE_DECL(rt5682s_if1_23_adc_enum, RT5682S_TDM_ADDA_CTRL_1,
	RT5682S_IF1_ADC2_SEL_SFT, rt5682s_data_select);

static SOC_ENUM_SINGLE_DECL(rt5682s_if1_45_adc_enum, RT5682S_TDM_ADDA_CTRL_1,
	RT5682S_IF1_ADC3_SEL_SFT, rt5682s_data_select);

static SOC_ENUM_SINGLE_DECL(rt5682s_if1_67_adc_enum, RT5682S_TDM_ADDA_CTRL_1,
	RT5682S_IF1_ADC4_SEL_SFT, rt5682s_data_select);

static const struct snd_kcontrol_new rt5682s_if2_adc_swap_mux =
	SOC_DAPM_ENUM("IF2 ADC Swap Mux", rt5682s_if2_adc_enum);

static const struct snd_kcontrol_new rt5682s_if1_01_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 01 ADC Swap Mux", rt5682s_if1_01_adc_enum);

static const struct snd_kcontrol_new rt5682s_if1_23_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 23 ADC Swap Mux", rt5682s_if1_23_adc_enum);

static const struct snd_kcontrol_new rt5682s_if1_45_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 45 ADC Swap Mux", rt5682s_if1_45_adc_enum);

static const struct snd_kcontrol_new rt5682s_if1_67_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 67 ADC Swap Mux", rt5682s_if1_67_adc_enum);

/* Digital Mixer */
static const struct snd_kcontrol_new rt5682s_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5682S_STO1_ADC_MIXER,
			RT5682S_M_STO1_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5682S_STO1_ADC_MIXER,
			RT5682S_M_STO1_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682s_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5682S_STO1_ADC_MIXER,
			RT5682S_M_STO1_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5682S_STO1_ADC_MIXER,
			RT5682S_M_STO1_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682s_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5682S_AD_DA_MIXER,
			RT5682S_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5682S_AD_DA_MIXER,
			RT5682S_M_DAC1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682s_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5682S_AD_DA_MIXER,
			RT5682S_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5682S_AD_DA_MIXER,
			RT5682S_M_DAC1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682s_sto1_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5682S_STO1_DAC_MIXER,
			RT5682S_M_DAC_L1_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5682S_STO1_DAC_MIXER,
			RT5682S_M_DAC_R1_STO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682s_sto1_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5682S_STO1_DAC_MIXER,
			RT5682S_M_DAC_L1_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5682S_STO1_DAC_MIXER,
			RT5682S_M_DAC_R1_STO_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5682s_rec1_l_mix[] = {
	SOC_DAPM_SINGLE("CBJ Switch", RT5682S_REC_MIXER,
			RT5682S_M_CBJ_RM1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5682s_rec1_r_mix[] = {
	SOC_DAPM_SINGLE("CBJ Switch", RT5682S_REC_MIXER,
			RT5682S_M_CBJ_RM1_R_SFT, 1, 1),
};

/* STO1 ADC1 Source */
/* MX-26 [13] [5] */
static const char * const rt5682s_sto1_adc1_src[] = {
	"DAC MIX", "ADC"
};

static SOC_ENUM_SINGLE_DECL(rt5682s_sto1_adc1l_enum, RT5682S_STO1_ADC_MIXER,
	RT5682S_STO1_ADC1L_SRC_SFT, rt5682s_sto1_adc1_src);

static const struct snd_kcontrol_new rt5682s_sto1_adc1l_mux =
	SOC_DAPM_ENUM("Stereo1 ADC1L Source", rt5682s_sto1_adc1l_enum);

static SOC_ENUM_SINGLE_DECL(rt5682s_sto1_adc1r_enum, RT5682S_STO1_ADC_MIXER,
	RT5682S_STO1_ADC1R_SRC_SFT, rt5682s_sto1_adc1_src);

static const struct snd_kcontrol_new rt5682s_sto1_adc1r_mux =
	SOC_DAPM_ENUM("Stereo1 ADC1L Source", rt5682s_sto1_adc1r_enum);

/* STO1 ADC Source */
/* MX-26 [11:10] [3:2] */
static const char * const rt5682s_sto1_adc_src[] = {
	"ADC1 L", "ADC1 R"
};

static SOC_ENUM_SINGLE_DECL(rt5682s_sto1_adcl_enum, RT5682S_STO1_ADC_MIXER,
	RT5682S_STO1_ADCL_SRC_SFT, rt5682s_sto1_adc_src);

static const struct snd_kcontrol_new rt5682s_sto1_adcl_mux =
	SOC_DAPM_ENUM("Stereo1 ADCL Source", rt5682s_sto1_adcl_enum);

static SOC_ENUM_SINGLE_DECL(rt5682s_sto1_adcr_enum, RT5682S_STO1_ADC_MIXER,
	RT5682S_STO1_ADCR_SRC_SFT, rt5682s_sto1_adc_src);

static const struct snd_kcontrol_new rt5682s_sto1_adcr_mux =
	SOC_DAPM_ENUM("Stereo1 ADCR Source", rt5682s_sto1_adcr_enum);

/* STO1 ADC2 Source */
/* MX-26 [12] [4] */
static const char * const rt5682s_sto1_adc2_src[] = {
	"DAC MIX", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(rt5682s_sto1_adc2l_enum, RT5682S_STO1_ADC_MIXER,
	RT5682S_STO1_ADC2L_SRC_SFT, rt5682s_sto1_adc2_src);

static const struct snd_kcontrol_new rt5682s_sto1_adc2l_mux =
	SOC_DAPM_ENUM("Stereo1 ADC2L Source", rt5682s_sto1_adc2l_enum);

static SOC_ENUM_SINGLE_DECL(rt5682s_sto1_adc2r_enum, RT5682S_STO1_ADC_MIXER,
	RT5682S_STO1_ADC2R_SRC_SFT, rt5682s_sto1_adc2_src);

static const struct snd_kcontrol_new rt5682s_sto1_adc2r_mux =
	SOC_DAPM_ENUM("Stereo1 ADC2R Source", rt5682s_sto1_adc2r_enum);

/* MX-79 [6:4] I2S1 ADC data location */
static const unsigned int rt5682s_if1_adc_slot_values[] = {
	0, 2, 4, 6,
};

static const char * const rt5682s_if1_adc_slot_src[] = {
	"Slot 0", "Slot 2", "Slot 4", "Slot 6"
};

static SOC_VALUE_ENUM_SINGLE_DECL(rt5682s_if1_adc_slot_enum,
	RT5682S_TDM_CTRL, RT5682S_TDM_ADC_LCA_SFT, RT5682S_TDM_ADC_LCA_MASK,
	rt5682s_if1_adc_slot_src, rt5682s_if1_adc_slot_values);

static const struct snd_kcontrol_new rt5682s_if1_adc_slot_mux =
	SOC_DAPM_ENUM("IF1 ADC Slot location", rt5682s_if1_adc_slot_enum);

/* Analog DAC L1 Source, Analog DAC R1 Source*/
/* MX-2B [4], MX-2B [0]*/
static const char * const rt5682s_alg_dac1_src[] = {
	"Stereo1 DAC Mixer", "DAC1"
};

static SOC_ENUM_SINGLE_DECL(rt5682s_alg_dac_l1_enum, RT5682S_A_DAC1_MUX,
	RT5682S_A_DACL1_SFT, rt5682s_alg_dac1_src);

static const struct snd_kcontrol_new rt5682s_alg_dac_l1_mux =
	SOC_DAPM_ENUM("Analog DAC L1 Source", rt5682s_alg_dac_l1_enum);

static SOC_ENUM_SINGLE_DECL(rt5682s_alg_dac_r1_enum, RT5682S_A_DAC1_MUX,
	RT5682S_A_DACR1_SFT, rt5682s_alg_dac1_src);

static const struct snd_kcontrol_new rt5682s_alg_dac_r1_mux =
	SOC_DAPM_ENUM("Analog DAC R1 Source", rt5682s_alg_dac_r1_enum);

static const unsigned int rt5682s_adcdat_pin_values[] = {
	1, 3,
};

static const char * const rt5682s_adcdat_pin_select[] = {
	"ADCDAT1", "ADCDAT2",
};

static SOC_VALUE_ENUM_SINGLE_DECL(rt5682s_adcdat_pin_enum,
	RT5682S_GPIO_CTRL_1, RT5682S_GP4_PIN_SFT, RT5682S_GP4_PIN_MASK,
	rt5682s_adcdat_pin_select, rt5682s_adcdat_pin_values);

static const struct snd_kcontrol_new rt5682s_adcdat_pin_ctrl =
	SOC_DAPM_ENUM("ADCDAT", rt5682s_adcdat_pin_enum);

static const struct snd_soc_dapm_widget rt5682s_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("LDO MB1", RT5682S_PWR_ANLG_3,
		RT5682S_PWR_LDO_MB1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LDO MB2", RT5682S_PWR_ANLG_3,
		RT5682S_PWR_LDO_MB2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LDO", RT5682S_PWR_ANLG_3,
		RT5682S_PWR_LDO_BIT, 0, NULL, 0),

	/* PLL Powers */
	SND_SOC_DAPM_SUPPLY_S("PLLA_LDO", 0, RT5682S_PWR_ANLG_3,
		RT5682S_PWR_LDO_PLLA_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("PLLA_BIAS", 0, RT5682S_PWR_ANLG_3,
		RT5682S_PWR_BIAS_PLLA_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("PLLA", 0, RT5682S_PWR_ANLG_3,
		RT5682S_PWR_PLLA_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("PLLA_RST", 1, RT5682S_PWR_ANLG_3,
		RT5682S_RSTB_PLLA_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLLB", SND_SOC_NOPM, 0, 0,
		set_pllb_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* ASRC */
	SND_SOC_DAPM_SUPPLY_S("DAC STO1 ASRC", 1, RT5682S_PLL_TRACK_1,
		RT5682S_DAC_STO1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO1 ASRC", 1, RT5682S_PLL_TRACK_1,
		RT5682S_ADC_STO1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AD ASRC", 1, RT5682S_PLL_TRACK_1,
		RT5682S_AD_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DA ASRC", 1, RT5682S_PLL_TRACK_1,
		RT5682S_DA_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC ASRC", 1, RT5682S_PLL_TRACK_1,
		RT5682S_DMIC_ASRC_SFT, 0, NULL, 0),

	/* Input Side */
	SND_SOC_DAPM_SUPPLY("MICBIAS1", RT5682S_PWR_ANLG_2,
		RT5682S_PWR_MB1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS2", RT5682S_PWR_ANLG_2,
		RT5682S_PWR_MB2_BIT, 0, NULL, 0),

	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),

	SND_SOC_DAPM_INPUT("IN1P"),

	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
		set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC1 Power", RT5682S_DMIC_CTRL_1, RT5682S_DMIC_1_EN_SFT, 0,
		set_dmic_power, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	/* Boost */
	SND_SOC_DAPM_PGA("BST1 CBJ", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIX1L", SND_SOC_NOPM, 0, 0, rt5682s_rec1_l_mix,
		ARRAY_SIZE(rt5682s_rec1_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIX1R", SND_SOC_NOPM, 0, 0, rt5682s_rec1_r_mix,
		ARRAY_SIZE(rt5682s_rec1_r_mix)),
	SND_SOC_DAPM_SUPPLY("RECMIX1L Power", RT5682S_CAL_REC,
		RT5682S_PWR_RM1_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RECMIX1R Power", RT5682S_CAL_REC,
		RT5682S_PWR_RM1_R_BIT, 0, NULL, 0),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC1 L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC1 R", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY("ADC1 L Power", RT5682S_PWR_DIG_1,
		RT5682S_PWR_ADC_L1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1 R Power", RT5682S_PWR_DIG_1,
		RT5682S_PWR_ADC_R1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1 clock", RT5682S_CHOP_ADC,
		RT5682S_CKGEN_ADC1_SFT, 0, NULL, 0),

	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo1 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_sto1_adc1l_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_sto1_adc1r_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_sto1_adc2l_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_sto1_adc2r_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_sto1_adcl_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_sto1_adcr_mux),
	SND_SOC_DAPM_MUX("IF1_ADC Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_if1_adc_slot_mux),

	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("ADC Stereo1 Filter", RT5682S_PWR_DIG_2,
		RT5682S_PWR_ADC_S1F_BIT, 0, set_filter_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MIXER_E("Stereo1 ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5682s_sto1_adc_l_mix, ARRAY_SIZE(rt5682s_sto1_adc_l_mix),
		rt5682s_stereo1_adc_mixl_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXR", RT5682S_STO1_ADC_DIG_VOL,
		RT5682S_R_MUTE_SFT, 1, rt5682s_sto1_adc_r_mix,
		ARRAY_SIZE(rt5682s_sto1_adc_r_mix)),

	/* ADC PGA */
	SND_SOC_DAPM_PGA("Stereo1 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", SND_SOC_NOPM, 0, 0,
		set_i2s_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("I2S2", SND_SOC_NOPM, 0, 0,
		set_i2s_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA("IF1 DAC1 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 R", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface Select */
	SND_SOC_DAPM_MUX("IF1 01 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_if1_01_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1 23 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_if1_23_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1 45 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_if1_45_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1 67 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_if1_67_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF2 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
		&rt5682s_if2_adc_swap_mux),

	SND_SOC_DAPM_MUX("ADCDAT Mux", SND_SOC_NOPM, 0, 0, &rt5682s_adcdat_pin_ctrl),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, RT5682S_I2S1_SDP,
		RT5682S_SEL_ADCDAT_SFT, 1),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, RT5682S_I2S2_SDP,
		RT5682S_I2S2_PIN_CFG_SFT, 1),
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC1 MIXL", SND_SOC_NOPM, 0, 0,
		rt5682s_dac_l_mix, ARRAY_SIZE(rt5682s_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC1 MIXR", SND_SOC_NOPM, 0, 0,
		rt5682s_dac_r_mix, ARRAY_SIZE(rt5682s_dac_r_mix)),

	/* DAC channel Mux */
	SND_SOC_DAPM_MUX("DAC L1 Source", SND_SOC_NOPM, 0, 0, &rt5682s_alg_dac_l1_mux),
	SND_SOC_DAPM_MUX("DAC R1 Source", SND_SOC_NOPM, 0, 0, &rt5682s_alg_dac_r1_mux),

	/* DAC Mixer */
	SND_SOC_DAPM_SUPPLY("DAC Stereo1 Filter", RT5682S_PWR_DIG_2,
		RT5682S_PWR_DAC_S1F_BIT, 0, set_filter_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MIXER("Stereo1 DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5682s_sto1_dac_l_mix, ARRAY_SIZE(rt5682s_sto1_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo1 DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5682s_sto1_dac_r_mix, ARRAY_SIZE(rt5682s_sto1_dac_r_mix)),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC L1", NULL, RT5682S_PWR_DIG_1, RT5682S_PWR_DAC_L1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, RT5682S_PWR_DIG_1, RT5682S_PWR_DAC_R1_BIT, 0),

	/* HPO */
	SND_SOC_DAPM_PGA_S("HP Amp", 1, SND_SOC_NOPM, 0, 0, rt5682s_hp_amp_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

	/* CLK DET */
	SND_SOC_DAPM_SUPPLY("CLKDET SYS", RT5682S_CLK_DET,
		RT5682S_SYS_CLK_DET_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKDET PLL1", RT5682S_CLK_DET,
		RT5682S_PLL1_CLK_DET_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MCLK0 DET PWR", RT5682S_PWR_ANLG_2,
		RT5682S_PWR_MCLK0_WD_BIT, 0, NULL, 0),

	/* SAR */
	SND_SOC_DAPM_SUPPLY("SAR", SND_SOC_NOPM, 0, 0, sar_power_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_route rt5682s_dapm_routes[] = {
	/*PLL*/
	{"ADC Stereo1 Filter", NULL, "PLLA", is_sys_clk_from_plla},
	{"ADC Stereo1 Filter", NULL, "PLLB", is_sys_clk_from_pllb},
	{"DAC Stereo1 Filter", NULL, "PLLA", is_sys_clk_from_plla},
	{"DAC Stereo1 Filter", NULL, "PLLB", is_sys_clk_from_pllb},
	{"PLLA", NULL, "PLLA_LDO"},
	{"PLLA", NULL, "PLLA_BIAS"},
	{"PLLA", NULL, "PLLA_RST"},

	/*ASRC*/
	{"ADC Stereo1 Filter", NULL, "ADC STO1 ASRC", is_using_asrc},
	{"DAC Stereo1 Filter", NULL, "DAC STO1 ASRC", is_using_asrc},
	{"ADC STO1 ASRC", NULL, "AD ASRC"},
	{"ADC STO1 ASRC", NULL, "DA ASRC"},
	{"DAC STO1 ASRC", NULL, "AD ASRC"},
	{"DAC STO1 ASRC", NULL, "DA ASRC"},

	{"CLKDET SYS", NULL, "MCLK0 DET PWR"},

	{"BST1 CBJ", NULL, "IN1P"},
	{"BST1 CBJ", NULL, "SAR"},

	{"RECMIX1L", "CBJ Switch", "BST1 CBJ"},
	{"RECMIX1L", NULL, "RECMIX1L Power"},
	{"RECMIX1R", "CBJ Switch", "BST1 CBJ"},
	{"RECMIX1R", NULL, "RECMIX1R Power"},

	{"ADC1 L", NULL, "RECMIX1L"},
	{"ADC1 L", NULL, "ADC1 L Power"},
	{"ADC1 L", NULL, "ADC1 clock"},
	{"ADC1 R", NULL, "RECMIX1R"},
	{"ADC1 R", NULL, "ADC1 R Power"},
	{"ADC1 R", NULL, "ADC1 clock"},

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

	{"IF1 DAC1 L", NULL, "AIF1RX"},
	{"IF1 DAC1 L", NULL, "I2S1"},
	{"IF1 DAC1 L", NULL, "DAC Stereo1 Filter"},
	{"IF1 DAC1 R", NULL, "AIF1RX"},
	{"IF1 DAC1 R", NULL, "I2S1"},
	{"IF1 DAC1 R", NULL, "DAC Stereo1 Filter"},

	{"DAC1 MIXL", "Stereo ADC Switch", "Stereo1 ADC MIXL"},
	{"DAC1 MIXL", "DAC1 Switch", "IF1 DAC1 L"},
	{"DAC1 MIXR", "Stereo ADC Switch", "Stereo1 ADC MIXR"},
	{"DAC1 MIXR", "DAC1 Switch", "IF1 DAC1 R"},

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

	{"HP Amp", NULL, "DAC L1"},
	{"HP Amp", NULL, "DAC R1"},
	{"HP Amp", NULL, "CLKDET SYS"},
	{"HP Amp", NULL, "SAR"},

	{"HPOL", NULL, "HP Amp"},
	{"HPOR", NULL, "HP Amp"},
};

static int rt5682s_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
		unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	unsigned int cl, val = 0, tx_slotnum;

	if (tx_mask || rx_mask)
		snd_soc_component_update_bits(component,
			RT5682S_TDM_ADDA_CTRL_2, RT5682S_TDM_EN, RT5682S_TDM_EN);
	else
		snd_soc_component_update_bits(component,
			RT5682S_TDM_ADDA_CTRL_2, RT5682S_TDM_EN, 0);

	/* Tx slot configuration */
	tx_slotnum = hweight_long(tx_mask);
	if (tx_slotnum) {
		if (tx_slotnum > slots) {
			dev_err(component->dev, "Invalid or oversized Tx slots.\n");
			return -EINVAL;
		}
		val |= (tx_slotnum - 1) << RT5682S_TDM_ADC_DL_SFT;
	}

	switch (slots) {
	case 4:
		val |= RT5682S_TDM_TX_CH_4;
		val |= RT5682S_TDM_RX_CH_4;
		break;
	case 6:
		val |= RT5682S_TDM_TX_CH_6;
		val |= RT5682S_TDM_RX_CH_6;
		break;
	case 8:
		val |= RT5682S_TDM_TX_CH_8;
		val |= RT5682S_TDM_RX_CH_8;
		break;
	case 2:
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT5682S_TDM_CTRL,
		RT5682S_TDM_TX_CH_MASK | RT5682S_TDM_RX_CH_MASK |
		RT5682S_TDM_ADC_DL_MASK, val);

	switch (slot_width) {
	case 8:
		if (tx_mask || rx_mask)
			return -EINVAL;
		cl = RT5682S_I2S1_TX_CHL_8 | RT5682S_I2S1_RX_CHL_8;
		break;
	case 16:
		val = RT5682S_TDM_CL_16;
		cl = RT5682S_I2S1_TX_CHL_16 | RT5682S_I2S1_RX_CHL_16;
		break;
	case 20:
		val = RT5682S_TDM_CL_20;
		cl = RT5682S_I2S1_TX_CHL_20 | RT5682S_I2S1_RX_CHL_20;
		break;
	case 24:
		val = RT5682S_TDM_CL_24;
		cl = RT5682S_I2S1_TX_CHL_24 | RT5682S_I2S1_RX_CHL_24;
		break;
	case 32:
		val = RT5682S_TDM_CL_32;
		cl = RT5682S_I2S1_TX_CHL_32 | RT5682S_I2S1_RX_CHL_32;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT5682S_TDM_TCON_CTRL_1,
		RT5682S_TDM_CL_MASK, val);
	snd_soc_component_update_bits(component, RT5682S_I2S1_SDP,
		RT5682S_I2S1_TX_CHL_MASK | RT5682S_I2S1_RX_CHL_MASK, cl);

	return 0;
}

static int rt5682s_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	unsigned int len_1 = 0, len_2 = 0;
	int frame_size;

	rt5682s->lrck[dai->id] = params_rate(params);

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		len_1 |= RT5682S_I2S1_DL_20;
		len_2 |= RT5682S_I2S2_DL_20;
		break;
	case 24:
		len_1 |= RT5682S_I2S1_DL_24;
		len_2 |= RT5682S_I2S2_DL_24;
		break;
	case 32:
		len_1 |= RT5682S_I2S1_DL_32;
		len_2 |= RT5682S_I2S2_DL_24;
		break;
	case 8:
		len_1 |= RT5682S_I2S2_DL_8;
		len_2 |= RT5682S_I2S2_DL_8;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5682S_AIF1:
		snd_soc_component_update_bits(component, RT5682S_I2S1_SDP,
			RT5682S_I2S1_DL_MASK, len_1);
		if (params_channels(params) == 1) /* mono mode */
			snd_soc_component_update_bits(component, RT5682S_I2S1_SDP,
				RT5682S_I2S1_MONO_MASK, RT5682S_I2S1_MONO_EN);
		else
			snd_soc_component_update_bits(component, RT5682S_I2S1_SDP,
				RT5682S_I2S1_MONO_MASK, RT5682S_I2S1_MONO_DIS);
		break;
	case RT5682S_AIF2:
		snd_soc_component_update_bits(component, RT5682S_I2S2_SDP,
			RT5682S_I2S2_DL_MASK, len_2);
		if (params_channels(params) == 1) /* mono mode */
			snd_soc_component_update_bits(component, RT5682S_I2S2_SDP,
				RT5682S_I2S2_MONO_MASK, RT5682S_I2S2_MONO_EN);
		else
			snd_soc_component_update_bits(component, RT5682S_I2S2_SDP,
				RT5682S_I2S2_MONO_MASK, RT5682S_I2S2_MONO_DIS);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	return 0;
}

static int rt5682s_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0, tdm_ctrl = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5682s->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		rt5682s->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5682S_I2S_BP_INV;
		tdm_ctrl |= RT5682S_TDM_S_BP_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		if (dai->id == RT5682S_AIF1)
			tdm_ctrl |= RT5682S_TDM_S_LP_INV | RT5682S_TDM_M_BP_INV;
		else
			return -EINVAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		if (dai->id == RT5682S_AIF1)
			tdm_ctrl |= RT5682S_TDM_S_BP_INV | RT5682S_TDM_S_LP_INV |
				RT5682S_TDM_M_BP_INV | RT5682S_TDM_M_LP_INV;
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
		reg_val |= RT5682S_I2S_DF_LEFT;
		tdm_ctrl |= RT5682S_TDM_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5682S_I2S_DF_PCM_A;
		tdm_ctrl |= RT5682S_TDM_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5682S_I2S_DF_PCM_B;
		tdm_ctrl |= RT5682S_TDM_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5682S_AIF1:
		snd_soc_component_update_bits(component, RT5682S_I2S1_SDP,
			RT5682S_I2S_DF_MASK, reg_val);
		snd_soc_component_update_bits(component, RT5682S_TDM_TCON_CTRL_1,
			RT5682S_TDM_MS_MASK | RT5682S_TDM_S_BP_MASK |
			RT5682S_TDM_DF_MASK | RT5682S_TDM_M_BP_MASK |
			RT5682S_TDM_M_LP_MASK | RT5682S_TDM_S_LP_MASK,
			tdm_ctrl | rt5682s->master[dai->id]);
		break;
	case RT5682S_AIF2:
		if (rt5682s->master[dai->id] == 0)
			reg_val |= RT5682S_I2S2_MS_S;
		snd_soc_component_update_bits(component, RT5682S_I2S2_SDP,
			RT5682S_I2S2_MS_MASK | RT5682S_I2S_BP_MASK |
			RT5682S_I2S_DF_MASK, reg_val);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}
	return 0;
}

static int rt5682s_set_component_sysclk(struct snd_soc_component *component,
		int clk_id, int source, unsigned int freq, int dir)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	unsigned int src = 0;

	if (freq == rt5682s->sysclk && clk_id == rt5682s->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5682S_SCLK_S_MCLK:
		src = RT5682S_CLK_SRC_MCLK;
		break;
	case RT5682S_SCLK_S_PLL1:
		src = RT5682S_CLK_SRC_PLL1;
		break;
	case RT5682S_SCLK_S_PLL2:
		src = RT5682S_CLK_SRC_PLL2;
		break;
	case RT5682S_SCLK_S_RCCLK:
		src = RT5682S_CLK_SRC_RCCLK;
		break;
	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT5682S_GLB_CLK,
		RT5682S_SCLK_SRC_MASK, src << RT5682S_SCLK_SRC_SFT);
	snd_soc_component_update_bits(component, RT5682S_ADDA_CLK_1,
		RT5682S_I2S_M_CLK_SRC_MASK, src << RT5682S_I2S_M_CLK_SRC_SFT);
	snd_soc_component_update_bits(component, RT5682S_I2S2_M_CLK_CTRL_1,
		RT5682S_I2S2_M_CLK_SRC_MASK, src << RT5682S_I2S2_M_CLK_SRC_SFT);

	rt5682s->sysclk = freq;
	rt5682s->sysclk_src = clk_id;

	dev_dbg(component->dev, "Sysclk is %dHz and clock id is %d\n",
		freq, clk_id);

	return 0;
}

static const struct pll_calc_map plla_table[] = {
	{2048000, 24576000, 0, 46, 2, true, false, false, false},
	{256000, 24576000, 0, 382, 2, true, false, false, false},
	{512000, 24576000, 0, 190, 2, true, false, false, false},
	{4096000, 24576000, 0, 22, 2, true, false, false, false},
	{1024000, 24576000, 0, 94, 2, true, false, false, false},
	{11289600, 22579200, 1, 22, 2, false, false, false, false},
	{1411200, 22579200, 0, 62, 2, true, false, false, false},
	{2822400, 22579200, 0, 30, 2, true, false, false, false},
	{12288000, 24576000, 1, 22, 2, false, false, false, false},
	{1536000, 24576000, 0, 62, 2, true, false, false, false},
	{3072000, 24576000, 0, 30, 2, true, false, false, false},
	{24576000, 49152000, 4, 22, 0, false, false, false, false},
	{3072000, 49152000, 0, 30, 0, true, false, false, false},
	{6144000, 49152000, 0, 30, 0, false, false, false, false},
	{49152000, 98304000, 10, 22, 0, false, true, false, false},
	{6144000, 98304000, 0, 30, 0, false, true, false, false},
	{12288000, 98304000, 1, 22, 0, false, true, false, false},
	{48000000, 3840000, 10, 22, 23, false, false, false, false},
	{24000000, 3840000, 4, 22, 23, false, false, false, false},
	{19200000, 3840000, 3, 23, 23, false, false, false, false},
	{38400000, 3840000, 8, 23, 23, false, false, false, false},
};

static const struct pll_calc_map pllb_table[] = {
	{48000000, 24576000, 8, 6, 3, false, false, false, false},
	{48000000, 22579200, 23, 12, 3, false, false, false, true},
	{24000000, 24576000, 3, 6, 3, false, false, false, false},
	{24000000, 22579200, 23, 26, 3, false, false, false, true},
	{19200000, 24576000, 2, 6, 3, false, false, false, false},
	{19200000, 22579200, 3, 5, 3, false, false, false, true},
	{38400000, 24576000, 6, 6, 3, false, false, false, false},
	{38400000, 22579200, 8, 5, 3, false, false, false, true},
	{3840000, 49152000, 0, 6, 0, true, false, false, false},
};

static int find_pll_inter_combination(unsigned int f_in, unsigned int f_out,
		struct pll_calc_map *a, struct pll_calc_map *b)
{
	int i, j;

	/* Look at PLLA table */
	for (i = 0; i < ARRAY_SIZE(plla_table); i++) {
		if (plla_table[i].freq_in == f_in && plla_table[i].freq_out == f_out) {
			memcpy(a, plla_table + i, sizeof(*a));
			return USE_PLLA;
		}
	}

	/* Look at PLLB table */
	for (i = 0; i < ARRAY_SIZE(pllb_table); i++) {
		if (pllb_table[i].freq_in == f_in && pllb_table[i].freq_out == f_out) {
			memcpy(b, pllb_table + i, sizeof(*b));
			return USE_PLLB;
		}
	}

	/* Find a combination of PLLA & PLLB */
	for (i = ARRAY_SIZE(plla_table) - 1; i >= 0; i--) {
		if (plla_table[i].freq_in == f_in && plla_table[i].freq_out == 3840000) {
			for (j = ARRAY_SIZE(pllb_table) - 1; j >= 0; j--) {
				if (pllb_table[j].freq_in == 3840000 &&
					pllb_table[j].freq_out == f_out) {
					memcpy(a, plla_table + i, sizeof(*a));
					memcpy(b, pllb_table + j, sizeof(*b));
					return USE_PLLAB;
				}
			}
		}
	}

	return -EINVAL;
}

static int rt5682s_set_component_pll(struct snd_soc_component *component,
		int pll_id, int source, unsigned int freq_in,
		unsigned int freq_out)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	struct pll_calc_map a_map, b_map;

	if (source == rt5682s->pll_src[pll_id] && freq_in == rt5682s->pll_in[pll_id] &&
	    freq_out == rt5682s->pll_out[pll_id])
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");
		rt5682s->pll_in[pll_id] = 0;
		rt5682s->pll_out[pll_id] = 0;
		snd_soc_component_update_bits(component, RT5682S_GLB_CLK,
			RT5682S_SCLK_SRC_MASK, RT5682S_CLK_SRC_MCLK << RT5682S_SCLK_SRC_SFT);
		return 0;
	}

	switch (source) {
	case RT5682S_PLL_S_MCLK:
		snd_soc_component_update_bits(component, RT5682S_GLB_CLK,
			RT5682S_PLL_SRC_MASK, RT5682S_PLL_SRC_MCLK);
		break;
	case RT5682S_PLL_S_BCLK1:
		snd_soc_component_update_bits(component, RT5682S_GLB_CLK,
			RT5682S_PLL_SRC_MASK, RT5682S_PLL_SRC_BCLK1);
		break;
	default:
		dev_err(component->dev, "Unknown PLL Source %d\n", source);
		return -EINVAL;
	}

	rt5682s->pll_comb = find_pll_inter_combination(freq_in, freq_out,
							&a_map, &b_map);

	if ((pll_id == RT5682S_PLL1 && rt5682s->pll_comb == USE_PLLA) ||
	    (pll_id == RT5682S_PLL2 && (rt5682s->pll_comb == USE_PLLB ||
					rt5682s->pll_comb == USE_PLLAB))) {
		dev_dbg(component->dev,
			"Supported freq conversion for PLL%d:(%d->%d): %d\n",
			pll_id + 1, freq_in, freq_out, rt5682s->pll_comb);
	} else {
		dev_err(component->dev,
			"Unsupported freq conversion for PLL%d:(%d->%d): %d\n",
			pll_id + 1, freq_in, freq_out, rt5682s->pll_comb);
		return -EINVAL;
	}

	if (rt5682s->pll_comb == USE_PLLA || rt5682s->pll_comb == USE_PLLAB) {
		dev_dbg(component->dev,
			"PLLA: fin=%d fout=%d m_bp=%d k_bp=%d m=%d n=%d k=%d\n",
			a_map.freq_in, a_map.freq_out, a_map.m_bp, a_map.k_bp,
			(a_map.m_bp ? 0 : a_map.m), a_map.n, (a_map.k_bp ? 0 : a_map.k));
		snd_soc_component_update_bits(component, RT5682S_PLL_CTRL_1,
			RT5682S_PLLA_N_MASK, a_map.n);
		snd_soc_component_update_bits(component, RT5682S_PLL_CTRL_2,
			RT5682S_PLLA_M_MASK | RT5682S_PLLA_K_MASK,
			a_map.m << RT5682S_PLLA_M_SFT | a_map.k);
		snd_soc_component_update_bits(component, RT5682S_PLL_CTRL_6,
			RT5682S_PLLA_M_BP_MASK | RT5682S_PLLA_K_BP_MASK,
			a_map.m_bp << RT5682S_PLLA_M_BP_SFT |
			a_map.k_bp << RT5682S_PLLA_K_BP_SFT);
	}

	if (rt5682s->pll_comb == USE_PLLB || rt5682s->pll_comb == USE_PLLAB) {
		dev_dbg(component->dev,
			"PLLB: fin=%d fout=%d m_bp=%d k_bp=%d m=%d n=%d k=%d byp_ps=%d sel_ps=%d\n",
			b_map.freq_in, b_map.freq_out, b_map.m_bp, b_map.k_bp,
			(b_map.m_bp ? 0 : b_map.m), b_map.n, (b_map.k_bp ? 0 : b_map.k),
			b_map.byp_ps, b_map.sel_ps);
		snd_soc_component_update_bits(component, RT5682S_PLL_CTRL_3,
			RT5682S_PLLB_N_MASK, b_map.n);
		snd_soc_component_update_bits(component, RT5682S_PLL_CTRL_4,
			RT5682S_PLLB_M_MASK | RT5682S_PLLB_K_MASK,
			b_map.m << RT5682S_PLLB_M_SFT | b_map.k);
		snd_soc_component_update_bits(component, RT5682S_PLL_CTRL_6,
			RT5682S_PLLB_SEL_PS_MASK | RT5682S_PLLB_BYP_PS_MASK |
			RT5682S_PLLB_M_BP_MASK | RT5682S_PLLB_K_BP_MASK,
			b_map.sel_ps << RT5682S_PLLB_SEL_PS_SFT |
			b_map.byp_ps << RT5682S_PLLB_BYP_PS_SFT |
			b_map.m_bp << RT5682S_PLLB_M_BP_SFT |
			b_map.k_bp << RT5682S_PLLB_K_BP_SFT);
	}

	if (rt5682s->pll_comb == USE_PLLB)
		snd_soc_component_update_bits(component, RT5682S_PLL_CTRL_7,
			RT5682S_PLLB_SRC_MASK, RT5682S_PLLB_SRC_DFIN);

	rt5682s->pll_in[pll_id] = freq_in;
	rt5682s->pll_out[pll_id] = freq_out;
	rt5682s->pll_src[pll_id] = source;

	return 0;
}

static int rt5682s_set_bclk1_ratio(struct snd_soc_dai *dai,
		unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	rt5682s->bclk[dai->id] = ratio;

	switch (ratio) {
	case 256:
		snd_soc_component_update_bits(component, RT5682S_TDM_TCON_CTRL_1,
			RT5682S_TDM_BCLK_MS1_MASK, RT5682S_TDM_BCLK_MS1_256);
		break;
	case 128:
		snd_soc_component_update_bits(component, RT5682S_TDM_TCON_CTRL_1,
			RT5682S_TDM_BCLK_MS1_MASK, RT5682S_TDM_BCLK_MS1_128);
		break;
	case 64:
		snd_soc_component_update_bits(component, RT5682S_TDM_TCON_CTRL_1,
			RT5682S_TDM_BCLK_MS1_MASK, RT5682S_TDM_BCLK_MS1_64);
		break;
	case 32:
		snd_soc_component_update_bits(component, RT5682S_TDM_TCON_CTRL_1,
			RT5682S_TDM_BCLK_MS1_MASK, RT5682S_TDM_BCLK_MS1_32);
		break;
	default:
		dev_err(dai->dev, "Invalid bclk1 ratio %d\n", ratio);
		return -EINVAL;
	}

	return 0;
}

static int rt5682s_set_bclk2_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	rt5682s->bclk[dai->id] = ratio;

	switch (ratio) {
	case 64:
		snd_soc_component_update_bits(component, RT5682S_ADDA_CLK_2,
			RT5682S_I2S2_BCLK_MS2_MASK, RT5682S_I2S2_BCLK_MS2_64);
		break;
	case 32:
		snd_soc_component_update_bits(component, RT5682S_ADDA_CLK_2,
			RT5682S_I2S2_BCLK_MS2_MASK, RT5682S_I2S2_BCLK_MS2_32);
		break;
	default:
		dev_err(dai->dev, "Invalid bclk2 ratio %d\n", ratio);
		return -EINVAL;
	}

	return 0;
}

static int rt5682s_set_bias_level(struct snd_soc_component *component,
		enum snd_soc_bias_level level)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		regmap_update_bits(rt5682s->regmap, RT5682S_PWR_DIG_1,
			RT5682S_PWR_LDO, RT5682S_PWR_LDO);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			regmap_update_bits(rt5682s->regmap, RT5682S_PWR_DIG_1,
				RT5682S_DIG_GATE_CTRL, RT5682S_DIG_GATE_CTRL);
		break;
	case SND_SOC_BIAS_OFF:
		regmap_update_bits(rt5682s->regmap, RT5682S_PWR_DIG_1, RT5682S_PWR_LDO, 0);
		if (!rt5682s->wclk_enabled)
			regmap_update_bits(rt5682s->regmap, RT5682S_PWR_DIG_1,
				RT5682S_DIG_GATE_CTRL, 0);
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

static bool rt5682s_clk_check(struct rt5682s_priv *rt5682s)
{
	if (!rt5682s->master[RT5682S_AIF1]) {
		dev_dbg(rt5682s->component->dev, "dai clk fmt not set correctly\n");
		return false;
	}
	return true;
}

static int rt5682s_wclk_prepare(struct clk_hw *hw)
{
	struct rt5682s_priv *rt5682s =
		container_of(hw, struct rt5682s_priv, dai_clks_hw[RT5682S_DAI_WCLK_IDX]);
	struct snd_soc_component *component = rt5682s->component;
	int ref, reg;

	if (!rt5682s_clk_check(rt5682s))
		return -EINVAL;

	mutex_lock(&rt5682s->wclk_mutex);

	snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_1,
		RT5682S_PWR_VREF2 | RT5682S_PWR_FV2 | RT5682S_PWR_MB,
		RT5682S_PWR_VREF2 | RT5682S_PWR_MB);
	usleep_range(15000, 20000);
	snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_1,
		RT5682S_PWR_FV2, RT5682S_PWR_FV2);

	/* Set and power on I2S1 */
	snd_soc_component_update_bits(component, RT5682S_PWR_DIG_1,
		RT5682S_DIG_GATE_CTRL, RT5682S_DIG_GATE_CTRL);
	rt5682s_set_i2s(rt5682s, RT5682S_AIF1, 1);

	/* Only need to power on PLLB due to the rate set restriction */
	reg = RT5682S_PLL_TRACK_2;
	ref = 256 * rt5682s->lrck[RT5682S_AIF1];
	rt5682s_set_filter_clk(rt5682s, reg, ref);
	rt5682s_set_pllb_power(rt5682s, 1);

	rt5682s->wclk_enabled = 1;

	mutex_unlock(&rt5682s->wclk_mutex);

	return 0;
}

static void rt5682s_wclk_unprepare(struct clk_hw *hw)
{
	struct rt5682s_priv *rt5682s =
		container_of(hw, struct rt5682s_priv, dai_clks_hw[RT5682S_DAI_WCLK_IDX]);
	struct snd_soc_component *component = rt5682s->component;

	if (!rt5682s_clk_check(rt5682s))
		return;

	mutex_lock(&rt5682s->wclk_mutex);

	if (!rt5682s->jack_type)
		snd_soc_component_update_bits(component, RT5682S_PWR_ANLG_1,
			RT5682S_PWR_VREF2 | RT5682S_PWR_FV2 | RT5682S_PWR_MB, 0);

	/* Power down I2S1 */
	rt5682s_set_i2s(rt5682s, RT5682S_AIF1, 0);
	snd_soc_component_update_bits(component, RT5682S_PWR_DIG_1,
		RT5682S_DIG_GATE_CTRL, 0);

	/* Power down PLLB */
	rt5682s_set_pllb_power(rt5682s, 0);

	rt5682s->wclk_enabled = 0;

	mutex_unlock(&rt5682s->wclk_mutex);
}

static unsigned long rt5682s_wclk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct rt5682s_priv *rt5682s =
		container_of(hw, struct rt5682s_priv, dai_clks_hw[RT5682S_DAI_WCLK_IDX]);
	struct snd_soc_component *component = rt5682s->component;
	const char * const clk_name = clk_hw_get_name(hw);

	if (!rt5682s_clk_check(rt5682s))
		return 0;
	/*
	 * Only accept to set wclk rate to 44.1k or 48kHz.
	 */
	if (rt5682s->lrck[RT5682S_AIF1] != CLK_48 &&
	    rt5682s->lrck[RT5682S_AIF1] != CLK_44) {
		dev_warn(component->dev, "%s: clk %s only support %d or %d Hz output\n",
			__func__, clk_name, CLK_44, CLK_48);
		return 0;
	}

	return rt5682s->lrck[RT5682S_AIF1];
}

static long rt5682s_wclk_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct rt5682s_priv *rt5682s =
		container_of(hw, struct rt5682s_priv, dai_clks_hw[RT5682S_DAI_WCLK_IDX]);
	struct snd_soc_component *component = rt5682s->component;
	const char * const clk_name = clk_hw_get_name(hw);

	if (!rt5682s_clk_check(rt5682s))
		return -EINVAL;
	/*
	 * Only accept to set wclk rate to 44.1k or 48kHz.
	 * It will force to 48kHz if not both.
	 */
	if (rate != CLK_48 && rate != CLK_44) {
		dev_warn(component->dev, "%s: clk %s only support %d or %d Hz output\n",
			__func__, clk_name, CLK_44, CLK_48);
		rate = CLK_48;
	}

	return rate;
}

static int rt5682s_wclk_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct rt5682s_priv *rt5682s =
		container_of(hw, struct rt5682s_priv, dai_clks_hw[RT5682S_DAI_WCLK_IDX]);
	struct snd_soc_component *component = rt5682s->component;
	struct clk *parent_clk;
	const char * const clk_name = clk_hw_get_name(hw);
	unsigned int clk_pll2_fout;

	if (!rt5682s_clk_check(rt5682s))
		return -EINVAL;

	/*
	 * Whether the wclk's parent clk (mclk) exists or not, please ensure
	 * it is fixed or set to 48MHz before setting wclk rate. It's a
	 * temporary limitation. Only accept 48MHz clk as the clk provider.
	 *
	 * It will set the codec anyway by assuming mclk is 48MHz.
	 */
	parent_clk = clk_get_parent(hw->clk);
	if (!parent_clk)
		dev_warn(component->dev,
			"Parent mclk of wclk not acquired in driver. Please ensure mclk was provided as %d Hz.\n",
			CLK_PLL2_FIN);

	if (parent_rate != CLK_PLL2_FIN)
		dev_warn(component->dev, "clk %s only support %d Hz input\n",
			clk_name, CLK_PLL2_FIN);

	/*
	 * To achieve the rate conversion from 48MHz to 44.1k or 48kHz,
	 * PLL2 is needed.
	 */
	clk_pll2_fout = rate * 512;
	rt5682s_set_component_pll(component, RT5682S_PLL2, RT5682S_PLL_S_MCLK,
		CLK_PLL2_FIN, clk_pll2_fout);

	rt5682s_set_component_sysclk(component, RT5682S_SCLK_S_PLL2, 0,
		clk_pll2_fout, SND_SOC_CLOCK_IN);

	rt5682s->lrck[RT5682S_AIF1] = rate;

	return 0;
}

static unsigned long rt5682s_bclk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct rt5682s_priv *rt5682s =
		container_of(hw, struct rt5682s_priv, dai_clks_hw[RT5682S_DAI_BCLK_IDX]);
	struct snd_soc_component *component = rt5682s->component;
	unsigned int bclks_per_wclk;

	bclks_per_wclk = snd_soc_component_read(component, RT5682S_TDM_TCON_CTRL_1);

	switch (bclks_per_wclk & RT5682S_TDM_BCLK_MS1_MASK) {
	case RT5682S_TDM_BCLK_MS1_256:
		return parent_rate * 256;
	case RT5682S_TDM_BCLK_MS1_128:
		return parent_rate * 128;
	case RT5682S_TDM_BCLK_MS1_64:
		return parent_rate * 64;
	case RT5682S_TDM_BCLK_MS1_32:
		return parent_rate * 32;
	default:
		return 0;
	}
}

static unsigned long rt5682s_bclk_get_factor(unsigned long rate,
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

static long rt5682s_bclk_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct rt5682s_priv *rt5682s =
		container_of(hw, struct rt5682s_priv, dai_clks_hw[RT5682S_DAI_BCLK_IDX]);
	unsigned long factor;

	if (!*parent_rate || !rt5682s_clk_check(rt5682s))
		return -EINVAL;

	/*
	 * BCLK rates are set as a multiplier of WCLK in HW.
	 * We don't allow changing the parent WCLK. We just do
	 * some rounding down based on the parent WCLK rate
	 * and find the appropriate multiplier of BCLK to
	 * get the rounded down BCLK value.
	 */
	factor = rt5682s_bclk_get_factor(rate, *parent_rate);

	return *parent_rate * factor;
}

static int rt5682s_bclk_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct rt5682s_priv *rt5682s =
		container_of(hw, struct rt5682s_priv, dai_clks_hw[RT5682S_DAI_BCLK_IDX]);
	struct snd_soc_component *component = rt5682s->component;
	struct snd_soc_dai *dai;
	unsigned long factor;

	if (!rt5682s_clk_check(rt5682s))
		return -EINVAL;

	factor = rt5682s_bclk_get_factor(rate, parent_rate);

	for_each_component_dais(component, dai)
		if (dai->id == RT5682S_AIF1)
			return rt5682s_set_bclk1_ratio(dai, factor);

	dev_err(component->dev, "dai %d not found in component\n",
		RT5682S_AIF1);
	return -ENODEV;
}

static const struct clk_ops rt5682s_dai_clk_ops[RT5682S_DAI_NUM_CLKS] = {
	[RT5682S_DAI_WCLK_IDX] = {
		.prepare = rt5682s_wclk_prepare,
		.unprepare = rt5682s_wclk_unprepare,
		.recalc_rate = rt5682s_wclk_recalc_rate,
		.round_rate = rt5682s_wclk_round_rate,
		.set_rate = rt5682s_wclk_set_rate,
	},
	[RT5682S_DAI_BCLK_IDX] = {
		.recalc_rate = rt5682s_bclk_recalc_rate,
		.round_rate = rt5682s_bclk_round_rate,
		.set_rate = rt5682s_bclk_set_rate,
	},
};

static int rt5682s_register_dai_clks(struct snd_soc_component *component)
{
	struct device *dev = component->dev;
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	struct rt5682s_platform_data *pdata = &rt5682s->pdata;
	struct clk_hw *dai_clk_hw;
	int i, ret;

	for (i = 0; i < RT5682S_DAI_NUM_CLKS; ++i) {
		struct clk_init_data init = { };
		struct clk_parent_data parent_data;
		const struct clk_hw *parent;

		dai_clk_hw = &rt5682s->dai_clks_hw[i];

		switch (i) {
		case RT5682S_DAI_WCLK_IDX:
			/* Make MCLK the parent of WCLK */
			if (rt5682s->mclk) {
				parent_data = (struct clk_parent_data){
					.fw_name = "mclk",
				};
				init.parent_data = &parent_data;
				init.num_parents = 1;
			}
			break;
		case RT5682S_DAI_BCLK_IDX:
			/* Make WCLK the parent of BCLK */
			parent = &rt5682s->dai_clks_hw[RT5682S_DAI_WCLK_IDX];
			init.parent_hws = &parent;
			init.num_parents = 1;
			break;
		default:
			dev_err(dev, "Invalid clock index\n");
			return -EINVAL;
		}

		init.name = pdata->dai_clk_names[i];
		init.ops = &rt5682s_dai_clk_ops[i];
		init.flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_GATE;
		dai_clk_hw->init = &init;

		ret = devm_clk_hw_register(dev, dai_clk_hw);
		if (ret) {
			dev_warn(dev, "Failed to register %s: %d\n", init.name, ret);
			return ret;
		}

		if (dev->of_node) {
			devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, dai_clk_hw);
		} else {
			ret = devm_clk_hw_register_clkdev(dev, dai_clk_hw,
							  init.name, dev_name(dev));
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int rt5682s_dai_probe_clks(struct snd_soc_component *component)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);
	int ret;

	/* Check if MCLK provided */
	rt5682s->mclk = devm_clk_get_optional(component->dev, "mclk");
	if (IS_ERR(rt5682s->mclk))
		return PTR_ERR(rt5682s->mclk);

	/* Register CCF DAI clock control */
	ret = rt5682s_register_dai_clks(component);
	if (ret)
		return ret;

	/* Initial setup for CCF */
	rt5682s->lrck[RT5682S_AIF1] = CLK_48;

	return 0;
}
#else
static inline int rt5682s_dai_probe_clks(struct snd_soc_component *component)
{
	return 0;
}
#endif /* CONFIG_COMMON_CLK */

static int rt5682s_probe(struct snd_soc_component *component)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	rt5682s->component = component;

	return rt5682s_dai_probe_clks(component);
}

static void rt5682s_remove(struct snd_soc_component *component)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	rt5682s_reset(rt5682s);
}

#ifdef CONFIG_PM
static int rt5682s_suspend(struct snd_soc_component *component)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	if (rt5682s->irq)
		disable_irq(rt5682s->irq);

	cancel_delayed_work_sync(&rt5682s->jack_detect_work);
	cancel_delayed_work_sync(&rt5682s->jd_check_work);

	if (rt5682s->hs_jack)
		rt5682s->jack_type = rt5682s_headset_detect(component, 0);

	regcache_cache_only(rt5682s->regmap, true);
	regcache_mark_dirty(rt5682s->regmap);

	return 0;
}

static int rt5682s_resume(struct snd_soc_component *component)
{
	struct rt5682s_priv *rt5682s = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt5682s->regmap, false);
	regcache_sync(rt5682s->regmap);

	if (rt5682s->hs_jack) {
		mod_delayed_work(system_power_efficient_wq,
			&rt5682s->jack_detect_work, msecs_to_jiffies(0));
	}

	if (rt5682s->irq)
		enable_irq(rt5682s->irq);

	return 0;
}
#else
#define rt5682s_suspend NULL
#define rt5682s_resume NULL
#endif

static const struct snd_soc_dai_ops rt5682s_aif1_dai_ops = {
	.hw_params = rt5682s_hw_params,
	.set_fmt = rt5682s_set_dai_fmt,
	.set_tdm_slot = rt5682s_set_tdm_slot,
	.set_bclk_ratio = rt5682s_set_bclk1_ratio,
};

static const struct snd_soc_dai_ops rt5682s_aif2_dai_ops = {
	.hw_params = rt5682s_hw_params,
	.set_fmt = rt5682s_set_dai_fmt,
	.set_bclk_ratio = rt5682s_set_bclk2_ratio,
};

static const struct snd_soc_component_driver rt5682s_soc_component_dev = {
	.probe = rt5682s_probe,
	.remove = rt5682s_remove,
	.suspend = rt5682s_suspend,
	.resume = rt5682s_resume,
	.set_bias_level = rt5682s_set_bias_level,
	.controls = rt5682s_snd_controls,
	.num_controls = ARRAY_SIZE(rt5682s_snd_controls),
	.dapm_widgets = rt5682s_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5682s_dapm_widgets),
	.dapm_routes = rt5682s_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5682s_dapm_routes),
	.set_sysclk = rt5682s_set_component_sysclk,
	.set_pll = rt5682s_set_component_pll,
	.set_jack = rt5682s_set_jack_detect,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int rt5682s_parse_dt(struct rt5682s_priv *rt5682s, struct device *dev)
{
	device_property_read_u32(dev, "realtek,dmic1-data-pin",
		&rt5682s->pdata.dmic1_data_pin);
	device_property_read_u32(dev, "realtek,dmic1-clk-pin",
		&rt5682s->pdata.dmic1_clk_pin);
	device_property_read_u32(dev, "realtek,jd-src",
		&rt5682s->pdata.jd_src);
	device_property_read_u32(dev, "realtek,dmic-clk-rate-hz",
		&rt5682s->pdata.dmic_clk_rate);
	device_property_read_u32(dev, "realtek,dmic-delay-ms",
		&rt5682s->pdata.dmic_delay);
	device_property_read_u32(dev, "realtek,amic-delay-ms",
		&rt5682s->pdata.amic_delay);

	rt5682s->pdata.ldo1_en = of_get_named_gpio(dev->of_node,
		"realtek,ldo1-en-gpios", 0);

	if (device_property_read_string_array(dev, "clock-output-names",
					      rt5682s->pdata.dai_clk_names,
					      RT5682S_DAI_NUM_CLKS) < 0)
		dev_warn(dev, "Using default DAI clk names: %s, %s\n",
			 rt5682s->pdata.dai_clk_names[RT5682S_DAI_WCLK_IDX],
			 rt5682s->pdata.dai_clk_names[RT5682S_DAI_BCLK_IDX]);

	rt5682s->pdata.dmic_clk_driving_high = device_property_read_bool(dev,
		"realtek,dmic-clk-driving-high");

	return 0;
}

static void rt5682s_calibrate(struct rt5682s_priv *rt5682s)
{
	unsigned int count, value;

	mutex_lock(&rt5682s->calibrate_mutex);

	regmap_write(rt5682s->regmap, RT5682S_PWR_ANLG_1, 0xaa80);
	usleep_range(15000, 20000);
	regmap_write(rt5682s->regmap, RT5682S_PWR_ANLG_1, 0xfa80);
	regmap_write(rt5682s->regmap, RT5682S_PWR_DIG_1, 0x01c0);
	regmap_write(rt5682s->regmap, RT5682S_MICBIAS_2, 0x0380);
	regmap_write(rt5682s->regmap, RT5682S_GLB_CLK, 0x8000);
	regmap_write(rt5682s->regmap, RT5682S_ADDA_CLK_1, 0x1001);
	regmap_write(rt5682s->regmap, RT5682S_CHOP_DAC_2, 0x3030);
	regmap_write(rt5682s->regmap, RT5682S_CHOP_ADC, 0xb000);
	regmap_write(rt5682s->regmap, RT5682S_STO1_ADC_MIXER, 0x686c);
	regmap_write(rt5682s->regmap, RT5682S_CAL_REC, 0x5151);
	regmap_write(rt5682s->regmap, RT5682S_HP_CALIB_CTRL_2, 0x0321);
	regmap_write(rt5682s->regmap, RT5682S_HP_LOGIC_CTRL_2, 0x0004);
	regmap_write(rt5682s->regmap, RT5682S_HP_CALIB_CTRL_1, 0x7c00);
	regmap_write(rt5682s->regmap, RT5682S_HP_CALIB_CTRL_1, 0xfc00);

	for (count = 0; count < 60; count++) {
		regmap_read(rt5682s->regmap, RT5682S_HP_CALIB_ST_1, &value);
		if (!(value & 0x8000))
			break;

		usleep_range(10000, 10005);
	}

	if (count >= 60)
		dev_err(rt5682s->component->dev, "HP Calibration Failure\n");

	/* restore settings */
	regmap_write(rt5682s->regmap, RT5682S_MICBIAS_2, 0x0180);
	regmap_write(rt5682s->regmap, RT5682S_CAL_REC, 0x5858);
	regmap_write(rt5682s->regmap, RT5682S_STO1_ADC_MIXER, 0xc0c4);
	regmap_write(rt5682s->regmap, RT5682S_HP_CALIB_CTRL_2, 0x0320);
	regmap_write(rt5682s->regmap, RT5682S_PWR_DIG_1, 0x00c0);
	regmap_write(rt5682s->regmap, RT5682S_PWR_ANLG_1, 0x0800);
	regmap_write(rt5682s->regmap, RT5682S_GLB_CLK, 0x0000);

	mutex_unlock(&rt5682s->calibrate_mutex);
}

static const struct regmap_config rt5682s_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.max_register = RT5682S_MAX_REG,
	.volatile_reg = rt5682s_volatile_register,
	.readable_reg = rt5682s_readable_register,
	.cache_type = REGCACHE_MAPLE,
	.reg_defaults = rt5682s_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5682s_reg),
	.use_single_read = true,
	.use_single_write = true,
};

static struct snd_soc_dai_driver rt5682s_dai[] = {
	{
		.name = "rt5682s-aif1",
		.id = RT5682S_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682S_STEREO_RATES,
			.formats = RT5682S_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682S_STEREO_RATES,
			.formats = RT5682S_FORMATS,
		},
		.ops = &rt5682s_aif1_dai_ops,
	},
	{
		.name = "rt5682s-aif2",
		.id = RT5682S_AIF2,
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682S_STEREO_RATES,
			.formats = RT5682S_FORMATS,
		},
		.ops = &rt5682s_aif2_dai_ops,
	},
};

static void rt5682s_i2c_disable_regulators(void *data)
{
	struct rt5682s_priv *rt5682s = data;
	struct device *dev = regmap_get_device(rt5682s->regmap);
	int ret;

	ret = regulator_disable(rt5682s->supplies[RT5682S_SUPPLY_AVDD].consumer);
	if (ret)
		dev_err(dev, "Failed to disable supply AVDD: %d\n", ret);

	ret = regulator_disable(rt5682s->supplies[RT5682S_SUPPLY_DBVDD].consumer);
	if (ret)
		dev_err(dev, "Failed to disable supply DBVDD: %d\n", ret);

	ret = regulator_disable(rt5682s->supplies[RT5682S_SUPPLY_LDO1_IN].consumer);
	if (ret)
		dev_err(dev, "Failed to disable supply LDO1-IN: %d\n", ret);

	usleep_range(1000, 1500);

	ret = regulator_disable(rt5682s->supplies[RT5682S_SUPPLY_MICVDD].consumer);
	if (ret)
		dev_err(dev, "Failed to disable supply MICVDD: %d\n", ret);
}

static int rt5682s_i2c_probe(struct i2c_client *i2c)
{
	struct rt5682s_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt5682s_priv *rt5682s;
	int i, ret;
	unsigned int val;

	rt5682s = devm_kzalloc(&i2c->dev, sizeof(struct rt5682s_priv), GFP_KERNEL);
	if (!rt5682s)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5682s);

	rt5682s->pdata = i2s_default_platform_data;

	if (pdata)
		rt5682s->pdata = *pdata;
	else
		rt5682s_parse_dt(rt5682s, &i2c->dev);

	rt5682s->regmap = devm_regmap_init_i2c(i2c, &rt5682s_regmap);
	if (IS_ERR(rt5682s->regmap)) {
		ret = PTR_ERR(rt5682s->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(rt5682s->supplies); i++)
		rt5682s->supplies[i].supply = rt5682s_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev,
			ARRAY_SIZE(rt5682s->supplies), rt5682s->supplies);
	if (ret) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&i2c->dev, rt5682s_i2c_disable_regulators, rt5682s);
	if (ret)
		return ret;

	ret = regulator_enable(rt5682s->supplies[RT5682S_SUPPLY_MICVDD].consumer);
	if (ret) {
		dev_err(&i2c->dev, "Failed to enable supply MICVDD: %d\n", ret);
		return ret;
	}
	usleep_range(1000, 1500);

	ret = regulator_enable(rt5682s->supplies[RT5682S_SUPPLY_AVDD].consumer);
	if (ret) {
		dev_err(&i2c->dev, "Failed to enable supply AVDD: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(rt5682s->supplies[RT5682S_SUPPLY_DBVDD].consumer);
	if (ret) {
		dev_err(&i2c->dev, "Failed to enable supply DBVDD: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(rt5682s->supplies[RT5682S_SUPPLY_LDO1_IN].consumer);
	if (ret) {
		dev_err(&i2c->dev, "Failed to enable supply LDO1-IN: %d\n", ret);
		return ret;
	}

	if (gpio_is_valid(rt5682s->pdata.ldo1_en)) {
		if (devm_gpio_request_one(&i2c->dev, rt5682s->pdata.ldo1_en,
					  GPIOF_OUT_INIT_HIGH, "rt5682s"))
			dev_err(&i2c->dev, "Fail gpio_request gpio_ldo\n");
	}

	/* Sleep for 50 ms minimum */
	usleep_range(50000, 55000);

	regmap_read(rt5682s->regmap, RT5682S_DEVICE_ID, &val);
	if (val != DEVICE_ID) {
		dev_err(&i2c->dev, "Device with ID register %x is not rt5682s\n", val);
		return -ENODEV;
	}

	rt5682s_reset(rt5682s);
	rt5682s_apply_patch_list(rt5682s, &i2c->dev);

	regmap_update_bits(rt5682s->regmap, RT5682S_PWR_DIG_2,
		RT5682S_DLDO_I_LIMIT_MASK, RT5682S_DLDO_I_LIMIT_DIS);
	usleep_range(20000, 25000);

	mutex_init(&rt5682s->calibrate_mutex);
	mutex_init(&rt5682s->sar_mutex);
	mutex_init(&rt5682s->wclk_mutex);
	rt5682s_calibrate(rt5682s);

	regmap_update_bits(rt5682s->regmap, RT5682S_MICBIAS_2,
		RT5682S_PWR_CLK25M_MASK | RT5682S_PWR_CLK1M_MASK,
		RT5682S_PWR_CLK25M_PD | RT5682S_PWR_CLK1M_PU);
	regmap_update_bits(rt5682s->regmap, RT5682S_PWR_ANLG_1,
		RT5682S_PWR_BG, RT5682S_PWR_BG);
	regmap_update_bits(rt5682s->regmap, RT5682S_HP_LOGIC_CTRL_2,
		RT5682S_HP_SIG_SRC_MASK, RT5682S_HP_SIG_SRC_1BIT_CTL);
	regmap_update_bits(rt5682s->regmap, RT5682S_HP_CHARGE_PUMP_2,
		RT5682S_PM_HP_MASK, RT5682S_PM_HP_HV);
	regmap_update_bits(rt5682s->regmap, RT5682S_HP_AMP_DET_CTL_1,
		RT5682S_CP_SW_SIZE_MASK, RT5682S_CP_SW_SIZE_M);

	/* DMIC data pin */
	switch (rt5682s->pdata.dmic1_data_pin) {
	case RT5682S_DMIC1_DATA_NULL:
		break;
	case RT5682S_DMIC1_DATA_GPIO2: /* share with LRCK2 */
		regmap_update_bits(rt5682s->regmap, RT5682S_DMIC_CTRL_1,
			RT5682S_DMIC_1_DP_MASK, RT5682S_DMIC_1_DP_GPIO2);
		regmap_update_bits(rt5682s->regmap, RT5682S_GPIO_CTRL_1,
			RT5682S_GP2_PIN_MASK, RT5682S_GP2_PIN_DMIC_SDA);
		break;
	case RT5682S_DMIC1_DATA_GPIO5: /* share with DACDAT1 */
		regmap_update_bits(rt5682s->regmap, RT5682S_DMIC_CTRL_1,
			RT5682S_DMIC_1_DP_MASK, RT5682S_DMIC_1_DP_GPIO5);
		regmap_update_bits(rt5682s->regmap, RT5682S_GPIO_CTRL_1,
			RT5682S_GP5_PIN_MASK, RT5682S_GP5_PIN_DMIC_SDA);
		break;
	default:
		dev_warn(&i2c->dev, "invalid DMIC_DAT pin\n");
		break;
	}

	/* DMIC clk pin */
	switch (rt5682s->pdata.dmic1_clk_pin) {
	case RT5682S_DMIC1_CLK_NULL:
		break;
	case RT5682S_DMIC1_CLK_GPIO1: /* share with IRQ */
		regmap_update_bits(rt5682s->regmap, RT5682S_GPIO_CTRL_1,
			RT5682S_GP1_PIN_MASK, RT5682S_GP1_PIN_DMIC_CLK);
		break;
	case RT5682S_DMIC1_CLK_GPIO3: /* share with BCLK2 */
		regmap_update_bits(rt5682s->regmap, RT5682S_GPIO_CTRL_1,
			RT5682S_GP3_PIN_MASK, RT5682S_GP3_PIN_DMIC_CLK);
		if (rt5682s->pdata.dmic_clk_driving_high)
			regmap_update_bits(rt5682s->regmap, RT5682S_PAD_DRIVING_CTRL,
				RT5682S_PAD_DRV_GP3_MASK, RT5682S_PAD_DRV_GP3_HIGH);
		break;
	default:
		dev_warn(&i2c->dev, "invalid DMIC_CLK pin\n");
		break;
	}

	INIT_DELAYED_WORK(&rt5682s->jack_detect_work, rt5682s_jack_detect_handler);
	INIT_DELAYED_WORK(&rt5682s->jd_check_work, rt5682s_jd_check_handler);

	if (i2c->irq) {
		ret = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL, rt5682s_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"rt5682s", rt5682s);
		if (!ret)
			rt5682s->irq = i2c->irq;
		else
			dev_err(&i2c->dev, "Failed to reguest IRQ: %d\n", ret);
	}

	return devm_snd_soc_register_component(&i2c->dev, &rt5682s_soc_component_dev,
			rt5682s_dai, ARRAY_SIZE(rt5682s_dai));
}

static void rt5682s_i2c_shutdown(struct i2c_client *client)
{
	struct rt5682s_priv *rt5682s = i2c_get_clientdata(client);

	disable_irq(client->irq);
	cancel_delayed_work_sync(&rt5682s->jack_detect_work);
	cancel_delayed_work_sync(&rt5682s->jd_check_work);

	rt5682s_reset(rt5682s);
}

static void rt5682s_i2c_remove(struct i2c_client *client)
{
	rt5682s_i2c_shutdown(client);
}

static const struct of_device_id rt5682s_of_match[] = {
	{.compatible = "realtek,rt5682s"},
	{},
};
MODULE_DEVICE_TABLE(of, rt5682s_of_match);

static const struct acpi_device_id rt5682s_acpi_match[] = {
	{"RTL5682", 0,},
	{},
};
MODULE_DEVICE_TABLE(acpi, rt5682s_acpi_match);

static const struct i2c_device_id rt5682s_i2c_id[] = {
	{"rt5682s", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rt5682s_i2c_id);

static struct i2c_driver rt5682s_i2c_driver = {
	.driver = {
		.name = "rt5682s",
		.of_match_table = rt5682s_of_match,
		.acpi_match_table = rt5682s_acpi_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = rt5682s_i2c_probe,
	.remove = rt5682s_i2c_remove,
	.shutdown = rt5682s_i2c_shutdown,
	.id_table = rt5682s_i2c_id,
};
module_i2c_driver(rt5682s_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5682I-VS driver");
MODULE_AUTHOR("Derek Fang <derek.fang@realtek.com>");
MODULE_LICENSE("GPL v2");
