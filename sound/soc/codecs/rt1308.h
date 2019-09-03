/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rt1308.h  --  RT1308 ALSA SoC amplifier component driver
 *
 * Copyright 2019 Realtek Semiconductor Corp.
 * Author: Derek Fang <derek.fang@realtek.com>
 *
 */

#ifndef _RT1308_H_
#define _RT1308_H_

#define RT1308_DEVICE_ID_NUM			0x10ec1300

#define RT1308_RESET				0x00
#define RT1308_RESET_N				0x01
#define RT1308_CLK_GATING			0x02
#define RT1308_PLL_1				0x03
#define RT1308_PLL_2				0x04
#define RT1308_PLL_INT				0x05
#define RT1308_CLK_1				0x06
#define RT1308_DATA_PATH			0x07
#define RT1308_CLK_2				0x08
#define RT1308_SIL_DET				0x09
#define RT1308_CLK_DET				0x0a
#define RT1308_DC_DET				0x0b
#define RT1308_DC_DET_THRES			0x0c
#define RT1308_DAC_SET				0x10
#define RT1308_SRC_SET				0x11
#define RT1308_DAC_BUF				0x12
#define RT1308_ADC_SET				0x13
#define RT1308_ADC_SET_INT			0x14
#define RT1308_I2S_SET_1			0x15
#define RT1308_I2S_SET_2			0x16
#define RT1308_I2C_I2S_SDW_SET			0x17
#define RT1308_SDW_REG_RW			0x18
#define RT1308_SDW_REG_RDATA			0x19
#define RT1308_IV_SENSE				0x1a
#define RT1308_I2S_TX_DAC_SET			0x1b
#define RT1308_AD_FILTER_SET			0x1c
#define RT1308_DC_CAL_1				0x20
#define RT1308_DC_CAL_2				0x21
#define RT1308_DC_CAL_L_OFFSET			0x22
#define RT1308_DC_CAL_R_OFFSET			0x23
#define RT1308_PVDD_OFFSET_CTL			0x24
#define RT1308_PVDD_OFFSET_L			0x25
#define RT1308_PVDD_OFFSET_R			0x26
#define RT1308_PVDD_OFFSET_PBTL			0x27
#define RT1308_PVDD_OFFSET_PVDD			0x28
#define RT1308_CAL_OFFSET_DAC_PBTL		0x29
#define RT1308_CAL_OFFSET_DAC_L			0x2a
#define RT1308_CAL_OFFSET_DAC_R			0x2b
#define RT1308_CAL_OFFSET_PWM_L			0x2c
#define RT1308_CAL_OFFSET_PWM_R			0x2d
#define RT1308_CAL_PWM_VOS_ADC_L		0x2e
#define RT1308_CAL_PWM_VOS_ADC_R		0x2f
#define RT1308_CLASS_D_SET_1			0x30
#define RT1308_CLASS_D_SET_2			0x31
#define RT1308_POWER				0x32
#define RT1308_LDO				0x33
#define RT1308_VREF				0x34
#define RT1308_MBIAS				0x35
#define RT1308_POWER_STATUS			0x36
#define RT1308_POWER_INT			0x37
#define RT1308_SINE_TONE_GEN_1			0x50
#define RT1308_SINE_TONE_GEN_2			0x51
#define RT1308_BQ_SET				0x54
#define RT1308_BQ_PARA_UPDATE			0x55
#define RT1308_BQ_PRE_VOL_L			0x56
#define RT1308_BQ_PRE_VOL_R			0x57
#define RT1308_BQ_POST_VOL_L			0x58
#define RT1308_BQ_POST_VOL_R			0x59
#define RT1308_BQ1_L_H0				0x5b
#define RT1308_BQ1_L_B1				0x5c
#define RT1308_BQ1_L_B2				0x5d
#define RT1308_BQ1_L_A1				0x5e
#define RT1308_BQ1_L_A2				0x5f
#define RT1308_BQ1_R_H0				0x60
#define RT1308_BQ1_R_B1				0x61
#define RT1308_BQ1_R_B2				0x62
#define RT1308_BQ1_R_A1				0x63
#define RT1308_BQ1_R_A2				0x64
#define RT1308_BQ2_L_H0				0x65
#define RT1308_BQ2_L_B1				0x66
#define RT1308_BQ2_L_B2				0x67
#define RT1308_BQ2_L_A1				0x68
#define RT1308_BQ2_L_A2				0x69
#define RT1308_BQ2_R_H0				0x6a
#define RT1308_BQ2_R_B1				0x6b
#define RT1308_BQ2_R_B2				0x6c
#define RT1308_BQ2_R_A1				0x6d
#define RT1308_BQ2_R_A2				0x6e
#define RT1308_VEN_DEV_ID			0x70
#define RT1308_VERSION_ID			0x71
#define RT1308_SPK_BOUND			0x72
#define RT1308_BQ1_EQ_L_1			0x73
#define RT1308_BQ1_EQ_L_2			0x74
#define RT1308_BQ1_EQ_L_3			0x75
#define RT1308_BQ1_EQ_R_1			0x76
#define RT1308_BQ1_EQ_R_2			0x77
#define RT1308_BQ1_EQ_R_3			0x78
#define RT1308_BQ2_EQ_L_1			0x79
#define RT1308_BQ2_EQ_L_2			0x7a
#define RT1308_BQ2_EQ_L_3			0x7b
#define RT1308_BQ2_EQ_R_1			0x7c
#define RT1308_BQ2_EQ_R_2			0x7d
#define RT1308_BQ2_EQ_R_3			0x7e
#define RT1308_EFUSE_1				0x7f
#define RT1308_EFUSE_2				0x80
#define RT1308_EFUSE_PROG_PVDD_L		0x81
#define RT1308_EFUSE_PROG_PVDD_R		0x82
#define RT1308_EFUSE_PROG_R0_L			0x83
#define RT1308_EFUSE_PROG_R0_R			0x84
#define RT1308_EFUSE_PROG_DEV			0x85
#define RT1308_EFUSE_READ_PVDD_L		0x86
#define RT1308_EFUSE_READ_PVDD_R		0x87
#define RT1308_EFUSE_READ_PVDD_PTBL		0x88
#define RT1308_EFUSE_READ_DEV			0x89
#define RT1308_EFUSE_READ_R0			0x8a
#define RT1308_EFUSE_READ_ADC_L			0x8b
#define RT1308_EFUSE_READ_ADC_R			0x8c
#define RT1308_EFUSE_READ_ADC_PBTL		0x8d
#define RT1308_EFUSE_RESERVE			0x8e
#define RT1308_PADS_1				0x90
#define RT1308_PADS_2				0x91
#define RT1308_TEST_MODE			0xa0
#define RT1308_TEST_1				0xa1
#define RT1308_TEST_2				0xa2
#define RT1308_TEST_3				0xa3
#define RT1308_TEST_4				0xa4
#define RT1308_EFUSE_DATA_0_MSB			0xb0
#define RT1308_EFUSE_DATA_0_LSB			0xb1
#define RT1308_EFUSE_DATA_1_MSB			0xb2
#define RT1308_EFUSE_DATA_1_LSB			0xb3
#define RT1308_EFUSE_DATA_2_MSB			0xb4
#define RT1308_EFUSE_DATA_2_LSB			0xb5
#define RT1308_EFUSE_DATA_3_MSB			0xb6
#define RT1308_EFUSE_DATA_3_LSB			0xb7
#define RT1308_EFUSE_DATA_TEST_MSB		0xb8
#define RT1308_EFUSE_DATA_TEST_LSB		0xb9
#define RT1308_EFUSE_STATUS_1			0xba
#define RT1308_EFUSE_STATUS_2			0xbb
#define RT1308_TCON_1				0xc0
#define RT1308_TCON_2				0xc1
#define RT1308_DUMMY_REG			0xf0
#define RT1308_MAX_REG				0xff

/* PLL1 M/N/K Code-1 (0x03) */
#define RT1308_PLL1_K_SFT			24
#define RT1308_PLL1_K_MASK			(0x1f << 24)
#define RT1308_PLL1_M_BYPASS_MASK		(0x1 << 23)
#define RT1308_PLL1_M_BYPASS_SFT		23
#define RT1308_PLL1_M_BYPASS			(0x1 << 23)
#define RT1308_PLL1_M_MASK			(0x3f << 16)
#define RT1308_PLL1_M_SFT			16
#define RT1308_PLL1_N_MASK			(0x7f << 8)
#define RT1308_PLL1_N_SFT			8

/* CLOCK-1 (0x06) */
#define RT1308_DIV_FS_SYS_MASK			(0xf << 28)
#define RT1308_DIV_FS_SYS_SFT			28
#define RT1308_SEL_FS_SYS_MASK			(0x7 << 24)
#define RT1308_SEL_FS_SYS_SFT			24
#define RT1308_SEL_FS_SYS_SRC_MCLK		(0x0 << 24)
#define RT1308_SEL_FS_SYS_SRC_BCLK		(0x1 << 24)
#define RT1308_SEL_FS_SYS_SRC_PLL		(0x2 << 24)
#define RT1308_SEL_FS_SYS_SRC_RCCLK		(0x4 << 24)

/* CLOCK-2 (0x08) */
#define RT1308_DIV_PRE_PLL_MASK			(0xf << 28)
#define RT1308_DIV_PRE_PLL_SFT			28
#define RT1308_SEL_PLL_SRC_MASK			(0x7 << 24)
#define RT1308_SEL_PLL_SRC_SFT			24
#define RT1308_SEL_PLL_SRC_MCLK			(0x0 << 24)
#define RT1308_SEL_PLL_SRC_BCLK			(0x1 << 24)
#define RT1308_SEL_PLL_SRC_RCCLK		(0x4 << 24)

/* Clock Detect (0x0a) */
#define RT1308_MCLK_DET_EN_MASK			(0x1 << 25)
#define RT1308_MCLK_DET_EN_SFT			25
#define RT1308_MCLK_DET_EN			(0x1 << 25)
#define RT1308_BCLK_DET_EN_MASK			(0x1 << 24)
#define RT1308_BCLK_DET_EN_SFT			24
#define RT1308_BCLK_DET_EN			(0x1 << 24)

/* DAC Setting (0x10) */
#define RT1308_DVOL_MUTE_R_EN_SFT		7
#define RT1308_DVOL_MUTE_L_EN_SFT		6

/* I2S Setting-1 (0x15) */
#define RT1308_I2S_DF_SEL_MASK			(0x3 << 12)
#define RT1308_I2S_DF_SEL_SFT			12
#define RT1308_I2S_DF_SEL_I2S			(0x0 << 12)
#define RT1308_I2S_DF_SEL_LEFT			(0x1 << 12)
#define RT1308_I2S_DF_SEL_PCM_A			(0x2 << 12)
#define RT1308_I2S_DF_SEL_PCM_B			(0x3 << 12)
#define RT1308_I2S_DL_RX_SEL_MASK		(0x7 << 4)
#define RT1308_I2S_DL_RX_SEL_SFT		4
#define RT1308_I2S_DL_RX_SEL_16B		(0x0 << 4)
#define RT1308_I2S_DL_RX_SEL_20B		(0x1 << 4)
#define RT1308_I2S_DL_RX_SEL_24B		(0x2 << 4)
#define RT1308_I2S_DL_RX_SEL_32B		(0x3 << 4)
#define RT1308_I2S_DL_RX_SEL_8B			(0x4 << 4)
#define RT1308_I2S_DL_TX_SEL_MASK		(0x7 << 0)
#define RT1308_I2S_DL_TX_SEL_SFT		0
#define RT1308_I2S_DL_TX_SEL_16B		(0x0 << 0)
#define RT1308_I2S_DL_TX_SEL_20B		(0x1 << 0)
#define RT1308_I2S_DL_TX_SEL_24B		(0x2 << 0)
#define RT1308_I2S_DL_TX_SEL_32B		(0x3 << 0)
#define RT1308_I2S_DL_TX_SEL_8B			(0x4 << 0)

/* I2S Setting-2 (0x16) */
#define RT1308_I2S_DL_SEL_MASK			(0x7 << 24)
#define RT1308_I2S_DL_SEL_SFT			24
#define RT1308_I2S_DL_SEL_16B			(0x0 << 24)
#define RT1308_I2S_DL_SEL_20B			(0x1 << 24)
#define RT1308_I2S_DL_SEL_24B			(0x2 << 24)
#define RT1308_I2S_DL_SEL_32B			(0x3 << 24)
#define RT1308_I2S_DL_SEL_8B			(0x4 << 24)
#define RT1308_I2S_BCLK_MASK			(0x1 << 14)
#define RT1308_I2S_BCLK_SFT			14
#define RT1308_I2S_BCLK_NORMAL			(0x0 << 14)
#define RT1308_I2S_BCLK_INV			(0x1 << 14)

/* Power Control-1 (0x32) */
#define RT1308_POW_MBIAS20U			(0x1 << 31)
#define RT1308_POW_MBIAS20U_BIT			31
#define RT1308_POW_ALDO				(0x1 << 30)
#define RT1308_POW_ALDO_BIT			30
#define RT1308_POW_DBG				(0x1 << 29)
#define RT1308_POW_DBG_BIT			29
#define RT1308_POW_DACL				(0x1 << 28)
#define RT1308_POW_DACL_BIT			28
#define RT1308_POW_DAC1				(0x1 << 27)
#define RT1308_POW_DAC1_BIT			27
#define RT1308_POW_CLK25M			(0x1 << 26)
#define RT1308_POW_CLK25M_BIT			26
#define RT1308_POW_ADC_R			(0x1 << 25)
#define RT1308_POW_ADC_R_BIT			25
#define RT1308_POW_ADC_L			(0x1 << 24)
#define RT1308_POW_ADC_L_BIT			24
#define RT1308_POW_DLDO				(0x1 << 21)
#define RT1308_POW_DLDO_BIT			21
#define RT1308_POW_VREF				(0x1 << 20)
#define RT1308_POW_VREF_BIT			20
#define RT1308_POW_MIXER_R			(0x1 << 18)
#define RT1308_POW_MIXER_R_BIT			18
#define RT1308_POW_MIXER_L			(0x1 << 17)
#define RT1308_POW_MIXER_L_BIT			17
#define RT1308_POW_MBIAS4U			(0x1 << 16)
#define RT1308_POW_MBIAS4U_BIT			16
#define RT1308_POW_PLL2_LDO_EN			(0x1 << 12)
#define RT1308_POW_PLL2_LDO_EN_BIT		12
#define RT1308_POW_PLL2B_EN			(0x1 << 11)
#define RT1308_POW_PLL2B_EN_BIT			11
#define RT1308_POW_PLL2F_EN			(0x1 << 10)
#define RT1308_POW_PLL2F_EN_BIT			10
#define RT1308_POW_PLL2F2_EN			(0x1 << 9)
#define RT1308_POW_PLL2F2_EN_BIT		9
#define RT1308_POW_PLL2B2_EN			(0x1 << 8)
#define RT1308_POW_PLL2B2_EN_BIT		8

/* Power Control-2 (0x36) */
#define RT1308_POW_PDB_SRC_BIT			(0x1 << 27)
#define RT1308_POW_PDB_MN_BIT			(0x1 << 25)
#define RT1308_POW_PDB_REG_BIT			(0x1 << 24)


/* System Clock Source */
enum {
	RT1308_FS_SYS_S_MCLK,
	RT1308_FS_SYS_S_BCLK,
	RT1308_FS_SYS_S_PLL,
	RT1308_FS_SYS_S_RCCLK,	/* 25.0 MHz */
};

/* PLL Source */
enum {
	RT1308_PLL_S_MCLK,
	RT1308_PLL_S_BCLK,
	RT1308_PLL_S_RCCLK,
};

enum {
	RT1308_AIF1,
	RT1308_AIFS
};

#endif		/* end of _RT1308_H_ */
