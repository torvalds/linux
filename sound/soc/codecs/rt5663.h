/*
 * rt5663.h  --  RT5663 ALSA SoC audio driver
 *
 * Copyright 2016 Realtek Microelectronics
 * Author: Jack Yu <jack.yu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5663_H__
#define __RT5663_H__

/* Info */
#define RT5663_RESET				0x0000
#define RT5663_VENDOR_ID			0x00fd
#define RT5663_VENDOR_ID_1			0x00fe
#define RT5663_VENDOR_ID_2			0x00ff

#define RT5663_LOUT_CTRL			0x0001
#define RT5663_HP_AMP_2				0x0003
#define RT5663_MONO_OUT				0x0004
#define RT5663_MONO_GAIN			0x0007

#define RT5663_AEC_BST				0x000b
#define RT5663_IN1_IN2				0x000c
#define RT5663_IN3_IN4				0x000d
#define RT5663_INL1_INR1			0x000f
#define RT5663_CBJ_TYPE_2			0x0011
#define RT5663_CBJ_TYPE_3			0x0012
#define RT5663_CBJ_TYPE_4			0x0013
#define RT5663_CBJ_TYPE_5			0x0014
#define RT5663_CBJ_TYPE_8			0x0017

/* I/O - ADC/DAC/DMIC */
#define RT5663_DAC3_DIG_VOL			0x001a
#define RT5663_DAC3_CTRL			0x001b
#define RT5663_MONO_ADC_DIG_VOL			0x001d
#define RT5663_STO2_ADC_DIG_VOL			0x001e
#define RT5663_MONO_ADC_BST_GAIN		0x0020
#define RT5663_STO2_ADC_BST_GAIN		0x0021
#define RT5663_SIDETONE_CTRL			0x0024
/* Mixer - D-D */
#define RT5663_MONO1_ADC_MIXER			0x0027
#define RT5663_STO2_ADC_MIXER			0x0028
#define RT5663_MONO_DAC_MIXER			0x002b
#define RT5663_DAC2_SRC_CTRL			0x002e
#define RT5663_IF_3_4_DATA_CTL			0x002f
#define RT5663_IF_5_DATA_CTL			0x0030
#define RT5663_PDM_OUT_CTL			0x0031
#define RT5663_PDM_I2C_DATA_CTL1		0x0032
#define RT5663_PDM_I2C_DATA_CTL2		0x0033
#define RT5663_PDM_I2C_DATA_CTL3		0x0034
#define RT5663_PDM_I2C_DATA_CTL4		0x0035

/*Mixer - Analog*/
#define RT5663_RECMIX1_NEW			0x003a
#define RT5663_RECMIX1L_0			0x003b
#define RT5663_RECMIX1L				0x003c
#define RT5663_RECMIX1R_0			0x003d
#define RT5663_RECMIX1R				0x003e
#define RT5663_RECMIX2_NEW			0x003f
#define RT5663_RECMIX2_L_2			0x0041
#define RT5663_RECMIX2_R			0x0042
#define RT5663_RECMIX2_R_2			0x0043
#define RT5663_CALIB_REC_LR			0x0044
#define RT5663_ALC_BK_GAIN			0x0049
#define RT5663_MONOMIX_GAIN			0x004a
#define RT5663_MONOMIX_IN_GAIN			0x004b
#define RT5663_OUT_MIXL_GAIN			0x004d
#define RT5663_OUT_LMIX_IN_GAIN			0x004e
#define RT5663_OUT_RMIX_IN_GAIN			0x004f
#define RT5663_OUT_RMIX_IN_GAIN1		0x0050
#define RT5663_LOUT_MIXER_CTRL			0x0052
/* Power */
#define RT5663_PWR_VOL				0x0067

#define RT5663_ADCDAC_RST			0x006d
/* Format - ADC/DAC */
#define RT5663_I2S34_SDP			0x0071
#define RT5663_I2S5_SDP				0x0072

/* Function - Analog */
#define RT5663_ASRC_3				0x0085
#define RT5663_ASRC_6				0x0088
#define RT5663_ASRC_7				0x0089
#define RT5663_PLL_TRK_13			0x0099
#define RT5663_I2S_M_CLK_CTL			0x00a0
#define RT5663_FDIV_I2S34_M_CLK			0x00a1
#define RT5663_FDIV_I2S34_M_CLK2		0x00a2
#define RT5663_FDIV_I2S5_M_CLK			0x00a3
#define RT5663_FDIV_I2S5_M_CLK2			0x00a4

/* Function - Digital */
#define RT5663_V2_IRQ_4				0x00b9
#define RT5663_GPIO_3				0x00c2
#define RT5663_GPIO_4				0x00c3
#define RT5663_GPIO_STA2			0x00c4
#define RT5663_HP_AMP_DET1			0x00d0
#define RT5663_HP_AMP_DET2			0x00d1
#define RT5663_HP_AMP_DET3			0x00d2
#define RT5663_MID_BD_HP_AMP			0x00d3
#define RT5663_LOW_BD_HP_AMP			0x00d4
#define RT5663_SOF_VOL_ZC2			0x00da
#define RT5663_ADC_STO2_ADJ1			0x00ee
#define RT5663_ADC_STO2_ADJ2			0x00ef
/* General Control */
#define RT5663_A_JD_CTRL			0x00f0
#define RT5663_JD1_TRES_CTRL			0x00f1
#define RT5663_JD2_TRES_CTRL			0x00f2
#define RT5663_V2_JD_CTRL2			0x00f7
#define RT5663_DUM_REG_2			0x00fb
#define RT5663_DUM_REG_3			0x00fc


#define RT5663_DACADC_DIG_VOL2			0x0101
#define RT5663_DIG_IN_PIN2			0x0133
#define RT5663_PAD_DRV_CTL1			0x0136
#define RT5663_SOF_RAM_DEPOP			0x0138
#define RT5663_VOL_TEST				0x013f
#define RT5663_MONO_DYNA_1			0x0170
#define RT5663_MONO_DYNA_2			0x0171
#define RT5663_MONO_DYNA_3			0x0172
#define RT5663_MONO_DYNA_4			0x0173
#define RT5663_MONO_DYNA_5			0x0174
#define RT5663_MONO_DYNA_6			0x0175
#define RT5663_STO1_SIL_DET			0x0190
#define RT5663_MONOL_SIL_DET			0x0191
#define RT5663_MONOR_SIL_DET			0x0192
#define RT5663_STO2_DAC_SIL			0x0193
#define RT5663_PWR_SAV_CTL1			0x0194
#define RT5663_PWR_SAV_CTL2			0x0195
#define RT5663_PWR_SAV_CTL3			0x0196
#define RT5663_PWR_SAV_CTL4			0x0197
#define RT5663_PWR_SAV_CTL5			0x0198
#define RT5663_PWR_SAV_CTL6			0x0199
#define RT5663_MONO_AMP_CAL1			0x01a0
#define RT5663_MONO_AMP_CAL2			0x01a1
#define RT5663_MONO_AMP_CAL3			0x01a2
#define RT5663_MONO_AMP_CAL4			0x01a3
#define RT5663_MONO_AMP_CAL5			0x01a4
#define RT5663_MONO_AMP_CAL6			0x01a5
#define RT5663_MONO_AMP_CAL7			0x01a6
#define RT5663_MONO_AMP_CAL_ST1			0x01a7
#define RT5663_MONO_AMP_CAL_ST2			0x01a8
#define RT5663_MONO_AMP_CAL_ST3			0x01a9
#define RT5663_MONO_AMP_CAL_ST4			0x01aa
#define RT5663_MONO_AMP_CAL_ST5			0x01ab
#define RT5663_V2_HP_IMP_SEN_13			0x01b9
#define RT5663_V2_HP_IMP_SEN_14			0x01ba
#define RT5663_V2_HP_IMP_SEN_6			0x01bb
#define RT5663_V2_HP_IMP_SEN_7			0x01bc
#define RT5663_V2_HP_IMP_SEN_8			0x01bd
#define RT5663_V2_HP_IMP_SEN_9			0x01be
#define RT5663_V2_HP_IMP_SEN_10			0x01bf
#define RT5663_HP_LOGIC_3			0x01dc
#define RT5663_HP_CALIB_ST10			0x01f3
#define RT5663_HP_CALIB_ST11			0x01f4
#define RT5663_PRO_REG_TBL_4			0x0203
#define RT5663_PRO_REG_TBL_5			0x0204
#define RT5663_PRO_REG_TBL_6			0x0205
#define RT5663_PRO_REG_TBL_7			0x0206
#define RT5663_PRO_REG_TBL_8			0x0207
#define RT5663_PRO_REG_TBL_9			0x0208
#define RT5663_SAR_ADC_INL_1			0x0210
#define RT5663_SAR_ADC_INL_2			0x0211
#define RT5663_SAR_ADC_INL_3			0x0212
#define RT5663_SAR_ADC_INL_4			0x0213
#define RT5663_SAR_ADC_INL_5			0x0214
#define RT5663_SAR_ADC_INL_6			0x0215
#define RT5663_SAR_ADC_INL_7			0x0216
#define RT5663_SAR_ADC_INL_8			0x0217
#define RT5663_SAR_ADC_INL_9			0x0218
#define RT5663_SAR_ADC_INL_10			0x0219
#define RT5663_SAR_ADC_INL_11			0x021a
#define RT5663_SAR_ADC_INL_12			0x021b
#define RT5663_DRC_CTRL_1			0x02ff
#define RT5663_DRC1_CTRL_2			0x0301
#define RT5663_DRC1_CTRL_3			0x0302
#define RT5663_DRC1_CTRL_4			0x0303
#define RT5663_DRC1_CTRL_5			0x0304
#define RT5663_DRC1_CTRL_6			0x0305
#define RT5663_DRC1_HD_CTRL_1			0x0306
#define RT5663_DRC1_HD_CTRL_2			0x0307
#define RT5663_DRC1_PRI_REG_1			0x0310
#define RT5663_DRC1_PRI_REG_2			0x0311
#define RT5663_DRC1_PRI_REG_3			0x0312
#define RT5663_DRC1_PRI_REG_4			0x0313
#define RT5663_DRC1_PRI_REG_5			0x0314
#define RT5663_DRC1_PRI_REG_6			0x0315
#define RT5663_DRC1_PRI_REG_7			0x0316
#define RT5663_DRC1_PRI_REG_8			0x0317
#define RT5663_ALC_PGA_CTL_1			0x0330
#define RT5663_ALC_PGA_CTL_2			0x0331
#define RT5663_ALC_PGA_CTL_3			0x0332
#define RT5663_ALC_PGA_CTL_4			0x0333
#define RT5663_ALC_PGA_CTL_5			0x0334
#define RT5663_ALC_PGA_CTL_6			0x0335
#define RT5663_ALC_PGA_CTL_7			0x0336
#define RT5663_ALC_PGA_CTL_8			0x0337
#define RT5663_ALC_PGA_REG_1			0x0338
#define RT5663_ALC_PGA_REG_2			0x0339
#define RT5663_ALC_PGA_REG_3			0x033a
#define RT5663_ADC_EQ_RECOV_1			0x03c0
#define RT5663_ADC_EQ_RECOV_2			0x03c1
#define RT5663_ADC_EQ_RECOV_3			0x03c2
#define RT5663_ADC_EQ_RECOV_4			0x03c3
#define RT5663_ADC_EQ_RECOV_5			0x03c4
#define RT5663_ADC_EQ_RECOV_6			0x03c5
#define RT5663_ADC_EQ_RECOV_7			0x03c6
#define RT5663_ADC_EQ_RECOV_8			0x03c7
#define RT5663_ADC_EQ_RECOV_9			0x03c8
#define RT5663_ADC_EQ_RECOV_10			0x03c9
#define RT5663_ADC_EQ_RECOV_11			0x03ca
#define RT5663_ADC_EQ_RECOV_12			0x03cb
#define RT5663_ADC_EQ_RECOV_13			0x03cc
#define RT5663_VID_HIDDEN			0x03fe
#define RT5663_VID_CUSTOMER			0x03ff
#define RT5663_SCAN_MODE			0x07f0
#define RT5663_I2C_BYPA				0x07fa

/* Headphone Amp Control 2 (0x0003) */
#define RT5663_EN_DAC_HPO_MASK			(0x1 << 14)
#define RT5663_EN_DAC_HPO_SHIFT			14
#define RT5663_EN_DAC_HPO_DIS			(0x0 << 14)
#define RT5663_EN_DAC_HPO_EN			(0x1 << 14)

/*Headphone Amp L/R Analog Gain and Digital NG2 Gain Control (0x0005 0x0006)*/
#define RT5663_GAIN_HP				(0x1f << 8)
#define RT5663_GAIN_HP_SHIFT			8

/* AEC BST Control (0x000b) */
#define RT5663_GAIN_CBJ_MASK			(0xf << 8)
#define RT5663_GAIN_CBJ_SHIFT			8

/* IN1 Control / MIC GND REF (0x000c) */
#define RT5663_IN1_DF_MASK			(0x1 << 15)
#define RT5663_IN1_DF_SHIFT			15

/* Combo Jack and Type Detection Control 1 (0x0010) */
#define RT5663_CBJ_DET_MASK			(0x1 << 15)
#define RT5663_CBJ_DET_SHIFT			15
#define RT5663_CBJ_DET_DIS			(0x0 << 15)
#define RT5663_CBJ_DET_EN			(0x1 << 15)
#define RT5663_DET_TYPE_MASK			(0x1 << 12)
#define RT5663_DET_TYPE_SHIFT			12
#define RT5663_DET_TYPE_WLCSP			(0x0 << 12)
#define RT5663_DET_TYPE_QFN			(0x1 << 12)
#define RT5663_VREF_BIAS_MASK			(0x1 << 6)
#define RT5663_VREF_BIAS_SHIFT			6
#define RT5663_VREF_BIAS_FSM			(0x0 << 6)
#define RT5663_VREF_BIAS_REG			(0x1 << 6)

/* REC Left Mixer Control 2 (0x003c) */
#define RT5663_RECMIX1L_BST1_CBJ		(0x1 << 7)
#define RT5663_RECMIX1L_BST1_CBJ_SHIFT		7
#define RT5663_RECMIX1L_BST2			(0x1 << 4)
#define RT5663_RECMIX1L_BST2_SHIFT		4

/* REC Right Mixer Control 2 (0x003e) */
#define RT5663_RECMIX1R_BST2			(0x1 << 4)
#define RT5663_RECMIX1R_BST2_SHIFT		4

/* DAC1 Digital Volume (0x0019) */
#define RT5663_DAC_L1_VOL_MASK			(0xff << 8)
#define RT5663_DAC_L1_VOL_SHIFT			8
#define RT5663_DAC_R1_VOL_MASK			(0xff)
#define RT5663_DAC_R1_VOL_SHIFT			0

/* ADC Digital Volume Control (0x001c) */
#define RT5663_ADC_L_MUTE_MASK			(0x1 << 15)
#define RT5663_ADC_L_MUTE_SHIFT			15
#define RT5663_ADC_L_VOL_MASK			(0x7f << 8)
#define RT5663_ADC_L_VOL_SHIFT			8
#define RT5663_ADC_R_MUTE_MASK			(0x1 << 7)
#define RT5663_ADC_R_MUTE_SHIFT			7
#define RT5663_ADC_R_VOL_MASK			(0x7f)
#define RT5663_ADC_R_VOL_SHIFT			0

/* Stereo ADC Mixer Control (0x0026) */
#define RT5663_M_STO1_ADC_L1			(0x1 << 15)
#define RT5663_M_STO1_ADC_L1_SHIFT		15
#define RT5663_M_STO1_ADC_L2			(0x1 << 14)
#define RT5663_M_STO1_ADC_L2_SHIFT		14
#define RT5663_STO1_ADC_L1_SRC			(0x1 << 13)
#define RT5663_STO1_ADC_L1_SRC_SHIFT		13
#define RT5663_STO1_ADC_L2_SRC			(0x1 << 12)
#define RT5663_STO1_ADC_L2_SRC_SHIFT		12
#define RT5663_STO1_ADC_L_SRC			(0x3 << 10)
#define RT5663_STO1_ADC_L_SRC_SHIFT		10
#define RT5663_M_STO1_ADC_R1			(0x1 << 7)
#define RT5663_M_STO1_ADC_R1_SHIFT		7
#define RT5663_M_STO1_ADC_R2			(0x1 << 6)
#define RT5663_M_STO1_ADC_R2_SHIFT		6
#define RT5663_STO1_ADC_R1_SRC			(0x1 << 5)
#define RT5663_STO1_ADC_R1_SRC_SHIFT		5
#define RT5663_STO1_ADC_R2_SRC			(0x1 << 4)
#define RT5663_STO1_ADC_R2_SRC_SHIFT		4
#define RT5663_STO1_ADC_R_SRC			(0x3 << 2)
#define RT5663_STO1_ADC_R_SRC_SHIFT		2

/* ADC Mixer to DAC Mixer Control (0x0029) */
#define RT5663_M_ADCMIX_L			(0x1 << 15)
#define RT5663_M_ADCMIX_L_SHIFT			15
#define RT5663_M_DAC1_L				(0x1 << 14)
#define RT5663_M_DAC1_L_SHIFT			14
#define RT5663_M_ADCMIX_R			(0x1 << 7)
#define RT5663_M_ADCMIX_R_SHIFT			7
#define RT5663_M_DAC1_R				(0x1 << 6)
#define RT5663_M_DAC1_R_SHIFT			6

/* Stereo DAC Mixer Control (0x002a) */
#define RT5663_M_DAC_L1_STO_L			(0x1 << 15)
#define RT5663_M_DAC_L1_STO_L_SHIFT		15
#define RT5663_M_DAC_R1_STO_L			(0x1 << 13)
#define RT5663_M_DAC_R1_STO_L_SHIFT		13
#define RT5663_M_DAC_L1_STO_R			(0x1 << 7)
#define RT5663_M_DAC_L1_STO_R_SHIFT		7
#define RT5663_M_DAC_R1_STO_R			(0x1 << 5)
#define RT5663_M_DAC_R1_STO_R_SHIFT		5

/* Power Management for Digital 1 (0x0061) */
#define RT5663_PWR_I2S1				(0x1 << 15)
#define RT5663_PWR_I2S1_SHIFT			15
#define RT5663_PWR_DAC_L1			(0x1 << 11)
#define RT5663_PWR_DAC_L1_SHIFT			11
#define RT5663_PWR_DAC_R1			(0x1 << 10)
#define RT5663_PWR_DAC_R1_SHIFT			10
#define RT5663_PWR_LDO_DACREF_MASK		(0x1 << 8)
#define RT5663_PWR_LDO_DACREF_SHIFT		8
#define RT5663_PWR_LDO_DACREF_ON		(0x1 << 8)
#define RT5663_PWR_LDO_DACREF_DOWN		(0x0 << 8)
#define RT5663_PWR_LDO_SHIFT			8
#define RT5663_PWR_ADC_L1			(0x1 << 4)
#define RT5663_PWR_ADC_L1_SHIFT			4
#define RT5663_PWR_ADC_R1			(0x1 << 3)
#define RT5663_PWR_ADC_R1_SHIFT			3

/* Power Management for Digital 2 (0x0062) */
#define RT5663_PWR_ADC_S1F			(0x1 << 15)
#define RT5663_PWR_ADC_S1F_SHIFT		15
#define RT5663_PWR_DAC_S1F			(0x1 << 10)
#define RT5663_PWR_DAC_S1F_SHIFT		10

/* Power Management for Analog 1 (0x0063) */
#define RT5663_PWR_VREF1			(0x1 << 15)
#define RT5663_PWR_VREF1_MASK			(0x1 << 15)
#define RT5663_PWR_VREF1_SHIFT			15
#define RT5663_PWR_FV1				(0x1 << 14)
#define RT5663_PWR_FV1_MASK			(0x1 << 14)
#define RT5663_PWR_FV1_SHIFT			14
#define RT5663_PWR_VREF2			(0x1 << 13)
#define RT5663_PWR_VREF2_MASK			(0x1 << 13)
#define RT5663_PWR_VREF2_SHIFT			13
#define RT5663_PWR_FV2				(0x1 << 12)
#define RT5663_PWR_FV2_MASK			(0x1 << 12)
#define RT5663_PWR_FV2_SHIFT			12
#define RT5663_PWR_MB				(0x1 << 9)
#define RT5663_PWR_MB_MASK			(0x1 << 9)
#define RT5663_PWR_MB_SHIFT			9
#define RT5663_AMP_HP_MASK			(0x3 << 2)
#define RT5663_AMP_HP_SHIFT			2
#define RT5663_AMP_HP_1X			(0x0 << 2)
#define RT5663_AMP_HP_3X			(0x1 << 2)
#define RT5663_AMP_HP_5X			(0x3 << 2)
#define RT5663_LDO1_DVO_MASK			(0x3)
#define RT5663_LDO1_DVO_SHIFT			0
#define RT5663_LDO1_DVO_0_9V			(0x0)
#define RT5663_LDO1_DVO_1_0V			(0x1)
#define RT5663_LDO1_DVO_1_2V			(0x2)
#define RT5663_LDO1_DVO_1_4V			(0x3)

/* Power Management for Analog 2 (0x0064) */
#define RT5663_PWR_BST1				(0x1 << 15)
#define RT5663_PWR_BST1_MASK			(0x1 << 15)
#define RT5663_PWR_BST1_SHIFT			15
#define RT5663_PWR_BST1_OFF			(0x0 << 15)
#define RT5663_PWR_BST1_ON			(0x1 << 15)
#define RT5663_PWR_BST2				(0x1 << 14)
#define RT5663_PWR_BST2_MASK			(0x1 << 14)
#define RT5663_PWR_BST2_SHIFT			14
#define RT5663_PWR_MB1				(0x1 << 11)
#define RT5663_PWR_MB1_SHIFT			11
#define RT5663_PWR_MB2				(0x1 << 10)
#define RT5663_PWR_MB2_SHIFT			10
#define RT5663_PWR_BST2_OP			(0x1 << 6)
#define RT5663_PWR_BST2_OP_MASK			(0x1 << 6)
#define RT5663_PWR_BST2_OP_SHIFT		6
#define RT5663_PWR_JD1				(0x1 << 3)
#define RT5663_PWR_JD1_MASK			(0x1 << 3)
#define RT5663_PWR_JD1_SHIFT			3
#define RT5663_PWR_JD2				(0x1 << 2)
#define RT5663_PWR_JD2_MASK			(0x1 << 2)
#define RT5663_PWR_JD2_SHIFT			2
#define RT5663_PWR_RECMIX1			(0x1 << 1)
#define RT5663_PWR_RECMIX1_SHIFT		1
#define RT5663_PWR_RECMIX2			(0x1)
#define RT5663_PWR_RECMIX2_SHIFT		0

/* Power Management for Analog 3 (0x0065) */
#define RT5663_PWR_CBJ_MASK			(0x1 << 9)
#define RT5663_PWR_CBJ_SHIFT			9
#define RT5663_PWR_CBJ_OFF			(0x0 << 9)
#define RT5663_PWR_CBJ_ON			(0x1 << 9)
#define RT5663_PWR_PLL				(0x1 << 6)
#define RT5663_PWR_PLL_SHIFT			6
#define RT5663_PWR_LDO2				(0x1 << 2)
#define RT5663_PWR_LDO2_SHIFT			2

/* Power Management for Volume (0x0067) */
#define RT5663_V2_PWR_MIC_DET			(0x1 << 5)
#define RT5663_V2_PWR_MIC_DET_SHIFT		5

/* MCLK and System Clock Detection Control (0x006b) */
#define RT5663_EN_ANA_CLK_DET_MASK		(0x1 << 15)
#define RT5663_EN_ANA_CLK_DET_SHIFT		15
#define RT5663_EN_ANA_CLK_DET_DIS		(0x0 << 15)
#define RT5663_EN_ANA_CLK_DET_AUTO		(0x1 << 15)
#define RT5663_PWR_CLK_DET_MASK			(0x1)
#define RT5663_PWR_CLK_DET_SHIFT		0
#define RT5663_PWR_CLK_DET_DIS			(0x0)
#define RT5663_PWR_CLK_DET_EN			(0x1)

/* I2S1 Audio Serial Data Port Control (0x0070) */
#define RT5663_I2S_MS_MASK			(0x1 << 15)
#define RT5663_I2S_MS_SHIFT			15
#define RT5663_I2S_MS_M				(0x0 << 15)
#define RT5663_I2S_MS_S				(0x1 << 15)
#define RT5663_I2S_BP_MASK			(0x1 << 8)
#define RT5663_I2S_BP_SHIFT			8
#define RT5663_I2S_BP_NOR			(0x0 << 8)
#define RT5663_I2S_BP_INV			(0x1 << 8)
#define RT5663_I2S_DL_MASK			(0x3 << 4)
#define RT5663_I2S_DL_SHIFT			4
#define RT5663_I2S_DL_16			(0x0 << 4)
#define RT5663_I2S_DL_20			(0x1 << 4)
#define RT5663_I2S_DL_24			(0x2 << 4)
#define RT5663_I2S_DL_8				(0x3 << 4)
#define RT5663_I2S_DF_MASK			(0x7)
#define RT5663_I2S_DF_SHIFT			0
#define RT5663_I2S_DF_I2S			(0x0)
#define RT5663_I2S_DF_LEFT			(0x1)
#define RT5663_I2S_DF_PCM_A			(0x2)
#define RT5663_I2S_DF_PCM_B			(0x3)
#define RT5663_I2S_DF_PCM_A_N			(0x6)
#define RT5663_I2S_DF_PCM_B_N			(0x7)

/* ADC/DAC Clock Control 1 (0x0073) */
#define RT5663_I2S_PD1_MASK			(0x7 << 12)
#define RT5663_I2S_PD1_SHIFT			12
#define RT5663_M_I2S_DIV_MASK			(0x7 << 8)
#define RT5663_M_I2S_DIV_SHIFT			8
#define RT5663_CLK_SRC_MASK			(0x3 << 4)
#define RT5663_CLK_SRC_MCLK			(0x0 << 4)
#define RT5663_CLK_SRC_PLL_OUT			(0x1 << 4)
#define RT5663_CLK_SRC_DIV			(0x2 << 4)
#define RT5663_CLK_SRC_RC			(0x3 << 4)
#define RT5663_DAC_OSR_MASK			(0x3 << 2)
#define RT5663_DAC_OSR_SHIFT			2
#define RT5663_DAC_OSR_128			(0x0 << 2)
#define RT5663_DAC_OSR_64			(0x1 << 2)
#define RT5663_DAC_OSR_32			(0x2 << 2)
#define RT5663_ADC_OSR_MASK			(0x3)
#define RT5663_ADC_OSR_SHIFT			0
#define RT5663_ADC_OSR_128			(0x0)
#define RT5663_ADC_OSR_64			(0x1)
#define RT5663_ADC_OSR_32			(0x2)

/* TDM1 control 1 (0x0078) */
#define RT5663_TDM_MODE_MASK			(0x1 << 15)
#define RT5663_TDM_MODE_SHIFT			15
#define RT5663_TDM_MODE_I2S			(0x0 << 15)
#define RT5663_TDM_MODE_TDM			(0x1 << 15)
#define RT5663_TDM_IN_CH_MASK			(0x3 << 10)
#define RT5663_TDM_IN_CH_SHIFT			10
#define RT5663_TDM_IN_CH_2			(0x0 << 10)
#define RT5663_TDM_IN_CH_4			(0x1 << 10)
#define RT5663_TDM_IN_CH_6			(0x2 << 10)
#define RT5663_TDM_IN_CH_8			(0x3 << 10)
#define RT5663_TDM_OUT_CH_MASK			(0x3 << 8)
#define RT5663_TDM_OUT_CH_SHIFT			8
#define RT5663_TDM_OUT_CH_2			(0x0 << 8)
#define RT5663_TDM_OUT_CH_4			(0x1 << 8)
#define RT5663_TDM_OUT_CH_6			(0x2 << 8)
#define RT5663_TDM_OUT_CH_8			(0x3 << 8)
#define RT5663_TDM_IN_LEN_MASK			(0x3 << 6)
#define RT5663_TDM_IN_LEN_SHIFT			6
#define RT5663_TDM_IN_LEN_16			(0x0 << 6)
#define RT5663_TDM_IN_LEN_20			(0x1 << 6)
#define RT5663_TDM_IN_LEN_24			(0x2 << 6)
#define RT5663_TDM_IN_LEN_32			(0x3 << 6)
#define RT5663_TDM_OUT_LEN_MASK			(0x3 << 4)
#define RT5663_TDM_OUT_LEN_SHIFT		4
#define RT5663_TDM_OUT_LEN_16			(0x0 << 4)
#define RT5663_TDM_OUT_LEN_20			(0x1 << 4)
#define RT5663_TDM_OUT_LEN_24			(0x2 << 4)
#define RT5663_TDM_OUT_LEN_32			(0x3 << 4)

/* Global Clock Control (0x0080) */
#define RT5663_SCLK_SRC_MASK			(0x3 << 14)
#define RT5663_SCLK_SRC_SHIFT			14
#define RT5663_SCLK_SRC_MCLK			(0x0 << 14)
#define RT5663_SCLK_SRC_PLL1			(0x1 << 14)
#define RT5663_SCLK_SRC_RCCLK			(0x2 << 14)
#define RT5663_PLL1_SRC_MASK			(0x7 << 11)
#define RT5663_PLL1_SRC_SHIFT			11
#define RT5663_PLL1_SRC_MCLK			(0x0 << 11)
#define RT5663_PLL1_SRC_BCLK1			(0x1 << 11)
#define RT5663_V2_PLL1_SRC_MASK			(0x7 << 8)
#define RT5663_V2_PLL1_SRC_SHIFT		8
#define RT5663_V2_PLL1_SRC_MCLK			(0x0 << 8)
#define RT5663_V2_PLL1_SRC_BCLK1		(0x1 << 8)
#define RT5663_PLL1_PD_MASK			(0x1 << 4)
#define RT5663_PLL1_PD_SHIFT			4

#define RT5663_PLL_INP_MAX			40000000
#define RT5663_PLL_INP_MIN			256000
/* PLL M/N/K Code Control 1 (0x0081) */
#define RT5663_PLL_N_MAX			0x001ff
#define RT5663_PLL_N_MASK			(RT5663_PLL_N_MAX << 7)
#define RT5663_PLL_N_SHIFT			7
#define RT5663_PLL_K_MAX			0x001f
#define RT5663_PLL_K_MASK			(RT5663_PLL_K_MAX)
#define RT5663_PLL_K_SHIFT			0

/* PLL M/N/K Code Control 2 (0x0082) */
#define RT5663_PLL_M_MAX			0x00f
#define RT5663_PLL_M_MASK			(RT5663_PLL_M_MAX << 12)
#define RT5663_PLL_M_SHIFT			12
#define RT5663_PLL_M_BP				(0x1 << 11)
#define RT5663_PLL_M_BP_SHIFT			11

/* PLL tracking mode 1 (0x0083) */
#define RT5663_V2_I2S1_ASRC_MASK			(0x1 << 13)
#define RT5663_V2_I2S1_ASRC_SHIFT			13
#define RT5663_V2_DAC_STO1_ASRC_MASK		(0x1 << 12)
#define RT5663_V2_DAC_STO1_ASRC_SHIFT		12
#define RT5663_V2_ADC_STO1_ASRC_MASK		(0x1 << 4)
#define RT5663_V2_ADC_STO1_ASRC_SHIFT		4

/* PLL tracking mode 2 (0x0084)*/
#define RT5663_DA_STO1_TRACK_MASK		(0x7 << 12)
#define RT5663_DA_STO1_TRACK_SHIFT		12
#define RT5663_DA_STO1_TRACK_SYSCLK		(0x0 << 12)
#define RT5663_DA_STO1_TRACK_I2S1		(0x1 << 12)

/* PLL tracking mode 3 (0x0085)*/
#define RT5663_V2_AD_STO1_TRACK_MASK		(0x7 << 12)
#define RT5663_V2_AD_STO1_TRACK_SHIFT		12
#define RT5663_V2_AD_STO1_TRACK_SYSCLK		(0x0 << 12)
#define RT5663_V2_AD_STO1_TRACK_I2S1		(0x1 << 12)

/* HPOUT Charge pump control 1 (0x0091) */
#define RT5663_OSW_HP_L_MASK			(0x1 << 11)
#define RT5663_OSW_HP_L_SHIFT			11
#define RT5663_OSW_HP_L_EN			(0x1 << 11)
#define RT5663_OSW_HP_L_DIS			(0x0 << 11)
#define RT5663_OSW_HP_R_MASK			(0x1 << 10)
#define RT5663_OSW_HP_R_SHIFT			10
#define RT5663_OSW_HP_R_EN			(0x1 << 10)
#define RT5663_OSW_HP_R_DIS			(0x0 << 10)
#define RT5663_SEL_PM_HP_MASK			(0x3 << 8)
#define RT5663_SEL_PM_HP_SHIFT			8
#define RT5663_SEL_PM_HP_0_6			(0x0 << 8)
#define RT5663_SEL_PM_HP_0_9			(0x1 << 8)
#define RT5663_SEL_PM_HP_1_8			(0x2 << 8)
#define RT5663_SEL_PM_HP_HIGH			(0x3 << 8)
#define RT5663_OVCD_HP_MASK			(0x1 << 2)
#define RT5663_OVCD_HP_SHIFT			2
#define RT5663_OVCD_HP_EN			(0x1 << 2)
#define RT5663_OVCD_HP_DIS			(0x0 << 2)

/* RC Clock Control (0x0094) */
#define RT5663_DIG_25M_CLK_MASK			(0x1 << 9)
#define RT5663_DIG_25M_CLK_SHIFT		9
#define RT5663_DIG_25M_CLK_DIS			(0x0 << 9)
#define RT5663_DIG_25M_CLK_EN			(0x1 << 9)
#define RT5663_DIG_1M_CLK_MASK			(0x1 << 8)
#define RT5663_DIG_1M_CLK_SHIFT			8
#define RT5663_DIG_1M_CLK_DIS			(0x0 << 8)
#define RT5663_DIG_1M_CLK_EN			(0x1 << 8)

/* Auto Turn On 1M RC CLK (0x009f) */
#define RT5663_IRQ_POW_SAV_MASK			(0x1 << 15)
#define RT5663_IRQ_POW_SAV_SHIFT		15
#define RT5663_IRQ_POW_SAV_DIS			(0x0 << 15)
#define RT5663_IRQ_POW_SAV_EN			(0x1 << 15)
#define RT5663_IRQ_POW_SAV_JD1_MASK		(0x1 << 14)
#define RT5663_IRQ_POW_SAV_JD1_SHIFT		14
#define RT5663_IRQ_POW_SAV_JD1_DIS		(0x0 << 14)
#define RT5663_IRQ_POW_SAV_JD1_EN		(0x1 << 14)
#define RT5663_IRQ_MANUAL_MASK			(0x1 << 8)
#define RT5663_IRQ_MANUAL_SHIFT			8
#define RT5663_IRQ_MANUAL_DIS			(0x0 << 8)
#define RT5663_IRQ_MANUAL_EN			(0x1 << 8)

/* IRQ Control 1 (0x00b6) */
#define RT5663_EN_CB_JD_MASK			(0x1 << 3)
#define RT5663_EN_CB_JD_SHIFT			3
#define RT5663_EN_CB_JD_EN			(0x1 << 3)
#define RT5663_EN_CB_JD_DIS			(0x0 << 3)

/* IRQ Control 3 (0x00b8) */
#define RT5663_V2_EN_IRQ_INLINE_MASK		(0x1 << 6)
#define RT5663_V2_EN_IRQ_INLINE_SHIFT		6
#define RT5663_V2_EN_IRQ_INLINE_BYP		(0x0 << 6)
#define RT5663_V2_EN_IRQ_INLINE_NOR		(0x1 << 6)

/* GPIO Control 1 (0x00c0) */
#define RT5663_GP1_PIN_MASK			(0x1 << 15)
#define RT5663_GP1_PIN_SHIFT			15
#define RT5663_GP1_PIN_GPIO1			(0x0 << 15)
#define RT5663_GP1_PIN_IRQ			(0x1 << 15)

/* GPIO Control 2 (0x00c1) */
#define RT5663_GP4_PIN_CONF_MASK		(0x1 << 5)
#define RT5663_GP4_PIN_CONF_SHIFT		5
#define RT5663_GP4_PIN_CONF_INPUT		(0x0 << 5)
#define RT5663_GP4_PIN_CONF_OUTPUT		(0x1 << 5)

/* GPIO Control 2 (0x00c2) */
#define RT5663_GP8_PIN_CONF_MASK		(0x1 << 13)
#define RT5663_GP8_PIN_CONF_SHIFT		13
#define RT5663_GP8_PIN_CONF_INPUT		(0x0 << 13)
#define RT5663_GP8_PIN_CONF_OUTPUT		(0x1 << 13)

/* 4 Buttons Inline Command Function 1 (0x00df) */
#define RT5663_4BTN_CLK_DEB_MASK		(0x3 << 2)
#define RT5663_4BTN_CLK_DEB_SHIFT		2
#define RT5663_4BTN_CLK_DEB_8MS			(0x0 << 2)
#define RT5663_4BTN_CLK_DEB_16MS		(0x1 << 2)
#define RT5663_4BTN_CLK_DEB_32MS		(0x2 << 2)
#define RT5663_4BTN_CLK_DEB_65MS		(0x3 << 2)

/* Inline Command Function 6 (0x00e0) */
#define RT5663_EN_4BTN_INL_MASK			(0x1 << 15)
#define RT5663_EN_4BTN_INL_SHIFT		15
#define RT5663_EN_4BTN_INL_DIS			(0x0 << 15)
#define RT5663_EN_4BTN_INL_EN			(0x1 << 15)
#define RT5663_RESET_4BTN_INL_MASK		(0x1 << 14)
#define RT5663_RESET_4BTN_INL_SHIFT		14
#define RT5663_RESET_4BTN_INL_RESET		(0x0 << 14)
#define RT5663_RESET_4BTN_INL_NOR		(0x1 << 14)

/* Digital Misc Control (0x00fa) */
#define RT5663_DIG_GATE_CTRL_MASK		0x1
#define RT5663_DIG_GATE_CTRL_SHIFT		(0)
#define RT5663_DIG_GATE_CTRL_DIS		0x0
#define RT5663_DIG_GATE_CTRL_EN			0x1

/* Chopper and Clock control for DAC L (0x013a)*/
#define RT5663_CKXEN_DAC1_MASK			(0x1 << 13)
#define RT5663_CKXEN_DAC1_SHIFT			13
#define RT5663_CKGEN_DAC1_MASK			(0x1 << 12)
#define RT5663_CKGEN_DAC1_SHIFT			12

/* Chopper and Clock control for ADC (0x013b)*/
#define RT5663_CKXEN_ADCC_MASK			(0x1 << 13)
#define RT5663_CKXEN_ADCC_SHIFT			13
#define RT5663_CKGEN_ADCC_MASK			(0x1 << 12)
#define RT5663_CKGEN_ADCC_SHIFT			12

/* HP Behavior Logic Control 2 (0x01db) */
#define RT5663_HP_SIG_SRC1_MASK			(0x3)
#define RT5663_HP_SIG_SRC1_SHIFT		0
#define RT5663_HP_SIG_SRC1_HP_DC		(0x0)
#define RT5663_HP_SIG_SRC1_HP_CALIB		(0x1)
#define RT5663_HP_SIG_SRC1_REG			(0x2)
#define RT5663_HP_SIG_SRC1_SILENCE		(0x3)

/* RT5663 specific register */
#define RT5663_HP_OUT_EN			0x0002
#define RT5663_HP_LCH_DRE			0x0005
#define RT5663_HP_RCH_DRE			0x0006
#define RT5663_CALIB_BST			0x000a
#define RT5663_RECMIX				0x0010
#define RT5663_SIL_DET_CTL			0x0015
#define RT5663_PWR_SAV_SILDET			0x0016
#define RT5663_SIDETONE_CTL			0x0018
#define RT5663_STO1_DAC_DIG_VOL			0x0019
#define RT5663_STO1_ADC_DIG_VOL			0x001c
#define RT5663_STO1_BOOST			0x001f
#define RT5663_HP_IMP_GAIN_1			0x0022
#define RT5663_HP_IMP_GAIN_2			0x0023
#define RT5663_STO1_ADC_MIXER			0x0026
#define RT5663_AD_DA_MIXER			0x0029
#define RT5663_STO_DAC_MIXER			0x002a
#define RT5663_DIG_SIDE_MIXER			0x002c
#define RT5663_BYPASS_STO_DAC			0x002d
#define RT5663_CALIB_REC_MIX			0x0040
#define RT5663_PWR_DIG_1			0x0061
#define RT5663_PWR_DIG_2			0x0062
#define RT5663_PWR_ANLG_1			0x0063
#define RT5663_PWR_ANLG_2			0x0064
#define RT5663_PWR_ANLG_3			0x0065
#define RT5663_PWR_MIXER			0x0066
#define RT5663_SIG_CLK_DET			0x006b
#define RT5663_PRE_DIV_GATING_1			0x006e
#define RT5663_PRE_DIV_GATING_2			0x006f
#define RT5663_I2S1_SDP				0x0070
#define RT5663_ADDA_CLK_1			0x0073
#define RT5663_ADDA_RST				0x0074
#define RT5663_FRAC_DIV_1			0x0075
#define RT5663_FRAC_DIV_2			0x0076
#define RT5663_TDM_1				0x0077
#define RT5663_TDM_2				0x0078
#define RT5663_TDM_3				0x0079
#define RT5663_TDM_4				0x007a
#define RT5663_TDM_5				0x007b
#define RT5663_TDM_6				0x007c
#define RT5663_TDM_7				0x007d
#define RT5663_TDM_8				0x007e
#define RT5663_TDM_9				0x007f
#define RT5663_GLB_CLK				0x0080
#define RT5663_PLL_1				0x0081
#define RT5663_PLL_2				0x0082
#define RT5663_ASRC_1				0x0083
#define RT5663_ASRC_2				0x0084
#define RT5663_ASRC_4				0x0086
#define RT5663_DUMMY_REG			0x0087
#define RT5663_ASRC_8				0x008a
#define RT5663_ASRC_9				0x008b
#define RT5663_ASRC_11				0x008c
#define RT5663_DEPOP_1				0x008e
#define RT5663_DEPOP_2				0x008f
#define RT5663_DEPOP_3				0x0090
#define RT5663_HP_CHARGE_PUMP_1			0x0091
#define RT5663_HP_CHARGE_PUMP_2			0x0092
#define RT5663_MICBIAS_1			0x0093
#define RT5663_RC_CLK				0x0094
#define RT5663_ASRC_11_2			0x0097
#define RT5663_DUMMY_REG_2			0x0098
#define RT5663_REC_PATH_GAIN			0x009a
#define RT5663_AUTO_1MRC_CLK			0x009f
#define RT5663_ADC_EQ_1				0x00ae
#define RT5663_ADC_EQ_2				0x00af
#define RT5663_IRQ_1				0x00b6
#define RT5663_IRQ_2				0x00b7
#define RT5663_IRQ_3				0x00b8
#define RT5663_IRQ_4				0x00ba
#define RT5663_IRQ_5				0x00bb
#define RT5663_INT_ST_1				0x00be
#define RT5663_INT_ST_2				0x00bf
#define RT5663_GPIO_1				0x00c0
#define RT5663_GPIO_2				0x00c1
#define RT5663_GPIO_STA1			0x00c5
#define RT5663_SIN_GEN_1			0x00cb
#define RT5663_SIN_GEN_2			0x00cc
#define RT5663_SIN_GEN_3			0x00cd
#define RT5663_SOF_VOL_ZC1			0x00d9
#define RT5663_IL_CMD_1				0x00db
#define RT5663_IL_CMD_2				0x00dc
#define RT5663_IL_CMD_3				0x00dd
#define RT5663_IL_CMD_4				0x00de
#define RT5663_IL_CMD_5				0x00df
#define RT5663_IL_CMD_6				0x00e0
#define RT5663_IL_CMD_7				0x00e1
#define RT5663_IL_CMD_8				0x00e2
#define RT5663_IL_CMD_PWRSAV1			0x00e4
#define RT5663_IL_CMD_PWRSAV2			0x00e5
#define RT5663_EM_JACK_TYPE_1			0x00e6
#define RT5663_EM_JACK_TYPE_2			0x00e7
#define RT5663_EM_JACK_TYPE_3			0x00e8
#define RT5663_EM_JACK_TYPE_4			0x00e9
#define RT5663_EM_JACK_TYPE_5			0x00ea
#define RT5663_EM_JACK_TYPE_6			0x00eb
#define RT5663_STO1_HPF_ADJ1			0x00ec
#define RT5663_STO1_HPF_ADJ2			0x00ed
#define RT5663_FAST_OFF_MICBIAS			0x00f4
#define RT5663_JD_CTRL1				0x00f6
#define RT5663_JD_CTRL2				0x00f8
#define RT5663_DIG_MISC				0x00fa
#define RT5663_DIG_VOL_ZCD			0x0100
#define RT5663_ANA_BIAS_CUR_1			0x0108
#define RT5663_ANA_BIAS_CUR_2			0x0109
#define RT5663_ANA_BIAS_CUR_3			0x010a
#define RT5663_ANA_BIAS_CUR_4			0x010b
#define RT5663_ANA_BIAS_CUR_5			0x010c
#define RT5663_ANA_BIAS_CUR_6			0x010d
#define RT5663_BIAS_CUR_5			0x010e
#define RT5663_BIAS_CUR_6			0x010f
#define RT5663_BIAS_CUR_7			0x0110
#define RT5663_BIAS_CUR_8			0x0111
#define RT5663_DACREF_LDO			0x0112
#define RT5663_DUMMY_REG_3			0x0113
#define RT5663_BIAS_CUR_9			0x0114
#define RT5663_DUMMY_REG_4			0x0116
#define RT5663_VREFADJ_OP			0x0117
#define RT5663_VREF_RECMIX			0x0118
#define RT5663_CHARGE_PUMP_1			0x0125
#define RT5663_CHARGE_PUMP_1_2			0x0126
#define RT5663_CHARGE_PUMP_1_3			0x0127
#define RT5663_CHARGE_PUMP_2			0x0128
#define RT5663_DIG_IN_PIN1			0x0132
#define RT5663_PAD_DRV_CTL			0x0137
#define RT5663_PLL_INT_REG			0x0139
#define RT5663_CHOP_DAC_L			0x013a
#define RT5663_CHOP_ADC				0x013b
#define RT5663_CALIB_ADC			0x013c
#define RT5663_CHOP_DAC_R			0x013d
#define RT5663_DUMMY_CTL_DACLR			0x013e
#define RT5663_DUMMY_REG_5			0x0140
#define RT5663_SOFT_RAMP			0x0141
#define RT5663_TEST_MODE_1			0x0144
#define RT5663_TEST_MODE_2			0x0145
#define RT5663_TEST_MODE_3			0x0146
#define RT5663_TEST_MODE_4			0x0147
#define RT5663_TEST_MODE_5			0x0148
#define RT5663_STO_DRE_1			0x0160
#define RT5663_STO_DRE_2			0x0161
#define RT5663_STO_DRE_3			0x0162
#define RT5663_STO_DRE_4			0x0163
#define RT5663_STO_DRE_5			0x0164
#define RT5663_STO_DRE_6			0x0165
#define RT5663_STO_DRE_7			0x0166
#define RT5663_STO_DRE_8			0x0167
#define RT5663_STO_DRE_9			0x0168
#define RT5663_STO_DRE_10			0x0169
#define RT5663_MIC_DECRO_1			0x0180
#define RT5663_MIC_DECRO_2			0x0181
#define RT5663_MIC_DECRO_3			0x0182
#define RT5663_MIC_DECRO_4			0x0183
#define RT5663_MIC_DECRO_5			0x0184
#define RT5663_MIC_DECRO_6			0x0185
#define RT5663_HP_DECRO_1			0x01b0
#define RT5663_HP_DECRO_2			0x01b1
#define RT5663_HP_DECRO_3			0x01b2
#define RT5663_HP_DECRO_4			0x01b3
#define RT5663_HP_DECOUP			0x01b4
#define RT5663_HP_IMP_SEN_MAP8			0x01b5
#define RT5663_HP_IMP_SEN_MAP9			0x01b6
#define RT5663_HP_IMP_SEN_MAP10			0x01b7
#define RT5663_HP_IMP_SEN_MAP11			0x01b8
#define RT5663_HP_IMP_SEN_1			0x01c0
#define RT5663_HP_IMP_SEN_2			0x01c1
#define RT5663_HP_IMP_SEN_3			0x01c2
#define RT5663_HP_IMP_SEN_4			0x01c3
#define RT5663_HP_IMP_SEN_5			0x01c4
#define RT5663_HP_IMP_SEN_6			0x01c5
#define RT5663_HP_IMP_SEN_7			0x01c6
#define RT5663_HP_IMP_SEN_8			0x01c7
#define RT5663_HP_IMP_SEN_9			0x01c8
#define RT5663_HP_IMP_SEN_10			0x01c9
#define RT5663_HP_IMP_SEN_11			0x01ca
#define RT5663_HP_IMP_SEN_12			0x01cb
#define RT5663_HP_IMP_SEN_13			0x01cc
#define RT5663_HP_IMP_SEN_14			0x01cd
#define RT5663_HP_IMP_SEN_15			0x01ce
#define RT5663_HP_IMP_SEN_16			0x01cf
#define RT5663_HP_IMP_SEN_17			0x01d0
#define RT5663_HP_IMP_SEN_18			0x01d1
#define RT5663_HP_IMP_SEN_19			0x01d2
#define RT5663_HP_IMPSEN_DIG5			0x01d3
#define RT5663_HP_IMPSEN_MAP1			0x01d4
#define RT5663_HP_IMPSEN_MAP2			0x01d5
#define RT5663_HP_IMPSEN_MAP3			0x01d6
#define RT5663_HP_IMPSEN_MAP4			0x01d7
#define RT5663_HP_IMPSEN_MAP5			0x01d8
#define RT5663_HP_IMPSEN_MAP7			0x01d9
#define RT5663_HP_LOGIC_1			0x01da
#define RT5663_HP_LOGIC_2			0x01db
#define RT5663_HP_CALIB_1			0x01dd
#define RT5663_HP_CALIB_1_1			0x01de
#define RT5663_HP_CALIB_2			0x01df
#define RT5663_HP_CALIB_3			0x01e0
#define RT5663_HP_CALIB_4			0x01e1
#define RT5663_HP_CALIB_5			0x01e2
#define RT5663_HP_CALIB_5_1			0x01e3
#define RT5663_HP_CALIB_6			0x01e4
#define RT5663_HP_CALIB_7			0x01e5
#define RT5663_HP_CALIB_9			0x01e6
#define RT5663_HP_CALIB_10			0x01e7
#define RT5663_HP_CALIB_11			0x01e8
#define RT5663_HP_CALIB_ST1			0x01ea
#define RT5663_HP_CALIB_ST2			0x01eb
#define RT5663_HP_CALIB_ST3			0x01ec
#define RT5663_HP_CALIB_ST4			0x01ed
#define RT5663_HP_CALIB_ST5			0x01ee
#define RT5663_HP_CALIB_ST6			0x01ef
#define RT5663_HP_CALIB_ST7			0x01f0
#define RT5663_HP_CALIB_ST8			0x01f1
#define RT5663_HP_CALIB_ST9			0x01f2
#define RT5663_HP_AMP_DET			0x0200
#define RT5663_DUMMY_REG_6			0x0201
#define RT5663_HP_BIAS				0x0202
#define RT5663_CBJ_1				0x0250
#define RT5663_CBJ_2				0x0251
#define RT5663_CBJ_3				0x0252
#define RT5663_DUMMY_1				0x02fa
#define RT5663_DUMMY_2				0x02fb
#define RT5663_DUMMY_3				0x02fc
#define RT5663_ANA_JD				0x0300
#define RT5663_ADC_LCH_LPF1_A1			0x03d0
#define RT5663_ADC_RCH_LPF1_A1			0x03d1
#define RT5663_ADC_LCH_LPF1_H0			0x03d2
#define RT5663_ADC_RCH_LPF1_H0			0x03d3
#define RT5663_ADC_LCH_BPF1_A1			0x03d4
#define RT5663_ADC_RCH_BPF1_A1			0x03d5
#define RT5663_ADC_LCH_BPF1_A2			0x03d6
#define RT5663_ADC_RCH_BPF1_A2			0x03d7
#define RT5663_ADC_LCH_BPF1_H0			0x03d8
#define RT5663_ADC_RCH_BPF1_H0			0x03d9
#define RT5663_ADC_LCH_BPF2_A1			0x03da
#define RT5663_ADC_RCH_BPF2_A1			0x03db
#define RT5663_ADC_LCH_BPF2_A2			0x03dc
#define RT5663_ADC_RCH_BPF2_A2			0x03dd
#define RT5663_ADC_LCH_BPF2_H0			0x03de
#define RT5663_ADC_RCH_BPF2_H0			0x03df
#define RT5663_ADC_LCH_BPF3_A1			0x03e0
#define RT5663_ADC_RCH_BPF3_A1			0x03e1
#define RT5663_ADC_LCH_BPF3_A2			0x03e2
#define RT5663_ADC_RCH_BPF3_A2			0x03e3
#define RT5663_ADC_LCH_BPF3_H0			0x03e4
#define RT5663_ADC_RCH_BPF3_H0			0x03e5
#define RT5663_ADC_LCH_BPF4_A1			0x03e6
#define RT5663_ADC_RCH_BPF4_A1			0x03e7
#define RT5663_ADC_LCH_BPF4_A2			0x03e8
#define RT5663_ADC_RCH_BPF4_A2			0x03e9
#define RT5663_ADC_LCH_BPF4_H0			0x03ea
#define RT5663_ADC_RCH_BPF4_H0			0x03eb
#define RT5663_ADC_LCH_HPF1_A1			0x03ec
#define RT5663_ADC_RCH_HPF1_A1			0x03ed
#define RT5663_ADC_LCH_HPF1_H0			0x03ee
#define RT5663_ADC_RCH_HPF1_H0			0x03ef
#define RT5663_ADC_EQ_PRE_VOL_L			0x03f0
#define RT5663_ADC_EQ_PRE_VOL_R			0x03f1
#define RT5663_ADC_EQ_POST_VOL_L		0x03f2
#define RT5663_ADC_EQ_POST_VOL_R		0x03f3

/* RECMIX Control (0x0010) */
#define RT5663_RECMIX1_BST1_MASK		(0x1)
#define RT5663_RECMIX1_BST1_SHIFT		0
#define RT5663_RECMIX1_BST1_ON			(0x0)
#define RT5663_RECMIX1_BST1_OFF			(0x1)

/* Bypass Stereo1 DAC Mixer Control (0x002d) */
#define RT5663_DACL1_SRC_MASK			(0x1 << 3)
#define RT5663_DACL1_SRC_SHIFT			3
#define RT5663_DACR1_SRC_MASK			(0x1 << 2)
#define RT5663_DACR1_SRC_SHIFT			2

/* TDM control 2 (0x0078) */
#define RT5663_DATA_SWAP_ADCDAT1_MASK		(0x3 << 14)
#define RT5663_DATA_SWAP_ADCDAT1_SHIFT		14
#define RT5663_DATA_SWAP_ADCDAT1_LR		(0x0 << 14)
#define RT5663_DATA_SWAP_ADCDAT1_RL		(0x1 << 14)
#define RT5663_DATA_SWAP_ADCDAT1_LL		(0x2 << 14)
#define RT5663_DATA_SWAP_ADCDAT1_RR		(0x3 << 14)

/* TDM control 5 (0x007b) */
#define RT5663_TDM_LENGTN_MASK			(0x3)
#define RT5663_TDM_LENGTN_SHIFT			0
#define RT5663_TDM_LENGTN_16			(0x0)
#define RT5663_TDM_LENGTN_20			(0x1)
#define RT5663_TDM_LENGTN_24			(0x2)
#define RT5663_TDM_LENGTN_32			(0x3)

/* PLL tracking mode 1 (0x0083) */
#define RT5663_I2S1_ASRC_MASK			(0x1 << 11)
#define RT5663_I2S1_ASRC_SHIFT			11
#define RT5663_DAC_STO1_ASRC_MASK		(0x1 << 10)
#define RT5663_DAC_STO1_ASRC_SHIFT		10
#define RT5663_ADC_STO1_ASRC_MASK		(0x1 << 3)
#define RT5663_ADC_STO1_ASRC_SHIFT		3

/* PLL tracking mode 2 (0x0084)*/
#define RT5663_DA_STO1_TRACK_MASK		(0x7 << 12)
#define RT5663_DA_STO1_TRACK_SHIFT		12
#define RT5663_DA_STO1_TRACK_SYSCLK		(0x0 << 12)
#define RT5663_DA_STO1_TRACK_I2S1		(0x1 << 12)
#define RT5663_AD_STO1_TRACK_MASK		(0x7)
#define RT5663_AD_STO1_TRACK_SHIFT		0
#define RT5663_AD_STO1_TRACK_SYSCLK		(0x0)
#define RT5663_AD_STO1_TRACK_I2S1		(0x1)

/* HPOUT Charge pump control 1 (0x0091) */
#define RT5663_SI_HP_MASK			(0x1 << 12)
#define RT5663_SI_HP_SHIFT			12
#define RT5663_SI_HP_EN				(0x1 << 12)
#define RT5663_SI_HP_DIS			(0x0 << 12)

/* GPIO Control 2 (0x00b6) */
#define RT5663_GP1_PIN_CONF_MASK		(0x1 << 2)
#define RT5663_GP1_PIN_CONF_SHIFT		2
#define RT5663_GP1_PIN_CONF_OUTPUT		(0x1 << 2)
#define RT5663_GP1_PIN_CONF_INPUT		(0x0 << 2)

/* GPIO Control 2 (0x00b7) */
#define RT5663_EN_IRQ_INLINE_MASK		(0x1 << 3)
#define RT5663_EN_IRQ_INLINE_SHIFT		3
#define RT5663_EN_IRQ_INLINE_NOR		(0x1 << 3)
#define RT5663_EN_IRQ_INLINE_BYP		(0x0 << 3)

/* GPIO Control 1 (0x00c0) */
#define RT5663_GPIO1_TYPE_MASK			(0x1 << 15)
#define RT5663_GPIO1_TYPE_SHIFT			15
#define RT5663_GPIO1_TYPE_EN			(0x1 << 15)
#define RT5663_GPIO1_TYPE_DIS			(0x0 << 15)

/* IRQ Control 1 (0x00c1) */
#define RT5663_EN_IRQ_JD1_MASK			(0x1 << 6)
#define RT5663_EN_IRQ_JD1_SHIFT			6
#define RT5663_EN_IRQ_JD1_EN			(0x1 << 6)
#define RT5663_EN_IRQ_JD1_DIS			(0x0 << 6)
#define RT5663_SEL_GPIO1_MASK			(0x1 << 2)
#define RT5663_SEL_GPIO1_SHIFT			6
#define RT5663_SEL_GPIO1_EN			(0x1 << 2)
#define RT5663_SEL_GPIO1_DIS			(0x0 << 2)

/* Inline Command Function 2 (0x00dc) */
#define RT5663_PWR_MIC_DET_MASK			(0x1)
#define RT5663_PWR_MIC_DET_SHIFT		0
#define RT5663_PWR_MIC_DET_ON			(0x1)
#define RT5663_PWR_MIC_DET_OFF			(0x0)

/* Embeeded Jack and Type Detection Control 1 (0x00e6)*/
#define RT5663_CBJ_DET_MASK			(0x1 << 15)
#define RT5663_CBJ_DET_SHIFT			15
#define RT5663_CBJ_DET_DIS			(0x0 << 15)
#define RT5663_CBJ_DET_EN			(0x1 << 15)
#define RT5663_EXT_JD_MASK			(0x1 << 11)
#define RT5663_EXT_JD_SHIFT			11
#define RT5663_EXT_JD_EN			(0x1 << 11)
#define RT5663_EXT_JD_DIS			(0x0 << 11)
#define RT5663_POL_EXT_JD_MASK			(0x1 << 10)
#define RT5663_POL_EXT_JD_SHIFT			10
#define RT5663_POL_EXT_JD_EN			(0x1 << 10)
#define RT5663_POL_EXT_JD_DIS			(0x0 << 10)

/* DACREF LDO Control (0x0112)*/
#define RT5663_PWR_LDO_DACREFL_MASK		(0x1 << 9)
#define RT5663_PWR_LDO_DACREFL_SHIFT		9
#define RT5663_PWR_LDO_DACREFR_MASK		(0x1 << 1)
#define RT5663_PWR_LDO_DACREFR_SHIFT		1

/* Stereo Dynamic Range Enhancement Control 9 (0x0168, 0x0169)*/
#define RT5663_DRE_GAIN_HP_MASK			(0x1f)
#define RT5663_DRE_GAIN_HP_SHIFT		0

/* Combo Jack Control (0x0250) */
#define RT5663_INBUF_CBJ_BST1_MASK		(0x1 << 11)
#define RT5663_INBUF_CBJ_BST1_SHIFT		11
#define RT5663_INBUF_CBJ_BST1_ON		(0x1 << 11)
#define RT5663_INBUF_CBJ_BST1_OFF		(0x0 << 11)
#define RT5663_CBJ_SENSE_BST1_MASK		(0x1 << 10)
#define RT5663_CBJ_SENSE_BST1_SHIFT		10
#define RT5663_CBJ_SENSE_BST1_L			(0x1 << 10)
#define RT5663_CBJ_SENSE_BST1_R			(0x0 << 10)

/* Combo Jack Control (0x0251) */
#define RT5663_GAIN_BST1_MASK			(0xf)
#define RT5663_GAIN_BST1_SHIFT			0

/* Dummy register 1 (0x02fa) */
#define RT5663_EMB_CLK_MASK			(0x1 << 9)
#define RT5663_EMB_CLK_SHIFT			9
#define RT5663_EMB_CLK_EN			(0x1 << 9)
#define RT5663_EMB_CLK_DIS			(0x0 << 9)
#define RT5663_HPA_CPL_BIAS_MASK		(0x7 << 6)
#define RT5663_HPA_CPL_BIAS_SHIFT		6
#define RT5663_HPA_CPL_BIAS_0_5			(0x0 << 6)
#define RT5663_HPA_CPL_BIAS_1			(0x1 << 6)
#define RT5663_HPA_CPL_BIAS_2			(0x2 << 6)
#define RT5663_HPA_CPL_BIAS_3			(0x3 << 6)
#define RT5663_HPA_CPL_BIAS_4_1			(0x4 << 6)
#define RT5663_HPA_CPL_BIAS_4_2			(0x5 << 6)
#define RT5663_HPA_CPL_BIAS_6			(0x6 << 6)
#define RT5663_HPA_CPL_BIAS_8			(0x7 << 6)
#define RT5663_HPA_CPR_BIAS_MASK		(0x7 << 3)
#define RT5663_HPA_CPR_BIAS_SHIFT		3
#define RT5663_HPA_CPR_BIAS_0_5			(0x0 << 3)
#define RT5663_HPA_CPR_BIAS_1			(0x1 << 3)
#define RT5663_HPA_CPR_BIAS_2			(0x2 << 3)
#define RT5663_HPA_CPR_BIAS_3			(0x3 << 3)
#define RT5663_HPA_CPR_BIAS_4_1			(0x4 << 3)
#define RT5663_HPA_CPR_BIAS_4_2			(0x5 << 3)
#define RT5663_HPA_CPR_BIAS_6			(0x6 << 3)
#define RT5663_HPA_CPR_BIAS_8			(0x7 << 3)
#define RT5663_DUMMY_BIAS_MASK			(0x7)
#define RT5663_DUMMY_BIAS_SHIFT			0
#define RT5663_DUMMY_BIAS_0_5			(0x0)
#define RT5663_DUMMY_BIAS_1			(0x1)
#define RT5663_DUMMY_BIAS_2			(0x2)
#define RT5663_DUMMY_BIAS_3			(0x3)
#define RT5663_DUMMY_BIAS_4_1			(0x4)
#define RT5663_DUMMY_BIAS_4_2			(0x5)
#define RT5663_DUMMY_BIAS_6			(0x6)
#define RT5663_DUMMY_BIAS_8			(0x7)


/* System Clock Source */
enum {
	RT5663_SCLK_S_MCLK,
	RT5663_SCLK_S_PLL1,
	RT5663_SCLK_S_RCCLK,
};

/* PLL1 Source */
enum {
	RT5663_PLL1_S_MCLK,
	RT5663_PLL1_S_BCLK1,
};

enum {
	RT5663_AIF,
	RT5663_AIFS,
};

/* asrc clock source */
enum {
	RT5663_CLK_SEL_SYS = 0x0,
	RT5663_CLK_SEL_I2S1_ASRC = 0x1,
};

/* filter mask */
enum {
	RT5663_DA_STEREO_FILTER = 0x1,
	RT5663_AD_STEREO_FILTER = 0x2,
};

int rt5663_set_jack_detect(struct snd_soc_codec *codec,
	struct snd_soc_jack *hs_jack);
int rt5663_sel_asrc_clk_src(struct snd_soc_codec *codec,
	unsigned int filter_mask, unsigned int clk_src);

#endif /* __RT5663_H__ */
