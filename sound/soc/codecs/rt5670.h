/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt5670.h  --  RT5670 ALSA SoC audio driver
 *
 * Copyright 2014 Realtek Microelectronics
 * Author: Bard Liao <bardliao@realtek.com>
 */

#ifndef __RT5670_H__
#define __RT5670_H__

/* Info */
#define RT5670_RESET				0x00
#define RT5670_VENDOR_ID			0xfd
#define RT5670_VENDOR_ID1			0xfe
#define RT5670_VENDOR_ID2			0xff
/*  I/O - Output */
#define RT5670_HP_VOL				0x02
#define RT5670_LOUT1				0x03
/* I/O - Input */
#define RT5670_CJ_CTRL1				0x0a
#define RT5670_CJ_CTRL2				0x0b
#define RT5670_CJ_CTRL3				0x0c
#define RT5670_IN2				0x0e
#define RT5670_INL1_INR1_VOL			0x0f
/* I/O - ADC/DAC/DMIC */
#define RT5670_DAC1_DIG_VOL			0x19
#define RT5670_DAC2_DIG_VOL			0x1a
#define RT5670_DAC_CTRL				0x1b
#define RT5670_STO1_ADC_DIG_VOL			0x1c
#define RT5670_MONO_ADC_DIG_VOL			0x1d
#define RT5670_ADC_BST_VOL1			0x1e
#define RT5670_STO2_ADC_DIG_VOL			0x1f
/* Mixer - D-D */
#define RT5670_ADC_BST_VOL2			0x20
#define RT5670_STO2_ADC_MIXER			0x26
#define RT5670_STO1_ADC_MIXER			0x27
#define RT5670_MONO_ADC_MIXER			0x28
#define RT5670_AD_DA_MIXER			0x29
#define RT5670_STO_DAC_MIXER			0x2a
#define RT5670_DD_MIXER				0x2b
#define RT5670_DIG_MIXER			0x2c
#define RT5670_DSP_PATH1			0x2d
#define RT5670_DSP_PATH2			0x2e
#define RT5670_DIG_INF1_DATA			0x2f
#define RT5670_DIG_INF2_DATA			0x30
/* Mixer - PDM */
#define RT5670_PDM_OUT_CTRL			0x31
#define RT5670_PDM_DATA_CTRL1			0x32
#define RT5670_PDM1_DATA_CTRL2			0x33
#define RT5670_PDM1_DATA_CTRL3			0x34
#define RT5670_PDM1_DATA_CTRL4			0x35
#define RT5670_PDM2_DATA_CTRL2			0x36
#define RT5670_PDM2_DATA_CTRL3			0x37
#define RT5670_PDM2_DATA_CTRL4			0x38
/* Mixer - ADC */
#define RT5670_REC_L1_MIXER			0x3b
#define RT5670_REC_L2_MIXER			0x3c
#define RT5670_REC_R1_MIXER			0x3d
#define RT5670_REC_R2_MIXER			0x3e
/* Mixer - DAC */
#define RT5670_HPO_MIXER			0x45
#define RT5670_MONO_MIXER			0x4c
#define RT5670_OUT_L1_MIXER			0x4f
#define RT5670_OUT_R1_MIXER			0x52
#define RT5670_LOUT_MIXER			0x53
/* Power */
#define RT5670_PWR_DIG1				0x61
#define RT5670_PWR_DIG2				0x62
#define RT5670_PWR_ANLG1			0x63
#define RT5670_PWR_ANLG2			0x64
#define RT5670_PWR_MIXER			0x65
#define RT5670_PWR_VOL				0x66
/* Private Register Control */
#define RT5670_PRIV_INDEX			0x6a
#define RT5670_PRIV_DATA			0x6c
/* Format - ADC/DAC */
#define RT5670_I2S4_SDP				0x6f
#define RT5670_I2S1_SDP				0x70
#define RT5670_I2S2_SDP				0x71
#define RT5670_I2S3_SDP				0x72
#define RT5670_ADDA_CLK1			0x73
#define RT5670_ADDA_CLK2			0x74
#define RT5670_DMIC_CTRL1			0x75
#define RT5670_DMIC_CTRL2			0x76
/* Format - TDM Control */
#define RT5670_TDM_CTRL_1			0x77
#define RT5670_TDM_CTRL_2			0x78
#define RT5670_TDM_CTRL_3			0x79

/* Function - Analog */
#define RT5670_DSP_CLK				0x7f
#define RT5670_GLB_CLK				0x80
#define RT5670_PLL_CTRL1			0x81
#define RT5670_PLL_CTRL2			0x82
#define RT5670_ASRC_1				0x83
#define RT5670_ASRC_2				0x84
#define RT5670_ASRC_3				0x85
#define RT5670_ASRC_4				0x86
#define RT5670_ASRC_5				0x87
#define RT5670_ASRC_7				0x89
#define RT5670_ASRC_8				0x8a
#define RT5670_ASRC_9				0x8b
#define RT5670_ASRC_10				0x8c
#define RT5670_ASRC_11				0x8d
#define RT5670_DEPOP_M1				0x8e
#define RT5670_DEPOP_M2				0x8f
#define RT5670_DEPOP_M3				0x90
#define RT5670_CHARGE_PUMP			0x91
#define RT5670_MICBIAS				0x93
#define RT5670_A_JD_CTRL1			0x94
#define RT5670_A_JD_CTRL2			0x95
#define RT5670_ASRC_12				0x97
#define RT5670_ASRC_13				0x98
#define RT5670_ASRC_14				0x99
#define RT5670_VAD_CTRL1			0x9a
#define RT5670_VAD_CTRL2			0x9b
#define RT5670_VAD_CTRL3			0x9c
#define RT5670_VAD_CTRL4			0x9d
#define RT5670_VAD_CTRL5			0x9e
/* Function - Digital */
#define RT5670_ADC_EQ_CTRL1			0xae
#define RT5670_ADC_EQ_CTRL2			0xaf
#define RT5670_EQ_CTRL1				0xb0
#define RT5670_EQ_CTRL2				0xb1
#define RT5670_ALC_DRC_CTRL1			0xb2
#define RT5670_ALC_DRC_CTRL2			0xb3
#define RT5670_ALC_CTRL_1			0xb4
#define RT5670_ALC_CTRL_2			0xb5
#define RT5670_ALC_CTRL_3			0xb6
#define RT5670_ALC_CTRL_4			0xb7
#define RT5670_JD_CTRL				0xbb
#define RT5670_IRQ_CTRL1			0xbd
#define RT5670_IRQ_CTRL2			0xbe
#define RT5670_INT_IRQ_ST			0xbf
#define RT5670_GPIO_CTRL1			0xc0
#define RT5670_GPIO_CTRL2			0xc1
#define RT5670_GPIO_CTRL3			0xc2
#define RT5670_SCRABBLE_FUN			0xcd
#define RT5670_SCRABBLE_CTRL			0xce
#define RT5670_BASE_BACK			0xcf
#define RT5670_MP3_PLUS1			0xd0
#define RT5670_MP3_PLUS2			0xd1
#define RT5670_ADJ_HPF1				0xd3
#define RT5670_ADJ_HPF2				0xd4
#define RT5670_HP_CALIB_AMP_DET			0xd6
#define RT5670_SV_ZCD1				0xd9
#define RT5670_SV_ZCD2				0xda
#define RT5670_IL_CMD				0xdb
#define RT5670_IL_CMD2				0xdc
#define RT5670_IL_CMD3				0xdd
#define RT5670_DRC_HL_CTRL1			0xe6
#define RT5670_DRC_HL_CTRL2			0xe7
#define RT5670_ADC_MONO_HP_CTRL1		0xec
#define RT5670_ADC_MONO_HP_CTRL2		0xed
#define RT5670_ADC_STO2_HP_CTRL1		0xee
#define RT5670_ADC_STO2_HP_CTRL2		0xef
#define RT5670_JD_CTRL3				0xf8
#define RT5670_JD_CTRL4				0xf9
/* General Control */
#define RT5670_DIG_MISC				0xfa
#define RT5670_GEN_CTRL2			0xfb
#define RT5670_GEN_CTRL3			0xfc


/* Index of Codec Private Register definition */
#define RT5670_DIG_VOL				0x00
#define RT5670_PR_ALC_CTRL_1			0x01
#define RT5670_PR_ALC_CTRL_2			0x02
#define RT5670_PR_ALC_CTRL_3			0x03
#define RT5670_PR_ALC_CTRL_4			0x04
#define RT5670_PR_ALC_CTRL_5			0x05
#define RT5670_PR_ALC_CTRL_6			0x06
#define RT5670_BIAS_CUR1			0x12
#define RT5670_BIAS_CUR3			0x14
#define RT5670_CLSD_INT_REG1			0x1c
#define RT5670_MAMP_INT_REG2			0x37
#define RT5670_CHOP_DAC_ADC			0x3d
#define RT5670_MIXER_INT_REG			0x3f
#define RT5670_3D_SPK				0x63
#define RT5670_WND_1				0x6c
#define RT5670_WND_2				0x6d
#define RT5670_WND_3				0x6e
#define RT5670_WND_4				0x6f
#define RT5670_WND_5				0x70
#define RT5670_WND_8				0x73
#define RT5670_DIP_SPK_INF			0x75
#define RT5670_HP_DCC_INT1			0x77
#define RT5670_EQ_BW_LOP			0xa0
#define RT5670_EQ_GN_LOP			0xa1
#define RT5670_EQ_FC_BP1			0xa2
#define RT5670_EQ_BW_BP1			0xa3
#define RT5670_EQ_GN_BP1			0xa4
#define RT5670_EQ_FC_BP2			0xa5
#define RT5670_EQ_BW_BP2			0xa6
#define RT5670_EQ_GN_BP2			0xa7
#define RT5670_EQ_FC_BP3			0xa8
#define RT5670_EQ_BW_BP3			0xa9
#define RT5670_EQ_GN_BP3			0xaa
#define RT5670_EQ_FC_BP4			0xab
#define RT5670_EQ_BW_BP4			0xac
#define RT5670_EQ_GN_BP4			0xad
#define RT5670_EQ_FC_HIP1			0xae
#define RT5670_EQ_GN_HIP1			0xaf
#define RT5670_EQ_FC_HIP2			0xb0
#define RT5670_EQ_BW_HIP2			0xb1
#define RT5670_EQ_GN_HIP2			0xb2
#define RT5670_EQ_PRE_VOL			0xb3
#define RT5670_EQ_PST_VOL			0xb4


/* global definition */
#define RT5670_L_MUTE				(0x1 << 15)
#define RT5670_L_MUTE_SFT			15
#define RT5670_VOL_L_MUTE			(0x1 << 14)
#define RT5670_VOL_L_SFT			14
#define RT5670_R_MUTE				(0x1 << 7)
#define RT5670_R_MUTE_SFT			7
#define RT5670_VOL_R_MUTE			(0x1 << 6)
#define RT5670_VOL_R_SFT			6
#define RT5670_L_VOL_MASK			(0x3f << 8)
#define RT5670_L_VOL_SFT			8
#define RT5670_R_VOL_MASK			(0x3f)
#define RT5670_R_VOL_SFT			0

/* SW Reset & Device ID (0x00) */
#define RT5670_ID_MASK				(0x3 << 1)
#define RT5670_ID_5670				(0x0 << 1)
#define RT5670_ID_5672				(0x1 << 1)
#define RT5670_ID_5671				(0x2 << 1)

/* Combo Jack Control 1 (0x0a) */
#define RT5670_CBJ_BST1_MASK			(0xf << 12)
#define RT5670_CBJ_BST1_SFT			(12)
#define RT5670_CBJ_JD_HP_EN			(0x1 << 9)
#define RT5670_CBJ_JD_MIC_EN			(0x1 << 8)
#define RT5670_CBJ_BST1_EN			(0x1 << 2)

/* Combo Jack Control 1 (0x0b) */
#define RT5670_CBJ_MN_JD			(0x1 << 12)
#define RT5670_CAPLESS_EN			(0x1 << 11)
#define RT5670_CBJ_DET_MODE			(0x1 << 7)

/* IN2 Control (0x0e) */
#define RT5670_BST_MASK1			(0xf<<12)
#define RT5670_BST_SFT1				12
#define RT5670_BST_MASK2			(0xf<<8)
#define RT5670_BST_SFT2				8
#define RT5670_IN_DF1				(0x1 << 7)
#define RT5670_IN_SFT1				7
#define RT5670_IN_DF2				(0x1 << 6)
#define RT5670_IN_SFT2				6

/* INL and INR Volume Control (0x0f) */
#define RT5670_INL_SEL_MASK			(0x1 << 15)
#define RT5670_INL_SEL_SFT			15
#define RT5670_INL_SEL_IN4P			(0x0 << 15)
#define RT5670_INL_SEL_MONOP			(0x1 << 15)
#define RT5670_INL_VOL_MASK			(0x1f << 8)
#define RT5670_INL_VOL_SFT			8
#define RT5670_INR_SEL_MASK			(0x1 << 7)
#define RT5670_INR_SEL_SFT			7
#define RT5670_INR_SEL_IN4N			(0x0 << 7)
#define RT5670_INR_SEL_MONON			(0x1 << 7)
#define RT5670_INR_VOL_MASK			(0x1f)
#define RT5670_INR_VOL_SFT			0

/* Sidetone Control (0x18) */
#define RT5670_ST_SEL_MASK			(0x7 << 9)
#define RT5670_ST_SEL_SFT			9
#define RT5670_M_ST_DACR2			(0x1 << 8)
#define RT5670_M_ST_DACR2_SFT			8
#define RT5670_M_ST_DACL2			(0x1 << 7)
#define RT5670_M_ST_DACL2_SFT			7
#define RT5670_ST_EN				(0x1 << 6)
#define RT5670_ST_EN_SFT			6

/* DAC1 Digital Volume (0x19) */
#define RT5670_DAC_L1_VOL_MASK			(0xff << 8)
#define RT5670_DAC_L1_VOL_SFT			8
#define RT5670_DAC_R1_VOL_MASK			(0xff)
#define RT5670_DAC_R1_VOL_SFT			0

/* DAC2 Digital Volume (0x1a) */
#define RT5670_DAC_L2_VOL_MASK			(0xff << 8)
#define RT5670_DAC_L2_VOL_SFT			8
#define RT5670_DAC_R2_VOL_MASK			(0xff)
#define RT5670_DAC_R2_VOL_SFT			0

/* DAC2 Control (0x1b) */
#define RT5670_M_DAC_L2_VOL			(0x1 << 13)
#define RT5670_M_DAC_L2_VOL_SFT			13
#define RT5670_M_DAC_R2_VOL			(0x1 << 12)
#define RT5670_M_DAC_R2_VOL_SFT			12
#define RT5670_DAC2_L_SEL_MASK			(0x7 << 4)
#define RT5670_DAC2_L_SEL_SFT			4
#define RT5670_DAC2_R_SEL_MASK			(0x7 << 0)
#define RT5670_DAC2_R_SEL_SFT			0

/* ADC Digital Volume Control (0x1c) */
#define RT5670_ADC_L_VOL_MASK			(0x7f << 8)
#define RT5670_ADC_L_VOL_SFT			8
#define RT5670_ADC_R_VOL_MASK			(0x7f)
#define RT5670_ADC_R_VOL_SFT			0

/* Mono ADC Digital Volume Control (0x1d) */
#define RT5670_MONO_ADC_L_VOL_MASK		(0x7f << 8)
#define RT5670_MONO_ADC_L_VOL_SFT		8
#define RT5670_MONO_ADC_R_VOL_MASK		(0x7f)
#define RT5670_MONO_ADC_R_VOL_SFT		0

/* ADC Boost Volume Control (0x1e) */
#define RT5670_STO1_ADC_L_BST_MASK		(0x3 << 14)
#define RT5670_STO1_ADC_L_BST_SFT		14
#define RT5670_STO1_ADC_R_BST_MASK		(0x3 << 12)
#define RT5670_STO1_ADC_R_BST_SFT		12
#define RT5670_STO1_ADC_COMP_MASK		(0x3 << 10)
#define RT5670_STO1_ADC_COMP_SFT		10
#define RT5670_STO2_ADC_L_BST_MASK		(0x3 << 8)
#define RT5670_STO2_ADC_L_BST_SFT		8
#define RT5670_STO2_ADC_R_BST_MASK		(0x3 << 6)
#define RT5670_STO2_ADC_R_BST_SFT		6
#define RT5670_STO2_ADC_COMP_MASK		(0x3 << 4)
#define RT5670_STO2_ADC_COMP_SFT		4

/* Stereo2 ADC Mixer Control (0x26) */
#define RT5670_STO2_ADC_SRC_MASK		(0x1 << 15)
#define RT5670_STO2_ADC_SRC_SFT			15

/* Stereo ADC Mixer Control (0x26 0x27) */
#define RT5670_M_ADC_L1				(0x1 << 14)
#define RT5670_M_ADC_L1_SFT			14
#define RT5670_M_ADC_L2				(0x1 << 13)
#define RT5670_M_ADC_L2_SFT			13
#define RT5670_ADC_1_SRC_MASK			(0x1 << 12)
#define RT5670_ADC_1_SRC_SFT			12
#define RT5670_ADC_1_SRC_ADC			(0x1 << 12)
#define RT5670_ADC_1_SRC_DACMIX			(0x0 << 12)
#define RT5670_ADC_2_SRC_MASK			(0x1 << 11)
#define RT5670_ADC_2_SRC_SFT			11
#define RT5670_ADC_SRC_MASK			(0x1 << 10)
#define RT5670_ADC_SRC_SFT			10
#define RT5670_DMIC_SRC_MASK			(0x3 << 8)
#define RT5670_DMIC_SRC_SFT			8
#define RT5670_M_ADC_R1				(0x1 << 6)
#define RT5670_M_ADC_R1_SFT			6
#define RT5670_M_ADC_R2				(0x1 << 5)
#define RT5670_M_ADC_R2_SFT			5
#define RT5670_DMIC3_SRC_MASK			(0x1 << 1)
#define RT5670_DMIC3_SRC_SFT			0

/* Mono ADC Mixer Control (0x28) */
#define RT5670_M_MONO_ADC_L1			(0x1 << 14)
#define RT5670_M_MONO_ADC_L1_SFT		14
#define RT5670_M_MONO_ADC_L2			(0x1 << 13)
#define RT5670_M_MONO_ADC_L2_SFT		13
#define RT5670_MONO_ADC_L1_SRC_MASK		(0x1 << 12)
#define RT5670_MONO_ADC_L1_SRC_SFT		12
#define RT5670_MONO_ADC_L1_SRC_DACMIXL		(0x0 << 12)
#define RT5670_MONO_ADC_L1_SRC_ADCL		(0x1 << 12)
#define RT5670_MONO_ADC_L2_SRC_MASK		(0x1 << 11)
#define RT5670_MONO_ADC_L2_SRC_SFT		11
#define RT5670_MONO_ADC_L_SRC_MASK		(0x1 << 10)
#define RT5670_MONO_ADC_L_SRC_SFT		10
#define RT5670_MONO_DMIC_L_SRC_MASK		(0x3 << 8)
#define RT5670_MONO_DMIC_L_SRC_SFT		8
#define RT5670_M_MONO_ADC_R1			(0x1 << 6)
#define RT5670_M_MONO_ADC_R1_SFT		6
#define RT5670_M_MONO_ADC_R2			(0x1 << 5)
#define RT5670_M_MONO_ADC_R2_SFT		5
#define RT5670_MONO_ADC_R1_SRC_MASK		(0x1 << 4)
#define RT5670_MONO_ADC_R1_SRC_SFT		4
#define RT5670_MONO_ADC_R1_SRC_ADCR		(0x1 << 4)
#define RT5670_MONO_ADC_R1_SRC_DACMIXR		(0x0 << 4)
#define RT5670_MONO_ADC_R2_SRC_MASK		(0x1 << 3)
#define RT5670_MONO_ADC_R2_SRC_SFT		3
#define RT5670_MONO_DMIC_R_SRC_MASK		(0x3)
#define RT5670_MONO_DMIC_R_SRC_SFT		0

/* ADC Mixer to DAC Mixer Control (0x29) */
#define RT5670_M_ADCMIX_L			(0x1 << 15)
#define RT5670_M_ADCMIX_L_SFT			15
#define RT5670_M_DAC1_L				(0x1 << 14)
#define RT5670_M_DAC1_L_SFT			14
#define RT5670_DAC1_R_SEL_MASK			(0x3 << 10)
#define RT5670_DAC1_R_SEL_SFT			10
#define RT5670_DAC1_R_SEL_IF1			(0x0 << 10)
#define RT5670_DAC1_R_SEL_IF2			(0x1 << 10)
#define RT5670_DAC1_R_SEL_IF3			(0x2 << 10)
#define RT5670_DAC1_R_SEL_IF4			(0x3 << 10)
#define RT5670_DAC1_L_SEL_MASK			(0x3 << 8)
#define RT5670_DAC1_L_SEL_SFT			8
#define RT5670_DAC1_L_SEL_IF1			(0x0 << 8)
#define RT5670_DAC1_L_SEL_IF2			(0x1 << 8)
#define RT5670_DAC1_L_SEL_IF3			(0x2 << 8)
#define RT5670_DAC1_L_SEL_IF4			(0x3 << 8)
#define RT5670_M_ADCMIX_R			(0x1 << 7)
#define RT5670_M_ADCMIX_R_SFT			7
#define RT5670_M_DAC1_R				(0x1 << 6)
#define RT5670_M_DAC1_R_SFT			6

/* Stereo DAC Mixer Control (0x2a) */
#define RT5670_M_DAC_L1				(0x1 << 14)
#define RT5670_M_DAC_L1_SFT			14
#define RT5670_DAC_L1_STO_L_VOL_MASK		(0x1 << 13)
#define RT5670_DAC_L1_STO_L_VOL_SFT		13
#define RT5670_M_DAC_L2				(0x1 << 12)
#define RT5670_M_DAC_L2_SFT			12
#define RT5670_DAC_L2_STO_L_VOL_MASK		(0x1 << 11)
#define RT5670_DAC_L2_STO_L_VOL_SFT		11
#define RT5670_M_DAC_R1_STO_L			(0x1 << 9)
#define RT5670_M_DAC_R1_STO_L_SFT		9
#define RT5670_DAC_R1_STO_L_VOL_MASK		(0x1 << 8)
#define RT5670_DAC_R1_STO_L_VOL_SFT		8
#define RT5670_M_DAC_R1				(0x1 << 6)
#define RT5670_M_DAC_R1_SFT			6
#define RT5670_DAC_R1_STO_R_VOL_MASK		(0x1 << 5)
#define RT5670_DAC_R1_STO_R_VOL_SFT		5
#define RT5670_M_DAC_R2				(0x1 << 4)
#define RT5670_M_DAC_R2_SFT			4
#define RT5670_DAC_R2_STO_R_VOL_MASK		(0x1 << 3)
#define RT5670_DAC_R2_STO_R_VOL_SFT		3
#define RT5670_M_DAC_L1_STO_R			(0x1 << 1)
#define RT5670_M_DAC_L1_STO_R_SFT		1
#define RT5670_DAC_L1_STO_R_VOL_MASK		(0x1)
#define RT5670_DAC_L1_STO_R_VOL_SFT		0

/* Mono DAC Mixer Control (0x2b) */
#define RT5670_M_DAC_L1_MONO_L			(0x1 << 14)
#define RT5670_M_DAC_L1_MONO_L_SFT		14
#define RT5670_DAC_L1_MONO_L_VOL_MASK		(0x1 << 13)
#define RT5670_DAC_L1_MONO_L_VOL_SFT		13
#define RT5670_M_DAC_L2_MONO_L			(0x1 << 12)
#define RT5670_M_DAC_L2_MONO_L_SFT		12
#define RT5670_DAC_L2_MONO_L_VOL_MASK		(0x1 << 11)
#define RT5670_DAC_L2_MONO_L_VOL_SFT		11
#define RT5670_M_DAC_R2_MONO_L			(0x1 << 10)
#define RT5670_M_DAC_R2_MONO_L_SFT		10
#define RT5670_DAC_R2_MONO_L_VOL_MASK		(0x1 << 9)
#define RT5670_DAC_R2_MONO_L_VOL_SFT		9
#define RT5670_M_DAC_R1_MONO_R			(0x1 << 6)
#define RT5670_M_DAC_R1_MONO_R_SFT		6
#define RT5670_DAC_R1_MONO_R_VOL_MASK		(0x1 << 5)
#define RT5670_DAC_R1_MONO_R_VOL_SFT		5
#define RT5670_M_DAC_R2_MONO_R			(0x1 << 4)
#define RT5670_M_DAC_R2_MONO_R_SFT		4
#define RT5670_DAC_R2_MONO_R_VOL_MASK		(0x1 << 3)
#define RT5670_DAC_R2_MONO_R_VOL_SFT		3
#define RT5670_M_DAC_L2_MONO_R			(0x1 << 2)
#define RT5670_M_DAC_L2_MONO_R_SFT		2
#define RT5670_DAC_L2_MONO_R_VOL_MASK		(0x1 << 1)
#define RT5670_DAC_L2_MONO_R_VOL_SFT		1

/* Digital Mixer Control (0x2c) */
#define RT5670_M_STO_L_DAC_L			(0x1 << 15)
#define RT5670_M_STO_L_DAC_L_SFT		15
#define RT5670_STO_L_DAC_L_VOL_MASK		(0x1 << 14)
#define RT5670_STO_L_DAC_L_VOL_SFT		14
#define RT5670_M_DAC_L2_DAC_L			(0x1 << 13)
#define RT5670_M_DAC_L2_DAC_L_SFT		13
#define RT5670_DAC_L2_DAC_L_VOL_MASK		(0x1 << 12)
#define RT5670_DAC_L2_DAC_L_VOL_SFT		12
#define RT5670_M_STO_R_DAC_R			(0x1 << 11)
#define RT5670_M_STO_R_DAC_R_SFT		11
#define RT5670_STO_R_DAC_R_VOL_MASK		(0x1 << 10)
#define RT5670_STO_R_DAC_R_VOL_SFT		10
#define RT5670_M_DAC_R2_DAC_R			(0x1 << 9)
#define RT5670_M_DAC_R2_DAC_R_SFT		9
#define RT5670_DAC_R2_DAC_R_VOL_MASK		(0x1 << 8)
#define RT5670_DAC_R2_DAC_R_VOL_SFT		8
#define RT5670_M_DAC_R2_DAC_L			(0x1 << 7)
#define RT5670_M_DAC_R2_DAC_L_SFT		7
#define RT5670_DAC_R2_DAC_L_VOL_MASK		(0x1 << 6)
#define RT5670_DAC_R2_DAC_L_VOL_SFT		6
#define RT5670_M_DAC_L2_DAC_R			(0x1 << 5)
#define RT5670_M_DAC_L2_DAC_R_SFT		5
#define RT5670_DAC_L2_DAC_R_VOL_MASK		(0x1 << 4)
#define RT5670_DAC_L2_DAC_R_VOL_SFT		4

/* DSP Path Control 1 (0x2d) */
#define RT5670_RXDP_SEL_MASK			(0x7 << 13)
#define RT5670_RXDP_SEL_SFT			13
#define RT5670_RXDP_SRC_MASK			(0x3 << 11)
#define RT5670_RXDP_SRC_SFT			11
#define RT5670_RXDP_SRC_NOR			(0x0 << 11)
#define RT5670_RXDP_SRC_DIV2			(0x1 << 11)
#define RT5670_RXDP_SRC_DIV3			(0x2 << 11)
#define RT5670_TXDP_SRC_MASK			(0x3 << 4)
#define RT5670_TXDP_SRC_SFT			4
#define RT5670_TXDP_SRC_NOR			(0x0 << 4)
#define RT5670_TXDP_SRC_DIV2			(0x1 << 4)
#define RT5670_TXDP_SRC_DIV3			(0x2 << 4)
#define RT5670_TXDP_SLOT_SEL_MASK		(0x3 << 2)
#define RT5670_TXDP_SLOT_SEL_SFT		2
#define RT5670_DSP_UL_SEL			(0x1 << 1)
#define RT5670_DSP_UL_SFT			1
#define RT5670_DSP_DL_SEL			0x1
#define RT5670_DSP_DL_SFT			0

/* DSP Path Control 2 (0x2e) */
#define RT5670_TXDP_L_VOL_MASK			(0x7f << 8)
#define RT5670_TXDP_L_VOL_SFT			8
#define RT5670_TXDP_R_VOL_MASK			(0x7f)
#define RT5670_TXDP_R_VOL_SFT			0

/* Digital Interface Data Control (0x2f) */
#define RT5670_IF1_ADC2_IN_SEL			(0x1 << 15)
#define RT5670_IF1_ADC2_IN_SFT			15
#define RT5670_IF2_ADC_IN_MASK			(0x7 << 12)
#define RT5670_IF2_ADC_IN_SFT			12
#define RT5670_IF2_DAC_SEL_MASK			(0x3 << 10)
#define RT5670_IF2_DAC_SEL_SFT			10
#define RT5670_IF2_ADC_SEL_MASK			(0x3 << 8)
#define RT5670_IF2_ADC_SEL_SFT			8

/* Digital Interface Data Control (0x30) */
#define RT5670_IF4_ADC_IN_MASK			(0x3 << 4)
#define RT5670_IF4_ADC_IN_SFT			4

/* PDM Output Control (0x31) */
#define RT5670_PDM1_L_MASK			(0x1 << 15)
#define RT5670_PDM1_L_SFT			15
#define RT5670_M_PDM1_L				(0x1 << 14)
#define RT5670_M_PDM1_L_SFT			14
#define RT5670_PDM1_R_MASK			(0x1 << 13)
#define RT5670_PDM1_R_SFT			13
#define RT5670_M_PDM1_R				(0x1 << 12)
#define RT5670_M_PDM1_R_SFT			12
#define RT5670_PDM2_L_MASK			(0x1 << 11)
#define RT5670_PDM2_L_SFT			11
#define RT5670_M_PDM2_L				(0x1 << 10)
#define RT5670_M_PDM2_L_SFT			10
#define RT5670_PDM2_R_MASK			(0x1 << 9)
#define RT5670_PDM2_R_SFT			9
#define RT5670_M_PDM2_R				(0x1 << 8)
#define RT5670_M_PDM2_R_SFT			8
#define RT5670_PDM2_BUSY			(0x1 << 7)
#define RT5670_PDM1_BUSY			(0x1 << 6)
#define RT5670_PDM_PATTERN			(0x1 << 5)
#define RT5670_PDM_GAIN				(0x1 << 4)
#define RT5670_PDM_DIV_MASK			(0x3)

/* REC Left Mixer Control 1 (0x3b) */
#define RT5670_G_HP_L_RM_L_MASK			(0x7 << 13)
#define RT5670_G_HP_L_RM_L_SFT			13
#define RT5670_G_IN_L_RM_L_MASK			(0x7 << 10)
#define RT5670_G_IN_L_RM_L_SFT			10
#define RT5670_G_BST4_RM_L_MASK			(0x7 << 7)
#define RT5670_G_BST4_RM_L_SFT			7
#define RT5670_G_BST3_RM_L_MASK			(0x7 << 4)
#define RT5670_G_BST3_RM_L_SFT			4
#define RT5670_G_BST2_RM_L_MASK			(0x7 << 1)
#define RT5670_G_BST2_RM_L_SFT			1

/* REC Left Mixer Control 2 (0x3c) */
#define RT5670_G_BST1_RM_L_MASK			(0x7 << 13)
#define RT5670_G_BST1_RM_L_SFT			13
#define RT5670_M_IN_L_RM_L			(0x1 << 5)
#define RT5670_M_IN_L_RM_L_SFT			5
#define RT5670_M_BST2_RM_L			(0x1 << 3)
#define RT5670_M_BST2_RM_L_SFT			3
#define RT5670_M_BST1_RM_L			(0x1 << 1)
#define RT5670_M_BST1_RM_L_SFT			1

/* REC Right Mixer Control 1 (0x3d) */
#define RT5670_G_HP_R_RM_R_MASK			(0x7 << 13)
#define RT5670_G_HP_R_RM_R_SFT			13
#define RT5670_G_IN_R_RM_R_MASK			(0x7 << 10)
#define RT5670_G_IN_R_RM_R_SFT			10
#define RT5670_G_BST4_RM_R_MASK			(0x7 << 7)
#define RT5670_G_BST4_RM_R_SFT			7
#define RT5670_G_BST3_RM_R_MASK			(0x7 << 4)
#define RT5670_G_BST3_RM_R_SFT			4
#define RT5670_G_BST2_RM_R_MASK			(0x7 << 1)
#define RT5670_G_BST2_RM_R_SFT			1

/* REC Right Mixer Control 2 (0x3e) */
#define RT5670_G_BST1_RM_R_MASK			(0x7 << 13)
#define RT5670_G_BST1_RM_R_SFT			13
#define RT5670_M_IN_R_RM_R			(0x1 << 5)
#define RT5670_M_IN_R_RM_R_SFT			5
#define RT5670_M_BST2_RM_R			(0x1 << 3)
#define RT5670_M_BST2_RM_R_SFT			3
#define RT5670_M_BST1_RM_R			(0x1 << 1)
#define RT5670_M_BST1_RM_R_SFT			1

/* HPMIX Control (0x45) */
#define RT5670_M_DAC2_HM			(0x1 << 15)
#define RT5670_M_DAC2_HM_SFT			15
#define RT5670_M_HPVOL_HM			(0x1 << 14)
#define RT5670_M_HPVOL_HM_SFT			14
#define RT5670_M_DAC1_HM			(0x1 << 13)
#define RT5670_M_DAC1_HM_SFT			13
#define RT5670_G_HPOMIX_MASK			(0x1 << 12)
#define RT5670_G_HPOMIX_SFT			12
#define RT5670_M_INR1_HMR			(0x1 << 3)
#define RT5670_M_INR1_HMR_SFT			3
#define RT5670_M_DACR1_HMR			(0x1 << 2)
#define RT5670_M_DACR1_HMR_SFT			2
#define RT5670_M_INL1_HML			(0x1 << 1)
#define RT5670_M_INL1_HML_SFT			1
#define RT5670_M_DACL1_HML			(0x1)
#define RT5670_M_DACL1_HML_SFT			0

/* Mono Output Mixer Control (0x4c) */
#define RT5670_M_DAC_R2_MA			(0x1 << 15)
#define RT5670_M_DAC_R2_MA_SFT			15
#define RT5670_M_DAC_L2_MA			(0x1 << 14)
#define RT5670_M_DAC_L2_MA_SFT			14
#define RT5670_M_OV_R_MM			(0x1 << 13)
#define RT5670_M_OV_R_MM_SFT			13
#define RT5670_M_OV_L_MM			(0x1 << 12)
#define RT5670_M_OV_L_MM_SFT			12
#define RT5670_G_MONOMIX_MASK			(0x1 << 10)
#define RT5670_G_MONOMIX_SFT			10
#define RT5670_M_DAC_R2_MM			(0x1 << 9)
#define RT5670_M_DAC_R2_MM_SFT			9
#define RT5670_M_DAC_L2_MM			(0x1 << 8)
#define RT5670_M_DAC_L2_MM_SFT			8
#define RT5670_M_BST4_MM			(0x1 << 7)
#define RT5670_M_BST4_MM_SFT			7

/* Output Left Mixer Control 1 (0x4d) */
#define RT5670_G_BST3_OM_L_MASK			(0x7 << 13)
#define RT5670_G_BST3_OM_L_SFT			13
#define RT5670_G_BST2_OM_L_MASK			(0x7 << 10)
#define RT5670_G_BST2_OM_L_SFT			10
#define RT5670_G_BST1_OM_L_MASK			(0x7 << 7)
#define RT5670_G_BST1_OM_L_SFT			7
#define RT5670_G_IN_L_OM_L_MASK			(0x7 << 4)
#define RT5670_G_IN_L_OM_L_SFT			4
#define RT5670_G_RM_L_OM_L_MASK			(0x7 << 1)
#define RT5670_G_RM_L_OM_L_SFT			1

/* Output Left Mixer Control 2 (0x4e) */
#define RT5670_G_DAC_R2_OM_L_MASK		(0x7 << 13)
#define RT5670_G_DAC_R2_OM_L_SFT		13
#define RT5670_G_DAC_L2_OM_L_MASK		(0x7 << 10)
#define RT5670_G_DAC_L2_OM_L_SFT		10
#define RT5670_G_DAC_L1_OM_L_MASK		(0x7 << 7)
#define RT5670_G_DAC_L1_OM_L_SFT		7

/* Output Left Mixer Control 3 (0x4f) */
#define RT5670_M_BST1_OM_L			(0x1 << 5)
#define RT5670_M_BST1_OM_L_SFT			5
#define RT5670_M_IN_L_OM_L			(0x1 << 4)
#define RT5670_M_IN_L_OM_L_SFT			4
#define RT5670_M_DAC_L2_OM_L			(0x1 << 1)
#define RT5670_M_DAC_L2_OM_L_SFT		1
#define RT5670_M_DAC_L1_OM_L			(0x1)
#define RT5670_M_DAC_L1_OM_L_SFT		0

/* Output Right Mixer Control 1 (0x50) */
#define RT5670_G_BST4_OM_R_MASK			(0x7 << 13)
#define RT5670_G_BST4_OM_R_SFT			13
#define RT5670_G_BST2_OM_R_MASK			(0x7 << 10)
#define RT5670_G_BST2_OM_R_SFT			10
#define RT5670_G_BST1_OM_R_MASK			(0x7 << 7)
#define RT5670_G_BST1_OM_R_SFT			7
#define RT5670_G_IN_R_OM_R_MASK			(0x7 << 4)
#define RT5670_G_IN_R_OM_R_SFT			4
#define RT5670_G_RM_R_OM_R_MASK			(0x7 << 1)
#define RT5670_G_RM_R_OM_R_SFT			1

/* Output Right Mixer Control 2 (0x51) */
#define RT5670_G_DAC_L2_OM_R_MASK		(0x7 << 13)
#define RT5670_G_DAC_L2_OM_R_SFT		13
#define RT5670_G_DAC_R2_OM_R_MASK		(0x7 << 10)
#define RT5670_G_DAC_R2_OM_R_SFT		10
#define RT5670_G_DAC_R1_OM_R_MASK		(0x7 << 7)
#define RT5670_G_DAC_R1_OM_R_SFT		7

/* Output Right Mixer Control 3 (0x52) */
#define RT5670_M_BST2_OM_R			(0x1 << 6)
#define RT5670_M_BST2_OM_R_SFT			6
#define RT5670_M_IN_R_OM_R			(0x1 << 4)
#define RT5670_M_IN_R_OM_R_SFT			4
#define RT5670_M_DAC_R2_OM_R			(0x1 << 1)
#define RT5670_M_DAC_R2_OM_R_SFT		1
#define RT5670_M_DAC_R1_OM_R			(0x1)
#define RT5670_M_DAC_R1_OM_R_SFT		0

/* LOUT Mixer Control (0x53) */
#define RT5670_M_DAC_L1_LM			(0x1 << 15)
#define RT5670_M_DAC_L1_LM_SFT			15
#define RT5670_M_DAC_R1_LM			(0x1 << 14)
#define RT5670_M_DAC_R1_LM_SFT			14
#define RT5670_M_OV_L_LM			(0x1 << 13)
#define RT5670_M_OV_L_LM_SFT			13
#define RT5670_M_OV_R_LM			(0x1 << 12)
#define RT5670_M_OV_R_LM_SFT			12
#define RT5670_G_LOUTMIX_MASK			(0x1 << 11)
#define RT5670_G_LOUTMIX_SFT			11

/* Power Management for Digital 1 (0x61) */
#define RT5670_PWR_I2S1				(0x1 << 15)
#define RT5670_PWR_I2S1_BIT			15
#define RT5670_PWR_I2S2				(0x1 << 14)
#define RT5670_PWR_I2S2_BIT			14
#define RT5670_PWR_DAC_L1			(0x1 << 12)
#define RT5670_PWR_DAC_L1_BIT			12
#define RT5670_PWR_DAC_R1			(0x1 << 11)
#define RT5670_PWR_DAC_R1_BIT			11
#define RT5670_PWR_DAC_L2			(0x1 << 7)
#define RT5670_PWR_DAC_L2_BIT			7
#define RT5670_PWR_DAC_R2			(0x1 << 6)
#define RT5670_PWR_DAC_R2_BIT			6
#define RT5670_PWR_ADC_L			(0x1 << 2)
#define RT5670_PWR_ADC_L_BIT			2
#define RT5670_PWR_ADC_R			(0x1 << 1)
#define RT5670_PWR_ADC_R_BIT			1
#define RT5670_PWR_CLS_D			(0x1)
#define RT5670_PWR_CLS_D_BIT			0

/* Power Management for Digital 2 (0x62) */
#define RT5670_PWR_ADC_S1F			(0x1 << 15)
#define RT5670_PWR_ADC_S1F_BIT			15
#define RT5670_PWR_ADC_MF_L			(0x1 << 14)
#define RT5670_PWR_ADC_MF_L_BIT			14
#define RT5670_PWR_ADC_MF_R			(0x1 << 13)
#define RT5670_PWR_ADC_MF_R_BIT			13
#define RT5670_PWR_I2S_DSP			(0x1 << 12)
#define RT5670_PWR_I2S_DSP_BIT			12
#define RT5670_PWR_DAC_S1F			(0x1 << 11)
#define RT5670_PWR_DAC_S1F_BIT			11
#define RT5670_PWR_DAC_MF_L			(0x1 << 10)
#define RT5670_PWR_DAC_MF_L_BIT			10
#define RT5670_PWR_DAC_MF_R			(0x1 << 9)
#define RT5670_PWR_DAC_MF_R_BIT			9
#define RT5670_PWR_ADC_S2F			(0x1 << 8)
#define RT5670_PWR_ADC_S2F_BIT			8
#define RT5670_PWR_PDM1				(0x1 << 7)
#define RT5670_PWR_PDM1_BIT			7
#define RT5670_PWR_PDM2				(0x1 << 6)
#define RT5670_PWR_PDM2_BIT			6

/* Power Management for Analog 1 (0x63) */
#define RT5670_PWR_VREF1			(0x1 << 15)
#define RT5670_PWR_VREF1_BIT			15
#define RT5670_PWR_FV1				(0x1 << 14)
#define RT5670_PWR_FV1_BIT			14
#define RT5670_PWR_MB				(0x1 << 13)
#define RT5670_PWR_MB_BIT			13
#define RT5670_PWR_LM				(0x1 << 12)
#define RT5670_PWR_LM_BIT			12
#define RT5670_PWR_BG				(0x1 << 11)
#define RT5670_PWR_BG_BIT			11
#define RT5670_PWR_HP_L				(0x1 << 7)
#define RT5670_PWR_HP_L_BIT			7
#define RT5670_PWR_HP_R				(0x1 << 6)
#define RT5670_PWR_HP_R_BIT			6
#define RT5670_PWR_HA				(0x1 << 5)
#define RT5670_PWR_HA_BIT			5
#define RT5670_PWR_VREF2			(0x1 << 4)
#define RT5670_PWR_VREF2_BIT			4
#define RT5670_PWR_FV2				(0x1 << 3)
#define RT5670_PWR_FV2_BIT			3
#define RT5670_LDO_SEL_MASK			(0x7)
#define RT5670_LDO_SEL_SFT			0

/* Power Management for Analog 2 (0x64) */
#define RT5670_PWR_BST1				(0x1 << 15)
#define RT5670_PWR_BST1_BIT			15
#define RT5670_PWR_BST2				(0x1 << 13)
#define RT5670_PWR_BST2_BIT			13
#define RT5670_PWR_MB1				(0x1 << 11)
#define RT5670_PWR_MB1_BIT			11
#define RT5670_PWR_MB2				(0x1 << 10)
#define RT5670_PWR_MB2_BIT			10
#define RT5670_PWR_PLL				(0x1 << 9)
#define RT5670_PWR_PLL_BIT			9
#define RT5670_PWR_BST1_P			(0x1 << 6)
#define RT5670_PWR_BST1_P_BIT			6
#define RT5670_PWR_BST2_P			(0x1 << 4)
#define RT5670_PWR_BST2_P_BIT			4
#define RT5670_PWR_JD1				(0x1 << 2)
#define RT5670_PWR_JD1_BIT			2
#define RT5670_PWR_JD				(0x1 << 1)
#define RT5670_PWR_JD_BIT			1

/* Power Management for Mixer (0x65) */
#define RT5670_PWR_OM_L				(0x1 << 15)
#define RT5670_PWR_OM_L_BIT			15
#define RT5670_PWR_OM_R				(0x1 << 14)
#define RT5670_PWR_OM_R_BIT			14
#define RT5670_PWR_RM_L				(0x1 << 11)
#define RT5670_PWR_RM_L_BIT			11
#define RT5670_PWR_RM_R				(0x1 << 10)
#define RT5670_PWR_RM_R_BIT			10

/* Power Management for Volume (0x66) */
#define RT5670_PWR_HV_L				(0x1 << 11)
#define RT5670_PWR_HV_L_BIT			11
#define RT5670_PWR_HV_R				(0x1 << 10)
#define RT5670_PWR_HV_R_BIT			10
#define RT5670_PWR_IN_L				(0x1 << 9)
#define RT5670_PWR_IN_L_BIT			9
#define RT5670_PWR_IN_R				(0x1 << 8)
#define RT5670_PWR_IN_R_BIT			8
#define RT5670_PWR_MIC_DET			(0x1 << 5)
#define RT5670_PWR_MIC_DET_BIT			5

/* I2S1/2/3 Audio Serial Data Port Control (0x70 0x71 0x72) */
#define RT5670_I2S_MS_MASK			(0x1 << 15)
#define RT5670_I2S_MS_SFT			15
#define RT5670_I2S_MS_M				(0x0 << 15)
#define RT5670_I2S_MS_S				(0x1 << 15)
#define RT5670_I2S_IF_MASK			(0x7 << 12)
#define RT5670_I2S_IF_SFT			12
#define RT5670_I2S_O_CP_MASK			(0x3 << 10)
#define RT5670_I2S_O_CP_SFT			10
#define RT5670_I2S_O_CP_OFF			(0x0 << 10)
#define RT5670_I2S_O_CP_U_LAW			(0x1 << 10)
#define RT5670_I2S_O_CP_A_LAW			(0x2 << 10)
#define RT5670_I2S_I_CP_MASK			(0x3 << 8)
#define RT5670_I2S_I_CP_SFT			8
#define RT5670_I2S_I_CP_OFF			(0x0 << 8)
#define RT5670_I2S_I_CP_U_LAW			(0x1 << 8)
#define RT5670_I2S_I_CP_A_LAW			(0x2 << 8)
#define RT5670_I2S_BP_MASK			(0x1 << 7)
#define RT5670_I2S_BP_SFT			7
#define RT5670_I2S_BP_NOR			(0x0 << 7)
#define RT5670_I2S_BP_INV			(0x1 << 7)
#define RT5670_I2S_DL_MASK			(0x3 << 2)
#define RT5670_I2S_DL_SFT			2
#define RT5670_I2S_DL_16			(0x0 << 2)
#define RT5670_I2S_DL_20			(0x1 << 2)
#define RT5670_I2S_DL_24			(0x2 << 2)
#define RT5670_I2S_DL_8				(0x3 << 2)
#define RT5670_I2S_DF_MASK			(0x3)
#define RT5670_I2S_DF_SFT			0
#define RT5670_I2S_DF_I2S			(0x0)
#define RT5670_I2S_DF_LEFT			(0x1)
#define RT5670_I2S_DF_PCM_A			(0x2)
#define RT5670_I2S_DF_PCM_B			(0x3)

/* I2S2 Audio Serial Data Port Control (0x71) */
#define RT5670_I2S2_SDI_MASK			(0x1 << 6)
#define RT5670_I2S2_SDI_SFT			6
#define RT5670_I2S2_SDI_I2S1			(0x0 << 6)
#define RT5670_I2S2_SDI_I2S2			(0x1 << 6)

/* ADC/DAC Clock Control 1 (0x73) */
#define RT5670_I2S_BCLK_MS1_MASK		(0x1 << 15)
#define RT5670_I2S_BCLK_MS1_SFT			15
#define RT5670_I2S_BCLK_MS1_32			(0x0 << 15)
#define RT5670_I2S_BCLK_MS1_64			(0x1 << 15)
#define RT5670_I2S_PD1_MASK			(0x7 << 12)
#define RT5670_I2S_PD1_SFT			12
#define RT5670_I2S_PD1_1			(0x0 << 12)
#define RT5670_I2S_PD1_2			(0x1 << 12)
#define RT5670_I2S_PD1_3			(0x2 << 12)
#define RT5670_I2S_PD1_4			(0x3 << 12)
#define RT5670_I2S_PD1_6			(0x4 << 12)
#define RT5670_I2S_PD1_8			(0x5 << 12)
#define RT5670_I2S_PD1_12			(0x6 << 12)
#define RT5670_I2S_PD1_16			(0x7 << 12)
#define RT5670_I2S_BCLK_MS2_MASK		(0x1 << 11)
#define RT5670_I2S_BCLK_MS2_SFT			11
#define RT5670_I2S_BCLK_MS2_32			(0x0 << 11)
#define RT5670_I2S_BCLK_MS2_64			(0x1 << 11)
#define RT5670_I2S_PD2_MASK			(0x7 << 8)
#define RT5670_I2S_PD2_SFT			8
#define RT5670_I2S_PD2_1			(0x0 << 8)
#define RT5670_I2S_PD2_2			(0x1 << 8)
#define RT5670_I2S_PD2_3			(0x2 << 8)
#define RT5670_I2S_PD2_4			(0x3 << 8)
#define RT5670_I2S_PD2_6			(0x4 << 8)
#define RT5670_I2S_PD2_8			(0x5 << 8)
#define RT5670_I2S_PD2_12			(0x6 << 8)
#define RT5670_I2S_PD2_16			(0x7 << 8)
#define RT5670_I2S_BCLK_MS3_MASK		(0x1 << 7)
#define RT5670_I2S_BCLK_MS3_SFT			7
#define RT5670_I2S_BCLK_MS3_32			(0x0 << 7)
#define RT5670_I2S_BCLK_MS3_64			(0x1 << 7)
#define RT5670_I2S_PD3_MASK			(0x7 << 4)
#define RT5670_I2S_PD3_SFT			4
#define RT5670_I2S_PD3_1			(0x0 << 4)
#define RT5670_I2S_PD3_2			(0x1 << 4)
#define RT5670_I2S_PD3_3			(0x2 << 4)
#define RT5670_I2S_PD3_4			(0x3 << 4)
#define RT5670_I2S_PD3_6			(0x4 << 4)
#define RT5670_I2S_PD3_8			(0x5 << 4)
#define RT5670_I2S_PD3_12			(0x6 << 4)
#define RT5670_I2S_PD3_16			(0x7 << 4)
#define RT5670_DAC_OSR_MASK			(0x3 << 2)
#define RT5670_DAC_OSR_SFT			2
#define RT5670_DAC_OSR_128			(0x0 << 2)
#define RT5670_DAC_OSR_64			(0x1 << 2)
#define RT5670_DAC_OSR_32			(0x2 << 2)
#define RT5670_DAC_OSR_16			(0x3 << 2)
#define RT5670_ADC_OSR_MASK			(0x3)
#define RT5670_ADC_OSR_SFT			0
#define RT5670_ADC_OSR_128			(0x0)
#define RT5670_ADC_OSR_64			(0x1)
#define RT5670_ADC_OSR_32			(0x2)
#define RT5670_ADC_OSR_16			(0x3)

/* ADC/DAC Clock Control 2 (0x74) */
#define RT5670_DAC_L_OSR_MASK			(0x3 << 14)
#define RT5670_DAC_L_OSR_SFT			14
#define RT5670_DAC_L_OSR_128			(0x0 << 14)
#define RT5670_DAC_L_OSR_64			(0x1 << 14)
#define RT5670_DAC_L_OSR_32			(0x2 << 14)
#define RT5670_DAC_L_OSR_16			(0x3 << 14)
#define RT5670_ADC_R_OSR_MASK			(0x3 << 12)
#define RT5670_ADC_R_OSR_SFT			12
#define RT5670_ADC_R_OSR_128			(0x0 << 12)
#define RT5670_ADC_R_OSR_64			(0x1 << 12)
#define RT5670_ADC_R_OSR_32			(0x2 << 12)
#define RT5670_ADC_R_OSR_16			(0x3 << 12)
#define RT5670_DAHPF_EN				(0x1 << 11)
#define RT5670_DAHPF_EN_SFT			11
#define RT5670_ADHPF_EN				(0x1 << 10)
#define RT5670_ADHPF_EN_SFT			10

/* Digital Microphone Control (0x75) */
#define RT5670_DMIC_1_EN_MASK			(0x1 << 15)
#define RT5670_DMIC_1_EN_SFT			15
#define RT5670_DMIC_1_DIS			(0x0 << 15)
#define RT5670_DMIC_1_EN			(0x1 << 15)
#define RT5670_DMIC_2_EN_MASK			(0x1 << 14)
#define RT5670_DMIC_2_EN_SFT			14
#define RT5670_DMIC_2_DIS			(0x0 << 14)
#define RT5670_DMIC_2_EN			(0x1 << 14)
#define RT5670_DMIC_1L_LH_MASK			(0x1 << 13)
#define RT5670_DMIC_1L_LH_SFT			13
#define RT5670_DMIC_1L_LH_FALLING		(0x0 << 13)
#define RT5670_DMIC_1L_LH_RISING		(0x1 << 13)
#define RT5670_DMIC_1R_LH_MASK			(0x1 << 12)
#define RT5670_DMIC_1R_LH_SFT			12
#define RT5670_DMIC_1R_LH_FALLING		(0x0 << 12)
#define RT5670_DMIC_1R_LH_RISING		(0x1 << 12)
#define RT5670_DMIC_2_DP_MASK			(0x1 << 10)
#define RT5670_DMIC_2_DP_SFT			10
#define RT5670_DMIC_2_DP_GPIO8			(0x0 << 10)
#define RT5670_DMIC_2_DP_IN3N			(0x1 << 10)
#define RT5670_DMIC_2L_LH_MASK			(0x1 << 9)
#define RT5670_DMIC_2L_LH_SFT			9
#define RT5670_DMIC_2L_LH_FALLING		(0x0 << 9)
#define RT5670_DMIC_2L_LH_RISING		(0x1 << 9)
#define RT5670_DMIC_2R_LH_MASK			(0x1 << 8)
#define RT5670_DMIC_2R_LH_SFT			8
#define RT5670_DMIC_2R_LH_FALLING		(0x0 << 8)
#define RT5670_DMIC_2R_LH_RISING		(0x1 << 8)
#define RT5670_DMIC_CLK_MASK			(0x7 << 5)
#define RT5670_DMIC_CLK_SFT			5
#define RT5670_DMIC_3_EN_MASK			(0x1 << 4)
#define RT5670_DMIC_3_EN_SFT			4
#define RT5670_DMIC_3_DIS			(0x0 << 4)
#define RT5670_DMIC_3_EN			(0x1 << 4)
#define RT5670_DMIC_1_DP_MASK			(0x3 << 0)
#define RT5670_DMIC_1_DP_SFT			0
#define RT5670_DMIC_1_DP_GPIO6			(0x0 << 0)
#define RT5670_DMIC_1_DP_IN2P			(0x1 << 0)
#define RT5670_DMIC_1_DP_GPIO7			(0x2 << 0)

/* Digital Microphone Control2 (0x76) */
#define RT5670_DMIC_3_DP_MASK			(0x3 << 6)
#define RT5670_DMIC_3_DP_SFT			6
#define RT5670_DMIC_3_DP_GPIO9			(0x0 << 6)
#define RT5670_DMIC_3_DP_GPIO10			(0x1 << 6)
#define RT5670_DMIC_3_DP_GPIO5			(0x2 << 6)

/* Global Clock Control (0x80) */
#define RT5670_SCLK_SRC_MASK			(0x3 << 14)
#define RT5670_SCLK_SRC_SFT			14
#define RT5670_SCLK_SRC_MCLK			(0x0 << 14)
#define RT5670_SCLK_SRC_PLL1			(0x1 << 14)
#define RT5670_SCLK_SRC_RCCLK			(0x2 << 14) /* 15MHz */
#define RT5670_PLL1_SRC_MASK			(0x7 << 11)
#define RT5670_PLL1_SRC_SFT			11
#define RT5670_PLL1_SRC_MCLK			(0x0 << 11)
#define RT5670_PLL1_SRC_BCLK1			(0x1 << 11)
#define RT5670_PLL1_SRC_BCLK2			(0x2 << 11)
#define RT5670_PLL1_SRC_BCLK3			(0x3 << 11)
#define RT5670_PLL1_PD_MASK			(0x1 << 3)
#define RT5670_PLL1_PD_SFT			3
#define RT5670_PLL1_PD_1			(0x0 << 3)
#define RT5670_PLL1_PD_2			(0x1 << 3)

#define RT5670_PLL_INP_MAX			40000000
#define RT5670_PLL_INP_MIN			256000
/* PLL M/N/K Code Control 1 (0x81) */
#define RT5670_PLL_N_MAX			0x1ff
#define RT5670_PLL_N_MASK			(RT5670_PLL_N_MAX << 7)
#define RT5670_PLL_N_SFT			7
#define RT5670_PLL_K_MAX			0x1f
#define RT5670_PLL_K_MASK			(RT5670_PLL_K_MAX)
#define RT5670_PLL_K_SFT			0

/* PLL M/N/K Code Control 2 (0x82) */
#define RT5670_PLL_M_MAX			0xf
#define RT5670_PLL_M_MASK			(RT5670_PLL_M_MAX << 12)
#define RT5670_PLL_M_SFT			12
#define RT5670_PLL_M_BP				(0x1 << 11)
#define RT5670_PLL_M_BP_SFT			11

/* ASRC Control 1 (0x83) */
#define RT5670_STO_T_MASK			(0x1 << 15)
#define RT5670_STO_T_SFT			15
#define RT5670_STO_T_SCLK			(0x0 << 15)
#define RT5670_STO_T_LRCK1			(0x1 << 15)
#define RT5670_M1_T_MASK			(0x1 << 14)
#define RT5670_M1_T_SFT				14
#define RT5670_M1_T_I2S2			(0x0 << 14)
#define RT5670_M1_T_I2S2_D3			(0x1 << 14)
#define RT5670_I2S2_F_MASK			(0x1 << 12)
#define RT5670_I2S2_F_SFT			12
#define RT5670_I2S2_F_I2S2_D2			(0x0 << 12)
#define RT5670_I2S2_F_I2S1_TCLK			(0x1 << 12)
#define RT5670_DMIC_1_M_MASK			(0x1 << 9)
#define RT5670_DMIC_1_M_SFT			9
#define RT5670_DMIC_1_M_NOR			(0x0 << 9)
#define RT5670_DMIC_1_M_ASYN			(0x1 << 9)
#define RT5670_DMIC_2_M_MASK			(0x1 << 8)
#define RT5670_DMIC_2_M_SFT			8
#define RT5670_DMIC_2_M_NOR			(0x0 << 8)
#define RT5670_DMIC_2_M_ASYN			(0x1 << 8)

/* ASRC clock source selection (0x84, 0x85) */
#define RT5670_CLK_SEL_SYS			(0x0)
#define RT5670_CLK_SEL_I2S1_ASRC		(0x1)
#define RT5670_CLK_SEL_I2S2_ASRC		(0x2)
#define RT5670_CLK_SEL_I2S3_ASRC		(0x3)
#define RT5670_CLK_SEL_SYS2			(0x5)
#define RT5670_CLK_SEL_SYS3			(0x6)

/* ASRC Control 2 (0x84) */
#define RT5670_DA_STO_CLK_SEL_MASK		(0xf << 12)
#define RT5670_DA_STO_CLK_SEL_SFT		12
#define RT5670_DA_MONOL_CLK_SEL_MASK		(0xf << 8)
#define RT5670_DA_MONOL_CLK_SEL_SFT		8
#define RT5670_DA_MONOR_CLK_SEL_MASK		(0xf << 4)
#define RT5670_DA_MONOR_CLK_SEL_SFT		4
#define RT5670_AD_STO1_CLK_SEL_MASK		(0xf << 0)
#define RT5670_AD_STO1_CLK_SEL_SFT		0

/* ASRC Control 3 (0x85) */
#define RT5670_UP_CLK_SEL_MASK			(0xf << 12)
#define RT5670_UP_CLK_SEL_SFT			12
#define RT5670_DOWN_CLK_SEL_MASK		(0xf << 8)
#define RT5670_DOWN_CLK_SEL_SFT			8
#define RT5670_AD_MONOL_CLK_SEL_MASK		(0xf << 4)
#define RT5670_AD_MONOL_CLK_SEL_SFT		4
#define RT5670_AD_MONOR_CLK_SEL_MASK		(0xf << 0)
#define RT5670_AD_MONOR_CLK_SEL_SFT		0

/* ASRC Control 4 (0x89) */
#define RT5670_I2S1_PD_MASK			(0x7 << 12)
#define RT5670_I2S1_PD_SFT			12
#define RT5670_I2S2_PD_MASK			(0x7 << 8)
#define RT5670_I2S2_PD_SFT			8

/* HPOUT Over Current Detection (0x8b) */
#define RT5670_HP_OVCD_MASK			(0x1 << 10)
#define RT5670_HP_OVCD_SFT			10
#define RT5670_HP_OVCD_DIS			(0x0 << 10)
#define RT5670_HP_OVCD_EN			(0x1 << 10)
#define RT5670_HP_OC_TH_MASK			(0x3 << 8)
#define RT5670_HP_OC_TH_SFT			8
#define RT5670_HP_OC_TH_90			(0x0 << 8)
#define RT5670_HP_OC_TH_105			(0x1 << 8)
#define RT5670_HP_OC_TH_120			(0x2 << 8)
#define RT5670_HP_OC_TH_135			(0x3 << 8)

/* Class D Over Current Control (0x8c) */
#define RT5670_CLSD_OC_MASK			(0x1 << 9)
#define RT5670_CLSD_OC_SFT			9
#define RT5670_CLSD_OC_PU			(0x0 << 9)
#define RT5670_CLSD_OC_PD			(0x1 << 9)
#define RT5670_AUTO_PD_MASK			(0x1 << 8)
#define RT5670_AUTO_PD_SFT			8
#define RT5670_AUTO_PD_DIS			(0x0 << 8)
#define RT5670_AUTO_PD_EN			(0x1 << 8)
#define RT5670_CLSD_OC_TH_MASK			(0x3f)
#define RT5670_CLSD_OC_TH_SFT			0

/* Class D Output Control (0x8d) */
#define RT5670_CLSD_RATIO_MASK			(0xf << 12)
#define RT5670_CLSD_RATIO_SFT			12
#define RT5670_CLSD_OM_MASK			(0x1 << 11)
#define RT5670_CLSD_OM_SFT			11
#define RT5670_CLSD_OM_MONO			(0x0 << 11)
#define RT5670_CLSD_OM_STO			(0x1 << 11)
#define RT5670_CLSD_SCH_MASK			(0x1 << 10)
#define RT5670_CLSD_SCH_SFT			10
#define RT5670_CLSD_SCH_L			(0x0 << 10)
#define RT5670_CLSD_SCH_S			(0x1 << 10)

/* Depop Mode Control 1 (0x8e) */
#define RT5670_SMT_TRIG_MASK			(0x1 << 15)
#define RT5670_SMT_TRIG_SFT			15
#define RT5670_SMT_TRIG_DIS			(0x0 << 15)
#define RT5670_SMT_TRIG_EN			(0x1 << 15)
#define RT5670_HP_L_SMT_MASK			(0x1 << 9)
#define RT5670_HP_L_SMT_SFT			9
#define RT5670_HP_L_SMT_DIS			(0x0 << 9)
#define RT5670_HP_L_SMT_EN			(0x1 << 9)
#define RT5670_HP_R_SMT_MASK			(0x1 << 8)
#define RT5670_HP_R_SMT_SFT			8
#define RT5670_HP_R_SMT_DIS			(0x0 << 8)
#define RT5670_HP_R_SMT_EN			(0x1 << 8)
#define RT5670_HP_CD_PD_MASK			(0x1 << 7)
#define RT5670_HP_CD_PD_SFT			7
#define RT5670_HP_CD_PD_DIS			(0x0 << 7)
#define RT5670_HP_CD_PD_EN			(0x1 << 7)
#define RT5670_RSTN_MASK			(0x1 << 6)
#define RT5670_RSTN_SFT				6
#define RT5670_RSTN_DIS				(0x0 << 6)
#define RT5670_RSTN_EN				(0x1 << 6)
#define RT5670_RSTP_MASK			(0x1 << 5)
#define RT5670_RSTP_SFT				5
#define RT5670_RSTP_DIS				(0x0 << 5)
#define RT5670_RSTP_EN				(0x1 << 5)
#define RT5670_HP_CO_MASK			(0x1 << 4)
#define RT5670_HP_CO_SFT			4
#define RT5670_HP_CO_DIS			(0x0 << 4)
#define RT5670_HP_CO_EN				(0x1 << 4)
#define RT5670_HP_CP_MASK			(0x1 << 3)
#define RT5670_HP_CP_SFT			3
#define RT5670_HP_CP_PD				(0x0 << 3)
#define RT5670_HP_CP_PU				(0x1 << 3)
#define RT5670_HP_SG_MASK			(0x1 << 2)
#define RT5670_HP_SG_SFT			2
#define RT5670_HP_SG_DIS			(0x0 << 2)
#define RT5670_HP_SG_EN				(0x1 << 2)
#define RT5670_HP_DP_MASK			(0x1 << 1)
#define RT5670_HP_DP_SFT			1
#define RT5670_HP_DP_PD				(0x0 << 1)
#define RT5670_HP_DP_PU				(0x1 << 1)
#define RT5670_HP_CB_MASK			(0x1)
#define RT5670_HP_CB_SFT			0
#define RT5670_HP_CB_PD				(0x0)
#define RT5670_HP_CB_PU				(0x1)

/* Depop Mode Control 2 (0x8f) */
#define RT5670_DEPOP_MASK			(0x1 << 13)
#define RT5670_DEPOP_SFT			13
#define RT5670_DEPOP_AUTO			(0x0 << 13)
#define RT5670_DEPOP_MAN			(0x1 << 13)
#define RT5670_RAMP_MASK			(0x1 << 12)
#define RT5670_RAMP_SFT				12
#define RT5670_RAMP_DIS				(0x0 << 12)
#define RT5670_RAMP_EN				(0x1 << 12)
#define RT5670_BPS_MASK				(0x1 << 11)
#define RT5670_BPS_SFT				11
#define RT5670_BPS_DIS				(0x0 << 11)
#define RT5670_BPS_EN				(0x1 << 11)
#define RT5670_FAST_UPDN_MASK			(0x1 << 10)
#define RT5670_FAST_UPDN_SFT			10
#define RT5670_FAST_UPDN_DIS			(0x0 << 10)
#define RT5670_FAST_UPDN_EN			(0x1 << 10)
#define RT5670_MRES_MASK			(0x3 << 8)
#define RT5670_MRES_SFT				8
#define RT5670_MRES_15MO			(0x0 << 8)
#define RT5670_MRES_25MO			(0x1 << 8)
#define RT5670_MRES_35MO			(0x2 << 8)
#define RT5670_MRES_45MO			(0x3 << 8)
#define RT5670_VLO_MASK				(0x1 << 7)
#define RT5670_VLO_SFT				7
#define RT5670_VLO_3V				(0x0 << 7)
#define RT5670_VLO_32V				(0x1 << 7)
#define RT5670_DIG_DP_MASK			(0x1 << 6)
#define RT5670_DIG_DP_SFT			6
#define RT5670_DIG_DP_DIS			(0x0 << 6)
#define RT5670_DIG_DP_EN			(0x1 << 6)
#define RT5670_DP_TH_MASK			(0x3 << 4)
#define RT5670_DP_TH_SFT			4

/* Depop Mode Control 3 (0x90) */
#define RT5670_CP_SYS_MASK			(0x7 << 12)
#define RT5670_CP_SYS_SFT			12
#define RT5670_CP_FQ1_MASK			(0x7 << 8)
#define RT5670_CP_FQ1_SFT			8
#define RT5670_CP_FQ2_MASK			(0x7 << 4)
#define RT5670_CP_FQ2_SFT			4
#define RT5670_CP_FQ3_MASK			(0x7)
#define RT5670_CP_FQ3_SFT			0
#define RT5670_CP_FQ_1_5_KHZ			0
#define RT5670_CP_FQ_3_KHZ			1
#define RT5670_CP_FQ_6_KHZ			2
#define RT5670_CP_FQ_12_KHZ			3
#define RT5670_CP_FQ_24_KHZ			4
#define RT5670_CP_FQ_48_KHZ			5
#define RT5670_CP_FQ_96_KHZ			6
#define RT5670_CP_FQ_192_KHZ			7

/* HPOUT charge pump (0x91) */
#define RT5670_OSW_L_MASK			(0x1 << 11)
#define RT5670_OSW_L_SFT			11
#define RT5670_OSW_L_DIS			(0x0 << 11)
#define RT5670_OSW_L_EN				(0x1 << 11)
#define RT5670_OSW_R_MASK			(0x1 << 10)
#define RT5670_OSW_R_SFT			10
#define RT5670_OSW_R_DIS			(0x0 << 10)
#define RT5670_OSW_R_EN				(0x1 << 10)
#define RT5670_PM_HP_MASK			(0x3 << 8)
#define RT5670_PM_HP_SFT			8
#define RT5670_PM_HP_LV				(0x0 << 8)
#define RT5670_PM_HP_MV				(0x1 << 8)
#define RT5670_PM_HP_HV				(0x2 << 8)
#define RT5670_IB_HP_MASK			(0x3 << 6)
#define RT5670_IB_HP_SFT			6
#define RT5670_IB_HP_125IL			(0x0 << 6)
#define RT5670_IB_HP_25IL			(0x1 << 6)
#define RT5670_IB_HP_5IL			(0x2 << 6)
#define RT5670_IB_HP_1IL			(0x3 << 6)

/* PV detection and SPK gain control (0x92) */
#define RT5670_PVDD_DET_MASK			(0x1 << 15)
#define RT5670_PVDD_DET_SFT			15
#define RT5670_PVDD_DET_DIS			(0x0 << 15)
#define RT5670_PVDD_DET_EN			(0x1 << 15)
#define RT5670_SPK_AG_MASK			(0x1 << 14)
#define RT5670_SPK_AG_SFT			14
#define RT5670_SPK_AG_DIS			(0x0 << 14)
#define RT5670_SPK_AG_EN			(0x1 << 14)

/* Micbias Control (0x93) */
#define RT5670_MIC1_BS_MASK			(0x1 << 15)
#define RT5670_MIC1_BS_SFT			15
#define RT5670_MIC1_BS_9AV			(0x0 << 15)
#define RT5670_MIC1_BS_75AV			(0x1 << 15)
#define RT5670_MIC2_BS_MASK			(0x1 << 14)
#define RT5670_MIC2_BS_SFT			14
#define RT5670_MIC2_BS_9AV			(0x0 << 14)
#define RT5670_MIC2_BS_75AV			(0x1 << 14)
#define RT5670_MIC1_CLK_MASK			(0x1 << 13)
#define RT5670_MIC1_CLK_SFT			13
#define RT5670_MIC1_CLK_DIS			(0x0 << 13)
#define RT5670_MIC1_CLK_EN			(0x1 << 13)
#define RT5670_MIC2_CLK_MASK			(0x1 << 12)
#define RT5670_MIC2_CLK_SFT			12
#define RT5670_MIC2_CLK_DIS			(0x0 << 12)
#define RT5670_MIC2_CLK_EN			(0x1 << 12)
#define RT5670_MIC1_OVCD_MASK			(0x1 << 11)
#define RT5670_MIC1_OVCD_SFT			11
#define RT5670_MIC1_OVCD_DIS			(0x0 << 11)
#define RT5670_MIC1_OVCD_EN			(0x1 << 11)
#define RT5670_MIC1_OVTH_MASK			(0x3 << 9)
#define RT5670_MIC1_OVTH_SFT			9
#define RT5670_MIC1_OVTH_600UA			(0x0 << 9)
#define RT5670_MIC1_OVTH_1500UA			(0x1 << 9)
#define RT5670_MIC1_OVTH_2000UA			(0x2 << 9)
#define RT5670_MIC2_OVCD_MASK			(0x1 << 8)
#define RT5670_MIC2_OVCD_SFT			8
#define RT5670_MIC2_OVCD_DIS			(0x0 << 8)
#define RT5670_MIC2_OVCD_EN			(0x1 << 8)
#define RT5670_MIC2_OVTH_MASK			(0x3 << 6)
#define RT5670_MIC2_OVTH_SFT			6
#define RT5670_MIC2_OVTH_600UA			(0x0 << 6)
#define RT5670_MIC2_OVTH_1500UA			(0x1 << 6)
#define RT5670_MIC2_OVTH_2000UA			(0x2 << 6)
#define RT5670_PWR_MB_MASK			(0x1 << 5)
#define RT5670_PWR_MB_SFT			5
#define RT5670_PWR_MB_PD			(0x0 << 5)
#define RT5670_PWR_MB_PU			(0x1 << 5)
#define RT5670_PWR_CLK25M_MASK			(0x1 << 4)
#define RT5670_PWR_CLK25M_SFT			4
#define RT5670_PWR_CLK25M_PD			(0x0 << 4)
#define RT5670_PWR_CLK25M_PU			(0x1 << 4)

/* Analog JD Control 1 (0x94) */
#define RT5670_JD1_MODE_MASK			(0x3 << 0)
#define RT5670_JD1_MODE_0			(0x0 << 0)
#define RT5670_JD1_MODE_1			(0x1 << 0)
#define RT5670_JD1_MODE_2			(0x2 << 0)

/* VAD Control 4 (0x9d) */
#define RT5670_VAD_SEL_MASK			(0x3 << 8)
#define RT5670_VAD_SEL_SFT			8

/* EQ Control 1 (0xb0) */
#define RT5670_EQ_SRC_MASK			(0x1 << 15)
#define RT5670_EQ_SRC_SFT			15
#define RT5670_EQ_SRC_DAC			(0x0 << 15)
#define RT5670_EQ_SRC_ADC			(0x1 << 15)
#define RT5670_EQ_UPD				(0x1 << 14)
#define RT5670_EQ_UPD_BIT			14
#define RT5670_EQ_CD_MASK			(0x1 << 13)
#define RT5670_EQ_CD_SFT			13
#define RT5670_EQ_CD_DIS			(0x0 << 13)
#define RT5670_EQ_CD_EN				(0x1 << 13)
#define RT5670_EQ_DITH_MASK			(0x3 << 8)
#define RT5670_EQ_DITH_SFT			8
#define RT5670_EQ_DITH_NOR			(0x0 << 8)
#define RT5670_EQ_DITH_LSB			(0x1 << 8)
#define RT5670_EQ_DITH_LSB_1			(0x2 << 8)
#define RT5670_EQ_DITH_LSB_2			(0x3 << 8)

/* EQ Control 2 (0xb1) */
#define RT5670_EQ_HPF1_M_MASK			(0x1 << 8)
#define RT5670_EQ_HPF1_M_SFT			8
#define RT5670_EQ_HPF1_M_HI			(0x0 << 8)
#define RT5670_EQ_HPF1_M_1ST			(0x1 << 8)
#define RT5670_EQ_LPF1_M_MASK			(0x1 << 7)
#define RT5670_EQ_LPF1_M_SFT			7
#define RT5670_EQ_LPF1_M_LO			(0x0 << 7)
#define RT5670_EQ_LPF1_M_1ST			(0x1 << 7)
#define RT5670_EQ_HPF2_MASK			(0x1 << 6)
#define RT5670_EQ_HPF2_SFT			6
#define RT5670_EQ_HPF2_DIS			(0x0 << 6)
#define RT5670_EQ_HPF2_EN			(0x1 << 6)
#define RT5670_EQ_HPF1_MASK			(0x1 << 5)
#define RT5670_EQ_HPF1_SFT			5
#define RT5670_EQ_HPF1_DIS			(0x0 << 5)
#define RT5670_EQ_HPF1_EN			(0x1 << 5)
#define RT5670_EQ_BPF4_MASK			(0x1 << 4)
#define RT5670_EQ_BPF4_SFT			4
#define RT5670_EQ_BPF4_DIS			(0x0 << 4)
#define RT5670_EQ_BPF4_EN			(0x1 << 4)
#define RT5670_EQ_BPF3_MASK			(0x1 << 3)
#define RT5670_EQ_BPF3_SFT			3
#define RT5670_EQ_BPF3_DIS			(0x0 << 3)
#define RT5670_EQ_BPF3_EN			(0x1 << 3)
#define RT5670_EQ_BPF2_MASK			(0x1 << 2)
#define RT5670_EQ_BPF2_SFT			2
#define RT5670_EQ_BPF2_DIS			(0x0 << 2)
#define RT5670_EQ_BPF2_EN			(0x1 << 2)
#define RT5670_EQ_BPF1_MASK			(0x1 << 1)
#define RT5670_EQ_BPF1_SFT			1
#define RT5670_EQ_BPF1_DIS			(0x0 << 1)
#define RT5670_EQ_BPF1_EN			(0x1 << 1)
#define RT5670_EQ_LPF_MASK			(0x1)
#define RT5670_EQ_LPF_SFT			0
#define RT5670_EQ_LPF_DIS			(0x0)
#define RT5670_EQ_LPF_EN			(0x1)
#define RT5670_EQ_CTRL_MASK			(0x7f)

/* Memory Test (0xb2) */
#define RT5670_MT_MASK				(0x1 << 15)
#define RT5670_MT_SFT				15
#define RT5670_MT_DIS				(0x0 << 15)
#define RT5670_MT_EN				(0x1 << 15)

/* DRC/AGC Control 1 (0xb4) */
#define RT5670_DRC_AGC_P_MASK			(0x1 << 15)
#define RT5670_DRC_AGC_P_SFT			15
#define RT5670_DRC_AGC_P_DAC			(0x0 << 15)
#define RT5670_DRC_AGC_P_ADC			(0x1 << 15)
#define RT5670_DRC_AGC_MASK			(0x1 << 14)
#define RT5670_DRC_AGC_SFT			14
#define RT5670_DRC_AGC_DIS			(0x0 << 14)
#define RT5670_DRC_AGC_EN			(0x1 << 14)
#define RT5670_DRC_AGC_UPD			(0x1 << 13)
#define RT5670_DRC_AGC_UPD_BIT			13
#define RT5670_DRC_AGC_AR_MASK			(0x1f << 8)
#define RT5670_DRC_AGC_AR_SFT			8
#define RT5670_DRC_AGC_R_MASK			(0x7 << 5)
#define RT5670_DRC_AGC_R_SFT			5
#define RT5670_DRC_AGC_R_48K			(0x1 << 5)
#define RT5670_DRC_AGC_R_96K			(0x2 << 5)
#define RT5670_DRC_AGC_R_192K			(0x3 << 5)
#define RT5670_DRC_AGC_R_441K			(0x5 << 5)
#define RT5670_DRC_AGC_R_882K			(0x6 << 5)
#define RT5670_DRC_AGC_R_1764K			(0x7 << 5)
#define RT5670_DRC_AGC_RC_MASK			(0x1f)
#define RT5670_DRC_AGC_RC_SFT			0

/* DRC/AGC Control 2 (0xb5) */
#define RT5670_DRC_AGC_POB_MASK			(0x3f << 8)
#define RT5670_DRC_AGC_POB_SFT			8
#define RT5670_DRC_AGC_CP_MASK			(0x1 << 7)
#define RT5670_DRC_AGC_CP_SFT			7
#define RT5670_DRC_AGC_CP_DIS			(0x0 << 7)
#define RT5670_DRC_AGC_CP_EN			(0x1 << 7)
#define RT5670_DRC_AGC_CPR_MASK			(0x3 << 5)
#define RT5670_DRC_AGC_CPR_SFT			5
#define RT5670_DRC_AGC_CPR_1_1			(0x0 << 5)
#define RT5670_DRC_AGC_CPR_1_2			(0x1 << 5)
#define RT5670_DRC_AGC_CPR_1_3			(0x2 << 5)
#define RT5670_DRC_AGC_CPR_1_4			(0x3 << 5)
#define RT5670_DRC_AGC_PRB_MASK			(0x1f)
#define RT5670_DRC_AGC_PRB_SFT			0

/* DRC/AGC Control 3 (0xb6) */
#define RT5670_DRC_AGC_NGB_MASK			(0xf << 12)
#define RT5670_DRC_AGC_NGB_SFT			12
#define RT5670_DRC_AGC_TAR_MASK			(0x1f << 7)
#define RT5670_DRC_AGC_TAR_SFT			7
#define RT5670_DRC_AGC_NG_MASK			(0x1 << 6)
#define RT5670_DRC_AGC_NG_SFT			6
#define RT5670_DRC_AGC_NG_DIS			(0x0 << 6)
#define RT5670_DRC_AGC_NG_EN			(0x1 << 6)
#define RT5670_DRC_AGC_NGH_MASK			(0x1 << 5)
#define RT5670_DRC_AGC_NGH_SFT			5
#define RT5670_DRC_AGC_NGH_DIS			(0x0 << 5)
#define RT5670_DRC_AGC_NGH_EN			(0x1 << 5)
#define RT5670_DRC_AGC_NGT_MASK			(0x1f)
#define RT5670_DRC_AGC_NGT_SFT			0

/* Jack Detect Control (0xbb) */
#define RT5670_JD_MASK				(0x7 << 13)
#define RT5670_JD_SFT				13
#define RT5670_JD_DIS				(0x0 << 13)
#define RT5670_JD_GPIO1				(0x1 << 13)
#define RT5670_JD_JD1_IN4P			(0x2 << 13)
#define RT5670_JD_JD2_IN4N			(0x3 << 13)
#define RT5670_JD_GPIO2				(0x4 << 13)
#define RT5670_JD_GPIO3				(0x5 << 13)
#define RT5670_JD_GPIO4				(0x6 << 13)
#define RT5670_JD_HP_MASK			(0x1 << 11)
#define RT5670_JD_HP_SFT			11
#define RT5670_JD_HP_DIS			(0x0 << 11)
#define RT5670_JD_HP_EN				(0x1 << 11)
#define RT5670_JD_HP_TRG_MASK			(0x1 << 10)
#define RT5670_JD_HP_TRG_SFT			10
#define RT5670_JD_HP_TRG_LO			(0x0 << 10)
#define RT5670_JD_HP_TRG_HI			(0x1 << 10)
#define RT5670_JD_SPL_MASK			(0x1 << 9)
#define RT5670_JD_SPL_SFT			9
#define RT5670_JD_SPL_DIS			(0x0 << 9)
#define RT5670_JD_SPL_EN			(0x1 << 9)
#define RT5670_JD_SPL_TRG_MASK			(0x1 << 8)
#define RT5670_JD_SPL_TRG_SFT			8
#define RT5670_JD_SPL_TRG_LO			(0x0 << 8)
#define RT5670_JD_SPL_TRG_HI			(0x1 << 8)
#define RT5670_JD_SPR_MASK			(0x1 << 7)
#define RT5670_JD_SPR_SFT			7
#define RT5670_JD_SPR_DIS			(0x0 << 7)
#define RT5670_JD_SPR_EN			(0x1 << 7)
#define RT5670_JD_SPR_TRG_MASK			(0x1 << 6)
#define RT5670_JD_SPR_TRG_SFT			6
#define RT5670_JD_SPR_TRG_LO			(0x0 << 6)
#define RT5670_JD_SPR_TRG_HI			(0x1 << 6)
#define RT5670_JD_MO_MASK			(0x1 << 5)
#define RT5670_JD_MO_SFT			5
#define RT5670_JD_MO_DIS			(0x0 << 5)
#define RT5670_JD_MO_EN				(0x1 << 5)
#define RT5670_JD_MO_TRG_MASK			(0x1 << 4)
#define RT5670_JD_MO_TRG_SFT			4
#define RT5670_JD_MO_TRG_LO			(0x0 << 4)
#define RT5670_JD_MO_TRG_HI			(0x1 << 4)
#define RT5670_JD_LO_MASK			(0x1 << 3)
#define RT5670_JD_LO_SFT			3
#define RT5670_JD_LO_DIS			(0x0 << 3)
#define RT5670_JD_LO_EN				(0x1 << 3)
#define RT5670_JD_LO_TRG_MASK			(0x1 << 2)
#define RT5670_JD_LO_TRG_SFT			2
#define RT5670_JD_LO_TRG_LO			(0x0 << 2)
#define RT5670_JD_LO_TRG_HI			(0x1 << 2)
#define RT5670_JD1_IN4P_MASK			(0x1 << 1)
#define RT5670_JD1_IN4P_SFT			1
#define RT5670_JD1_IN4P_DIS			(0x0 << 1)
#define RT5670_JD1_IN4P_EN			(0x1 << 1)
#define RT5670_JD2_IN4N_MASK			(0x1)
#define RT5670_JD2_IN4N_SFT			0
#define RT5670_JD2_IN4N_DIS			(0x0)
#define RT5670_JD2_IN4N_EN			(0x1)

/* IRQ Control 1 (0xbd) */
#define RT5670_IRQ_JD_MASK			(0x1 << 15)
#define RT5670_IRQ_JD_SFT			15
#define RT5670_IRQ_JD_BP			(0x0 << 15)
#define RT5670_IRQ_JD_NOR			(0x1 << 15)
#define RT5670_IRQ_OT_MASK			(0x1 << 14)
#define RT5670_IRQ_OT_SFT			14
#define RT5670_IRQ_OT_BP			(0x0 << 14)
#define RT5670_IRQ_OT_NOR			(0x1 << 14)
#define RT5670_JD_STKY_MASK			(0x1 << 13)
#define RT5670_JD_STKY_SFT			13
#define RT5670_JD_STKY_DIS			(0x0 << 13)
#define RT5670_JD_STKY_EN			(0x1 << 13)
#define RT5670_OT_STKY_MASK			(0x1 << 12)
#define RT5670_OT_STKY_SFT			12
#define RT5670_OT_STKY_DIS			(0x0 << 12)
#define RT5670_OT_STKY_EN			(0x1 << 12)
#define RT5670_JD_P_MASK			(0x1 << 11)
#define RT5670_JD_P_SFT				11
#define RT5670_JD_P_NOR				(0x0 << 11)
#define RT5670_JD_P_INV				(0x1 << 11)
#define RT5670_OT_P_MASK			(0x1 << 10)
#define RT5670_OT_P_SFT				10
#define RT5670_OT_P_NOR				(0x0 << 10)
#define RT5670_OT_P_INV				(0x1 << 10)
#define RT5670_JD1_1_EN_MASK			(0x1 << 9)
#define RT5670_JD1_1_EN_SFT			9
#define RT5670_JD1_1_DIS			(0x0 << 9)
#define RT5670_JD1_1_EN				(0x1 << 9)

/* IRQ Control 2 (0xbe) */
#define RT5670_IRQ_MB1_OC_MASK			(0x1 << 15)
#define RT5670_IRQ_MB1_OC_SFT			15
#define RT5670_IRQ_MB1_OC_BP			(0x0 << 15)
#define RT5670_IRQ_MB1_OC_NOR			(0x1 << 15)
#define RT5670_IRQ_MB2_OC_MASK			(0x1 << 14)
#define RT5670_IRQ_MB2_OC_SFT			14
#define RT5670_IRQ_MB2_OC_BP			(0x0 << 14)
#define RT5670_IRQ_MB2_OC_NOR			(0x1 << 14)
#define RT5670_MB1_OC_STKY_MASK			(0x1 << 11)
#define RT5670_MB1_OC_STKY_SFT			11
#define RT5670_MB1_OC_STKY_DIS			(0x0 << 11)
#define RT5670_MB1_OC_STKY_EN			(0x1 << 11)
#define RT5670_MB2_OC_STKY_MASK			(0x1 << 10)
#define RT5670_MB2_OC_STKY_SFT			10
#define RT5670_MB2_OC_STKY_DIS			(0x0 << 10)
#define RT5670_MB2_OC_STKY_EN			(0x1 << 10)
#define RT5670_MB1_OC_P_MASK			(0x1 << 7)
#define RT5670_MB1_OC_P_SFT			7
#define RT5670_MB1_OC_P_NOR			(0x0 << 7)
#define RT5670_MB1_OC_P_INV			(0x1 << 7)
#define RT5670_MB2_OC_P_MASK			(0x1 << 6)
#define RT5670_MB2_OC_P_SFT			6
#define RT5670_MB2_OC_P_NOR			(0x0 << 6)
#define RT5670_MB2_OC_P_INV			(0x1 << 6)
#define RT5670_MB1_OC_CLR			(0x1 << 3)
#define RT5670_MB1_OC_CLR_SFT			3
#define RT5670_MB2_OC_CLR			(0x1 << 2)
#define RT5670_MB2_OC_CLR_SFT			2

/* GPIO Control 1 (0xc0) */
#define RT5670_GP1_PIN_MASK			(0x1 << 15)
#define RT5670_GP1_PIN_SFT			15
#define RT5670_GP1_PIN_GPIO1			(0x0 << 15)
#define RT5670_GP1_PIN_IRQ			(0x1 << 15)
#define RT5670_GP2_PIN_MASK			(0x1 << 14)
#define RT5670_GP2_PIN_SFT			14
#define RT5670_GP2_PIN_GPIO2			(0x0 << 14)
#define RT5670_GP2_PIN_DMIC1_SCL		(0x1 << 14)
#define RT5670_GP3_PIN_MASK			(0x3 << 12)
#define RT5670_GP3_PIN_SFT			12
#define RT5670_GP3_PIN_GPIO3			(0x0 << 12)
#define RT5670_GP3_PIN_DMIC1_SDA		(0x1 << 12)
#define RT5670_GP3_PIN_IRQ			(0x2 << 12)
#define RT5670_GP4_PIN_MASK			(0x1 << 11)
#define RT5670_GP4_PIN_SFT			11
#define RT5670_GP4_PIN_GPIO4			(0x0 << 11)
#define RT5670_GP4_PIN_DMIC2_SDA		(0x1 << 11)
#define RT5670_DP_SIG_MASK			(0x1 << 10)
#define RT5670_DP_SIG_SFT			10
#define RT5670_DP_SIG_TEST			(0x0 << 10)
#define RT5670_DP_SIG_AP			(0x1 << 10)
#define RT5670_GPIO_M_MASK			(0x1 << 9)
#define RT5670_GPIO_M_SFT			9
#define RT5670_GPIO_M_FLT			(0x0 << 9)
#define RT5670_GPIO_M_PH			(0x1 << 9)
#define RT5670_I2S2_PIN_MASK			(0x1 << 8)
#define RT5670_I2S2_PIN_SFT			8
#define RT5670_I2S2_PIN_I2S			(0x0 << 8)
#define RT5670_I2S2_PIN_GPIO			(0x1 << 8)
#define RT5670_GP5_PIN_MASK			(0x1 << 7)
#define RT5670_GP5_PIN_SFT			7
#define RT5670_GP5_PIN_GPIO5			(0x0 << 7)
#define RT5670_GP5_PIN_DMIC3_SDA		(0x1 << 7)
#define RT5670_GP6_PIN_MASK			(0x1 << 6)
#define RT5670_GP6_PIN_SFT			6
#define RT5670_GP6_PIN_GPIO6			(0x0 << 6)
#define RT5670_GP6_PIN_DMIC1_SDA		(0x1 << 6)
#define RT5670_GP7_PIN_MASK			(0x3 << 4)
#define RT5670_GP7_PIN_SFT			4
#define RT5670_GP7_PIN_GPIO7			(0x0 << 4)
#define RT5670_GP7_PIN_DMIC1_SDA		(0x1 << 4)
#define RT5670_GP7_PIN_PDM_SCL2			(0x2 << 4)
#define RT5670_GP8_PIN_MASK			(0x1 << 3)
#define RT5670_GP8_PIN_SFT			3
#define RT5670_GP8_PIN_GPIO8			(0x0 << 3)
#define RT5670_GP8_PIN_DMIC2_SDA		(0x1 << 3)
#define RT5670_GP9_PIN_MASK			(0x1 << 2)
#define RT5670_GP9_PIN_SFT			2
#define RT5670_GP9_PIN_GPIO9			(0x0 << 2)
#define RT5670_GP9_PIN_DMIC3_SDA		(0x1 << 2)
#define RT5670_GP10_PIN_MASK			(0x3)
#define RT5670_GP10_PIN_SFT			0
#define RT5670_GP10_PIN_GPIO9			(0x0)
#define RT5670_GP10_PIN_DMIC3_SDA		(0x1)
#define RT5670_GP10_PIN_PDM_ADT2		(0x2)

/* GPIO Control 2 (0xc1) */
#define RT5670_GP4_PF_MASK			(0x1 << 11)
#define RT5670_GP4_PF_SFT			11
#define RT5670_GP4_PF_IN			(0x0 << 11)
#define RT5670_GP4_PF_OUT			(0x1 << 11)
#define RT5670_GP4_OUT_MASK			(0x1 << 10)
#define RT5670_GP4_OUT_SFT			10
#define RT5670_GP4_OUT_LO			(0x0 << 10)
#define RT5670_GP4_OUT_HI			(0x1 << 10)
#define RT5670_GP4_P_MASK			(0x1 << 9)
#define RT5670_GP4_P_SFT			9
#define RT5670_GP4_P_NOR			(0x0 << 9)
#define RT5670_GP4_P_INV			(0x1 << 9)
#define RT5670_GP3_PF_MASK			(0x1 << 8)
#define RT5670_GP3_PF_SFT			8
#define RT5670_GP3_PF_IN			(0x0 << 8)
#define RT5670_GP3_PF_OUT			(0x1 << 8)
#define RT5670_GP3_OUT_MASK			(0x1 << 7)
#define RT5670_GP3_OUT_SFT			7
#define RT5670_GP3_OUT_LO			(0x0 << 7)
#define RT5670_GP3_OUT_HI			(0x1 << 7)
#define RT5670_GP3_P_MASK			(0x1 << 6)
#define RT5670_GP3_P_SFT			6
#define RT5670_GP3_P_NOR			(0x0 << 6)
#define RT5670_GP3_P_INV			(0x1 << 6)
#define RT5670_GP2_PF_MASK			(0x1 << 5)
#define RT5670_GP2_PF_SFT			5
#define RT5670_GP2_PF_IN			(0x0 << 5)
#define RT5670_GP2_PF_OUT			(0x1 << 5)
#define RT5670_GP2_OUT_MASK			(0x1 << 4)
#define RT5670_GP2_OUT_SFT			4
#define RT5670_GP2_OUT_LO			(0x0 << 4)
#define RT5670_GP2_OUT_HI			(0x1 << 4)
#define RT5670_GP2_P_MASK			(0x1 << 3)
#define RT5670_GP2_P_SFT			3
#define RT5670_GP2_P_NOR			(0x0 << 3)
#define RT5670_GP2_P_INV			(0x1 << 3)
#define RT5670_GP1_PF_MASK			(0x1 << 2)
#define RT5670_GP1_PF_SFT			2
#define RT5670_GP1_PF_IN			(0x0 << 2)
#define RT5670_GP1_PF_OUT			(0x1 << 2)
#define RT5670_GP1_OUT_MASK			(0x1 << 1)
#define RT5670_GP1_OUT_SFT			1
#define RT5670_GP1_OUT_LO			(0x0 << 1)
#define RT5670_GP1_OUT_HI			(0x1 << 1)
#define RT5670_GP1_P_MASK			(0x1)
#define RT5670_GP1_P_SFT			0
#define RT5670_GP1_P_NOR			(0x0)
#define RT5670_GP1_P_INV			(0x1)

/* Scramble Function (0xcd) */
#define RT5670_SCB_KEY_MASK			(0xff)
#define RT5670_SCB_KEY_SFT			0

/* Scramble Control (0xce) */
#define RT5670_SCB_SWAP_MASK			(0x1 << 15)
#define RT5670_SCB_SWAP_SFT			15
#define RT5670_SCB_SWAP_DIS			(0x0 << 15)
#define RT5670_SCB_SWAP_EN			(0x1 << 15)
#define RT5670_SCB_MASK				(0x1 << 14)
#define RT5670_SCB_SFT				14
#define RT5670_SCB_DIS				(0x0 << 14)
#define RT5670_SCB_EN				(0x1 << 14)

/* Baseback Control (0xcf) */
#define RT5670_BB_MASK				(0x1 << 15)
#define RT5670_BB_SFT				15
#define RT5670_BB_DIS				(0x0 << 15)
#define RT5670_BB_EN				(0x1 << 15)
#define RT5670_BB_CT_MASK			(0x7 << 12)
#define RT5670_BB_CT_SFT			12
#define RT5670_BB_CT_A				(0x0 << 12)
#define RT5670_BB_CT_B				(0x1 << 12)
#define RT5670_BB_CT_C				(0x2 << 12)
#define RT5670_BB_CT_D				(0x3 << 12)
#define RT5670_M_BB_L_MASK			(0x1 << 9)
#define RT5670_M_BB_L_SFT			9
#define RT5670_M_BB_R_MASK			(0x1 << 8)
#define RT5670_M_BB_R_SFT			8
#define RT5670_M_BB_HPF_L_MASK			(0x1 << 7)
#define RT5670_M_BB_HPF_L_SFT			7
#define RT5670_M_BB_HPF_R_MASK			(0x1 << 6)
#define RT5670_M_BB_HPF_R_SFT			6
#define RT5670_G_BB_BST_MASK			(0x3f)
#define RT5670_G_BB_BST_SFT			0

/* MP3 Plus Control 1 (0xd0) */
#define RT5670_M_MP3_L_MASK			(0x1 << 15)
#define RT5670_M_MP3_L_SFT			15
#define RT5670_M_MP3_R_MASK			(0x1 << 14)
#define RT5670_M_MP3_R_SFT			14
#define RT5670_M_MP3_MASK			(0x1 << 13)
#define RT5670_M_MP3_SFT			13
#define RT5670_M_MP3_DIS			(0x0 << 13)
#define RT5670_M_MP3_EN				(0x1 << 13)
#define RT5670_EG_MP3_MASK			(0x1f << 8)
#define RT5670_EG_MP3_SFT			8
#define RT5670_MP3_HLP_MASK			(0x1 << 7)
#define RT5670_MP3_HLP_SFT			7
#define RT5670_MP3_HLP_DIS			(0x0 << 7)
#define RT5670_MP3_HLP_EN			(0x1 << 7)
#define RT5670_M_MP3_ORG_L_MASK			(0x1 << 6)
#define RT5670_M_MP3_ORG_L_SFT			6
#define RT5670_M_MP3_ORG_R_MASK			(0x1 << 5)
#define RT5670_M_MP3_ORG_R_SFT			5

/* MP3 Plus Control 2 (0xd1) */
#define RT5670_MP3_WT_MASK			(0x1 << 13)
#define RT5670_MP3_WT_SFT			13
#define RT5670_MP3_WT_1_4			(0x0 << 13)
#define RT5670_MP3_WT_1_2			(0x1 << 13)
#define RT5670_OG_MP3_MASK			(0x1f << 8)
#define RT5670_OG_MP3_SFT			8
#define RT5670_HG_MP3_MASK			(0x3f)
#define RT5670_HG_MP3_SFT			0

/* 3D HP Control 1 (0xd2) */
#define RT5670_3D_CF_MASK			(0x1 << 15)
#define RT5670_3D_CF_SFT			15
#define RT5670_3D_CF_DIS			(0x0 << 15)
#define RT5670_3D_CF_EN				(0x1 << 15)
#define RT5670_3D_HP_MASK			(0x1 << 14)
#define RT5670_3D_HP_SFT			14
#define RT5670_3D_HP_DIS			(0x0 << 14)
#define RT5670_3D_HP_EN				(0x1 << 14)
#define RT5670_3D_BT_MASK			(0x1 << 13)
#define RT5670_3D_BT_SFT			13
#define RT5670_3D_BT_DIS			(0x0 << 13)
#define RT5670_3D_BT_EN				(0x1 << 13)
#define RT5670_3D_1F_MIX_MASK			(0x3 << 11)
#define RT5670_3D_1F_MIX_SFT			11
#define RT5670_3D_HP_M_MASK			(0x1 << 10)
#define RT5670_3D_HP_M_SFT			10
#define RT5670_3D_HP_M_SUR			(0x0 << 10)
#define RT5670_3D_HP_M_FRO			(0x1 << 10)
#define RT5670_M_3D_HRTF_MASK			(0x1 << 9)
#define RT5670_M_3D_HRTF_SFT			9
#define RT5670_M_3D_D2H_MASK			(0x1 << 8)
#define RT5670_M_3D_D2H_SFT			8
#define RT5670_M_3D_D2R_MASK			(0x1 << 7)
#define RT5670_M_3D_D2R_SFT			7
#define RT5670_M_3D_REVB_MASK			(0x1 << 6)
#define RT5670_M_3D_REVB_SFT			6

/* Adjustable high pass filter control 1 (0xd3) */
#define RT5670_2ND_HPF_MASK			(0x1 << 15)
#define RT5670_2ND_HPF_SFT			15
#define RT5670_2ND_HPF_DIS			(0x0 << 15)
#define RT5670_2ND_HPF_EN			(0x1 << 15)
#define RT5670_HPF_CF_L_MASK			(0x7 << 12)
#define RT5670_HPF_CF_L_SFT			12
#define RT5670_1ST_HPF_MASK			(0x1 << 11)
#define RT5670_1ST_HPF_SFT			11
#define RT5670_1ST_HPF_DIS			(0x0 << 11)
#define RT5670_1ST_HPF_EN			(0x1 << 11)
#define RT5670_HPF_CF_R_MASK			(0x7 << 8)
#define RT5670_HPF_CF_R_SFT			8
#define RT5670_ZD_T_MASK			(0x3 << 6)
#define RT5670_ZD_T_SFT				6
#define RT5670_ZD_F_MASK			(0x3 << 4)
#define RT5670_ZD_F_SFT				4
#define RT5670_ZD_F_IM				(0x0 << 4)
#define RT5670_ZD_F_ZC_IM			(0x1 << 4)
#define RT5670_ZD_F_ZC_IOD			(0x2 << 4)
#define RT5670_ZD_F_UN				(0x3 << 4)

/* HP calibration control and Amp detection (0xd6) */
#define RT5670_SI_DAC_MASK			(0x1 << 11)
#define RT5670_SI_DAC_SFT			11
#define RT5670_SI_DAC_AUTO			(0x0 << 11)
#define RT5670_SI_DAC_TEST			(0x1 << 11)
#define RT5670_DC_CAL_M_MASK			(0x1 << 10)
#define RT5670_DC_CAL_M_SFT			10
#define RT5670_DC_CAL_M_CAL			(0x0 << 10)
#define RT5670_DC_CAL_M_NOR			(0x1 << 10)
#define RT5670_DC_CAL_MASK			(0x1 << 9)
#define RT5670_DC_CAL_SFT			9
#define RT5670_DC_CAL_DIS			(0x0 << 9)
#define RT5670_DC_CAL_EN			(0x1 << 9)
#define RT5670_HPD_RCV_MASK			(0x7 << 6)
#define RT5670_HPD_RCV_SFT			6
#define RT5670_HPD_PS_MASK			(0x1 << 5)
#define RT5670_HPD_PS_SFT			5
#define RT5670_HPD_PS_DIS			(0x0 << 5)
#define RT5670_HPD_PS_EN			(0x1 << 5)
#define RT5670_CAL_M_MASK			(0x1 << 4)
#define RT5670_CAL_M_SFT			4
#define RT5670_CAL_M_DEP			(0x0 << 4)
#define RT5670_CAL_M_CAL			(0x1 << 4)
#define RT5670_CAL_MASK				(0x1 << 3)
#define RT5670_CAL_SFT				3
#define RT5670_CAL_DIS				(0x0 << 3)
#define RT5670_CAL_EN				(0x1 << 3)
#define RT5670_CAL_TEST_MASK			(0x1 << 2)
#define RT5670_CAL_TEST_SFT			2
#define RT5670_CAL_TEST_DIS			(0x0 << 2)
#define RT5670_CAL_TEST_EN			(0x1 << 2)
#define RT5670_CAL_P_MASK			(0x3)
#define RT5670_CAL_P_SFT			0
#define RT5670_CAL_P_NONE			(0x0)
#define RT5670_CAL_P_CAL			(0x1)
#define RT5670_CAL_P_DAC_CAL			(0x2)

/* Soft volume and zero cross control 1 (0xd9) */
#define RT5670_SV_MASK				(0x1 << 15)
#define RT5670_SV_SFT				15
#define RT5670_SV_DIS				(0x0 << 15)
#define RT5670_SV_EN				(0x1 << 15)
#define RT5670_SPO_SV_MASK			(0x1 << 14)
#define RT5670_SPO_SV_SFT			14
#define RT5670_SPO_SV_DIS			(0x0 << 14)
#define RT5670_SPO_SV_EN			(0x1 << 14)
#define RT5670_OUT_SV_MASK			(0x1 << 13)
#define RT5670_OUT_SV_SFT			13
#define RT5670_OUT_SV_DIS			(0x0 << 13)
#define RT5670_OUT_SV_EN			(0x1 << 13)
#define RT5670_HP_SV_MASK			(0x1 << 12)
#define RT5670_HP_SV_SFT			12
#define RT5670_HP_SV_DIS			(0x0 << 12)
#define RT5670_HP_SV_EN				(0x1 << 12)
#define RT5670_ZCD_DIG_MASK			(0x1 << 11)
#define RT5670_ZCD_DIG_SFT			11
#define RT5670_ZCD_DIG_DIS			(0x0 << 11)
#define RT5670_ZCD_DIG_EN			(0x1 << 11)
#define RT5670_ZCD_MASK				(0x1 << 10)
#define RT5670_ZCD_SFT				10
#define RT5670_ZCD_PD				(0x0 << 10)
#define RT5670_ZCD_PU				(0x1 << 10)
#define RT5670_M_ZCD_MASK			(0x3f << 4)
#define RT5670_M_ZCD_SFT			4
#define RT5670_M_ZCD_RM_L			(0x1 << 9)
#define RT5670_M_ZCD_RM_R			(0x1 << 8)
#define RT5670_M_ZCD_SM_L			(0x1 << 7)
#define RT5670_M_ZCD_SM_R			(0x1 << 6)
#define RT5670_M_ZCD_OM_L			(0x1 << 5)
#define RT5670_M_ZCD_OM_R			(0x1 << 4)
#define RT5670_SV_DLY_MASK			(0xf)
#define RT5670_SV_DLY_SFT			0

/* Soft volume and zero cross control 2 (0xda) */
#define RT5670_ZCD_HP_MASK			(0x1 << 15)
#define RT5670_ZCD_HP_SFT			15
#define RT5670_ZCD_HP_DIS			(0x0 << 15)
#define RT5670_ZCD_HP_EN			(0x1 << 15)

/* General Control 3 (0xfc) */
#define RT5670_TDM_DATA_MODE_SEL		(0x1 << 11)
#define RT5670_TDM_DATA_MODE_NOR		(0x0 << 11)
#define RT5670_TDM_DATA_MODE_50FS		(0x1 << 11)

/* Codec Private Register definition */
/* 3D Speaker Control (0x63) */
#define RT5670_3D_SPK_MASK			(0x1 << 15)
#define RT5670_3D_SPK_SFT			15
#define RT5670_3D_SPK_DIS			(0x0 << 15)
#define RT5670_3D_SPK_EN			(0x1 << 15)
#define RT5670_3D_SPK_M_MASK			(0x3 << 13)
#define RT5670_3D_SPK_M_SFT			13
#define RT5670_3D_SPK_CG_MASK			(0x1f << 8)
#define RT5670_3D_SPK_CG_SFT			8
#define RT5670_3D_SPK_SG_MASK			(0x1f)
#define RT5670_3D_SPK_SG_SFT			0

/* Wind Noise Detection Control 1 (0x6c) */
#define RT5670_WND_MASK				(0x1 << 15)
#define RT5670_WND_SFT				15
#define RT5670_WND_DIS				(0x0 << 15)
#define RT5670_WND_EN				(0x1 << 15)

/* Wind Noise Detection Control 2 (0x6d) */
#define RT5670_WND_FC_NW_MASK			(0x3f << 10)
#define RT5670_WND_FC_NW_SFT			10
#define RT5670_WND_FC_WK_MASK			(0x3f << 4)
#define RT5670_WND_FC_WK_SFT			4

/* Wind Noise Detection Control 3 (0x6e) */
#define RT5670_HPF_FC_MASK			(0x3f << 6)
#define RT5670_HPF_FC_SFT			6
#define RT5670_WND_FC_ST_MASK			(0x3f)
#define RT5670_WND_FC_ST_SFT			0

/* Wind Noise Detection Control 4 (0x6f) */
#define RT5670_WND_TH_LO_MASK			(0x3ff)
#define RT5670_WND_TH_LO_SFT			0

/* Wind Noise Detection Control 5 (0x70) */
#define RT5670_WND_TH_HI_MASK			(0x3ff)
#define RT5670_WND_TH_HI_SFT			0

/* Wind Noise Detection Control 8 (0x73) */
#define RT5670_WND_WIND_MASK			(0x1 << 13) /* Read-Only */
#define RT5670_WND_WIND_SFT			13
#define RT5670_WND_STRONG_MASK			(0x1 << 12) /* Read-Only */
#define RT5670_WND_STRONG_SFT			12
enum {
	RT5670_NO_WIND,
	RT5670_BREEZE,
	RT5670_STORM,
};

/* Dipole Speaker Interface (0x75) */
#define RT5670_DP_ATT_MASK			(0x3 << 14)
#define RT5670_DP_ATT_SFT			14
#define RT5670_DP_SPK_MASK			(0x1 << 10)
#define RT5670_DP_SPK_SFT			10
#define RT5670_DP_SPK_DIS			(0x0 << 10)
#define RT5670_DP_SPK_EN			(0x1 << 10)

/* EQ Pre Volume Control (0xb3) */
#define RT5670_EQ_PRE_VOL_MASK			(0xffff)
#define RT5670_EQ_PRE_VOL_SFT			0

/* EQ Post Volume Control (0xb4) */
#define RT5670_EQ_PST_VOL_MASK			(0xffff)
#define RT5670_EQ_PST_VOL_SFT			0

/* Jack Detect Control 3 (0xf8) */
#define RT5670_CMP_MIC_IN_DET_MASK		(0x7 << 12)
#define RT5670_JD_CBJ_EN			(0x1 << 7)
#define RT5670_JD_CBJ_POL			(0x1 << 6)
#define RT5670_JD_TRI_CBJ_SEL_MASK		(0x7 << 3)
#define RT5670_JD_TRI_CBJ_SEL_SFT		(3)
#define RT5670_JD_CBJ_GPIO_JD1			(0x0 << 3)
#define RT5670_JD_CBJ_JD1_1			(0x1 << 3)
#define RT5670_JD_CBJ_JD1_2			(0x2 << 3)
#define RT5670_JD_CBJ_JD2			(0x3 << 3)
#define RT5670_JD_CBJ_JD3			(0x4 << 3)
#define RT5670_JD_CBJ_GPIO_JD2			(0x5 << 3)
#define RT5670_JD_CBJ_MX0B_12			(0x6 << 3)
#define RT5670_JD_TRI_HPO_SEL_MASK		(0x7 << 3)
#define RT5670_JD_TRI_HPO_SEL_SFT		(0)
#define RT5670_JD_HPO_GPIO_JD1			(0x0)
#define RT5670_JD_HPO_JD1_1			(0x1)
#define RT5670_JD_HPO_JD1_2			(0x2)
#define RT5670_JD_HPO_JD2			(0x3)
#define RT5670_JD_HPO_JD3			(0x4)
#define RT5670_JD_HPO_GPIO_JD2			(0x5)
#define RT5670_JD_HPO_MX0B_12			(0x6)

/* Digital Misc Control (0xfa) */
#define RT5670_RST_DSP				(0x1 << 13)
#define RT5670_IF1_ADC1_IN1_SEL			(0x1 << 12)
#define RT5670_IF1_ADC1_IN1_SFT			12
#define RT5670_IF1_ADC1_IN2_SEL			(0x1 << 11)
#define RT5670_IF1_ADC1_IN2_SFT			11
#define RT5670_IF1_ADC2_IN1_SEL			(0x1 << 10)
#define RT5670_IF1_ADC2_IN1_SFT			10
#define RT5670_MCLK_DET				(0x1 << 3)

/* General Control2 (0xfb) */
#define RT5670_RXDC_SRC_MASK			(0x1 << 7)
#define RT5670_RXDC_SRC_STO			(0x0 << 7)
#define RT5670_RXDC_SRC_MONO			(0x1 << 7)
#define RT5670_RXDC_SRC_SFT			(7)
#define RT5670_RXDP2_SEL_MASK			(0x1 << 3)
#define RT5670_RXDP2_SEL_IF2			(0x0 << 3)
#define RT5670_RXDP2_SEL_ADC			(0x1 << 3)
#define RT5670_RXDP2_SEL_SFT			(3)

/* System Clock Source */
enum {
	RT5670_SCLK_S_MCLK,
	RT5670_SCLK_S_PLL1,
	RT5670_SCLK_S_RCCLK,
};

/* PLL1 Source */
enum {
	RT5670_PLL1_S_MCLK,
	RT5670_PLL1_S_BCLK1,
	RT5670_PLL1_S_BCLK2,
	RT5670_PLL1_S_BCLK3,
	RT5670_PLL1_S_BCLK4,
};

enum {
	RT5670_AIF1,
	RT5670_AIF2,
	RT5670_AIF3,
	RT5670_AIF4,
	RT5670_AIFS,
};

enum {
	RT5670_DMIC1_DISABLED,
	RT5670_DMIC_DATA_GPIO6,
	RT5670_DMIC_DATA_IN2P,
	RT5670_DMIC_DATA_GPIO7,
};

enum {
	RT5670_DMIC2_DISABLED,
	RT5670_DMIC_DATA_GPIO8,
	RT5670_DMIC_DATA_IN3N,
};

enum {
	RT5670_DMIC3_DISABLED,
	RT5670_DMIC_DATA_GPIO9,
	RT5670_DMIC_DATA_GPIO10,
	RT5670_DMIC_DATA_GPIO5,
};

/* filter mask */
enum {
	RT5670_DA_STEREO_FILTER = 0x1,
	RT5670_DA_MONO_L_FILTER = (0x1 << 1),
	RT5670_DA_MONO_R_FILTER = (0x1 << 2),
	RT5670_AD_STEREO_FILTER = (0x1 << 3),
	RT5670_AD_MONO_L_FILTER = (0x1 << 4),
	RT5670_AD_MONO_R_FILTER = (0x1 << 5),
	RT5670_UP_RATE_FILTER   = (0x1 << 6),
	RT5670_DOWN_RATE_FILTER = (0x1 << 7),
};

int rt5670_sel_asrc_clk_src(struct snd_soc_component *component,
			    unsigned int filter_mask, unsigned int clk_src);

struct rt5670_priv {
	struct snd_soc_component *component;
	struct regmap *regmap;
	struct snd_soc_jack *jack;
	struct snd_soc_jack_gpio hp_gpio;

	int jd_mode;
	bool in2_diff;
	bool gpio1_is_irq;
	bool gpio1_is_ext_spk_en;

	bool dmic_en;
	unsigned int dmic1_data_pin;
	/* 0 = GPIO6; 1 = IN2P; 3 = GPIO7*/
	unsigned int dmic2_data_pin;
	/* 0 = GPIO8; 1 = IN3N; */
	unsigned int dmic3_data_pin;
	/* 0 = GPIO9; 1 = GPIO10; 2 = GPIO5*/

	int sysclk;
	int sysclk_src;
	int lrck[RT5670_AIFS];
	int bclk[RT5670_AIFS];
	int master[RT5670_AIFS];

	int pll_src;
	int pll_in;
	int pll_out;

	int dsp_sw; /* expected parameter setting */
	int dsp_rate;
	int jack_type;
	int jack_type_saved;
};

void rt5670_jack_suspend(struct snd_soc_component *component);
void rt5670_jack_resume(struct snd_soc_component *component);
int rt5670_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *jack);
#endif /* __RT5670_H__ */
