/*
 * rt5616.h  --  RT5616 ALSA SoC audio driver
 *
 * Copyright 2011 Realtek Microelectronics
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5616_H__
#define __RT5616_H__

/* Info */
#define RT5616_RESET				0x00
#define RT5616_VERSION_ID			0xfd
#define RT5616_VENDOR_ID			0xfe
#define RT5616_DEVICE_ID			0xff
/*  I/O - Output */
#define RT5616_HP_VOL				0x02
#define RT5616_LOUT_CTRL1			0x03
#define RT5616_LOUT_CTRL2			0x05
/* I/O - Input */
#define RT5616_IN1_IN2				0x0d
#define RT5616_INL1_INR1_VOL			0x0f
/* I/O - ADC/DAC/DMIC */
#define RT5616_DAC1_DIG_VOL			0x19
#define RT5616_ADC_DIG_VOL			0x1c
#define RT5616_ADC_BST_VOL			0x1e
/* Mixer - D-D */
#define RT5616_STO1_ADC_MIXER			0x27
#define RT5616_AD_DA_MIXER			0x29
#define RT5616_STO_DAC_MIXER			0x2a

/* Mixer - ADC */
#define RT5616_REC_L1_MIXER			0x3b
#define RT5616_REC_L2_MIXER			0x3c
#define RT5616_REC_R1_MIXER			0x3d
#define RT5616_REC_R2_MIXER			0x3e
/* Mixer - DAC */
#define RT5616_HPO_MIXER			0x45
#define RT5616_OUT_L1_MIXER			0x4d
#define RT5616_OUT_L2_MIXER			0x4e
#define RT5616_OUT_L3_MIXER			0x4f
#define RT5616_OUT_R1_MIXER			0x50
#define RT5616_OUT_R2_MIXER			0x51
#define RT5616_OUT_R3_MIXER			0x52
#define RT5616_LOUT_MIXER			0x53
/* Power */
#define RT5616_PWR_DIG1				0x61
#define RT5616_PWR_DIG2				0x62
#define RT5616_PWR_ANLG1			0x63
#define RT5616_PWR_ANLG2			0x64
#define RT5616_PWR_MIXER			0x65
#define RT5616_PWR_VOL				0x66
/* Private Register Control */
#define RT5616_PRIV_INDEX			0x6a
#define RT5616_PRIV_DATA			0x6c
/* Format - ADC/DAC */
#define RT5616_I2S1_SDP				0x70
#define RT5616_ADDA_CLK1			0x73
#define RT5616_ADDA_CLK2			0x74

/* Function - Analog */
#define RT5616_GLB_CLK				0x80
#define RT5616_PLL_CTRL1			0x81
#define RT5616_PLL_CTRL2			0x82
#define RT5616_HP_OVCD				0x8b
#define RT5616_DEPOP_M1				0x8e
#define RT5616_DEPOP_M2				0x8f
#define RT5616_DEPOP_M3				0x90
#define RT5616_CHARGE_PUMP			0x91
#define RT5616_PV_DET_SPK_G			0x92
#define RT5616_MICBIAS				0x93
#define RT5616_A_JD_CTL1			0x94
#define RT5616_A_JD_CTL2			0x95
/* Function - Digital */
#define RT5616_EQ_CTRL1				0xb0
#define RT5616_EQ_CTRL2				0xb1
#define RT5616_WIND_FILTER			0xb2
#define RT5616_DRC_AGC_1			0xb4
#define RT5616_DRC_AGC_2			0xb5
#define RT5616_DRC_AGC_3			0xb6
#define RT5616_SVOL_ZC				0xb7
#define RT5616_JD_CTRL1				0xbb
#define RT5616_JD_CTRL2				0xbc
#define RT5616_IRQ_CTRL1			0xbd
#define RT5616_IRQ_CTRL2			0xbe
#define RT5616_INT_IRQ_ST			0xbf
#define RT5616_GPIO_CTRL1			0xc0
#define RT5616_GPIO_CTRL2			0xc1
#define RT5616_GPIO_CTRL3			0xc2
#define RT5616_PGM_REG_ARR1			0xc8
#define RT5616_PGM_REG_ARR2			0xc9
#define RT5616_PGM_REG_ARR3			0xca
#define RT5616_PGM_REG_ARR4			0xcb
#define RT5616_PGM_REG_ARR5			0xcc
#define RT5616_SCB_FUNC				0xcd
#define RT5616_SCB_CTRL				0xce
#define RT5616_BASE_BACK			0xcf
#define RT5616_MP3_PLUS1			0xd0
#define RT5616_MP3_PLUS2			0xd1
#define RT5616_ADJ_HPF_CTRL1			0xd3
#define RT5616_ADJ_HPF_CTRL2			0xd4
#define RT5616_HP_CALIB_AMP_DET			0xd6
#define RT5616_HP_CALIB2			0xd7
#define RT5616_SV_ZCD1				0xd9
#define RT5616_SV_ZCD2				0xda
#define RT5616_D_MISC				0xfa
/* Dummy Register */
#define RT5616_DUMMY2				0xfb
#define RT5616_DUMMY3				0xfc


/* Index of Codec Private Register definition */
#define RT5616_BIAS_CUR1			0x12
#define RT5616_BIAS_CUR3			0x14
#define RT5616_CLSD_INT_REG1			0x1c
#define RT5616_MAMP_INT_REG2			0x37
#define RT5616_CHOP_DAC_ADC			0x3d
#define RT5616_3D_SPK				0x63
#define RT5616_WND_1				0x6c
#define RT5616_WND_2				0x6d
#define RT5616_WND_3				0x6e
#define RT5616_WND_4				0x6f
#define RT5616_WND_5				0x70
#define RT5616_WND_8				0x73
#define RT5616_DIP_SPK_INF			0x75
#define RT5616_HP_DCC_INT1			0x77
#define RT5616_EQ_BW_LOP			0xa0
#define RT5616_EQ_GN_LOP			0xa1
#define RT5616_EQ_FC_BP1			0xa2
#define RT5616_EQ_BW_BP1			0xa3
#define RT5616_EQ_GN_BP1			0xa4
#define RT5616_EQ_FC_BP2			0xa5
#define RT5616_EQ_BW_BP2			0xa6
#define RT5616_EQ_GN_BP2			0xa7
#define RT5616_EQ_FC_BP3			0xa8
#define RT5616_EQ_BW_BP3			0xa9
#define RT5616_EQ_GN_BP3			0xaa
#define RT5616_EQ_FC_BP4			0xab
#define RT5616_EQ_BW_BP4			0xac
#define RT5616_EQ_GN_BP4			0xad
#define RT5616_EQ_FC_HIP1			0xae
#define RT5616_EQ_GN_HIP1			0xaf
#define RT5616_EQ_FC_HIP2			0xb0
#define RT5616_EQ_BW_HIP2			0xb1
#define RT5616_EQ_GN_HIP2			0xb2
#define RT5616_EQ_PRE_VOL			0xb3
#define RT5616_EQ_PST_VOL			0xb4


/* global definition */
#define RT5616_L_MUTE				(0x1 << 15)
#define RT5616_L_MUTE_SFT			15
#define RT5616_VOL_L_MUTE			(0x1 << 14)
#define RT5616_VOL_L_SFT			14
#define RT5616_R_MUTE				(0x1 << 7)
#define RT5616_R_MUTE_SFT			7
#define RT5616_VOL_R_MUTE			(0x1 << 6)
#define RT5616_VOL_R_SFT			6
#define RT5616_L_VOL_MASK			(0x3f << 8)
#define RT5616_L_VOL_SFT			8
#define RT5616_R_VOL_MASK			(0x3f)
#define RT5616_R_VOL_SFT			0

/* LOUT Control 2(0x05) */
#define RT5616_EN_DFO				(0x1 << 15)

/* IN1 and IN2 Control (0x0d) */
/* IN3 and IN4 Control (0x0e) */
#define RT5616_BST_MASK1			(0xf<<12)
#define RT5616_BST_SFT1				12
#define RT5616_BST_MASK2			(0xf<<8)
#define RT5616_BST_SFT2				8
#define RT5616_IN_DF1				(0x1 << 7)
#define RT5616_IN_SFT1				7
#define RT5616_IN_DF2				(0x1 << 6)
#define RT5616_IN_SFT2				6

/* INL1 and INR1 Volume Control (0x0f) */
#define RT5616_INL_VOL_MASK			(0x1f << 8)
#define RT5616_INL_VOL_SFT			8
#define RT5616_INR_SEL_MASK			(0x1 << 7)
#define RT5616_INR_SEL_SFT			7
#define RT5616_INR_SEL_IN4N			(0x0 << 7)
#define RT5616_INR_SEL_MONON			(0x1 << 7)
#define RT5616_INR_VOL_MASK			(0x1f)
#define RT5616_INR_VOL_SFT			0

/* DAC1 Digital Volume (0x19) */
#define RT5616_DAC_L1_VOL_MASK			(0xff << 8)
#define RT5616_DAC_L1_VOL_SFT			8
#define RT5616_DAC_R1_VOL_MASK			(0xff)
#define RT5616_DAC_R1_VOL_SFT			0

/* DAC2 Digital Volume (0x1a) */
#define RT5616_DAC_L2_VOL_MASK			(0xff << 8)
#define RT5616_DAC_L2_VOL_SFT			8
#define RT5616_DAC_R2_VOL_MASK			(0xff)
#define RT5616_DAC_R2_VOL_SFT			0

/* ADC Digital Volume Control (0x1c) */
#define RT5616_ADC_L_VOL_MASK			(0x7f << 8)
#define RT5616_ADC_L_VOL_SFT			8
#define RT5616_ADC_R_VOL_MASK			(0x7f)
#define RT5616_ADC_R_VOL_SFT			0

/* Mono ADC Digital Volume Control (0x1d) */
#define RT5616_M_MONO_ADC_L			(0x1 << 15)
#define RT5616_M_MONO_ADC_L_SFT			15
#define RT5616_MONO_ADC_L_VOL_MASK		(0x7f << 8)
#define RT5616_MONO_ADC_L_VOL_SFT		8
#define RT5616_M_MONO_ADC_R			(0x1 << 7)
#define RT5616_M_MONO_ADC_R_SFT			7
#define RT5616_MONO_ADC_R_VOL_MASK		(0x7f)
#define RT5616_MONO_ADC_R_VOL_SFT		0

/* ADC Boost Volume Control (0x1e) */
#define RT5616_ADC_L_BST_MASK			(0x3 << 14)
#define RT5616_ADC_L_BST_SFT			14
#define RT5616_ADC_R_BST_MASK			(0x3 << 12)
#define RT5616_ADC_R_BST_SFT			12
#define RT5616_ADC_COMP_MASK			(0x3 << 10)
#define RT5616_ADC_COMP_SFT			10

/* Stereo ADC1 Mixer Control (0x27) */
#define RT5616_M_STO1_ADC_L1			(0x1 << 14)
#define RT5616_M_STO1_ADC_L1_SFT		14
#define RT5616_M_STO1_ADC_R1			(0x1 << 6)
#define RT5616_M_STO1_ADC_R1_SFT		6

/* ADC Mixer to DAC Mixer Control (0x29) */
#define RT5616_M_ADCMIX_L			(0x1 << 15)
#define RT5616_M_ADCMIX_L_SFT			15
#define RT5616_M_IF1_DAC_L			(0x1 << 14)
#define RT5616_M_IF1_DAC_L_SFT			14
#define RT5616_M_ADCMIX_R			(0x1 << 7)
#define RT5616_M_ADCMIX_R_SFT			7
#define RT5616_M_IF1_DAC_R			(0x1 << 6)
#define RT5616_M_IF1_DAC_R_SFT			6

/* Stereo DAC Mixer Control (0x2a) */
#define RT5616_M_DAC_L1_MIXL			(0x1 << 14)
#define RT5616_M_DAC_L1_MIXL_SFT		14
#define RT5616_DAC_L1_STO_L_VOL_MASK		(0x1 << 13)
#define RT5616_DAC_L1_STO_L_VOL_SFT		13
#define RT5616_M_DAC_R1_MIXL			(0x1 << 9)
#define RT5616_M_DAC_R1_MIXL_SFT		9
#define RT5616_DAC_R1_STO_L_VOL_MASK		(0x1 << 8)
#define RT5616_DAC_R1_STO_L_VOL_SFT		8
#define RT5616_M_DAC_R1_MIXR			(0x1 << 6)
#define RT5616_M_DAC_R1_MIXR_SFT		6
#define RT5616_DAC_R1_STO_R_VOL_MASK		(0x1 << 5)
#define RT5616_DAC_R1_STO_R_VOL_SFT		5
#define RT5616_M_DAC_L1_MIXR			(0x1 << 1)
#define RT5616_M_DAC_L1_MIXR_SFT		1
#define RT5616_DAC_L1_STO_R_VOL_MASK		(0x1)
#define RT5616_DAC_L1_STO_R_VOL_SFT		0

/* DD Mixer Control (0x2b) */
#define RT5616_M_STO_DD_L1			(0x1 << 14)
#define RT5616_M_STO_DD_L1_SFT			14
#define RT5616_STO_DD_L1_VOL_MASK		(0x1 << 13)
#define RT5616_DAC_DD_L1_VOL_SFT		13
#define RT5616_M_STO_DD_L2			(0x1 << 12)
#define RT5616_M_STO_DD_L2_SFT			12
#define RT5616_STO_DD_L2_VOL_MASK		(0x1 << 11)
#define RT5616_STO_DD_L2_VOL_SFT		11
#define RT5616_M_STO_DD_R2_L			(0x1 << 10)
#define RT5616_M_STO_DD_R2_L_SFT		10
#define RT5616_STO_DD_R2_L_VOL_MASK		(0x1 << 9)
#define RT5616_STO_DD_R2_L_VOL_SFT		9
#define RT5616_M_STO_DD_R1			(0x1 << 6)
#define RT5616_M_STO_DD_R1_SFT			6
#define RT5616_STO_DD_R1_VOL_MASK		(0x1 << 5)
#define RT5616_STO_DD_R1_VOL_SFT		5
#define RT5616_M_STO_DD_R2			(0x1 << 4)
#define RT5616_M_STO_DD_R2_SFT			4
#define RT5616_STO_DD_R2_VOL_MASK		(0x1 << 3)
#define RT5616_STO_DD_R2_VOL_SFT		3
#define RT5616_M_STO_DD_L2_R			(0x1 << 2)
#define RT5616_M_STO_DD_L2_R_SFT		2
#define RT5616_STO_DD_L2_R_VOL_MASK		(0x1 << 1)
#define RT5616_STO_DD_L2_R_VOL_SFT		1

/* Digital Mixer Control (0x2c) */
#define RT5616_M_STO_L_DAC_L			(0x1 << 15)
#define RT5616_M_STO_L_DAC_L_SFT		15
#define RT5616_STO_L_DAC_L_VOL_MASK		(0x1 << 14)
#define RT5616_STO_L_DAC_L_VOL_SFT		14
#define RT5616_M_DAC_L2_DAC_L			(0x1 << 13)
#define RT5616_M_DAC_L2_DAC_L_SFT		13
#define RT5616_DAC_L2_DAC_L_VOL_MASK		(0x1 << 12)
#define RT5616_DAC_L2_DAC_L_VOL_SFT		12
#define RT5616_M_STO_R_DAC_R			(0x1 << 11)
#define RT5616_M_STO_R_DAC_R_SFT		11
#define RT5616_STO_R_DAC_R_VOL_MASK		(0x1 << 10)
#define RT5616_STO_R_DAC_R_VOL_SFT		10
#define RT5616_M_DAC_R2_DAC_R			(0x1 << 9)
#define RT5616_M_DAC_R2_DAC_R_SFT		9
#define RT5616_DAC_R2_DAC_R_VOL_MASK		(0x1 << 8)
#define RT5616_DAC_R2_DAC_R_VOL_SFT		8

/* DSP Path Control 1 (0x2d) */
#define RT5616_RXDP_SRC_MASK			(0x1 << 15)
#define RT5616_RXDP_SRC_SFT			15
#define RT5616_RXDP_SRC_NOR			(0x0 << 15)
#define RT5616_RXDP_SRC_DIV3			(0x1 << 15)
#define RT5616_TXDP_SRC_MASK			(0x1 << 14)
#define RT5616_TXDP_SRC_SFT			14
#define RT5616_TXDP_SRC_NOR			(0x0 << 14)
#define RT5616_TXDP_SRC_DIV3			(0x1 << 14)

/* DSP Path Control 2 (0x2e) */
#define RT5616_DAC_L2_SEL_MASK			(0x3 << 14)
#define RT5616_DAC_L2_SEL_SFT			14
#define RT5616_DAC_L2_SEL_IF2			(0x0 << 14)
#define RT5616_DAC_L2_SEL_IF3			(0x1 << 14)
#define RT5616_DAC_L2_SEL_TXDC			(0x2 << 14)
#define RT5616_DAC_L2_SEL_BASS			(0x3 << 14)
#define RT5616_DAC_R2_SEL_MASK			(0x3 << 12)
#define RT5616_DAC_R2_SEL_SFT			12
#define RT5616_DAC_R2_SEL_IF2			(0x0 << 12)
#define RT5616_DAC_R2_SEL_IF3			(0x1 << 12)
#define RT5616_DAC_R2_SEL_TXDC			(0x2 << 12)
#define RT5616_IF2_ADC_L_SEL_MASK		(0x1 << 11)
#define RT5616_IF2_ADC_L_SEL_SFT		11
#define RT5616_IF2_ADC_L_SEL_TXDP		(0x0 << 11)
#define RT5616_IF2_ADC_L_SEL_PASS		(0x1 << 11)
#define RT5616_IF2_ADC_R_SEL_MASK		(0x1 << 10)
#define RT5616_IF2_ADC_R_SEL_SFT		10
#define RT5616_IF2_ADC_R_SEL_TXDP		(0x0 << 10)
#define RT5616_IF2_ADC_R_SEL_PASS		(0x1 << 10)
#define RT5616_RXDC_SEL_MASK			(0x3 << 8)
#define RT5616_RXDC_SEL_SFT			8
#define RT5616_RXDC_SEL_NOR			(0x0 << 8)
#define RT5616_RXDC_SEL_L2R			(0x1 << 8)
#define RT5616_RXDC_SEL_R2L			(0x2 << 8)
#define RT5616_RXDC_SEL_SWAP			(0x3 << 8)
#define RT5616_RXDP_SEL_MASK			(0x3 << 6)
#define RT5616_RXDP_SEL_SFT			6
#define RT5616_RXDP_SEL_NOR			(0x0 << 6)
#define RT5616_RXDP_SEL_L2R			(0x1 << 6)
#define RT5616_RXDP_SEL_R2L			(0x2 << 6)
#define RT5616_RXDP_SEL_SWAP			(0x3 << 6)
#define RT5616_TXDC_SEL_MASK			(0x3 << 4)
#define RT5616_TXDC_SEL_SFT			4
#define RT5616_TXDC_SEL_NOR			(0x0 << 4)
#define RT5616_TXDC_SEL_L2R			(0x1 << 4)
#define RT5616_TXDC_SEL_R2L			(0x2 << 4)
#define RT5616_TXDC_SEL_SWAP			(0x3 << 4)
#define RT5616_TXDP_SEL_MASK			(0x3 << 2)
#define RT5616_TXDP_SEL_SFT			2
#define RT5616_TXDP_SEL_NOR			(0x0 << 2)
#define RT5616_TXDP_SEL_L2R			(0x1 << 2)
#define RT5616_TXDP_SEL_R2L			(0x2 << 2)
#define RT5616_TRXDP_SEL_SWAP			(0x3 << 2)

/* REC Left Mixer Control 1 (0x3b) */
#define RT5616_G_LN_L2_RM_L_MASK		(0x7 << 13)
#define RT5616_G_IN_L2_RM_L_SFT			13
#define RT5616_G_LN_L1_RM_L_MASK		(0x7 << 10)
#define RT5616_G_IN_L1_RM_L_SFT			10
#define RT5616_G_BST3_RM_L_MASK			(0x7 << 4)
#define RT5616_G_BST3_RM_L_SFT			4
#define RT5616_G_BST2_RM_L_MASK			(0x7 << 1)
#define RT5616_G_BST2_RM_L_SFT			1

/* REC Left Mixer Control 2 (0x3c) */
#define RT5616_G_BST1_RM_L_MASK			(0x7 << 13)
#define RT5616_G_BST1_RM_L_SFT			13
#define RT5616_G_OM_L_RM_L_MASK			(0x7 << 10)
#define RT5616_G_OM_L_RM_L_SFT			10
#define RT5616_M_IN2_L_RM_L			(0x1 << 6)
#define RT5616_M_IN2_L_RM_L_SFT			6
#define RT5616_M_IN1_L_RM_L			(0x1 << 5)
#define RT5616_M_IN1_L_RM_L_SFT			5
#define RT5616_M_BST3_RM_L			(0x1 << 3)
#define RT5616_M_BST3_RM_L_SFT			3
#define RT5616_M_BST2_RM_L			(0x1 << 2)
#define RT5616_M_BST2_RM_L_SFT			2
#define RT5616_M_BST1_RM_L			(0x1 << 1)
#define RT5616_M_BST1_RM_L_SFT			1
#define RT5616_M_OM_L_RM_L			(0x1)
#define RT5616_M_OM_L_RM_L_SFT			0

/* REC Right Mixer Control 1 (0x3d) */
#define RT5616_G_IN2_R_RM_R_MASK		(0x7 << 13)
#define RT5616_G_IN2_R_RM_R_SFT			13
#define RT5616_G_IN1_R_RM_R_MASK		(0x7 << 10)
#define RT5616_G_IN1_R_RM_R_SFT			10
#define RT5616_G_BST3_RM_R_MASK			(0x7 << 4)
#define RT5616_G_BST3_RM_R_SFT			4
#define RT5616_G_BST2_RM_R_MASK			(0x7 << 1)
#define RT5616_G_BST2_RM_R_SFT			1

/* REC Right Mixer Control 2 (0x3e) */
#define RT5616_G_BST1_RM_R_MASK			(0x7 << 13)
#define RT5616_G_BST1_RM_R_SFT			13
#define RT5616_G_OM_R_RM_R_MASK			(0x7 << 10)
#define RT5616_G_OM_R_RM_R_SFT			10
#define RT5616_M_IN2_R_RM_R			(0x1 << 6)
#define RT5616_M_IN2_R_RM_R_SFT			6
#define RT5616_M_IN1_R_RM_R			(0x1 << 5)
#define RT5616_M_IN1_R_RM_R_SFT			5
#define RT5616_M_BST3_RM_R			(0x1 << 3)
#define RT5616_M_BST3_RM_R_SFT			3
#define RT5616_M_BST2_RM_R			(0x1 << 2)
#define RT5616_M_BST2_RM_R_SFT			2
#define RT5616_M_BST1_RM_R			(0x1 << 1)
#define RT5616_M_BST1_RM_R_SFT			1
#define RT5616_M_OM_R_RM_R			(0x1)
#define RT5616_M_OM_R_RM_R_SFT			0

/* HPMIX Control (0x45) */
#define RT5616_M_DAC1_HM			(0x1 << 14)
#define RT5616_M_DAC1_HM_SFT			14
#define RT5616_M_HPVOL_HM			(0x1 << 13)
#define RT5616_M_HPVOL_HM_SFT			13
#define RT5616_G_HPOMIX_MASK			(0x1 << 12)
#define RT5616_G_HPOMIX_SFT			12

/* SPK Left Mixer Control (0x46) */
#define RT5616_G_RM_L_SM_L_MASK			(0x3 << 14)
#define RT5616_G_RM_L_SM_L_SFT			14
#define RT5616_G_IN_L_SM_L_MASK			(0x3 << 12)
#define RT5616_G_IN_L_SM_L_SFT			12
#define RT5616_G_DAC_L1_SM_L_MASK		(0x3 << 10)
#define RT5616_G_DAC_L1_SM_L_SFT		10
#define RT5616_G_DAC_L2_SM_L_MASK		(0x3 << 8)
#define RT5616_G_DAC_L2_SM_L_SFT		8
#define RT5616_G_OM_L_SM_L_MASK			(0x3 << 6)
#define RT5616_G_OM_L_SM_L_SFT			6
#define RT5616_M_RM_L_SM_L			(0x1 << 5)
#define RT5616_M_RM_L_SM_L_SFT			5
#define RT5616_M_IN_L_SM_L			(0x1 << 4)
#define RT5616_M_IN_L_SM_L_SFT			4
#define RT5616_M_DAC_L1_SM_L			(0x1 << 3)
#define RT5616_M_DAC_L1_SM_L_SFT		3
#define RT5616_M_DAC_L2_SM_L			(0x1 << 2)
#define RT5616_M_DAC_L2_SM_L_SFT		2
#define RT5616_M_OM_L_SM_L			(0x1 << 1)
#define RT5616_M_OM_L_SM_L_SFT			1

/* SPK Right Mixer Control (0x47) */
#define RT5616_G_RM_R_SM_R_MASK			(0x3 << 14)
#define RT5616_G_RM_R_SM_R_SFT			14
#define RT5616_G_IN_R_SM_R_MASK			(0x3 << 12)
#define RT5616_G_IN_R_SM_R_SFT			12
#define RT5616_G_DAC_R1_SM_R_MASK		(0x3 << 10)
#define RT5616_G_DAC_R1_SM_R_SFT		10
#define RT5616_G_DAC_R2_SM_R_MASK		(0x3 << 8)
#define RT5616_G_DAC_R2_SM_R_SFT		8
#define RT5616_G_OM_R_SM_R_MASK			(0x3 << 6)
#define RT5616_G_OM_R_SM_R_SFT			6
#define RT5616_M_RM_R_SM_R			(0x1 << 5)
#define RT5616_M_RM_R_SM_R_SFT			5
#define RT5616_M_IN_R_SM_R			(0x1 << 4)
#define RT5616_M_IN_R_SM_R_SFT			4
#define RT5616_M_DAC_R1_SM_R			(0x1 << 3)
#define RT5616_M_DAC_R1_SM_R_SFT		3
#define RT5616_M_DAC_R2_SM_R			(0x1 << 2)
#define RT5616_M_DAC_R2_SM_R_SFT		2
#define RT5616_M_OM_R_SM_R			(0x1 << 1)
#define RT5616_M_OM_R_SM_R_SFT			1

/* SPOLMIX Control (0x48) */
#define RT5616_M_DAC_R1_SPM_L			(0x1 << 15)
#define RT5616_M_DAC_R1_SPM_L_SFT		15
#define RT5616_M_DAC_L1_SPM_L			(0x1 << 14)
#define RT5616_M_DAC_L1_SPM_L_SFT		14
#define RT5616_M_SV_R_SPM_L			(0x1 << 13)
#define RT5616_M_SV_R_SPM_L_SFT			13
#define RT5616_M_SV_L_SPM_L			(0x1 << 12)
#define RT5616_M_SV_L_SPM_L_SFT			12
#define RT5616_M_BST1_SPM_L			(0x1 << 11)
#define RT5616_M_BST1_SPM_L_SFT			11

/* SPORMIX Control (0x49) */
#define RT5616_M_DAC_R1_SPM_R			(0x1 << 13)
#define RT5616_M_DAC_R1_SPM_R_SFT		13
#define RT5616_M_SV_R_SPM_R			(0x1 << 12)
#define RT5616_M_SV_R_SPM_R_SFT			12
#define RT5616_M_BST1_SPM_R			(0x1 << 11)
#define RT5616_M_BST1_SPM_R_SFT			11

/* SPOLMIX / SPORMIX Ratio Control (0x4a) */
#define RT5616_SPO_CLSD_RATIO_MASK		(0x7)
#define RT5616_SPO_CLSD_RATIO_SFT		0

/* Mono Output Mixer Control (0x4c) */
#define RT5616_M_DAC_R2_MM			(0x1 << 15)
#define RT5616_M_DAC_R2_MM_SFT			15
#define RT5616_M_DAC_L2_MM			(0x1 << 14)
#define RT5616_M_DAC_L2_MM_SFT			14
#define RT5616_M_OV_R_MM			(0x1 << 13)
#define RT5616_M_OV_R_MM_SFT			13
#define RT5616_M_OV_L_MM			(0x1 << 12)
#define RT5616_M_OV_L_MM_SFT			12
#define RT5616_M_BST1_MM			(0x1 << 11)
#define RT5616_M_BST1_MM_SFT			11
#define RT5616_G_MONOMIX_MASK			(0x1 << 10)
#define RT5616_G_MONOMIX_SFT			10

/* Output Left Mixer Control 1 (0x4d) */
#define RT5616_G_BST2_OM_L_MASK			(0x7 << 10)
#define RT5616_G_BST2_OM_L_SFT			10
#define RT5616_G_BST1_OM_L_MASK			(0x7 << 7)
#define RT5616_G_BST1_OM_L_SFT			7
#define RT5616_G_IN1_L_OM_L_MASK		(0x7 << 4)
#define RT5616_G_IN1_L_OM_L_SFT			4
#define RT5616_G_RM_L_OM_L_MASK			(0x7 << 1)
#define RT5616_G_RM_L_OM_L_SFT			1

/* Output Left Mixer Control 2 (0x4e) */
#define RT5616_G_DAC_L1_OM_L_MASK		(0x7 << 7)
#define RT5616_G_DAC_L1_OM_L_SFT		7
#define RT5616_G_IN2_L_OM_L_MASK		(0x7 << 4)
#define RT5616_G_IN2_L_OM_L_SFT			4

/* Output Left Mixer Control 3 (0x4f) */
#define RT5616_M_IN2_L_OM_L			(0x1 << 9)
#define RT5616_M_IN2_L_OM_L_SFT			9
#define RT5616_M_BST2_OM_L			(0x1 << 6)
#define RT5616_M_BST2_OM_L_SFT			6
#define RT5616_M_BST1_OM_L			(0x1 << 5)
#define RT5616_M_BST1_OM_L_SFT			5
#define RT5616_M_IN1_L_OM_L			(0x1 << 4)
#define RT5616_M_IN1_L_OM_L_SFT			4
#define RT5616_M_RM_L_OM_L			(0x1 << 3)
#define RT5616_M_RM_L_OM_L_SFT			3
#define RT5616_M_DAC_L1_OM_L			(0x1)
#define RT5616_M_DAC_L1_OM_L_SFT		0

/* Output Right Mixer Control 1 (0x50) */
#define RT5616_G_BST2_OM_R_MASK			(0x7 << 10)
#define RT5616_G_BST2_OM_R_SFT			10
#define RT5616_G_BST1_OM_R_MASK			(0x7 << 7)
#define RT5616_G_BST1_OM_R_SFT			7
#define RT5616_G_IN1_R_OM_R_MASK		(0x7 << 4)
#define RT5616_G_IN1_R_OM_R_SFT			4
#define RT5616_G_RM_R_OM_R_MASK			(0x7 << 1)
#define RT5616_G_RM_R_OM_R_SFT			1

/* Output Right Mixer Control 2 (0x51) */
#define RT5616_G_DAC_R1_OM_R_MASK		(0x7 << 7)
#define RT5616_G_DAC_R1_OM_R_SFT		7
#define RT5616_G_IN2_R_OM_R_MASK		(0x7 << 4)
#define RT5616_G_IN2_R_OM_R_SFT			4

/* Output Right Mixer Control 3 (0x52) */
#define RT5616_M_IN2_R_OM_R			(0x1 << 9)
#define RT5616_M_IN2_R_OM_R_SFT			9
#define RT5616_M_BST2_OM_R			(0x1 << 6)
#define RT5616_M_BST2_OM_R_SFT			6
#define RT5616_M_BST1_OM_R			(0x1 << 5)
#define RT5616_M_BST1_OM_R_SFT			5
#define RT5616_M_IN1_R_OM_R			(0x1 << 4)
#define RT5616_M_IN1_R_OM_R_SFT			4
#define RT5616_M_RM_R_OM_R			(0x1 << 3)
#define RT5616_M_RM_R_OM_R_SFT			3
#define RT5616_M_DAC_R1_OM_R			(0x1)
#define RT5616_M_DAC_R1_OM_R_SFT		0

/* LOUT Mixer Control (0x53) */
#define RT5616_M_DAC_L1_LM			(0x1 << 15)
#define RT5616_M_DAC_L1_LM_SFT			15
#define RT5616_M_DAC_R1_LM			(0x1 << 14)
#define RT5616_M_DAC_R1_LM_SFT			14
#define RT5616_M_OV_L_LM			(0x1 << 13)
#define RT5616_M_OV_L_LM_SFT			13
#define RT5616_M_OV_R_LM			(0x1 << 12)
#define RT5616_M_OV_R_LM_SFT			12
#define RT5616_G_LOUTMIX_MASK			(0x1 << 11)
#define RT5616_G_LOUTMIX_SFT			11

/* Power Management for Digital 1 (0x61) */
#define RT5616_PWR_I2S1				(0x1 << 15)
#define RT5616_PWR_I2S1_BIT			15
#define RT5616_PWR_I2S2				(0x1 << 14)
#define RT5616_PWR_I2S2_BIT			14
#define RT5616_PWR_DAC_L1			(0x1 << 12)
#define RT5616_PWR_DAC_L1_BIT			12
#define RT5616_PWR_DAC_R1			(0x1 << 11)
#define RT5616_PWR_DAC_R1_BIT			11
#define RT5616_PWR_ADC_L			(0x1 << 2)
#define RT5616_PWR_ADC_L_BIT			2
#define RT5616_PWR_ADC_R			(0x1 << 1)
#define RT5616_PWR_ADC_R_BIT			1

/* Power Management for Digital 2 (0x62) */
#define RT5616_PWR_ADC_STO1_F			(0x1 << 15)
#define RT5616_PWR_ADC_STO1_F_BIT		15
#define RT5616_PWR_DAC_STO1_F			(0x1 << 11)
#define RT5616_PWR_DAC_STO1_F_BIT		11

/* Power Management for Analog 1 (0x63) */
#define RT5616_PWR_VREF1			(0x1 << 15)
#define RT5616_PWR_VREF1_BIT			15
#define RT5616_PWR_FV1				(0x1 << 14)
#define RT5616_PWR_FV1_BIT			14
#define RT5616_PWR_MB				(0x1 << 13)
#define RT5616_PWR_MB_BIT			13
#define RT5616_PWR_LM				(0x1 << 12)
#define RT5616_PWR_LM_BIT			12
#define RT5616_PWR_BG				(0x1 << 11)
#define RT5616_PWR_BG_BIT			11
#define RT5616_PWR_HP_L				(0x1 << 7)
#define RT5616_PWR_HP_L_BIT			7
#define RT5616_PWR_HP_R				(0x1 << 6)
#define RT5616_PWR_HP_R_BIT			6
#define RT5616_PWR_HA				(0x1 << 5)
#define RT5616_PWR_HA_BIT			5
#define RT5616_PWR_VREF2			(0x1 << 4)
#define RT5616_PWR_VREF2_BIT			4
#define RT5616_PWR_FV2				(0x1 << 3)
#define RT5616_PWR_FV2_BIT			3
#define RT5616_PWR_LDO				(0x1 << 2)
#define RT5616_PWR_LDO_BIT			2
#define RT5616_PWR_LDO_DVO_MASK			(0x3)
#define RT5616_PWR_LDO_DVO_1_0V			0
#define RT5616_PWR_LDO_DVO_1_1V			1
#define RT5616_PWR_LDO_DVO_1_2V			2
#define RT5616_PWR_LDO_DVO_1_3V			3

/* Power Management for Analog 2 (0x64) */
#define RT5616_PWR_BST1				(0x1 << 15)
#define RT5616_PWR_BST1_BIT			15
#define RT5616_PWR_BST2				(0x1 << 14)
#define RT5616_PWR_BST2_BIT			14
#define RT5616_PWR_MB1				(0x1 << 11)
#define RT5616_PWR_MB1_BIT			11
#define RT5616_PWR_PLL				(0x1 << 9)
#define RT5616_PWR_PLL_BIT			9
#define RT5616_PWR_BST1_OP2			(0x1 << 5)
#define RT5616_PWR_BST1_OP2_BIT			5
#define RT5616_PWR_BST2_OP2			(0x1 << 4)
#define RT5616_PWR_BST2_OP2_BIT			4
#define RT5616_PWR_BST3_OP2			(0x1 << 3)
#define RT5616_PWR_BST3_OP2_BIT			3
#define RT5616_PWR_JD_M				(0x1 << 2)
#define RT5616_PWM_JD_M_BIT			2
#define RT5616_PWR_JD2				(0x1 << 1)
#define RT5616_PWM_JD2_BIT			1
#define RT5616_PWR_JD3				(0x1)
#define RT5616_PWM_JD3_BIT			0

/* Power Management for Mixer (0x65) */
#define RT5616_PWR_OM_L				(0x1 << 15)
#define RT5616_PWR_OM_L_BIT			15
#define RT5616_PWR_OM_R				(0x1 << 14)
#define RT5616_PWR_OM_R_BIT			14
#define RT5616_PWR_RM_L				(0x1 << 11)
#define RT5616_PWR_RM_L_BIT			11
#define RT5616_PWR_RM_R				(0x1 << 10)
#define RT5616_PWR_RM_R_BIT			10

/* Power Management for Volume (0x66) */
#define RT5616_PWR_OV_L				(0x1 << 13)
#define RT5616_PWR_OV_L_BIT			13
#define RT5616_PWR_OV_R				(0x1 << 12)
#define RT5616_PWR_OV_R_BIT			12
#define RT5616_PWR_HV_L				(0x1 << 11)
#define RT5616_PWR_HV_L_BIT			11
#define RT5616_PWR_HV_R				(0x1 << 10)
#define RT5616_PWR_HV_R_BIT			10
#define RT5616_PWR_IN1_L			(0x1 << 9)
#define RT5616_PWR_IN1_L_BIT			9
#define RT5616_PWR_IN1_R			(0x1 << 8)
#define RT5616_PWR_IN1_R_BIT			8
#define RT5616_PWR_IN2_L			(0x1 << 7)
#define RT5616_PWR_IN2_L_BIT			7
#define RT5616_PWR_IN2_R			(0x1 << 6)
#define RT5616_PWR_IN2_R_BIT			6

/* I2S1/2/3 Audio Serial Data Port Control (0x70 0x71) */
#define RT5616_I2S_MS_MASK			(0x1 << 15)
#define RT5616_I2S_MS_SFT			15
#define RT5616_I2S_MS_M				(0x0 << 15)
#define RT5616_I2S_MS_S				(0x1 << 15)
#define RT5616_I2S_O_CP_MASK			(0x3 << 10)
#define RT5616_I2S_O_CP_SFT			10
#define RT5616_I2S_O_CP_OFF			(0x0 << 10)
#define RT5616_I2S_O_CP_U_LAW			(0x1 << 10)
#define RT5616_I2S_O_CP_A_LAW			(0x2 << 10)
#define RT5616_I2S_I_CP_MASK			(0x3 << 8)
#define RT5616_I2S_I_CP_SFT			8
#define RT5616_I2S_I_CP_OFF			(0x0 << 8)
#define RT5616_I2S_I_CP_U_LAW			(0x1 << 8)
#define RT5616_I2S_I_CP_A_LAW			(0x2 << 8)
#define RT5616_I2S_BP_MASK			(0x1 << 7)
#define RT5616_I2S_BP_SFT			7
#define RT5616_I2S_BP_NOR			(0x0 << 7)
#define RT5616_I2S_BP_INV			(0x1 << 7)
#define RT5616_I2S_DL_MASK			(0x3 << 2)
#define RT5616_I2S_DL_SFT			2
#define RT5616_I2S_DL_16			(0x0 << 2)
#define RT5616_I2S_DL_20			(0x1 << 2)
#define RT5616_I2S_DL_24			(0x2 << 2)
#define RT5616_I2S_DL_8				(0x3 << 2)
#define RT5616_I2S_DF_MASK			(0x3)
#define RT5616_I2S_DF_SFT			0
#define RT5616_I2S_DF_I2S			(0x0)
#define RT5616_I2S_DF_LEFT			(0x1)
#define RT5616_I2S_DF_PCM_A			(0x2)
#define RT5616_I2S_DF_PCM_B			(0x3)

/* ADC/DAC Clock Control 1 (0x73) */
#define RT5616_I2S_PD1_MASK			(0x7 << 12)
#define RT5616_I2S_PD1_SFT			12
#define RT5616_I2S_PD1_1			(0x0 << 12)
#define RT5616_I2S_PD1_2			(0x1 << 12)
#define RT5616_I2S_PD1_3			(0x2 << 12)
#define RT5616_I2S_PD1_4			(0x3 << 12)
#define RT5616_I2S_PD1_6			(0x4 << 12)
#define RT5616_I2S_PD1_8			(0x5 << 12)
#define RT5616_I2S_PD1_12			(0x6 << 12)
#define RT5616_I2S_PD1_16			(0x7 << 12)
#define RT5616_I2S_BCLK_MS2_MASK		(0x1 << 11)
#define RT5616_DAC_OSR_MASK			(0x3 << 2)
#define RT5616_DAC_OSR_SFT			2
#define RT5616_DAC_OSR_128			(0x0 << 2)
#define RT5616_DAC_OSR_64			(0x1 << 2)
#define RT5616_DAC_OSR_32			(0x2 << 2)
#define RT5616_DAC_OSR_128_3			(0x3 << 2)
#define RT5616_ADC_OSR_MASK			(0x3)
#define RT5616_ADC_OSR_SFT			0
#define RT5616_ADC_OSR_128			(0x0)
#define RT5616_ADC_OSR_64			(0x1)
#define RT5616_ADC_OSR_32			(0x2)
#define RT5616_ADC_OSR_128_3			(0x3)

/* ADC/DAC Clock Control 2 (0x74) */
#define RT5616_DAHPF_EN				(0x1 << 11)
#define RT5616_DAHPF_EN_SFT			11
#define RT5616_ADHPF_EN				(0x1 << 10)
#define RT5616_ADHPF_EN_SFT			10

/* TDM Control 1 (0x77) */
#define RT5616_TDM_INTEL_SEL_MASK		(0x1 << 15)
#define RT5616_TDM_INTEL_SEL_SFT		15
#define RT5616_TDM_INTEL_SEL_64			(0x0 << 15)
#define RT5616_TDM_INTEL_SEL_50			(0x1 << 15)
#define RT5616_TDM_MODE_SEL_MASK		(0x1 << 14)
#define RT5616_TDM_MODE_SEL_SFT			14
#define RT5616_TDM_MODE_SEL_NOR			(0x0 << 14)
#define RT5616_TDM_MODE_SEL_TDM			(0x1 << 14)
#define RT5616_TDM_CH_NUM_SEL_MASK		(0x3 << 12)
#define RT5616_TDM_CH_NUM_SEL_SFT		12
#define RT5616_TDM_CH_NUM_SEL_2			(0x0 << 12)
#define RT5616_TDM_CH_NUM_SEL_4			(0x1 << 12)
#define RT5616_TDM_CH_NUM_SEL_6			(0x2 << 12)
#define RT5616_TDM_CH_NUM_SEL_8			(0x3 << 12)
#define RT5616_TDM_CH_LEN_SEL_MASK		(0x3 << 10)
#define RT5616_TDM_CH_LEN_SEL_SFT		10
#define RT5616_TDM_CH_LEN_SEL_16		(0x0 << 10)
#define RT5616_TDM_CH_LEN_SEL_20		(0x1 << 10)
#define RT5616_TDM_CH_LEN_SEL_24		(0x2 << 10)
#define RT5616_TDM_CH_LEN_SEL_32		(0x3 << 10)
#define RT5616_TDM_ADC_SEL_MASK			(0x1 << 9)
#define RT5616_TDM_ADC_SEL_SFT			9
#define RT5616_TDM_ADC_SEL_NOR			(0x0 << 9)
#define RT5616_TDM_ADC_SEL_SWAP			(0x1 << 9)
#define RT5616_TDM_ADC_START_SEL_MASK		(0x1 << 8)
#define RT5616_TDM_ADC_START_SEL_SFT		8
#define RT5616_TDM_ADC_START_SEL_SL0		(0x0 << 8)
#define RT5616_TDM_ADC_START_SEL_SL4		(0x1 << 8)
#define RT5616_TDM_I2S_CH2_SEL_MASK		(0x3 << 6)
#define RT5616_TDM_I2S_CH2_SEL_SFT		6
#define RT5616_TDM_I2S_CH2_SEL_LR		(0x0 << 6)
#define RT5616_TDM_I2S_CH2_SEL_RL		(0x1 << 6)
#define RT5616_TDM_I2S_CH2_SEL_LL		(0x2 << 6)
#define RT5616_TDM_I2S_CH2_SEL_RR		(0x3 << 6)
#define RT5616_TDM_I2S_CH4_SEL_MASK		(0x3 << 4)
#define RT5616_TDM_I2S_CH4_SEL_SFT		4
#define RT5616_TDM_I2S_CH4_SEL_LR		(0x0 << 4)
#define RT5616_TDM_I2S_CH4_SEL_RL		(0x1 << 4)
#define RT5616_TDM_I2S_CH4_SEL_LL		(0x2 << 4)
#define RT5616_TDM_I2S_CH4_SEL_RR		(0x3 << 4)
#define RT5616_TDM_I2S_CH6_SEL_MASK		(0x3 << 2)
#define RT5616_TDM_I2S_CH6_SEL_SFT		2
#define RT5616_TDM_I2S_CH6_SEL_LR		(0x0 << 2)
#define RT5616_TDM_I2S_CH6_SEL_RL		(0x1 << 2)
#define RT5616_TDM_I2S_CH6_SEL_LL		(0x2 << 2)
#define RT5616_TDM_I2S_CH6_SEL_RR		(0x3 << 2)
#define RT5616_TDM_I2S_CH8_SEL_MASK		(0x3)
#define RT5616_TDM_I2S_CH8_SEL_SFT		0
#define RT5616_TDM_I2S_CH8_SEL_LR		(0x0)
#define RT5616_TDM_I2S_CH8_SEL_RL		(0x1)
#define RT5616_TDM_I2S_CH8_SEL_LL		(0x2)
#define RT5616_TDM_I2S_CH8_SEL_RR		(0x3)

/* TDM Control 2 (0x78) */
#define RT5616_TDM_LRCK_POL_SEL_MASK		(0x1 << 15)
#define RT5616_TDM_LRCK_POL_SEL_SFT		15
#define RT5616_TDM_LRCK_POL_SEL_NOR		(0x0 << 15)
#define RT5616_TDM_LRCK_POL_SEL_INV		(0x1 << 15)
#define RT5616_TDM_CH_VAL_SEL_MASK		(0x1 << 14)
#define RT5616_TDM_CH_VAL_SEL_SFT		14
#define RT5616_TDM_CH_VAL_SEL_CH01		(0x0 << 14)
#define RT5616_TDM_CH_VAL_SEL_CH0123		(0x1 << 14)
#define RT5616_TDM_CH_VAL_EN			(0x1 << 13)
#define RT5616_TDM_CH_VAL_SFT			13
#define RT5616_TDM_LPBK_EN			(0x1 << 12)
#define RT5616_TDM_LPBK_SFT			12
#define RT5616_TDM_LRCK_PULSE_SEL_MASK		(0x1 << 11)
#define RT5616_TDM_LRCK_PULSE_SEL_SFT		11
#define RT5616_TDM_LRCK_PULSE_SEL_BCLK		(0x0 << 11)
#define RT5616_TDM_LRCK_PULSE_SEL_CH		(0x1 << 11)
#define RT5616_TDM_END_EDGE_SEL_MASK		(0x1 << 10)
#define RT5616_TDM_END_EDGE_SEL_SFT		10
#define RT5616_TDM_END_EDGE_SEL_POS		(0x0 << 10)
#define RT5616_TDM_END_EDGE_SEL_NEG		(0x1 << 10)
#define RT5616_TDM_END_EDGE_EN			(0x1 << 9)
#define RT5616_TDM_END_EDGE_EN_SFT		9
#define RT5616_TDM_TRAN_EDGE_SEL_MASK		(0x1 << 8)
#define RT5616_TDM_TRAN_EDGE_SEL_SFT		8
#define RT5616_TDM_TRAN_EDGE_SEL_POS		(0x0 << 8)
#define RT5616_TDM_TRAN_EDGE_SEL_NEG		(0x1 << 8)
#define RT5616_M_TDM2_L				(0x1 << 7)
#define RT5616_M_TDM2_L_SFT			7
#define RT5616_M_TDM2_R				(0x1 << 6)
#define RT5616_M_TDM2_R_SFT			6
#define RT5616_M_TDM4_L				(0x1 << 5)
#define RT5616_M_TDM4_L_SFT			5
#define RT5616_M_TDM4_R				(0x1 << 4)
#define RT5616_M_TDM4_R_SFT			4

/* Global Clock Control (0x80) */
#define RT5616_SCLK_SRC_MASK			(0x3 << 14)
#define RT5616_SCLK_SRC_SFT			14
#define RT5616_SCLK_SRC_MCLK			(0x0 << 14)
#define RT5616_SCLK_SRC_PLL1			(0x1 << 14)
#define RT5616_PLL1_SRC_MASK			(0x3 << 12)
#define RT5616_PLL1_SRC_SFT			12
#define RT5616_PLL1_SRC_MCLK			(0x0 << 12)
#define RT5616_PLL1_SRC_BCLK1			(0x1 << 12)
#define RT5616_PLL1_SRC_BCLK2			(0x2 << 12)
#define RT5616_PLL1_PD_MASK			(0x1 << 3)
#define RT5616_PLL1_PD_SFT			3
#define RT5616_PLL1_PD_1			(0x0 << 3)
#define RT5616_PLL1_PD_2			(0x1 << 3)

#define RT5616_PLL_INP_MAX			40000000
#define RT5616_PLL_INP_MIN			256000
/* PLL M/N/K Code Control 1 (0x81) */
#define RT5616_PLL_N_MAX			0x1ff
#define RT5616_PLL_N_MASK			(RT5616_PLL_N_MAX << 7)
#define RT5616_PLL_N_SFT			7
#define RT5616_PLL_K_MAX			0x1f
#define RT5616_PLL_K_MASK			(RT5616_PLL_K_MAX)
#define RT5616_PLL_K_SFT			0

/* PLL M/N/K Code Control 2 (0x82) */
#define RT5616_PLL_M_MAX			0xf
#define RT5616_PLL_M_MASK			(RT5616_PLL_M_MAX << 12)
#define RT5616_PLL_M_SFT			12
#define RT5616_PLL_M_BP				(0x1 << 11)
#define RT5616_PLL_M_BP_SFT			11

/* PLL tracking mode 1 (0x83) */
#define RT5616_STO1_T_MASK			(0x1 << 15)
#define RT5616_STO1_T_SFT			15
#define RT5616_STO1_T_SCLK			(0x0 << 15)
#define RT5616_STO1_T_LRCK1			(0x1 << 15)
#define RT5616_STO2_T_MASK			(0x1 << 12)
#define RT5616_STO2_T_SFT			12
#define RT5616_STO2_T_I2S2			(0x0 << 12)
#define RT5616_STO2_T_LRCK2			(0x1 << 12)
#define RT5616_ASRC2_REF_MASK			(0x1 << 11)
#define RT5616_ASRC2_REF_SFT			11
#define RT5616_ASRC2_REF_LRCK2			(0x0 << 11)
#define RT5616_ASRC2_REF_LRCK1			(0x1 << 11)
#define RT5616_DMIC_1_M_MASK			(0x1 << 9)
#define RT5616_DMIC_1_M_SFT			9
#define RT5616_DMIC_1_M_NOR			(0x0 << 9)
#define RT5616_DMIC_1_M_ASYN			(0x1 << 9)

/* PLL tracking mode 2 (0x84) */
#define RT5616_STO1_ASRC_EN			(0x1 << 15)
#define RT5616_STO1_ASRC_EN_SFT			15
#define RT5616_STO2_ASRC_EN			(0x1 << 14)
#define RT5616_STO2_ASRC_EN_SFT			14
#define RT5616_STO1_DAC_M_MASK			(0x1 << 13)
#define RT5616_STO1_DAC_M_SFT			13
#define RT5616_STO1_DAC_M_NOR			(0x0 << 13)
#define RT5616_STO1_DAC_M_ASRC			(0x1 << 13)
#define RT5616_STO2_DAC_M_MASK			(0x1 << 12)
#define RT5616_STO2_DAC_M_SFT			12
#define RT5616_STO2_DAC_M_NOR			(0x0 << 12)
#define RT5616_STO2_DAC_M_ASRC			(0x1 << 12)
#define RT5616_ADC_M_MASK			(0x1 << 11)
#define RT5616_ADC_M_SFT			11
#define RT5616_ADC_M_NOR			(0x0 << 11)
#define RT5616_ADC_M_ASRC			(0x1 << 11)
#define RT5616_I2S1_R_D_MASK			(0x1 << 4)
#define RT5616_I2S1_R_D_SFT			4
#define RT5616_I2S1_R_D_DIS			(0x0 << 4)
#define RT5616_I2S1_R_D_EN			(0x1 << 4)
#define RT5616_I2S2_R_D_MASK			(0x1 << 3)
#define RT5616_I2S2_R_D_SFT			3
#define RT5616_I2S2_R_D_DIS			(0x0 << 3)
#define RT5616_I2S2_R_D_EN			(0x1 << 3)
#define RT5616_PRE_SCLK_MASK			(0x3)
#define RT5616_PRE_SCLK_SFT			0
#define RT5616_PRE_SCLK_512			(0x0)
#define RT5616_PRE_SCLK_1024			(0x1)
#define RT5616_PRE_SCLK_2048			(0x2)

/* PLL tracking mode 3 (0x85) */
#define RT5616_I2S1_RATE_MASK			(0xf << 12)
#define RT5616_I2S1_RATE_SFT			12
#define RT5616_I2S2_RATE_MASK			(0xf << 8)
#define RT5616_I2S2_RATE_SFT			8
#define RT5616_G_ASRC_LP_MASK			(0x1 << 3)
#define RT5616_G_ASRC_LP_SFT			3
#define RT5616_ASRC_LP_F_M			(0x1 << 2)
#define RT5616_ASRC_LP_F_SFT			2
#define RT5616_ASRC_LP_F_NOR			(0x0 << 2)
#define RT5616_ASRC_LP_F_SB			(0x1 << 2)
#define RT5616_FTK_PH_DET_MASK			(0x3)
#define RT5616_FTK_PH_DET_SFT			0
#define RT5616_FTK_PH_DET_DIV1			(0x0)
#define RT5616_FTK_PH_DET_DIV2			(0x1)
#define RT5616_FTK_PH_DET_DIV4			(0x2)
#define RT5616_FTK_PH_DET_DIV8			(0x3)

/*PLL tracking mode 6 (0x89) */
#define RT5616_I2S1_PD_MASK			(0x7 << 12)
#define RT5616_I2S1_PD_SFT			12
#define RT5616_I2S2_PD_MASK			(0x7 << 8)
#define RT5616_I2S2_PD_SFT			8

/*PLL tracking mode 7 (0x8a) */
#define RT5616_FSI1_RATE_MASK			(0xf << 12)
#define RT5616_FSI1_RATE_SFT			12
#define RT5616_FSI2_RATE_MASK			(0xf << 8)
#define RT5616_FSI2_RATE_SFT			8

/* HPOUT Over Current Detection (0x8b) */
#define RT5616_HP_OVCD_MASK			(0x1 << 10)
#define RT5616_HP_OVCD_SFT			10
#define RT5616_HP_OVCD_DIS			(0x0 << 10)
#define RT5616_HP_OVCD_EN			(0x1 << 10)
#define RT5616_HP_OC_TH_MASK			(0x3 << 8)
#define RT5616_HP_OC_TH_SFT			8
#define RT5616_HP_OC_TH_90			(0x0 << 8)
#define RT5616_HP_OC_TH_105			(0x1 << 8)
#define RT5616_HP_OC_TH_120			(0x2 << 8)
#define RT5616_HP_OC_TH_135			(0x3 << 8)

/* Depop Mode Control 1 (0x8e) */
#define RT5616_SMT_TRIG_MASK			(0x1 << 15)
#define RT5616_SMT_TRIG_SFT			15
#define RT5616_SMT_TRIG_DIS			(0x0 << 15)
#define RT5616_SMT_TRIG_EN			(0x1 << 15)
#define RT5616_HP_L_SMT_MASK			(0x1 << 9)
#define RT5616_HP_L_SMT_SFT			9
#define RT5616_HP_L_SMT_DIS			(0x0 << 9)
#define RT5616_HP_L_SMT_EN			(0x1 << 9)
#define RT5616_HP_R_SMT_MASK			(0x1 << 8)
#define RT5616_HP_R_SMT_SFT			8
#define RT5616_HP_R_SMT_DIS			(0x0 << 8)
#define RT5616_HP_R_SMT_EN			(0x1 << 8)
#define RT5616_HP_CD_PD_MASK			(0x1 << 7)
#define RT5616_HP_CD_PD_SFT			7
#define RT5616_HP_CD_PD_DIS			(0x0 << 7)
#define RT5616_HP_CD_PD_EN			(0x1 << 7)
#define RT5616_RSTN_MASK			(0x1 << 6)
#define RT5616_RSTN_SFT				6
#define RT5616_RSTN_DIS				(0x0 << 6)
#define RT5616_RSTN_EN				(0x1 << 6)
#define RT5616_RSTP_MASK			(0x1 << 5)
#define RT5616_RSTP_SFT				5
#define RT5616_RSTP_DIS				(0x0 << 5)
#define RT5616_RSTP_EN				(0x1 << 5)
#define RT5616_HP_CO_MASK			(0x1 << 4)
#define RT5616_HP_CO_SFT			4
#define RT5616_HP_CO_DIS			(0x0 << 4)
#define RT5616_HP_CO_EN				(0x1 << 4)
#define RT5616_HP_CP_MASK			(0x1 << 3)
#define RT5616_HP_CP_SFT			3
#define RT5616_HP_CP_PD				(0x0 << 3)
#define RT5616_HP_CP_PU				(0x1 << 3)
#define RT5616_HP_SG_MASK			(0x1 << 2)
#define RT5616_HP_SG_SFT			2
#define RT5616_HP_SG_DIS			(0x0 << 2)
#define RT5616_HP_SG_EN				(0x1 << 2)
#define RT5616_HP_DP_MASK			(0x1 << 1)
#define RT5616_HP_DP_SFT			1
#define RT5616_HP_DP_PD				(0x0 << 1)
#define RT5616_HP_DP_PU				(0x1 << 1)
#define RT5616_HP_CB_MASK			(0x1)
#define RT5616_HP_CB_SFT			0
#define RT5616_HP_CB_PD				(0x0)
#define RT5616_HP_CB_PU				(0x1)

/* Depop Mode Control 2 (0x8f) */
#define RT5616_DEPOP_MASK			(0x1 << 13)
#define RT5616_DEPOP_SFT			13
#define RT5616_DEPOP_AUTO			(0x0 << 13)
#define RT5616_DEPOP_MAN			(0x1 << 13)
#define RT5616_RAMP_MASK			(0x1 << 12)
#define RT5616_RAMP_SFT				12
#define RT5616_RAMP_DIS				(0x0 << 12)
#define RT5616_RAMP_EN				(0x1 << 12)
#define RT5616_BPS_MASK				(0x1 << 11)
#define RT5616_BPS_SFT				11
#define RT5616_BPS_DIS				(0x0 << 11)
#define RT5616_BPS_EN				(0x1 << 11)
#define RT5616_FAST_UPDN_MASK			(0x1 << 10)
#define RT5616_FAST_UPDN_SFT			10
#define RT5616_FAST_UPDN_DIS			(0x0 << 10)
#define RT5616_FAST_UPDN_EN			(0x1 << 10)
#define RT5616_MRES_MASK			(0x3 << 8)
#define RT5616_MRES_SFT				8
#define RT5616_MRES_15MO			(0x0 << 8)
#define RT5616_MRES_25MO			(0x1 << 8)
#define RT5616_MRES_35MO			(0x2 << 8)
#define RT5616_MRES_45MO			(0x3 << 8)
#define RT5616_VLO_MASK				(0x1 << 7)
#define RT5616_VLO_SFT				7
#define RT5616_VLO_3V				(0x0 << 7)
#define RT5616_VLO_32V				(0x1 << 7)
#define RT5616_DIG_DP_MASK			(0x1 << 6)
#define RT5616_DIG_DP_SFT			6
#define RT5616_DIG_DP_DIS			(0x0 << 6)
#define RT5616_DIG_DP_EN			(0x1 << 6)
#define RT5616_DP_TH_MASK			(0x3 << 4)
#define RT5616_DP_TH_SFT			4

/* Depop Mode Control 3 (0x90) */
#define RT5616_CP_SYS_MASK			(0x7 << 12)
#define RT5616_CP_SYS_SFT			12
#define RT5616_CP_FQ1_MASK			(0x7 << 8)
#define RT5616_CP_FQ1_SFT			8
#define RT5616_CP_FQ2_MASK			(0x7 << 4)
#define RT5616_CP_FQ2_SFT			4
#define RT5616_CP_FQ3_MASK			(0x7)
#define RT5616_CP_FQ3_SFT			0
#define RT5616_CP_FQ_1_5_KHZ			0
#define RT5616_CP_FQ_3_KHZ			1
#define RT5616_CP_FQ_6_KHZ			2
#define RT5616_CP_FQ_12_KHZ			3
#define RT5616_CP_FQ_24_KHZ			4
#define RT5616_CP_FQ_48_KHZ			5
#define RT5616_CP_FQ_96_KHZ			6
#define RT5616_CP_FQ_192_KHZ			7

/* HPOUT charge pump (0x91) */
#define RT5616_OSW_L_MASK			(0x1 << 11)
#define RT5616_OSW_L_SFT			11
#define RT5616_OSW_L_DIS			(0x0 << 11)
#define RT5616_OSW_L_EN				(0x1 << 11)
#define RT5616_OSW_R_MASK			(0x1 << 10)
#define RT5616_OSW_R_SFT			10
#define RT5616_OSW_R_DIS			(0x0 << 10)
#define RT5616_OSW_R_EN				(0x1 << 10)
#define RT5616_PM_HP_MASK			(0x3 << 8)
#define RT5616_PM_HP_SFT			8
#define RT5616_PM_HP_LV				(0x0 << 8)
#define RT5616_PM_HP_MV				(0x1 << 8)
#define RT5616_PM_HP_HV				(0x2 << 8)
#define RT5616_IB_HP_MASK			(0x3 << 6)
#define RT5616_IB_HP_SFT			6
#define RT5616_IB_HP_125IL			(0x0 << 6)
#define RT5616_IB_HP_25IL			(0x1 << 6)
#define RT5616_IB_HP_5IL			(0x2 << 6)
#define RT5616_IB_HP_1IL			(0x3 << 6)

/* Micbias Control (0x93) */
#define RT5616_MIC1_BS_MASK			(0x1 << 15)
#define RT5616_MIC1_BS_SFT			15
#define RT5616_MIC1_BS_9AV			(0x0 << 15)
#define RT5616_MIC1_BS_75AV			(0x1 << 15)
#define RT5616_MIC1_CLK_MASK			(0x1 << 13)
#define RT5616_MIC1_CLK_SFT			13
#define RT5616_MIC1_CLK_DIS			(0x0 << 13)
#define RT5616_MIC1_CLK_EN			(0x1 << 13)
#define RT5616_MIC1_OVCD_MASK			(0x1 << 11)
#define RT5616_MIC1_OVCD_SFT			11
#define RT5616_MIC1_OVCD_DIS			(0x0 << 11)
#define RT5616_MIC1_OVCD_EN			(0x1 << 11)
#define RT5616_MIC1_OVTH_MASK			(0x3 << 9)
#define RT5616_MIC1_OVTH_SFT			9
#define RT5616_MIC1_OVTH_600UA			(0x0 << 9)
#define RT5616_MIC1_OVTH_1500UA			(0x1 << 9)
#define RT5616_MIC1_OVTH_2000UA			(0x2 << 9)
#define RT5616_PWR_MB_MASK			(0x1 << 5)
#define RT5616_PWR_MB_SFT			5
#define RT5616_PWR_MB_PD			(0x0 << 5)
#define RT5616_PWR_MB_PU			(0x1 << 5)
#define RT5616_PWR_CLK12M_MASK			(0x1 << 4)
#define RT5616_PWR_CLK12M_SFT			4
#define RT5616_PWR_CLK12M_PD			(0x0 << 4)
#define RT5616_PWR_CLK12M_PU			(0x1 << 4)

/* Analog JD Control 1 (0x94) */
#define RT5616_JD2_CMP_MASK			(0x7 << 12)
#define RT5616_JD2_CMP_SFT			12
#define RT5616_JD_PU				(0x1 << 11)
#define RT5616_JD_PU_SFT			11
#define RT5616_JD_PD				(0x1 << 10)
#define RT5616_JD_PD_SFT			10
#define RT5616_JD_MODE_SEL_MASK			(0x3 << 8)
#define RT5616_JD_MODE_SEL_SFT			8
#define RT5616_JD_MODE_SEL_M0			(0x0 << 8)
#define RT5616_JD_MODE_SEL_M1			(0x1 << 8)
#define RT5616_JD_MODE_SEL_M2			(0x2 << 8)
#define RT5616_JD_M_CMP				(0x7 << 4)
#define RT5616_JD_M_CMP_SFT			4
#define RT5616_JD_M_PU				(0x1 << 3)
#define RT5616_JD_M_PU_SFT			3
#define RT5616_JD_M_PD				(0x1 << 2)
#define RT5616_JD_M_PD_SFT			2
#define RT5616_JD_M_MODE_SEL_MASK		(0x3)
#define RT5616_JD_M_MODE_SEL_SFT		0
#define RT5616_JD_M_MODE_SEL_M0			(0x0)
#define RT5616_JD_M_MODE_SEL_M1			(0x1)
#define RT5616_JD_M_MODE_SEL_M2			(0x2)

/* Analog JD Control 2 (0x95) */
#define RT5616_JD3_CMP_MASK			(0x7 << 12)
#define RT5616_JD3_CMP_SFT			12

/* EQ Control 1 (0xb0) */
#define RT5616_EQ_SRC_MASK			(0x1 << 15)
#define RT5616_EQ_SRC_SFT			15
#define RT5616_EQ_SRC_DAC			(0x0 << 15)
#define RT5616_EQ_SRC_ADC			(0x1 << 15)
#define RT5616_EQ_UPD				(0x1 << 14)
#define RT5616_EQ_UPD_BIT			14
#define RT5616_EQ_CD_MASK			(0x1 << 13)
#define RT5616_EQ_CD_SFT			13
#define RT5616_EQ_CD_DIS			(0x0 << 13)
#define RT5616_EQ_CD_EN				(0x1 << 13)
#define RT5616_EQ_DITH_MASK			(0x3 << 8)
#define RT5616_EQ_DITH_SFT			8
#define RT5616_EQ_DITH_NOR			(0x0 << 8)
#define RT5616_EQ_DITH_LSB			(0x1 << 8)
#define RT5616_EQ_DITH_LSB_1			(0x2 << 8)
#define RT5616_EQ_DITH_LSB_2			(0x3 << 8)
#define RT5616_EQ_CD_F				(0x1 << 7)
#define RT5616_EQ_CD_F_BIT			7
#define RT5616_EQ_STA_HP2			(0x1 << 6)
#define RT5616_EQ_STA_HP2_BIT			6
#define RT5616_EQ_STA_HP1			(0x1 << 5)
#define RT5616_EQ_STA_HP1_BIT			5
#define RT5616_EQ_STA_BP4			(0x1 << 4)
#define RT5616_EQ_STA_BP4_BIT			4
#define RT5616_EQ_STA_BP3			(0x1 << 3)
#define RT5616_EQ_STA_BP3_BIT			3
#define RT5616_EQ_STA_BP2			(0x1 << 2)
#define RT5616_EQ_STA_BP2_BIT			2
#define RT5616_EQ_STA_BP1			(0x1 << 1)
#define RT5616_EQ_STA_BP1_BIT			1
#define RT5616_EQ_STA_LP			(0x1)
#define RT5616_EQ_STA_LP_BIT			0

/* EQ Control 2 (0xb1) */
#define RT5616_EQ_HPF1_M_MASK			(0x1 << 8)
#define RT5616_EQ_HPF1_M_SFT			8
#define RT5616_EQ_HPF1_M_HI			(0x0 << 8)
#define RT5616_EQ_HPF1_M_1ST			(0x1 << 8)
#define RT5616_EQ_LPF1_M_MASK			(0x1 << 7)
#define RT5616_EQ_LPF1_M_SFT			7
#define RT5616_EQ_LPF1_M_LO			(0x0 << 7)
#define RT5616_EQ_LPF1_M_1ST			(0x1 << 7)
#define RT5616_EQ_HPF2_MASK			(0x1 << 6)
#define RT5616_EQ_HPF2_SFT			6
#define RT5616_EQ_HPF2_DIS			(0x0 << 6)
#define RT5616_EQ_HPF2_EN			(0x1 << 6)
#define RT5616_EQ_HPF1_MASK			(0x1 << 5)
#define RT5616_EQ_HPF1_SFT			5
#define RT5616_EQ_HPF1_DIS			(0x0 << 5)
#define RT5616_EQ_HPF1_EN			(0x1 << 5)
#define RT5616_EQ_BPF4_MASK			(0x1 << 4)
#define RT5616_EQ_BPF4_SFT			4
#define RT5616_EQ_BPF4_DIS			(0x0 << 4)
#define RT5616_EQ_BPF4_EN			(0x1 << 4)
#define RT5616_EQ_BPF3_MASK			(0x1 << 3)
#define RT5616_EQ_BPF3_SFT			3
#define RT5616_EQ_BPF3_DIS			(0x0 << 3)
#define RT5616_EQ_BPF3_EN			(0x1 << 3)
#define RT5616_EQ_BPF2_MASK			(0x1 << 2)
#define RT5616_EQ_BPF2_SFT			2
#define RT5616_EQ_BPF2_DIS			(0x0 << 2)
#define RT5616_EQ_BPF2_EN			(0x1 << 2)
#define RT5616_EQ_BPF1_MASK			(0x1 << 1)
#define RT5616_EQ_BPF1_SFT			1
#define RT5616_EQ_BPF1_DIS			(0x0 << 1)
#define RT5616_EQ_BPF1_EN			(0x1 << 1)
#define RT5616_EQ_LPF_MASK			(0x1)
#define RT5616_EQ_LPF_SFT			0
#define RT5616_EQ_LPF_DIS			(0x0)
#define RT5616_EQ_LPF_EN			(0x1)
#define RT5616_EQ_CTRL_MASK			(0x7f)

/* Memory Test (0xb2) */
#define RT5616_MT_MASK				(0x1 << 15)
#define RT5616_MT_SFT				15
#define RT5616_MT_DIS				(0x0 << 15)
#define RT5616_MT_EN				(0x1 << 15)

/* DRC/AGC Control 1 (0xb4) */
#define RT5616_DRC_AGC_P_MASK			(0x1 << 15)
#define RT5616_DRC_AGC_P_SFT			15
#define RT5616_DRC_AGC_P_DAC			(0x0 << 15)
#define RT5616_DRC_AGC_P_ADC			(0x1 << 15)
#define RT5616_DRC_AGC_MASK			(0x1 << 14)
#define RT5616_DRC_AGC_SFT			14
#define RT5616_DRC_AGC_DIS			(0x0 << 14)
#define RT5616_DRC_AGC_EN			(0x1 << 14)
#define RT5616_DRC_AGC_UPD			(0x1 << 13)
#define RT5616_DRC_AGC_UPD_BIT			13
#define RT5616_DRC_AGC_AR_MASK			(0x1f << 8)
#define RT5616_DRC_AGC_AR_SFT			8
#define RT5616_DRC_AGC_R_MASK			(0x7 << 5)
#define RT5616_DRC_AGC_R_SFT			5
#define RT5616_DRC_AGC_R_48K			(0x1 << 5)
#define RT5616_DRC_AGC_R_96K			(0x2 << 5)
#define RT5616_DRC_AGC_R_192K			(0x3 << 5)
#define RT5616_DRC_AGC_R_441K			(0x5 << 5)
#define RT5616_DRC_AGC_R_882K			(0x6 << 5)
#define RT5616_DRC_AGC_R_1764K			(0x7 << 5)
#define RT5616_DRC_AGC_RC_MASK			(0x1f)
#define RT5616_DRC_AGC_RC_SFT			0

/* DRC/AGC Control 2 (0xb5) */
#define RT5616_DRC_AGC_POB_MASK			(0x3f << 8)
#define RT5616_DRC_AGC_POB_SFT			8
#define RT5616_DRC_AGC_CP_MASK			(0x1 << 7)
#define RT5616_DRC_AGC_CP_SFT			7
#define RT5616_DRC_AGC_CP_DIS			(0x0 << 7)
#define RT5616_DRC_AGC_CP_EN			(0x1 << 7)
#define RT5616_DRC_AGC_CPR_MASK			(0x3 << 5)
#define RT5616_DRC_AGC_CPR_SFT			5
#define RT5616_DRC_AGC_CPR_1_1			(0x0 << 5)
#define RT5616_DRC_AGC_CPR_1_2			(0x1 << 5)
#define RT5616_DRC_AGC_CPR_1_3			(0x2 << 5)
#define RT5616_DRC_AGC_CPR_1_4			(0x3 << 5)
#define RT5616_DRC_AGC_PRB_MASK			(0x1f)
#define RT5616_DRC_AGC_PRB_SFT			0

/* DRC/AGC Control 3 (0xb6) */
#define RT5616_DRC_AGC_NGB_MASK			(0xf << 12)
#define RT5616_DRC_AGC_NGB_SFT			12
#define RT5616_DRC_AGC_TAR_MASK			(0x1f << 7)
#define RT5616_DRC_AGC_TAR_SFT			7
#define RT5616_DRC_AGC_NG_MASK			(0x1 << 6)
#define RT5616_DRC_AGC_NG_SFT			6
#define RT5616_DRC_AGC_NG_DIS			(0x0 << 6)
#define RT5616_DRC_AGC_NG_EN			(0x1 << 6)
#define RT5616_DRC_AGC_NGH_MASK			(0x1 << 5)
#define RT5616_DRC_AGC_NGH_SFT			5
#define RT5616_DRC_AGC_NGH_DIS			(0x0 << 5)
#define RT5616_DRC_AGC_NGH_EN			(0x1 << 5)
#define RT5616_DRC_AGC_NGT_MASK			(0x1f)
#define RT5616_DRC_AGC_NGT_SFT			0

/* Jack Detect Control 1 (0xbb) */
#define RT5616_JD_MASK				(0x7 << 13)
#define RT5616_JD_SFT				13
#define RT5616_JD_DIS				(0x0 << 13)
#define RT5616_JD_GPIO1				(0x1 << 13)
#define RT5616_JD_GPIO2				(0x2 << 13)
#define RT5616_JD_GPIO3				(0x3 << 13)
#define RT5616_JD_GPIO4				(0x4 << 13)
#define RT5616_JD_GPIO5				(0x5 << 13)
#define RT5616_JD_GPIO6				(0x6 << 13)
#define RT5616_JD_HP_MASK			(0x1 << 11)
#define RT5616_JD_HP_SFT			11
#define RT5616_JD_HP_DIS			(0x0 << 11)
#define RT5616_JD_HP_EN				(0x1 << 11)
#define RT5616_JD_HP_TRG_MASK			(0x1 << 10)
#define RT5616_JD_HP_TRG_SFT			10
#define RT5616_JD_HP_TRG_LO			(0x0 << 10)
#define RT5616_JD_HP_TRG_HI			(0x1 << 10)
#define RT5616_JD_SPL_MASK			(0x1 << 9)
#define RT5616_JD_SPL_SFT			9
#define RT5616_JD_SPL_DIS			(0x0 << 9)
#define RT5616_JD_SPL_EN			(0x1 << 9)
#define RT5616_JD_SPL_TRG_MASK			(0x1 << 8)
#define RT5616_JD_SPL_TRG_SFT			8
#define RT5616_JD_SPL_TRG_LO			(0x0 << 8)
#define RT5616_JD_SPL_TRG_HI			(0x1 << 8)
#define RT5616_JD_SPR_MASK			(0x1 << 7)
#define RT5616_JD_SPR_SFT			7
#define RT5616_JD_SPR_DIS			(0x0 << 7)
#define RT5616_JD_SPR_EN			(0x1 << 7)
#define RT5616_JD_SPR_TRG_MASK			(0x1 << 6)
#define RT5616_JD_SPR_TRG_SFT			6
#define RT5616_JD_SPR_TRG_LO			(0x0 << 6)
#define RT5616_JD_SPR_TRG_HI			(0x1 << 6)
#define RT5616_JD_LO_MASK			(0x1 << 3)
#define RT5616_JD_LO_SFT			3
#define RT5616_JD_LO_DIS			(0x0 << 3)
#define RT5616_JD_LO_EN				(0x1 << 3)
#define RT5616_JD_LO_TRG_MASK			(0x1 << 2)
#define RT5616_JD_LO_TRG_SFT			2
#define RT5616_JD_LO_TRG_LO			(0x0 << 2)
#define RT5616_JD_LO_TRG_HI			(0x1 << 2)

/* Jack Detect Control 2 (0xbc) */
#define RT5616_JD_TRG_SEL_MASK			(0x7 << 9)
#define RT5616_JD_TRG_SEL_SFT			9
#define RT5616_JD_TRG_SEL_GPIO			(0x0 << 9)
#define RT5616_JD_TRG_SEL_JD1_1			(0x1 << 9)
#define RT5616_JD_TRG_SEL_JD1_2			(0x2 << 9)
#define RT5616_JD_TRG_SEL_JD2			(0x3 << 9)
#define RT5616_JD_TRG_SEL_JD3			(0x4 << 9)
#define RT5616_JD3_IRQ_EN			(0x1 << 8)
#define RT5616_JD3_IRQ_EN_SFT			8
#define RT5616_JD3_EN_STKY			(0x1 << 7)
#define RT5616_JD3_EN_STKY_SFT			7
#define RT5616_JD3_INV				(0x1 << 6)
#define RT5616_JD3_INV_SFT			6

/* IRQ Control 1 (0xbd) */
#define RT5616_IRQ_JD_MASK			(0x1 << 15)
#define RT5616_IRQ_JD_SFT			15
#define RT5616_IRQ_JD_BP			(0x0 << 15)
#define RT5616_IRQ_JD_NOR			(0x1 << 15)
#define RT5616_JD_STKY_MASK			(0x1 << 13)
#define RT5616_JD_STKY_SFT			13
#define RT5616_JD_STKY_DIS			(0x0 << 13)
#define RT5616_JD_STKY_EN			(0x1 << 13)
#define RT5616_JD_P_MASK			(0x1 << 11)
#define RT5616_JD_P_SFT				11
#define RT5616_JD_P_NOR				(0x0 << 11)
#define RT5616_JD_P_INV				(0x1 << 11)
#define RT5616_JD1_1_IRQ_EN			(0x1 << 9)
#define RT5616_JD1_1_IRQ_EN_SFT			9
#define RT5616_JD1_1_EN_STKY			(0x1 << 8)
#define RT5616_JD1_1_EN_STKY_SFT			8
#define RT5616_JD1_1_INV			(0x1 << 7)
#define RT5616_JD1_1_INV_SFT			7
#define RT5616_JD1_2_IRQ_EN			(0x1 << 6)
#define RT5616_JD1_2_IRQ_EN_SFT			6
#define RT5616_JD1_2_EN_STKY			(0x1 << 5)
#define RT5616_JD1_2_EN_STKY_SFT			5
#define RT5616_JD1_2_INV			(0x1 << 4)
#define RT5616_JD1_2_INV_SFT			4
#define RT5616_JD2_IRQ_EN			(0x1 << 3)
#define RT5616_JD2_IRQ_EN_SFT			3
#define RT5616_JD2_EN_STKY			(0x1 << 2)
#define RT5616_JD2_EN_STKY_SFT			2
#define RT5616_JD2_INV				(0x1 << 1)
#define RT5616_JD2_INV_SFT			1

/* IRQ Control 2 (0xbe) */
#define RT5616_IRQ_MB1_OC_MASK			(0x1 << 15)
#define RT5616_IRQ_MB1_OC_SFT			15
#define RT5616_IRQ_MB1_OC_BP			(0x0 << 15)
#define RT5616_IRQ_MB1_OC_NOR			(0x1 << 15)
#define RT5616_MB1_OC_STKY_MASK			(0x1 << 11)
#define RT5616_MB1_OC_STKY_SFT			11
#define RT5616_MB1_OC_STKY_DIS			(0x0 << 11)
#define RT5616_MB1_OC_STKY_EN			(0x1 << 11)
#define RT5616_MB1_OC_P_MASK			(0x1 << 7)
#define RT5616_MB1_OC_P_SFT			7
#define RT5616_MB1_OC_P_NOR			(0x0 << 7)
#define RT5616_MB1_OC_P_INV			(0x1 << 7)
#define RT5616_MB2_OC_P_MASK			(0x1 << 6)
#define RT5616_MB1_OC_CLR			(0x1 << 3)
#define RT5616_MB1_OC_CLR_SFT			3
#define RT5616_STA_GPIO8			(0x1)
#define RT5616_STA_GPIO8_BIT			0

/* Internal Status and GPIO status (0xbf) */
#define RT5616_STA_JD3				(0x1 << 15)
#define RT5616_STA_JD3_BIT			15
#define RT5616_STA_JD2				(0x1 << 14)
#define RT5616_STA_JD2_BIT			14
#define RT5616_STA_JD1_2			(0x1 << 13)
#define RT5616_STA_JD1_2_BIT			13
#define RT5616_STA_JD1_1			(0x1 << 12)
#define RT5616_STA_JD1_1_BIT			12
#define RT5616_STA_GP7				(0x1 << 11)
#define RT5616_STA_GP7_BIT			11
#define RT5616_STA_GP6				(0x1 << 10)
#define RT5616_STA_GP6_BIT			10
#define RT5616_STA_GP5				(0x1 << 9)
#define RT5616_STA_GP5_BIT			9
#define RT5616_STA_GP1				(0x1 << 8)
#define RT5616_STA_GP1_BIT			8
#define RT5616_STA_GP2				(0x1 << 7)
#define RT5616_STA_GP2_BIT			7
#define RT5616_STA_GP3				(0x1 << 6)
#define RT5616_STA_GP3_BIT			6
#define RT5616_STA_GP4				(0x1 << 5)
#define RT5616_STA_GP4_BIT			5
#define RT5616_STA_GP_JD			(0x1 << 4)
#define RT5616_STA_GP_JD_BIT			4

/* GPIO Control 1 (0xc0) */
#define RT5616_GP1_PIN_MASK			(0x1 << 15)
#define RT5616_GP1_PIN_SFT			15
#define RT5616_GP1_PIN_GPIO1			(0x0 << 15)
#define RT5616_GP1_PIN_IRQ			(0x1 << 15)
#define RT5616_GP2_PIN_MASK			(0x1 << 14)
#define RT5616_GP2_PIN_SFT			14
#define RT5616_GP2_PIN_GPIO2			(0x0 << 14)
#define RT5616_GP2_PIN_DMIC1_SCL		(0x1 << 14)
#define RT5616_GPIO_M_MASK			(0x1 << 9)
#define RT5616_GPIO_M_SFT			9
#define RT5616_GPIO_M_FLT			(0x0 << 9)
#define RT5616_GPIO_M_PH			(0x1 << 9)
#define RT5616_I2S2_SEL_MASK			(0x1 << 8)
#define RT5616_I2S2_SEL_SFT			8
#define RT5616_I2S2_SEL_I2S			(0x0 << 8)
#define RT5616_I2S2_SEL_GPIO			(0x1 << 8)
#define RT5616_GP5_PIN_MASK			(0x1 << 7)
#define RT5616_GP5_PIN_SFT			7
#define RT5616_GP5_PIN_GPIO5			(0x0 << 7)
#define RT5616_GP5_PIN_IRQ			(0x1 << 7)
#define RT5616_GP6_PIN_MASK			(0x1 << 6)
#define RT5616_GP6_PIN_SFT			6
#define RT5616_GP6_PIN_GPIO6			(0x0 << 6)
#define RT5616_GP6_PIN_DMIC_SDA			(0x1 << 6)
#define RT5616_GP7_PIN_MASK			(0x1 << 5)
#define RT5616_GP7_PIN_SFT			5
#define RT5616_GP7_PIN_GPIO7			(0x0 << 5)
#define RT5616_GP7_PIN_IRQ			(0x1 << 5)
#define RT5616_GP8_PIN_MASK			(0x1 << 4)
#define RT5616_GP8_PIN_SFT			4
#define RT5616_GP8_PIN_GPIO8			(0x0 << 4)
#define RT5616_GP8_PIN_DMIC_SDA			(0x1 << 4)
#define RT5616_GPIO_PDM_SEL_MASK		(0x1 << 3)
#define RT5616_GPIO_PDM_SEL_SFT			3
#define RT5616_GPIO_PDM_SEL_GPIO		(0x0 << 3)
#define RT5616_GPIO_PDM_SEL_PDM			(0x1 << 3)

/* GPIO Control 2 (0xc1) */
#define RT5616_GP5_DR_MASK			(0x1 << 14)
#define RT5616_GP5_DR_SFT			14
#define RT5616_GP5_DR_IN			(0x0 << 14)
#define RT5616_GP5_DR_OUT			(0x1 << 14)
#define RT5616_GP5_OUT_MASK			(0x1 << 13)
#define RT5616_GP5_OUT_SFT			13
#define RT5616_GP5_OUT_LO			(0x0 << 13)
#define RT5616_GP5_OUT_HI			(0x1 << 13)
#define RT5616_GP5_P_MASK			(0x1 << 12)
#define RT5616_GP5_P_SFT			12
#define RT5616_GP5_P_NOR			(0x0 << 12)
#define RT5616_GP5_P_INV			(0x1 << 12)
#define RT5616_GP4_DR_MASK			(0x1 << 11)
#define RT5616_GP4_DR_SFT			11
#define RT5616_GP4_DR_IN			(0x0 << 11)
#define RT5616_GP4_DR_OUT			(0x1 << 11)
#define RT5616_GP4_OUT_MASK			(0x1 << 10)
#define RT5616_GP4_OUT_SFT			10
#define RT5616_GP4_OUT_LO			(0x0 << 10)
#define RT5616_GP4_OUT_HI			(0x1 << 10)
#define RT5616_GP4_P_MASK			(0x1 << 9)
#define RT5616_GP4_P_SFT			9
#define RT5616_GP4_P_NOR			(0x0 << 9)
#define RT5616_GP4_P_INV			(0x1 << 9)
#define RT5616_GP3_DR_MASK			(0x1 << 8)
#define RT5616_GP3_DR_SFT			8
#define RT5616_GP3_DR_IN			(0x0 << 8)
#define RT5616_GP3_DR_OUT			(0x1 << 8)
#define RT5616_GP3_OUT_MASK			(0x1 << 7)
#define RT5616_GP3_OUT_SFT			7
#define RT5616_GP3_OUT_LO			(0x0 << 7)
#define RT5616_GP3_OUT_HI			(0x1 << 7)
#define RT5616_GP3_P_MASK			(0x1 << 6)
#define RT5616_GP3_P_SFT			6
#define RT5616_GP3_P_NOR			(0x0 << 6)
#define RT5616_GP3_P_INV			(0x1 << 6)
#define RT5616_GP2_DR_MASK			(0x1 << 5)
#define RT5616_GP2_DR_SFT			5
#define RT5616_GP2_DR_IN			(0x0 << 5)
#define RT5616_GP2_DR_OUT			(0x1 << 5)
#define RT5616_GP2_OUT_MASK			(0x1 << 4)
#define RT5616_GP2_OUT_SFT			4
#define RT5616_GP2_OUT_LO			(0x0 << 4)
#define RT5616_GP2_OUT_HI			(0x1 << 4)
#define RT5616_GP2_P_MASK			(0x1 << 3)
#define RT5616_GP2_P_SFT			3
#define RT5616_GP2_P_NOR			(0x0 << 3)
#define RT5616_GP2_P_INV			(0x1 << 3)
#define RT5616_GP1_DR_MASK			(0x1 << 2)
#define RT5616_GP1_DR_SFT			2
#define RT5616_GP1_DR_IN			(0x0 << 2)
#define RT5616_GP1_DR_OUT			(0x1 << 2)
#define RT5616_GP1_OUT_MASK			(0x1 << 1)
#define RT5616_GP1_OUT_SFT			1
#define RT5616_GP1_OUT_LO			(0x0 << 1)
#define RT5616_GP1_OUT_HI			(0x1 << 1)
#define RT5616_GP1_P_MASK			(0x1)
#define RT5616_GP1_P_SFT			0
#define RT5616_GP1_P_NOR			(0x0)
#define RT5616_GP1_P_INV			(0x1)

/* GPIO Control 3 (0xc2) */
#define RT5616_GP8_DR_MASK			(0x1 << 8)
#define RT5616_GP8_DR_SFT			8
#define RT5616_GP8_DR_IN			(0x0 << 8)
#define RT5616_GP8_DR_OUT			(0x1 << 8)
#define RT5616_GP8_OUT_MASK			(0x1 << 7)
#define RT5616_GP8_OUT_SFT			7
#define RT5616_GP8_OUT_LO			(0x0 << 7)
#define RT5616_GP8_OUT_HI			(0x1 << 7)
#define RT5616_GP8_P_MASK			(0x1 << 6)
#define RT5616_GP8_P_SFT			6
#define RT5616_GP8_P_NOR			(0x0 << 6)
#define RT5616_GP8_P_INV			(0x1 << 6)
#define RT5616_GP7_DR_MASK			(0x1 << 5)
#define RT5616_GP7_DR_SFT			5
#define RT5616_GP7_DR_IN			(0x0 << 5)
#define RT5616_GP7_DR_OUT			(0x1 << 5)
#define RT5616_GP7_OUT_MASK			(0x1 << 4)
#define RT5616_GP7_OUT_SFT			4
#define RT5616_GP7_OUT_LO			(0x0 << 4)
#define RT5616_GP7_OUT_HI			(0x1 << 4)
#define RT5616_GP7_P_MASK			(0x1 << 3)
#define RT5616_GP7_P_SFT			3
#define RT5616_GP7_P_NOR			(0x0 << 3)
#define RT5616_GP7_P_INV			(0x1 << 3)
#define RT5616_GP6_DR_MASK			(0x1 << 2)
#define RT5616_GP6_DR_SFT			2
#define RT5616_GP6_DR_IN			(0x0 << 2)
#define RT5616_GP6_DR_OUT			(0x1 << 2)
#define RT5616_GP6_OUT_MASK			(0x1 << 1)
#define RT5616_GP6_OUT_SFT			1
#define RT5616_GP6_OUT_LO			(0x0 << 1)
#define RT5616_GP6_OUT_HI			(0x1 << 1)
#define RT5616_GP6_P_MASK			(0x1)
#define RT5616_GP6_P_SFT			0
#define RT5616_GP6_P_NOR			(0x0)
#define RT5616_GP6_P_INV			(0x1)

/* Scramble Control (0xce) */
#define RT5616_SCB_SWAP_MASK			(0x1 << 15)
#define RT5616_SCB_SWAP_SFT			15
#define RT5616_SCB_SWAP_DIS			(0x0 << 15)
#define RT5616_SCB_SWAP_EN			(0x1 << 15)
#define RT5616_SCB_MASK				(0x1 << 14)
#define RT5616_SCB_SFT				14
#define RT5616_SCB_DIS				(0x0 << 14)
#define RT5616_SCB_EN				(0x1 << 14)

/* Baseback Control (0xcf) */
#define RT5616_BB_MASK				(0x1 << 15)
#define RT5616_BB_SFT				15
#define RT5616_BB_DIS				(0x0 << 15)
#define RT5616_BB_EN				(0x1 << 15)
#define RT5616_BB_CT_MASK			(0x7 << 12)
#define RT5616_BB_CT_SFT			12
#define RT5616_BB_CT_A				(0x0 << 12)
#define RT5616_BB_CT_B				(0x1 << 12)
#define RT5616_BB_CT_C				(0x2 << 12)
#define RT5616_BB_CT_D				(0x3 << 12)
#define RT5616_M_BB_L_MASK			(0x1 << 9)
#define RT5616_M_BB_L_SFT			9
#define RT5616_M_BB_R_MASK			(0x1 << 8)
#define RT5616_M_BB_R_SFT			8
#define RT5616_M_BB_HPF_L_MASK			(0x1 << 7)
#define RT5616_M_BB_HPF_L_SFT			7
#define RT5616_M_BB_HPF_R_MASK			(0x1 << 6)
#define RT5616_M_BB_HPF_R_SFT			6
#define RT5616_G_BB_BST_MASK			(0x3f)
#define RT5616_G_BB_BST_SFT			0

/* MP3 Plus Control 1 (0xd0) */
#define RT5616_M_MP3_L_MASK			(0x1 << 15)
#define RT5616_M_MP3_L_SFT			15
#define RT5616_M_MP3_R_MASK			(0x1 << 14)
#define RT5616_M_MP3_R_SFT			14
#define RT5616_M_MP3_MASK			(0x1 << 13)
#define RT5616_M_MP3_SFT			13
#define RT5616_M_MP3_DIS			(0x0 << 13)
#define RT5616_M_MP3_EN				(0x1 << 13)
#define RT5616_EG_MP3_MASK			(0x1f << 8)
#define RT5616_EG_MP3_SFT			8
#define RT5616_MP3_HLP_MASK			(0x1 << 7)
#define RT5616_MP3_HLP_SFT			7
#define RT5616_MP3_HLP_DIS			(0x0 << 7)
#define RT5616_MP3_HLP_EN			(0x1 << 7)
#define RT5616_M_MP3_ORG_L_MASK			(0x1 << 6)
#define RT5616_M_MP3_ORG_L_SFT			6
#define RT5616_M_MP3_ORG_R_MASK			(0x1 << 5)
#define RT5616_M_MP3_ORG_R_SFT			5

/* MP3 Plus Control 2 (0xd1) */
#define RT5616_MP3_WT_MASK			(0x1 << 13)
#define RT5616_MP3_WT_SFT			13
#define RT5616_MP3_WT_1_4			(0x0 << 13)
#define RT5616_MP3_WT_1_2			(0x1 << 13)
#define RT5616_OG_MP3_MASK			(0x1f << 8)
#define RT5616_OG_MP3_SFT			8
#define RT5616_HG_MP3_MASK			(0x3f)
#define RT5616_HG_MP3_SFT			0

/* 3D HP Control 1 (0xd2) */
#define RT5616_3D_CF_MASK			(0x1 << 15)
#define RT5616_3D_CF_SFT			15
#define RT5616_3D_CF_DIS			(0x0 << 15)
#define RT5616_3D_CF_EN				(0x1 << 15)
#define RT5616_3D_HP_MASK			(0x1 << 14)
#define RT5616_3D_HP_SFT			14
#define RT5616_3D_HP_DIS			(0x0 << 14)
#define RT5616_3D_HP_EN				(0x1 << 14)
#define RT5616_3D_BT_MASK			(0x1 << 13)
#define RT5616_3D_BT_SFT			13
#define RT5616_3D_BT_DIS			(0x0 << 13)
#define RT5616_3D_BT_EN				(0x1 << 13)
#define RT5616_3D_1F_MIX_MASK			(0x3 << 11)
#define RT5616_3D_1F_MIX_SFT			11
#define RT5616_3D_HP_M_MASK			(0x1 << 10)
#define RT5616_3D_HP_M_SFT			10
#define RT5616_3D_HP_M_SUR			(0x0 << 10)
#define RT5616_3D_HP_M_FRO			(0x1 << 10)
#define RT5616_M_3D_HRTF_MASK			(0x1 << 9)
#define RT5616_M_3D_HRTF_SFT			9
#define RT5616_M_3D_D2H_MASK			(0x1 << 8)
#define RT5616_M_3D_D2H_SFT			8
#define RT5616_M_3D_D2R_MASK			(0x1 << 7)
#define RT5616_M_3D_D2R_SFT			7
#define RT5616_M_3D_REVB_MASK			(0x1 << 6)
#define RT5616_M_3D_REVB_SFT			6

/* Adjustable high pass filter control 1 (0xd3) */
#define RT5616_2ND_HPF_MASK			(0x1 << 15)
#define RT5616_2ND_HPF_SFT			15
#define RT5616_2ND_HPF_DIS			(0x0 << 15)
#define RT5616_2ND_HPF_EN			(0x1 << 15)
#define RT5616_HPF_CF_L_MASK			(0x7 << 12)
#define RT5616_HPF_CF_L_SFT			12
#define RT5616_HPF_CF_R_MASK			(0x7 << 8)
#define RT5616_HPF_CF_R_SFT			8
#define RT5616_ZD_T_MASK			(0x3 << 6)
#define RT5616_ZD_T_SFT				6
#define RT5616_ZD_F_MASK			(0x3 << 4)
#define RT5616_ZD_F_SFT				4
#define RT5616_ZD_F_IM				(0x0 << 4)
#define RT5616_ZD_F_ZC_IM			(0x1 << 4)
#define RT5616_ZD_F_ZC_IOD			(0x2 << 4)
#define RT5616_ZD_F_UN				(0x3 << 4)

/* Adjustable high pass filter control 2 (0xd4) */
#define RT5616_HPF_CF_L_NUM_MASK		(0x3f << 8)
#define RT5616_HPF_CF_L_NUM_SFT			8
#define RT5616_HPF_CF_R_NUM_MASK		(0x3f)
#define RT5616_HPF_CF_R_NUM_SFT			0

/* HP calibration control and Amp detection (0xd6) */
#define RT5616_SI_DAC_MASK			(0x1 << 11)
#define RT5616_SI_DAC_SFT			11
#define RT5616_SI_DAC_AUTO			(0x0 << 11)
#define RT5616_SI_DAC_TEST			(0x1 << 11)
#define RT5616_DC_CAL_M_MASK			(0x1 << 10)
#define RT5616_DC_CAL_M_SFT			10
#define RT5616_DC_CAL_M_NOR			(0x0 << 10)
#define RT5616_DC_CAL_M_CAL			(0x1 << 10)
#define RT5616_DC_CAL_MASK			(0x1 << 9)
#define RT5616_DC_CAL_SFT			9
#define RT5616_DC_CAL_DIS			(0x0 << 9)
#define RT5616_DC_CAL_EN			(0x1 << 9)
#define RT5616_HPD_RCV_MASK			(0x7 << 6)
#define RT5616_HPD_RCV_SFT			6
#define RT5616_HPD_PS_MASK			(0x1 << 5)
#define RT5616_HPD_PS_SFT			5
#define RT5616_HPD_PS_DIS			(0x0 << 5)
#define RT5616_HPD_PS_EN			(0x1 << 5)
#define RT5616_CAL_M_MASK			(0x1 << 4)
#define RT5616_CAL_M_SFT			4
#define RT5616_CAL_M_DEP			(0x0 << 4)
#define RT5616_CAL_M_CAL			(0x1 << 4)
#define RT5616_CAL_MASK				(0x1 << 3)
#define RT5616_CAL_SFT				3
#define RT5616_CAL_DIS				(0x0 << 3)
#define RT5616_CAL_EN				(0x1 << 3)
#define RT5616_CAL_TEST_MASK			(0x1 << 2)
#define RT5616_CAL_TEST_SFT			2
#define RT5616_CAL_TEST_DIS			(0x0 << 2)
#define RT5616_CAL_TEST_EN			(0x1 << 2)
#define RT5616_CAL_P_MASK			(0x3)
#define RT5616_CAL_P_SFT			0
#define RT5616_CAL_P_NONE			(0x0)
#define RT5616_CAL_P_CAL			(0x1)
#define RT5616_CAL_P_DAC_CAL			(0x2)

/* Soft volume and zero cross control 1 (0xd9) */
#define RT5616_SV_MASK				(0x1 << 15)
#define RT5616_SV_SFT				15
#define RT5616_SV_DIS				(0x0 << 15)
#define RT5616_SV_EN				(0x1 << 15)
#define RT5616_OUT_SV_MASK			(0x1 << 13)
#define RT5616_OUT_SV_SFT			13
#define RT5616_OUT_SV_DIS			(0x0 << 13)
#define RT5616_OUT_SV_EN			(0x1 << 13)
#define RT5616_HP_SV_MASK			(0x1 << 12)
#define RT5616_HP_SV_SFT			12
#define RT5616_HP_SV_DIS			(0x0 << 12)
#define RT5616_HP_SV_EN				(0x1 << 12)
#define RT5616_ZCD_DIG_MASK			(0x1 << 11)
#define RT5616_ZCD_DIG_SFT			11
#define RT5616_ZCD_DIG_DIS			(0x0 << 11)
#define RT5616_ZCD_DIG_EN			(0x1 << 11)
#define RT5616_ZCD_MASK				(0x1 << 10)
#define RT5616_ZCD_SFT				10
#define RT5616_ZCD_PD				(0x0 << 10)
#define RT5616_ZCD_PU				(0x1 << 10)
#define RT5616_M_ZCD_MASK			(0x3f << 4)
#define RT5616_M_ZCD_SFT			4
#define RT5616_M_ZCD_OM_L			(0x1 << 7)
#define RT5616_M_ZCD_OM_R			(0x1 << 6)
#define RT5616_M_ZCD_RM_L			(0x1 << 5)
#define RT5616_M_ZCD_RM_R			(0x1 << 4)
#define RT5616_SV_DLY_MASK			(0xf)
#define RT5616_SV_DLY_SFT			0

/* Soft volume and zero cross control 2 (0xda) */
#define RT5616_ZCD_HP_MASK			(0x1 << 15)
#define RT5616_ZCD_HP_SFT			15
#define RT5616_ZCD_HP_DIS			(0x0 << 15)
#define RT5616_ZCD_HP_EN			(0x1 << 15)

/* Digital Misc Control (0xfa) */
#define RT5616_I2S2_MS_SP_MASK			(0x1 << 8)
#define RT5616_I2S2_MS_SP_SEL			8
#define RT5616_I2S2_MS_SP_64			(0x0 << 8)
#define RT5616_I2S2_MS_SP_50			(0x1 << 8)
#define RT5616_CLK_DET_EN			(0x1 << 3)
#define RT5616_CLK_DET_EN_SFT			3
#define RT5616_AMP_DET_EN			(0x1 << 1)
#define RT5616_AMP_DET_EN_SFT			1
#define RT5616_D_GATE_EN			(0x1)
#define RT5616_D_GATE_EN_SFT			0

/* Codec Private Register definition */
/* 3D Speaker Control (0x63) */
#define RT5616_3D_SPK_MASK			(0x1 << 15)
#define RT5616_3D_SPK_SFT			15
#define RT5616_3D_SPK_DIS			(0x0 << 15)
#define RT5616_3D_SPK_EN			(0x1 << 15)
#define RT5616_3D_SPK_M_MASK			(0x3 << 13)
#define RT5616_3D_SPK_M_SFT			13
#define RT5616_3D_SPK_CG_MASK			(0x1f << 8)
#define RT5616_3D_SPK_CG_SFT			8
#define RT5616_3D_SPK_SG_MASK			(0x1f)
#define RT5616_3D_SPK_SG_SFT			0

/* Wind Noise Detection Control 1 (0x6c) */
#define RT5616_WND_MASK				(0x1 << 15)
#define RT5616_WND_SFT				15
#define RT5616_WND_DIS				(0x0 << 15)
#define RT5616_WND_EN				(0x1 << 15)

/* Wind Noise Detection Control 2 (0x6d) */
#define RT5616_WND_FC_NW_MASK			(0x3f << 10)
#define RT5616_WND_FC_NW_SFT			10
#define RT5616_WND_FC_WK_MASK			(0x3f << 4)
#define RT5616_WND_FC_WK_SFT			4

/* Wind Noise Detection Control 3 (0x6e) */
#define RT5616_HPF_FC_MASK			(0x3f << 6)
#define RT5616_HPF_FC_SFT			6
#define RT5616_WND_FC_ST_MASK			(0x3f)
#define RT5616_WND_FC_ST_SFT			0

/* Wind Noise Detection Control 4 (0x6f) */
#define RT5616_WND_TH_LO_MASK			(0x3ff)
#define RT5616_WND_TH_LO_SFT			0

/* Wind Noise Detection Control 5 (0x70) */
#define RT5616_WND_TH_HI_MASK			(0x3ff)
#define RT5616_WND_TH_HI_SFT			0

/* Wind Noise Detection Control 8 (0x73) */
#define RT5616_WND_WIND_MASK			(0x1 << 13) /* Read-Only */
#define RT5616_WND_WIND_SFT			13
#define RT5616_WND_STRONG_MASK			(0x1 << 12) /* Read-Only */
#define RT5616_WND_STRONG_SFT			12
enum {
	RT5616_NO_WIND,
	RT5616_BREEZE,
	RT5616_STORM,
};

/* Dipole Speaker Interface (0x75) */
#define RT5616_DP_ATT_MASK			(0x3 << 14)
#define RT5616_DP_ATT_SFT			14
#define RT5616_DP_SPK_MASK			(0x1 << 10)
#define RT5616_DP_SPK_SFT			10
#define RT5616_DP_SPK_DIS			(0x0 << 10)
#define RT5616_DP_SPK_EN			(0x1 << 10)

/* EQ Pre Volume Control (0xb3) */
#define RT5616_EQ_PRE_VOL_MASK			(0xffff)
#define RT5616_EQ_PRE_VOL_SFT			0

/* EQ Post Volume Control (0xb4) */
#define RT5616_EQ_PST_VOL_MASK			(0xffff)
#define RT5616_EQ_PST_VOL_SFT			0

/* System Clock Source */
enum {
	RT5616_SCLK_S_MCLK,
	RT5616_SCLK_S_PLL1,
};

/* PLL1 Source */
enum {
	RT5616_PLL1_S_MCLK,
	RT5616_PLL1_S_BCLK1,
	RT5616_PLL1_S_BCLK2,
};

enum {
	RT5616_AIF1,
	RT5616_AIFS,
};

#endif /* __RT5616_H__ */
