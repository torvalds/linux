/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RT1305.h  --  RT1305 ALSA SoC amplifier component driver
 *
 * Copyright 2018 Realtek Semiconductor Corp.
 * Author: Shuming Fan <shumingf@realtek.com>
 */

#ifndef _RT1305_H_
#define _RT1305_H_

#define RT1305_DEVICE_ID_NUM 0x6251

#define RT1305_RESET				0x00
#define RT1305_CLK_1				0x04
#define RT1305_CLK_2				0x05
#define RT1305_CLK_3				0x06
#define RT1305_DFLL_REG				0x07
#define RT1305_CAL_EFUSE_CLOCK	0x08
#define RT1305_PLL0_1				0x0a
#define RT1305_PLL0_2				0x0b
#define RT1305_PLL1_1				0x0c
#define RT1305_PLL1_2				0x0d
#define RT1305_MIXER_CTRL_1 0x10
#define RT1305_MIXER_CTRL_2 0x11
#define RT1305_DAC_SET_1             0x12
#define RT1305_DAC_SET_2             0x14
#define RT1305_ADC_SET_1            0x16
#define RT1305_ADC_SET_2            0x17
#define RT1305_ADC_SET_3            0x18
#define RT1305_PATH_SET             0x20
#define RT1305_SPDIF_IN_SET_1                 0x22
#define RT1305_SPDIF_IN_SET_2                 0x24
#define RT1305_SPDIF_IN_SET_3                 0x26
#define RT1305_SPDIF_OUT_SET_1                 0x28
#define RT1305_SPDIF_OUT_SET_2                 0x2a
#define RT1305_SPDIF_OUT_SET_3                 0x2b
#define RT1305_I2S_SET_1                       0x2d
#define RT1305_I2S_SET_2                      0x2e
#define RT1305_PBTL_MONO_MODE_SRC            0x2f
#define RT1305_MANUALLY_I2C_DEVICE 0x32
#define RT1305_POWER_STATUS                  0x39
#define RT1305_POWER_CTRL_1                  0x3a
#define RT1305_POWER_CTRL_2                  0x3b
#define RT1305_POWER_CTRL_3                  0x3c
#define RT1305_POWER_CTRL_4                  0x3d
#define RT1305_POWER_CTRL_5                  0x3e
#define RT1305_CLOCK_DETECT                  0x3f
#define RT1305_BIQUAD_SET_1                  0x40
#define RT1305_BIQUAD_SET_2                  0x42
#define RT1305_ADJUSTED_HPF_1             0x46
#define RT1305_ADJUSTED_HPF_2               0x47
#define RT1305_EQ_SET_1                  0x4b
#define RT1305_EQ_SET_2                  0x4c
#define RT1305_SPK_TEMP_PROTECTION_0 0x4f
#define RT1305_SPK_TEMP_PROTECTION_1 0x50
#define RT1305_SPK_TEMP_PROTECTION_2 0x51
#define RT1305_SPK_TEMP_PROTECTION_3 0x52
#define RT1305_SPK_DC_DETECT_1                  0x53
#define RT1305_SPK_DC_DETECT_2                  0x54
#define RT1305_LOUDNESS 0x58
#define RT1305_THERMAL_FOLD_BACK_1 0x5e
#define RT1305_THERMAL_FOLD_BACK_2 0x5f
#define RT1305_SILENCE_DETECT                  0x60
#define RT1305_ALC_DRC_1                  0x62
#define RT1305_ALC_DRC_2                  0x63
#define RT1305_ALC_DRC_3                  0x64
#define RT1305_ALC_DRC_4                  0x65
#define RT1305_PRIV_INDEX			0x6a
#define RT1305_PRIV_DATA			0x6c
#define RT1305_SPK_EXCURSION_LIMITER_7 0x76
#define RT1305_VERSION_ID			0x7a
#define RT1305_VENDOR_ID			0x7c
#define RT1305_DEVICE_ID			0x7e
#define RT1305_EFUSE_1                  0x80
#define RT1305_EFUSE_2                  0x81
#define RT1305_EFUSE_3                  0x82
#define RT1305_DC_CALIB_1                  0x90
#define RT1305_DC_CALIB_2                  0x91
#define RT1305_DC_CALIB_3                  0x92
#define RT1305_DAC_OFFSET_1            0x93
#define RT1305_DAC_OFFSET_2            0x94
#define RT1305_DAC_OFFSET_3            0x95
#define RT1305_DAC_OFFSET_4            0x96
#define RT1305_DAC_OFFSET_5            0x97
#define RT1305_DAC_OFFSET_6            0x98
#define RT1305_DAC_OFFSET_7            0x99
#define RT1305_DAC_OFFSET_8            0x9a
#define RT1305_DAC_OFFSET_9            0x9b
#define RT1305_DAC_OFFSET_10            0x9c
#define RT1305_DAC_OFFSET_11            0x9d
#define RT1305_DAC_OFFSET_12            0x9e
#define RT1305_DAC_OFFSET_13            0x9f
#define RT1305_DAC_OFFSET_14            0xa0
#define RT1305_TRIM_1                  0xb0
#define RT1305_TRIM_2                  0xb1
#define RT1305_TUNE_INTERNAL_OSC             0xb2
#define RT1305_BIQUAD1_H0_L_28_16 0xc0
#define RT1305_BIQUAD3_A2_R_15_0 0xfb
#define RT1305_MAX_REG	                 0xff

/* CLOCK-1 (0x04) */
#define RT1305_SEL_PLL_SRC_2_MASK			(0x1 << 15)
#define RT1305_SEL_PLL_SRC_2_SFT			15
#define RT1305_SEL_PLL_SRC_2_MCLK			(0x0 << 15)
#define RT1305_SEL_PLL_SRC_2_RCCLK			(0x1 << 15)
#define RT1305_DIV_PLL_SRC_2_MASK			(0x3 << 13)
#define RT1305_DIV_PLL_SRC_2_SFT			13
#define RT1305_SEL_PLL_SRC_1_MASK			(0x3 << 10)
#define RT1305_SEL_PLL_SRC_1_SFT			10
#define RT1305_SEL_PLL_SRC_1_PLL2			(0x0 << 10)
#define RT1305_SEL_PLL_SRC_1_BCLK			(0x1 << 10)
#define RT1305_SEL_PLL_SRC_1_DFLL			(0x2 << 10)
#define RT1305_SEL_FS_SYS_PRE_MASK			(0x3 << 8)
#define RT1305_SEL_FS_SYS_PRE_SFT			8
#define RT1305_SEL_FS_SYS_PRE_MCLK			(0x0 << 8)
#define RT1305_SEL_FS_SYS_PRE_PLL			(0x1 << 8)
#define RT1305_SEL_FS_SYS_PRE_RCCLK			(0x2 << 8)
#define RT1305_DIV_FS_SYS_MASK				(0x7 << 4)
#define RT1305_DIV_FS_SYS_SFT				4

/* PLL1M/N/K Code-1 (0x0c) */
#define RT1305_PLL_1_M_SFT		12
#define RT1305_PLL_1_M_BYPASS_MASK			(0x1 << 11)
#define RT1305_PLL_1_M_BYPASS_SFT		11
#define RT1305_PLL_1_M_BYPASS			(0x1 << 11)
#define RT1305_PLL_1_N_MASK			(0x1ff << 0)

/* DAC Setting (0x14) */
#define RT1305_DVOL_MUTE_L_EN_SFT		15
#define RT1305_DVOL_MUTE_R_EN_SFT		14

/* I2S Setting-1 (0x2d) */
#define RT1305_SEL_I2S_OUT_MODE_MASK		(0x1 << 15)
#define RT1305_SEL_I2S_OUT_MODE_SFT			15
#define RT1305_SEL_I2S_OUT_MODE_S			(0x0 << 15)
#define RT1305_SEL_I2S_OUT_MODE_M			(0x1 << 15)

/* I2S Setting-2 (0x2e) */
#define RT1305_I2S_DF_SEL_MASK			(0x3 << 12)
#define RT1305_I2S_DF_SEL_SFT			12
#define RT1305_I2S_DF_SEL_I2S			(0x0 << 12)
#define RT1305_I2S_DF_SEL_LEFT			(0x1 << 12)
#define RT1305_I2S_DF_SEL_PCM_A			(0x2 << 12)
#define RT1305_I2S_DF_SEL_PCM_B			(0x3 << 12)
#define RT1305_I2S_DL_SEL_MASK			(0x3 << 10)
#define RT1305_I2S_DL_SEL_SFT			10
#define RT1305_I2S_DL_SEL_16B			(0x0 << 10)
#define RT1305_I2S_DL_SEL_20B			(0x1 << 10)
#define RT1305_I2S_DL_SEL_24B			(0x2 << 10)
#define RT1305_I2S_DL_SEL_8B			(0x3 << 10)
#define RT1305_I2S_BCLK_MASK		(0x1 << 9)
#define RT1305_I2S_BCLK_SFT			9
#define RT1305_I2S_BCLK_NORMAL		(0x0 << 9)
#define RT1305_I2S_BCLK_INV			(0x1 << 9)

/* Power Control-1 (0x3a) */
#define RT1305_POW_PDB_JD_MASK				(0x1 << 12)
#define RT1305_POW_PDB_JD				(0x1 << 12)
#define RT1305_POW_PDB_JD_BIT			12
#define RT1305_POW_PLL0_EN				(0x1 << 11)
#define RT1305_POW_PLL0_EN_BIT			11
#define RT1305_POW_PLL1_EN				(0x1 << 10)
#define RT1305_POW_PLL1_EN_BIT			10
#define RT1305_POW_PDB_JD_POLARITY				(0x1 << 9)
#define RT1305_POW_PDB_JD_POLARITY_BIT			9
#define RT1305_POW_MBIAS_LV				(0x1 << 8)
#define RT1305_POW_MBIAS_LV_BIT			8
#define RT1305_POW_BG_MBIAS_LV				(0x1 << 7)
#define RT1305_POW_BG_MBIAS_LV_BIT			7
#define RT1305_POW_LDO2				(0x1 << 6)
#define RT1305_POW_LDO2_BIT			6
#define RT1305_POW_BG2				(0x1 << 5)
#define RT1305_POW_BG2_BIT			5
#define RT1305_POW_LDO2_IB2				(0x1 << 4)
#define RT1305_POW_LDO2_IB2_BIT			4
#define RT1305_POW_VREF				(0x1 << 3)
#define RT1305_POW_VREF_BIT			3
#define RT1305_POW_VREF1				(0x1 << 2)
#define RT1305_POW_VREF1_BIT			2
#define RT1305_POW_VREF2				(0x1 << 1)
#define RT1305_POW_VREF2_BIT			1

/* Power Control-2 (0x3b) */
#define RT1305_POW_DISC_VREF           (1 << 15)
#define RT1305_POW_DISC_VREF_BIT       15
#define RT1305_POW_FASTB_VREF          (1 << 14)
#define RT1305_POW_FASTB_VREF_BIT          14
#define RT1305_POW_ULTRA_FAST_VREF     (1 << 13)
#define RT1305_POW_ULTRA_FAST_VREF_BIT     13
#define RT1305_POW_CKXEN_DAC           (1 << 12)
#define RT1305_POW_CKXEN_DAC_BIT           12
#define RT1305_POW_EN_CKGEN_DAC        (1 << 11)
#define RT1305_POW_EN_CKGEN_DAC_BIT        11
#define RT1305_POW_DAC1_L          (1 << 10)
#define RT1305_POW_DAC1_L_BIT          10
#define RT1305_POW_DAC1_R          (1 << 9)
#define RT1305_POW_DAC1_R_BIT          9
#define RT1305_POW_CLAMP           (1 << 8)
#define RT1305_POW_CLAMP_BIT           8
#define RT1305_POW_BUFL            (1 << 7)
#define RT1305_POW_BUFL_BIT            7
#define RT1305_POW_BUFR              (1 << 6)
#define RT1305_POW_BUFR_BIT              6
#define RT1305_POW_EN_CKGEN_ADC       (1 << 5)
#define RT1305_POW_EN_CKGEN_ADC_BIT       5
#define RT1305_POW_ADC3_L             (1 << 4)
#define RT1305_POW_ADC3_L_BIT             4
#define RT1305_POW_ADC3_R             (1 << 3)
#define RT1305_POW_ADC3_R_BIT             3
#define RT1305_POW_TRIOSC               (1 << 2)
#define RT1305_POW_TRIOSC_BIT               2
#define RT1305_POR_AVDD1              (1 << 1)
#define RT1305_POR_AVDD1_BIT              1
#define RT1305_POR_AVDD2           (1 << 0)
#define RT1305_POR_AVDD2_BIT           0

/* Power Control-3 (0x3c) */
#define RT1305_POW_VSENSE_RCH           (1 << 15)
#define RT1305_POW_VSENSE_RCH_BIT        15
#define RT1305_POW_VSENSE_LCH           (1 << 14)
#define RT1305_POW_VSENSE_LCH_BIT           14
#define RT1305_POW_ISENSE_RCH            (1 << 13)
#define RT1305_POW_ISENSE_RCH_BIT          13
#define RT1305_POW_ISENSE_LCH            (1 << 12)
#define RT1305_POW_ISENSE_LCH_BIT            12
#define RT1305_POW_POR_AVDD1            (1 << 11)
#define RT1305_POW_POR_AVDD1_BIT          11
#define RT1305_POW_POR_AVDD2            (1 << 10)
#define RT1305_POW_POR_AVDD2_BIT            10
#define RT1305_EN_K_HV            (1 << 9)
#define RT1305_EN_K_HV_BIT           9
#define RT1305_EN_PRE_K_HV            (1 << 8)
#define RT1305_EN_PRE_K_HV_BIT           8
#define RT1305_EN_EFUSE_1P8V            (1 << 7)
#define RT1305_EN_EFUSE_1P8V_BIT           7
#define RT1305_EN_EFUSE_5V             (1 << 6)
#define RT1305_EN_EFUSE_5V_BIT           6
#define RT1305_EN_VCM_6172           (1 << 5)
#define RT1305_EN_VCM_6172_BIT          5
#define RT1305_POR_EFUSE           (1 << 4)
#define RT1305_POR_EFUSE_BIT             4

/* Clock Detect (0x3f) */
#define RT1305_SEL_CLK_DET_SRC_MASK			(0x1 << 12)
#define RT1305_SEL_CLK_DET_SRC_SFT			12
#define RT1305_SEL_CLK_DET_SRC_MCLK			(0x0 << 12)
#define RT1305_SEL_CLK_DET_SRC_BCLK			(0x1 << 12)


/* System Clock Source */
enum {
	RT1305_FS_SYS_PRE_S_MCLK,
	RT1305_FS_SYS_PRE_S_PLL1,
	RT1305_FS_SYS_PRE_S_RCCLK,	/* 98.304M Hz */
};

/* PLL Source 1/2 */
enum {
	RT1305_PLL1_S_BCLK,
	RT1305_PLL2_S_MCLK,
	RT1305_PLL2_S_RCCLK,	/* 98.304M Hz */
};

enum {
	RT1305_AIF1,
	RT1305_AIFS
};

#define R0_UPPER 0x2E8BA2 //5.5 ohm
#define R0_LOWER 0x666666 //2.5 ohm

#endif		/* end of _RT1305_H_ */
