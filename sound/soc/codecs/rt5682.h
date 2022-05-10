/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt5682.h  --  RT5682/RT5658 ALSA SoC audio driver
 *
 * Copyright 2018 Realtek Microelectronics
 * Author: Bard Liao <bardliao@realtek.com>
 */

#ifndef __RT5682_H__
#define __RT5682_H__

#include <sound/rt5682.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>

#define DEVICE_ID 0x6530

/* Info */
#define RT5682_RESET				0x0000
#define RT5682_VERSION_ID			0x00fd
#define RT5682_VENDOR_ID			0x00fe
#define RT5682_DEVICE_ID			0x00ff
/*  I/O - Output */
#define RT5682_HP_CTRL_1			0x0002
#define RT5682_HP_CTRL_2			0x0003
#define RT5682_HPL_GAIN				0x0005
#define RT5682_HPR_GAIN				0x0006

#define RT5682_I2C_CTRL				0x0008

/* I/O - Input */
#define RT5682_CBJ_BST_CTRL			0x000b
#define RT5682_CBJ_CTRL_1			0x0010
#define RT5682_CBJ_CTRL_2			0x0011
#define RT5682_CBJ_CTRL_3			0x0012
#define RT5682_CBJ_CTRL_4			0x0013
#define RT5682_CBJ_CTRL_5			0x0014
#define RT5682_CBJ_CTRL_6			0x0015
#define RT5682_CBJ_CTRL_7			0x0016
/* I/O - ADC/DAC/DMIC */
#define RT5682_DAC1_DIG_VOL			0x0019
#define RT5682_STO1_ADC_DIG_VOL			0x001c
#define RT5682_STO1_ADC_BOOST			0x001f
#define RT5682_HP_IMP_GAIN_1			0x0022
#define RT5682_HP_IMP_GAIN_2			0x0023
/* Mixer - D-D */
#define RT5682_SIDETONE_CTRL			0x0024
#define RT5682_STO1_ADC_MIXER			0x0026
#define RT5682_AD_DA_MIXER			0x0029
#define RT5682_STO1_DAC_MIXER			0x002a
#define RT5682_A_DAC1_MUX			0x002b
#define RT5682_DIG_INF2_DATA			0x0030
/* Mixer - ADC */
#define RT5682_REC_MIXER			0x003c
#define RT5682_CAL_REC				0x0044
#define RT5682_ALC_BACK_GAIN			0x0049
/* Power */
#define RT5682_PWR_DIG_1			0x0061
#define RT5682_PWR_DIG_2			0x0062
#define RT5682_PWR_ANLG_1			0x0063
#define RT5682_PWR_ANLG_2			0x0064
#define RT5682_PWR_ANLG_3			0x0065
#define RT5682_PWR_MIXER			0x0066
#define RT5682_PWR_VOL				0x0067
/* Clock Detect */
#define RT5682_CLK_DET				0x006b
/* Filter Auto Reset */
#define RT5682_RESET_LPF_CTRL			0x006c
#define RT5682_RESET_HPF_CTRL			0x006d
/* DMIC */
#define RT5682_DMIC_CTRL_1			0x006e
/* Format - ADC/DAC */
#define RT5682_I2S1_SDP				0x0070
#define RT5682_I2S2_SDP				0x0071
#define RT5682_ADDA_CLK_1			0x0073
#define RT5682_ADDA_CLK_2			0x0074
#define RT5682_I2S1_F_DIV_CTRL_1		0x0075
#define RT5682_I2S1_F_DIV_CTRL_2		0x0076
/* Format - TDM Control */
#define RT5682_TDM_CTRL				0x0079
#define RT5682_TDM_ADDA_CTRL_1			0x007a
#define RT5682_TDM_ADDA_CTRL_2			0x007b
#define RT5682_DATA_SEL_CTRL_1			0x007c
#define RT5682_TDM_TCON_CTRL			0x007e
/* Function - Analog */
#define RT5682_GLB_CLK				0x0080
#define RT5682_PLL_CTRL_1			0x0081
#define RT5682_PLL_CTRL_2			0x0082
#define RT5682_PLL_TRACK_1			0x0083
#define RT5682_PLL_TRACK_2			0x0084
#define RT5682_PLL_TRACK_3			0x0085
#define RT5682_PLL_TRACK_4			0x0086
#define RT5682_PLL_TRACK_5			0x0087
#define RT5682_PLL_TRACK_6			0x0088
#define RT5682_PLL_TRACK_11			0x008c
#define RT5682_SDW_REF_CLK			0x008d
#define RT5682_DEPOP_1				0x008e
#define RT5682_DEPOP_2				0x008f
#define RT5682_HP_CHARGE_PUMP_1			0x0091
#define RT5682_HP_CHARGE_PUMP_2			0x0092
#define RT5682_MICBIAS_1			0x0093
#define RT5682_MICBIAS_2			0x0094
#define RT5682_PLL_TRACK_12			0x0098
#define RT5682_PLL_TRACK_14			0x009a
#define RT5682_PLL2_CTRL_1			0x009b
#define RT5682_PLL2_CTRL_2			0x009c
#define RT5682_PLL2_CTRL_3			0x009d
#define RT5682_PLL2_CTRL_4			0x009e
#define RT5682_RC_CLK_CTRL			0x009f
#define RT5682_I2S_M_CLK_CTRL_1			0x00a0
#define RT5682_I2S2_F_DIV_CTRL_1		0x00a3
#define RT5682_I2S2_F_DIV_CTRL_2		0x00a4
/* Function - Digital */
#define RT5682_EQ_CTRL_1			0x00ae
#define RT5682_EQ_CTRL_2			0x00af
#define RT5682_IRQ_CTRL_1			0x00b6
#define RT5682_IRQ_CTRL_2			0x00b7
#define RT5682_IRQ_CTRL_3			0x00b8
#define RT5682_IRQ_CTRL_4			0x00b9
#define RT5682_INT_ST_1				0x00be
#define RT5682_GPIO_CTRL_1			0x00c0
#define RT5682_GPIO_CTRL_2			0x00c1
#define RT5682_GPIO_CTRL_3			0x00c2
#define RT5682_HP_AMP_DET_CTRL_1		0x00d0
#define RT5682_HP_AMP_DET_CTRL_2		0x00d1
#define RT5682_MID_HP_AMP_DET			0x00d2
#define RT5682_LOW_HP_AMP_DET			0x00d3
#define RT5682_DELAY_BUF_CTRL			0x00d4
#define RT5682_SV_ZCD_1				0x00d9
#define RT5682_SV_ZCD_2				0x00da
#define RT5682_IL_CMD_1				0x00db
#define RT5682_IL_CMD_2				0x00dc
#define RT5682_IL_CMD_3				0x00dd
#define RT5682_IL_CMD_4				0x00de
#define RT5682_IL_CMD_5				0x00df
#define RT5682_IL_CMD_6				0x00e0
#define RT5682_4BTN_IL_CMD_1			0x00e2
#define RT5682_4BTN_IL_CMD_2			0x00e3
#define RT5682_4BTN_IL_CMD_3			0x00e4
#define RT5682_4BTN_IL_CMD_4			0x00e5
#define RT5682_4BTN_IL_CMD_5			0x00e6
#define RT5682_4BTN_IL_CMD_6			0x00e7
#define RT5682_4BTN_IL_CMD_7			0x00e8

#define RT5682_ADC_STO1_HP_CTRL_1		0x00ea
#define RT5682_ADC_STO1_HP_CTRL_2		0x00eb
#define RT5682_AJD1_CTRL			0x00f0
#define RT5682_JD1_THD				0x00f1
#define RT5682_JD2_THD				0x00f2
#define RT5682_JD_CTRL_1			0x00f6
/* General Control */
#define RT5682_DUMMY_1				0x00fa
#define RT5682_DUMMY_2				0x00fb
#define RT5682_DUMMY_3				0x00fc

#define RT5682_DAC_ADC_DIG_VOL1			0x0100
#define RT5682_BIAS_CUR_CTRL_2			0x010b
#define RT5682_BIAS_CUR_CTRL_3			0x010c
#define RT5682_BIAS_CUR_CTRL_4			0x010d
#define RT5682_BIAS_CUR_CTRL_5			0x010e
#define RT5682_BIAS_CUR_CTRL_6			0x010f
#define RT5682_BIAS_CUR_CTRL_7			0x0110
#define RT5682_BIAS_CUR_CTRL_8			0x0111
#define RT5682_BIAS_CUR_CTRL_9			0x0112
#define RT5682_BIAS_CUR_CTRL_10			0x0113
#define RT5682_VREF_REC_OP_FB_CAP_CTRL		0x0117
#define RT5682_CHARGE_PUMP_1			0x0125
#define RT5682_DIG_IN_CTRL_1			0x0132
#define RT5682_PAD_DRIVING_CTRL			0x0136
#define RT5682_SOFT_RAMP_DEPOP			0x0138
#define RT5682_CHOP_DAC				0x013a
#define RT5682_CHOP_ADC				0x013b
#define RT5682_CALIB_ADC_CTRL			0x013c
#define RT5682_VOL_TEST				0x013f
#define RT5682_SPKVDD_DET_STA			0x0142
#define RT5682_TEST_MODE_CTRL_1			0x0145
#define RT5682_TEST_MODE_CTRL_2			0x0146
#define RT5682_TEST_MODE_CTRL_3			0x0147
#define RT5682_TEST_MODE_CTRL_4			0x0148
#define RT5682_TEST_MODE_CTRL_5			0x0149
#define RT5682_PLL1_INTERNAL			0x0150
#define RT5682_PLL2_INTERNAL			0x0156
#define RT5682_STO_NG2_CTRL_1			0x0160
#define RT5682_STO_NG2_CTRL_2			0x0161
#define RT5682_STO_NG2_CTRL_3			0x0162
#define RT5682_STO_NG2_CTRL_4			0x0163
#define RT5682_STO_NG2_CTRL_5			0x0164
#define RT5682_STO_NG2_CTRL_6			0x0165
#define RT5682_STO_NG2_CTRL_7			0x0166
#define RT5682_STO_NG2_CTRL_8			0x0167
#define RT5682_STO_NG2_CTRL_9			0x0168
#define RT5682_STO_NG2_CTRL_10			0x0169
#define RT5682_STO1_DAC_SIL_DET			0x0190
#define RT5682_SIL_PSV_CTRL1			0x0194
#define RT5682_SIL_PSV_CTRL2			0x0195
#define RT5682_SIL_PSV_CTRL3			0x0197
#define RT5682_SIL_PSV_CTRL4			0x0198
#define RT5682_SIL_PSV_CTRL5			0x0199
#define RT5682_HP_IMP_SENS_CTRL_01		0x01af
#define RT5682_HP_IMP_SENS_CTRL_02		0x01b0
#define RT5682_HP_IMP_SENS_CTRL_03		0x01b1
#define RT5682_HP_IMP_SENS_CTRL_04		0x01b2
#define RT5682_HP_IMP_SENS_CTRL_05		0x01b3
#define RT5682_HP_IMP_SENS_CTRL_06		0x01b4
#define RT5682_HP_IMP_SENS_CTRL_07		0x01b5
#define RT5682_HP_IMP_SENS_CTRL_08		0x01b6
#define RT5682_HP_IMP_SENS_CTRL_09		0x01b7
#define RT5682_HP_IMP_SENS_CTRL_10		0x01b8
#define RT5682_HP_IMP_SENS_CTRL_11		0x01b9
#define RT5682_HP_IMP_SENS_CTRL_12		0x01ba
#define RT5682_HP_IMP_SENS_CTRL_13		0x01bb
#define RT5682_HP_IMP_SENS_CTRL_14		0x01bc
#define RT5682_HP_IMP_SENS_CTRL_15		0x01bd
#define RT5682_HP_IMP_SENS_CTRL_16		0x01be
#define RT5682_HP_IMP_SENS_CTRL_17		0x01bf
#define RT5682_HP_IMP_SENS_CTRL_18		0x01c0
#define RT5682_HP_IMP_SENS_CTRL_19		0x01c1
#define RT5682_HP_IMP_SENS_CTRL_20		0x01c2
#define RT5682_HP_IMP_SENS_CTRL_21		0x01c3
#define RT5682_HP_IMP_SENS_CTRL_22		0x01c4
#define RT5682_HP_IMP_SENS_CTRL_23		0x01c5
#define RT5682_HP_IMP_SENS_CTRL_24		0x01c6
#define RT5682_HP_IMP_SENS_CTRL_25		0x01c7
#define RT5682_HP_IMP_SENS_CTRL_26		0x01c8
#define RT5682_HP_IMP_SENS_CTRL_27		0x01c9
#define RT5682_HP_IMP_SENS_CTRL_28		0x01ca
#define RT5682_HP_IMP_SENS_CTRL_29		0x01cb
#define RT5682_HP_IMP_SENS_CTRL_30		0x01cc
#define RT5682_HP_IMP_SENS_CTRL_31		0x01cd
#define RT5682_HP_IMP_SENS_CTRL_32		0x01ce
#define RT5682_HP_IMP_SENS_CTRL_33		0x01cf
#define RT5682_HP_IMP_SENS_CTRL_34		0x01d0
#define RT5682_HP_IMP_SENS_CTRL_35		0x01d1
#define RT5682_HP_IMP_SENS_CTRL_36		0x01d2
#define RT5682_HP_IMP_SENS_CTRL_37		0x01d3
#define RT5682_HP_IMP_SENS_CTRL_38		0x01d4
#define RT5682_HP_IMP_SENS_CTRL_39		0x01d5
#define RT5682_HP_IMP_SENS_CTRL_40		0x01d6
#define RT5682_HP_IMP_SENS_CTRL_41		0x01d7
#define RT5682_HP_IMP_SENS_CTRL_42		0x01d8
#define RT5682_HP_IMP_SENS_CTRL_43		0x01d9
#define RT5682_HP_LOGIC_CTRL_1			0x01da
#define RT5682_HP_LOGIC_CTRL_2			0x01db
#define RT5682_HP_LOGIC_CTRL_3			0x01dc
#define RT5682_HP_CALIB_CTRL_1			0x01de
#define RT5682_HP_CALIB_CTRL_2			0x01df
#define RT5682_HP_CALIB_CTRL_3			0x01e0
#define RT5682_HP_CALIB_CTRL_4			0x01e1
#define RT5682_HP_CALIB_CTRL_5			0x01e2
#define RT5682_HP_CALIB_CTRL_6			0x01e3
#define RT5682_HP_CALIB_CTRL_7			0x01e4
#define RT5682_HP_CALIB_CTRL_9			0x01e6
#define RT5682_HP_CALIB_CTRL_10			0x01e7
#define RT5682_HP_CALIB_CTRL_11			0x01e8
#define RT5682_HP_CALIB_STA_1			0x01ea
#define RT5682_HP_CALIB_STA_2			0x01eb
#define RT5682_HP_CALIB_STA_3			0x01ec
#define RT5682_HP_CALIB_STA_4			0x01ed
#define RT5682_HP_CALIB_STA_5			0x01ee
#define RT5682_HP_CALIB_STA_6			0x01ef
#define RT5682_HP_CALIB_STA_7			0x01f0
#define RT5682_HP_CALIB_STA_8			0x01f1
#define RT5682_HP_CALIB_STA_9			0x01f2
#define RT5682_HP_CALIB_STA_10			0x01f3
#define RT5682_HP_CALIB_STA_11			0x01f4
#define RT5682_SAR_IL_CMD_1			0x0210
#define RT5682_SAR_IL_CMD_2			0x0211
#define RT5682_SAR_IL_CMD_3			0x0212
#define RT5682_SAR_IL_CMD_4			0x0213
#define RT5682_SAR_IL_CMD_5			0x0214
#define RT5682_SAR_IL_CMD_6			0x0215
#define RT5682_SAR_IL_CMD_7			0x0216
#define RT5682_SAR_IL_CMD_8			0x0217
#define RT5682_SAR_IL_CMD_9			0x0218
#define RT5682_SAR_IL_CMD_10			0x0219
#define RT5682_SAR_IL_CMD_11			0x021a
#define RT5682_SAR_IL_CMD_12			0x021b
#define RT5682_SAR_IL_CMD_13			0x021c
#define RT5682_EFUSE_CTRL_1			0x0250
#define RT5682_EFUSE_CTRL_2			0x0251
#define RT5682_EFUSE_CTRL_3			0x0252
#define RT5682_EFUSE_CTRL_4			0x0253
#define RT5682_EFUSE_CTRL_5			0x0254
#define RT5682_EFUSE_CTRL_6			0x0255
#define RT5682_EFUSE_CTRL_7			0x0256
#define RT5682_EFUSE_CTRL_8			0x0257
#define RT5682_EFUSE_CTRL_9			0x0258
#define RT5682_EFUSE_CTRL_10			0x0259
#define RT5682_EFUSE_CTRL_11			0x025a
#define RT5682_JD_TOP_VC_VTRL			0x0270
#define RT5682_DRC1_CTRL_0			0x02ff
#define RT5682_DRC1_CTRL_1			0x0300
#define RT5682_DRC1_CTRL_2			0x0301
#define RT5682_DRC1_CTRL_3			0x0302
#define RT5682_DRC1_CTRL_4			0x0303
#define RT5682_DRC1_CTRL_5			0x0304
#define RT5682_DRC1_CTRL_6			0x0305
#define RT5682_DRC1_HARD_LMT_CTRL_1		0x0306
#define RT5682_DRC1_HARD_LMT_CTRL_2		0x0307
#define RT5682_DRC1_PRIV_1			0x0310
#define RT5682_DRC1_PRIV_2			0x0311
#define RT5682_DRC1_PRIV_3			0x0312
#define RT5682_DRC1_PRIV_4			0x0313
#define RT5682_DRC1_PRIV_5			0x0314
#define RT5682_DRC1_PRIV_6			0x0315
#define RT5682_DRC1_PRIV_7			0x0316
#define RT5682_DRC1_PRIV_8			0x0317
#define RT5682_EQ_AUTO_RCV_CTRL1		0x03c0
#define RT5682_EQ_AUTO_RCV_CTRL2		0x03c1
#define RT5682_EQ_AUTO_RCV_CTRL3		0x03c2
#define RT5682_EQ_AUTO_RCV_CTRL4		0x03c3
#define RT5682_EQ_AUTO_RCV_CTRL5		0x03c4
#define RT5682_EQ_AUTO_RCV_CTRL6		0x03c5
#define RT5682_EQ_AUTO_RCV_CTRL7		0x03c6
#define RT5682_EQ_AUTO_RCV_CTRL8		0x03c7
#define RT5682_EQ_AUTO_RCV_CTRL9		0x03c8
#define RT5682_EQ_AUTO_RCV_CTRL10		0x03c9
#define RT5682_EQ_AUTO_RCV_CTRL11		0x03ca
#define RT5682_EQ_AUTO_RCV_CTRL12		0x03cb
#define RT5682_EQ_AUTO_RCV_CTRL13		0x03cc
#define RT5682_ADC_L_EQ_LPF1_A1			0x03d0
#define RT5682_R_EQ_LPF1_A1			0x03d1
#define RT5682_L_EQ_LPF1_H0			0x03d2
#define RT5682_R_EQ_LPF1_H0			0x03d3
#define RT5682_L_EQ_BPF1_A1			0x03d4
#define RT5682_R_EQ_BPF1_A1			0x03d5
#define RT5682_L_EQ_BPF1_A2			0x03d6
#define RT5682_R_EQ_BPF1_A2			0x03d7
#define RT5682_L_EQ_BPF1_H0			0x03d8
#define RT5682_R_EQ_BPF1_H0			0x03d9
#define RT5682_L_EQ_BPF2_A1			0x03da
#define RT5682_R_EQ_BPF2_A1			0x03db
#define RT5682_L_EQ_BPF2_A2			0x03dc
#define RT5682_R_EQ_BPF2_A2			0x03dd
#define RT5682_L_EQ_BPF2_H0			0x03de
#define RT5682_R_EQ_BPF2_H0			0x03df
#define RT5682_L_EQ_BPF3_A1			0x03e0
#define RT5682_R_EQ_BPF3_A1			0x03e1
#define RT5682_L_EQ_BPF3_A2			0x03e2
#define RT5682_R_EQ_BPF3_A2			0x03e3
#define RT5682_L_EQ_BPF3_H0			0x03e4
#define RT5682_R_EQ_BPF3_H0			0x03e5
#define RT5682_L_EQ_BPF4_A1			0x03e6
#define RT5682_R_EQ_BPF4_A1			0x03e7
#define RT5682_L_EQ_BPF4_A2			0x03e8
#define RT5682_R_EQ_BPF4_A2			0x03e9
#define RT5682_L_EQ_BPF4_H0			0x03ea
#define RT5682_R_EQ_BPF4_H0			0x03eb
#define RT5682_L_EQ_HPF1_A1			0x03ec
#define RT5682_R_EQ_HPF1_A1			0x03ed
#define RT5682_L_EQ_HPF1_H0			0x03ee
#define RT5682_R_EQ_HPF1_H0			0x03ef
#define RT5682_L_EQ_PRE_VOL			0x03f0
#define RT5682_R_EQ_PRE_VOL			0x03f1
#define RT5682_L_EQ_POST_VOL			0x03f2
#define RT5682_R_EQ_POST_VOL			0x03f3
#define RT5682_I2C_MODE				0xffff


/* global definition */
#define RT5682_L_MUTE				(0x1 << 15)
#define RT5682_L_MUTE_SFT			15
#define RT5682_VOL_L_MUTE			(0x1 << 14)
#define RT5682_VOL_L_SFT			14
#define RT5682_R_MUTE				(0x1 << 7)
#define RT5682_R_MUTE_SFT			7
#define RT5682_VOL_R_MUTE			(0x1 << 6)
#define RT5682_VOL_R_SFT			6
#define RT5682_L_VOL_MASK			(0x3f << 8)
#define RT5682_L_VOL_SFT			8
#define RT5682_R_VOL_MASK			(0x3f)
#define RT5682_R_VOL_SFT			0

/* Headphone Amp Control 2 (0x0003) */
#define RT5682_HP_C2_DAC_AMP_MUTE_SFT		15
#define RT5682_HP_C2_DAC_AMP_MUTE		(0x1 << 15)
#define RT5682_HP_C2_DAC_L_EN_SFT		14
#define RT5682_HP_C2_DAC_L_EN			(0x1 << 14)
#define RT5682_HP_C2_DAC_R_EN_SFT		13
#define RT5682_HP_C2_DAC_R_EN			(0x1 << 13)

/*Headphone Amp L/R Analog Gain and Digital NG2 Gain Control (0x0005 0x0006)*/
#define RT5682_G_HP				(0xf << 8)
#define RT5682_G_HP_SFT				8
#define RT5682_G_STO_DA_DMIX			(0xf)
#define RT5682_G_STO_DA_SFT			0

/* CBJ Control (0x000b) */
#define RT5682_BST_CBJ_MASK			(0xf << 8)
#define RT5682_BST_CBJ_SFT			8

/* Embeeded Jack and Type Detection Control 1 (0x0010) */
#define RT5682_EMB_JD_EN			(0x1 << 15)
#define RT5682_EMB_JD_EN_SFT			15
#define RT5682_EMB_JD_RST			(0x1 << 14)
#define RT5682_JD_MODE				(0x1 << 13)
#define RT5682_JD_MODE_SFT			13
#define RT5682_DET_TYPE				(0x1 << 12)
#define RT5682_DET_TYPE_SFT			12
#define RT5682_POLA_EXT_JD_MASK			(0x1 << 11)
#define RT5682_POLA_EXT_JD_LOW			(0x1 << 11)
#define RT5682_POLA_EXT_JD_HIGH			(0x0 << 11)
#define RT5682_EXT_JD_DIG			(0x1 << 9)
#define RT5682_POL_FAST_OFF_MASK		(0x1 << 8)
#define RT5682_POL_FAST_OFF_HIGH		(0x1 << 8)
#define RT5682_POL_FAST_OFF_LOW			(0x0 << 8)
#define RT5682_FAST_OFF_MASK			(0x1 << 7)
#define RT5682_FAST_OFF_EN			(0x1 << 7)
#define RT5682_FAST_OFF_DIS			(0x0 << 7)
#define RT5682_VREF_POW_MASK			(0x1 << 6)
#define RT5682_VREF_POW_FSM			(0x0 << 6)
#define RT5682_VREF_POW_REG			(0x1 << 6)
#define RT5682_MB1_PATH_MASK			(0x1 << 5)
#define RT5682_CTRL_MB1_REG			(0x1 << 5)
#define RT5682_CTRL_MB1_FSM			(0x0 << 5)
#define RT5682_MB2_PATH_MASK			(0x1 << 4)
#define RT5682_CTRL_MB2_REG			(0x1 << 4)
#define RT5682_CTRL_MB2_FSM			(0x0 << 4)
#define RT5682_TRIG_JD_MASK			(0x1 << 3)
#define RT5682_TRIG_JD_HIGH			(0x1 << 3)
#define RT5682_TRIG_JD_LOW			(0x0 << 3)
#define RT5682_MIC_CAP_MASK			(0x1 << 1)
#define RT5682_MIC_CAP_HS			(0x1 << 1)
#define RT5682_MIC_CAP_HP			(0x0 << 1)
#define RT5682_MIC_CAP_SRC_MASK			(0x1)
#define RT5682_MIC_CAP_SRC_REG			(0x1)
#define RT5682_MIC_CAP_SRC_ANA			(0x0)

/* Embeeded Jack and Type Detection Control 2 (0x0011) */
#define RT5682_EXT_JD_SRC			(0x7 << 4)
#define RT5682_EXT_JD_SRC_SFT			4
#define RT5682_EXT_JD_SRC_GPIO_JD1		(0x0 << 4)
#define RT5682_EXT_JD_SRC_GPIO_JD2		(0x1 << 4)
#define RT5682_EXT_JD_SRC_JDH			(0x2 << 4)
#define RT5682_EXT_JD_SRC_JDL			(0x3 << 4)
#define RT5682_EXT_JD_SRC_MANUAL		(0x4 << 4)
#define RT5682_JACK_TYPE_MASK			(0x3)

/* Combo Jack and Type Detection Control 3 (0x0012) */
#define RT5682_CBJ_IN_BUF_EN			(0x1 << 7)

/* Combo Jack and Type Detection Control 4 (0x0013) */
#define RT5682_SEL_SHT_MID_TON_MASK		(0x3 << 12)
#define RT5682_SEL_SHT_MID_TON_2		(0x0 << 12)
#define RT5682_SEL_SHT_MID_TON_3		(0x1 << 12)
#define RT5682_CBJ_JD_TEST_MASK			(0x1 << 6)
#define RT5682_CBJ_JD_TEST_NORM			(0x0 << 6)
#define RT5682_CBJ_JD_TEST_MODE			(0x1 << 6)

/* DAC1 Digital Volume (0x0019) */
#define RT5682_DAC_L1_VOL_MASK			(0xff << 8)
#define RT5682_DAC_L1_VOL_SFT			8
#define RT5682_DAC_R1_VOL_MASK			(0xff)
#define RT5682_DAC_R1_VOL_SFT			0

/* ADC Digital Volume Control (0x001c) */
#define RT5682_ADC_L_VOL_MASK			(0x7f << 8)
#define RT5682_ADC_L_VOL_SFT			8
#define RT5682_ADC_R_VOL_MASK			(0x7f)
#define RT5682_ADC_R_VOL_SFT			0

/* Stereo1 ADC Boost Gain Control (0x001f) */
#define RT5682_STO1_ADC_L_BST_MASK		(0x3 << 14)
#define RT5682_STO1_ADC_L_BST_SFT		14
#define RT5682_STO1_ADC_R_BST_MASK		(0x3 << 12)
#define RT5682_STO1_ADC_R_BST_SFT		12

/* Sidetone Control (0x0024) */
#define RT5682_ST_SRC_SEL			(0x1 << 8)
#define RT5682_ST_SRC_SFT			8
#define RT5682_ST_EN_MASK			(0x1 << 6)
#define RT5682_ST_DIS				(0x0 << 6)
#define RT5682_ST_EN				(0x1 << 6)
#define RT5682_ST_EN_SFT			6

/* Stereo1 ADC Mixer Control (0x0026) */
#define RT5682_M_STO1_ADC_L1			(0x1 << 15)
#define RT5682_M_STO1_ADC_L1_SFT		15
#define RT5682_M_STO1_ADC_L2			(0x1 << 14)
#define RT5682_M_STO1_ADC_L2_SFT		14
#define RT5682_STO1_ADC1L_SRC_MASK		(0x1 << 13)
#define RT5682_STO1_ADC1L_SRC_SFT		13
#define RT5682_STO1_ADC1_SRC_ADC		(0x1 << 13)
#define RT5682_STO1_ADC1_SRC_DACMIX		(0x0 << 13)
#define RT5682_STO1_ADC2L_SRC_MASK		(0x1 << 12)
#define RT5682_STO1_ADC2L_SRC_SFT		12
#define RT5682_STO1_ADCL_SRC_MASK		(0x3 << 10)
#define RT5682_STO1_ADCL_SRC_SFT		10
#define RT5682_STO1_DD_L_SRC_MASK		(0x1 << 9)
#define RT5682_STO1_DD_L_SRC_SFT		9
#define RT5682_STO1_DMIC_SRC_MASK		(0x1 << 8)
#define RT5682_STO1_DMIC_SRC_SFT		8
#define RT5682_STO1_DMIC_SRC_DMIC2		(0x1 << 8)
#define RT5682_STO1_DMIC_SRC_DMIC1		(0x0 << 8)
#define RT5682_M_STO1_ADC_R1			(0x1 << 7)
#define RT5682_M_STO1_ADC_R1_SFT		7
#define RT5682_M_STO1_ADC_R2			(0x1 << 6)
#define RT5682_M_STO1_ADC_R2_SFT		6
#define RT5682_STO1_ADC1R_SRC_MASK		(0x1 << 5)
#define RT5682_STO1_ADC1R_SRC_SFT		5
#define RT5682_STO1_ADC2R_SRC_MASK		(0x1 << 4)
#define RT5682_STO1_ADC2R_SRC_SFT		4
#define RT5682_STO1_ADCR_SRC_MASK		(0x3 << 2)
#define RT5682_STO1_ADCR_SRC_SFT		2

/* ADC Mixer to DAC Mixer Control (0x0029) */
#define RT5682_M_ADCMIX_L			(0x1 << 15)
#define RT5682_M_ADCMIX_L_SFT			15
#define RT5682_M_DAC1_L				(0x1 << 14)
#define RT5682_M_DAC1_L_SFT			14
#define RT5682_DAC1_R_SEL_MASK			(0x1 << 10)
#define RT5682_DAC1_R_SEL_SFT			10
#define RT5682_DAC1_L_SEL_MASK			(0x1 << 8)
#define RT5682_DAC1_L_SEL_SFT			8
#define RT5682_M_ADCMIX_R			(0x1 << 7)
#define RT5682_M_ADCMIX_R_SFT			7
#define RT5682_M_DAC1_R				(0x1 << 6)
#define RT5682_M_DAC1_R_SFT			6

/* Stereo1 DAC Mixer Control (0x002a) */
#define RT5682_M_DAC_L1_STO_L			(0x1 << 15)
#define RT5682_M_DAC_L1_STO_L_SFT		15
#define RT5682_G_DAC_L1_STO_L_MASK		(0x1 << 14)
#define RT5682_G_DAC_L1_STO_L_SFT		14
#define RT5682_M_DAC_R1_STO_L			(0x1 << 13)
#define RT5682_M_DAC_R1_STO_L_SFT		13
#define RT5682_G_DAC_R1_STO_L_MASK		(0x1 << 12)
#define RT5682_G_DAC_R1_STO_L_SFT		12
#define RT5682_M_DAC_L1_STO_R			(0x1 << 7)
#define RT5682_M_DAC_L1_STO_R_SFT		7
#define RT5682_G_DAC_L1_STO_R_MASK		(0x1 << 6)
#define RT5682_G_DAC_L1_STO_R_SFT		6
#define RT5682_M_DAC_R1_STO_R			(0x1 << 5)
#define RT5682_M_DAC_R1_STO_R_SFT		5
#define RT5682_G_DAC_R1_STO_R_MASK		(0x1 << 4)
#define RT5682_G_DAC_R1_STO_R_SFT		4

/* Analog DAC1 Input Source Control (0x002b) */
#define RT5682_M_ST_STO_L			(0x1 << 9)
#define RT5682_M_ST_STO_L_SFT			9
#define RT5682_M_ST_STO_R			(0x1 << 8)
#define RT5682_M_ST_STO_R_SFT			8
#define RT5682_DAC_L1_SRC_MASK			(0x3 << 4)
#define RT5682_A_DACL1_SFT			4
#define RT5682_DAC_R1_SRC_MASK			(0x3)
#define RT5682_A_DACR1_SFT			0

/* Digital Interface Data Control (0x0030) */
#define RT5682_IF2_ADC_SEL_MASK			(0x3 << 0)
#define RT5682_IF2_ADC_SEL_SFT			0

/* REC Left Mixer Control 2 (0x003c) */
#define RT5682_G_CBJ_RM1_L			(0x7 << 10)
#define RT5682_G_CBJ_RM1_L_SFT			10
#define RT5682_M_CBJ_RM1_L			(0x1 << 7)
#define RT5682_M_CBJ_RM1_L_SFT			7

/* Power Management for Digital 1 (0x0061) */
#define RT5682_PWR_I2S1				(0x1 << 15)
#define RT5682_PWR_I2S1_BIT			15
#define RT5682_PWR_I2S2				(0x1 << 14)
#define RT5682_PWR_I2S2_BIT			14
#define RT5682_PWR_DAC_L1			(0x1 << 11)
#define RT5682_PWR_DAC_L1_BIT			11
#define RT5682_PWR_DAC_R1			(0x1 << 10)
#define RT5682_PWR_DAC_R1_BIT			10
#define RT5682_PWR_LDO				(0x1 << 8)
#define RT5682_PWR_LDO_BIT			8
#define RT5682_PWR_ADC_L1			(0x1 << 4)
#define RT5682_PWR_ADC_L1_BIT			4
#define RT5682_PWR_ADC_R1			(0x1 << 3)
#define RT5682_PWR_ADC_R1_BIT			3
#define RT5682_DIG_GATE_CTRL			(0x1 << 0)
#define RT5682_DIG_GATE_CTRL_SFT		0


/* Power Management for Digital 2 (0x0062) */
#define RT5682_PWR_ADC_S1F			(0x1 << 15)
#define RT5682_PWR_ADC_S1F_BIT			15
#define RT5682_PWR_DAC_S1F			(0x1 << 10)
#define RT5682_PWR_DAC_S1F_BIT			10

/* Power Management for Analog 1 (0x0063) */
#define RT5682_PWR_VREF1			(0x1 << 15)
#define RT5682_PWR_VREF1_BIT			15
#define RT5682_PWR_FV1				(0x1 << 14)
#define RT5682_PWR_FV1_BIT			14
#define RT5682_PWR_VREF2			(0x1 << 13)
#define RT5682_PWR_VREF2_BIT			13
#define RT5682_PWR_FV2				(0x1 << 12)
#define RT5682_PWR_FV2_BIT			12
#define RT5682_LDO1_DBG_MASK			(0x3 << 10)
#define RT5682_PWR_MB				(0x1 << 9)
#define RT5682_PWR_MB_BIT			9
#define RT5682_PWR_BG				(0x1 << 7)
#define RT5682_PWR_BG_BIT			7
#define RT5682_LDO1_BYPASS_MASK			(0x1 << 6)
#define RT5682_LDO1_BYPASS			(0x1 << 6)
#define RT5682_LDO1_NOT_BYPASS			(0x0 << 6)
#define RT5682_PWR_MA_BIT			6
#define RT5682_LDO1_DVO_MASK			(0x3 << 4)
#define RT5682_LDO1_DVO_09			(0x0 << 4)
#define RT5682_LDO1_DVO_10			(0x1 << 4)
#define RT5682_LDO1_DVO_12			(0x2 << 4)
#define RT5682_LDO1_DVO_14			(0x3 << 4)
#define RT5682_HP_DRIVER_MASK			(0x3 << 2)
#define RT5682_HP_DRIVER_1X			(0x0 << 2)
#define RT5682_HP_DRIVER_3X			(0x1 << 2)
#define RT5682_HP_DRIVER_5X			(0x3 << 2)
#define RT5682_PWR_HA_L				(0x1 << 1)
#define RT5682_PWR_HA_L_BIT			1
#define RT5682_PWR_HA_R				(0x1 << 0)
#define RT5682_PWR_HA_R_BIT			0

/* Power Management for Analog 2 (0x0064) */
#define RT5682_PWR_MB1				(0x1 << 11)
#define RT5682_PWR_MB1_PWR_DOWN			(0x0 << 11)
#define RT5682_PWR_MB1_BIT			11
#define RT5682_PWR_MB2				(0x1 << 10)
#define RT5682_PWR_MB2_PWR_DOWN			(0x0 << 10)
#define RT5682_PWR_MB2_BIT			10
#define RT5682_PWR_JDH				(0x1 << 3)
#define RT5682_PWR_JDH_BIT			3
#define RT5682_PWR_JDL				(0x1 << 2)
#define RT5682_PWR_JDL_BIT			2
#define RT5682_PWR_RM1_L			(0x1 << 1)
#define RT5682_PWR_RM1_L_BIT			1

/* Power Management for Analog 3 (0x0065) */
#define RT5682_PWR_CBJ				(0x1 << 9)
#define RT5682_PWR_CBJ_BIT			9
#define RT5682_PWR_PLL				(0x1 << 6)
#define RT5682_PWR_PLL_BIT			6
#define RT5682_PWR_PLL2B			(0x1 << 5)
#define RT5682_PWR_PLL2B_BIT			5
#define RT5682_PWR_PLL2F			(0x1 << 4)
#define RT5682_PWR_PLL2F_BIT			4
#define RT5682_PWR_LDO2				(0x1 << 2)
#define RT5682_PWR_LDO2_BIT			2
#define RT5682_PWR_DET_SPKVDD			(0x1 << 1)
#define RT5682_PWR_DET_SPKVDD_BIT		1

/* Power Management for Mixer (0x0066) */
#define RT5682_PWR_STO1_DAC_L			(0x1 << 5)
#define RT5682_PWR_STO1_DAC_L_BIT		5
#define RT5682_PWR_STO1_DAC_R			(0x1 << 4)
#define RT5682_PWR_STO1_DAC_R_BIT		4

/* MCLK and System Clock Detection Control (0x006b) */
#define RT5682_SYS_CLK_DET			(0x1 << 15)
#define RT5682_SYS_CLK_DET_SFT			15
#define RT5682_PLL1_CLK_DET			(0x1 << 14)
#define RT5682_PLL1_CLK_DET_SFT			14
#define RT5682_PLL2_CLK_DET			(0x1 << 13)
#define RT5682_PLL2_CLK_DET_SFT			13
#define RT5682_POW_CLK_DET2_SFT			8
#define RT5682_POW_CLK_DET_SFT			0

/* Digital Microphone Control 1 (0x006e) */
#define RT5682_DMIC_1_EN_MASK			(0x1 << 15)
#define RT5682_DMIC_1_EN_SFT			15
#define RT5682_DMIC_1_DIS			(0x0 << 15)
#define RT5682_DMIC_1_EN			(0x1 << 15)
#define RT5682_FIFO_CLK_DIV_MASK		(0x7 << 12)
#define RT5682_FIFO_CLK_DIV_2			(0x1 << 12)
#define RT5682_DMIC_1_DP_MASK			(0x3 << 4)
#define RT5682_DMIC_1_DP_SFT			4
#define RT5682_DMIC_1_DP_GPIO2			(0x0 << 4)
#define RT5682_DMIC_1_DP_GPIO5			(0x1 << 4)
#define RT5682_DMIC_CLK_MASK			(0xf << 0)
#define RT5682_DMIC_CLK_SFT			0

/* I2S1 Audio Serial Data Port Control (0x0070) */
#define RT5682_SEL_ADCDAT_MASK			(0x1 << 15)
#define RT5682_SEL_ADCDAT_OUT			(0x0 << 15)
#define RT5682_SEL_ADCDAT_IN			(0x1 << 15)
#define RT5682_SEL_ADCDAT_SFT			15
#define RT5682_I2S1_TX_CHL_MASK			(0x7 << 12)
#define RT5682_I2S1_TX_CHL_SFT			12
#define RT5682_I2S1_TX_CHL_16			(0x0 << 12)
#define RT5682_I2S1_TX_CHL_20			(0x1 << 12)
#define RT5682_I2S1_TX_CHL_24			(0x2 << 12)
#define RT5682_I2S1_TX_CHL_32			(0x3 << 12)
#define RT5682_I2S1_TX_CHL_8			(0x4 << 12)
#define RT5682_I2S1_RX_CHL_MASK			(0x7 << 8)
#define RT5682_I2S1_RX_CHL_SFT			8
#define RT5682_I2S1_RX_CHL_16			(0x0 << 8)
#define RT5682_I2S1_RX_CHL_20			(0x1 << 8)
#define RT5682_I2S1_RX_CHL_24			(0x2 << 8)
#define RT5682_I2S1_RX_CHL_32			(0x3 << 8)
#define RT5682_I2S1_RX_CHL_8			(0x4 << 8)
#define RT5682_I2S1_MONO_MASK			(0x1 << 7)
#define RT5682_I2S1_MONO_EN			(0x1 << 7)
#define RT5682_I2S1_MONO_DIS			(0x0 << 7)
#define RT5682_I2S2_MONO_MASK			(0x1 << 6)
#define RT5682_I2S2_MONO_EN			(0x1 << 6)
#define RT5682_I2S2_MONO_DIS			(0x0 << 6)
#define RT5682_I2S1_DL_MASK			(0x7 << 4)
#define RT5682_I2S1_DL_SFT			4
#define RT5682_I2S1_DL_16			(0x0 << 4)
#define RT5682_I2S1_DL_20			(0x1 << 4)
#define RT5682_I2S1_DL_24			(0x2 << 4)
#define RT5682_I2S1_DL_32			(0x3 << 4)
#define RT5682_I2S1_DL_8			(0x4 << 4)

/* I2S1/2 Audio Serial Data Port Control (0x0070)(0x0071) */
#define RT5682_I2S2_MS_MASK			(0x1 << 15)
#define RT5682_I2S2_MS_SFT			15
#define RT5682_I2S2_MS_M			(0x0 << 15)
#define RT5682_I2S2_MS_S			(0x1 << 15)
#define RT5682_I2S2_PIN_CFG_MASK		(0x1 << 14)
#define RT5682_I2S2_PIN_CFG_SFT			14
#define RT5682_I2S2_CLK_SEL_MASK		(0x1 << 11)
#define RT5682_I2S2_CLK_SEL_SFT			11
#define RT5682_I2S2_OUT_MASK			(0x1 << 9)
#define RT5682_I2S2_OUT_SFT			9
#define RT5682_I2S2_OUT_UM			(0x0 << 9)
#define RT5682_I2S2_OUT_M			(0x1 << 9)
#define RT5682_I2S_BP_MASK			(0x1 << 8)
#define RT5682_I2S_BP_SFT			8
#define RT5682_I2S_BP_NOR			(0x0 << 8)
#define RT5682_I2S_BP_INV			(0x1 << 8)
#define RT5682_I2S2_MONO_EN			(0x1 << 6)
#define RT5682_I2S2_MONO_DIS			(0x0 << 6)
#define RT5682_I2S2_DL_MASK			(0x3 << 4)
#define RT5682_I2S2_DL_SFT			4
#define RT5682_I2S2_DL_16			(0x0 << 4)
#define RT5682_I2S2_DL_20			(0x1 << 4)
#define RT5682_I2S2_DL_24			(0x2 << 4)
#define RT5682_I2S2_DL_8			(0x3 << 4)
#define RT5682_I2S_DF_MASK			(0x7)
#define RT5682_I2S_DF_SFT			0
#define RT5682_I2S_DF_I2S			(0x0)
#define RT5682_I2S_DF_LEFT			(0x1)
#define RT5682_I2S_DF_PCM_A			(0x2)
#define RT5682_I2S_DF_PCM_B			(0x3)
#define RT5682_I2S_DF_PCM_A_N			(0x6)
#define RT5682_I2S_DF_PCM_B_N			(0x7)

/* ADC/DAC Clock Control 1 (0x0073) */
#define RT5682_ADC_OSR_MASK			(0xf << 12)
#define RT5682_ADC_OSR_SFT			12
#define RT5682_ADC_OSR_D_1			(0x0 << 12)
#define RT5682_ADC_OSR_D_2			(0x1 << 12)
#define RT5682_ADC_OSR_D_4			(0x2 << 12)
#define RT5682_ADC_OSR_D_6			(0x3 << 12)
#define RT5682_ADC_OSR_D_8			(0x4 << 12)
#define RT5682_ADC_OSR_D_12			(0x5 << 12)
#define RT5682_ADC_OSR_D_16			(0x6 << 12)
#define RT5682_ADC_OSR_D_24			(0x7 << 12)
#define RT5682_ADC_OSR_D_32			(0x8 << 12)
#define RT5682_ADC_OSR_D_48			(0x9 << 12)
#define RT5682_I2S_M_DIV_MASK			(0xf << 8)
#define RT5682_I2S_M_DIV_SFT			8
#define RT5682_I2S_M_D_1			(0x0 << 8)
#define RT5682_I2S_M_D_2			(0x1 << 8)
#define RT5682_I2S_M_D_3			(0x2 << 8)
#define RT5682_I2S_M_D_4			(0x3 << 8)
#define RT5682_I2S_M_D_6			(0x4 << 8)
#define RT5682_I2S_M_D_8			(0x5 << 8)
#define RT5682_I2S_M_D_12			(0x6 << 8)
#define RT5682_I2S_M_D_16			(0x7 << 8)
#define RT5682_I2S_M_D_24			(0x8 << 8)
#define RT5682_I2S_M_D_32			(0x9 << 8)
#define RT5682_I2S_M_D_48			(0x10 << 8)
#define RT5682_I2S_CLK_SRC_MASK			(0x7 << 4)
#define RT5682_I2S_CLK_SRC_SFT			4
#define RT5682_I2S_CLK_SRC_MCLK			(0x0 << 4)
#define RT5682_I2S_CLK_SRC_PLL1			(0x1 << 4)
#define RT5682_I2S_CLK_SRC_PLL2			(0x2 << 4)
#define RT5682_I2S_CLK_SRC_SDW			(0x3 << 4)
#define RT5682_I2S_CLK_SRC_RCCLK		(0x4 << 4) /* 25M */
#define RT5682_DAC_OSR_MASK			(0xf << 0)
#define RT5682_DAC_OSR_SFT			0
#define RT5682_DAC_OSR_D_1			(0x0 << 0)
#define RT5682_DAC_OSR_D_2			(0x1 << 0)
#define RT5682_DAC_OSR_D_4			(0x2 << 0)
#define RT5682_DAC_OSR_D_6			(0x3 << 0)
#define RT5682_DAC_OSR_D_8			(0x4 << 0)
#define RT5682_DAC_OSR_D_12			(0x5 << 0)
#define RT5682_DAC_OSR_D_16			(0x6 << 0)
#define RT5682_DAC_OSR_D_24			(0x7 << 0)
#define RT5682_DAC_OSR_D_32			(0x8 << 0)
#define RT5682_DAC_OSR_D_48			(0x9 << 0)

/* ADC/DAC Clock Control 2 (0x0074) */
#define RT5682_I2S2_BCLK_MS2_MASK		(0x1 << 11)
#define RT5682_I2S2_BCLK_MS2_SFT		11
#define RT5682_I2S2_BCLK_MS2_32			(0x0 << 11)
#define RT5682_I2S2_BCLK_MS2_64			(0x1 << 11)


/* TDM control 1 (0x0079) */
#define RT5682_TDM_TX_CH_MASK			(0x3 << 12)
#define RT5682_TDM_TX_CH_2			(0x0 << 12)
#define RT5682_TDM_TX_CH_4			(0x1 << 12)
#define RT5682_TDM_TX_CH_6			(0x2 << 12)
#define RT5682_TDM_TX_CH_8			(0x3 << 12)
#define RT5682_TDM_RX_CH_MASK			(0x3 << 8)
#define RT5682_TDM_RX_CH_2			(0x0 << 8)
#define RT5682_TDM_RX_CH_4			(0x1 << 8)
#define RT5682_TDM_RX_CH_6			(0x2 << 8)
#define RT5682_TDM_RX_CH_8			(0x3 << 8)
#define RT5682_TDM_ADC_LCA_MASK			(0xf << 4)
#define RT5682_TDM_ADC_LCA_SFT			4
#define RT5682_TDM_ADC_DL_SFT			0

/* TDM control 2 (0x007a) */
#define RT5682_IF1_ADC1_SEL_SFT			14
#define RT5682_IF1_ADC2_SEL_SFT			12
#define RT5682_IF1_ADC3_SEL_SFT			10
#define RT5682_IF1_ADC4_SEL_SFT			8
#define RT5682_TDM_ADC_SEL_SFT			4

/* TDM control 3 (0x007b) */
#define RT5682_TDM_EN				(0x1 << 7)

/* TDM/I2S control (0x007e) */
#define RT5682_TDM_S_BP_MASK			(0x1 << 15)
#define RT5682_TDM_S_BP_SFT			15
#define RT5682_TDM_S_BP_NOR			(0x0 << 15)
#define RT5682_TDM_S_BP_INV			(0x1 << 15)
#define RT5682_TDM_S_LP_MASK			(0x1 << 14)
#define RT5682_TDM_S_LP_SFT			14
#define RT5682_TDM_S_LP_NOR			(0x0 << 14)
#define RT5682_TDM_S_LP_INV			(0x1 << 14)
#define RT5682_TDM_DF_MASK			(0x7 << 11)
#define RT5682_TDM_DF_SFT			11
#define RT5682_TDM_DF_I2S			(0x0 << 11)
#define RT5682_TDM_DF_LEFT			(0x1 << 11)
#define RT5682_TDM_DF_PCM_A			(0x2 << 11)
#define RT5682_TDM_DF_PCM_B			(0x3 << 11)
#define RT5682_TDM_DF_PCM_A_N			(0x6 << 11)
#define RT5682_TDM_DF_PCM_B_N			(0x7 << 11)
#define RT5682_TDM_BCLK_MS1_MASK		(0x3 << 9)
#define RT5682_TDM_BCLK_MS1_SFT			9
#define RT5682_TDM_BCLK_MS1_32			(0x0 << 9)
#define RT5682_TDM_BCLK_MS1_64			(0x1 << 9)
#define RT5682_TDM_BCLK_MS1_128			(0x2 << 9)
#define RT5682_TDM_BCLK_MS1_256			(0x3 << 9)
#define RT5682_TDM_CL_MASK			(0x3 << 4)
#define RT5682_TDM_CL_16			(0x0 << 4)
#define RT5682_TDM_CL_20			(0x1 << 4)
#define RT5682_TDM_CL_24			(0x2 << 4)
#define RT5682_TDM_CL_32			(0x3 << 4)
#define RT5682_TDM_M_BP_MASK			(0x1 << 2)
#define RT5682_TDM_M_BP_SFT			2
#define RT5682_TDM_M_BP_NOR			(0x0 << 2)
#define RT5682_TDM_M_BP_INV			(0x1 << 2)
#define RT5682_TDM_M_LP_MASK			(0x1 << 1)
#define RT5682_TDM_M_LP_SFT			1
#define RT5682_TDM_M_LP_NOR			(0x0 << 1)
#define RT5682_TDM_M_LP_INV			(0x1 << 1)
#define RT5682_TDM_MS_MASK			(0x1 << 0)
#define RT5682_TDM_MS_SFT			0
#define RT5682_TDM_MS_S				(0x0 << 0)
#define RT5682_TDM_MS_M				(0x1 << 0)

/* Global Clock Control (0x0080) */
#define RT5682_SCLK_SRC_MASK			(0x7 << 13)
#define RT5682_SCLK_SRC_SFT			13
#define RT5682_SCLK_SRC_MCLK			(0x0 << 13)
#define RT5682_SCLK_SRC_PLL1			(0x1 << 13)
#define RT5682_SCLK_SRC_PLL2			(0x2 << 13)
#define RT5682_SCLK_SRC_SDW			(0x3 << 13)
#define RT5682_SCLK_SRC_RCCLK			(0x4 << 13)
#define RT5682_PLL2_SRC_MASK			(0x3 << 10)
#define RT5682_PLL2_SRC_SFT			10
#define RT5682_PLL2_SRC_MCLK			(0x0 << 10)
#define RT5682_PLL2_SRC_BCLK1			(0x1 << 10)
#define RT5682_PLL2_SRC_SDW			(0x2 << 10)
#define RT5682_PLL2_SRC_RC			(0x3 << 10)
#define RT5682_PLL1_SRC_MASK			(0x3 << 8)
#define RT5682_PLL1_SRC_SFT			8
#define RT5682_PLL1_SRC_MCLK			(0x0 << 8)
#define RT5682_PLL1_SRC_BCLK1			(0x1 << 8)
#define RT5682_PLL1_SRC_SDW			(0x2 << 8)
#define RT5682_PLL1_SRC_RC			(0x3 << 8)



#define RT5682_PLL_INP_MAX			40000000
#define RT5682_PLL_INP_MIN			256000
/* PLL M/N/K Code Control 1 (0x0081) */
#define RT5682_PLL_N_MAX			0x001ff
#define RT5682_PLL_N_MASK			(RT5682_PLL_N_MAX << 7)
#define RT5682_PLL_N_SFT			7
#define RT5682_PLL_K_MAX			0x001f
#define RT5682_PLL_K_MASK			(RT5682_PLL_K_MAX)
#define RT5682_PLL_K_SFT			0

/* PLL M/N/K Code Control 2 (0x0082) */
#define RT5682_PLL_M_MAX			0x00f
#define RT5682_PLL_M_MASK			(RT5682_PLL_M_MAX << 12)
#define RT5682_PLL_M_SFT			12
#define RT5682_PLL_M_BP				(0x1 << 11)
#define RT5682_PLL_M_BP_SFT			11
#define RT5682_PLL_K_BP				(0x1 << 10)
#define RT5682_PLL_K_BP_SFT			10
#define RT5682_PLL_RST				(0x1 << 1)

/* PLL tracking mode 1 (0x0083) */
#define RT5682_DA_ASRC_MASK			(0x1 << 13)
#define RT5682_DA_ASRC_SFT			13
#define RT5682_DAC_STO1_ASRC_MASK		(0x1 << 12)
#define RT5682_DAC_STO1_ASRC_SFT		12
#define RT5682_AD_ASRC_MASK			(0x1 << 8)
#define RT5682_AD_ASRC_SFT			8
#define RT5682_AD_ASRC_SEL_MASK			(0x1 << 4)
#define RT5682_AD_ASRC_SEL_SFT			4
#define RT5682_DMIC_ASRC_MASK			(0x1 << 3)
#define RT5682_DMIC_ASRC_SFT			3
#define RT5682_ADC_STO1_ASRC_MASK		(0x1 << 2)
#define RT5682_ADC_STO1_ASRC_SFT		2
#define RT5682_DA_ASRC_SEL_MASK			(0x1 << 0)
#define RT5682_DA_ASRC_SEL_SFT			0

/* PLL tracking mode 2 3 (0x0084)(0x0085)*/
#define RT5682_FILTER_CLK_SEL_MASK		(0x7 << 12)
#define RT5682_FILTER_CLK_SEL_SFT		12
#define RT5682_FILTER_CLK_DIV_MASK		(0xf << 8)
#define RT5682_FILTER_CLK_DIV_SFT		8

/* ASRC Control 4 (0x0086) */
#define RT5682_ASRCIN_FTK_N1_MASK		(0x3 << 14)
#define RT5682_ASRCIN_FTK_N1_SFT		14
#define RT5682_ASRCIN_FTK_N2_MASK		(0x3 << 12)
#define RT5682_ASRCIN_FTK_N2_SFT		12
#define RT5682_ASRCIN_FTK_M1_MASK		(0x7 << 8)
#define RT5682_ASRCIN_FTK_M1_SFT		8
#define RT5682_ASRCIN_FTK_M2_MASK		(0x7 << 4)
#define RT5682_ASRCIN_FTK_M2_SFT		4

/* SoundWire reference clk (0x008d) */
#define RT5682_PLL2_OUT_MASK			(0x1 << 8)
#define RT5682_PLL2_OUT_98M			(0x0 << 8)
#define RT5682_PLL2_OUT_49M			(0x1 << 8)
#define RT5682_SDW_REF_2_MASK			(0xf << 4)
#define RT5682_SDW_REF_2_SFT			4
#define RT5682_SDW_REF_2_48K			(0x0 << 4)
#define RT5682_SDW_REF_2_96K			(0x1 << 4)
#define RT5682_SDW_REF_2_192K			(0x2 << 4)
#define RT5682_SDW_REF_2_32K			(0x3 << 4)
#define RT5682_SDW_REF_2_24K			(0x4 << 4)
#define RT5682_SDW_REF_2_16K			(0x5 << 4)
#define RT5682_SDW_REF_2_12K			(0x6 << 4)
#define RT5682_SDW_REF_2_8K			(0x7 << 4)
#define RT5682_SDW_REF_2_44K			(0x8 << 4)
#define RT5682_SDW_REF_2_88K			(0x9 << 4)
#define RT5682_SDW_REF_2_176K			(0xa << 4)
#define RT5682_SDW_REF_2_353K			(0xb << 4)
#define RT5682_SDW_REF_2_22K			(0xc << 4)
#define RT5682_SDW_REF_2_384K			(0xd << 4)
#define RT5682_SDW_REF_2_11K			(0xe << 4)
#define RT5682_SDW_REF_1_MASK			(0xf << 0)
#define RT5682_SDW_REF_1_SFT			0
#define RT5682_SDW_REF_1_48K			(0x0 << 0)
#define RT5682_SDW_REF_1_96K			(0x1 << 0)
#define RT5682_SDW_REF_1_192K			(0x2 << 0)
#define RT5682_SDW_REF_1_32K			(0x3 << 0)
#define RT5682_SDW_REF_1_24K			(0x4 << 0)
#define RT5682_SDW_REF_1_16K			(0x5 << 0)
#define RT5682_SDW_REF_1_12K			(0x6 << 0)
#define RT5682_SDW_REF_1_8K			(0x7 << 0)
#define RT5682_SDW_REF_1_44K			(0x8 << 0)
#define RT5682_SDW_REF_1_88K			(0x9 << 0)
#define RT5682_SDW_REF_1_176K			(0xa << 0)
#define RT5682_SDW_REF_1_353K			(0xb << 0)
#define RT5682_SDW_REF_1_22K			(0xc << 0)
#define RT5682_SDW_REF_1_384K			(0xd << 0)
#define RT5682_SDW_REF_1_11K			(0xe << 0)

/* Depop Mode Control 1 (0x008e) */
#define RT5682_PUMP_EN				(0x1 << 3)
#define RT5682_PUMP_EN_SFT				3
#define RT5682_CAPLESS_EN			(0x1 << 0)
#define RT5682_CAPLESS_EN_SFT			0

/* Depop Mode Control 2 (0x8f) */
#define RT5682_RAMP_MASK			(0x1 << 12)
#define RT5682_RAMP_SFT				12
#define RT5682_RAMP_DIS				(0x0 << 12)
#define RT5682_RAMP_EN				(0x1 << 12)
#define RT5682_BPS_MASK				(0x1 << 11)
#define RT5682_BPS_SFT				11
#define RT5682_BPS_DIS				(0x0 << 11)
#define RT5682_BPS_EN				(0x1 << 11)
#define RT5682_FAST_UPDN_MASK			(0x1 << 10)
#define RT5682_FAST_UPDN_SFT			10
#define RT5682_FAST_UPDN_DIS			(0x0 << 10)
#define RT5682_FAST_UPDN_EN			(0x1 << 10)
#define RT5682_VLO_MASK				(0x1 << 7)
#define RT5682_VLO_SFT				7
#define RT5682_VLO_3V				(0x0 << 7)
#define RT5682_VLO_33V				(0x1 << 7)

/* HPOUT charge pump 1 (0x0091) */
#define RT5682_OSW_L_MASK			(0x1 << 11)
#define RT5682_OSW_L_SFT			11
#define RT5682_OSW_L_DIS			(0x0 << 11)
#define RT5682_OSW_L_EN				(0x1 << 11)
#define RT5682_OSW_R_MASK			(0x1 << 10)
#define RT5682_OSW_R_SFT			10
#define RT5682_OSW_R_DIS			(0x0 << 10)
#define RT5682_OSW_R_EN				(0x1 << 10)
#define RT5682_PM_HP_MASK			(0x3 << 8)
#define RT5682_PM_HP_SFT			8
#define RT5682_PM_HP_LV				(0x0 << 8)
#define RT5682_PM_HP_MV				(0x1 << 8)
#define RT5682_PM_HP_HV				(0x2 << 8)
#define RT5682_IB_HP_MASK			(0x3 << 6)
#define RT5682_IB_HP_SFT			6
#define RT5682_IB_HP_125IL			(0x0 << 6)
#define RT5682_IB_HP_25IL			(0x1 << 6)
#define RT5682_IB_HP_5IL			(0x2 << 6)
#define RT5682_IB_HP_1IL			(0x3 << 6)

/* Micbias Control1 (0x93) */
#define RT5682_MIC1_OV_MASK			(0x3 << 14)
#define RT5682_MIC1_OV_SFT			14
#define RT5682_MIC1_OV_2V7			(0x0 << 14)
#define RT5682_MIC1_OV_2V4			(0x1 << 14)
#define RT5682_MIC1_OV_2V25			(0x3 << 14)
#define RT5682_MIC1_OV_1V8			(0x4 << 14)
#define RT5682_MIC1_CLK_MASK			(0x1 << 13)
#define RT5682_MIC1_CLK_SFT			13
#define RT5682_MIC1_CLK_DIS			(0x0 << 13)
#define RT5682_MIC1_CLK_EN			(0x1 << 13)
#define RT5682_MIC1_OVCD_MASK			(0x1 << 12)
#define RT5682_MIC1_OVCD_SFT			12
#define RT5682_MIC1_OVCD_DIS			(0x0 << 12)
#define RT5682_MIC1_OVCD_EN			(0x1 << 12)
#define RT5682_MIC1_OVTH_MASK			(0x3 << 10)
#define RT5682_MIC1_OVTH_SFT			10
#define RT5682_MIC1_OVTH_768UA			(0x0 << 10)
#define RT5682_MIC1_OVTH_960UA			(0x1 << 10)
#define RT5682_MIC1_OVTH_1152UA			(0x2 << 10)
#define RT5682_MIC1_OVTH_1960UA			(0x3 << 10)
#define RT5682_MIC2_OV_MASK			(0x3 << 8)
#define RT5682_MIC2_OV_SFT			8
#define RT5682_MIC2_OV_2V7			(0x0 << 8)
#define RT5682_MIC2_OV_2V4			(0x1 << 8)
#define RT5682_MIC2_OV_2V25			(0x3 << 8)
#define RT5682_MIC2_OV_1V8			(0x4 << 8)
#define RT5682_MIC2_CLK_MASK			(0x1 << 7)
#define RT5682_MIC2_CLK_SFT			7
#define RT5682_MIC2_CLK_DIS			(0x0 << 7)
#define RT5682_MIC2_CLK_EN			(0x1 << 7)
#define RT5682_MIC2_OVTH_MASK			(0x3 << 4)
#define RT5682_MIC2_OVTH_SFT			4
#define RT5682_MIC2_OVTH_768UA			(0x0 << 4)
#define RT5682_MIC2_OVTH_960UA			(0x1 << 4)
#define RT5682_MIC2_OVTH_1152UA			(0x2 << 4)
#define RT5682_MIC2_OVTH_1960UA			(0x3 << 4)
#define RT5682_PWR_MB_MASK			(0x1 << 3)
#define RT5682_PWR_MB_SFT			3
#define RT5682_PWR_MB_PD			(0x0 << 3)
#define RT5682_PWR_MB_PU			(0x1 << 3)

/* Micbias Control2 (0x0094) */
#define RT5682_PWR_CLK25M_MASK			(0x1 << 9)
#define RT5682_PWR_CLK25M_SFT			9
#define RT5682_PWR_CLK25M_PD			(0x0 << 9)
#define RT5682_PWR_CLK25M_PU			(0x1 << 9)
#define RT5682_PWR_CLK1M_MASK			(0x1 << 8)
#define RT5682_PWR_CLK1M_SFT			8
#define RT5682_PWR_CLK1M_PD			(0x0 << 8)
#define RT5682_PWR_CLK1M_PU			(0x1 << 8)

/* PLL2 M/N/K Code Control 1 (0x009b) */
#define RT5682_PLL2F_K_MASK			(0x1f << 8)
#define RT5682_PLL2F_K_SFT			8
#define RT5682_PLL2B_K_MASK			(0xf << 4)
#define RT5682_PLL2B_K_SFT			4
#define RT5682_PLL2B_M_MASK			(0xf << 0)

/* PLL2 M/N/K Code Control 2 (0x009c) */
#define RT5682_PLL2F_M_MASK			(0x3f << 8)
#define RT5682_PLL2F_M_SFT			8
#define RT5682_PLL2B_N_MASK			(0x3f << 0)

/* PLL2 M/N/K Code Control 2 (0x009d) */
#define RT5682_PLL2F_N_MASK			(0x7f << 8)
#define RT5682_PLL2F_N_SFT			8

/* PLL2 M/N/K Code Control 2 (0x009e) */
#define RT5682_PLL2B_SEL_PS_MASK		(0x1 << 13)
#define RT5682_PLL2B_SEL_PS_SFT			13
#define RT5682_PLL2B_PS_BYP_MASK		(0x1 << 12)
#define RT5682_PLL2B_PS_BYP_SFT			12
#define RT5682_PLL2B_M_BP_MASK			(0x1 << 11)
#define RT5682_PLL2B_M_BP_SFT			11
#define RT5682_PLL2F_M_BP_MASK			(0x1 << 7)
#define RT5682_PLL2F_M_BP_SFT			7

/* RC Clock Control (0x009f) */
#define RT5682_POW_IRQ				(0x1 << 15)
#define RT5682_POW_JDH				(0x1 << 14)
#define RT5682_POW_JDL				(0x1 << 13)
#define RT5682_POW_ANA				(0x1 << 12)

/* I2S Master Mode Clock Control 1 (0x00a0) */
#define RT5682_CLK_SRC_MCLK			(0x0)
#define RT5682_CLK_SRC_PLL1			(0x1)
#define RT5682_CLK_SRC_PLL2			(0x2)
#define RT5682_CLK_SRC_SDW			(0x3)
#define RT5682_CLK_SRC_RCCLK			(0x4)
#define RT5682_I2S_PD_1				(0x0)
#define RT5682_I2S_PD_2				(0x1)
#define RT5682_I2S_PD_3				(0x2)
#define RT5682_I2S_PD_4				(0x3)
#define RT5682_I2S_PD_6				(0x4)
#define RT5682_I2S_PD_8				(0x5)
#define RT5682_I2S_PD_12			(0x6)
#define RT5682_I2S_PD_16			(0x7)
#define RT5682_I2S_PD_24			(0x8)
#define RT5682_I2S_PD_32			(0x9)
#define RT5682_I2S_PD_48			(0xa)
#define RT5682_I2S2_SRC_MASK			(0x3 << 4)
#define RT5682_I2S2_SRC_SFT			4
#define RT5682_I2S2_M_PD_MASK			(0xf << 0)
#define RT5682_I2S2_M_PD_SFT			0

/* IRQ Control 1 (0x00b6) */
#define RT5682_JD1_PULSE_EN_MASK		(0x1 << 10)
#define RT5682_JD1_PULSE_EN_SFT			10
#define RT5682_JD1_PULSE_DIS			(0x0 << 10)
#define RT5682_JD1_PULSE_EN			(0x1 << 10)

/* IRQ Control 2 (0x00b7) */
#define RT5682_JD1_EN_MASK			(0x1 << 15)
#define RT5682_JD1_EN_SFT			15
#define RT5682_JD1_DIS				(0x0 << 15)
#define RT5682_JD1_EN				(0x1 << 15)
#define RT5682_JD1_POL_MASK			(0x1 << 13)
#define RT5682_JD1_POL_NOR			(0x0 << 13)
#define RT5682_JD1_POL_INV			(0x1 << 13)
#define RT5682_JD1_IRQ_MASK			(0x1 << 10)
#define RT5682_JD1_IRQ_LEV			(0x0 << 10)
#define RT5682_JD1_IRQ_PUL			(0x1 << 10)

/* IRQ Control 3 (0x00b8) */
#define RT5682_IL_IRQ_MASK			(0x1 << 7)
#define RT5682_IL_IRQ_DIS			(0x0 << 7)
#define RT5682_IL_IRQ_EN			(0x1 << 7)
#define RT5682_IL_IRQ_TYPE_MASK			(0x1 << 4)
#define RT5682_IL_IRQ_LEV			(0x0 << 4)
#define RT5682_IL_IRQ_PUL			(0x1 << 4)

/* GPIO Control 1 (0x00c0) */
#define RT5682_GP1_PIN_MASK			(0x3 << 14)
#define RT5682_GP1_PIN_SFT			14
#define RT5682_GP1_PIN_GPIO1			(0x0 << 14)
#define RT5682_GP1_PIN_IRQ			(0x1 << 14)
#define RT5682_GP1_PIN_DMIC_CLK			(0x2 << 14)
#define RT5682_GP2_PIN_MASK			(0x3 << 12)
#define RT5682_GP2_PIN_SFT			12
#define RT5682_GP2_PIN_GPIO2			(0x0 << 12)
#define RT5682_GP2_PIN_LRCK2			(0x1 << 12)
#define RT5682_GP2_PIN_DMIC_SDA			(0x2 << 12)
#define RT5682_GP3_PIN_MASK			(0x3 << 10)
#define RT5682_GP3_PIN_SFT			10
#define RT5682_GP3_PIN_GPIO3			(0x0 << 10)
#define RT5682_GP3_PIN_BCLK2			(0x1 << 10)
#define RT5682_GP3_PIN_DMIC_CLK			(0x2 << 10)
#define RT5682_GP4_PIN_MASK			(0x3 << 8)
#define RT5682_GP4_PIN_SFT			8
#define RT5682_GP4_PIN_GPIO4			(0x0 << 8)
#define RT5682_GP4_PIN_ADCDAT1			(0x1 << 8)
#define RT5682_GP4_PIN_DMIC_CLK			(0x2 << 8)
#define RT5682_GP4_PIN_ADCDAT2			(0x3 << 8)
#define RT5682_GP5_PIN_MASK			(0x3 << 6)
#define RT5682_GP5_PIN_SFT			6
#define RT5682_GP5_PIN_GPIO5			(0x0 << 6)
#define RT5682_GP5_PIN_DACDAT1			(0x1 << 6)
#define RT5682_GP5_PIN_DMIC_SDA			(0x2 << 6)
#define RT5682_GP6_PIN_MASK			(0x1 << 5)
#define RT5682_GP6_PIN_SFT			5
#define RT5682_GP6_PIN_GPIO6			(0x0 << 5)
#define RT5682_GP6_PIN_LRCK1			(0x1 << 5)

/* GPIO Control 2 (0x00c1)*/
#define RT5682_GP1_PF_MASK			(0x1 << 15)
#define RT5682_GP1_PF_IN			(0x0 << 15)
#define RT5682_GP1_PF_OUT			(0x1 << 15)
#define RT5682_GP1_OUT_MASK			(0x1 << 14)
#define RT5682_GP1_OUT_L			(0x0 << 14)
#define RT5682_GP1_OUT_H			(0x1 << 14)
#define RT5682_GP2_PF_MASK			(0x1 << 13)
#define RT5682_GP2_PF_IN			(0x0 << 13)
#define RT5682_GP2_PF_OUT			(0x1 << 13)
#define RT5682_GP2_OUT_MASK			(0x1 << 12)
#define RT5682_GP2_OUT_L			(0x0 << 12)
#define RT5682_GP2_OUT_H			(0x1 << 12)
#define RT5682_GP3_PF_MASK			(0x1 << 11)
#define RT5682_GP3_PF_IN			(0x0 << 11)
#define RT5682_GP3_PF_OUT			(0x1 << 11)
#define RT5682_GP3_OUT_MASK			(0x1 << 10)
#define RT5682_GP3_OUT_L			(0x0 << 10)
#define RT5682_GP3_OUT_H			(0x1 << 10)
#define RT5682_GP4_PF_MASK			(0x1 << 9)
#define RT5682_GP4_PF_IN			(0x0 << 9)
#define RT5682_GP4_PF_OUT			(0x1 << 9)
#define RT5682_GP4_OUT_MASK			(0x1 << 8)
#define RT5682_GP4_OUT_L			(0x0 << 8)
#define RT5682_GP4_OUT_H			(0x1 << 8)
#define RT5682_GP5_PF_MASK			(0x1 << 7)
#define RT5682_GP5_PF_IN			(0x0 << 7)
#define RT5682_GP5_PF_OUT			(0x1 << 7)
#define RT5682_GP5_OUT_MASK			(0x1 << 6)
#define RT5682_GP5_OUT_L			(0x0 << 6)
#define RT5682_GP5_OUT_H			(0x1 << 6)
#define RT5682_GP6_PF_MASK			(0x1 << 5)
#define RT5682_GP6_PF_IN			(0x0 << 5)
#define RT5682_GP6_PF_OUT			(0x1 << 5)
#define RT5682_GP6_OUT_MASK			(0x1 << 4)
#define RT5682_GP6_OUT_L			(0x0 << 4)
#define RT5682_GP6_OUT_H			(0x1 << 4)


/* GPIO Status (0x00c2) */
#define RT5682_GP6_STA				(0x1 << 6)
#define RT5682_GP5_STA				(0x1 << 5)
#define RT5682_GP4_STA				(0x1 << 4)
#define RT5682_GP3_STA				(0x1 << 3)
#define RT5682_GP2_STA				(0x1 << 2)
#define RT5682_GP1_STA				(0x1 << 1)

/* Soft volume and zero cross control 1 (0x00d9) */
#define RT5682_SV_MASK				(0x1 << 15)
#define RT5682_SV_SFT				15
#define RT5682_SV_DIS				(0x0 << 15)
#define RT5682_SV_EN				(0x1 << 15)
#define RT5682_ZCD_MASK				(0x1 << 10)
#define RT5682_ZCD_SFT				10
#define RT5682_ZCD_PD				(0x0 << 10)
#define RT5682_ZCD_PU				(0x1 << 10)
#define RT5682_SV_DLY_MASK			(0xf)
#define RT5682_SV_DLY_SFT			0

/* Soft volume and zero cross control 2 (0x00da) */
#define RT5682_ZCD_BST1_CBJ_MASK		(0x1 << 7)
#define RT5682_ZCD_BST1_CBJ_SFT			7
#define RT5682_ZCD_BST1_CBJ_DIS			(0x0 << 7)
#define RT5682_ZCD_BST1_CBJ_EN			(0x1 << 7)
#define RT5682_ZCD_RECMIX_MASK			(0x1)
#define RT5682_ZCD_RECMIX_SFT			0
#define RT5682_ZCD_RECMIX_DIS			(0x0)
#define RT5682_ZCD_RECMIX_EN			(0x1)

/* 4 Button Inline Command Control 2 (0x00e3) */
#define RT5682_4BTN_IL_MASK			(0x1 << 15)
#define RT5682_4BTN_IL_EN			(0x1 << 15)
#define RT5682_4BTN_IL_DIS			(0x0 << 15)
#define RT5682_4BTN_IL_RST_MASK			(0x1 << 14)
#define RT5682_4BTN_IL_NOR			(0x1 << 14)
#define RT5682_4BTN_IL_RST			(0x0 << 14)

/* Analog JD Control (0x00f0) */
#define RT5682_JDH_RS_MASK			(0x1 << 4)
#define RT5682_JDH_NO_PLUG			(0x1 << 4)
#define RT5682_JDH_PLUG				(0x0 << 4)

/* Bias current control 8 (0x0111) */
#define RT5682_HPA_CP_BIAS_CTRL_MASK			(0x3 << 2)
#define RT5682_HPA_CP_BIAS_2UA			(0x0 << 2)
#define RT5682_HPA_CP_BIAS_3UA			(0x1 << 2)
#define RT5682_HPA_CP_BIAS_4UA			(0x2 << 2)
#define RT5682_HPA_CP_BIAS_6UA			(0x3 << 2)

/* Charge Pump Internal Register1 (0x0125) */
#define RT5682_CP_SW_SIZE_MASK			(0x7 << 8)
#define RT5682_CP_SW_SIZE_L			(0x4 << 8)
#define RT5682_CP_SW_SIZE_M			(0x2 << 8)
#define RT5682_CP_SW_SIZE_S			(0x1 << 8)
#define RT5682_CP_CLK_HP_MASK			(0x3 << 4)
#define RT5682_CP_CLK_HP_100KHZ			(0x0 << 4)
#define RT5682_CP_CLK_HP_200KHZ			(0x1 << 4)
#define RT5682_CP_CLK_HP_300KHZ			(0x2 << 4)
#define RT5682_CP_CLK_HP_600KHZ			(0x3 << 4)

/* Pad Driving Control (0x0136) */
#define RT5682_PAD_DRV_GP1_MASK			(0x3 << 14)
#define RT5682_PAD_DRV_GP1_SFT			14
#define RT5682_PAD_DRV_GP2_MASK			(0x3 << 12)
#define RT5682_PAD_DRV_GP2_SFT			12
#define RT5682_PAD_DRV_GP3_MASK			(0x3 << 10)
#define RT5682_PAD_DRV_GP3_SFT			10
#define RT5682_PAD_DRV_GP4_MASK			(0x3 << 8)
#define RT5682_PAD_DRV_GP4_SFT			8
#define RT5682_PAD_DRV_GP5_MASK			(0x3 << 6)
#define RT5682_PAD_DRV_GP5_SFT			6
#define RT5682_PAD_DRV_GP6_MASK			(0x3 << 4)
#define RT5682_PAD_DRV_GP6_SFT			4

/* Chopper and Clock control for DAC (0x013a)*/
#define RT5682_CKXEN_DAC1_MASK			(0x1 << 13)
#define RT5682_CKXEN_DAC1_SFT			13
#define RT5682_CKGEN_DAC1_MASK			(0x1 << 12)
#define RT5682_CKGEN_DAC1_SFT			12

/* Chopper and Clock control for ADC (0x013b)*/
#define RT5682_CKXEN_ADC1_MASK			(0x1 << 13)
#define RT5682_CKXEN_ADC1_SFT			13
#define RT5682_CKGEN_ADC1_MASK			(0x1 << 12)
#define RT5682_CKGEN_ADC1_SFT			12

/* Volume test (0x013f)*/
#define RT5682_SEL_CLK_VOL_MASK			(0x1 << 15)
#define RT5682_SEL_CLK_VOL_EN			(0x1 << 15)
#define RT5682_SEL_CLK_VOL_DIS			(0x0 << 15)

/* Test Mode Control 1 (0x0145) */
#define RT5682_AD2DA_LB_MASK			(0x1 << 10)
#define RT5682_AD2DA_LB_SFT			10

/* Stereo Noise Gate Control 1 (0x0160) */
#define RT5682_NG2_EN_MASK			(0x1 << 15)
#define RT5682_NG2_EN				(0x1 << 15)
#define RT5682_NG2_DIS				(0x0 << 15)

/* Stereo1 DAC Silence Detection Control (0x0190) */
#define RT5682_DEB_STO_DAC_MASK			(0x7 << 4)
#define RT5682_DEB_80_MS			(0x0 << 4)

/* HP Behavior Logic Control 2 (0x01db) */
#define RT5682_HP_LC2_SIG_SOUR2_MASK		(0x1 << 4)
#define RT5682_HP_LC2_SIG_SOUR2_REG		(0x1 << 4)
#define RT5682_HP_LC2_SIG_SOUR2_DC_CAL		(0x0 << 4)
#define RT5682_HP_LC2_SIG_SOUR1_MASK		(0x7)
#define RT5682_HP_LC2_SIG_SOUR1_1BIT		(0x7)
#define RT5682_HP_LC2_SIG_SOUR1_LEGA		(0x2)

/* SAR ADC Inline Command Control 1 (0x0210) */
#define RT5682_SAR_BUTT_DET_MASK		(0x1 << 15)
#define RT5682_SAR_BUTT_DET_EN			(0x1 << 15)
#define RT5682_SAR_BUTT_DET_DIS			(0x0 << 15)
#define RT5682_SAR_BUTDET_MODE_MASK		(0x1 << 14)
#define RT5682_SAR_BUTDET_POW_SAV		(0x1 << 14)
#define RT5682_SAR_BUTDET_POW_NORM		(0x0 << 14)
#define RT5682_SAR_BUTDET_RST_MASK		(0x1 << 13)
#define RT5682_SAR_BUTDET_RST_NORMAL		(0x1 << 13)
#define RT5682_SAR_BUTDET_RST			(0x0 << 13)
#define RT5682_SAR_POW_MASK			(0x1 << 12)
#define RT5682_SAR_POW_EN			(0x1 << 12)
#define RT5682_SAR_POW_DIS			(0x0 << 12)
#define RT5682_SAR_RST_MASK			(0x1 << 11)
#define RT5682_SAR_RST_NORMAL			(0x1 << 11)
#define RT5682_SAR_RST				(0x0 << 11)
#define RT5682_SAR_BYPASS_MASK			(0x1 << 10)
#define RT5682_SAR_BYPASS_EN			(0x1 << 10)
#define RT5682_SAR_BYPASS_DIS			(0x0 << 10)
#define RT5682_SAR_SEL_MB1_MASK			(0x1 << 9)
#define RT5682_SAR_SEL_MB1_SEL			(0x1 << 9)
#define RT5682_SAR_SEL_MB1_NOSEL		(0x0 << 9)
#define RT5682_SAR_SEL_MB2_MASK			(0x1 << 8)
#define RT5682_SAR_SEL_MB2_SEL			(0x1 << 8)
#define RT5682_SAR_SEL_MB2_NOSEL		(0x0 << 8)
#define RT5682_SAR_SEL_MODE_MASK		(0x1 << 7)
#define RT5682_SAR_SEL_MODE_CMP			(0x1 << 7)
#define RT5682_SAR_SEL_MODE_ADC			(0x0 << 7)
#define RT5682_SAR_SEL_MB1_MB2_MASK		(0x1 << 5)
#define RT5682_SAR_SEL_MB1_MB2_AUTO		(0x1 << 5)
#define RT5682_SAR_SEL_MB1_MB2_MANU		(0x0 << 5)
#define RT5682_SAR_SEL_SIGNAL_MASK		(0x1 << 4)
#define RT5682_SAR_SEL_SIGNAL_AUTO		(0x1 << 4)
#define RT5682_SAR_SEL_SIGNAL_MANU		(0x0 << 4)

/* SAR ADC Inline Command Control 13 (0x021c) */
#define RT5682_SAR_SOUR_MASK			(0x3f)
#define RT5682_SAR_SOUR_BTN			(0x3f)
#define RT5682_SAR_SOUR_TYPE			(0x0)

/* soundwire timeout */
#define RT5682_PROBE_TIMEOUT			5000


#define RT5682_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT5682_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

/* System Clock Source */
enum {
	RT5682_SCLK_S_MCLK,
	RT5682_SCLK_S_PLL1,
	RT5682_SCLK_S_PLL2,
	RT5682_SCLK_S_RCCLK,
};

/* PLL Source */
enum {
	RT5682_PLL1_S_MCLK,
	RT5682_PLL1_S_BCLK1,
	RT5682_PLL1_S_RCCLK,
	RT5682_PLL2_S_MCLK,
};

enum {
	RT5682_PLL1,
	RT5682_PLL2,
	RT5682_PLLS,
};

enum {
	RT5682_AIF1,
	RT5682_AIF2,
	RT5682_SDW,
	RT5682_AIFS
};

/* filter mask */
enum {
	RT5682_DA_STEREO1_FILTER = 0x1,
	RT5682_AD_STEREO1_FILTER = (0x1 << 1),
};

enum {
	RT5682_CLK_SEL_SYS,
	RT5682_CLK_SEL_I2S1_ASRC,
	RT5682_CLK_SEL_I2S2_ASRC,
};

#define RT5682_NUM_SUPPLIES 3

struct rt5682_priv {
	struct snd_soc_component *component;
	struct rt5682_platform_data pdata;
	struct regmap *regmap;
	struct regmap *sdw_regmap;
	struct snd_soc_jack *hs_jack;
	struct regulator_bulk_data supplies[RT5682_NUM_SUPPLIES];
	struct delayed_work jack_detect_work;
	struct delayed_work jd_check_work;
	struct mutex disable_irq_lock; /* imp-def irq lock protection */
	bool disable_irq;
	struct mutex calibrate_mutex;
	struct sdw_slave *slave;
	enum sdw_slave_status status;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_hw_init;
	bool is_sdw;

#ifdef CONFIG_COMMON_CLK
	struct clk_hw dai_clks_hw[RT5682_DAI_NUM_CLKS];
	struct clk *mclk;
#endif

	int sysclk;
	int sysclk_src;
	int lrck[RT5682_AIFS];
	int bclk[RT5682_AIFS];
	int master[RT5682_AIFS];

	int pll_src[RT5682_PLLS];
	int pll_in[RT5682_PLLS];
	int pll_out[RT5682_PLLS];

	int jack_type;
	int irq_work_delay_time;
};

extern const char *rt5682_supply_names[RT5682_NUM_SUPPLIES];

int rt5682_sel_asrc_clk_src(struct snd_soc_component *component,
		unsigned int filter_mask, unsigned int clk_src);

void rt5682_apply_patch_list(struct rt5682_priv *rt5682, struct device *dev);

int rt5682_headset_detect(struct snd_soc_component *component, int jack_insert);
void rt5682_jack_detect_handler(struct work_struct *work);

bool rt5682_volatile_register(struct device *dev, unsigned int reg);
bool rt5682_readable_register(struct device *dev, unsigned int reg);

int rt5682_register_component(struct device *dev);
void rt5682_calibrate(struct rt5682_priv *rt5682);
void rt5682_reset(struct rt5682_priv *rt5682);
int rt5682_parse_dt(struct rt5682_priv *rt5682, struct device *dev);

#define RT5682_REG_NUM 318
extern const struct reg_default rt5682_reg[RT5682_REG_NUM];

extern const struct snd_soc_dai_ops rt5682_aif1_dai_ops;
extern const struct snd_soc_dai_ops rt5682_aif2_dai_ops;
extern const struct snd_soc_component_driver rt5682_soc_component_dev;

#endif /* __RT5682_H__ */
