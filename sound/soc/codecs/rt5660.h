/*
 * rt5660.h  --  RT5660 ALSA SoC audio driver
 *
 * Copyright 2016 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _RT5660_H
#define _RT5660_H

#include <linux/clk.h>
#include <sound/rt5660.h>

/* Info */
#define RT5660_RESET				0x00
#define RT5660_VENDOR_ID			0xfd
#define RT5660_VENDOR_ID1			0xfe
#define RT5660_VENDOR_ID2			0xff
/*  I/O - Output */
#define RT5660_SPK_VOL				0x01
#define RT5660_LOUT_VOL				0x02
/* I/O - Input */
#define RT5660_IN1_IN2				0x0d
#define RT5660_IN3_IN4				0x0e
/* I/O - ADC/DAC/DMIC */
#define RT5660_DAC1_DIG_VOL			0x19
#define RT5660_STO1_ADC_DIG_VOL			0x1c
#define RT5660_ADC_BST_VOL1			0x1e
/* Mixer - D-D */
#define RT5660_STO1_ADC_MIXER			0x27
#define RT5660_AD_DA_MIXER			0x29
#define RT5660_STO_DAC_MIXER			0x2a
#define RT5660_DIG_INF1_DATA			0x2f
/* Mixer - ADC */
#define RT5660_REC_L1_MIXER			0x3b
#define RT5660_REC_L2_MIXER			0x3c
#define RT5660_REC_R1_MIXER			0x3d
#define RT5660_REC_R2_MIXER			0x3e
/* Mixer - DAC */
#define RT5660_LOUT_MIXER			0x45
#define RT5660_SPK_MIXER			0x46
#define RT5660_SPO_MIXER			0x48
#define RT5660_SPO_CLSD_RATIO			0x4a
#define RT5660_OUT_L_GAIN1			0x4d
#define RT5660_OUT_L_GAIN2			0x4e
#define RT5660_OUT_L1_MIXER			0x4f
#define RT5660_OUT_R_GAIN1			0x50
#define RT5660_OUT_R_GAIN2			0x51
#define RT5660_OUT_R1_MIXER			0x52
/* Power */
#define RT5660_PWR_DIG1				0x61
#define RT5660_PWR_DIG2				0x62
#define RT5660_PWR_ANLG1			0x63
#define RT5660_PWR_ANLG2			0x64
#define RT5660_PWR_MIXER			0x65
#define RT5660_PWR_VOL				0x66
/* Private Register Control */
#define RT5660_PRIV_INDEX			0x6a
#define RT5660_PRIV_DATA			0x6c
/* Format - ADC/DAC */
#define RT5660_I2S1_SDP				0x70
#define RT5660_ADDA_CLK1			0x73
#define RT5660_ADDA_CLK2			0x74
#define RT5660_DMIC_CTRL1			0x75
/* Function - Analog */
#define RT5660_GLB_CLK				0x80
#define RT5660_PLL_CTRL1			0x81
#define RT5660_PLL_CTRL2			0x82
#define RT5660_CLSD_AMP_OC_CTRL			0x8c
#define RT5660_CLSD_AMP_CTRL			0x8d
#define RT5660_LOUT_AMP_CTRL			0x8e
#define RT5660_SPK_AMP_SPKVDD			0x92
#define RT5660_MICBIAS				0x93
#define RT5660_CLSD_OUT_CTRL1			0xa1
#define RT5660_CLSD_OUT_CTRL2			0xa2
#define RT5660_DIPOLE_MIC_CTRL1			0xa3
#define RT5660_DIPOLE_MIC_CTRL2			0xa4
#define RT5660_DIPOLE_MIC_CTRL3			0xa5
#define RT5660_DIPOLE_MIC_CTRL4			0xa6
#define RT5660_DIPOLE_MIC_CTRL5			0xa7
#define RT5660_DIPOLE_MIC_CTRL6			0xa8
#define RT5660_DIPOLE_MIC_CTRL7			0xa9
#define RT5660_DIPOLE_MIC_CTRL8			0xaa
#define RT5660_DIPOLE_MIC_CTRL9			0xab
#define RT5660_DIPOLE_MIC_CTRL10		0xac
#define RT5660_DIPOLE_MIC_CTRL11		0xad
#define RT5660_DIPOLE_MIC_CTRL12		0xae
/* Function - Digital */
#define RT5660_EQ_CTRL1				0xb0
#define RT5660_EQ_CTRL2				0xb1
#define RT5660_DRC_AGC_CTRL1			0xb3
#define RT5660_DRC_AGC_CTRL2			0xb4
#define RT5660_DRC_AGC_CTRL3			0xb5
#define RT5660_DRC_AGC_CTRL4			0xb6
#define RT5660_DRC_AGC_CTRL5			0xb7
#define RT5660_JD_CTRL				0xbb
#define RT5660_IRQ_CTRL1			0xbd
#define RT5660_IRQ_CTRL2			0xbe
#define RT5660_INT_IRQ_ST			0xbf
#define RT5660_GPIO_CTRL1			0xc0
#define RT5660_GPIO_CTRL2			0xc2
#define RT5660_WIND_FILTER_CTRL1		0xd3
#define RT5660_SV_ZCD1				0xd9
#define RT5660_SV_ZCD2				0xda
#define RT5660_DRC1_LM_CTRL1			0xe0
#define RT5660_DRC1_LM_CTRL2			0xe1
#define RT5660_DRC2_LM_CTRL1			0xe2
#define RT5660_DRC2_LM_CTRL2			0xe3
#define RT5660_MULTI_DRC_CTRL			0xe4
#define RT5660_DRC2_CTRL1			0xe5
#define RT5660_DRC2_CTRL2			0xe6
#define RT5660_DRC2_CTRL3			0xe7
#define RT5660_DRC2_CTRL4			0xe8
#define RT5660_DRC2_CTRL5			0xe9
#define RT5660_ALC_PGA_CTRL1			0xea
#define RT5660_ALC_PGA_CTRL2			0xeb
#define RT5660_ALC_PGA_CTRL3			0xec
#define RT5660_ALC_PGA_CTRL4			0xed
#define RT5660_ALC_PGA_CTRL5			0xee
#define RT5660_ALC_PGA_CTRL6			0xef
#define RT5660_ALC_PGA_CTRL7			0xf0

/* General Control */
#define RT5660_GEN_CTRL1			0xfa
#define RT5660_GEN_CTRL2			0xfb
#define RT5660_GEN_CTRL3			0xfc

/* Index of Codec Private Register definition */
#define RT5660_CHOP_DAC_ADC			0x3d

/* Global Definition */
#define RT5660_L_MUTE				(0x1 << 15)
#define RT5660_L_MUTE_SFT			15
#define RT5660_VOL_L_MUTE			(0x1 << 14)
#define RT5660_VOL_L_SFT			14
#define RT5660_R_MUTE				(0x1 << 7)
#define RT5660_R_MUTE_SFT			7
#define RT5660_VOL_R_MUTE			(0x1 << 6)
#define RT5660_VOL_R_SFT			6
#define RT5660_L_VOL_MASK			(0x3f << 8)
#define RT5660_L_VOL_SFT			8
#define RT5660_R_VOL_MASK			(0x3f)
#define RT5660_R_VOL_SFT			0

/* IN1 and IN2 Control (0x0d) */
#define RT5660_IN_DF1				(0x1 << 15)
#define RT5660_IN_SFT1				15
#define RT5660_BST_MASK1			(0x7f << 8)
#define RT5660_BST_SFT1				8
#define RT5660_IN_DF2				(0x1 << 7)
#define RT5660_IN_SFT2				7
#define RT5660_BST_MASK2			(0x7f << 0)
#define RT5660_BST_SFT2				0

/* IN3 and IN4 Control (0x0e) */
#define RT5660_IN_DF3				(0x1 << 15)
#define RT5660_IN_SFT3				15
#define RT5660_BST_MASK3			(0x7f << 8)
#define RT5660_BST_SFT3				8
#define RT5660_IN_DF4				(0x1 << 7)
#define RT5660_IN_SFT4				7
#define RT5660_BST_MASK4			(0x7f << 0)
#define RT5660_BST_SFT4				0

/* DAC1 Digital Volume (0x19) */
#define RT5660_DAC_L1_VOL_MASK			(0x7f << 9)
#define RT5660_DAC_L1_VOL_SFT			9
#define RT5660_DAC_R1_VOL_MASK			(0x7f << 1)
#define RT5660_DAC_R1_VOL_SFT			1

/* ADC Digital Volume Control (0x1c) */
#define RT5660_ADC_L_VOL_MASK			(0x3f << 9)
#define RT5660_ADC_L_VOL_SFT			9
#define RT5660_ADC_R_VOL_MASK			(0x3f << 1)
#define RT5660_ADC_R_VOL_SFT			1

/* ADC Boost Volume Control (0x1e) */
#define RT5660_STO1_ADC_L_BST_MASK		(0x3 << 14)
#define RT5660_STO1_ADC_L_BST_SFT		14
#define RT5660_STO1_ADC_R_BST_MASK		(0x3 << 12)
#define RT5660_STO1_ADC_R_BST_SFT		12

/* Stereo ADC Mixer Control (0x27) */
#define RT5660_M_ADC_L1				(0x1 << 14)
#define RT5660_M_ADC_L1_SFT			14
#define RT5660_M_ADC_L2				(0x1 << 13)
#define RT5660_M_ADC_L2_SFT			13
#define RT5660_M_ADC_R1				(0x1 << 6)
#define RT5660_M_ADC_R1_SFT			6
#define RT5660_M_ADC_R2				(0x1 << 5)
#define RT5660_M_ADC_R2_SFT			5

/* ADC Mixer to DAC Mixer Control (0x29) */
#define RT5660_M_ADCMIX_L			(0x1 << 15)
#define RT5660_M_ADCMIX_L_SFT			15
#define RT5660_M_DAC1_L				(0x1 << 14)
#define RT5660_M_DAC1_L_SFT			14
#define RT5660_M_ADCMIX_R			(0x1 << 7)
#define RT5660_M_ADCMIX_R_SFT			7
#define RT5660_M_DAC1_R				(0x1 << 6)
#define RT5660_M_DAC1_R_SFT			6

/* Stereo DAC Mixer Control (0x2a) */
#define RT5660_M_DAC_L1				(0x1 << 14)
#define RT5660_M_DAC_L1_SFT			14
#define RT5660_DAC_L1_STO_L_VOL_MASK		(0x1 << 13)
#define RT5660_DAC_L1_STO_L_VOL_SFT		13
#define RT5660_M_DAC_R1_STO_L			(0x1 << 9)
#define RT5660_M_DAC_R1_STO_L_SFT		9
#define RT5660_DAC_R1_STO_L_VOL_MASK		(0x1 << 8)
#define RT5660_DAC_R1_STO_L_VOL_SFT		8
#define RT5660_M_DAC_R1				(0x1 << 6)
#define RT5660_M_DAC_R1_SFT			6
#define RT5660_DAC_R1_STO_R_VOL_MASK		(0x1 << 5)
#define RT5660_DAC_R1_STO_R_VOL_SFT		5
#define RT5660_M_DAC_L1_STO_R			(0x1 << 1)
#define RT5660_M_DAC_L1_STO_R_SFT		1
#define RT5660_DAC_L1_STO_R_VOL_MASK		(0x1)
#define RT5660_DAC_L1_STO_R_VOL_SFT		0

/* Digital Interface Data Control (0x2f) */
#define RT5660_IF1_DAC_IN_SEL			(0x3 << 14)
#define RT5660_IF1_DAC_IN_SFT			14
#define RT5660_IF1_ADC_IN_SEL			(0x3 << 12)
#define RT5660_IF1_ADC_IN_SFT			12

/* REC Left Mixer Control 1 (0x3b) */
#define RT5660_G_BST3_RM_L_MASK			(0x7 << 4)
#define RT5660_G_BST3_RM_L_SFT			4
#define RT5660_G_BST2_RM_L_MASK			(0x7 << 1)
#define RT5660_G_BST2_RM_L_SFT			1

/* REC Left Mixer Control 2 (0x3c) */
#define RT5660_G_BST1_RM_L_MASK			(0x7 << 13)
#define RT5660_G_BST1_RM_L_SFT			13
#define RT5660_G_OM_L_RM_L_MASK			(0x7 << 10)
#define RT5660_G_OM_L_RM_L_SFT			10
#define RT5660_M_BST3_RM_L			(0x1 << 3)
#define RT5660_M_BST3_RM_L_SFT			3
#define RT5660_M_BST2_RM_L			(0x1 << 2)
#define RT5660_M_BST2_RM_L_SFT			2
#define RT5660_M_BST1_RM_L			(0x1 << 1)
#define RT5660_M_BST1_RM_L_SFT			1
#define RT5660_M_OM_L_RM_L			(0x1)
#define RT5660_M_OM_L_RM_L_SFT			0

/* REC Right Mixer Control 1 (0x3d) */
#define RT5660_G_BST3_RM_R_MASK			(0x7 << 4)
#define RT5660_G_BST3_RM_R_SFT			4
#define RT5660_G_BST2_RM_R_MASK			(0x7 << 1)
#define RT5660_G_BST2_RM_R_SFT			1

/* REC Right Mixer Control 2 (0x3e) */
#define RT5660_G_BST1_RM_R_MASK			(0x7 << 13)
#define RT5660_G_BST1_RM_R_SFT			13
#define RT5660_G_OM_R_RM_R_MASK			(0x7 << 10)
#define RT5660_G_OM_R_RM_R_SFT			10
#define RT5660_M_BST3_RM_R			(0x1 << 3)
#define RT5660_M_BST3_RM_R_SFT			3
#define RT5660_M_BST2_RM_R			(0x1 << 2)
#define RT5660_M_BST2_RM_R_SFT			2
#define RT5660_M_BST1_RM_R			(0x1 << 1)
#define RT5660_M_BST1_RM_R_SFT			1
#define RT5660_M_OM_R_RM_R			(0x1)
#define RT5660_M_OM_R_RM_R_SFT			0

/* LOUTMIX Control (0x45) */
#define RT5660_M_DAC1_LM			(0x1 << 14)
#define RT5660_M_DAC1_LM_SFT			14
#define RT5660_M_LOVOL_M			(0x1 << 13)
#define RT5660_M_LOVOL_LM_SFT			13

/* SPK Mixer Control (0x46) */
#define RT5660_G_BST3_SM_MASK			(0x3 << 14)
#define RT5660_G_BST3_SM_SFT			14
#define RT5660_G_BST1_SM_MASK			(0x3 << 12)
#define RT5660_G_BST1_SM_SFT			12
#define RT5660_G_DACl_SM_MASK			(0x3 << 10)
#define RT5660_G_DACl_SM_SFT			10
#define RT5660_G_DACR_SM_MASK			(0x3 << 8)
#define RT5660_G_DACR_SM_SFT			8
#define RT5660_G_OM_L_SM_MASK			(0x3 << 6)
#define RT5660_G_OM_L_SM_SFT			6
#define RT5660_M_DACR_SM			(0x1 << 5)
#define RT5660_M_DACR_SM_SFT			5
#define RT5660_M_BST1_SM			(0x1 << 4)
#define RT5660_M_BST1_SM_SFT			4
#define RT5660_M_BST3_SM			(0x1 << 3)
#define RT5660_M_BST3_SM_SFT			3
#define RT5660_M_DACL_SM			(0x1 << 2)
#define RT5660_M_DACL_SM_SFT			2
#define RT5660_M_OM_L_SM			(0x1 << 1)
#define RT5660_M_OM_L_SM_SFT			1

/* SPOMIX Control (0x48) */
#define RT5660_M_DAC_R_SPM			(0x1 << 14)
#define RT5660_M_DAC_R_SPM_SFT			14
#define RT5660_M_DAC_L_SPM			(0x1 << 13)
#define RT5660_M_DAC_L_SPM_SFT			13
#define RT5660_M_SV_SPM				(0x1 << 12)
#define RT5660_M_SV_SPM_SFT			12
#define RT5660_M_BST1_SPM			(0x1 << 11)
#define RT5660_M_BST1_SPM_SFT			11

/* Output Left Mixer Control 1 (0x4d) */
#define RT5660_G_BST3_OM_L_MASK			(0x7 << 13)
#define RT5660_G_BST3_OM_L_SFT			13
#define RT5660_G_BST2_OM_L_MASK			(0x7 << 10)
#define RT5660_G_BST2_OM_L_SFT			10
#define RT5660_G_BST1_OM_L_MASK			(0x7 << 7)
#define RT5660_G_BST1_OM_L_SFT			7
#define RT5660_G_RM_L_OM_L_MASK			(0x7 << 1)
#define RT5660_G_RM_L_OM_L_SFT			1

/* Output Left Mixer Control 2 (0x4e) */
#define RT5660_G_DAC_R1_OM_L_MASK		(0x7 << 10)
#define RT5660_G_DAC_R1_OM_L_SFT		10
#define RT5660_G_DAC_L1_OM_L_MASK		(0x7 << 7)
#define RT5660_G_DAC_L1_OM_L_SFT		7

/* Output Left Mixer Control 3 (0x4f) */
#define RT5660_M_BST3_OM_L			(0x1 << 5)
#define RT5660_M_BST3_OM_L_SFT			5
#define RT5660_M_BST2_OM_L			(0x1 << 4)
#define RT5660_M_BST2_OM_L_SFT			4
#define RT5660_M_BST1_OM_L			(0x1 << 3)
#define RT5660_M_BST1_OM_L_SFT			3
#define RT5660_M_RM_L_OM_L			(0x1 << 2)
#define RT5660_M_RM_L_OM_L_SFT			2
#define RT5660_M_DAC_R_OM_L			(0x1 << 1)
#define RT5660_M_DAC_R_OM_L_SFT			1
#define RT5660_M_DAC_L_OM_L			(0x1)
#define RT5660_M_DAC_L_OM_L_SFT			0

/* Output Right Mixer Control 1 (0x50) */
#define RT5660_G_BST2_OM_R_MASK			(0x7 << 10)
#define RT5660_G_BST2_OM_R_SFT			10
#define RT5660_G_BST1_OM_R_MASK			(0x7 << 7)
#define RT5660_G_BST1_OM_R_SFT			7
#define RT5660_G_RM_R_OM_R_MASK			(0x7 << 1)
#define RT5660_G_RM_R_OM_R_SFT			1

/* Output Right Mixer Control 2 (0x51) */
#define RT5660_G_DAC_L_OM_R_MASK		(0x7 << 10)
#define RT5660_G_DAC_L_OM_R_SFT			10
#define RT5660_G_DAC_R_OM_R_MASK		(0x7 << 7)
#define RT5660_G_DAC_R_OM_R_SFT			7

/* Output Right Mixer Control 3 (0x52) */
#define RT5660_M_BST2_OM_R			(0x1 << 4)
#define RT5660_M_BST2_OM_R_SFT			4
#define RT5660_M_BST1_OM_R			(0x1 << 3)
#define RT5660_M_BST1_OM_R_SFT			3
#define RT5660_M_RM_R_OM_R			(0x1 << 2)
#define RT5660_M_RM_R_OM_R_SFT			2
#define RT5660_M_DAC_L_OM_R			(0x1 << 1)
#define RT5660_M_DAC_L_OM_R_SFT			1
#define RT5660_M_DAC_R_OM_R			(0x1)
#define RT5660_M_DAC_R_OM_R_SFT			0

/* Power Management for Digital 1 (0x61) */
#define RT5660_PWR_I2S1				(0x1 << 15)
#define RT5660_PWR_I2S1_BIT			15
#define RT5660_PWR_DAC_L1			(0x1 << 12)
#define RT5660_PWR_DAC_L1_BIT			12
#define RT5660_PWR_DAC_R1			(0x1 << 11)
#define RT5660_PWR_DAC_R1_BIT			11
#define RT5660_PWR_ADC_L			(0x1 << 2)
#define RT5660_PWR_ADC_L_BIT			2
#define RT5660_PWR_ADC_R			(0x1 << 1)
#define RT5660_PWR_ADC_R_BIT			1
#define RT5660_PWR_CLS_D			(0x1)
#define RT5660_PWR_CLS_D_BIT			0

/* Power Management for Digital 2 (0x62) */
#define RT5660_PWR_ADC_S1F			(0x1 << 15)
#define RT5660_PWR_ADC_S1F_BIT			15
#define RT5660_PWR_DAC_S1F			(0x1 << 11)
#define RT5660_PWR_DAC_S1F_BIT			11

/* Power Management for Analog 1 (0x63) */
#define RT5660_PWR_VREF1			(0x1 << 15)
#define RT5660_PWR_VREF1_BIT			15
#define RT5660_PWR_FV1				(0x1 << 14)
#define RT5660_PWR_FV1_BIT			14
#define RT5660_PWR_MB				(0x1 << 13)
#define RT5660_PWR_MB_BIT			13
#define RT5660_PWR_BG				(0x1 << 11)
#define RT5660_PWR_BG_BIT			11
#define RT5660_PWR_HP_L				(0x1 << 7)
#define RT5660_PWR_HP_L_BIT			7
#define RT5660_PWR_HP_R				(0x1 << 6)
#define RT5660_PWR_HP_R_BIT			6
#define RT5660_PWR_HA				(0x1 << 5)
#define RT5660_PWR_HA_BIT			5
#define RT5660_PWR_VREF2			(0x1 << 4)
#define RT5660_PWR_VREF2_BIT			4
#define RT5660_PWR_FV2				(0x1 << 3)
#define RT5660_PWR_FV2_BIT			3
#define RT5660_PWR_LDO2				(0x1 << 2)
#define RT5660_PWR_LDO2_BIT			2

/* Power Management for Analog 2 (0x64) */
#define RT5660_PWR_BST1				(0x1 << 15)
#define RT5660_PWR_BST1_BIT			15
#define RT5660_PWR_BST2				(0x1 << 14)
#define RT5660_PWR_BST2_BIT			14
#define RT5660_PWR_BST3				(0x1 << 13)
#define RT5660_PWR_BST3_BIT			13
#define RT5660_PWR_MB1				(0x1 << 11)
#define RT5660_PWR_MB1_BIT			11
#define RT5660_PWR_MB2				(0x1 << 10)
#define RT5660_PWR_MB2_BIT			10
#define RT5660_PWR_PLL				(0x1 << 9)
#define RT5660_PWR_PLL_BIT			9

/* Power Management for Mixer (0x65) */
#define RT5660_PWR_OM_L				(0x1 << 15)
#define RT5660_PWR_OM_L_BIT			15
#define RT5660_PWR_OM_R				(0x1 << 14)
#define RT5660_PWR_OM_R_BIT			14
#define RT5660_PWR_SM				(0x1 << 13)
#define RT5660_PWR_SM_BIT			13
#define RT5660_PWR_RM_L				(0x1 << 11)
#define RT5660_PWR_RM_L_BIT			11
#define RT5660_PWR_RM_R				(0x1 << 10)
#define RT5660_PWR_RM_R_BIT			10

/* Power Management for Volume (0x66) */
#define RT5660_PWR_SV				(0x1 << 15)
#define RT5660_PWR_SV_BIT			15
#define RT5660_PWR_LV_L				(0x1 << 11)
#define RT5660_PWR_LV_L_BIT			11
#define RT5660_PWR_LV_R				(0x1 << 10)
#define RT5660_PWR_LV_R_BIT			10

/* I2S1 Audio Serial Data Port Control (0x70) */
#define RT5660_I2S_MS_MASK			(0x1 << 15)
#define RT5660_I2S_MS_SFT			15
#define RT5660_I2S_MS_M				(0x0 << 15)
#define RT5660_I2S_MS_S				(0x1 << 15)
#define RT5660_I2S_O_CP_MASK			(0x3 << 10)
#define RT5660_I2S_O_CP_SFT			10
#define RT5660_I2S_O_CP_OFF			(0x0 << 10)
#define RT5660_I2S_O_CP_U_LAW			(0x1 << 10)
#define RT5660_I2S_O_CP_A_LAW			(0x2 << 10)
#define RT5660_I2S_I_CP_MASK			(0x3 << 8)
#define RT5660_I2S_I_CP_SFT			8
#define RT5660_I2S_I_CP_OFF			(0x0 << 8)
#define RT5660_I2S_I_CP_U_LAW			(0x1 << 8)
#define RT5660_I2S_I_CP_A_LAW			(0x2 << 8)
#define RT5660_I2S_BP_MASK			(0x1 << 7)
#define RT5660_I2S_BP_SFT			7
#define RT5660_I2S_BP_NOR			(0x0 << 7)
#define RT5660_I2S_BP_INV			(0x1 << 7)
#define RT5660_I2S_DL_MASK			(0x3 << 2)
#define RT5660_I2S_DL_SFT			2
#define RT5660_I2S_DL_16			(0x0 << 2)
#define RT5660_I2S_DL_20			(0x1 << 2)
#define RT5660_I2S_DL_24			(0x2 << 2)
#define RT5660_I2S_DL_8				(0x3 << 2)
#define RT5660_I2S_DF_MASK			(0x3)
#define RT5660_I2S_DF_SFT			0
#define RT5660_I2S_DF_I2S			(0x0)
#define RT5660_I2S_DF_LEFT			(0x1)
#define RT5660_I2S_DF_PCM_A			(0x2)
#define RT5660_I2S_DF_PCM_B			(0x3)

/* ADC/DAC Clock Control 1 (0x73) */
#define RT5660_I2S_BCLK_MS1_MASK		(0x1 << 15)
#define RT5660_I2S_BCLK_MS1_SFT			15
#define RT5660_I2S_BCLK_MS1_32			(0x0 << 15)
#define RT5660_I2S_BCLK_MS1_64			(0x1 << 15)
#define RT5660_I2S_PD1_MASK			(0x7 << 12)
#define RT5660_I2S_PD1_SFT			12
#define RT5660_I2S_PD1_1			(0x0 << 12)
#define RT5660_I2S_PD1_2			(0x1 << 12)
#define RT5660_I2S_PD1_3			(0x2 << 12)
#define RT5660_I2S_PD1_4			(0x3 << 12)
#define RT5660_I2S_PD1_6			(0x4 << 12)
#define RT5660_I2S_PD1_8			(0x5 << 12)
#define RT5660_I2S_PD1_12			(0x6 << 12)
#define RT5660_I2S_PD1_16			(0x7 << 12)
#define RT5660_DAC_OSR_MASK			(0x3 << 2)
#define RT5660_DAC_OSR_SFT			2
#define RT5660_DAC_OSR_128			(0x0 << 2)
#define RT5660_DAC_OSR_64			(0x1 << 2)
#define RT5660_DAC_OSR_32			(0x2 << 2)
#define RT5660_DAC_OSR_16			(0x3 << 2)
#define RT5660_ADC_OSR_MASK			(0x3)
#define RT5660_ADC_OSR_SFT			0
#define RT5660_ADC_OSR_128			(0x0)
#define RT5660_ADC_OSR_64			(0x1)
#define RT5660_ADC_OSR_32			(0x2)
#define RT5660_ADC_OSR_16			(0x3)

/* ADC/DAC Clock Control 2 (0x74) */
#define RT5660_RESET_ADF			(0x1 << 13)
#define RT5660_RESET_ADF_SFT			13
#define RT5660_RESET_DAF			(0x1 << 12)
#define RT5660_RESET_DAF_SFT			12
#define RT5660_DAHPF_EN				(0x1 << 11)
#define RT5660_DAHPF_EN_SFT			11
#define RT5660_ADHPF_EN				(0x1 << 10)
#define RT5660_ADHPF_EN_SFT			10

/* Digital Microphone Control (0x75) */
#define RT5660_DMIC_1_EN_MASK			(0x1 << 15)
#define RT5660_DMIC_1_EN_SFT			15
#define RT5660_DMIC_1_DIS			(0x0 << 15)
#define RT5660_DMIC_1_EN			(0x1 << 15)
#define RT5660_DMIC_1L_LH_MASK			(0x1 << 13)
#define RT5660_DMIC_1L_LH_SFT			13
#define RT5660_DMIC_1L_LH_RISING		(0x0 << 13)
#define RT5660_DMIC_1L_LH_FALLING		(0x1 << 13)
#define RT5660_DMIC_1R_LH_MASK			(0x1 << 12)
#define RT5660_DMIC_1R_LH_SFT			12
#define RT5660_DMIC_1R_LH_RISING		(0x0 << 12)
#define RT5660_DMIC_1R_LH_FALLING		(0x1 << 12)
#define RT5660_SEL_DMIC_DATA_MASK		(0x1 << 11)
#define RT5660_SEL_DMIC_DATA_SFT		11
#define RT5660_SEL_DMIC_DATA_GPIO2		(0x0 << 11)
#define RT5660_SEL_DMIC_DATA_IN1P		(0x1 << 11)
#define RT5660_DMIC_CLK_MASK			(0x7 << 5)
#define RT5660_DMIC_CLK_SFT			5

/* Global Clock Control (0x80) */
#define RT5660_SCLK_SRC_MASK			(0x3 << 14)
#define RT5660_SCLK_SRC_SFT			14
#define RT5660_SCLK_SRC_MCLK			(0x0 << 14)
#define RT5660_SCLK_SRC_PLL1			(0x1 << 14)
#define RT5660_SCLK_SRC_RCCLK			(0x2 << 14)
#define RT5660_PLL1_SRC_MASK			(0x3 << 12)
#define RT5660_PLL1_SRC_SFT			12
#define RT5660_PLL1_SRC_MCLK			(0x0 << 12)
#define RT5660_PLL1_SRC_BCLK1			(0x1 << 12)
#define RT5660_PLL1_SRC_RCCLK			(0x2 << 12)
#define RT5660_PLL1_PD_MASK			(0x1 << 3)
#define RT5660_PLL1_PD_SFT			3
#define RT5660_PLL1_PD_1			(0x0 << 3)
#define RT5660_PLL1_PD_2			(0x1 << 3)

#define RT5660_PLL_INP_MAX			40000000
#define RT5660_PLL_INP_MIN			256000
/* PLL M/N/K Code Control 1 (0x81) */
#define RT5660_PLL_N_MAX			0x1ff
#define RT5660_PLL_N_MASK			(RT5660_PLL_N_MAX << 7)
#define RT5660_PLL_N_SFT			7
#define RT5660_PLL_K_MAX			0x1f
#define RT5660_PLL_K_MASK			(RT5660_PLL_K_MAX)
#define RT5660_PLL_K_SFT			0

/* PLL M/N/K Code Control 2 (0x82) */
#define RT5660_PLL_M_MAX			0xf
#define RT5660_PLL_M_MASK			(RT5660_PLL_M_MAX << 12)
#define RT5660_PLL_M_SFT			12
#define RT5660_PLL_M_BP				(0x1 << 11)
#define RT5660_PLL_M_BP_SFT			11

/* Class D Over Current Control (0x8c) */
#define RT5660_CLSD_OC_MASK			(0x1 << 9)
#define RT5660_CLSD_OC_SFT			9
#define RT5660_CLSD_OC_PU			(0x0 << 9)
#define RT5660_CLSD_OC_PD			(0x1 << 9)
#define RT5660_AUTO_PD_MASK			(0x1 << 8)
#define RT5660_AUTO_PD_SFT			8
#define RT5660_AUTO_PD_DIS			(0x0 << 8)
#define RT5660_AUTO_PD_EN			(0x1 << 8)
#define RT5660_CLSD_OC_TH_MASK			(0x3f)
#define RT5660_CLSD_OC_TH_SFT			0

/* Class D Output Control (0x8d) */
#define RT5660_CLSD_RATIO_MASK			(0xf << 12)
#define RT5660_CLSD_RATIO_SFT			12

/* Lout Amp Control 1 (0x8e) */
#define RT5660_LOUT_CO_MASK			(0x1 << 4)
#define RT5660_LOUT_CO_SFT			4
#define RT5660_LOUT_CO_DIS			(0x0 << 4)
#define RT5660_LOUT_CO_EN			(0x1 << 4)
#define RT5660_LOUT_CB_MASK			(0x1)
#define RT5660_LOUT_CB_SFT			0
#define RT5660_LOUT_CB_PD			(0x0)
#define RT5660_LOUT_CB_PU			(0x1)

/* SPKVDD detection control (0x92) */
#define RT5660_SPKVDD_DET_MASK			(0x1 << 15)
#define RT5660_SPKVDD_DET_SFT			15
#define RT5660_SPKVDD_DET_DIS			(0x0 << 15)
#define RT5660_SPKVDD_DET_EN			(0x1 << 15)
#define RT5660_SPK_AG_MASK			(0x1 << 14)
#define RT5660_SPK_AG_SFT			14
#define RT5660_SPK_AG_DIS			(0x0 << 14)
#define RT5660_SPK_AG_EN			(0x1 << 14)

/* Micbias Control (0x93) */
#define RT5660_MIC1_BS_MASK			(0x1 << 15)
#define RT5660_MIC1_BS_SFT			15
#define RT5660_MIC1_BS_9AV			(0x0 << 15)
#define RT5660_MIC1_BS_75AV			(0x1 << 15)
#define RT5660_MIC2_BS_MASK			(0x1 << 14)
#define RT5660_MIC2_BS_SFT			14
#define RT5660_MIC2_BS_9AV			(0x0 << 14)
#define RT5660_MIC2_BS_75AV			(0x1 << 14)
#define RT5660_MIC1_OVCD_MASK			(0x1 << 11)
#define RT5660_MIC1_OVCD_SFT			11
#define RT5660_MIC1_OVCD_DIS			(0x0 << 11)
#define RT5660_MIC1_OVCD_EN			(0x1 << 11)
#define RT5660_MIC1_OVTH_MASK			(0x3 << 9)
#define RT5660_MIC1_OVTH_SFT			9
#define RT5660_MIC1_OVTH_600UA			(0x0 << 9)
#define RT5660_MIC1_OVTH_1500UA			(0x1 << 9)
#define RT5660_MIC1_OVTH_2000UA			(0x2 << 9)
#define RT5660_MIC2_OVCD_MASK			(0x1 << 8)
#define RT5660_MIC2_OVCD_SFT			8
#define RT5660_MIC2_OVCD_DIS			(0x0 << 8)
#define RT5660_MIC2_OVCD_EN			(0x1 << 8)
#define RT5660_MIC2_OVTH_MASK			(0x3 << 6)
#define RT5660_MIC2_OVTH_SFT			6
#define RT5660_MIC2_OVTH_600UA			(0x0 << 6)
#define RT5660_MIC2_OVTH_1500UA			(0x1 << 6)
#define RT5660_MIC2_OVTH_2000UA			(0x2 << 6)
#define RT5660_PWR_CLK25M_MASK			(0x1 << 4)
#define RT5660_PWR_CLK25M_SFT			4
#define RT5660_PWR_CLK25M_PD			(0x0 << 4)
#define RT5660_PWR_CLK25M_PU			(0x1 << 4)

/* EQ Control 1 (0xb0) */
#define RT5660_EQ_SRC_MASK			(0x1 << 15)
#define RT5660_EQ_SRC_SFT			15
#define RT5660_EQ_SRC_DAC			(0x0 << 15)
#define RT5660_EQ_SRC_ADC			(0x1 << 15)
#define RT5660_EQ_UPD				(0x1 << 14)
#define RT5660_EQ_UPD_BIT			14

/* Jack Detect Control (0xbb) */
#define RT5660_JD_MASK				(0x3 << 14)
#define RT5660_JD_SFT				14
#define RT5660_JD_DIS				(0x0 << 14)
#define RT5660_JD_GPIO1				(0x1 << 14)
#define RT5660_JD_GPIO2				(0x2 << 14)
#define RT5660_JD_LOUT_MASK			(0x1 << 11)
#define RT5660_JD_LOUT_SFT			11
#define RT5660_JD_LOUT_DIS			(0x0 << 11)
#define RT5660_JD_LOUT_EN			(0x1 << 11)
#define RT5660_JD_LOUT_TRG_MASK			(0x1 << 10)
#define RT5660_JD_LOUT_TRG_SFT			10
#define RT5660_JD_LOUT_TRG_LO			(0x0 << 10)
#define RT5660_JD_LOUT_TRG_HI			(0x1 << 10)
#define RT5660_JD_SPO_MASK			(0x1 << 9)
#define RT5660_JD_SPO_SFT			9
#define RT5660_JD_SPO_DIS			(0x0 << 9)
#define RT5660_JD_SPO_EN			(0x1 << 9)
#define RT5660_JD_SPO_TRG_MASK			(0x1 << 8)
#define RT5660_JD_SPO_TRG_SFT			8
#define RT5660_JD_SPO_TRG_LO			(0x0 << 8)
#define RT5660_JD_SPO_TRG_HI			(0x1 << 8)

/* IRQ Control 1 (0xbd) */
#define RT5660_IRQ_JD_MASK			(0x1 << 15)
#define RT5660_IRQ_JD_SFT			15
#define RT5660_IRQ_JD_BP			(0x0 << 15)
#define RT5660_IRQ_JD_NOR			(0x1 << 15)
#define RT5660_IRQ_OT_MASK			(0x1 << 14)
#define RT5660_IRQ_OT_SFT			14
#define RT5660_IRQ_OT_BP			(0x0 << 14)
#define RT5660_IRQ_OT_NOR			(0x1 << 14)
#define RT5660_JD_STKY_MASK			(0x1 << 13)
#define RT5660_JD_STKY_SFT			13
#define RT5660_JD_STKY_DIS			(0x0 << 13)
#define RT5660_JD_STKY_EN			(0x1 << 13)
#define RT5660_OT_STKY_MASK			(0x1 << 12)
#define RT5660_OT_STKY_SFT			12
#define RT5660_OT_STKY_DIS			(0x0 << 12)
#define RT5660_OT_STKY_EN			(0x1 << 12)
#define RT5660_JD_P_MASK			(0x1 << 11)
#define RT5660_JD_P_SFT				11
#define RT5660_JD_P_NOR				(0x0 << 11)
#define RT5660_JD_P_INV				(0x1 << 11)
#define RT5660_OT_P_MASK			(0x1 << 10)
#define RT5660_OT_P_SFT				10
#define RT5660_OT_P_NOR				(0x0 << 10)
#define RT5660_OT_P_INV				(0x1 << 10)

/* IRQ Control 2 (0xbe) */
#define RT5660_IRQ_MB1_OC_MASK			(0x1 << 15)
#define RT5660_IRQ_MB1_OC_SFT			15
#define RT5660_IRQ_MB1_OC_BP			(0x0 << 15)
#define RT5660_IRQ_MB1_OC_NOR			(0x1 << 15)
#define RT5660_IRQ_MB2_OC_MASK			(0x1 << 14)
#define RT5660_IRQ_MB2_OC_SFT			14
#define RT5660_IRQ_MB2_OC_BP			(0x0 << 14)
#define RT5660_IRQ_MB2_OC_NOR			(0x1 << 14)
#define RT5660_MB1_OC_STKY_MASK			(0x1 << 11)
#define RT5660_MB1_OC_STKY_SFT			11
#define RT5660_MB1_OC_STKY_DIS			(0x0 << 11)
#define RT5660_MB1_OC_STKY_EN			(0x1 << 11)
#define RT5660_MB2_OC_STKY_MASK			(0x1 << 10)
#define RT5660_MB2_OC_STKY_SFT			10
#define RT5660_MB2_OC_STKY_DIS			(0x0 << 10)
#define RT5660_MB2_OC_STKY_EN			(0x1 << 10)
#define RT5660_MB1_OC_P_MASK			(0x1 << 7)
#define RT5660_MB1_OC_P_SFT			7
#define RT5660_MB1_OC_P_NOR			(0x0 << 7)
#define RT5660_MB1_OC_P_INV			(0x1 << 7)
#define RT5660_MB2_OC_P_MASK			(0x1 << 6)
#define RT5660_MB2_OC_P_SFT			6
#define RT5660_MB2_OC_P_NOR			(0x0 << 6)
#define RT5660_MB2_OC_P_INV			(0x1 << 6)
#define RT5660_MB1_OC_CLR			(0x1 << 3)
#define RT5660_MB1_OC_CLR_SFT			3
#define RT5660_MB2_OC_CLR			(0x1 << 2)
#define RT5660_MB2_OC_CLR_SFT			2

/* GPIO Control 1 (0xc0) */
#define RT5660_GP2_PIN_MASK			(0x1 << 14)
#define RT5660_GP2_PIN_SFT			14
#define RT5660_GP2_PIN_GPIO2			(0x0 << 14)
#define RT5660_GP2_PIN_DMIC1_SDA		(0x1 << 14)
#define RT5660_GP1_PIN_MASK			(0x3 << 12)
#define RT5660_GP1_PIN_SFT			12
#define RT5660_GP1_PIN_GPIO1			(0x0 << 12)
#define RT5660_GP1_PIN_DMIC1_SCL		(0x1 << 12)
#define RT5660_GP1_PIN_IRQ			(0x2 << 12)
#define RT5660_GPIO_M_MASK			(0x1 << 9)
#define RT5660_GPIO_M_SFT			9
#define RT5660_GPIO_M_FLT			(0x0 << 9)
#define RT5660_GPIO_M_PH			(0x1 << 9)

/* GPIO Control 3 (0xc2) */
#define RT5660_GP2_PF_MASK			(0x1 << 5)
#define RT5660_GP2_PF_SFT			5
#define RT5660_GP2_PF_IN			(0x0 << 5)
#define RT5660_GP2_PF_OUT			(0x1 << 5)
#define RT5660_GP2_OUT_MASK			(0x1 << 4)
#define RT5660_GP2_OUT_SFT			4
#define RT5660_GP2_OUT_LO			(0x0 << 4)
#define RT5660_GP2_OUT_HI			(0x1 << 4)
#define RT5660_GP2_P_MASK			(0x1 << 3)
#define RT5660_GP2_P_SFT			3
#define RT5660_GP2_P_NOR			(0x0 << 3)
#define RT5660_GP2_P_INV			(0x1 << 3)
#define RT5660_GP1_PF_MASK			(0x1 << 2)
#define RT5660_GP1_PF_SFT			2
#define RT5660_GP1_PF_IN			(0x0 << 2)
#define RT5660_GP1_PF_OUT			(0x1 << 2)
#define RT5660_GP1_OUT_MASK			(0x1 << 1)
#define RT5660_GP1_OUT_SFT			1
#define RT5660_GP1_OUT_LO			(0x0 << 1)
#define RT5660_GP1_OUT_HI			(0x1 << 1)
#define RT5660_GP1_P_MASK			(0x1)
#define RT5660_GP1_P_SFT			0
#define RT5660_GP1_P_NOR			(0x0)
#define RT5660_GP1_P_INV			(0x1)

/* Soft volume and zero cross control 1 (0xd9) */
#define RT5660_SV_MASK				(0x1 << 15)
#define RT5660_SV_SFT				15
#define RT5660_SV_DIS				(0x0 << 15)
#define RT5660_SV_EN				(0x1 << 15)
#define RT5660_SPO_SV_MASK			(0x1 << 14)
#define RT5660_SPO_SV_SFT			14
#define RT5660_SPO_SV_DIS			(0x0 << 14)
#define RT5660_SPO_SV_EN			(0x1 << 14)
#define RT5660_OUT_SV_MASK			(0x1 << 12)
#define RT5660_OUT_SV_SFT			12
#define RT5660_OUT_SV_DIS			(0x0 << 12)
#define RT5660_OUT_SV_EN			(0x1 << 12)
#define RT5660_ZCD_DIG_MASK			(0x1 << 11)
#define RT5660_ZCD_DIG_SFT			11
#define RT5660_ZCD_DIG_DIS			(0x0 << 11)
#define RT5660_ZCD_DIG_EN			(0x1 << 11)
#define RT5660_ZCD_MASK				(0x1 << 10)
#define RT5660_ZCD_SFT				10
#define RT5660_ZCD_PD				(0x0 << 10)
#define RT5660_ZCD_PU				(0x1 << 10)
#define RT5660_SV_DLY_MASK			(0xf)
#define RT5660_SV_DLY_SFT			0

/* Soft volume and zero cross control 2 (0xda) */
#define RT5660_ZCD_SPO_MASK			(0x1 << 15)
#define RT5660_ZCD_SPO_SFT			15
#define RT5660_ZCD_SPO_DIS			(0x0 << 15)
#define RT5660_ZCD_SPO_EN			(0x1 << 15)
#define RT5660_ZCD_OMR_MASK			(0x1 << 8)
#define RT5660_ZCD_OMR_SFT			8
#define RT5660_ZCD_OMR_DIS			(0x0 << 8)
#define RT5660_ZCD_OMR_EN			(0x1 << 8)
#define RT5660_ZCD_OML_MASK			(0x1 << 7)
#define RT5660_ZCD_OML_SFT			7
#define RT5660_ZCD_OML_DIS			(0x0 << 7)
#define RT5660_ZCD_OML_EN			(0x1 << 7)
#define RT5660_ZCD_SPM_MASK			(0x1 << 6)
#define RT5660_ZCD_SPM_SFT			6
#define RT5660_ZCD_SPM_DIS			(0x0 << 6)
#define RT5660_ZCD_SPM_EN			(0x1 << 6)
#define RT5660_ZCD_RMR_MASK			(0x1 << 5)
#define RT5660_ZCD_RMR_SFT			5
#define RT5660_ZCD_RMR_DIS			(0x0 << 5)
#define RT5660_ZCD_RMR_EN			(0x1 << 5)
#define RT5660_ZCD_RML_MASK			(0x1 << 4)
#define RT5660_ZCD_RML_SFT			4
#define RT5660_ZCD_RML_DIS			(0x0 << 4)
#define RT5660_ZCD_RML_EN			(0x1 << 4)

/* General Control 1 (0xfa) */
#define RT5660_PWR_VREF_HP			(0x1 << 11)
#define RT5660_PWR_VREF_HP_SFT			11
#define RT5660_AUTO_DIS_AMP			(0x1 << 6)
#define RT5660_MCLK_DET				(0x1 << 5)
#define RT5660_POW_CLKDET			(0x1 << 1)
#define RT5660_DIG_GATE_CTRL			(0x1)
#define RT5660_DIG_GATE_CTRL_SFT		0

/* System Clock Source */
#define RT5660_SCLK_S_MCLK			0
#define RT5660_SCLK_S_PLL1			1
#define RT5660_SCLK_S_RCCLK			2

/* PLL1 Source */
#define RT5660_PLL1_S_MCLK			0
#define RT5660_PLL1_S_BCLK			1

enum {
	RT5660_AIF1,
	RT5660_AIFS,
};

struct rt5660_priv {
	struct snd_soc_component *component;
	struct rt5660_platform_data pdata;
	struct regmap *regmap;
	struct clk *mclk;

	int sysclk;
	int sysclk_src;
	int lrck[RT5660_AIFS];
	int bclk[RT5660_AIFS];
	int master[RT5660_AIFS];

	int pll_src;
	int pll_in;
	int pll_out;
};

#endif
