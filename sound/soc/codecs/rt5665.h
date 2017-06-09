/*
 * rt5665.h  --  RT5665/RT5658 ALSA SoC audio driver
 *
 * Copyright 2016 Realtek Microelectronics
 * Author: Bard Liao <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5665_H__
#define __RT5665_H__

#include <sound/rt5665.h>

#define DEVICE_ID 0x6451

/* Info */
#define RT5665_RESET				0x0000
#define RT5665_VENDOR_ID			0x00fd
#define RT5665_VENDOR_ID_1			0x00fe
#define RT5665_DEVICE_ID			0x00ff
/*  I/O - Output */
#define RT5665_LOUT				0x0001
#define RT5665_HP_CTRL_1			0x0002
#define RT5665_HP_CTRL_2			0x0003
#define RT5665_MONO_OUT				0x0004
#define RT5665_HPL_GAIN				0x0005
#define RT5665_HPR_GAIN				0x0006
#define RT5665_MONO_GAIN			0x0007

/* I/O - Input */
#define RT5665_CAL_BST_CTRL			0x000a
#define RT5665_CBJ_BST_CTRL			0x000b
#define RT5665_IN1_IN2				0x000c
#define RT5665_IN3_IN4				0x000d
#define RT5665_INL1_INR1_VOL			0x000f
/* I/O - Speaker */
#define RT5665_EJD_CTRL_1			0x0010
#define RT5665_EJD_CTRL_2			0x0011
#define RT5665_EJD_CTRL_3			0x0012
#define RT5665_EJD_CTRL_4			0x0013
#define RT5665_EJD_CTRL_5			0x0014
#define RT5665_EJD_CTRL_6			0x0015
#define RT5665_EJD_CTRL_7			0x0016
/* I/O - ADC/DAC/DMIC */
#define RT5665_DAC2_CTRL			0x0017
#define RT5665_DAC2_DIG_VOL			0x0018
#define RT5665_DAC1_DIG_VOL			0x0019
#define RT5665_DAC3_DIG_VOL			0x001a
#define RT5665_DAC3_CTRL			0x001b
#define RT5665_STO1_ADC_DIG_VOL			0x001c
#define RT5665_MONO_ADC_DIG_VOL			0x001d
#define RT5665_STO2_ADC_DIG_VOL			0x001e
#define RT5665_STO1_ADC_BOOST			0x001f
#define RT5665_MONO_ADC_BOOST			0x0020
#define RT5665_STO2_ADC_BOOST			0x0021
#define RT5665_HP_IMP_GAIN_1			0x0022
#define RT5665_HP_IMP_GAIN_2			0x0023
/* Mixer - D-D */
#define RT5665_STO1_ADC_MIXER			0x0026
#define RT5665_MONO_ADC_MIXER			0x0027
#define RT5665_STO2_ADC_MIXER			0x0028
#define RT5665_AD_DA_MIXER			0x0029
#define RT5665_STO1_DAC_MIXER			0x002a
#define RT5665_MONO_DAC_MIXER			0x002b
#define RT5665_STO2_DAC_MIXER			0x002c
#define RT5665_A_DAC1_MUX			0x002d
#define RT5665_A_DAC2_MUX			0x002e
#define RT5665_DIG_INF2_DATA			0x002f
#define RT5665_DIG_INF3_DATA			0x0030
/* Mixer - PDM */
#define RT5665_PDM_OUT_CTRL			0x0031
#define RT5665_PDM_DATA_CTRL_1			0x0032
#define RT5665_PDM_DATA_CTRL_2			0x0033
#define RT5665_PDM_DATA_CTRL_3			0x0034
#define RT5665_PDM_DATA_CTRL_4			0x0035
/* Mixer - ADC */
#define RT5665_REC1_GAIN			0x003a
#define RT5665_REC1_L1_MIXER			0x003b
#define RT5665_REC1_L2_MIXER			0x003c
#define RT5665_REC1_R1_MIXER			0x003d
#define RT5665_REC1_R2_MIXER			0x003e
#define RT5665_REC2_GAIN			0x003f
#define RT5665_REC2_L1_MIXER			0x0040
#define RT5665_REC2_L2_MIXER			0x0041
#define RT5665_REC2_R1_MIXER			0x0042
#define RT5665_REC2_R2_MIXER			0x0043
#define RT5665_CAL_REC				0x0044
/* Mixer - DAC */
#define RT5665_ALC_BACK_GAIN			0x0049
#define RT5665_MONOMIX_GAIN			0x004a
#define RT5665_MONOMIX_IN_GAIN			0x004b
#define RT5665_OUT_L_GAIN			0x004d
#define RT5665_OUT_L_MIXER			0x004e
#define RT5665_OUT_R_GAIN			0x004f
#define RT5665_OUT_R_MIXER			0x0050
#define RT5665_LOUT_MIXER			0x0052
/* Power */
#define RT5665_PWR_DIG_1			0x0061
#define RT5665_PWR_DIG_2			0x0062
#define RT5665_PWR_ANLG_1			0x0063
#define RT5665_PWR_ANLG_2			0x0064
#define RT5665_PWR_ANLG_3			0x0065
#define RT5665_PWR_MIXER			0x0066
#define RT5665_PWR_VOL				0x0067
/* Clock Detect */
#define RT5665_CLK_DET				0x006b
/* Filter */
#define RT5665_HPF_CTRL1			0x006d
/* DMIC */
#define RT5665_DMIC_CTRL_1			0x006e
#define RT5665_DMIC_CTRL_2			0x006f
/* Format - ADC/DAC */
#define RT5665_I2S1_SDP				0x0070
#define RT5665_I2S2_SDP				0x0071
#define RT5665_I2S3_SDP				0x0072
#define RT5665_ADDA_CLK_1			0x0073
#define RT5665_ADDA_CLK_2			0x0074
#define RT5665_I2S1_F_DIV_CTRL_1		0x0075
#define RT5665_I2S1_F_DIV_CTRL_2		0x0076
/* Format - TDM Control */
#define RT5665_TDM_CTRL_1			0x0078
#define RT5665_TDM_CTRL_2			0x0079
#define RT5665_TDM_CTRL_3			0x007a
#define RT5665_TDM_CTRL_4			0x007b
#define RT5665_TDM_CTRL_5			0x007c
#define RT5665_TDM_CTRL_6			0x007d
#define RT5665_TDM_CTRL_7			0x007e
#define RT5665_TDM_CTRL_8			0x007f
/* Function - Analog */
#define RT5665_GLB_CLK				0x0080
#define RT5665_PLL_CTRL_1			0x0081
#define RT5665_PLL_CTRL_2			0x0082
#define RT5665_ASRC_1				0x0083
#define RT5665_ASRC_2				0x0084
#define RT5665_ASRC_3				0x0085
#define RT5665_ASRC_4				0x0086
#define RT5665_ASRC_5				0x0087
#define RT5665_ASRC_6				0x0088
#define RT5665_ASRC_7				0x0089
#define RT5665_ASRC_8				0x008a
#define RT5665_ASRC_9				0x008b
#define RT5665_ASRC_10				0x008c
#define RT5665_DEPOP_1				0x008e
#define RT5665_DEPOP_2				0x008f
#define RT5665_HP_CHARGE_PUMP_1			0x0091
#define RT5665_HP_CHARGE_PUMP_2			0x0092
#define RT5665_MICBIAS_1			0x0093
#define RT5665_MICBIAS_2			0x0094
#define RT5665_ASRC_12				0x0098
#define RT5665_ASRC_13				0x0099
#define RT5665_ASRC_14				0x009a
#define RT5665_RC_CLK_CTRL			0x009f
#define RT5665_I2S_M_CLK_CTRL_1			0x00a0
#define RT5665_I2S2_F_DIV_CTRL_1		0x00a1
#define RT5665_I2S2_F_DIV_CTRL_2		0x00a2
#define RT5665_I2S3_F_DIV_CTRL_1		0x00a3
#define RT5665_I2S3_F_DIV_CTRL_2		0x00a4
/* Function - Digital */
#define RT5665_EQ_CTRL_1			0x00ae
#define RT5665_EQ_CTRL_2			0x00af
#define RT5665_IRQ_CTRL_1			0x00b6
#define RT5665_IRQ_CTRL_2			0x00b7
#define RT5665_IRQ_CTRL_3			0x00b8
#define RT5665_IRQ_CTRL_4			0x00b9
#define RT5665_IRQ_CTRL_5			0x00ba
#define RT5665_IRQ_CTRL_6			0x00bb
#define RT5665_INT_ST_1				0x00be
#define RT5665_GPIO_CTRL_1			0x00c0
#define RT5665_GPIO_CTRL_2			0x00c1
#define RT5665_GPIO_CTRL_3			0x00c2
#define RT5665_GPIO_CTRL_4			0x00c3
#define RT5665_GPIO_STA				0x00c4
#define RT5665_HP_AMP_DET_CTRL_1		0x00d0
#define RT5665_HP_AMP_DET_CTRL_2		0x00d1
#define RT5665_MID_HP_AMP_DET			0x00d3
#define RT5665_LOW_HP_AMP_DET			0x00d4
#define RT5665_SV_ZCD_1				0x00d9
#define RT5665_SV_ZCD_2				0x00da
#define RT5665_IL_CMD_1				0x00db
#define RT5665_IL_CMD_2				0x00dc
#define RT5665_IL_CMD_3				0x00dd
#define RT5665_IL_CMD_4				0x00de
#define RT5665_4BTN_IL_CMD_1			0x00df
#define RT5665_4BTN_IL_CMD_2			0x00e0
#define RT5665_4BTN_IL_CMD_3			0x00e1
#define RT5665_PSV_IL_CMD_1			0x00e2

#define RT5665_ADC_STO1_HP_CTRL_1		0x00ea
#define RT5665_ADC_STO1_HP_CTRL_2		0x00eb
#define RT5665_ADC_MONO_HP_CTRL_1		0x00ec
#define RT5665_ADC_MONO_HP_CTRL_2		0x00ed
#define RT5665_ADC_STO2_HP_CTRL_1		0x00ee
#define RT5665_ADC_STO2_HP_CTRL_2		0x00ef
#define RT5665_AJD1_CTRL			0x00f0
#define RT5665_JD1_THD				0x00f1
#define RT5665_JD2_THD				0x00f2
#define RT5665_JD_CTRL_1			0x00f6
#define RT5665_JD_CTRL_2			0x00f7
#define RT5665_JD_CTRL_3			0x00f8
/* General Control */
#define RT5665_DIG_MISC				0x00fa
#define RT5665_DUMMY_2				0x00fb
#define RT5665_DUMMY_3				0x00fc

#define RT5665_DAC_ADC_DIG_VOL1			0x0100
#define RT5665_DAC_ADC_DIG_VOL2			0x0101
#define RT5665_BIAS_CUR_CTRL_1			0x010a
#define RT5665_BIAS_CUR_CTRL_2			0x010b
#define RT5665_BIAS_CUR_CTRL_3			0x010c
#define RT5665_BIAS_CUR_CTRL_4			0x010d
#define RT5665_BIAS_CUR_CTRL_5			0x010e
#define RT5665_BIAS_CUR_CTRL_6			0x010f
#define RT5665_BIAS_CUR_CTRL_7			0x0110
#define RT5665_BIAS_CUR_CTRL_8			0x0111
#define RT5665_BIAS_CUR_CTRL_9			0x0112
#define RT5665_BIAS_CUR_CTRL_10			0x0113
#define RT5665_VREF_REC_OP_FB_CAP_CTRL		0x0117
#define RT5665_CHARGE_PUMP_1			0x0125
#define RT5665_DIG_IN_CTRL_1			0x0132
#define RT5665_DIG_IN_CTRL_2			0x0133
#define RT5665_PAD_DRIVING_CTRL			0x0137
#define RT5665_SOFT_RAMP_DEPOP			0x0138
#define RT5665_PLL				0x0139
#define RT5665_CHOP_DAC				0x013a
#define RT5665_CHOP_ADC				0x013b
#define RT5665_CALIB_ADC_CTRL			0x013c
#define RT5665_VOL_TEST				0x013f
#define RT5665_TEST_MODE_CTRL_1			0x0145
#define RT5665_TEST_MODE_CTRL_2			0x0146
#define RT5665_TEST_MODE_CTRL_3			0x0147
#define RT5665_TEST_MODE_CTRL_4			0x0148
#define RT5665_BASSBACK_CTRL			0x0150
#define RT5665_STO_NG2_CTRL_1			0x0160
#define RT5665_STO_NG2_CTRL_2			0x0161
#define RT5665_STO_NG2_CTRL_3			0x0162
#define RT5665_STO_NG2_CTRL_4			0x0163
#define RT5665_STO_NG2_CTRL_5			0x0164
#define RT5665_STO_NG2_CTRL_6			0x0165
#define RT5665_STO_NG2_CTRL_7			0x0166
#define RT5665_STO_NG2_CTRL_8			0x0167
#define RT5665_MONO_NG2_CTRL_1			0x0170
#define RT5665_MONO_NG2_CTRL_2			0x0171
#define RT5665_MONO_NG2_CTRL_3			0x0172
#define RT5665_MONO_NG2_CTRL_4			0x0173
#define RT5665_MONO_NG2_CTRL_5			0x0174
#define RT5665_MONO_NG2_CTRL_6			0x0175
#define RT5665_STO1_DAC_SIL_DET			0x0190
#define RT5665_MONOL_DAC_SIL_DET		0x0191
#define RT5665_MONOR_DAC_SIL_DET		0x0192
#define RT5665_STO2_DAC_SIL_DET			0x0193
#define RT5665_SIL_PSV_CTRL1			0x0194
#define RT5665_SIL_PSV_CTRL2			0x0195
#define RT5665_SIL_PSV_CTRL3			0x0196
#define RT5665_SIL_PSV_CTRL4			0x0197
#define RT5665_SIL_PSV_CTRL5			0x0198
#define RT5665_SIL_PSV_CTRL6			0x0199
#define RT5665_MONO_AMP_CALIB_CTRL_1		0x01a0
#define RT5665_MONO_AMP_CALIB_CTRL_2		0x01a1
#define RT5665_MONO_AMP_CALIB_CTRL_3		0x01a2
#define RT5665_MONO_AMP_CALIB_CTRL_4		0x01a3
#define RT5665_MONO_AMP_CALIB_CTRL_5		0x01a4
#define RT5665_MONO_AMP_CALIB_CTRL_6		0x01a5
#define RT5665_MONO_AMP_CALIB_CTRL_7		0x01a6
#define RT5665_MONO_AMP_CALIB_STA1		0x01a7
#define RT5665_MONO_AMP_CALIB_STA2		0x01a8
#define RT5665_MONO_AMP_CALIB_STA3		0x01a9
#define RT5665_MONO_AMP_CALIB_STA4		0x01aa
#define RT5665_MONO_AMP_CALIB_STA6		0x01ab
#define RT5665_HP_IMP_SENS_CTRL_01		0x01b5
#define RT5665_HP_IMP_SENS_CTRL_02		0x01b6
#define RT5665_HP_IMP_SENS_CTRL_03		0x01b7
#define RT5665_HP_IMP_SENS_CTRL_04		0x01b8
#define RT5665_HP_IMP_SENS_CTRL_05		0x01b9
#define RT5665_HP_IMP_SENS_CTRL_06		0x01ba
#define RT5665_HP_IMP_SENS_CTRL_07		0x01bb
#define RT5665_HP_IMP_SENS_CTRL_08		0x01bc
#define RT5665_HP_IMP_SENS_CTRL_09		0x01bd
#define RT5665_HP_IMP_SENS_CTRL_10		0x01be
#define RT5665_HP_IMP_SENS_CTRL_11		0x01bf
#define RT5665_HP_IMP_SENS_CTRL_12		0x01c0
#define RT5665_HP_IMP_SENS_CTRL_13		0x01c1
#define RT5665_HP_IMP_SENS_CTRL_14		0x01c2
#define RT5665_HP_IMP_SENS_CTRL_15		0x01c3
#define RT5665_HP_IMP_SENS_CTRL_16		0x01c4
#define RT5665_HP_IMP_SENS_CTRL_17		0x01c5
#define RT5665_HP_IMP_SENS_CTRL_18		0x01c6
#define RT5665_HP_IMP_SENS_CTRL_19		0x01c7
#define RT5665_HP_IMP_SENS_CTRL_20		0x01c8
#define RT5665_HP_IMP_SENS_CTRL_21		0x01c9
#define RT5665_HP_IMP_SENS_CTRL_22		0x01ca
#define RT5665_HP_IMP_SENS_CTRL_23		0x01cb
#define RT5665_HP_IMP_SENS_CTRL_24		0x01cc
#define RT5665_HP_IMP_SENS_CTRL_25		0x01cd
#define RT5665_HP_IMP_SENS_CTRL_26		0x01ce
#define RT5665_HP_IMP_SENS_CTRL_27		0x01cf
#define RT5665_HP_IMP_SENS_CTRL_28		0x01d0
#define RT5665_HP_IMP_SENS_CTRL_29		0x01d1
#define RT5665_HP_IMP_SENS_CTRL_30		0x01d2
#define RT5665_HP_IMP_SENS_CTRL_31		0x01d3
#define RT5665_HP_IMP_SENS_CTRL_32		0x01d4
#define RT5665_HP_IMP_SENS_CTRL_33		0x01d5
#define RT5665_HP_IMP_SENS_CTRL_34		0x01d6
#define RT5665_HP_LOGIC_CTRL_1			0x01da
#define RT5665_HP_LOGIC_CTRL_2			0x01db
#define RT5665_HP_LOGIC_CTRL_3			0x01dc
#define RT5665_HP_CALIB_CTRL_1			0x01de
#define RT5665_HP_CALIB_CTRL_2			0x01df
#define RT5665_HP_CALIB_CTRL_3			0x01e0
#define RT5665_HP_CALIB_CTRL_4			0x01e1
#define RT5665_HP_CALIB_CTRL_5			0x01e2
#define RT5665_HP_CALIB_CTRL_6			0x01e3
#define RT5665_HP_CALIB_CTRL_7			0x01e4
#define RT5665_HP_CALIB_CTRL_9			0x01e6
#define RT5665_HP_CALIB_CTRL_10			0x01e7
#define RT5665_HP_CALIB_CTRL_11			0x01e8
#define RT5665_HP_CALIB_STA_1			0x01ea
#define RT5665_HP_CALIB_STA_2			0x01eb
#define RT5665_HP_CALIB_STA_3			0x01ec
#define RT5665_HP_CALIB_STA_4			0x01ed
#define RT5665_HP_CALIB_STA_5			0x01ee
#define RT5665_HP_CALIB_STA_6			0x01ef
#define RT5665_HP_CALIB_STA_7			0x01f0
#define RT5665_HP_CALIB_STA_8			0x01f1
#define RT5665_HP_CALIB_STA_9			0x01f2
#define RT5665_HP_CALIB_STA_10			0x01f3
#define RT5665_HP_CALIB_STA_11			0x01f4
#define RT5665_PGM_TAB_CTRL1			0x0200
#define RT5665_PGM_TAB_CTRL2			0x0201
#define RT5665_PGM_TAB_CTRL3			0x0202
#define RT5665_PGM_TAB_CTRL4			0x0203
#define RT5665_PGM_TAB_CTRL5			0x0204
#define RT5665_PGM_TAB_CTRL6			0x0205
#define RT5665_PGM_TAB_CTRL7			0x0206
#define RT5665_PGM_TAB_CTRL8			0x0207
#define RT5665_PGM_TAB_CTRL9			0x0208
#define RT5665_SAR_IL_CMD_1			0x0210
#define RT5665_SAR_IL_CMD_2			0x0211
#define RT5665_SAR_IL_CMD_3			0x0212
#define RT5665_SAR_IL_CMD_4			0x0213
#define RT5665_SAR_IL_CMD_5			0x0214
#define RT5665_SAR_IL_CMD_6			0x0215
#define RT5665_SAR_IL_CMD_7			0x0216
#define RT5665_SAR_IL_CMD_8			0x0217
#define RT5665_SAR_IL_CMD_9			0x0218
#define RT5665_SAR_IL_CMD_10			0x0219
#define RT5665_SAR_IL_CMD_11			0x021a
#define RT5665_SAR_IL_CMD_12			0x021b
#define RT5665_DRC1_CTRL_0			0x02ff
#define RT5665_DRC1_CTRL_1			0x0300
#define RT5665_DRC1_CTRL_2			0x0301
#define RT5665_DRC1_CTRL_3			0x0302
#define RT5665_DRC1_CTRL_4			0x0303
#define RT5665_DRC1_CTRL_5			0x0304
#define RT5665_DRC1_CTRL_6			0x0305
#define RT5665_DRC1_HARD_LMT_CTRL_1		0x0306
#define RT5665_DRC1_HARD_LMT_CTRL_2		0x0307
#define RT5665_DRC1_PRIV_1			0x0310
#define RT5665_DRC1_PRIV_2			0x0311
#define RT5665_DRC1_PRIV_3			0x0312
#define RT5665_DRC1_PRIV_4			0x0313
#define RT5665_DRC1_PRIV_5			0x0314
#define RT5665_DRC1_PRIV_6			0x0315
#define RT5665_DRC1_PRIV_7			0x0316
#define RT5665_DRC1_PRIV_8			0x0317
#define RT5665_ALC_PGA_CTRL_1			0x0330
#define RT5665_ALC_PGA_CTRL_2			0x0331
#define RT5665_ALC_PGA_CTRL_3			0x0332
#define RT5665_ALC_PGA_CTRL_4			0x0333
#define RT5665_ALC_PGA_CTRL_5			0x0334
#define RT5665_ALC_PGA_CTRL_6			0x0335
#define RT5665_ALC_PGA_CTRL_7			0x0336
#define RT5665_ALC_PGA_CTRL_8			0x0337
#define RT5665_ALC_PGA_STA_1			0x0338
#define RT5665_ALC_PGA_STA_2			0x0339
#define RT5665_ALC_PGA_STA_3			0x033a
#define RT5665_EQ_AUTO_RCV_CTRL1		0x03c0
#define RT5665_EQ_AUTO_RCV_CTRL2		0x03c1
#define RT5665_EQ_AUTO_RCV_CTRL3		0x03c2
#define RT5665_EQ_AUTO_RCV_CTRL4		0x03c3
#define RT5665_EQ_AUTO_RCV_CTRL5		0x03c4
#define RT5665_EQ_AUTO_RCV_CTRL6		0x03c5
#define RT5665_EQ_AUTO_RCV_CTRL7		0x03c6
#define RT5665_EQ_AUTO_RCV_CTRL8		0x03c7
#define RT5665_EQ_AUTO_RCV_CTRL9		0x03c8
#define RT5665_EQ_AUTO_RCV_CTRL10		0x03c9
#define RT5665_EQ_AUTO_RCV_CTRL11		0x03ca
#define RT5665_EQ_AUTO_RCV_CTRL12		0x03cb
#define RT5665_EQ_AUTO_RCV_CTRL13		0x03cc
#define RT5665_ADC_L_EQ_LPF1_A1			0x03d0
#define RT5665_R_EQ_LPF1_A1			0x03d1
#define RT5665_L_EQ_LPF1_H0			0x03d2
#define RT5665_R_EQ_LPF1_H0			0x03d3
#define RT5665_L_EQ_BPF1_A1			0x03d4
#define RT5665_R_EQ_BPF1_A1			0x03d5
#define RT5665_L_EQ_BPF1_A2			0x03d6
#define RT5665_R_EQ_BPF1_A2			0x03d7
#define RT5665_L_EQ_BPF1_H0			0x03d8
#define RT5665_R_EQ_BPF1_H0			0x03d9
#define RT5665_L_EQ_BPF2_A1			0x03da
#define RT5665_R_EQ_BPF2_A1			0x03db
#define RT5665_L_EQ_BPF2_A2			0x03dc
#define RT5665_R_EQ_BPF2_A2			0x03dd
#define RT5665_L_EQ_BPF2_H0			0x03de
#define RT5665_R_EQ_BPF2_H0			0x03df
#define RT5665_L_EQ_BPF3_A1			0x03e0
#define RT5665_R_EQ_BPF3_A1			0x03e1
#define RT5665_L_EQ_BPF3_A2			0x03e2
#define RT5665_R_EQ_BPF3_A2			0x03e3
#define RT5665_L_EQ_BPF3_H0			0x03e4
#define RT5665_R_EQ_BPF3_H0			0x03e5
#define RT5665_L_EQ_BPF4_A1			0x03e6
#define RT5665_R_EQ_BPF4_A1			0x03e7
#define RT5665_L_EQ_BPF4_A2			0x03e8
#define RT5665_R_EQ_BPF4_A2			0x03e9
#define RT5665_L_EQ_BPF4_H0			0x03ea
#define RT5665_R_EQ_BPF4_H0			0x03eb
#define RT5665_L_EQ_HPF1_A1			0x03ec
#define RT5665_R_EQ_HPF1_A1			0x03ed
#define RT5665_L_EQ_HPF1_H0			0x03ee
#define RT5665_R_EQ_HPF1_H0			0x03ef
#define RT5665_L_EQ_PRE_VOL			0x03f0
#define RT5665_R_EQ_PRE_VOL			0x03f1
#define RT5665_L_EQ_POST_VOL			0x03f2
#define RT5665_R_EQ_POST_VOL			0x03f3
#define RT5665_SCAN_MODE_CTRL			0x07f0
#define RT5665_I2C_MODE				0x07fa



/* global definition */
#define RT5665_L_MUTE				(0x1 << 15)
#define RT5665_L_MUTE_SFT			15
#define RT5665_VOL_L_MUTE			(0x1 << 14)
#define RT5665_VOL_L_SFT			14
#define RT5665_R_MUTE				(0x1 << 7)
#define RT5665_R_MUTE_SFT			7
#define RT5665_VOL_R_MUTE			(0x1 << 6)
#define RT5665_VOL_R_SFT			6
#define RT5665_L_VOL_MASK			(0x3f << 8)
#define RT5665_L_VOL_SFT			8
#define RT5665_R_VOL_MASK			(0x3f)
#define RT5665_R_VOL_SFT			0

/*Headphone Amp L/R Analog Gain and Digital NG2 Gain Control (0x0005 0x0006)*/
#define RT5665_G_HP				(0xf << 8)
#define RT5665_G_HP_SFT				8
#define RT5665_G_STO_DA_DMIX			(0xf)
#define RT5665_G_STO_DA_SFT			0

/* CBJ Control (0x000b) */
#define RT5665_BST_CBJ_MASK			(0xf << 8)
#define RT5665_BST_CBJ_SFT			8

/* IN1/IN2 Control (0x000c) */
#define RT5665_IN1_DF_MASK			(0x1 << 15)
#define RT5665_IN1_DF				15
#define RT5665_BST1_MASK			(0x7f << 8)
#define RT5665_BST1_SFT				8
#define RT5665_IN2_DF_MASK			(0x1 << 7)
#define RT5665_IN2_DF				7
#define RT5665_BST2_MASK			(0x7f)
#define RT5665_BST2_SFT				0

/* IN3/IN4 Control (0x000d) */
#define RT5665_IN3_DF_MASK			(0x1 << 15)
#define RT5665_IN3_DF				15
#define RT5665_BST3_MASK			(0x7f << 8)
#define RT5665_BST3_SFT				8
#define RT5665_IN4_DF_MASK			(0x1 << 7)
#define RT5665_IN4_DF				7
#define RT5665_BST4_MASK			(0x7f)
#define RT5665_BST4_SFT				0

/* INL and INR Volume Control (0x000f) */
#define RT5665_INL_VOL_MASK			(0x1f << 8)
#define RT5665_INL_VOL_SFT			8
#define RT5665_INR_VOL_MASK			(0x1f)
#define RT5665_INR_VOL_SFT			0

/* Embeeded Jack and Type Detection Control 1 (0x0010) */
#define RT5665_EMB_JD_EN			(0x1 << 15)
#define RT5665_EMB_JD_EN_SFT			15
#define RT5665_JD_MODE				(0x1 << 13)
#define RT5665_JD_MODE_SFT			13
#define RT5665_POLA_EXT_JD_MASK			(0x1 << 11)
#define RT5665_POLA_EXT_JD_LOW			(0x1 << 11)
#define RT5665_POLA_EXT_JD_HIGH			(0x0 << 11)
#define RT5665_EXT_JD_DIG			(0x1 << 9)
#define RT5665_POL_FAST_OFF_MASK		(0x1 << 8)
#define RT5665_POL_FAST_OFF_HIGH		(0x1 << 8)
#define RT5665_POL_FAST_OFF_LOW			(0x0 << 8)
#define RT5665_VREF_POW_MASK			(0x1 << 6)
#define RT5665_VREF_POW_FSM			(0x0 << 6)
#define RT5665_VREF_POW_REG			(0x1 << 6)
#define RT5665_MB1_PATH_MASK			(0x1 << 5)
#define RT5665_CTRL_MB1_REG			(0x1 << 5)
#define RT5665_CTRL_MB1_FSM			(0x0 << 5)
#define RT5665_MB2_PATH_MASK			(0x1 << 4)
#define RT5665_CTRL_MB2_REG			(0x1 << 4)
#define RT5665_CTRL_MB2_FSM			(0x0 << 4)
#define RT5665_TRIG_JD_MASK			(0x1 << 3)
#define RT5665_TRIG_JD_HIGH			(0x1 << 3)
#define RT5665_TRIG_JD_LOW			(0x0 << 3)

/* Embeeded Jack and Type Detection Control 2 (0x0011) */
#define RT5665_EXT_JD_SRC			(0x7 << 4)
#define RT5665_EXT_JD_SRC_SFT			4
#define RT5665_EXT_JD_SRC_GPIO_JD1		(0x0 << 4)
#define RT5665_EXT_JD_SRC_GPIO_JD2		(0x1 << 4)
#define RT5665_EXT_JD_SRC_JD1_1			(0x2 << 4)
#define RT5665_EXT_JD_SRC_JD1_2			(0x3 << 4)
#define RT5665_EXT_JD_SRC_JD2			(0x4 << 4)
#define RT5665_EXT_JD_SRC_JD3			(0x5 << 4)
#define RT5665_EXT_JD_SRC_MANUAL		(0x6 << 4)

/* Combo Jack and Type Detection Control 4 (0x0013) */
#define RT5665_SEL_SHT_MID_TON_MASK		(0x3 << 12)
#define RT5665_SEL_SHT_MID_TON_2		(0x0 << 12)
#define RT5665_SEL_SHT_MID_TON_3		(0x1 << 12)
#define RT5665_CBJ_JD_TEST_MASK			(0x1 << 6)
#define RT5665_CBJ_JD_TEST_NORM			(0x0 << 6)
#define RT5665_CBJ_JD_TEST_MODE			(0x1 << 6)

/* Slience Detection Control (0x0015) */
#define RT5665_SIL_DET_MASK			(0x1 << 15)
#define RT5665_SIL_DET_DIS			(0x0 << 15)
#define RT5665_SIL_DET_EN			(0x1 << 15)

/* DAC2 Control (0x0017) */
#define RT5665_M_DAC2_L_VOL			(0x1 << 13)
#define RT5665_M_DAC2_L_VOL_SFT			13
#define RT5665_M_DAC2_R_VOL			(0x1 << 12)
#define RT5665_M_DAC2_R_VOL_SFT			12
#define RT5665_DAC_L2_SEL_MASK			(0x7 << 4)
#define RT5665_DAC_L2_SEL_SFT			4
#define RT5665_DAC_R2_SEL_MASK			(0x7 << 0)
#define RT5665_DAC_R2_SEL_SFT			0

/* Sidetone Control (0x0018) */
#define RT5665_ST_SEL_MASK			(0x7 << 9)
#define RT5665_ST_SEL_SFT			9
#define RT5665_ST_EN				(0x1 << 6)
#define RT5665_ST_EN_SFT			6

/* DAC1 Digital Volume (0x0019) */
#define RT5665_DAC_L1_VOL_MASK			(0xff << 8)
#define RT5665_DAC_L1_VOL_SFT			8
#define RT5665_DAC_R1_VOL_MASK			(0xff)
#define RT5665_DAC_R1_VOL_SFT			0

/* DAC2 Digital Volume (0x001a) */
#define RT5665_DAC_L2_VOL_MASK			(0xff << 8)
#define RT5665_DAC_L2_VOL_SFT			8
#define RT5665_DAC_R2_VOL_MASK			(0xff)
#define RT5665_DAC_R2_VOL_SFT			0

/* DAC3 Control (0x001b) */
#define RT5665_M_DAC3_L_VOL			(0x1 << 13)
#define RT5665_M_DAC3_L_VOL_SFT			13
#define RT5665_M_DAC3_R_VOL			(0x1 << 12)
#define RT5665_M_DAC3_R_VOL_SFT			12
#define RT5665_DAC_L3_SEL_MASK			(0x7 << 4)
#define RT5665_DAC_L3_SEL_SFT			4
#define RT5665_DAC_R3_SEL_MASK			(0x7 << 0)
#define RT5665_DAC_R3_SEL_SFT			0

/* ADC Digital Volume Control (0x001c) */
#define RT5665_ADC_L_VOL_MASK			(0x7f << 8)
#define RT5665_ADC_L_VOL_SFT			8
#define RT5665_ADC_R_VOL_MASK			(0x7f)
#define RT5665_ADC_R_VOL_SFT			0

/* Mono ADC Digital Volume Control (0x001d) */
#define RT5665_MONO_ADC_L_VOL_MASK		(0x7f << 8)
#define RT5665_MONO_ADC_L_VOL_SFT		8
#define RT5665_MONO_ADC_R_VOL_MASK		(0x7f)
#define RT5665_MONO_ADC_R_VOL_SFT		0

/* Stereo1 ADC Boost Gain Control (0x001f) */
#define RT5665_STO1_ADC_L_BST_MASK		(0x3 << 14)
#define RT5665_STO1_ADC_L_BST_SFT		14
#define RT5665_STO1_ADC_R_BST_MASK		(0x3 << 12)
#define RT5665_STO1_ADC_R_BST_SFT		12

/* Mono ADC Boost Gain Control (0x0020) */
#define RT5665_MONO_ADC_L_BST_MASK		(0x3 << 14)
#define RT5665_MONO_ADC_L_BST_SFT		14
#define RT5665_MONO_ADC_R_BST_MASK		(0x3 << 12)
#define RT5665_MONO_ADC_R_BST_SFT		12

/* Stereo1 ADC Boost Gain Control (0x001f) */
#define RT5665_STO2_ADC_L_BST_MASK		(0x3 << 14)
#define RT5665_STO2_ADC_L_BST_SFT		14
#define RT5665_STO2_ADC_R_BST_MASK		(0x3 << 12)
#define RT5665_STO2_ADC_R_BST_SFT		12

/* Stereo1 ADC Mixer Control (0x0026) */
#define RT5665_M_STO1_ADC_L1			(0x1 << 15)
#define RT5665_M_STO1_ADC_L1_SFT		15
#define RT5665_M_STO1_ADC_L2			(0x1 << 14)
#define RT5665_M_STO1_ADC_L2_SFT		14
#define RT5665_STO1_ADC1L_SRC_MASK		(0x1 << 13)
#define RT5665_STO1_ADC1L_SRC_SFT		13
#define RT5665_STO1_ADC1_SRC_ADC		(0x1 << 13)
#define RT5665_STO1_ADC1_SRC_DACMIX		(0x0 << 13)
#define RT5665_STO1_ADC2L_SRC_MASK		(0x1 << 12)
#define RT5665_STO1_ADC2L_SRC_SFT		12
#define RT5665_STO1_ADCL_SRC_MASK		(0x3 << 10)
#define RT5665_STO1_ADCL_SRC_SFT		10
#define RT5665_STO1_DD_L_SRC_MASK		(0x1 << 9)
#define RT5665_STO1_DD_L_SRC_SFT		9
#define RT5665_STO1_DMIC_SRC_MASK		(0x1 << 8)
#define RT5665_STO1_DMIC_SRC_SFT		8
#define RT5665_STO1_DMIC_SRC_DMIC2		(0x1 << 8)
#define RT5665_STO1_DMIC_SRC_DMIC1		(0x0 << 8)
#define RT5665_M_STO1_ADC_R1			(0x1 << 7)
#define RT5665_M_STO1_ADC_R1_SFT		7
#define RT5665_M_STO1_ADC_R2			(0x1 << 6)
#define RT5665_M_STO1_ADC_R2_SFT		6
#define RT5665_STO1_ADC1R_SRC_MASK		(0x1 << 5)
#define RT5665_STO1_ADC1R_SRC_SFT		5
#define RT5665_STO1_ADC2R_SRC_MASK		(0x1 << 4)
#define RT5665_STO1_ADC2R_SRC_SFT		4
#define RT5665_STO1_ADCR_SRC_MASK		(0x3 << 2)
#define RT5665_STO1_ADCR_SRC_SFT		2
#define RT5665_STO1_DD_R_SRC_MASK		(0x3)
#define RT5665_STO1_DD_R_SRC_SFT		0


/* Mono1 ADC Mixer control (0x0027) */
#define RT5665_M_MONO_ADC_L1			(0x1 << 15)
#define RT5665_M_MONO_ADC_L1_SFT		15
#define RT5665_M_MONO_ADC_L2			(0x1 << 14)
#define RT5665_M_MONO_ADC_L2_SFT		14
#define RT5665_MONO_ADC_L1_SRC_MASK		(0x1 << 13)
#define RT5665_MONO_ADC_L1_SRC_SFT		13
#define RT5665_MONO_ADC_L2_SRC_MASK		(0x1 << 12)
#define RT5665_MONO_ADC_L2_SRC_SFT		12
#define RT5665_MONO_ADC_L_SRC_MASK		(0x3 << 10)
#define RT5665_MONO_ADC_L_SRC_SFT		10
#define RT5665_MONO_DD_L_SRC_MASK		(0x1 << 9)
#define RT5665_MONO_DD_L_SRC_SFT		9
#define RT5665_MONO_DMIC_L_SRC_MASK		(0x1 << 8)
#define RT5665_MONO_DMIC_L_SRC_SFT		8
#define RT5665_M_MONO_ADC_R1			(0x1 << 7)
#define RT5665_M_MONO_ADC_R1_SFT		7
#define RT5665_M_MONO_ADC_R2			(0x1 << 6)
#define RT5665_M_MONO_ADC_R2_SFT		6
#define RT5665_MONO_ADC_R1_SRC_MASK		(0x1 << 5)
#define RT5665_MONO_ADC_R1_SRC_SFT		5
#define RT5665_MONO_ADC_R2_SRC_MASK		(0x1 << 4)
#define RT5665_MONO_ADC_R2_SRC_SFT		4
#define RT5665_MONO_ADC_R_SRC_MASK		(0x3 << 2)
#define RT5665_MONO_ADC_R_SRC_SFT		2
#define RT5665_MONO_DD_R_SRC_MASK		(0x1 << 1)
#define RT5665_MONO_DD_R_SRC_SFT		1
#define RT5665_MONO_DMIC_R_SRC_MASK		0x1
#define RT5665_MONO_DMIC_R_SRC_SFT		0

/* Stereo2 ADC Mixer Control (0x0028) */
#define RT5665_M_STO2_ADC_L1			(0x1 << 15)
#define RT5665_M_STO2_ADC_L1_UN			(0x0 << 15)
#define RT5665_M_STO2_ADC_L1_SFT		15
#define RT5665_M_STO2_ADC_L2			(0x1 << 14)
#define RT5665_M_STO2_ADC_L2_SFT		14
#define RT5665_STO2_ADC1L_SRC_MASK		(0x1 << 13)
#define RT5665_STO2_ADC1L_SRC_SFT		13
#define RT5665_STO2_ADC1_SRC_ADC		(0x1 << 13)
#define RT5665_STO2_ADC1_SRC_DACMIX		(0x0 << 13)
#define RT5665_STO2_ADC2L_SRC_MASK		(0x1 << 12)
#define RT5665_STO2_ADC2L_SRC_SFT		12
#define RT5665_STO2_ADCL_SRC_MASK		(0x3 << 10)
#define RT5665_STO2_ADCL_SRC_SFT		10
#define RT5665_STO2_DD_L_SRC_MASK		(0x1 << 9)
#define RT5665_STO2_DD_L_SRC_SFT		9
#define RT5665_STO2_DMIC_SRC_MASK		(0x1 << 8)
#define RT5665_STO2_DMIC_SRC_SFT		8
#define RT5665_STO2_DMIC_SRC_DMIC2		(0x1 << 8)
#define RT5665_STO2_DMIC_SRC_DMIC1		(0x0 << 8)
#define RT5665_M_STO2_ADC_R1			(0x1 << 7)
#define RT5665_M_STO2_ADC_R1_UN			(0x0 << 7)
#define RT5665_M_STO2_ADC_R1_SFT		7
#define RT5665_M_STO2_ADC_R2			(0x1 << 6)
#define RT5665_M_STO2_ADC_R2_SFT		6
#define RT5665_STO2_ADC1R_SRC_MASK		(0x1 << 5)
#define RT5665_STO2_ADC1R_SRC_SFT		5
#define RT5665_STO2_ADC2R_SRC_MASK		(0x1 << 4)
#define RT5665_STO2_ADC2R_SRC_SFT		4
#define RT5665_STO2_ADCR_SRC_MASK		(0x3 << 2)
#define RT5665_STO2_ADCR_SRC_SFT		2
#define RT5665_STO2_DD_R_SRC_MASK		(0x1 << 1)
#define RT5665_STO2_DD_R_SRC_SFT		1

/* ADC Mixer to DAC Mixer Control (0x0029) */
#define RT5665_M_ADCMIX_L			(0x1 << 15)
#define RT5665_M_ADCMIX_L_SFT			15
#define RT5665_M_DAC1_L				(0x1 << 14)
#define RT5665_M_DAC1_L_SFT			14
#define RT5665_DAC1_R_SEL_MASK			(0x3 << 10)
#define RT5665_DAC1_R_SEL_SFT			10
#define RT5665_DAC1_L_SEL_MASK			(0x3 << 8)
#define RT5665_DAC1_L_SEL_SFT			8
#define RT5665_M_ADCMIX_R			(0x1 << 7)
#define RT5665_M_ADCMIX_R_SFT			7
#define RT5665_M_DAC1_R				(0x1 << 6)
#define RT5665_M_DAC1_R_SFT			6

/* Stereo1 DAC Mixer Control (0x002a) */
#define RT5665_M_DAC_L1_STO_L			(0x1 << 15)
#define RT5665_M_DAC_L1_STO_L_SFT		15
#define RT5665_G_DAC_L1_STO_L_MASK		(0x1 << 14)
#define RT5665_G_DAC_L1_STO_L_SFT		14
#define RT5665_M_DAC_R1_STO_L			(0x1 << 13)
#define RT5665_M_DAC_R1_STO_L_SFT		13
#define RT5665_G_DAC_R1_STO_L_MASK		(0x1 << 12)
#define RT5665_G_DAC_R1_STO_L_SFT		12
#define RT5665_M_DAC_L2_STO_L			(0x1 << 11)
#define RT5665_M_DAC_L2_STO_L_SFT		11
#define RT5665_G_DAC_L2_STO_L_MASK		(0x1 << 10)
#define RT5665_G_DAC_L2_STO_L_SFT		10
#define RT5665_M_DAC_R2_STO_L			(0x1 << 9)
#define RT5665_M_DAC_R2_STO_L_SFT		9
#define RT5665_G_DAC_R2_STO_L_MASK		(0x1 << 8)
#define RT5665_G_DAC_R2_STO_L_SFT		8
#define RT5665_M_DAC_L1_STO_R			(0x1 << 7)
#define RT5665_M_DAC_L1_STO_R_SFT		7
#define RT5665_G_DAC_L1_STO_R_MASK		(0x1 << 6)
#define RT5665_G_DAC_L1_STO_R_SFT		6
#define RT5665_M_DAC_R1_STO_R			(0x1 << 5)
#define RT5665_M_DAC_R1_STO_R_SFT		5
#define RT5665_G_DAC_R1_STO_R_MASK		(0x1 << 4)
#define RT5665_G_DAC_R1_STO_R_SFT		4
#define RT5665_M_DAC_L2_STO_R			(0x1 << 3)
#define RT5665_M_DAC_L2_STO_R_SFT		3
#define RT5665_G_DAC_L2_STO_R_MASK		(0x1 << 2)
#define RT5665_G_DAC_L2_STO_R_SFT		2
#define RT5665_M_DAC_R2_STO_R			(0x1 << 1)
#define RT5665_M_DAC_R2_STO_R_SFT		1
#define RT5665_G_DAC_R2_STO_R_MASK		(0x1)
#define RT5665_G_DAC_R2_STO_R_SFT		0

/* Mono DAC Mixer Control (0x002b) */
#define RT5665_M_DAC_L1_MONO_L			(0x1 << 15)
#define RT5665_M_DAC_L1_MONO_L_SFT		15
#define RT5665_G_DAC_L1_MONO_L_MASK		(0x1 << 14)
#define RT5665_G_DAC_L1_MONO_L_SFT		14
#define RT5665_M_DAC_R1_MONO_L			(0x1 << 13)
#define RT5665_M_DAC_R1_MONO_L_SFT		13
#define RT5665_G_DAC_R1_MONO_L_MASK		(0x1 << 12)
#define RT5665_G_DAC_R1_MONO_L_SFT		12
#define RT5665_M_DAC_L2_MONO_L			(0x1 << 11)
#define RT5665_M_DAC_L2_MONO_L_SFT		11
#define RT5665_G_DAC_L2_MONO_L_MASK		(0x1 << 10)
#define RT5665_G_DAC_L2_MONO_L_SFT		10
#define RT5665_M_DAC_R2_MONO_L			(0x1 << 9)
#define RT5665_M_DAC_R2_MONO_L_SFT		9
#define RT5665_G_DAC_R2_MONO_L_MASK		(0x1 << 8)
#define RT5665_G_DAC_R2_MONO_L_SFT		8
#define RT5665_M_DAC_L1_MONO_R			(0x1 << 7)
#define RT5665_M_DAC_L1_MONO_R_SFT		7
#define RT5665_G_DAC_L1_MONO_R_MASK		(0x1 << 6)
#define RT5665_G_DAC_L1_MONO_R_SFT		6
#define RT5665_M_DAC_R1_MONO_R			(0x1 << 5)
#define RT5665_M_DAC_R1_MONO_R_SFT		5
#define RT5665_G_DAC_R1_MONO_R_MASK		(0x1 << 4)
#define RT5665_G_DAC_R1_MONO_R_SFT		4
#define RT5665_M_DAC_L2_MONO_R			(0x1 << 3)
#define RT5665_M_DAC_L2_MONO_R_SFT		3
#define RT5665_G_DAC_L2_MONO_R_MASK		(0x1 << 2)
#define RT5665_G_DAC_L2_MONO_R_SFT		2
#define RT5665_M_DAC_R2_MONO_R			(0x1 << 1)
#define RT5665_M_DAC_R2_MONO_R_SFT		1
#define RT5665_G_DAC_R2_MONO_R_MASK		(0x1)
#define RT5665_G_DAC_R2_MONO_R_SFT		0

/* Stereo2 DAC Mixer Control (0x002c) */
#define RT5665_M_DAC_L1_STO2_L			(0x1 << 15)
#define RT5665_M_DAC_L1_STO2_L_SFT		15
#define RT5665_G_DAC_L1_STO2_L_MASK		(0x1 << 14)
#define RT5665_G_DAC_L1_STO2_L_SFT		14
#define RT5665_M_DAC_L2_STO2_L			(0x1 << 13)
#define RT5665_M_DAC_L2_STO2_L_SFT		13
#define RT5665_G_DAC_L2_STO2_L_MASK		(0x1 << 12)
#define RT5665_G_DAC_L2_STO2_L_SFT		12
#define RT5665_M_DAC_L3_STO2_L			(0x1 << 11)
#define RT5665_M_DAC_L3_STO2_L_SFT		11
#define RT5665_G_DAC_L3_STO2_L_MASK		(0x1 << 10)
#define RT5665_G_DAC_L3_STO2_L_SFT		10
#define RT5665_M_ST_DAC_L1			(0x1 << 9)
#define RT5665_M_ST_DAC_L1_SFT			9
#define RT5665_M_ST_DAC_R1			(0x1 << 8)
#define RT5665_M_ST_DAC_R1_SFT			8
#define RT5665_M_DAC_R1_STO2_R			(0x1 << 7)
#define RT5665_M_DAC_R1_STO2_R_SFT		7
#define RT5665_G_DAC_R1_STO2_R_MASK		(0x1 << 6)
#define RT5665_G_DAC_R1_STO2_R_SFT		6
#define RT5665_M_DAC_R2_STO2_R			(0x1 << 5)
#define RT5665_M_DAC_R2_STO2_R_SFT		5
#define RT5665_G_DAC_R2_STO2_R_MASK		(0x1 << 4)
#define RT5665_G_DAC_R2_STO2_R_SFT		4
#define RT5665_M_DAC_R3_STO2_R			(0x1 << 3)
#define RT5665_M_DAC_R3_STO2_R_SFT		3
#define RT5665_G_DAC_R3_STO2_R_MASK		(0x1 << 2)
#define RT5665_G_DAC_R3_STO2_R_SFT		2

/* Analog DAC1 Input Source Control (0x002d) */
#define RT5665_DAC_MIX_L_MASK			(0x3 << 12)
#define RT5665_DAC_MIX_L_SFT			12
#define RT5665_DAC_MIX_R_MASK			(0x3 << 8)
#define RT5665_DAC_MIX_R_SFT			8
#define RT5665_DAC_L1_SRC_MASK			(0x3 << 4)
#define RT5665_A_DACL1_SFT			4
#define RT5665_DAC_R1_SRC_MASK			(0x3)
#define RT5665_A_DACR1_SFT			0

/* Analog DAC Input Source Control (0x002e) */
#define RT5665_A_DACL2_SEL			(0x1 << 4)
#define RT5665_A_DACL2_SFT			4
#define RT5665_A_DACR2_SEL			(0x1 << 0)
#define RT5665_A_DACR2_SFT			0

/* Digital Interface Data Control (0x002f) */
#define RT5665_IF2_1_ADC_IN_MASK		(0x7 << 12)
#define RT5665_IF2_1_ADC_IN_SFT			12
#define RT5665_IF2_1_DAC_SEL_MASK		(0x3 << 10)
#define RT5665_IF2_1_DAC_SEL_SFT		10
#define RT5665_IF2_1_ADC_SEL_MASK		(0x3 << 8)
#define RT5665_IF2_1_ADC_SEL_SFT		8
#define RT5665_IF2_2_ADC_IN_MASK		(0x7 << 4)
#define RT5665_IF2_2_ADC_IN_SFT			4
#define RT5665_IF2_2_DAC_SEL_MASK		(0x3 << 2)
#define RT5665_IF2_2_DAC_SEL_SFT		2
#define RT5665_IF2_2_ADC_SEL_MASK		(0x3 << 0)
#define RT5665_IF2_2_ADC_SEL_SFT		0

/* Digital Interface Data Control (0x0030) */
#define RT5665_IF3_ADC_IN_MASK			(0x7 << 4)
#define RT5665_IF3_ADC_IN_SFT			4
#define RT5665_IF3_DAC_SEL_MASK			(0x3 << 2)
#define RT5665_IF3_DAC_SEL_SFT			2
#define RT5665_IF3_ADC_SEL_MASK			(0x3 << 0)
#define RT5665_IF3_ADC_SEL_SFT			0

/* PDM Output Control (0x0031) */
#define RT5665_M_PDM1_L				(0x1 << 14)
#define RT5665_M_PDM1_L_SFT			14
#define RT5665_M_PDM1_R				(0x1 << 12)
#define RT5665_M_PDM1_R_SFT			12
#define RT5665_PDM1_L_MASK			(0x3 << 10)
#define RT5665_PDM1_L_SFT			10
#define RT5665_PDM1_R_MASK			(0x3 << 8)
#define RT5665_PDM1_R_SFT			8
#define RT5665_PDM1_BUSY			(0x1 << 6)
#define RT5665_PDM_PATTERN			(0x1 << 5)
#define RT5665_PDM_GAIN				(0x1 << 4)
#define RT5665_LRCK_PDM_PI2C			(0x1 << 3)
#define RT5665_PDM_DIV_MASK			(0x3)

/*S/PDIF Output Control (0x0036) */
#define RT5665_SPDIF_SEL_MASK			(0x3 << 0)
#define RT5665_SPDIF_SEL_SFT			0

/* REC Left Mixer Control 2 (0x003c) */
#define RT5665_M_CBJ_RM1_L			(0x1 << 7)
#define RT5665_M_CBJ_RM1_L_SFT			7
#define RT5665_M_BST1_RM1_L			(0x1 << 5)
#define RT5665_M_BST1_RM1_L_SFT			5
#define RT5665_M_BST2_RM1_L			(0x1 << 4)
#define RT5665_M_BST2_RM1_L_SFT			4
#define RT5665_M_BST3_RM1_L			(0x1 << 3)
#define RT5665_M_BST3_RM1_L_SFT			3
#define RT5665_M_BST4_RM1_L			(0x1 << 2)
#define RT5665_M_BST4_RM1_L_SFT			2
#define RT5665_M_INL_RM1_L			(0x1 << 1)
#define RT5665_M_INL_RM1_L_SFT			1
#define RT5665_M_INR_RM1_L			(0x1)
#define RT5665_M_INR_RM1_L_SFT			0

/* REC Right Mixer Control 2 (0x003e) */
#define RT5665_M_AEC_REF_RM1_R			(0x1 << 7)
#define RT5665_M_AEC_REF_RM1_R_SFT		7
#define RT5665_M_BST1_RM1_R			(0x1 << 5)
#define RT5665_M_BST1_RM1_R_SFT			5
#define RT5665_M_BST2_RM1_R			(0x1 << 4)
#define RT5665_M_BST2_RM1_R_SFT			4
#define RT5665_M_BST3_RM1_R			(0x1 << 3)
#define RT5665_M_BST3_RM1_R_SFT			3
#define RT5665_M_BST4_RM1_R			(0x1 << 2)
#define RT5665_M_BST4_RM1_R_SFT			2
#define RT5665_M_INR_RM1_R			(0x1 << 1)
#define RT5665_M_INR_RM1_R_SFT			1
#define RT5665_M_MONOVOL_RM1_R			(0x1)
#define RT5665_M_MONOVOL_RM1_R_SFT		0

/* REC Mixer 2 Left Control 2 (0x0041) */
#define RT5665_M_CBJ_RM2_L			(0x1 << 7)
#define RT5665_M_CBJ_RM2_L_SFT			7
#define RT5665_M_BST1_RM2_L			(0x1 << 5)
#define RT5665_M_BST1_RM2_L_SFT			5
#define RT5665_M_BST2_RM2_L			(0x1 << 4)
#define RT5665_M_BST2_RM2_L_SFT			4
#define RT5665_M_BST3_RM2_L			(0x1 << 3)
#define RT5665_M_BST3_RM2_L_SFT			3
#define RT5665_M_BST4_RM2_L			(0x1 << 2)
#define RT5665_M_BST4_RM2_L_SFT			2
#define RT5665_M_INL_RM2_L			(0x1 << 1)
#define RT5665_M_INL_RM2_L_SFT			1
#define RT5665_M_INR_RM2_L			(0x1)
#define RT5665_M_INR_RM2_L_SFT			0

/* REC Mixer 2 Right Control 2 (0x0043) */
#define RT5665_M_MONOVOL_RM2_R			(0x1 << 7)
#define RT5665_M_MONOVOL_RM2_R_SFT		7
#define RT5665_M_BST1_RM2_R			(0x1 << 5)
#define RT5665_M_BST1_RM2_R_SFT			5
#define RT5665_M_BST2_RM2_R			(0x1 << 4)
#define RT5665_M_BST2_RM2_R_SFT			4
#define RT5665_M_BST3_RM2_R			(0x1 << 3)
#define RT5665_M_BST3_RM2_R_SFT			3
#define RT5665_M_BST4_RM2_R			(0x1 << 2)
#define RT5665_M_BST4_RM2_R_SFT			2
#define RT5665_M_INL_RM2_R			(0x1 << 1)
#define RT5665_M_INL_RM2_R_SFT			1
#define RT5665_M_INR_RM2_R			(0x1)
#define RT5665_M_INR_RM2_R_SFT			0

/* SPK Left Mixer Control (0x0046) */
#define RT5665_M_BST3_SM_L			(0x1 << 4)
#define RT5665_M_BST3_SM_L_SFT			4
#define RT5665_M_IN_R_SM_L			(0x1 << 3)
#define RT5665_M_IN_R_SM_L_SFT			3
#define RT5665_M_IN_L_SM_L			(0x1 << 2)
#define RT5665_M_IN_L_SM_L_SFT			2
#define RT5665_M_BST1_SM_L			(0x1 << 1)
#define RT5665_M_BST1_SM_L_SFT			1
#define RT5665_M_DAC_L2_SM_L			(0x1)
#define RT5665_M_DAC_L2_SM_L_SFT		0

/* SPK Right Mixer Control (0x0047) */
#define RT5665_M_BST3_SM_R			(0x1 << 4)
#define RT5665_M_BST3_SM_R_SFT			4
#define RT5665_M_IN_R_SM_R			(0x1 << 3)
#define RT5665_M_IN_R_SM_R_SFT			3
#define RT5665_M_IN_L_SM_R			(0x1 << 2)
#define RT5665_M_IN_L_SM_R_SFT			2
#define RT5665_M_BST4_SM_R			(0x1 << 1)
#define RT5665_M_BST4_SM_R_SFT			1
#define RT5665_M_DAC_R2_SM_R			(0x1)
#define RT5665_M_DAC_R2_SM_R_SFT		0

/* SPO Amp Input and Gain Control (0x0048) */
#define RT5665_M_DAC_L2_SPKOMIX			(0x1 << 13)
#define RT5665_M_DAC_L2_SPKOMIX_SFT		13
#define RT5665_M_SPKVOLL_SPKOMIX		(0x1 << 12)
#define RT5665_M_SPKVOLL_SPKOMIX_SFT		12
#define RT5665_M_DAC_R2_SPKOMIX			(0x1 << 9)
#define RT5665_M_DAC_R2_SPKOMIX_SFT		9
#define RT5665_M_SPKVOLR_SPKOMIX		(0x1 << 8)
#define RT5665_M_SPKVOLR_SPKOMIX_SFT		8

/* MONOMIX Input and Gain Control (0x004b) */
#define RT5665_G_MONOVOL_MA			(0x1 << 10)
#define RT5665_G_MONOVOL_MA_SFT			10
#define RT5665_M_MONOVOL_MA			(0x1 << 9)
#define RT5665_M_MONOVOL_MA_SFT			9
#define RT5665_M_DAC_L2_MA			(0x1 << 8)
#define RT5665_M_DAC_L2_MA_SFT			8
#define RT5665_M_BST3_MM			(0x1 << 4)
#define RT5665_M_BST3_MM_SFT			4
#define RT5665_M_BST2_MM			(0x1 << 3)
#define RT5665_M_BST2_MM_SFT			3
#define RT5665_M_BST1_MM			(0x1 << 2)
#define RT5665_M_BST1_MM_SFT			2
#define RT5665_M_RECMIC2L_MM			(0x1 << 1)
#define RT5665_M_RECMIC2L_MM_SFT		1
#define RT5665_M_DAC_L2_MM			(0x1)
#define RT5665_M_DAC_L2_MM_SFT			0

/* Output Left Mixer Control 1 (0x004d) */
#define RT5665_G_BST3_OM_L_MASK			(0x7 << 12)
#define RT5665_G_BST3_OM_L_SFT			12
#define RT5665_G_BST2_OM_L_MASK			(0x7 << 9)
#define RT5665_G_BST2_OM_L_SFT			9
#define RT5665_G_BST1_OM_L_MASK			(0x7 << 6)
#define RT5665_G_BST1_OM_L_SFT			6
#define RT5665_G_IN_L_OM_L_MASK			(0x7 << 3)
#define RT5665_G_IN_L_OM_L_SFT			3
#define RT5665_G_DAC_L2_OM_L_MASK		(0x7 << 0)
#define RT5665_G_DAC_L2_OM_L_SFT		0

/* Output Left Mixer Input Control (0x004e) */
#define RT5665_M_BST3_OM_L			(0x1 << 4)
#define RT5665_M_BST3_OM_L_SFT			4
#define RT5665_M_BST2_OM_L			(0x1 << 3)
#define RT5665_M_BST2_OM_L_SFT			3
#define RT5665_M_BST1_OM_L			(0x1 << 2)
#define RT5665_M_BST1_OM_L_SFT			2
#define RT5665_M_IN_L_OM_L			(0x1 << 1)
#define RT5665_M_IN_L_OM_L_SFT			1
#define RT5665_M_DAC_L2_OM_L			(0x1)
#define RT5665_M_DAC_L2_OM_L_SFT		0

/* Output Right Mixer Input Control (0x0050) */
#define RT5665_M_BST4_OM_R			(0x1 << 4)
#define RT5665_M_BST4_OM_R_SFT			4
#define RT5665_M_BST3_OM_R			(0x1 << 3)
#define RT5665_M_BST3_OM_R_SFT			3
#define RT5665_M_BST2_OM_R			(0x1 << 2)
#define RT5665_M_BST2_OM_R_SFT			2
#define RT5665_M_IN_R_OM_R			(0x1 << 1)
#define RT5665_M_IN_R_OM_R_SFT			1
#define RT5665_M_DAC_R2_OM_R			(0x1)
#define RT5665_M_DAC_R2_OM_R_SFT		0

/* LOUT Mixer Control (0x0052) */
#define RT5665_M_DAC_L2_LM			(0x1 << 15)
#define RT5665_M_DAC_L2_LM_SFT			15
#define RT5665_M_DAC_R2_LM			(0x1 << 14)
#define RT5665_M_DAC_R2_LM_SFT			14
#define RT5665_M_OV_L_LM			(0x1 << 13)
#define RT5665_M_OV_L_LM_SFT			13
#define RT5665_M_OV_R_LM			(0x1 << 12)
#define RT5665_M_OV_R_LM_SFT			12
#define RT5665_LOUT_BST_SFT			11
#define RT5665_LOUT_DF				(0x1 << 11)
#define RT5665_LOUT_DF_SFT			11

/* Power Management for Digital 1 (0x0061) */
#define RT5665_PWR_I2S1_1			(0x1 << 15)
#define RT5665_PWR_I2S1_1_BIT			15
#define RT5665_PWR_I2S1_2			(0x1 << 14)
#define RT5665_PWR_I2S1_2_BIT			14
#define RT5665_PWR_I2S2_1			(0x1 << 13)
#define RT5665_PWR_I2S2_1_BIT			13
#define RT5665_PWR_I2S2_2			(0x1 << 12)
#define RT5665_PWR_I2S2_2_BIT			12
#define RT5665_PWR_DAC_L1			(0x1 << 11)
#define RT5665_PWR_DAC_L1_BIT			11
#define RT5665_PWR_DAC_R1			(0x1 << 10)
#define RT5665_PWR_DAC_R1_BIT			10
#define RT5665_PWR_I2S3				(0x1 << 9)
#define RT5665_PWR_I2S3_BIT			9
#define RT5665_PWR_LDO				(0x1 << 8)
#define RT5665_PWR_LDO_BIT			8
#define RT5665_PWR_DAC_L2			(0x1 << 7)
#define RT5665_PWR_DAC_L2_BIT			7
#define RT5665_PWR_DAC_R2			(0x1 << 6)
#define RT5665_PWR_DAC_R2_BIT			6
#define RT5665_PWR_ADC_L1			(0x1 << 4)
#define RT5665_PWR_ADC_L1_BIT			4
#define RT5665_PWR_ADC_R1			(0x1 << 3)
#define RT5665_PWR_ADC_R1_BIT			3
#define RT5665_PWR_ADC_L2			(0x1 << 2)
#define RT5665_PWR_ADC_L2_BIT			2
#define RT5665_PWR_ADC_R2			(0x1 << 1)
#define RT5665_PWR_ADC_R2_BIT			1

/* Power Management for Digital 2 (0x0062) */
#define RT5665_PWR_ADC_S1F			(0x1 << 15)
#define RT5665_PWR_ADC_S1F_BIT			15
#define RT5665_PWR_ADC_S2F			(0x1 << 14)
#define RT5665_PWR_ADC_S2F_BIT			14
#define RT5665_PWR_ADC_MF_L			(0x1 << 13)
#define RT5665_PWR_ADC_MF_L_BIT			13
#define RT5665_PWR_ADC_MF_R			(0x1 << 12)
#define RT5665_PWR_ADC_MF_R_BIT			12
#define RT5665_PWR_DAC_S2F			(0x1 << 11)
#define RT5665_PWR_DAC_S2F_BIT			11
#define RT5665_PWR_DAC_S1F			(0x1 << 10)
#define RT5665_PWR_DAC_S1F_BIT			10
#define RT5665_PWR_DAC_MF_L			(0x1 << 9)
#define RT5665_PWR_DAC_MF_L_BIT			9
#define RT5665_PWR_DAC_MF_R			(0x1 << 8)
#define RT5665_PWR_DAC_MF_R_BIT			8
#define RT5665_PWR_PDM1				(0x1 << 7)
#define RT5665_PWR_PDM1_BIT			7

/* Power Management for Analog 1 (0x0063) */
#define RT5665_PWR_VREF1			(0x1 << 15)
#define RT5665_PWR_VREF1_BIT			15
#define RT5665_PWR_FV1				(0x1 << 14)
#define RT5665_PWR_FV1_BIT			14
#define RT5665_PWR_VREF2			(0x1 << 13)
#define RT5665_PWR_VREF2_BIT			13
#define RT5665_PWR_FV2				(0x1 << 12)
#define RT5665_PWR_FV2_BIT			12
#define RT5665_PWR_VREF3			(0x1 << 11)
#define RT5665_PWR_VREF3_BIT			11
#define RT5665_PWR_FV3				(0x1 << 10)
#define RT5665_PWR_FV3_BIT			10
#define RT5665_PWR_MB				(0x1 << 9)
#define RT5665_PWR_MB_BIT			9
#define RT5665_PWR_LM				(0x1 << 8)
#define RT5665_PWR_LM_BIT			8
#define RT5665_PWR_BG				(0x1 << 7)
#define RT5665_PWR_BG_BIT			7
#define RT5665_PWR_MA				(0x1 << 6)
#define RT5665_PWR_MA_BIT			6
#define RT5665_PWR_HA_L				(0x1 << 5)
#define RT5665_PWR_HA_L_BIT			5
#define RT5665_PWR_HA_R				(0x1 << 4)
#define RT5665_PWR_HA_R_BIT			4
#define RT5665_HP_DRIVER_MASK			(0x3 << 2)
#define RT5665_HP_DRIVER_1X			(0x0 << 2)
#define RT5665_HP_DRIVER_3X			(0x1 << 2)
#define RT5665_HP_DRIVER_5X			(0x3 << 2)
#define RT5665_LDO1_DVO_MASK			(0x3)
#define RT5665_LDO1_DVO_09			(0x0)
#define RT5665_LDO1_DVO_10			(0x1)
#define RT5665_LDO1_DVO_12			(0x2)
#define RT5665_LDO1_DVO_14			(0x3)

/* Power Management for Analog 2 (0x0064) */
#define RT5665_PWR_BST1				(0x1 << 15)
#define RT5665_PWR_BST1_BIT			15
#define RT5665_PWR_BST2				(0x1 << 14)
#define RT5665_PWR_BST2_BIT			14
#define RT5665_PWR_BST3				(0x1 << 13)
#define RT5665_PWR_BST3_BIT			13
#define RT5665_PWR_BST4				(0x1 << 12)
#define RT5665_PWR_BST4_BIT			12
#define RT5665_PWR_MB1				(0x1 << 11)
#define RT5665_PWR_MB1_PWR_DOWN			(0x0 << 11)
#define RT5665_PWR_MB1_BIT			11
#define RT5665_PWR_MB2				(0x1 << 10)
#define RT5665_PWR_MB2_PWR_DOWN			(0x0 << 10)
#define RT5665_PWR_MB2_BIT			10
#define RT5665_PWR_MB3				(0x1 << 9)
#define RT5665_PWR_MB3_BIT			9
#define RT5665_PWR_BST1_P			(0x1 << 7)
#define RT5665_PWR_BST1_P_BIT			7
#define RT5665_PWR_BST2_P			(0x1 << 6)
#define RT5665_PWR_BST2_P_BIT			6
#define RT5665_PWR_BST3_P			(0x1 << 5)
#define RT5665_PWR_BST3_P_BIT			5
#define RT5665_PWR_BST4_P			(0x1 << 4)
#define RT5665_PWR_BST4_P_BIT			4
#define RT5665_PWR_JD1				(0x1 << 3)
#define RT5665_PWR_JD1_BIT			3
#define RT5665_PWR_JD2				(0x1 << 2)
#define RT5665_PWR_JD2_BIT			2
#define RT5665_PWR_RM1_L			(0x1 << 1)
#define RT5665_PWR_RM1_L_BIT			1
#define RT5665_PWR_RM1_R			(0x1)
#define RT5665_PWR_RM1_R_BIT			0

/* Power Management for Analog 3 (0x0065) */
#define RT5665_PWR_CBJ				(0x1 << 9)
#define RT5665_PWR_CBJ_BIT			9
#define RT5665_PWR_BST_L			(0x1 << 8)
#define RT5665_PWR_BST_L_BIT			8
#define RT5665_PWR_BST_R			(0x1 << 7)
#define RT5665_PWR_BST_R_BIT			7
#define RT5665_PWR_PLL				(0x1 << 6)
#define RT5665_PWR_PLL_BIT			6
#define RT5665_PWR_LDO2				(0x1 << 2)
#define RT5665_PWR_LDO2_BIT			2
#define RT5665_PWR_SVD				(0x1 << 1)
#define RT5665_PWR_SVD_BIT			1

/* Power Management for Mixer (0x0066) */
#define RT5665_PWR_RM2_L			(0x1 << 15)
#define RT5665_PWR_RM2_L_BIT			15
#define RT5665_PWR_RM2_R			(0x1 << 14)
#define RT5665_PWR_RM2_R_BIT			14
#define RT5665_PWR_OM_L				(0x1 << 13)
#define RT5665_PWR_OM_L_BIT			13
#define RT5665_PWR_OM_R				(0x1 << 12)
#define RT5665_PWR_OM_R_BIT			12
#define RT5665_PWR_MM				(0x1 << 11)
#define RT5665_PWR_MM_BIT			11
#define RT5665_PWR_AEC_REF			(0x1 << 6)
#define RT5665_PWR_AEC_REF_BIT			6
#define RT5665_PWR_STO1_DAC_L			(0x1 << 5)
#define RT5665_PWR_STO1_DAC_L_BIT		5
#define RT5665_PWR_STO1_DAC_R			(0x1 << 4)
#define RT5665_PWR_STO1_DAC_R_BIT		4
#define RT5665_PWR_MONO_DAC_L			(0x1 << 3)
#define RT5665_PWR_MONO_DAC_L_BIT		3
#define RT5665_PWR_MONO_DAC_R			(0x1 << 2)
#define RT5665_PWR_MONO_DAC_R_BIT		2
#define RT5665_PWR_STO2_DAC_L			(0x1 << 1)
#define RT5665_PWR_STO2_DAC_L_BIT		1
#define RT5665_PWR_STO2_DAC_R			(0x1)
#define RT5665_PWR_STO2_DAC_R_BIT		0

/* Power Management for Volume (0x0067) */
#define RT5665_PWR_OV_L				(0x1 << 13)
#define RT5665_PWR_OV_L_BIT			13
#define RT5665_PWR_OV_R				(0x1 << 12)
#define RT5665_PWR_OV_R_BIT			12
#define RT5665_PWR_IN_L				(0x1 << 9)
#define RT5665_PWR_IN_L_BIT			9
#define RT5665_PWR_IN_R				(0x1 << 8)
#define RT5665_PWR_IN_R_BIT			8
#define RT5665_PWR_MV				(0x1 << 7)
#define RT5665_PWR_MV_BIT			7
#define RT5665_PWR_MIC_DET			(0x1 << 5)
#define RT5665_PWR_MIC_DET_BIT			5

/* (0x006b) */
#define RT5665_SYS_CLK_DET			15
#define RT5665_HP_CLK_DET			14
#define RT5665_MONO_CLK_DET			13
#define RT5665_LOUT_CLK_DET			12
#define RT5665_POW_CLK_DET			0

/* Digital Microphone Control 1 (0x006e) */
#define RT5665_DMIC_1_EN_MASK			(0x1 << 15)
#define RT5665_DMIC_1_EN_SFT			15
#define RT5665_DMIC_1_DIS			(0x0 << 15)
#define RT5665_DMIC_1_EN			(0x1 << 15)
#define RT5665_DMIC_2_EN_MASK			(0x1 << 14)
#define RT5665_DMIC_2_EN_SFT			14
#define RT5665_DMIC_2_DIS			(0x0 << 14)
#define RT5665_DMIC_2_EN			(0x1 << 14)
#define RT5665_DMIC_2_DP_MASK			(0x1 << 9)
#define RT5665_DMIC_2_DP_SFT			9
#define RT5665_DMIC_2_DP_GPIO5			(0x0 << 9)
#define RT5665_DMIC_2_DP_IN2P			(0x1 << 9)
#define RT5665_DMIC_CLK_MASK			(0x7 << 5)
#define RT5665_DMIC_CLK_SFT			5
#define RT5665_DMIC_1_DP_MASK			(0x1 << 1)
#define RT5665_DMIC_1_DP_SFT			1
#define RT5665_DMIC_1_DP_GPIO4			(0x0 << 1)
#define RT5665_DMIC_1_DP_IN2N			(0x1 << 1)


/* Digital Microphone Control 1 (0x006f) */
#define RT5665_DMIC_2L_LH_MASK			(0x1 << 3)
#define RT5665_DMIC_2L_LH_SFT			3
#define RT5665_DMIC_2L_LH_RISING		(0x0 << 3)
#define RT5665_DMIC_2L_LH_FALLING		(0x1 << 3)
#define RT5665_DMIC_2R_LH_MASK			(0x1 << 2)
#define RT5665_DMIC_2R_LH_SFT			2
#define RT5665_DMIC_2R_LH_RISING		(0x0 << 2)
#define RT5665_DMIC_2R_LH_FALLING		(0x1 << 2)
#define RT5665_DMIC_1L_LH_MASK			(0x1 << 1)
#define RT5665_DMIC_1L_LH_SFT			1
#define RT5665_DMIC_1L_LH_RISING		(0x0 << 1)
#define RT5665_DMIC_1L_LH_FALLING		(0x1 << 1)
#define RT5665_DMIC_1R_LH_MASK			(0x1 << 0)
#define RT5665_DMIC_1R_LH_SFT			0
#define RT5665_DMIC_1R_LH_RISING		(0x0)
#define RT5665_DMIC_1R_LH_FALLING		(0x1)

/* I2S1/2/3 Audio Serial Data Port Control (0x0070 0x0071 0x0072) */
#define RT5665_I2S_MS_MASK			(0x1 << 15)
#define RT5665_I2S_MS_SFT			15
#define RT5665_I2S_MS_M				(0x0 << 15)
#define RT5665_I2S_MS_S				(0x1 << 15)
#define RT5665_I2S_PIN_CFG_MASK			(0x1 << 14)
#define RT5665_I2S_PIN_CFG_SFT			14
#define RT5665_I2S_CLK_SEL_MASK			(0x1 << 11)
#define RT5665_I2S_CLK_SEL_SFT			11
#define RT5665_I2S_BP_MASK			(0x1 << 8)
#define RT5665_I2S_BP_SFT			8
#define RT5665_I2S_BP_NOR			(0x0 << 8)
#define RT5665_I2S_BP_INV			(0x1 << 8)
#define RT5665_I2S_DL_MASK			(0x3 << 4)
#define RT5665_I2S_DL_SFT			4
#define RT5665_I2S_DL_16			(0x0 << 4)
#define RT5665_I2S_DL_20			(0x1 << 4)
#define RT5665_I2S_DL_24			(0x2 << 4)
#define RT5665_I2S_DL_8				(0x3 << 4)
#define RT5665_I2S_DF_MASK			(0x7)
#define RT5665_I2S_DF_SFT			0
#define RT5665_I2S_DF_I2S			(0x0)
#define RT5665_I2S_DF_LEFT			(0x1)
#define RT5665_I2S_DF_PCM_A			(0x2)
#define RT5665_I2S_DF_PCM_B			(0x3)
#define RT5665_I2S_DF_PCM_A_N			(0x6)
#define RT5665_I2S_DF_PCM_B_N			(0x7)

/* ADC/DAC Clock Control 1 (0x0073) */
#define RT5665_I2S_PD1_MASK			(0x7 << 12)
#define RT5665_I2S_PD1_SFT			12
#define RT5665_I2S_PD1_1			(0x0 << 12)
#define RT5665_I2S_PD1_2			(0x1 << 12)
#define RT5665_I2S_PD1_3			(0x2 << 12)
#define RT5665_I2S_PD1_4			(0x3 << 12)
#define RT5665_I2S_PD1_6			(0x4 << 12)
#define RT5665_I2S_PD1_8			(0x5 << 12)
#define RT5665_I2S_PD1_12			(0x6 << 12)
#define RT5665_I2S_PD1_16			(0x7 << 12)
#define RT5665_I2S_M_PD2_MASK			(0x7 << 8)
#define RT5665_I2S_M_PD2_SFT			8
#define RT5665_I2S_M_PD2_1			(0x0 << 8)
#define RT5665_I2S_M_PD2_2			(0x1 << 8)
#define RT5665_I2S_M_PD2_3			(0x2 << 8)
#define RT5665_I2S_M_PD2_4			(0x3 << 8)
#define RT5665_I2S_M_PD2_6			(0x4 << 8)
#define RT5665_I2S_M_PD2_8			(0x5 << 8)
#define RT5665_I2S_M_PD2_12			(0x6 << 8)
#define RT5665_I2S_M_PD2_16			(0x7 << 8)
#define RT5665_I2S_CLK_SRC_MASK			(0x3 << 4)
#define RT5665_I2S_CLK_SRC_SFT			4
#define RT5665_I2S_CLK_SRC_MCLK			(0x0 << 4)
#define RT5665_I2S_CLK_SRC_PLL1			(0x1 << 4)
#define RT5665_I2S_CLK_SRC_RCCLK		(0x2 << 4)
#define RT5665_DAC_OSR_MASK			(0x3 << 2)
#define RT5665_DAC_OSR_SFT			2
#define RT5665_DAC_OSR_128			(0x0 << 2)
#define RT5665_DAC_OSR_64			(0x1 << 2)
#define RT5665_DAC_OSR_32			(0x2 << 2)
#define RT5665_ADC_OSR_MASK			(0x3)
#define RT5665_ADC_OSR_SFT			0
#define RT5665_ADC_OSR_128			(0x0)
#define RT5665_ADC_OSR_64			(0x1)
#define RT5665_ADC_OSR_32			(0x2)

/* ADC/DAC Clock Control 2 (0x0074) */
#define RT5665_I2S_BCLK_MS2_MASK		(0x1 << 15)
#define RT5665_I2S_BCLK_MS2_SFT			15
#define RT5665_I2S_BCLK_MS2_32			(0x0 << 15)
#define RT5665_I2S_BCLK_MS2_64			(0x1 << 15)
#define RT5665_I2S_PD2_MASK			(0x7 << 12)
#define RT5665_I2S_PD2_SFT			12
#define RT5665_I2S_PD2_1			(0x0 << 12)
#define RT5665_I2S_PD2_2			(0x1 << 12)
#define RT5665_I2S_PD2_3			(0x2 << 12)
#define RT5665_I2S_PD2_4			(0x3 << 12)
#define RT5665_I2S_PD2_6			(0x4 << 12)
#define RT5665_I2S_PD2_8			(0x5 << 12)
#define RT5665_I2S_PD2_12			(0x6 << 12)
#define RT5665_I2S_PD2_16			(0x7 << 12)
#define RT5665_I2S_BCLK_MS3_MASK		(0x1 << 11)
#define RT5665_I2S_BCLK_MS3_SFT			11
#define RT5665_I2S_BCLK_MS3_32			(0x0 << 11)
#define RT5665_I2S_BCLK_MS3_64			(0x1 << 11)
#define RT5665_I2S_PD3_MASK			(0x7 << 8)
#define RT5665_I2S_PD3_SFT			8
#define RT5665_I2S_PD3_1			(0x0 << 8)
#define RT5665_I2S_PD3_2			(0x1 << 8)
#define RT5665_I2S_PD3_3			(0x2 << 8)
#define RT5665_I2S_PD3_4			(0x3 << 8)
#define RT5665_I2S_PD3_6			(0x4 << 8)
#define RT5665_I2S_PD3_8			(0x5 << 8)
#define RT5665_I2S_PD3_12			(0x6 << 8)
#define RT5665_I2S_PD3_16			(0x7 << 8)
#define RT5665_I2S_PD4_MASK			(0x7 << 4)
#define RT5665_I2S_PD4_SFT			4
#define RT5665_I2S_PD4_1			(0x0 << 4)
#define RT5665_I2S_PD4_2			(0x1 << 4)
#define RT5665_I2S_PD4_3			(0x2 << 4)
#define RT5665_I2S_PD4_4			(0x3 << 4)
#define RT5665_I2S_PD4_6			(0x4 << 4)
#define RT5665_I2S_PD4_8			(0x5 << 4)
#define RT5665_I2S_PD4_12			(0x6 << 4)
#define RT5665_I2S_PD4_16			(0x7 << 4)

/* TDM control 1 (0x0078) */
#define RT5665_I2S1_MODE_MASK			(0x1 << 15)
#define RT5665_I2S1_MODE_I2S			(0x0 << 15)
#define RT5665_I2S1_MODE_TDM			(0x1 << 15)
#define RT5665_TDM_IN_CH_MASK			(0x3 << 10)
#define RT5665_TDM_IN_CH_2			(0x0 << 10)
#define RT5665_TDM_IN_CH_4			(0x1 << 10)
#define RT5665_TDM_IN_CH_6			(0x2 << 10)
#define RT5665_TDM_IN_CH_8			(0x3 << 10)
#define RT5665_TDM_OUT_CH_MASK			(0x3 << 8)
#define RT5665_TDM_OUT_CH_2			(0x0 << 8)
#define RT5665_TDM_OUT_CH_4			(0x1 << 8)
#define RT5665_TDM_OUT_CH_6			(0x2 << 8)
#define RT5665_TDM_OUT_CH_8			(0x3 << 8)
#define RT5665_TDM_IN_LEN_MASK			(0x3 << 6)
#define RT5665_TDM_IN_LEN_16			(0x0 << 6)
#define RT5665_TDM_IN_LEN_20			(0x1 << 6)
#define RT5665_TDM_IN_LEN_24			(0x2 << 6)
#define RT5665_TDM_IN_LEN_32			(0x3 << 6)
#define RT5665_TDM_OUT_LEN_MASK			(0x3 << 4)
#define RT5665_TDM_OUT_LEN_16			(0x0 << 4)
#define RT5665_TDM_OUT_LEN_20			(0x1 << 4)
#define RT5665_TDM_OUT_LEN_24			(0x2 << 4)
#define RT5665_TDM_OUT_LEN_32			(0x3 << 4)


/* TDM control 2 (0x0079) */
#define RT5665_I2S1_1_DS_ADC_SLOT01_SFT		14
#define RT5665_I2S1_1_DS_ADC_SLOT23_SFT		12
#define RT5665_I2S1_1_DS_ADC_SLOT45_SFT		10
#define RT5665_I2S1_1_DS_ADC_SLOT67_SFT		8
#define RT5665_I2S1_2_DS_ADC_SLOT01_SFT		6
#define RT5665_I2S1_2_DS_ADC_SLOT23_SFT		4
#define RT5665_I2S1_2_DS_ADC_SLOT45_SFT		2
#define RT5665_I2S1_2_DS_ADC_SLOT67_SFT		0

/* TDM control 3/4 (0x007a) (0x007b) */
#define RT5665_IF1_ADC1_SEL_SFT			10
#define RT5665_IF1_ADC2_SEL_SFT			9
#define RT5665_IF1_ADC3_SEL_SFT			8
#define RT5665_IF1_ADC4_SEL_SFT			7
#define RT5665_TDM_ADC_SEL_SFT			0
#define RT5665_TDM_ADC_CTRL_MASK		(0x1f << 0)
#define RT5665_TDM_ADC_DATA_06			(0x6 << 0)

/* Global Clock Control (0x0080) */
#define RT5665_SCLK_SRC_MASK			(0x3 << 14)
#define RT5665_SCLK_SRC_SFT			14
#define RT5665_SCLK_SRC_MCLK			(0x0 << 14)
#define RT5665_SCLK_SRC_PLL1			(0x1 << 14)
#define RT5665_SCLK_SRC_RCCLK			(0x2 << 14)
#define RT5665_PLL1_SRC_MASK			(0x7 << 8)
#define RT5665_PLL1_SRC_SFT			8
#define RT5665_PLL1_SRC_MCLK			(0x0 << 8)
#define RT5665_PLL1_SRC_BCLK1			(0x1 << 8)
#define RT5665_PLL1_SRC_BCLK2			(0x2 << 8)
#define RT5665_PLL1_SRC_BCLK3			(0x3 << 8)
#define RT5665_PLL1_PD_MASK			(0x7 << 4)
#define RT5665_PLL1_PD_SFT			4


#define RT5665_PLL_INP_MAX			40000000
#define RT5665_PLL_INP_MIN			256000
/* PLL M/N/K Code Control 1 (0x0081) */
#define RT5665_PLL_N_MAX			0x001ff
#define RT5665_PLL_N_MASK			(RT5665_PLL_N_MAX << 7)
#define RT5665_PLL_N_SFT			7
#define RT5665_PLL_K_MAX			0x001f
#define RT5665_PLL_K_MASK			(RT5665_PLL_K_MAX)
#define RT5665_PLL_K_SFT			0

/* PLL M/N/K Code Control 2 (0x0082) */
#define RT5665_PLL_M_MAX			0x00f
#define RT5665_PLL_M_MASK			(RT5665_PLL_M_MAX << 12)
#define RT5665_PLL_M_SFT			12
#define RT5665_PLL_M_BP				(0x1 << 11)
#define RT5665_PLL_M_BP_SFT			11
#define RT5665_PLL_K_BP				(0x1 << 10)
#define RT5665_PLL_K_BP_SFT			10

/* PLL tracking mode 1 (0x0083) */
#define RT5665_I2S3_ASRC_MASK			(0x1 << 15)
#define RT5665_I2S3_ASRC_SFT			15
#define RT5665_I2S2_ASRC_MASK			(0x1 << 14)
#define RT5665_I2S2_ASRC_SFT			14
#define RT5665_I2S1_ASRC_MASK			(0x1 << 13)
#define RT5665_I2S1_ASRC_SFT			13
#define RT5665_DAC_STO1_ASRC_MASK		(0x1 << 12)
#define RT5665_DAC_STO1_ASRC_SFT		12
#define RT5665_DAC_STO2_ASRC_MASK		(0x1 << 11)
#define RT5665_DAC_STO2_ASRC_SFT		11
#define RT5665_DAC_MONO_L_ASRC_MASK		(0x1 << 10)
#define RT5665_DAC_MONO_L_ASRC_SFT		10
#define RT5665_DAC_MONO_R_ASRC_MASK		(0x1 << 9)
#define RT5665_DAC_MONO_R_ASRC_SFT		9
#define RT5665_DMIC_STO1_ASRC_MASK		(0x1 << 8)
#define RT5665_DMIC_STO1_ASRC_SFT		8
#define RT5665_DMIC_STO2_ASRC_MASK		(0x1 << 7)
#define RT5665_DMIC_STO2_ASRC_SFT		7
#define RT5665_DMIC_MONO_L_ASRC_MASK		(0x1 << 6)
#define RT5665_DMIC_MONO_L_ASRC_SFT		6
#define RT5665_DMIC_MONO_R_ASRC_MASK		(0x1 << 5)
#define RT5665_DMIC_MONO_R_ASRC_SFT		5
#define RT5665_ADC_STO1_ASRC_MASK		(0x1 << 4)
#define RT5665_ADC_STO1_ASRC_SFT		4
#define RT5665_ADC_STO2_ASRC_MASK		(0x1 << 3)
#define RT5665_ADC_STO2_ASRC_SFT		3
#define RT5665_ADC_MONO_L_ASRC_MASK		(0x1 << 2)
#define RT5665_ADC_MONO_L_ASRC_SFT		2
#define RT5665_ADC_MONO_R_ASRC_MASK		(0x1 << 1)
#define RT5665_ADC_MONO_R_ASRC_SFT		1

/* PLL tracking mode 2 (0x0084)*/
#define RT5665_DA_STO1_CLK_SEL_MASK		(0x7 << 12)
#define RT5665_DA_STO1_CLK_SEL_SFT		12
#define RT5665_DA_STO2_CLK_SEL_MASK		(0x7 << 8)
#define RT5665_DA_STO2_CLK_SEL_SFT		8
#define RT5665_DA_MONOL_CLK_SEL_MASK		(0x7 << 4)
#define RT5665_DA_MONOL_CLK_SEL_SFT		4
#define RT5665_DA_MONOR_CLK_SEL_MASK		(0x7)
#define RT5665_DA_MONOR_CLK_SEL_SFT		0

/* PLL tracking mode 3 (0x0085)*/
#define RT5665_AD_STO1_CLK_SEL_MASK		(0x7 << 12)
#define RT5665_AD_STO1_CLK_SEL_SFT		12
#define RT5665_AD_STO2_CLK_SEL_MASK		(0x7 << 8)
#define RT5665_AD_STO2_CLK_SEL_SFT		8
#define RT5665_AD_MONOL_CLK_SEL_MASK		(0x7 << 4)
#define RT5665_AD_MONOL_CLK_SEL_SFT		4
#define RT5665_AD_MONOR_CLK_SEL_MASK		(0x7)
#define RT5665_AD_MONOR_CLK_SEL_SFT		0

/* ASRC Control 4 (0x0086) */
#define RT5665_I2S1_RATE_MASK			(0xf << 12)
#define RT5665_I2S1_RATE_SFT			12
#define RT5665_I2S2_RATE_MASK			(0xf << 8)
#define RT5665_I2S2_RATE_SFT			8
#define RT5665_I2S3_RATE_MASK			(0xf << 4)
#define RT5665_I2S3_RATE_SFT			4

/* Depop Mode Control 1 (0x008e) */
#define RT5665_PUMP_EN				(0x1 << 3)

/* Depop Mode Control 2 (0x8f) */
#define RT5665_DEPOP_MASK			(0x1 << 13)
#define RT5665_DEPOP_SFT			13
#define RT5665_DEPOP_AUTO			(0x0 << 13)
#define RT5665_DEPOP_MAN			(0x1 << 13)
#define RT5665_RAMP_MASK			(0x1 << 12)
#define RT5665_RAMP_SFT				12
#define RT5665_RAMP_DIS				(0x0 << 12)
#define RT5665_RAMP_EN				(0x1 << 12)
#define RT5665_BPS_MASK				(0x1 << 11)
#define RT5665_BPS_SFT				11
#define RT5665_BPS_DIS				(0x0 << 11)
#define RT5665_BPS_EN				(0x1 << 11)
#define RT5665_FAST_UPDN_MASK			(0x1 << 10)
#define RT5665_FAST_UPDN_SFT			10
#define RT5665_FAST_UPDN_DIS			(0x0 << 10)
#define RT5665_FAST_UPDN_EN			(0x1 << 10)
#define RT5665_MRES_MASK			(0x3 << 8)
#define RT5665_MRES_SFT				8
#define RT5665_MRES_15MO			(0x0 << 8)
#define RT5665_MRES_25MO			(0x1 << 8)
#define RT5665_MRES_35MO			(0x2 << 8)
#define RT5665_MRES_45MO			(0x3 << 8)
#define RT5665_VLO_MASK				(0x1 << 7)
#define RT5665_VLO_SFT				7
#define RT5665_VLO_3V				(0x0 << 7)
#define RT5665_VLO_32V				(0x1 << 7)
#define RT5665_DIG_DP_MASK			(0x1 << 6)
#define RT5665_DIG_DP_SFT			6
#define RT5665_DIG_DP_DIS			(0x0 << 6)
#define RT5665_DIG_DP_EN			(0x1 << 6)
#define RT5665_DP_TH_MASK			(0x3 << 4)
#define RT5665_DP_TH_SFT			4

/* Depop Mode Control 3 (0x90) */
#define RT5665_CP_SYS_MASK			(0x7 << 12)
#define RT5665_CP_SYS_SFT			12
#define RT5665_CP_FQ1_MASK			(0x7 << 8)
#define RT5665_CP_FQ1_SFT			8
#define RT5665_CP_FQ2_MASK			(0x7 << 4)
#define RT5665_CP_FQ2_SFT			4
#define RT5665_CP_FQ3_MASK			(0x7)
#define RT5665_CP_FQ3_SFT			0
#define RT5665_CP_FQ_1_5_KHZ			0
#define RT5665_CP_FQ_3_KHZ			1
#define RT5665_CP_FQ_6_KHZ			2
#define RT5665_CP_FQ_12_KHZ			3
#define RT5665_CP_FQ_24_KHZ			4
#define RT5665_CP_FQ_48_KHZ			5
#define RT5665_CP_FQ_96_KHZ			6
#define RT5665_CP_FQ_192_KHZ			7

/* HPOUT charge pump 1 (0x0091) */
#define RT5665_OSW_L_MASK			(0x1 << 11)
#define RT5665_OSW_L_SFT			11
#define RT5665_OSW_L_DIS			(0x0 << 11)
#define RT5665_OSW_L_EN				(0x1 << 11)
#define RT5665_OSW_R_MASK			(0x1 << 10)
#define RT5665_OSW_R_SFT			10
#define RT5665_OSW_R_DIS			(0x0 << 10)
#define RT5665_OSW_R_EN				(0x1 << 10)
#define RT5665_PM_HP_MASK			(0x3 << 8)
#define RT5665_PM_HP_SFT			8
#define RT5665_PM_HP_LV				(0x0 << 8)
#define RT5665_PM_HP_MV				(0x1 << 8)
#define RT5665_PM_HP_HV				(0x2 << 8)
#define RT5665_IB_HP_MASK			(0x3 << 6)
#define RT5665_IB_HP_SFT			6
#define RT5665_IB_HP_125IL			(0x0 << 6)
#define RT5665_IB_HP_25IL			(0x1 << 6)
#define RT5665_IB_HP_5IL			(0x2 << 6)
#define RT5665_IB_HP_1IL			(0x3 << 6)

/* PV detection and SPK gain control (0x92) */
#define RT5665_PVDD_DET_MASK			(0x1 << 15)
#define RT5665_PVDD_DET_SFT			15
#define RT5665_PVDD_DET_DIS			(0x0 << 15)
#define RT5665_PVDD_DET_EN			(0x1 << 15)
#define RT5665_SPK_AG_MASK			(0x1 << 14)
#define RT5665_SPK_AG_SFT			14
#define RT5665_SPK_AG_DIS			(0x0 << 14)
#define RT5665_SPK_AG_EN			(0x1 << 14)

/* Micbias Control1 (0x93) */
#define RT5665_MIC1_BS_MASK			(0x1 << 15)
#define RT5665_MIC1_BS_SFT			15
#define RT5665_MIC1_BS_9AV			(0x0 << 15)
#define RT5665_MIC1_BS_75AV			(0x1 << 15)
#define RT5665_MIC2_BS_MASK			(0x1 << 14)
#define RT5665_MIC2_BS_SFT			14
#define RT5665_MIC2_BS_9AV			(0x0 << 14)
#define RT5665_MIC2_BS_75AV			(0x1 << 14)
#define RT5665_MIC1_CLK_MASK			(0x1 << 13)
#define RT5665_MIC1_CLK_SFT			13
#define RT5665_MIC1_CLK_DIS			(0x0 << 13)
#define RT5665_MIC1_CLK_EN			(0x1 << 13)
#define RT5665_MIC2_CLK_MASK			(0x1 << 12)
#define RT5665_MIC2_CLK_SFT			12
#define RT5665_MIC2_CLK_DIS			(0x0 << 12)
#define RT5665_MIC2_CLK_EN			(0x1 << 12)
#define RT5665_MIC1_OVCD_MASK			(0x1 << 11)
#define RT5665_MIC1_OVCD_SFT			11
#define RT5665_MIC1_OVCD_DIS			(0x0 << 11)
#define RT5665_MIC1_OVCD_EN			(0x1 << 11)
#define RT5665_MIC1_OVTH_MASK			(0x3 << 9)
#define RT5665_MIC1_OVTH_SFT			9
#define RT5665_MIC1_OVTH_600UA			(0x0 << 9)
#define RT5665_MIC1_OVTH_1500UA			(0x1 << 9)
#define RT5665_MIC1_OVTH_2000UA			(0x2 << 9)
#define RT5665_MIC2_OVCD_MASK			(0x1 << 8)
#define RT5665_MIC2_OVCD_SFT			8
#define RT5665_MIC2_OVCD_DIS			(0x0 << 8)
#define RT5665_MIC2_OVCD_EN			(0x1 << 8)
#define RT5665_MIC2_OVTH_MASK			(0x3 << 6)
#define RT5665_MIC2_OVTH_SFT			6
#define RT5665_MIC2_OVTH_600UA			(0x0 << 6)
#define RT5665_MIC2_OVTH_1500UA			(0x1 << 6)
#define RT5665_MIC2_OVTH_2000UA			(0x2 << 6)
#define RT5665_PWR_MB_MASK			(0x1 << 5)
#define RT5665_PWR_MB_SFT			5
#define RT5665_PWR_MB_PD			(0x0 << 5)
#define RT5665_PWR_MB_PU			(0x1 << 5)

/* Micbias Control2 (0x94) */
#define RT5665_PWR_CLK25M_MASK			(0x1 << 9)
#define RT5665_PWR_CLK25M_SFT			9
#define RT5665_PWR_CLK25M_PD			(0x0 << 9)
#define RT5665_PWR_CLK25M_PU			(0x1 << 9)
#define RT5665_PWR_CLK1M_MASK			(0x1 << 8)
#define RT5665_PWR_CLK1M_SFT			8
#define RT5665_PWR_CLK1M_PD			(0x0 << 8)
#define RT5665_PWR_CLK1M_PU			(0x1 << 8)


/* EQ Control 1 (0x00b0) */
#define RT5665_EQ_SRC_DAC			(0x0 << 15)
#define RT5665_EQ_SRC_ADC			(0x1 << 15)
#define RT5665_EQ_UPD				(0x1 << 14)
#define RT5665_EQ_UPD_BIT			14
#define RT5665_EQ_CD_MASK			(0x1 << 13)
#define RT5665_EQ_CD_SFT			13
#define RT5665_EQ_CD_DIS			(0x0 << 13)
#define RT5665_EQ_CD_EN				(0x1 << 13)
#define RT5665_EQ_DITH_MASK			(0x3 << 8)
#define RT5665_EQ_DITH_SFT			8
#define RT5665_EQ_DITH_NOR			(0x0 << 8)
#define RT5665_EQ_DITH_LSB			(0x1 << 8)
#define RT5665_EQ_DITH_LSB_1			(0x2 << 8)
#define RT5665_EQ_DITH_LSB_2			(0x3 << 8)

/* IRQ Control 1 (0x00b7) */
#define RT5665_JD1_1_EN_MASK			(0x1 << 15)
#define RT5665_JD1_1_EN_SFT			15
#define RT5665_JD1_1_DIS			(0x0 << 15)
#define RT5665_JD1_1_EN				(0x1 << 15)
#define RT5665_JD1_2_EN_MASK			(0x1 << 12)
#define RT5665_JD1_2_EN_SFT			12
#define RT5665_JD1_2_DIS			(0x0 << 12)
#define RT5665_JD1_2_EN				(0x1 << 12)

/* IRQ Control 2 (0x00b8) */
#define RT5665_IL_IRQ_MASK			(0x1 << 6)
#define RT5665_IL_IRQ_DIS			(0x0 << 6)
#define RT5665_IL_IRQ_EN			(0x1 << 6)

/* IRQ Control 5 (0x00ba) */
#define RT5665_IRQ_JD_EN			(0x1 << 3)
#define RT5665_IRQ_JD_EN_SFT			3

/* GPIO Control 1 (0x00c0) */
#define RT5665_GP1_PIN_MASK			(0x1 << 15)
#define RT5665_GP1_PIN_SFT			15
#define RT5665_GP1_PIN_GPIO1			(0x0 << 15)
#define RT5665_GP1_PIN_IRQ			(0x1 << 15)
#define RT5665_GP2_PIN_MASK			(0x3 << 13)
#define RT5665_GP2_PIN_SFT			13
#define RT5665_GP2_PIN_GPIO2			(0x0 << 13)
#define RT5665_GP2_PIN_BCLK2			(0x1 << 13)
#define RT5665_GP2_PIN_PDM_SCL			(0x2 << 13)
#define RT5665_GP3_PIN_MASK			(0x3 << 11)
#define RT5665_GP3_PIN_SFT			11
#define RT5665_GP3_PIN_GPIO3			(0x0 << 11)
#define RT5665_GP3_PIN_LRCK2			(0x1 << 11)
#define RT5665_GP3_PIN_PDM_SDA			(0x2 << 11)
#define RT5665_GP4_PIN_MASK			(0x3 << 9)
#define RT5665_GP4_PIN_SFT			9
#define RT5665_GP4_PIN_GPIO4			(0x0 << 9)
#define RT5665_GP4_PIN_DACDAT2_1		(0x1 << 9)
#define RT5665_GP4_PIN_DMIC1_SDA		(0x2 << 9)
#define RT5665_GP5_PIN_MASK			(0x3 << 7)
#define RT5665_GP5_PIN_SFT			7
#define RT5665_GP5_PIN_GPIO5			(0x0 << 7)
#define RT5665_GP5_PIN_ADCDAT2_1		(0x1 << 7)
#define RT5665_GP5_PIN_DMIC2_SDA		(0x2 << 7)
#define RT5665_GP6_PIN_MASK			(0x3 << 5)
#define RT5665_GP6_PIN_SFT			5
#define RT5665_GP6_PIN_GPIO6			(0x0 << 5)
#define RT5665_GP6_PIN_BCLK3			(0x0 << 5)
#define RT5665_GP6_PIN_PDM_SCL			(0x1 << 5)
#define RT5665_GP7_PIN_MASK			(0x3 << 3)
#define RT5665_GP7_PIN_SFT			3
#define RT5665_GP7_PIN_GPIO7			(0x0 << 3)
#define RT5665_GP7_PIN_LRCK3			(0x1 << 3)
#define RT5665_GP7_PIN_PDM_SDA			(0x2 << 3)
#define RT5665_GP8_PIN_MASK			(0x3 << 1)
#define RT5665_GP8_PIN_SFT			1
#define RT5665_GP8_PIN_GPIO8			(0x0 << 1)
#define RT5665_GP8_PIN_DACDAT3			(0x1 << 1)
#define RT5665_GP8_PIN_DMIC2_SCL		(0x2 << 1)
#define RT5665_GP8_PIN_DACDAT2_2		(0x3 << 1)


/* GPIO Control 2 (0x00c1)*/
#define RT5665_GP9_PIN_MASK			(0x3 << 14)
#define RT5665_GP9_PIN_SFT			14
#define RT5665_GP9_PIN_GPIO9			(0x0 << 14)
#define RT5665_GP9_PIN_ADCDAT3			(0x1 << 14)
#define RT5665_GP9_PIN_DMIC1_SCL		(0x2 << 14)
#define RT5665_GP9_PIN_ADCDAT2_2		(0x3 << 14)
#define RT5665_GP10_PIN_MASK			(0x3 << 12)
#define RT5665_GP10_PIN_SFT			12
#define RT5665_GP10_PIN_GPIO10			(0x0 << 12)
#define RT5665_GP10_PIN_ADCDAT1_2		(0x1 << 12)
#define RT5665_GP10_PIN_LPD			(0x2 << 12)
#define RT5665_GP1_PF_MASK			(0x1 << 11)
#define RT5665_GP1_PF_IN			(0x0 << 11)
#define RT5665_GP1_PF_OUT			(0x1 << 11)
#define RT5665_GP1_OUT_MASK			(0x1 << 10)
#define RT5665_GP1_OUT_H			(0x0 << 10)
#define RT5665_GP1_OUT_L			(0x1 << 10)
#define RT5665_GP2_PF_MASK			(0x1 << 9)
#define RT5665_GP2_PF_IN			(0x0 << 9)
#define RT5665_GP2_PF_OUT			(0x1 << 9)
#define RT5665_GP2_OUT_MASK			(0x1 << 8)
#define RT5665_GP2_OUT_H			(0x0 << 8)
#define RT5665_GP2_OUT_L			(0x1 << 8)
#define RT5665_GP3_PF_MASK			(0x1 << 7)
#define RT5665_GP3_PF_IN			(0x0 << 7)
#define RT5665_GP3_PF_OUT			(0x1 << 7)
#define RT5665_GP3_OUT_MASK			(0x1 << 6)
#define RT5665_GP3_OUT_H			(0x0 << 6)
#define RT5665_GP3_OUT_L			(0x1 << 6)
#define RT5665_GP4_PF_MASK			(0x1 << 5)
#define RT5665_GP4_PF_IN			(0x0 << 5)
#define RT5665_GP4_PF_OUT			(0x1 << 5)
#define RT5665_GP4_OUT_MASK			(0x1 << 4)
#define RT5665_GP4_OUT_H			(0x0 << 4)
#define RT5665_GP4_OUT_L			(0x1 << 4)
#define RT5665_GP5_PF_MASK			(0x1 << 3)
#define RT5665_GP5_PF_IN			(0x0 << 3)
#define RT5665_GP5_PF_OUT			(0x1 << 3)
#define RT5665_GP5_OUT_MASK			(0x1 << 2)
#define RT5665_GP5_OUT_H			(0x0 << 2)
#define RT5665_GP5_OUT_L			(0x1 << 2)
#define RT5665_GP6_PF_MASK			(0x1 << 1)
#define RT5665_GP6_PF_IN			(0x0 << 1)
#define RT5665_GP6_PF_OUT			(0x1 << 1)
#define RT5665_GP6_OUT_MASK			(0x1)
#define RT5665_GP6_OUT_H			(0x0)
#define RT5665_GP6_OUT_L			(0x1)


/* GPIO Control 3 (0x00c2) */
#define RT5665_GP7_PF_MASK			(0x1 << 15)
#define RT5665_GP7_PF_IN			(0x0 << 15)
#define RT5665_GP7_PF_OUT			(0x1 << 15)
#define RT5665_GP7_OUT_MASK			(0x1 << 14)
#define RT5665_GP7_OUT_H			(0x0 << 14)
#define RT5665_GP7_OUT_L			(0x1 << 14)
#define RT5665_GP8_PF_MASK			(0x1 << 13)
#define RT5665_GP8_PF_IN			(0x0 << 13)
#define RT5665_GP8_PF_OUT			(0x1 << 13)
#define RT5665_GP8_OUT_MASK			(0x1 << 12)
#define RT5665_GP8_OUT_H			(0x0 << 12)
#define RT5665_GP8_OUT_L			(0x1 << 12)
#define RT5665_GP9_PF_MASK			(0x1 << 11)
#define RT5665_GP9_PF_IN			(0x0 << 11)
#define RT5665_GP9_PF_OUT			(0x1 << 11)
#define RT5665_GP9_OUT_MASK			(0x1 << 10)
#define RT5665_GP9_OUT_H			(0x0 << 10)
#define RT5665_GP9_OUT_L			(0x1 << 10)
#define RT5665_GP10_PF_MASK			(0x1 << 9)
#define RT5665_GP10_PF_IN			(0x0 << 9)
#define RT5665_GP10_PF_OUT			(0x1 << 9)
#define RT5665_GP10_OUT_MASK			(0x1 << 8)
#define RT5665_GP10_OUT_H			(0x0 << 8)
#define RT5665_GP10_OUT_L			(0x1 << 8)
#define RT5665_GP11_PF_MASK			(0x1 << 7)
#define RT5665_GP11_PF_IN			(0x0 << 7)
#define RT5665_GP11_PF_OUT			(0x1 << 7)
#define RT5665_GP11_OUT_MASK			(0x1 << 6)
#define RT5665_GP11_OUT_H			(0x0 << 6)
#define RT5665_GP11_OUT_L			(0x1 << 6)

/* Soft volume and zero cross control 1 (0x00d9) */
#define RT5665_SV_MASK				(0x1 << 15)
#define RT5665_SV_SFT				15
#define RT5665_SV_DIS				(0x0 << 15)
#define RT5665_SV_EN				(0x1 << 15)
#define RT5665_OUT_SV_MASK			(0x1 << 13)
#define RT5665_OUT_SV_SFT			13
#define RT5665_OUT_SV_DIS			(0x0 << 13)
#define RT5665_OUT_SV_EN			(0x1 << 13)
#define RT5665_HP_SV_MASK			(0x1 << 12)
#define RT5665_HP_SV_SFT			12
#define RT5665_HP_SV_DIS			(0x0 << 12)
#define RT5665_HP_SV_EN				(0x1 << 12)
#define RT5665_ZCD_DIG_MASK			(0x1 << 11)
#define RT5665_ZCD_DIG_SFT			11
#define RT5665_ZCD_DIG_DIS			(0x0 << 11)
#define RT5665_ZCD_DIG_EN			(0x1 << 11)
#define RT5665_ZCD_MASK				(0x1 << 10)
#define RT5665_ZCD_SFT				10
#define RT5665_ZCD_PD				(0x0 << 10)
#define RT5665_ZCD_PU				(0x1 << 10)
#define RT5665_SV_DLY_MASK			(0xf)
#define RT5665_SV_DLY_SFT			0

/* Soft volume and zero cross control 2 (0x00da) */
#define RT5665_ZCD_HP_MASK			(0x1 << 15)
#define RT5665_ZCD_HP_SFT			15
#define RT5665_ZCD_HP_DIS			(0x0 << 15)
#define RT5665_ZCD_HP_EN			(0x1 << 15)

/* 4 Button Inline Command Control 2 (0x00e0) */
#define RT5665_4BTN_IL_MASK			(0x1 << 15)
#define RT5665_4BTN_IL_EN			(0x1 << 15)
#define RT5665_4BTN_IL_DIS			(0x0 << 15)
#define RT5665_4BTN_IL_RST_MASK			(0x1 << 14)
#define RT5665_4BTN_IL_NOR			(0x1 << 14)
#define RT5665_4BTN_IL_RST			(0x0 << 14)

/* Analog JD Control 1 (0x00f0) */
#define RT5665_JD1_MODE_MASK			(0x3 << 0)
#define RT5665_JD1_MODE_0			(0x0 << 0)
#define RT5665_JD1_MODE_1			(0x1 << 0)
#define RT5665_JD1_MODE_2			(0x2 << 0)

/* Jack Detect Control 3 (0x00f8) */
#define RT5665_JD_TRI_HPO_SEL_MASK		(0x7)
#define RT5665_JD_TRI_HPO_SEL_SFT		(0)
#define RT5665_JD_HPO_GPIO_JD1			(0x0)
#define RT5665_JD_HPO_JD1_1			(0x1)
#define RT5665_JD_HPO_JD1_2			(0x2)
#define RT5665_JD_HPO_JD2			(0x3)
#define RT5665_JD_HPO_GPIO_JD2			(0x4)
#define RT5665_JD_HPO_JD3			(0x5)
#define RT5665_JD_HPO_JD_D			(0x6)

/* Digital Misc Control (0x00fa) */
#define RT5665_AM_MASK				(0x1 << 7)
#define RT5665_AM_EN				(0x1 << 7)
#define RT5665_AM_DIS				(0x1 << 7)
#define RT5665_DIG_GATE_CTRL			0x1
#define RT5665_DIG_GATE_CTRL_SFT		(0)

/* Chopper and Clock control for ADC (0x011c)*/
#define RT5665_M_RF_DIG_MASK			(0x1 << 12)
#define RT5665_M_RF_DIG_SFT			12
#define RT5665_M_RI_DIG				(0x1 << 11)

/* Chopper and Clock control for DAC (0x013a)*/
#define RT5665_CKXEN_DAC1_MASK			(0x1 << 13)
#define RT5665_CKXEN_DAC1_SFT			13
#define RT5665_CKGEN_DAC1_MASK			(0x1 << 12)
#define RT5665_CKGEN_DAC1_SFT			12
#define RT5665_CKXEN_DAC2_MASK			(0x1 << 5)
#define RT5665_CKXEN_DAC2_SFT			5
#define RT5665_CKGEN_DAC2_MASK			(0x1 << 4)
#define RT5665_CKGEN_DAC2_SFT			4

/* Chopper and Clock control for ADC (0x013b)*/
#define RT5665_CKXEN_ADC1_MASK			(0x1 << 13)
#define RT5665_CKXEN_ADC1_SFT			13
#define RT5665_CKGEN_ADC1_MASK			(0x1 << 12)
#define RT5665_CKGEN_ADC1_SFT			12
#define RT5665_CKXEN_ADC2_MASK			(0x1 << 5)
#define RT5665_CKXEN_ADC2_SFT			5
#define RT5665_CKGEN_ADC2_MASK			(0x1 << 4)
#define RT5665_CKGEN_ADC2_SFT			4

/* Volume test (0x013f)*/
#define RT5665_SEL_CLK_VOL_MASK			(0x1 << 15)
#define RT5665_SEL_CLK_VOL_EN			(0x1 << 15)
#define RT5665_SEL_CLK_VOL_DIS			(0x0 << 15)

/* Test Mode Control 1 (0x0145) */
#define RT5665_AD2DA_LB_MASK			(0x1 << 9)
#define RT5665_AD2DA_LB_SFT			9

/* Stereo Noise Gate Control 1 (0x0160) */
#define RT5665_NG2_EN_MASK			(0x1 << 15)
#define RT5665_NG2_EN				(0x1 << 15)
#define RT5665_NG2_DIS				(0x0 << 15)

/* Stereo1 DAC Silence Detection Control (0x0190) */
#define RT5665_DEB_STO_DAC_MASK			(0x7 << 4)
#define RT5665_DEB_80_MS			(0x0 << 4)

/* SAR ADC Inline Command Control 1 (0x0210) */
#define RT5665_SAR_BUTT_DET_MASK		(0x1 << 15)
#define RT5665_SAR_BUTT_DET_EN			(0x1 << 15)
#define RT5665_SAR_BUTT_DET_DIS			(0x0 << 15)
#define RT5665_SAR_BUTDET_MODE_MASK		(0x1 << 14)
#define RT5665_SAR_BUTDET_POW_SAV		(0x1 << 14)
#define RT5665_SAR_BUTDET_POW_NORM		(0x0 << 14)
#define RT5665_SAR_BUTDET_RST_MASK		(0x1 << 13)
#define RT5665_SAR_BUTDET_RST_NORMAL		(0x1 << 13)
#define RT5665_SAR_BUTDET_RST			(0x0 << 13)
#define RT5665_SAR_POW_MASK			(0x1 << 12)
#define RT5665_SAR_POW_EN			(0x1 << 12)
#define RT5665_SAR_POW_DIS			(0x0 << 12)
#define RT5665_SAR_RST_MASK			(0x1 << 11)
#define RT5665_SAR_RST_NORMAL			(0x1 << 11)
#define RT5665_SAR_RST				(0x0 << 11)
#define RT5665_SAR_BYPASS_MASK			(0x1 << 10)
#define RT5665_SAR_BYPASS_EN			(0x1 << 10)
#define RT5665_SAR_BYPASS_DIS			(0x0 << 10)
#define RT5665_SAR_SEL_MB1_MASK			(0x1 << 9)
#define RT5665_SAR_SEL_MB1_SEL			(0x1 << 9)
#define RT5665_SAR_SEL_MB1_NOSEL		(0x0 << 9)
#define RT5665_SAR_SEL_MB2_MASK			(0x1 << 8)
#define RT5665_SAR_SEL_MB2_SEL			(0x1 << 8)
#define RT5665_SAR_SEL_MB2_NOSEL		(0x0 << 8)
#define RT5665_SAR_SEL_MODE_MASK		(0x1 << 7)
#define RT5665_SAR_SEL_MODE_CMP			(0x1 << 7)
#define RT5665_SAR_SEL_MODE_ADC			(0x0 << 7)
#define RT5665_SAR_SEL_MB1_MB2_MASK		(0x1 << 5)
#define RT5665_SAR_SEL_MB1_MB2_AUTO		(0x1 << 5)
#define RT5665_SAR_SEL_MB1_MB2_MANU		(0x0 << 5)
#define RT5665_SAR_SEL_SIGNAL_MASK		(0x1 << 4)
#define RT5665_SAR_SEL_SIGNAL_AUTO		(0x1 << 4)
#define RT5665_SAR_SEL_SIGNAL_MANU		(0x0 << 4)

/* System Clock Source */
enum {
	RT5665_SCLK_S_MCLK,
	RT5665_SCLK_S_PLL1,
	RT5665_SCLK_S_RCCLK,
};

/* PLL1 Source */
enum {
	RT5665_PLL1_S_MCLK,
	RT5665_PLL1_S_BCLK1,
	RT5665_PLL1_S_BCLK2,
	RT5665_PLL1_S_BCLK3,
	RT5665_PLL1_S_BCLK4,
};

enum {
	RT5665_AIF1_1,
	RT5665_AIF1_2,
	RT5665_AIF2_1,
	RT5665_AIF2_2,
	RT5665_AIF3,
	RT5665_AIFS
};

enum {
	CODEC_5665,
	CODEC_5666,
	CODEC_5668,
};

/* filter mask */
enum {
	RT5665_DA_STEREO1_FILTER = 0x1,
	RT5665_DA_STEREO2_FILTER = (0x1 << 1),
	RT5665_DA_MONO_L_FILTER = (0x1 << 2),
	RT5665_DA_MONO_R_FILTER = (0x1 << 3),
	RT5665_AD_STEREO1_FILTER = (0x1 << 4),
	RT5665_AD_STEREO2_FILTER = (0x1 << 5),
	RT5665_AD_MONO_L_FILTER = (0x1 << 6),
	RT5665_AD_MONO_R_FILTER = (0x1 << 7),
};

enum {
	RT5665_CLK_SEL_SYS,
	RT5665_CLK_SEL_I2S1_ASRC,
	RT5665_CLK_SEL_I2S2_ASRC,
	RT5665_CLK_SEL_I2S3_ASRC,
	RT5665_CLK_SEL_SYS2,
	RT5665_CLK_SEL_SYS3,
	RT5665_CLK_SEL_SYS4,
};

int rt5665_sel_asrc_clk_src(struct snd_soc_codec *codec,
		unsigned int filter_mask, unsigned int clk_src);

#endif /* __RT5665_H__ */
