/*
 * rt3261.h  --  RT3261 ALSA SoC audio driver
 *
 * Copyright 2011 Realtek Microelectronics
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT3261_H__
#define __RT3261_H__

/* Info */
#define RT3261_RESET				0x00
#define RT3261_VENDOR_ID			0xfd
#define RT3261_VENDOR_ID1			0xfe
#define RT3261_VENDOR_ID2			0xff
/*  I/O - Output */
#define RT3261_SPK_VOL				0x01
#define RT3261_HP_VOL				0x02
#define RT3261_OUTPUT				0x03
#define RT3261_MONO_OUT			0x04
/* Dummy */
#define RT3261_DUMMY_PR3F				0x05
/* I/O - Input */
#define RT3261_IN1_IN2				0x0d
#define RT3261_IN3_IN4				0x0e
#define RT3261_INL_INR_VOL			0x0f
/* I/O - ADC/DAC/DMIC */
#define RT3261_DAC1_DIG_VOL			0x19
#define RT3261_DAC2_DIG_VOL			0x1a
#define RT3261_DAC2_CTRL			0x1b
#define RT3261_ADC_DIG_VOL			0x1c
#define RT3261_ADC_DATA			0x1d
#define RT3261_ADC_BST_VOL			0x1e
/* Mixer - D-D */
#define RT3261_STO_ADC_MIXER			0x27
#define RT3261_MONO_ADC_MIXER			0x28
#define RT3261_AD_DA_MIXER			0x29
#define RT3261_STO_DAC_MIXER			0x2a
#define RT3261_MONO_DAC_MIXER			0x2b
#define RT3261_DIG_MIXER			0x2c
#define RT3261_DSP_PATH1			0x2d
#define RT3261_DSP_PATH2			0x2e
#define RT3261_DIG_INF_DATA			0x2f
/* Mixer - ADC */
#define RT3261_REC_L1_MIXER			0x3b
#define RT3261_REC_L2_MIXER			0x3c
#define RT3261_REC_R1_MIXER			0x3d
#define RT3261_REC_R2_MIXER			0x3e
/* Mixer - DAC */
#define RT3261_HPO_MIXER			0x45
#define RT3261_SPK_L_MIXER			0x46
#define RT3261_SPK_R_MIXER			0x47
#define RT3261_SPO_L_MIXER			0x48
#define RT3261_SPO_R_MIXER			0x49
#define RT3261_SPO_CLSD_RATIO			0x4a
#define RT3261_MONO_MIXER			0x4c
#define RT3261_OUT_L1_MIXER			0x4d
#define RT3261_OUT_L2_MIXER			0x4e
#define RT3261_OUT_L3_MIXER			0x4f
#define RT3261_OUT_R1_MIXER			0x50
#define RT3261_OUT_R2_MIXER			0x51
#define RT3261_OUT_R3_MIXER			0x52
#define RT3261_LOUT_MIXER			0x53
/* Power */
#define RT3261_PWR_DIG1			0x61
#define RT3261_PWR_DIG2			0x62
#define RT3261_PWR_ANLG1			0x63
#define RT3261_PWR_ANLG2			0x64
#define RT3261_PWR_MIXER			0x65
#define RT3261_PWR_VOL				0x66
/* Private Register Control */
#define RT3261_PRIV_INDEX			0x6a
#define RT3261_PRIV_DATA			0x6c
/* Format - ADC/DAC */
#define RT3261_I2S1_SDP				0x70
#define RT3261_I2S2_SDP				0x71
#define RT3261_I2S3_SDP				0x72
#define RT3261_ADDA_CLK1			0x73
#define RT3261_ADDA_CLK2			0x74
#define RT3261_DMIC				0x75
/* Function - Analog */
#define RT3261_GLB_CLK				0x80
#define RT3261_PLL_CTRL1			0x81
#define RT3261_PLL_CTRL2			0x82
#define RT3261_ASRC_1				0x83
#define RT3261_ASRC_2				0x84
#define RT3261_ASRC_3				0x85
#define RT3261_ASRC_4				0x89
#define RT3261_ASRC_5				0x8a
#define RT3261_HP_OVCD				0x8b
#define RT3261_CLS_D_OVCD			0x8c
#define RT3261_CLS_D_OUT			0x8d
#define RT3261_DEPOP_M1			0x8e
#define RT3261_DEPOP_M2			0x8f
#define RT3261_DEPOP_M3			0x90
#define RT3261_CHARGE_PUMP			0x91
#define RT3261_PV_DET_SPK_G			0x92
#define RT3261_MICBIAS				0x93
/* Function - Digital */
#define RT3261_EQ_CTRL1				0xb0
#define RT3261_EQ_CTRL2				0xb1
#define RT3261_WIND_FILTER			0xb2
#define RT3261_DRC_AGC_1			0xb4
#define RT3261_DRC_AGC_2			0xb5
#define RT3261_DRC_AGC_3			0xb6
#define RT3261_SVOL_ZC				0xb7
#define RT3261_ANC_CTRL1			0xb8
#define RT3261_ANC_CTRL2			0xb9
#define RT3261_ANC_CTRL3			0xba
#define RT3261_JD_CTRL				0xbb
#define RT3261_ANC_JD				0xbc
#define RT3261_IRQ_CTRL1			0xbd
#define RT3261_IRQ_CTRL2			0xbe
#define RT3261_INT_IRQ_ST			0xbf
#define RT3261_GPIO_CTRL1			0xc0
#define RT3261_GPIO_CTRL2			0xc1
#define RT3261_GPIO_CTRL3			0xc2
#define RT3261_DSP_CTRL1			0xc4
#define RT3261_DSP_CTRL2			0xc5
#define RT3261_DSP_CTRL3			0xc6
#define RT3261_DSP_CTRL4			0xc7
#define RT3261_PGM_REG_ARR1			0xc8
#define RT3261_PGM_REG_ARR2			0xc9
#define RT3261_PGM_REG_ARR3			0xca
#define RT3261_PGM_REG_ARR4			0xcb
#define RT3261_PGM_REG_ARR5			0xcc
#define RT3261_SCB_FUNC			0xcd
#define RT3261_SCB_CTRL				0xce
#define RT3261_BASE_BACK			0xcf
#define RT3261_MP3_PLUS1			0xd0
#define RT3261_MP3_PLUS2			0xd1
#define RT3261_3D_HP				0xd2
#define RT3261_ADJ_HPF				0xd3
#define RT3261_HP_CALIB_AMP_DET		0xd6
#define RT3261_HP_CALIB2			0xd7
#define RT3261_SV_ZCD1				0xd9
#define RT3261_SV_ZCD2				0xda
/* General Control */
#define RT3261_GEN_CTRL1			0xfa
#define RT3261_GEN_CTRL2			0xfb
#define RT3261_GEN_CTRL3			0xfc


/* Index of Codec Private Register definition */
#define RT3261_BIAS_CUR1			0x12
#define RT3261_BIAS_CUR3			0x14
#define RT3261_CLSD_INT_REG1			0x1c
#define RT3261_MAMP_INT_REG2			0x37
#define RT3261_CHOP_DAC_ADC			0x3d
#define RT3261_MIXER_INT_REG			0x3f
#define RT3261_3D_SPK				0x63
#define RT3261_WND_1				0x6c
#define RT3261_WND_2				0x6d
#define RT3261_WND_3				0x6e
#define RT3261_WND_4				0x6f
#define RT3261_WND_5				0x70
#define RT3261_WND_8				0x73
#define RT3261_DIP_SPK_INF			0x75
#define RT3261_HP_DCC_INT1			0x77
#define RT3261_EQ_BW_LOP			0xa0
#define RT3261_EQ_GN_LOP			0xa1
#define RT3261_EQ_FC_BP1			0xa2
#define RT3261_EQ_BW_BP1			0xa3
#define RT3261_EQ_GN_BP1			0xa4
#define RT3261_EQ_FC_BP2			0xa5
#define RT3261_EQ_BW_BP2			0xa6
#define RT3261_EQ_GN_BP2			0xa7
#define RT3261_EQ_FC_BP3			0xa8
#define RT3261_EQ_BW_BP3			0xa9
#define RT3261_EQ_GN_BP3			0xaa
#define RT3261_EQ_FC_BP4			0xab
#define RT3261_EQ_BW_BP4			0xac
#define RT3261_EQ_GN_BP4			0xad
#define RT3261_EQ_FC_HIP1			0xae
#define RT3261_EQ_GN_HIP1			0xaf
#define RT3261_EQ_FC_HIP2			0xb0
#define RT3261_EQ_BW_HIP2			0xb1
#define RT3261_EQ_GN_HIP2			0xb2
#define RT3261_EQ_PRE_VOL			0xb3
#define RT3261_EQ_PST_VOL			0xb4


/* global definition */
#define RT3261_L_MUTE				(0x1 << 15)
#define RT3261_L_MUTE_SFT			15
#define RT3261_VOL_L_MUTE			(0x1 << 14)
#define RT3261_VOL_L_SFT			14
#define RT3261_R_MUTE				(0x1 << 7)
#define RT3261_R_MUTE_SFT			7
#define RT3261_VOL_R_MUTE			(0x1 << 6)
#define RT3261_VOL_R_SFT			6
#define RT3261_L_VOL_MASK			(0x3f << 8)
#define RT3261_L_VOL_SFT			8
#define RT3261_R_VOL_MASK			(0x3f)
#define RT3261_R_VOL_SFT			0

/* IN1 and IN2 Control (0x0d) */
/* IN3 and IN4 Control (0x0e) */
#define RT3261_BST_MASK1			(0xf<<12)
#define RT3261_BST_SFT1				12
#define RT3261_BST_MASK2			(0xf<<8)
#define RT3261_BST_SFT2				8
#define RT3261_IN_DF1				(0x1 << 7)
#define RT3261_IN_SFT1				7
#define RT3261_IN_DF2				(0x1 << 6)
#define RT3261_IN_SFT2				6

/* INL and INR Volume Control (0x0f) */
#define RT3261_INL_SEL_MASK			(0x1 << 15)
#define RT3261_INL_SEL_SFT			15
#define RT3261_INL_SEL_IN4P			(0x0 << 15)
#define RT3261_INL_SEL_MONOP			(0x1 << 15)
#define RT3261_INL_VOL_MASK			(0x1f << 8)
#define RT3261_INL_VOL_SFT			8
#define RT3261_INR_SEL_MASK			(0x1 << 7)
#define RT3261_INR_SEL_SFT			7
#define RT3261_INR_SEL_IN4N			(0x0 << 7)
#define RT3261_INR_SEL_MONON			(0x1 << 7)
#define RT3261_INR_VOL_MASK			(0x1f)
#define RT3261_INR_VOL_SFT			0

/* DAC1 Digital Volume (0x19) */
#define RT3261_DAC_L1_VOL_MASK		(0xff << 8)
#define RT3261_DAC_L1_VOL_SFT			8
#define RT3261_DAC_R1_VOL_MASK		(0xff)
#define RT3261_DAC_R1_VOL_SFT			0

/* DAC2 Digital Volume (0x1a) */
#define RT3261_DAC_L2_VOL_MASK		(0xff << 8)
#define RT3261_DAC_L2_VOL_SFT			8
#define RT3261_DAC_R2_VOL_MASK		(0xff)
#define RT3261_DAC_R2_VOL_SFT			0

/* DAC2 Control (0x1b) */
#define RT3261_M_DAC_L2_VOL			(0x1 << 13)
#define RT3261_M_DAC_L2_VOL_SFT		13
#define RT3261_M_DAC_R2_VOL			(0x1 << 12)
#define RT3261_M_DAC_R2_VOL_SFT		12

/* ADC Digital Volume Control (0x1c) */
#define RT3261_ADC_L_VOL_MASK			(0x7f << 8)
#define RT3261_ADC_L_VOL_SFT			8
#define RT3261_ADC_R_VOL_MASK			(0x7f)
#define RT3261_ADC_R_VOL_SFT			0

/* Mono ADC Digital Volume Control (0x1d) */
#define RT3261_MONO_ADC_L_VOL_MASK		(0x7f << 8)
#define RT3261_MONO_ADC_L_VOL_SFT		8
#define RT3261_MONO_ADC_R_VOL_MASK		(0x7f)
#define RT3261_MONO_ADC_R_VOL_SFT		0

/* ADC Boost Volume Control (0x1e) */
#define RT3261_ADC_L_BST_MASK			(0x3 << 14)
#define RT3261_ADC_L_BST_SFT			14
#define RT3261_ADC_R_BST_MASK			(0x3 << 12)
#define RT3261_ADC_R_BST_SFT			12
#define RT3261_ADC_COMP_MASK			(0x3 << 10)
#define RT3261_ADC_COMP_SFT			10

/* Stereo ADC Mixer Control (0x27) */
#define RT3261_M_ADC_L1			(0x1 << 14)
#define RT3261_M_ADC_L1_SFT			14
#define RT3261_M_ADC_L2			(0x1 << 13)
#define RT3261_M_ADC_L2_SFT			13
#define RT3261_ADC_1_SRC_MASK			(0x1 << 12)
#define RT3261_ADC_1_SRC_SFT			12
#define RT3261_ADC_1_SRC_ADC			(0x1 << 12)
#define RT3261_ADC_1_SRC_DACMIX		(0x0 << 12)
#define RT3261_ADC_2_SRC_MASK			(0x3 << 10)
#define RT3261_ADC_2_SRC_SFT			10
#define RT3261_ADC_2_SRC_DMIC1		(0x0 << 10)
#define RT3261_ADC_2_SRC_DMIC2		(0x1 << 10)
#define RT3261_ADC_2_SRC_DACMIX		(0x2 << 10)
#define RT3261_M_ADC_R1			(0x1 << 6)
#define RT3261_M_ADC_R1_SFT			6
#define RT3261_M_ADC_R2			(0x1 << 5)
#define RT3261_M_ADC_R2_SFT			5

/* Mono ADC Mixer Control (0x28) */
#define RT3261_M_MONO_ADC_L1			(0x1 << 14)
#define RT3261_M_MONO_ADC_L1_SFT		14
#define RT3261_M_MONO_ADC_L2			(0x1 << 13)
#define RT3261_M_MONO_ADC_L2_SFT		13
#define RT3261_MONO_ADC_L1_SRC_MASK		(0x1 << 12)
#define RT3261_MONO_ADC_L1_SRC_SFT		12
#define RT3261_MONO_ADC_L1_SRC_DACMIXL	(0x0 << 12)
#define RT3261_MONO_ADC_L1_SRC_ADCL		(0x1 << 12)
#define RT3261_MONO_ADC_L2_SRC_MASK		(0x3 << 10)
#define RT3261_MONO_ADC_L2_SRC_SFT		10
#define RT3261_MONO_ADC_L2_SRC_DMIC_L1	(0x0 << 10)
#define RT3261_MONO_ADC_L2_SRC_DMIC_L2	(0x1 << 10)
#define RT3261_MONO_ADC_L2_SRC_DACMIXL	(0x2 << 10)
#define RT3261_M_MONO_ADC_R1			(0x1 << 6)
#define RT3261_M_MONO_ADC_R1_SFT		6
#define RT3261_M_MONO_ADC_R2			(0x1 << 5)
#define RT3261_M_MONO_ADC_R2_SFT		5
#define RT3261_MONO_ADC_R1_SRC_MASK		(0x1 << 4)
#define RT3261_MONO_ADC_R1_SRC_SFT		4
#define RT3261_MONO_ADC_R1_SRC_ADCR		(0x1 << 4)
#define RT3261_MONO_ADC_R1_SRC_DACMIXR	(0x0 << 4)
#define RT3261_MONO_ADC_R2_SRC_MASK		(0x3 << 2)
#define RT3261_MONO_ADC_R2_SRC_SFT		2
#define RT3261_MONO_ADC_R2_SRC_DMIC_R1	(0x0 << 2)
#define RT3261_MONO_ADC_R2_SRC_DMIC_R2	(0x1 << 2)
#define RT3261_MONO_ADC_R2_SRC_DACMIXR	(0x2 << 2)

/* ADC Mixer to DAC Mixer Control (0x29) */
#define RT3261_M_ADCMIX_L			(0x1 << 15)
#define RT3261_M_ADCMIX_L_SFT			15
#define RT3261_M_IF1_DAC_L			(0x1 << 14)
#define RT3261_M_IF1_DAC_L_SFT			14
#define RT3261_M_ADCMIX_R			(0x1 << 7)
#define RT3261_M_ADCMIX_R_SFT			7
#define RT3261_M_IF1_DAC_R			(0x1 << 6)
#define RT3261_M_IF1_DAC_R_SFT			6

/* Stereo DAC Mixer Control (0x2a) */
#define RT3261_M_DAC_L1			(0x1 << 14)
#define RT3261_M_DAC_L1_SFT			14
#define RT3261_DAC_L1_STO_L_VOL_MASK		(0x1 << 13)
#define RT3261_DAC_L1_STO_L_VOL_SFT		13
#define RT3261_M_DAC_L2			(0x1 << 12)
#define RT3261_M_DAC_L2_SFT			12
#define RT3261_DAC_L2_STO_L_VOL_MASK		(0x1 << 11)
#define RT3261_DAC_L2_STO_L_VOL_SFT		11
#define RT3261_M_ANC_DAC_L			(0x1 << 10)
#define RT3261_M_ANC_DAC_L_SFT		10
#define RT3261_M_DAC_R1			(0x1 << 6)
#define RT3261_M_DAC_R1_SFT			6
#define RT3261_DAC_R1_STO_R_VOL_MASK		(0x1 << 5)
#define RT3261_DAC_R1_STO_R_VOL_SFT		5
#define RT3261_M_DAC_R2			(0x1 << 4)
#define RT3261_M_DAC_R2_SFT			4
#define RT3261_DAC_R2_STO_R_VOL_MASK		(0x1 << 3)
#define RT3261_DAC_R2_STO_R_VOL_SFT		3
#define RT3261_M_ANC_DAC_R			(0x1 << 2)
#define RT3261_M_ANC_DAC_R_SFT		2

/* Mono DAC Mixer Control (0x2b) */
#define RT3261_M_DAC_L1_MONO_L		(0x1 << 14)
#define RT3261_M_DAC_L1_MONO_L_SFT		14
#define RT3261_DAC_L1_MONO_L_VOL_MASK	(0x1 << 13)
#define RT3261_DAC_L1_MONO_L_VOL_SFT		13
#define RT3261_M_DAC_L2_MONO_L		(0x1 << 12)
#define RT3261_M_DAC_L2_MONO_L_SFT		12
#define RT3261_DAC_L2_MONO_L_VOL_MASK	(0x1 << 11)
#define RT3261_DAC_L2_MONO_L_VOL_SFT		11
#define RT3261_M_DAC_R2_MONO_L		(0x1 << 10)
#define RT3261_M_DAC_R2_MONO_L_SFT		10
#define RT3261_DAC_R2_MONO_L_VOL_MASK	(0x1 << 9)
#define RT3261_DAC_R2_MONO_L_VOL_SFT		9
#define RT3261_M_DAC_R1_MONO_R		(0x1 << 6)
#define RT3261_M_DAC_R1_MONO_R_SFT		6
#define RT3261_DAC_R1_MONO_R_VOL_MASK	(0x1 << 5)
#define RT3261_DAC_R1_MONO_R_VOL_SFT		5
#define RT3261_M_DAC_R2_MONO_R		(0x1 << 4)
#define RT3261_M_DAC_R2_MONO_R_SFT		4
#define RT3261_DAC_R2_MONO_R_VOL_MASK	(0x1 << 3)
#define RT3261_DAC_R2_MONO_R_VOL_SFT		3
#define RT3261_M_DAC_L2_MONO_R		(0x1 << 2)
#define RT3261_M_DAC_L2_MONO_R_SFT		2
#define RT3261_DAC_L2_MONO_R_VOL_MASK	(0x1 << 1)
#define RT3261_DAC_L2_MONO_R_VOL_SFT		1

/* Digital Mixer Control (0x2c) */
#define RT3261_M_STO_L_DAC_L			(0x1 << 15)
#define RT3261_M_STO_L_DAC_L_SFT		15
#define RT3261_STO_L_DAC_L_VOL_MASK		(0x1 << 14)
#define RT3261_STO_L_DAC_L_VOL_SFT		14
#define RT3261_M_DAC_L2_DAC_L			(0x1 << 13)
#define RT3261_M_DAC_L2_DAC_L_SFT		13
#define RT3261_DAC_L2_DAC_L_VOL_MASK		(0x1 << 12)
#define RT3261_DAC_L2_DAC_L_VOL_SFT		12
#define RT3261_M_STO_R_DAC_R			(0x1 << 11)
#define RT3261_M_STO_R_DAC_R_SFT		11
#define RT3261_STO_R_DAC_R_VOL_MASK		(0x1 << 10)
#define RT3261_STO_R_DAC_R_VOL_SFT		10
#define RT3261_M_DAC_R2_DAC_R			(0x1 << 9)
#define RT3261_M_DAC_R2_DAC_R_SFT		9
#define RT3261_DAC_R2_DAC_R_VOL_MASK		(0x1 << 8)
#define RT3261_DAC_R2_DAC_R_VOL_SFT		8

/* DSP Path Control 1 (0x2d) */
#define RT3261_RXDP_SRC_MASK			(0x1 << 15)
#define RT3261_RXDP_SRC_SFT			15
#define RT3261_RXDP_SRC_NOR			(0x0 << 15)
#define RT3261_RXDP_SRC_DIV3			(0x1 << 15)
#define RT3261_TXDP_SRC_MASK			(0x1 << 14)
#define RT3261_TXDP_SRC_SFT			14
#define RT3261_TXDP_SRC_NOR			(0x0 << 14)
#define RT3261_TXDP_SRC_DIV3			(0x1 << 14)

/* DSP Path Control 2 (0x2e) */
#define RT3261_DAC_L2_SEL_MASK		(0x3 << 14)
#define RT3261_DAC_L2_SEL_SFT			14
#define RT3261_DAC_L2_SEL_IF2			(0x0 << 14)
#define RT3261_DAC_L2_SEL_IF3			(0x1 << 14)
#define RT3261_DAC_L2_SEL_TXDC			(0x2 << 14)
#define RT3261_DAC_L2_SEL_BASS			(0x3 << 14)
#define RT3261_DAC_R2_SEL_MASK		(0x3 << 12)
#define RT3261_DAC_R2_SEL_SFT			12
#define RT3261_DAC_R2_SEL_IF2			(0x0 << 12)
#define RT3261_DAC_R2_SEL_IF3			(0x1 << 12)
#define RT3261_DAC_R2_SEL_TXDC		(0x2 << 12)
#define RT3261_IF2_ADC_L_SEL_MASK		(0x1 << 11)
#define RT3261_IF2_ADC_L_SEL_SFT		11
#define RT3261_IF2_ADC_L_SEL_TXDP		(0x0 << 11)
#define RT3261_IF2_ADC_L_SEL_PASS		(0x1 << 11)
#define RT3261_IF2_ADC_R_SEL_MASK		(0x1 << 10)
#define RT3261_IF2_ADC_R_SEL_SFT		10
#define RT3261_IF2_ADC_R_SEL_TXDP		(0x0 << 10)
#define RT3261_IF2_ADC_R_SEL_PASS		(0x1 << 10)
#define RT3261_RXDC_SEL_MASK			(0x3 << 8)
#define RT3261_RXDC_SEL_SFT			8
#define RT3261_RXDC_SEL_NOR			(0x0 << 8)
#define RT3261_RXDC_SEL_L2R			(0x1 << 8)
#define RT3261_RXDC_SEL_R2L			(0x2 << 8)
#define RT3261_RXDC_SEL_SWAP			(0x3 << 8)
#define RT3261_RXDP_SEL_MASK			(0x3 << 6)
#define RT3261_RXDP_SEL_SFT			6
#define RT3261_RXDP_SEL_NOR			(0x0 << 6)
#define RT3261_RXDP_SEL_L2R			(0x1 << 6)
#define RT3261_RXDP_SEL_R2L			(0x2 << 6)
#define RT3261_RXDP_SEL_SWAP			(0x3 << 6)
#define RT3261_TXDC_SEL_MASK			(0x3 << 4)
#define RT3261_TXDC_SEL_SFT			4
#define RT3261_TXDC_SEL_NOR			(0x0 << 4)
#define RT3261_TXDC_SEL_L2R			(0x1 << 4)
#define RT3261_TXDC_SEL_R2L			(0x2 << 4)
#define RT3261_TXDC_SEL_SWAP			(0x3 << 4)
#define RT3261_TXDP_SEL_MASK			(0x3 << 2)
#define RT3261_TXDP_SEL_SFT			2
#define RT3261_TXDP_SEL_NOR			(0x0 << 2)
#define RT3261_TXDP_SEL_L2R			(0x1 << 2)
#define RT3261_TXDP_SEL_R2L			(0x2 << 2)
#define RT3261_TRXDP_SEL_SWAP			(0x3 << 2)

/* Digital Interface Data Control (0x2f) */
#define RT3261_IF1_DAC_SEL_MASK		(0x3 << 14)
#define RT3261_IF1_DAC_SEL_SFT			14
#define RT3261_IF1_DAC_SEL_NOR			(0x0 << 14)
#define RT3261_IF1_DAC_SEL_L2R			(0x1 << 14)
#define RT3261_IF1_DAC_SEL_R2L			(0x2 << 14)
#define RT3261_IF1_DAC_SEL_SWAP		(0x3 << 14)
#define RT3261_IF1_ADC_SEL_MASK		(0x3 << 12)
#define RT3261_IF1_ADC_SEL_SFT			12
#define RT3261_IF1_ADC_SEL_NOR			(0x0 << 12)
#define RT3261_IF1_ADC_SEL_L2R			(0x1 << 12)
#define RT3261_IF1_ADC_SEL_R2L			(0x2 << 12)
#define RT3261_IF1_ADC_SEL_SWAP		(0x3 << 12)
#define RT3261_IF2_DAC_SEL_MASK		(0x3 << 10)
#define RT3261_IF2_DAC_SEL_SFT			10
#define RT3261_IF2_DAC_SEL_NOR			(0x0 << 10)
#define RT3261_IF2_DAC_SEL_L2R			(0x1 << 10)
#define RT3261_IF2_DAC_SEL_R2L			(0x2 << 10)
#define RT3261_IF2_DAC_SEL_SWAP		(0x3 << 10)
#define RT3261_IF2_ADC_SEL_MASK		(0x3 << 8)
#define RT3261_IF2_ADC_SEL_SFT			8
#define RT3261_IF2_ADC_SEL_NOR			(0x0 << 8)
#define RT3261_IF2_ADC_SEL_L2R			(0x1 << 8)
#define RT3261_IF2_ADC_SEL_R2L			(0x2 << 8)
#define RT3261_IF2_ADC_SEL_SWAP		(0x3 << 8)
#define RT3261_IF3_DAC_SEL_MASK		(0x3 << 6)
#define RT3261_IF3_DAC_SEL_SFT			6
#define RT3261_IF3_DAC_SEL_NOR			(0x0 << 6)
#define RT3261_IF3_DAC_SEL_L2R			(0x1 << 6)
#define RT3261_IF3_DAC_SEL_R2L			(0x2 << 6)
#define RT3261_IF3_DAC_SEL_SWAP		(0x3 << 6)
#define RT3261_IF3_ADC_SEL_MASK		(0x3 << 4)
#define RT3261_IF3_ADC_SEL_SFT			4
#define RT3261_IF3_ADC_SEL_NOR			(0x0 << 4)
#define RT3261_IF3_ADC_SEL_L2R			(0x1 << 4)
#define RT3261_IF3_ADC_SEL_R2L			(0x2 << 4)
#define RT3261_IF3_ADC_SEL_SWAP		(0x3 << 4)

/* REC Left Mixer Control 1 (0x3b) */
#define RT3261_G_HP_L_RM_L_MASK		(0x7 << 13)
#define RT3261_G_HP_L_RM_L_SFT			13
#define RT3261_G_IN_L_RM_L_MASK		(0x7 << 10)
#define RT3261_G_IN_L_RM_L_SFT			10
#define RT3261_G_BST4_RM_L_MASK		(0x7 << 7)
#define RT3261_G_BST4_RM_L_SFT			7
#define RT3261_G_BST3_RM_L_MASK		(0x7 << 4)
#define RT3261_G_BST3_RM_L_SFT			4
#define RT3261_G_BST2_RM_L_MASK		(0x7 << 1)
#define RT3261_G_BST2_RM_L_SFT			1

/* REC Left Mixer Control 2 (0x3c) */
#define RT3261_G_BST1_RM_L_MASK		(0x7 << 13)
#define RT3261_G_BST1_RM_L_SFT			13
#define RT3261_G_OM_L_RM_L_MASK		(0x7 << 10)
#define RT3261_G_OM_L_RM_L_SFT		10
#define RT3261_M_HP_L_RM_L			(0x1 << 6)
#define RT3261_M_HP_L_RM_L_SFT		6
#define RT3261_M_IN_L_RM_L			(0x1 << 5)
#define RT3261_M_IN_L_RM_L_SFT			5
#define RT3261_M_BST4_RM_L			(0x1 << 4)
#define RT3261_M_BST4_RM_L_SFT		4
#define RT3261_M_BST3_RM_L			(0x1 << 3)
#define RT3261_M_BST3_RM_L_SFT		3
#define RT3261_M_BST2_RM_L			(0x1 << 2)
#define RT3261_M_BST2_RM_L_SFT		2
#define RT3261_M_BST1_RM_L			(0x1 << 1)
#define RT3261_M_BST1_RM_L_SFT		1
#define RT3261_M_OM_L_RM_L			(0x1)
#define RT3261_M_OM_L_RM_L_SFT		0

/* REC Right Mixer Control 1 (0x3d) */
#define RT3261_G_HP_R_RM_R_MASK		(0x7 << 13)
#define RT3261_G_HP_R_RM_R_SFT		13
#define RT3261_G_IN_R_RM_R_MASK		(0x7 << 10)
#define RT3261_G_IN_R_RM_R_SFT			10
#define RT3261_G_BST4_RM_R_MASK		(0x7 << 7)
#define RT3261_G_BST4_RM_R_SFT			7
#define RT3261_G_BST3_RM_R_MASK		(0x7 << 4)
#define RT3261_G_BST3_RM_R_SFT			4
#define RT3261_G_BST2_RM_R_MASK		(0x7 << 1)
#define RT3261_G_BST2_RM_R_SFT			1

/* REC Right Mixer Control 2 (0x3e) */
#define RT3261_G_BST1_RM_R_MASK		(0x7 << 13)
#define RT3261_G_BST1_RM_R_SFT			13
#define RT3261_G_OM_R_RM_R_MASK		(0x7 << 10)
#define RT3261_G_OM_R_RM_R_SFT		10
#define RT3261_M_HP_R_RM_R			(0x1 << 6)
#define RT3261_M_HP_R_RM_R_SFT		6
#define RT3261_M_IN_R_RM_R			(0x1 << 5)
#define RT3261_M_IN_R_RM_R_SFT			5
#define RT3261_M_BST4_RM_R			(0x1 << 4)
#define RT3261_M_BST4_RM_R_SFT		4
#define RT3261_M_BST3_RM_R			(0x1 << 3)
#define RT3261_M_BST3_RM_R_SFT		3
#define RT3261_M_BST2_RM_R			(0x1 << 2)
#define RT3261_M_BST2_RM_R_SFT		2
#define RT3261_M_BST1_RM_R			(0x1 << 1)
#define RT3261_M_BST1_RM_R_SFT		1
#define RT3261_M_OM_R_RM_R			(0x1)
#define RT3261_M_OM_R_RM_R_SFT		0

/* HPMIX Control (0x45) */
#define RT3261_M_DAC2_HM			(0x1 << 15)
#define RT3261_M_DAC2_HM_SFT			15
#define RT3261_M_DAC1_HM			(0x1 << 14)
#define RT3261_M_DAC1_HM_SFT			14
#define RT3261_M_HPVOL_HM			(0x1 << 13)
#define RT3261_M_HPVOL_HM_SFT			13
#define RT3261_G_HPOMIX_MASK			(0x1 << 12)
#define RT3261_G_HPOMIX_SFT			12

/* SPK Left Mixer Control (0x46) */
#define RT3261_G_RM_L_SM_L_MASK		(0x3 << 14)
#define RT3261_G_RM_L_SM_L_SFT		14
#define RT3261_G_IN_L_SM_L_MASK		(0x3 << 12)
#define RT3261_G_IN_L_SM_L_SFT			12
#define RT3261_G_DAC_L1_SM_L_MASK		(0x3 << 10)
#define RT3261_G_DAC_L1_SM_L_SFT		10
#define RT3261_G_DAC_L2_SM_L_MASK		(0x3 << 8)
#define RT3261_G_DAC_L2_SM_L_SFT		8
#define RT3261_G_OM_L_SM_L_MASK		(0x3 << 6)
#define RT3261_G_OM_L_SM_L_SFT		6
#define RT3261_M_RM_L_SM_L			(0x1 << 5)
#define RT3261_M_RM_L_SM_L_SFT		5
#define RT3261_M_IN_L_SM_L			(0x1 << 4)
#define RT3261_M_IN_L_SM_L_SFT			4
#define RT3261_M_DAC_L1_SM_L			(0x1 << 3)
#define RT3261_M_DAC_L1_SM_L_SFT		3
#define RT3261_M_DAC_L2_SM_L			(0x1 << 2)
#define RT3261_M_DAC_L2_SM_L_SFT		2
#define RT3261_M_OM_L_SM_L			(0x1 << 1)
#define RT3261_M_OM_L_SM_L_SFT		1

/* SPK Right Mixer Control (0x47) */
#define RT3261_G_RM_R_SM_R_MASK		(0x3 << 14)
#define RT3261_G_RM_R_SM_R_SFT		14
#define RT3261_G_IN_R_SM_R_MASK		(0x3 << 12)
#define RT3261_G_IN_R_SM_R_SFT			12
#define RT3261_G_DAC_R1_SM_R_MASK		(0x3 << 10)
#define RT3261_G_DAC_R1_SM_R_SFT		10
#define RT3261_G_DAC_R2_SM_R_MASK		(0x3 << 8)
#define RT3261_G_DAC_R2_SM_R_SFT		8
#define RT3261_G_OM_R_SM_R_MASK		(0x3 << 6)
#define RT3261_G_OM_R_SM_R_SFT		6
#define RT3261_M_RM_R_SM_R			(0x1 << 5)
#define RT3261_M_RM_R_SM_R_SFT		5
#define RT3261_M_IN_R_SM_R			(0x1 << 4)
#define RT3261_M_IN_R_SM_R_SFT			4
#define RT3261_M_DAC_R1_SM_R			(0x1 << 3)
#define RT3261_M_DAC_R1_SM_R_SFT		3
#define RT3261_M_DAC_R2_SM_R			(0x1 << 2)
#define RT3261_M_DAC_R2_SM_R_SFT		2
#define RT3261_M_OM_R_SM_R			(0x1 << 1)
#define RT3261_M_OM_R_SM_R_SFT		1

/* SPOLMIX Control (0x48) */
#define RT3261_M_DAC_R1_SPM_L			(0x1 << 15)
#define RT3261_M_DAC_R1_SPM_L_SFT		15
#define RT3261_M_DAC_L1_SPM_L			(0x1 << 14)
#define RT3261_M_DAC_L1_SPM_L_SFT		14
#define RT3261_M_SV_R_SPM_L			(0x1 << 13)
#define RT3261_M_SV_R_SPM_L_SFT		13
#define RT3261_M_SV_L_SPM_L			(0x1 << 12)
#define RT3261_M_SV_L_SPM_L_SFT		12
#define RT3261_M_BST1_SPM_L			(0x1 << 11)
#define RT3261_M_BST1_SPM_L_SFT		11

/* SPORMIX Control (0x49) */
#define RT3261_M_DAC_R1_SPM_R			(0x1 << 13)
#define RT3261_M_DAC_R1_SPM_R_SFT		13
#define RT3261_M_SV_R_SPM_R			(0x1 << 12)
#define RT3261_M_SV_R_SPM_R_SFT		12
#define RT3261_M_BST1_SPM_R			(0x1 << 11)
#define RT3261_M_BST1_SPM_R_SFT		11

/* SPOLMIX / SPORMIX Ratio Control (0x4a) */
#define RT3261_SPO_CLSD_RATIO_MASK		(0x7)
#define RT3261_SPO_CLSD_RATIO_SFT		0

/* Mono Output Mixer Control (0x4c) */
#define RT3261_M_DAC_R2_MM			(0x1 << 15)
#define RT3261_M_DAC_R2_MM_SFT		15
#define RT3261_M_DAC_L2_MM			(0x1 << 14)
#define RT3261_M_DAC_L2_MM_SFT		14
#define RT3261_M_OV_R_MM			(0x1 << 13)
#define RT3261_M_OV_R_MM_SFT			13
#define RT3261_M_OV_L_MM			(0x1 << 12)
#define RT3261_M_OV_L_MM_SFT			12
#define RT3261_M_BST1_MM			(0x1 << 11)
#define RT3261_M_BST1_MM_SFT			11
#define RT3261_G_MONOMIX_MASK		(0x1 << 10)
#define RT3261_G_MONOMIX_SFT			10

/* Output Left Mixer Control 1 (0x4d) */
#define RT3261_G_BST3_OM_L_MASK		(0x7 << 13)
#define RT3261_G_BST3_OM_L_SFT		13
#define RT3261_G_BST2_OM_L_MASK		(0x7 << 10)
#define RT3261_G_BST2_OM_L_SFT		10
#define RT3261_G_BST1_OM_L_MASK		(0x7 << 7)
#define RT3261_G_BST1_OM_L_SFT		7
#define RT3261_G_IN_L_OM_L_MASK		(0x7 << 4)
#define RT3261_G_IN_L_OM_L_SFT			4
#define RT3261_G_RM_L_OM_L_MASK		(0x7 << 1)
#define RT3261_G_RM_L_OM_L_SFT		1

/* Output Left Mixer Control 2 (0x4e) */
#define RT3261_G_DAC_R2_OM_L_MASK		(0x7 << 13)
#define RT3261_G_DAC_R2_OM_L_SFT		13
#define RT3261_G_DAC_L2_OM_L_MASK		(0x7 << 10)
#define RT3261_G_DAC_L2_OM_L_SFT		10
#define RT3261_G_DAC_L1_OM_L_MASK		(0x7 << 7)
#define RT3261_G_DAC_L1_OM_L_SFT		7

/* Output Left Mixer Control 3 (0x4f) */
#define RT3261_M_SM_L_OM_L			(0x1 << 8)
#define RT3261_M_SM_L_OM_L_SFT		8
#define RT3261_M_BST3_OM_L			(0x1 << 7)
#define RT3261_M_BST3_OM_L_SFT		7
#define RT3261_M_BST2_OM_L			(0x1 << 6)
#define RT3261_M_BST2_OM_L_SFT		6
#define RT3261_M_BST1_OM_L			(0x1 << 5)
#define RT3261_M_BST1_OM_L_SFT		5
#define RT3261_M_IN_L_OM_L			(0x1 << 4)
#define RT3261_M_IN_L_OM_L_SFT			4
#define RT3261_M_RM_L_OM_L			(0x1 << 3)
#define RT3261_M_RM_L_OM_L_SFT		3
#define RT3261_M_DAC_R2_OM_L			(0x1 << 2)
#define RT3261_M_DAC_R2_OM_L_SFT		2
#define RT3261_M_DAC_L2_OM_L			(0x1 << 1)
#define RT3261_M_DAC_L2_OM_L_SFT		1
#define RT3261_M_DAC_L1_OM_L			(0x1)
#define RT3261_M_DAC_L1_OM_L_SFT		0

/* Output Right Mixer Control 1 (0x50) */
#define RT3261_G_BST4_OM_R_MASK		(0x7 << 13)
#define RT3261_G_BST4_OM_R_SFT		13
#define RT3261_G_BST2_OM_R_MASK		(0x7 << 10)
#define RT3261_G_BST2_OM_R_SFT		10
#define RT3261_G_BST1_OM_R_MASK		(0x7 << 7)
#define RT3261_G_BST1_OM_R_SFT		7
#define RT3261_G_IN_R_OM_R_MASK		(0x7 << 4)
#define RT3261_G_IN_R_OM_R_SFT			4
#define RT3261_G_RM_R_OM_R_MASK		(0x7 << 1)
#define RT3261_G_RM_R_OM_R_SFT		1

/* Output Right Mixer Control 2 (0x51) */
#define RT3261_G_DAC_L2_OM_R_MASK		(0x7 << 13)
#define RT3261_G_DAC_L2_OM_R_SFT		13
#define RT3261_G_DAC_R2_OM_R_MASK		(0x7 << 10)
#define RT3261_G_DAC_R2_OM_R_SFT		10
#define RT3261_G_DAC_R1_OM_R_MASK		(0x7 << 7)
#define RT3261_G_DAC_R1_OM_R_SFT		7

/* Output Right Mixer Control 3 (0x52) */
#define RT3261_M_SM_L_OM_R			(0x1 << 8)
#define RT3261_M_SM_L_OM_R_SFT		8
#define RT3261_M_BST4_OM_R			(0x1 << 7)
#define RT3261_M_BST4_OM_R_SFT		7
#define RT3261_M_BST2_OM_R			(0x1 << 6)
#define RT3261_M_BST2_OM_R_SFT		6
#define RT3261_M_BST1_OM_R			(0x1 << 5)
#define RT3261_M_BST1_OM_R_SFT		5
#define RT3261_M_IN_R_OM_R			(0x1 << 4)
#define RT3261_M_IN_R_OM_R_SFT		4
#define RT3261_M_RM_R_OM_R			(0x1 << 3)
#define RT3261_M_RM_R_OM_R_SFT		3
#define RT3261_M_DAC_L2_OM_R			(0x1 << 2)
#define RT3261_M_DAC_L2_OM_R_SFT		2
#define RT3261_M_DAC_R2_OM_R			(0x1 << 1)
#define RT3261_M_DAC_R2_OM_R_SFT		1
#define RT3261_M_DAC_R1_OM_R			(0x1)
#define RT3261_M_DAC_R1_OM_R_SFT		0

/* LOUT Mixer Control (0x53) */
#define RT3261_M_DAC_L1_LM			(0x1 << 15)
#define RT3261_M_DAC_L1_LM_SFT		15
#define RT3261_M_DAC_R1_LM			(0x1 << 14)
#define RT3261_M_DAC_R1_LM_SFT		14
#define RT3261_M_OV_L_LM			(0x1 << 13)
#define RT3261_M_OV_L_LM_SFT			13
#define RT3261_M_OV_R_LM			(0x1 << 12)
#define RT3261_M_OV_R_LM_SFT			12
#define RT3261_G_LOUTMIX_MASK			(0x1 << 11)
#define RT3261_G_LOUTMIX_SFT			11

/* Power Management for Digital 1 (0x61) */
#define RT3261_PWR_I2S1				(0x1 << 15)
#define RT3261_PWR_I2S1_BIT			15
#define RT3261_PWR_I2S2				(0x1 << 14)
#define RT3261_PWR_I2S2_BIT			14
#define RT3261_PWR_I2S3				(0x1 << 13)
#define RT3261_PWR_I2S3_BIT			13
#define RT3261_PWR_DAC_L1			(0x1 << 12)
#define RT3261_PWR_DAC_L1_BIT			12
#define RT3261_PWR_DAC_R1			(0x1 << 11)
#define RT3261_PWR_DAC_R1_BIT			11
#define RT3261_PWR_DAC_L2			(0x1 << 7)
#define RT3261_PWR_DAC_L2_BIT			7
#define RT3261_PWR_DAC_R2			(0x1 << 6)
#define RT3261_PWR_DAC_R2_BIT			6
#define RT3261_PWR_ADC_L			(0x1 << 2)
#define RT3261_PWR_ADC_L_BIT			2
#define RT3261_PWR_ADC_R			(0x1 << 1)
#define RT3261_PWR_ADC_R_BIT			1
#define RT3261_PWR_CLS_D			(0x1)
#define RT3261_PWR_CLS_D_BIT			0

/* Power Management for Digital 2 (0x62) */
#define RT3261_PWR_ADC_SF			(0x1 << 15)
#define RT3261_PWR_ADC_SF_BIT			15
#define RT3261_PWR_ADC_MF_L			(0x1 << 14)
#define RT3261_PWR_ADC_MF_L_BIT		14
#define RT3261_PWR_ADC_MF_R			(0x1 << 13)
#define RT3261_PWR_ADC_MF_R_BIT		13
#define RT3261_PWR_I2S_DSP			(0x1 << 12)
#define RT3261_PWR_I2S_DSP_BIT			12

/* Power Management for Analog 1 (0x63) */
#define RT3261_PWR_VREF1			(0x1 << 15)
#define RT3261_PWR_VREF1_BIT			15
#define RT3261_PWR_FV1				(0x1 << 14)
#define RT3261_PWR_FV1_BIT			14
#define RT3261_PWR_MB				(0x1 << 13)
#define RT3261_PWR_MB_BIT			13
#define RT3261_PWR_LM				(0x1 << 12)
#define RT3261_PWR_LM_BIT			12
#define RT3261_PWR_BG				(0x1 << 11)
#define RT3261_PWR_BG_BIT			11
#define RT3261_PWR_MM				(0x1 << 10)
#define RT3261_PWR_MM_BIT			10
#define RT3261_PWR_MA				(0x1 << 8)
#define RT3261_PWR_MA_BIT			8
#define RT3261_PWR_HP_L			(0x1 << 7)
#define RT3261_PWR_HP_L_BIT			7
#define RT3261_PWR_HP_R			(0x1 << 6)
#define RT3261_PWR_HP_R_BIT			6
#define RT3261_PWR_HA				(0x1 << 5)
#define RT3261_PWR_HA_BIT			5
#define RT3261_PWR_VREF2			(0x1 << 4)
#define RT3261_PWR_VREF2_BIT			4
#define RT3261_PWR_FV2				(0x1 << 3)
#define RT3261_PWR_FV2_BIT			3
#define RT3261_PWR_LDO2			(0x1 << 2)
#define RT3261_PWR_LDO2_BIT			2

/* Power Management for Analog 2 (0x64) */
#define RT3261_PWR_BST1			(0x1 << 15)
#define RT3261_PWR_BST1_BIT			15
#define RT3261_PWR_BST2			(0x1 << 14)
#define RT3261_PWR_BST2_BIT			14
#define RT3261_PWR_BST3			(0x1 << 13)
#define RT3261_PWR_BST3_BIT			13
#define RT3261_PWR_BST4			(0x1 << 12)
#define RT3261_PWR_BST4_BIT			12
#define RT3261_PWR_MB1				(0x1 << 11)
#define RT3261_PWR_MB1_BIT			11
#define RT3261_PWR_MB2				(0x1 << 10)
#define RT3261_PWR_MB2_BIT			10
#define RT3261_PWR_PLL				(0x1 << 9)
#define RT3261_PWR_PLL_BIT			9

/* Power Management for Mixer (0x65) */
#define RT3261_PWR_OM_L			(0x1 << 15)
#define RT3261_PWR_OM_L_BIT			15
#define RT3261_PWR_OM_R			(0x1 << 14)
#define RT3261_PWR_OM_R_BIT			14
#define RT3261_PWR_SM_L			(0x1 << 13)
#define RT3261_PWR_SM_L_BIT			13
#define RT3261_PWR_SM_R			(0x1 << 12)
#define RT3261_PWR_SM_R_BIT			12
#define RT3261_PWR_RM_L			(0x1 << 11)
#define RT3261_PWR_RM_L_BIT			11
#define RT3261_PWR_RM_R			(0x1 << 10)
#define RT3261_PWR_RM_R_BIT			10

/* Power Management for Volume (0x66) */
#define RT3261_PWR_SV_L			(0x1 << 15)
#define RT3261_PWR_SV_L_BIT			15
#define RT3261_PWR_SV_R			(0x1 << 14)
#define RT3261_PWR_SV_R_BIT			14
#define RT3261_PWR_OV_L			(0x1 << 13)
#define RT3261_PWR_OV_L_BIT			13
#define RT3261_PWR_OV_R			(0x1 << 12)
#define RT3261_PWR_OV_R_BIT			12
#define RT3261_PWR_HV_L			(0x1 << 11)
#define RT3261_PWR_HV_L_BIT			11
#define RT3261_PWR_HV_R			(0x1 << 10)
#define RT3261_PWR_HV_R_BIT			10
#define RT3261_PWR_IN_L				(0x1 << 9)
#define RT3261_PWR_IN_L_BIT			9
#define RT3261_PWR_IN_R			(0x1 << 8)
#define RT3261_PWR_IN_R_BIT			8

/* I2S1/2/3 Audio Serial Data Port Control (0x70 0x71 0x72) */
#define RT3261_I2S_MS_MASK			(0x1 << 15)
#define RT3261_I2S_MS_SFT			15
#define RT3261_I2S_MS_M			(0x0 << 15)
#define RT3261_I2S_MS_S				(0x1 << 15)
#define RT3261_I2S_IF_MASK			(0x7 << 12)
#define RT3261_I2S_IF_SFT			12
#define RT3261_I2S_O_CP_MASK			(0x3 << 10)
#define RT3261_I2S_O_CP_SFT			10
#define RT3261_I2S_O_CP_OFF			(0x0 << 10)
#define RT3261_I2S_O_CP_U_LAW			(0x1 << 10)
#define RT3261_I2S_O_CP_A_LAW			(0x2 << 10)
#define RT3261_I2S_I_CP_MASK			(0x3 << 8)
#define RT3261_I2S_I_CP_SFT			8
#define RT3261_I2S_I_CP_OFF			(0x0 << 8)
#define RT3261_I2S_I_CP_U_LAW			(0x1 << 8)
#define RT3261_I2S_I_CP_A_LAW			(0x2 << 8)
#define RT3261_I2S_BP_MASK			(0x1 << 7)
#define RT3261_I2S_BP_SFT			7
#define RT3261_I2S_BP_NOR			(0x0 << 7)
#define RT3261_I2S_BP_INV			(0x1 << 7)
#define RT3261_I2S_DL_MASK			(0x3 << 2)
#define RT3261_I2S_DL_SFT			2
#define RT3261_I2S_DL_16			(0x0 << 2)
#define RT3261_I2S_DL_20			(0x1 << 2)
#define RT3261_I2S_DL_24			(0x2 << 2)
#define RT3261_I2S_DL_8				(0x3 << 2)
#define RT3261_I2S_DF_MASK			(0x3)
#define RT3261_I2S_DF_SFT			0
#define RT3261_I2S_DF_I2S			(0x0)
#define RT3261_I2S_DF_LEFT			(0x1)
#define RT3261_I2S_DF_PCM_A			(0x2)
#define RT3261_I2S_DF_PCM_B			(0x3)

/* I2S2 Audio Serial Data Port Control (0x71) */
#define RT3261_I2S2_SDI_MASK			(0x1 << 6)
#define RT3261_I2S2_SDI_SFT			6
#define RT3261_I2S2_SDI_I2S1			(0x0 << 6)
#define RT3261_I2S2_SDI_I2S2			(0x1 << 6)

/* ADC/DAC Clock Control 1 (0x73) */
#define RT3261_I2S_BCLK_MS1_MASK		(0x1 << 15)
#define RT3261_I2S_BCLK_MS1_SFT		15
#define RT3261_I2S_BCLK_MS1_32			(0x0 << 15)
#define RT3261_I2S_BCLK_MS1_64			(0x1 << 15)
#define RT3261_I2S_PD1_MASK			(0x7 << 12)
#define RT3261_I2S_PD1_SFT			12
#define RT3261_I2S_PD1_1			(0x0 << 12)
#define RT3261_I2S_PD1_2			(0x1 << 12)
#define RT3261_I2S_PD1_3			(0x2 << 12)
#define RT3261_I2S_PD1_4			(0x3 << 12)
#define RT3261_I2S_PD1_6			(0x4 << 12)
#define RT3261_I2S_PD1_8			(0x5 << 12)
#define RT3261_I2S_PD1_12			(0x6 << 12)
#define RT3261_I2S_PD1_16			(0x7 << 12)
#define RT3261_I2S_BCLK_MS2_MASK		(0x1 << 11)
#define RT3261_I2S_BCLK_MS2_SFT		11
#define RT3261_I2S_BCLK_MS2_32			(0x0 << 11)
#define RT3261_I2S_BCLK_MS2_64			(0x1 << 11)
#define RT3261_I2S_PD2_MASK			(0x7 << 8)
#define RT3261_I2S_PD2_SFT			8
#define RT3261_I2S_PD2_1			(0x0 << 8)
#define RT3261_I2S_PD2_2			(0x1 << 8)
#define RT3261_I2S_PD2_3			(0x2 << 8)
#define RT3261_I2S_PD2_4			(0x3 << 8)
#define RT3261_I2S_PD2_6			(0x4 << 8)
#define RT3261_I2S_PD2_8			(0x5 << 8)
#define RT3261_I2S_PD2_12			(0x6 << 8)
#define RT3261_I2S_PD2_16			(0x7 << 8)
#define RT3261_I2S_BCLK_MS3_MASK		(0x1 << 7)
#define RT3261_I2S_BCLK_MS3_SFT		7
#define RT3261_I2S_BCLK_MS3_32			(0x0 << 7)
#define RT3261_I2S_BCLK_MS3_64			(0x1 << 7)
#define RT3261_I2S_PD3_MASK			(0x7 << 4)
#define RT3261_I2S_PD3_SFT			4
#define RT3261_I2S_PD3_1			(0x0 << 4)
#define RT3261_I2S_PD3_2			(0x1 << 4)
#define RT3261_I2S_PD3_3			(0x2 << 4)
#define RT3261_I2S_PD3_4			(0x3 << 4)
#define RT3261_I2S_PD3_6			(0x4 << 4)
#define RT3261_I2S_PD3_8			(0x5 << 4)
#define RT3261_I2S_PD3_12			(0x6 << 4)
#define RT3261_I2S_PD3_16			(0x7 << 4)
#define RT3261_DAC_OSR_MASK			(0x3 << 2)
#define RT3261_DAC_OSR_SFT			2
#define RT3261_DAC_OSR_128			(0x0 << 2)
#define RT3261_DAC_OSR_64			(0x1 << 2)
#define RT3261_DAC_OSR_32			(0x2 << 2)
#define RT3261_DAC_OSR_16			(0x3 << 2)
#define RT3261_ADC_OSR_MASK			(0x3)
#define RT3261_ADC_OSR_SFT			0
#define RT3261_ADC_OSR_128			(0x0)
#define RT3261_ADC_OSR_64			(0x1)
#define RT3261_ADC_OSR_32			(0x2)
#define RT3261_ADC_OSR_16			(0x3)

/* ADC/DAC Clock Control 2 (0x74) */
#define RT3261_DAC_L_OSR_MASK			(0x3 << 14)
#define RT3261_DAC_L_OSR_SFT			14
#define RT3261_DAC_L_OSR_128			(0x0 << 14)
#define RT3261_DAC_L_OSR_64			(0x1 << 14)
#define RT3261_DAC_L_OSR_32			(0x2 << 14)
#define RT3261_DAC_L_OSR_16			(0x3 << 14)
#define RT3261_ADC_R_OSR_MASK			(0x3 << 12)
#define RT3261_ADC_R_OSR_SFT			12
#define RT3261_ADC_R_OSR_128			(0x0 << 12)
#define RT3261_ADC_R_OSR_64			(0x1 << 12)
#define RT3261_ADC_R_OSR_32			(0x2 << 12)
#define RT3261_ADC_R_OSR_16			(0x3 << 12)
#define RT3261_DAHPF_EN			(0x1 << 11)
#define RT3261_DAHPF_EN_SFT			11
#define RT3261_ADHPF_EN			(0x1 << 10)
#define RT3261_ADHPF_EN_SFT			10

/* Digital Microphone Control (0x75) */
#define RT3261_DMIC_1_EN_MASK			(0x1 << 15)
#define RT3261_DMIC_1_EN_SFT			15
#define RT3261_DMIC_1_DIS			(0x0 << 15)
#define RT3261_DMIC_1_EN			(0x1 << 15)
#define RT3261_DMIC_2_EN_MASK			(0x1 << 14)
#define RT3261_DMIC_2_EN_SFT			14
#define RT3261_DMIC_2_DIS			(0x0 << 14)
#define RT3261_DMIC_2_EN			(0x1 << 14)
#define RT3261_DMIC_1L_LH_MASK		(0x1 << 13)
#define RT3261_DMIC_1L_LH_SFT			13
#define RT3261_DMIC_1L_LH_FALLING		(0x0 << 13)
#define RT3261_DMIC_1L_LH_RISING		(0x1 << 13)
#define RT3261_DMIC_1R_LH_MASK		(0x1 << 12)
#define RT3261_DMIC_1R_LH_SFT			12
#define RT3261_DMIC_1R_LH_FALLING		(0x0 << 12)
#define RT3261_DMIC_1R_LH_RISING		(0x1 << 12)
#define RT3261_DMIC_1_DP_MASK			(0x1 << 11)
#define RT3261_DMIC_1_DP_SFT			11
#define RT3261_DMIC_1_DP_GPIO3			(0x0 << 11)
#define RT3261_DMIC_1_DP_IN1P			(0x1 << 11)
#define RT3261_DMIC_2_DP_MASK			(0x1 << 10)
#define RT3261_DMIC_2_DP_SFT			10
#define RT3261_DMIC_2_DP_GPIO4			(0x0 << 10)
#define RT3261_DMIC_2_DP_IN1N			(0x1 << 10)
#define RT3261_DMIC_2L_LH_MASK		(0x1 << 9)
#define RT3261_DMIC_2L_LH_SFT			9
#define RT3261_DMIC_2L_LH_FALLING		(0x0 << 9)
#define RT3261_DMIC_2L_LH_RISING		(0x1 << 9)
#define RT3261_DMIC_2R_LH_MASK		(0x1 << 8)
#define RT3261_DMIC_2R_LH_SFT			8
#define RT3261_DMIC_2R_LH_FALLING		(0x0 << 8)
#define RT3261_DMIC_2R_LH_RISING		(0x1 << 8)
#define RT3261_DMIC_CLK_MASK			(0x7 << 5)
#define RT3261_DMIC_CLK_SFT			5

/* Global Clock Control (0x80) */
#define RT3261_SCLK_SRC_MASK			(0x3 << 14)
#define RT3261_SCLK_SRC_SFT			14
#define RT3261_SCLK_SRC_MCLK			(0x0 << 14)
#define RT3261_SCLK_SRC_PLL1			(0x1 << 14)
#define RT3261_SCLK_SRC_RCCLK			(0x2 << 14) /* 15MHz */
#define RT3261_PLL1_SRC_MASK			(0x3 << 12)
#define RT3261_PLL1_SRC_SFT			12
#define RT3261_PLL1_SRC_MCLK			(0x0 << 12)
#define RT3261_PLL1_SRC_BCLK1			(0x1 << 12)
#define RT3261_PLL1_SRC_BCLK2			(0x2 << 12)
#define RT3261_PLL1_SRC_BCLK3			(0x3 << 12)
#define RT3261_PLL1_PD_MASK			(0x1 << 3)
#define RT3261_PLL1_PD_SFT			3
#define RT3261_PLL1_PD_1			(0x0 << 3)
#define RT3261_PLL1_PD_2			(0x1 << 3)

#define RT3261_PLL_INP_MAX			40000000
#define RT3261_PLL_INP_MIN			256000
/* PLL M/N/K Code Control 1 (0x81) */
#define RT3261_PLL_N_MAX			0x1ff
#define RT3261_PLL_N_MASK			(RT3261_PLL_N_MAX << 7)
#define RT3261_PLL_N_SFT			7
#define RT3261_PLL_K_MAX			0x1f
#define RT3261_PLL_K_MASK			(RT3261_PLL_K_MAX)
#define RT3261_PLL_K_SFT			0

/* PLL M/N/K Code Control 2 (0x82) */
#define RT3261_PLL_M_MAX			0xf
#define RT3261_PLL_M_MASK			(RT3261_PLL_M_MAX << 12)
#define RT3261_PLL_M_SFT			12
#define RT3261_PLL_M_BP				(0x1 << 11)
#define RT3261_PLL_M_BP_SFT			11

/* ASRC Control 1 (0x83) */
#define RT3261_STO_T_MASK			(0x1 << 15)
#define RT3261_STO_T_SFT			15
#define RT3261_STO_T_SCLK			(0x0 << 15)
#define RT3261_STO_T_LRCK1			(0x1 << 15)
#define RT3261_M1_T_MASK			(0x1 << 14)
#define RT3261_M1_T_SFT				14
#define RT3261_M1_T_I2S2			(0x0 << 14)
#define RT3261_M1_T_I2S2_D3			(0x1 << 14)
#define RT3261_I2S2_F_MASK			(0x1 << 12)
#define RT3261_I2S2_F_SFT			12
#define RT3261_I2S2_F_I2S2_D2			(0x0 << 12)
#define RT3261_I2S2_F_I2S1_TCLK			(0x1 << 12)
#define RT3261_DMIC_1_M_MASK			(0x1 << 9)
#define RT3261_DMIC_1_M_SFT			9
#define RT3261_DMIC_1_M_NOR			(0x0 << 9)
#define RT3261_DMIC_1_M_ASYN			(0x1 << 9)
#define RT3261_DMIC_2_M_MASK			(0x1 << 8)
#define RT3261_DMIC_2_M_SFT			8
#define RT3261_DMIC_2_M_NOR			(0x0 << 8)
#define RT3261_DMIC_2_M_ASYN			(0x1 << 8)

/* ASRC Control 2 (0x84) */
#define RT3261_MDA_L_M_MASK			(0x1 << 15)
#define RT3261_MDA_L_M_SFT			15
#define RT3261_MDA_L_M_NOR			(0x0 << 15)
#define RT3261_MDA_L_M_ASYN			(0x1 << 15)
#define RT3261_MDA_R_M_MASK			(0x1 << 14)
#define RT3261_MDA_R_M_SFT			14
#define RT3261_MDA_R_M_NOR			(0x0 << 14)
#define RT3261_MDA_R_M_ASYN			(0x1 << 14)
#define RT3261_MAD_L_M_MASK			(0x1 << 13)
#define RT3261_MAD_L_M_SFT			13
#define RT3261_MAD_L_M_NOR			(0x0 << 13)
#define RT3261_MAD_L_M_ASYN			(0x1 << 13)
#define RT3261_MAD_R_M_MASK			(0x1 << 12)
#define RT3261_MAD_R_M_SFT			12
#define RT3261_MAD_R_M_NOR			(0x0 << 12)
#define RT3261_MAD_R_M_ASYN			(0x1 << 12)
#define RT3261_ADC_M_MASK			(0x1 << 11)
#define RT3261_ADC_M_SFT			11
#define RT3261_ADC_M_NOR			(0x0 << 11)
#define RT3261_ADC_M_ASYN			(0x1 << 11)
#define RT3261_STO_DAC_M_MASK			(0x1 << 5)
#define RT3261_STO_DAC_M_SFT			5
#define RT3261_STO_DAC_M_NOR			(0x0 << 5)
#define RT3261_STO_DAC_M_ASYN			(0x1 << 5)
#define RT3261_I2S1_R_D_MASK			(0x1 << 4)
#define RT3261_I2S1_R_D_SFT			4
#define RT3261_I2S1_R_D_DIS			(0x0 << 4)
#define RT3261_I2S1_R_D_EN			(0x1 << 4)
#define RT3261_I2S2_R_D_MASK			(0x1 << 3)
#define RT3261_I2S2_R_D_SFT			3
#define RT3261_I2S2_R_D_DIS			(0x0 << 3)
#define RT3261_I2S2_R_D_EN			(0x1 << 3)
#define RT3261_PRE_SCLK_MASK			(0x3)
#define RT3261_PRE_SCLK_SFT			0
#define RT3261_PRE_SCLK_512			(0x0)
#define RT3261_PRE_SCLK_1024			(0x1)
#define RT3261_PRE_SCLK_2048			(0x2)

/* ASRC Control 3 (0x85) */
#define RT3261_I2S1_RATE_MASK			(0xf << 12)
#define RT3261_I2S1_RATE_SFT			12
#define RT3261_I2S2_RATE_MASK			(0xf << 8)
#define RT3261_I2S2_RATE_SFT			8

/* ASRC Control 4 (0x89) */
#define RT3261_I2S1_PD_MASK			(0x7 << 12)
#define RT3261_I2S1_PD_SFT			12
#define RT3261_I2S2_PD_MASK			(0x7 << 8)
#define RT3261_I2S2_PD_SFT			8

/* HPOUT Over Current Detection (0x8b) */
#define RT3261_HP_OVCD_MASK			(0x1 << 10)
#define RT3261_HP_OVCD_SFT			10
#define RT3261_HP_OVCD_DIS			(0x0 << 10)
#define RT3261_HP_OVCD_EN			(0x1 << 10)
#define RT3261_HP_OC_TH_MASK			(0x3 << 8)
#define RT3261_HP_OC_TH_SFT			8
#define RT3261_HP_OC_TH_90			(0x0 << 8)
#define RT3261_HP_OC_TH_105			(0x1 << 8)
#define RT3261_HP_OC_TH_120			(0x2 << 8)
#define RT3261_HP_OC_TH_135			(0x3 << 8)

/* Class D Over Current Control (0x8c) */
#define RT3261_CLSD_OC_MASK			(0x1 << 9)
#define RT3261_CLSD_OC_SFT			9
#define RT3261_CLSD_OC_PU			(0x0 << 9)
#define RT3261_CLSD_OC_PD			(0x1 << 9)
#define RT3261_AUTO_PD_MASK			(0x1 << 8)
#define RT3261_AUTO_PD_SFT			8
#define RT3261_AUTO_PD_DIS			(0x0 << 8)
#define RT3261_AUTO_PD_EN			(0x1 << 8)
#define RT3261_CLSD_OC_TH_MASK		(0x3f)
#define RT3261_CLSD_OC_TH_SFT			0

/* Class D Output Control (0x8d) */
#define RT3261_CLSD_RATIO_MASK		(0xf << 12)
#define RT3261_CLSD_RATIO_SFT			12
#define RT3261_CLSD_OM_MASK			(0x1 << 11)
#define RT3261_CLSD_OM_SFT			11
#define RT3261_CLSD_OM_MONO			(0x0 << 11)
#define RT3261_CLSD_OM_STO			(0x1 << 11)
#define RT3261_CLSD_SCH_MASK			(0x1 << 10)
#define RT3261_CLSD_SCH_SFT			10
#define RT3261_CLSD_SCH_L			(0x0 << 10)
#define RT3261_CLSD_SCH_S			(0x1 << 10)

/* Depop Mode Control 1 (0x8e) */
#define RT3261_SMT_TRIG_MASK			(0x1 << 15)
#define RT3261_SMT_TRIG_SFT			15
#define RT3261_SMT_TRIG_DIS			(0x0 << 15)
#define RT3261_SMT_TRIG_EN			(0x1 << 15)
#define RT3261_HP_L_SMT_MASK			(0x1 << 9)
#define RT3261_HP_L_SMT_SFT			9
#define RT3261_HP_L_SMT_DIS			(0x0 << 9)
#define RT3261_HP_L_SMT_EN			(0x1 << 9)
#define RT3261_HP_R_SMT_MASK			(0x1 << 8)
#define RT3261_HP_R_SMT_SFT			8
#define RT3261_HP_R_SMT_DIS			(0x0 << 8)
#define RT3261_HP_R_SMT_EN			(0x1 << 8)
#define RT3261_HP_CD_PD_MASK			(0x1 << 7)
#define RT3261_HP_CD_PD_SFT			7
#define RT3261_HP_CD_PD_DIS			(0x0 << 7)
#define RT3261_HP_CD_PD_EN			(0x1 << 7)
#define RT3261_RSTN_MASK			(0x1 << 6)
#define RT3261_RSTN_SFT				6
#define RT3261_RSTN_DIS				(0x0 << 6)
#define RT3261_RSTN_EN				(0x1 << 6)
#define RT3261_RSTP_MASK			(0x1 << 5)
#define RT3261_RSTP_SFT				5
#define RT3261_RSTP_DIS				(0x0 << 5)
#define RT3261_RSTP_EN				(0x1 << 5)
#define RT3261_HP_CO_MASK			(0x1 << 4)
#define RT3261_HP_CO_SFT			4
#define RT3261_HP_CO_DIS			(0x0 << 4)
#define RT3261_HP_CO_EN			(0x1 << 4)
#define RT3261_HP_CP_MASK			(0x1 << 3)
#define RT3261_HP_CP_SFT			3
#define RT3261_HP_CP_PD			(0x0 << 3)
#define RT3261_HP_CP_PU			(0x1 << 3)
#define RT3261_HP_SG_MASK			(0x1 << 2)
#define RT3261_HP_SG_SFT			2
#define RT3261_HP_SG_DIS			(0x0 << 2)
#define RT3261_HP_SG_EN			(0x1 << 2)
#define RT3261_HP_DP_MASK			(0x1 << 1)
#define RT3261_HP_DP_SFT			1
#define RT3261_HP_DP_PD			(0x0 << 1)
#define RT3261_HP_DP_PU			(0x1 << 1)
#define RT3261_HP_CB_MASK			(0x1)
#define RT3261_HP_CB_SFT			0
#define RT3261_HP_CB_PD			(0x0)
#define RT3261_HP_CB_PU			(0x1)

/* Depop Mode Control 2 (0x8f) */
#define RT3261_DEPOP_MASK			(0x1 << 13)
#define RT3261_DEPOP_SFT			13
#define RT3261_DEPOP_AUTO			(0x0 << 13)
#define RT3261_DEPOP_MAN			(0x1 << 13)
#define RT3261_RAMP_MASK			(0x1 << 12)
#define RT3261_RAMP_SFT			12
#define RT3261_RAMP_DIS				(0x0 << 12)
#define RT3261_RAMP_EN				(0x1 << 12)
#define RT3261_BPS_MASK			(0x1 << 11)
#define RT3261_BPS_SFT				11
#define RT3261_BPS_DIS				(0x0 << 11)
#define RT3261_BPS_EN				(0x1 << 11)
#define RT3261_FAST_UPDN_MASK			(0x1 << 10)
#define RT3261_FAST_UPDN_SFT			10
#define RT3261_FAST_UPDN_DIS			(0x0 << 10)
#define RT3261_FAST_UPDN_EN			(0x1 << 10)
#define RT3261_MRES_MASK			(0x3 << 8)
#define RT3261_MRES_SFT				8
#define RT3261_MRES_15MO			(0x0 << 8)
#define RT3261_MRES_25MO			(0x1 << 8)
#define RT3261_MRES_35MO			(0x2 << 8)
#define RT3261_MRES_45MO			(0x3 << 8)
#define RT3261_VLO_MASK			(0x1 << 7)
#define RT3261_VLO_SFT				7
#define RT3261_VLO_3V				(0x0 << 7)
#define RT3261_VLO_32V				(0x1 << 7)
#define RT3261_DIG_DP_MASK			(0x1 << 6)
#define RT3261_DIG_DP_SFT			6
#define RT3261_DIG_DP_DIS			(0x0 << 6)
#define RT3261_DIG_DP_EN			(0x1 << 6)
#define RT3261_DP_TH_MASK			(0x3 << 4)
#define RT3261_DP_TH_SFT			4

/* Depop Mode Control 3 (0x90) */
#define RT3261_CP_SYS_MASK			(0x7 << 12)
#define RT3261_CP_SYS_SFT			12
#define RT3261_CP_FQ1_MASK			(0x7 << 8)
#define RT3261_CP_FQ1_SFT			8
#define RT3261_CP_FQ2_MASK			(0x7 << 4)
#define RT3261_CP_FQ2_SFT			4
#define RT3261_CP_FQ3_MASK			(0x7)
#define RT3261_CP_FQ3_SFT			0
#define RT3261_CP_FQ_1_5_KHZ			0
#define RT3261_CP_FQ_3_KHZ			1
#define RT3261_CP_FQ_6_KHZ			2
#define RT3261_CP_FQ_12_KHZ			3
#define RT3261_CP_FQ_24_KHZ			4
#define RT3261_CP_FQ_48_KHZ			5
#define RT3261_CP_FQ_96_KHZ			6
#define RT3261_CP_FQ_192_KHZ			7

/* HPOUT charge pump (0x91) */
#define RT3261_OSW_L_MASK			(0x1 << 11)
#define RT3261_OSW_L_SFT			11
#define RT3261_OSW_L_DIS			(0x0 << 11)
#define RT3261_OSW_L_EN			(0x1 << 11)
#define RT3261_OSW_R_MASK			(0x1 << 10)
#define RT3261_OSW_R_SFT			10
#define RT3261_OSW_R_DIS			(0x0 << 10)
#define RT3261_OSW_R_EN			(0x1 << 10)
#define RT3261_PM_HP_MASK			(0x3 << 8)
#define RT3261_PM_HP_SFT			8
#define RT3261_PM_HP_LV			(0x0 << 8)
#define RT3261_PM_HP_MV			(0x1 << 8)
#define RT3261_PM_HP_HV			(0x2 << 8)
#define RT3261_IB_HP_MASK			(0x3 << 6)
#define RT3261_IB_HP_SFT			6
#define RT3261_IB_HP_125IL			(0x0 << 6)
#define RT3261_IB_HP_25IL			(0x1 << 6)
#define RT3261_IB_HP_5IL			(0x2 << 6)
#define RT3261_IB_HP_1IL			(0x3 << 6)

/* PV detection and SPK gain control (0x92) */
#define RT3261_PVDD_DET_MASK			(0x1 << 15)
#define RT3261_PVDD_DET_SFT			15
#define RT3261_PVDD_DET_DIS			(0x0 << 15)
#define RT3261_PVDD_DET_EN			(0x1 << 15)
#define RT3261_SPK_AG_MASK			(0x1 << 14)
#define RT3261_SPK_AG_SFT			14
#define RT3261_SPK_AG_DIS			(0x0 << 14)
#define RT3261_SPK_AG_EN			(0x1 << 14)

/* Micbias Control (0x93) */
#define RT3261_MIC1_BS_MASK			(0x1 << 15)
#define RT3261_MIC1_BS_SFT			15
#define RT3261_MIC1_BS_9AV			(0x0 << 15)
#define RT3261_MIC1_BS_75AV			(0x1 << 15)
#define RT3261_MIC2_BS_MASK			(0x1 << 14)
#define RT3261_MIC2_BS_SFT			14
#define RT3261_MIC2_BS_9AV			(0x0 << 14)
#define RT3261_MIC2_BS_75AV			(0x1 << 14)
#define RT3261_MIC1_CLK_MASK			(0x1 << 13)
#define RT3261_MIC1_CLK_SFT			13
#define RT3261_MIC1_CLK_DIS			(0x0 << 13)
#define RT3261_MIC1_CLK_EN			(0x1 << 13)
#define RT3261_MIC2_CLK_MASK			(0x1 << 12)
#define RT3261_MIC2_CLK_SFT			12
#define RT3261_MIC2_CLK_DIS			(0x0 << 12)
#define RT3261_MIC2_CLK_EN			(0x1 << 12)
#define RT3261_MIC1_OVCD_MASK			(0x1 << 11)
#define RT3261_MIC1_OVCD_SFT			11
#define RT3261_MIC1_OVCD_DIS			(0x0 << 11)
#define RT3261_MIC1_OVCD_EN			(0x1 << 11)
#define RT3261_MIC1_OVTH_MASK			(0x3 << 9)
#define RT3261_MIC1_OVTH_SFT			9
#define RT3261_MIC1_OVTH_600UA		(0x0 << 9)
#define RT3261_MIC1_OVTH_1500UA		(0x1 << 9)
#define RT3261_MIC1_OVTH_2000UA		(0x2 << 9)
#define RT3261_MIC2_OVCD_MASK			(0x1 << 8)
#define RT3261_MIC2_OVCD_SFT			8
#define RT3261_MIC2_OVCD_DIS			(0x0 << 8)
#define RT3261_MIC2_OVCD_EN			(0x1 << 8)
#define RT3261_MIC2_OVTH_MASK			(0x3 << 6)
#define RT3261_MIC2_OVTH_SFT			6
#define RT3261_MIC2_OVTH_600UA		(0x0 << 6)
#define RT3261_MIC2_OVTH_1500UA		(0x1 << 6)
#define RT3261_MIC2_OVTH_2000UA		(0x2 << 6)
#define RT3261_PWR_MB_MASK			(0x1 << 5)
#define RT3261_PWR_MB_SFT			5
#define RT3261_PWR_MB_PD			(0x0 << 5)
#define RT3261_PWR_MB_PU			(0x1 << 5)
#define RT3261_PWR_CLK25M_MASK		(0x1 << 4)
#define RT3261_PWR_CLK25M_SFT			4
#define RT3261_PWR_CLK25M_PD			(0x0 << 4)
#define RT3261_PWR_CLK25M_PU			(0x1 << 4)

/* EQ Control 1 (0xb0) */
#define RT3261_EQ_SRC_MASK			(0x1 << 15)
#define RT3261_EQ_SRC_SFT			15
#define RT3261_EQ_SRC_DAC			(0x0 << 15)
#define RT3261_EQ_SRC_ADC			(0x1 << 15)
#define RT3261_EQ_UPD				(0x1 << 14)
#define RT3261_EQ_UPD_BIT			14
#define RT3261_EQ_CD_MASK			(0x1 << 13)
#define RT3261_EQ_CD_SFT			13
#define RT3261_EQ_CD_DIS			(0x0 << 13)
#define RT3261_EQ_CD_EN			(0x1 << 13)
#define RT3261_EQ_DITH_MASK			(0x3 << 8)
#define RT3261_EQ_DITH_SFT			8
#define RT3261_EQ_DITH_NOR			(0x0 << 8)
#define RT3261_EQ_DITH_LSB			(0x1 << 8)
#define RT3261_EQ_DITH_LSB_1			(0x2 << 8)
#define RT3261_EQ_DITH_LSB_2			(0x3 << 8)

/* EQ Control 2 (0xb1) */
#define RT3261_EQ_HPF1_M_MASK			(0x1 << 8)
#define RT3261_EQ_HPF1_M_SFT			8
#define RT3261_EQ_HPF1_M_HI			(0x0 << 8)
#define RT3261_EQ_HPF1_M_1ST			(0x1 << 8)
#define RT3261_EQ_LPF1_M_MASK			(0x1 << 7)
#define RT3261_EQ_LPF1_M_SFT			7
#define RT3261_EQ_LPF1_M_LO			(0x0 << 7)
#define RT3261_EQ_LPF1_M_1ST			(0x1 << 7)
#define RT3261_EQ_HPF2_MASK			(0x1 << 6)
#define RT3261_EQ_HPF2_SFT			6
#define RT3261_EQ_HPF2_DIS			(0x0 << 6)
#define RT3261_EQ_HPF2_EN			(0x1 << 6)
#define RT3261_EQ_HPF1_MASK			(0x1 << 5)
#define RT3261_EQ_HPF1_SFT			5
#define RT3261_EQ_HPF1_DIS			(0x0 << 5)
#define RT3261_EQ_HPF1_EN			(0x1 << 5)
#define RT3261_EQ_BPF4_MASK			(0x1 << 4)
#define RT3261_EQ_BPF4_SFT			4
#define RT3261_EQ_BPF4_DIS			(0x0 << 4)
#define RT3261_EQ_BPF4_EN			(0x1 << 4)
#define RT3261_EQ_BPF3_MASK			(0x1 << 3)
#define RT3261_EQ_BPF3_SFT			3
#define RT3261_EQ_BPF3_DIS			(0x0 << 3)
#define RT3261_EQ_BPF3_EN			(0x1 << 3)
#define RT3261_EQ_BPF2_MASK			(0x1 << 2)
#define RT3261_EQ_BPF2_SFT			2
#define RT3261_EQ_BPF2_DIS			(0x0 << 2)
#define RT3261_EQ_BPF2_EN			(0x1 << 2)
#define RT3261_EQ_BPF1_MASK			(0x1 << 1)
#define RT3261_EQ_BPF1_SFT			1
#define RT3261_EQ_BPF1_DIS			(0x0 << 1)
#define RT3261_EQ_BPF1_EN			(0x1 << 1)
#define RT3261_EQ_LPF_MASK			(0x1)
#define RT3261_EQ_LPF_SFT			0
#define RT3261_EQ_LPF_DIS			(0x0)
#define RT3261_EQ_LPF_EN			(0x1)
#define RT3261_EQ_CTRL_MASK			(0x7f)

/* Memory Test (0xb2) */
#define RT3261_MT_MASK				(0x1 << 15)
#define RT3261_MT_SFT				15
#define RT3261_MT_DIS				(0x0 << 15)
#define RT3261_MT_EN				(0x1 << 15)

/* DRC/AGC Control 1 (0xb4) */
#define RT3261_DRC_AGC_P_MASK			(0x1 << 15)
#define RT3261_DRC_AGC_P_SFT			15
#define RT3261_DRC_AGC_P_DAC			(0x0 << 15)
#define RT3261_DRC_AGC_P_ADC			(0x1 << 15)
#define RT3261_DRC_AGC_MASK			(0x1 << 14)
#define RT3261_DRC_AGC_SFT			14
#define RT3261_DRC_AGC_DIS			(0x0 << 14)
#define RT3261_DRC_AGC_EN			(0x1 << 14)
#define RT3261_DRC_AGC_UPD			(0x1 << 13)
#define RT3261_DRC_AGC_UPD_BIT		13
#define RT3261_DRC_AGC_AR_MASK		(0x1f << 8)
#define RT3261_DRC_AGC_AR_SFT			8
#define RT3261_DRC_AGC_R_MASK			(0x7 << 5)
#define RT3261_DRC_AGC_R_SFT			5
#define RT3261_DRC_AGC_R_48K			(0x1 << 5)
#define RT3261_DRC_AGC_R_96K			(0x2 << 5)
#define RT3261_DRC_AGC_R_192K			(0x3 << 5)
#define RT3261_DRC_AGC_R_441K			(0x5 << 5)
#define RT3261_DRC_AGC_R_882K			(0x6 << 5)
#define RT3261_DRC_AGC_R_1764K		(0x7 << 5)
#define RT3261_DRC_AGC_RC_MASK		(0x1f)
#define RT3261_DRC_AGC_RC_SFT			0

/* DRC/AGC Control 2 (0xb5) */
#define RT3261_DRC_AGC_POB_MASK		(0x3f << 8)
#define RT3261_DRC_AGC_POB_SFT		8
#define RT3261_DRC_AGC_CP_MASK		(0x1 << 7)
#define RT3261_DRC_AGC_CP_SFT			7
#define RT3261_DRC_AGC_CP_DIS			(0x0 << 7)
#define RT3261_DRC_AGC_CP_EN			(0x1 << 7)
#define RT3261_DRC_AGC_CPR_MASK		(0x3 << 5)
#define RT3261_DRC_AGC_CPR_SFT		5
#define RT3261_DRC_AGC_CPR_1_1		(0x0 << 5)
#define RT3261_DRC_AGC_CPR_1_2		(0x1 << 5)
#define RT3261_DRC_AGC_CPR_1_3		(0x2 << 5)
#define RT3261_DRC_AGC_CPR_1_4		(0x3 << 5)
#define RT3261_DRC_AGC_PRB_MASK		(0x1f)
#define RT3261_DRC_AGC_PRB_SFT		0

/* DRC/AGC Control 3 (0xb6) */
#define RT3261_DRC_AGC_NGB_MASK		(0xf << 12)
#define RT3261_DRC_AGC_NGB_SFT		12
#define RT3261_DRC_AGC_TAR_MASK		(0x1f << 7)
#define RT3261_DRC_AGC_TAR_SFT		7
#define RT3261_DRC_AGC_NG_MASK		(0x1 << 6)
#define RT3261_DRC_AGC_NG_SFT			6
#define RT3261_DRC_AGC_NG_DIS			(0x0 << 6)
#define RT3261_DRC_AGC_NG_EN			(0x1 << 6)
#define RT3261_DRC_AGC_NGH_MASK		(0x1 << 5)
#define RT3261_DRC_AGC_NGH_SFT		5
#define RT3261_DRC_AGC_NGH_DIS		(0x0 << 5)
#define RT3261_DRC_AGC_NGH_EN			(0x1 << 5)
#define RT3261_DRC_AGC_NGT_MASK		(0x1f)
#define RT3261_DRC_AGC_NGT_SFT		0

/* ANC Control 1 (0xb8) */
#define RT3261_ANC_M_MASK			(0x1 << 15)
#define RT3261_ANC_M_SFT			15
#define RT3261_ANC_M_NOR			(0x0 << 15)
#define RT3261_ANC_M_REV			(0x1 << 15)
#define RT3261_ANC_MASK			(0x1 << 14)
#define RT3261_ANC_SFT				14
#define RT3261_ANC_DIS				(0x0 << 14)
#define RT3261_ANC_EN				(0x1 << 14)
#define RT3261_ANC_MD_MASK			(0x3 << 12)
#define RT3261_ANC_MD_SFT			12
#define RT3261_ANC_MD_DIS			(0x0 << 12)
#define RT3261_ANC_MD_67MS			(0x1 << 12)
#define RT3261_ANC_MD_267MS			(0x2 << 12)
#define RT3261_ANC_MD_1067MS			(0x3 << 12)
#define RT3261_ANC_SN_MASK			(0x1 << 11)
#define RT3261_ANC_SN_SFT			11
#define RT3261_ANC_SN_DIS			(0x0 << 11)
#define RT3261_ANC_SN_EN			(0x1 << 11)
#define RT3261_ANC_CLK_MASK			(0x1 << 10)
#define RT3261_ANC_CLK_SFT			10
#define RT3261_ANC_CLK_ANC			(0x0 << 10)
#define RT3261_ANC_CLK_REG			(0x1 << 10)
#define RT3261_ANC_ZCD_MASK			(0x3 << 8)
#define RT3261_ANC_ZCD_SFT			8
#define RT3261_ANC_ZCD_DIS			(0x0 << 8)
#define RT3261_ANC_ZCD_T1			(0x1 << 8)
#define RT3261_ANC_ZCD_T2			(0x2 << 8)
#define RT3261_ANC_ZCD_WT			(0x3 << 8)
#define RT3261_ANC_CS_MASK			(0x1 << 7)
#define RT3261_ANC_CS_SFT			7
#define RT3261_ANC_CS_DIS			(0x0 << 7)
#define RT3261_ANC_CS_EN			(0x1 << 7)
#define RT3261_ANC_SW_MASK			(0x1 << 6)
#define RT3261_ANC_SW_SFT			6
#define RT3261_ANC_SW_NOR			(0x0 << 6)
#define RT3261_ANC_SW_AUTO			(0x1 << 6)
#define RT3261_ANC_CO_L_MASK			(0x3f)
#define RT3261_ANC_CO_L_SFT			0

/* ANC Control 2 (0xb6) */
#define RT3261_ANC_FG_R_MASK			(0xf << 12)
#define RT3261_ANC_FG_R_SFT			12
#define RT3261_ANC_FG_L_MASK			(0xf << 8)
#define RT3261_ANC_FG_L_SFT			8
#define RT3261_ANC_CG_R_MASK			(0xf << 4)
#define RT3261_ANC_CG_R_SFT			4
#define RT3261_ANC_CG_L_MASK			(0xf)
#define RT3261_ANC_CG_L_SFT			0

/* ANC Control 3 (0xb6) */
#define RT3261_ANC_CD_MASK			(0x1 << 6)
#define RT3261_ANC_CD_SFT			6
#define RT3261_ANC_CD_BOTH			(0x0 << 6)
#define RT3261_ANC_CD_IND			(0x1 << 6)
#define RT3261_ANC_CO_R_MASK			(0x3f)
#define RT3261_ANC_CO_R_SFT			0

/* Jack Detect Control (0xbb) */
#define RT3261_JD_MASK				(0x7 << 13)
#define RT3261_JD_SFT				13
#define RT3261_JD_DIS				(0x0 << 13)
#define RT3261_JD_GPIO1				(0x1 << 13)
#define RT3261_JD_JD1_IN4P			(0x2 << 13)
#define RT3261_JD_JD2_IN4N			(0x3 << 13)
#define RT3261_JD_GPIO2				(0x4 << 13)
#define RT3261_JD_GPIO3				(0x5 << 13)
#define RT3261_JD_GPIO4				(0x6 << 13)
#define RT3261_JD_HP_MASK			(0x1 << 11)
#define RT3261_JD_HP_SFT			11
#define RT3261_JD_HP_DIS			(0x0 << 11)
#define RT3261_JD_HP_EN				(0x1 << 11)
#define RT3261_JD_HP_TRG_MASK			(0x1 << 10)
#define RT3261_JD_HP_TRG_SFT			10
#define RT3261_JD_HP_TRG_LO			(0x0 << 10)
#define RT3261_JD_HP_TRG_HI			(0x1 << 10)
#define RT3261_JD_SPL_MASK			(0x1 << 9)
#define RT3261_JD_SPL_SFT			9
#define RT3261_JD_SPL_DIS			(0x0 << 9)
#define RT3261_JD_SPL_EN			(0x1 << 9)
#define RT3261_JD_SPL_TRG_MASK			(0x1 << 8)
#define RT3261_JD_SPL_TRG_SFT			8
#define RT3261_JD_SPL_TRG_LO			(0x0 << 8)
#define RT3261_JD_SPL_TRG_HI			(0x1 << 8)
#define RT3261_JD_SPR_MASK			(0x1 << 7)
#define RT3261_JD_SPR_SFT			7
#define RT3261_JD_SPR_DIS			(0x0 << 7)
#define RT3261_JD_SPR_EN			(0x1 << 7)
#define RT3261_JD_SPR_TRG_MASK			(0x1 << 6)
#define RT3261_JD_SPR_TRG_SFT			6
#define RT3261_JD_SPR_TRG_LO			(0x0 << 6)
#define RT3261_JD_SPR_TRG_HI			(0x1 << 6)
#define RT3261_JD_MO_MASK			(0x1 << 5)
#define RT3261_JD_MO_SFT			5
#define RT3261_JD_MO_DIS			(0x0 << 5)
#define RT3261_JD_MO_EN			(0x1 << 5)
#define RT3261_JD_MO_TRG_MASK			(0x1 << 4)
#define RT3261_JD_MO_TRG_SFT			4
#define RT3261_JD_MO_TRG_LO			(0x0 << 4)
#define RT3261_JD_MO_TRG_HI			(0x1 << 4)
#define RT3261_JD_LO_MASK			(0x1 << 3)
#define RT3261_JD_LO_SFT			3
#define RT3261_JD_LO_DIS			(0x0 << 3)
#define RT3261_JD_LO_EN				(0x1 << 3)
#define RT3261_JD_LO_TRG_MASK			(0x1 << 2)
#define RT3261_JD_LO_TRG_SFT			2
#define RT3261_JD_LO_TRG_LO			(0x0 << 2)
#define RT3261_JD_LO_TRG_HI			(0x1 << 2)
#define RT3261_JD1_IN4P_MASK			(0x1 << 1)
#define RT3261_JD1_IN4P_SFT			1
#define RT3261_JD1_IN4P_DIS			(0x0 << 1)
#define RT3261_JD1_IN4P_EN			(0x1 << 1)
#define RT3261_JD2_IN4N_MASK			(0x1)
#define RT3261_JD2_IN4N_SFT			0
#define RT3261_JD2_IN4N_DIS			(0x0)
#define RT3261_JD2_IN4N_EN			(0x1)

/* Jack detect for ANC (0xbc) */
#define RT3261_ANC_DET_MASK			(0x3 << 4)
#define RT3261_ANC_DET_SFT			4
#define RT3261_ANC_DET_DIS			(0x0 << 4)
#define RT3261_ANC_DET_MB1			(0x1 << 4)
#define RT3261_ANC_DET_MB2			(0x2 << 4)
#define RT3261_ANC_DET_JD			(0x3 << 4)
#define RT3261_AD_TRG_MASK			(0x1 << 3)
#define RT3261_AD_TRG_SFT			3
#define RT3261_AD_TRG_LO			(0x0 << 3)
#define RT3261_AD_TRG_HI			(0x1 << 3)
#define RT3261_ANCM_DET_MASK			(0x3 << 4)
#define RT3261_ANCM_DET_SFT			4
#define RT3261_ANCM_DET_DIS			(0x0 << 4)
#define RT3261_ANCM_DET_MB1			(0x1 << 4)
#define RT3261_ANCM_DET_MB2			(0x2 << 4)
#define RT3261_ANCM_DET_JD			(0x3 << 4)
#define RT3261_AMD_TRG_MASK			(0x1 << 3)
#define RT3261_AMD_TRG_SFT			3
#define RT3261_AMD_TRG_LO			(0x0 << 3)
#define RT3261_AMD_TRG_HI			(0x1 << 3)

/* IRQ Control 1 (0xbd) */
#define RT3261_IRQ_JD_MASK			(0x1 << 15)
#define RT3261_IRQ_JD_SFT			15
#define RT3261_IRQ_JD_BP			(0x0 << 15)
#define RT3261_IRQ_JD_NOR			(0x1 << 15)
#define RT3261_IRQ_OT_MASK			(0x1 << 14)
#define RT3261_IRQ_OT_SFT			14
#define RT3261_IRQ_OT_BP			(0x0 << 14)
#define RT3261_IRQ_OT_NOR			(0x1 << 14)
#define RT3261_JD_STKY_MASK			(0x1 << 13)
#define RT3261_JD_STKY_SFT			13
#define RT3261_JD_STKY_DIS			(0x0 << 13)
#define RT3261_JD_STKY_EN			(0x1 << 13)
#define RT3261_OT_STKY_MASK			(0x1 << 12)
#define RT3261_OT_STKY_SFT			12
#define RT3261_OT_STKY_DIS			(0x0 << 12)
#define RT3261_OT_STKY_EN			(0x1 << 12)
#define RT3261_JD_P_MASK			(0x1 << 11)
#define RT3261_JD_P_SFT				11
#define RT3261_JD_P_NOR				(0x0 << 11)
#define RT3261_JD_P_INV				(0x1 << 11)
#define RT3261_OT_P_MASK			(0x1 << 10)
#define RT3261_OT_P_SFT				10
#define RT3261_OT_P_NOR			(0x0 << 10)
#define RT3261_OT_P_INV				(0x1 << 10)

/* IRQ Control 2 (0xbe) */
#define RT3261_IRQ_MB1_OC_MASK		(0x1 << 15)
#define RT3261_IRQ_MB1_OC_SFT			15
#define RT3261_IRQ_MB1_OC_BP			(0x0 << 15)
#define RT3261_IRQ_MB1_OC_NOR			(0x1 << 15)
#define RT3261_IRQ_MB2_OC_MASK		(0x1 << 14)
#define RT3261_IRQ_MB2_OC_SFT			14
#define RT3261_IRQ_MB2_OC_BP			(0x0 << 14)
#define RT3261_IRQ_MB2_OC_NOR			(0x1 << 14)
#define RT3261_MB1_OC_STKY_MASK		(0x1 << 11)
#define RT3261_MB1_OC_STKY_SFT		11
#define RT3261_MB1_OC_STKY_DIS		(0x0 << 11)
#define RT3261_MB1_OC_STKY_EN			(0x1 << 11)
#define RT3261_MB2_OC_STKY_MASK		(0x1 << 10)
#define RT3261_MB2_OC_STKY_SFT		10
#define RT3261_MB2_OC_STKY_DIS		(0x0 << 10)
#define RT3261_MB2_OC_STKY_EN			(0x1 << 10)
#define RT3261_MB1_OC_P_MASK			(0x1 << 7)
#define RT3261_MB1_OC_P_SFT			7
#define RT3261_MB1_OC_P_NOR			(0x0 << 7)
#define RT3261_MB1_OC_P_INV			(0x1 << 7)
#define RT3261_MB2_OC_P_MASK			(0x1 << 6)
#define RT3261_MB2_OC_P_SFT			6
#define RT3261_MB2_OC_P_NOR			(0x0 << 6)
#define RT3261_MB2_OC_P_INV			(0x1 << 6)
#define RT3261_MB1_OC_CLR			(0x1 << 3)
#define RT3261_MB1_OC_CLR_SFT			3
#define RT3261_MB2_OC_CLR			(0x1 << 2)
#define RT3261_MB2_OC_CLR_SFT			2

/* GPIO Control 1 (0xc0) */
#define RT3261_GP1_PIN_MASK			(0x1 << 15)
#define RT3261_GP1_PIN_SFT			15
#define RT3261_GP1_PIN_GPIO1			(0x0 << 15)
#define RT3261_GP1_PIN_IRQ			(0x1 << 15)
#define RT3261_GP2_PIN_MASK			(0x1 << 14)
#define RT3261_GP2_PIN_SFT			14
#define RT3261_GP2_PIN_GPIO2			(0x0 << 14)
#define RT3261_GP2_PIN_DMIC1_SCL		(0x1 << 14)
#define RT3261_GP3_PIN_MASK			(0x3 << 12)
#define RT3261_GP3_PIN_SFT			12
#define RT3261_GP3_PIN_GPIO3			(0x0 << 12)
#define RT3261_GP3_PIN_DMIC1_SDA		(0x1 << 12)
#define RT3261_GP3_PIN_IRQ			(0x2 << 12)
#define RT3261_GP4_PIN_MASK			(0x1 << 11)
#define RT3261_GP4_PIN_SFT			11
#define RT3261_GP4_PIN_GPIO4			(0x0 << 11)
#define RT3261_GP4_PIN_DMIC2_SDA		(0x1 << 11)
#define RT3261_DP_SIG_MASK			(0x1 << 10)
#define RT3261_DP_SIG_SFT			10
#define RT3261_DP_SIG_TEST			(0x0 << 10)
#define RT3261_DP_SIG_AP			(0x1 << 10)
#define RT3261_GPIO_M_MASK			(0x1 << 9)
#define RT3261_GPIO_M_SFT			9
#define RT3261_GPIO_M_FLT			(0x0 << 9)
#define RT3261_GPIO_M_PH			(0x1 << 9)

/* GPIO Control 3 (0xc2) */
#define RT3261_GP4_PF_MASK			(0x1 << 11)
#define RT3261_GP4_PF_SFT			11
#define RT3261_GP4_PF_IN			(0x0 << 11)
#define RT3261_GP4_PF_OUT			(0x1 << 11)
#define RT3261_GP4_OUT_MASK			(0x1 << 10)
#define RT3261_GP4_OUT_SFT			10
#define RT3261_GP4_OUT_LO			(0x0 << 10)
#define RT3261_GP4_OUT_HI			(0x1 << 10)
#define RT3261_GP4_P_MASK			(0x1 << 9)
#define RT3261_GP4_P_SFT			9
#define RT3261_GP4_P_NOR			(0x0 << 9)
#define RT3261_GP4_P_INV			(0x1 << 9)
#define RT3261_GP3_PF_MASK			(0x1 << 8)
#define RT3261_GP3_PF_SFT			8
#define RT3261_GP3_PF_IN			(0x0 << 8)
#define RT3261_GP3_PF_OUT			(0x1 << 8)
#define RT3261_GP3_OUT_MASK			(0x1 << 7)
#define RT3261_GP3_OUT_SFT			7
#define RT3261_GP3_OUT_LO			(0x0 << 7)
#define RT3261_GP3_OUT_HI			(0x1 << 7)
#define RT3261_GP3_P_MASK			(0x1 << 6)
#define RT3261_GP3_P_SFT			6
#define RT3261_GP3_P_NOR			(0x0 << 6)
#define RT3261_GP3_P_INV			(0x1 << 6)
#define RT3261_GP2_PF_MASK			(0x1 << 5)
#define RT3261_GP2_PF_SFT			5
#define RT3261_GP2_PF_IN			(0x0 << 5)
#define RT3261_GP2_PF_OUT			(0x1 << 5)
#define RT3261_GP2_OUT_MASK			(0x1 << 4)
#define RT3261_GP2_OUT_SFT			4
#define RT3261_GP2_OUT_LO			(0x0 << 4)
#define RT3261_GP2_OUT_HI			(0x1 << 4)
#define RT3261_GP2_P_MASK			(0x1 << 3)
#define RT3261_GP2_P_SFT			3
#define RT3261_GP2_P_NOR			(0x0 << 3)
#define RT3261_GP2_P_INV			(0x1 << 3)
#define RT3261_GP1_PF_MASK			(0x1 << 2)
#define RT3261_GP1_PF_SFT			2
#define RT3261_GP1_PF_IN			(0x0 << 2)
#define RT3261_GP1_PF_OUT			(0x1 << 2)
#define RT3261_GP1_OUT_MASK			(0x1 << 1)
#define RT3261_GP1_OUT_SFT			1
#define RT3261_GP1_OUT_LO			(0x0 << 1)
#define RT3261_GP1_OUT_HI			(0x1 << 1)
#define RT3261_GP1_P_MASK			(0x1)
#define RT3261_GP1_P_SFT			0
#define RT3261_GP1_P_NOR			(0x0)
#define RT3261_GP1_P_INV			(0x1)

/* FM34-500 Register Control 1 (0xc4) */
#define RT3261_DSP_ADD_SFT			0

/* FM34-500 Register Control 2 (0xc5) */
#define RT3261_DSP_DAT_SFT			0

/* FM34-500 Register Control 3 (0xc6) */
#define RT3261_DSP_BUSY_MASK			(0x1 << 15)
#define RT3261_DSP_BUSY_BIT			15
#define RT3261_DSP_DS_MASK			(0x1 << 14)
#define RT3261_DSP_DS_SFT			14
#define RT3261_DSP_DS_FM3010			(0x1 << 14)
#define RT3261_DSP_DS_TEMP			(0x1 << 14)
#define RT3261_DSP_CLK_MASK			(0x3 << 12)
#define RT3261_DSP_CLK_SFT			12
#define RT3261_DSP_CLK_384K			(0x0 << 12)
#define RT3261_DSP_CLK_192K			(0x1 << 12)
#define RT3261_DSP_CLK_96K			(0x2 << 12)
#define RT3261_DSP_CLK_64K			(0x3 << 12)
#define RT3261_DSP_PD_PIN_MASK		(0x1 << 11)
#define RT3261_DSP_PD_PIN_SFT			11
#define RT3261_DSP_PD_PIN_LO			(0x0 << 11)
#define RT3261_DSP_PD_PIN_HI			(0x1 << 11)
#define RT3261_DSP_RST_PIN_MASK		(0x1 << 10)
#define RT3261_DSP_RST_PIN_SFT			10
#define RT3261_DSP_RST_PIN_LO			(0x0 << 10)
#define RT3261_DSP_RST_PIN_HI			(0x1 << 10)
#define RT3261_DSP_R_EN				(0x1 << 9)
#define RT3261_DSP_W_EN			(0x1 << 8)
#define RT3261_DSP_CMD_MASK			(0xff)
#define RT3261_DSP_CMD_PE			(0x0d)	/* Patch Entry */
#define RT3261_DSP_CMD_MW			(0x3b)	/* Memory Write */
#define RT3261_DSP_CMD_MR			(0x37)	/* Memory Read */
#define RT3261_DSP_CMD_RR			(0x60)	/* Register Read */
#define RT3261_DSP_CMD_RW			(0x68)	/* Register Write */
#define RT3261_DSP_REG_DATHI			(0x26)	/* High Data Addr */
#define RT3261_DSP_REG_DATLO			(0x25)	/* Low Data Addr */

/* Programmable Register Array Control 1 (0xc8) */
#define RT3261_REG_SEQ_MASK			(0xf << 12)
#define RT3261_REG_SEQ_SFT			12
#define RT3261_SEQ1_ST_MASK			(0x1 << 11) /*RO*/
#define RT3261_SEQ1_ST_SFT			11
#define RT3261_SEQ1_ST_RUN			(0x0 << 11)
#define RT3261_SEQ1_ST_FIN			(0x1 << 11)
#define RT3261_SEQ2_ST_MASK			(0x1 << 10) /*RO*/
#define RT3261_SEQ2_ST_SFT			10
#define RT3261_SEQ2_ST_RUN			(0x0 << 10)
#define RT3261_SEQ2_ST_FIN			(0x1 << 10)
#define RT3261_REG_LV_MASK			(0x1 << 9)
#define RT3261_REG_LV_SFT			9
#define RT3261_REG_LV_MX			(0x0 << 9)
#define RT3261_REG_LV_PR			(0x1 << 9)
#define RT3261_SEQ_2_PT_MASK			(0x1 << 8)
#define RT3261_SEQ_2_PT_BIT			8
#define RT3261_REG_IDX_MASK			(0xff)
#define RT3261_REG_IDX_SFT			0

/* Programmable Register Array Control 2 (0xc9) */
#define RT3261_REG_DAT_MASK			(0xffff)
#define RT3261_REG_DAT_SFT			0

/* Programmable Register Array Control 3 (0xca) */
#define RT3261_SEQ_DLY_MASK			(0xff << 8)
#define RT3261_SEQ_DLY_SFT			8
#define RT3261_PROG_MASK			(0x1 << 7)
#define RT3261_PROG_SFT				7
#define RT3261_PROG_DIS				(0x0 << 7)
#define RT3261_PROG_EN				(0x1 << 7)
#define RT3261_SEQ1_PT_RUN			(0x1 << 6)
#define RT3261_SEQ1_PT_RUN_BIT			6
#define RT3261_SEQ2_PT_RUN			(0x1 << 5)
#define RT3261_SEQ2_PT_RUN_BIT			5

/* Programmable Register Array Control 4 (0xcb) */
#define RT3261_SEQ1_START_MASK		(0xf << 8)
#define RT3261_SEQ1_START_SFT			8
#define RT3261_SEQ1_END_MASK			(0xf)
#define RT3261_SEQ1_END_SFT			0

/* Programmable Register Array Control 5 (0xcc) */
#define RT3261_SEQ2_START_MASK		(0xf << 8)
#define RT3261_SEQ2_START_SFT			8
#define RT3261_SEQ2_END_MASK			(0xf)
#define RT3261_SEQ2_END_SFT			0

/* Scramble Function (0xcd) */
#define RT3261_SCB_KEY_MASK			(0xff)
#define RT3261_SCB_KEY_SFT			0

/* Scramble Control (0xce) */
#define RT3261_SCB_SWAP_MASK			(0x1 << 15)
#define RT3261_SCB_SWAP_SFT			15
#define RT3261_SCB_SWAP_DIS			(0x0 << 15)
#define RT3261_SCB_SWAP_EN			(0x1 << 15)
#define RT3261_SCB_MASK			(0x1 << 14)
#define RT3261_SCB_SFT				14
#define RT3261_SCB_DIS				(0x0 << 14)
#define RT3261_SCB_EN				(0x1 << 14)

/* Baseback Control (0xcf) */
#define RT3261_BB_MASK				(0x1 << 15)
#define RT3261_BB_SFT				15
#define RT3261_BB_DIS				(0x0 << 15)
#define RT3261_BB_EN				(0x1 << 15)
#define RT3261_BB_CT_MASK			(0x7 << 12)
#define RT3261_BB_CT_SFT			12
#define RT3261_BB_CT_A				(0x0 << 12)
#define RT3261_BB_CT_B				(0x1 << 12)
#define RT3261_BB_CT_C				(0x2 << 12)
#define RT3261_BB_CT_D				(0x3 << 12)
#define RT3261_M_BB_L_MASK			(0x1 << 9)
#define RT3261_M_BB_L_SFT			9
#define RT3261_M_BB_R_MASK			(0x1 << 8)
#define RT3261_M_BB_R_SFT			8
#define RT3261_M_BB_HPF_L_MASK		(0x1 << 7)
#define RT3261_M_BB_HPF_L_SFT			7
#define RT3261_M_BB_HPF_R_MASK		(0x1 << 6)
#define RT3261_M_BB_HPF_R_SFT			6
#define RT3261_G_BB_BST_MASK			(0x3f)
#define RT3261_G_BB_BST_SFT			0

/* MP3 Plus Control 1 (0xd0) */
#define RT3261_M_MP3_L_MASK			(0x1 << 15)
#define RT3261_M_MP3_L_SFT			15
#define RT3261_M_MP3_R_MASK			(0x1 << 14)
#define RT3261_M_MP3_R_SFT			14
#define RT3261_M_MP3_MASK			(0x1 << 13)
#define RT3261_M_MP3_SFT			13
#define RT3261_M_MP3_DIS			(0x0 << 13)
#define RT3261_M_MP3_EN			(0x1 << 13)
#define RT3261_EG_MP3_MASK			(0x1f << 8)
#define RT3261_EG_MP3_SFT			8
#define RT3261_MP3_HLP_MASK			(0x1 << 7)
#define RT3261_MP3_HLP_SFT			7
#define RT3261_MP3_HLP_DIS			(0x0 << 7)
#define RT3261_MP3_HLP_EN			(0x1 << 7)
#define RT3261_M_MP3_ORG_L_MASK		(0x1 << 6)
#define RT3261_M_MP3_ORG_L_SFT		6
#define RT3261_M_MP3_ORG_R_MASK		(0x1 << 5)
#define RT3261_M_MP3_ORG_R_SFT		5

/* MP3 Plus Control 2 (0xd1) */
#define RT3261_MP3_WT_MASK			(0x1 << 13)
#define RT3261_MP3_WT_SFT			13
#define RT3261_MP3_WT_1_4			(0x0 << 13)
#define RT3261_MP3_WT_1_2			(0x1 << 13)
#define RT3261_OG_MP3_MASK			(0x1f << 8)
#define RT3261_OG_MP3_SFT			8
#define RT3261_HG_MP3_MASK			(0x3f)
#define RT3261_HG_MP3_SFT			0

/* 3D HP Control 1 (0xd2) */
#define RT3261_3D_CF_MASK			(0x1 << 15)
#define RT3261_3D_CF_SFT			15
#define RT3261_3D_CF_DIS			(0x0 << 15)
#define RT3261_3D_CF_EN			(0x1 << 15)
#define RT3261_3D_HP_MASK			(0x1 << 14)
#define RT3261_3D_HP_SFT			14
#define RT3261_3D_HP_DIS			(0x0 << 14)
#define RT3261_3D_HP_EN			(0x1 << 14)
#define RT3261_3D_BT_MASK			(0x1 << 13)
#define RT3261_3D_BT_SFT			13
#define RT3261_3D_BT_DIS			(0x0 << 13)
#define RT3261_3D_BT_EN				(0x1 << 13)
#define RT3261_3D_1F_MIX_MASK			(0x3 << 11)
#define RT3261_3D_1F_MIX_SFT			11
#define RT3261_3D_HP_M_MASK			(0x1 << 10)
#define RT3261_3D_HP_M_SFT			10
#define RT3261_3D_HP_M_SUR			(0x0 << 10)
#define RT3261_3D_HP_M_FRO			(0x1 << 10)
#define RT3261_M_3D_HRTF_MASK			(0x1 << 9)
#define RT3261_M_3D_HRTF_SFT			9
#define RT3261_M_3D_D2H_MASK			(0x1 << 8)
#define RT3261_M_3D_D2H_SFT			8
#define RT3261_M_3D_D2R_MASK			(0x1 << 7)
#define RT3261_M_3D_D2R_SFT			7
#define RT3261_M_3D_REVB_MASK			(0x1 << 6)
#define RT3261_M_3D_REVB_SFT			6

/* Adjustable high pass filter control 1 (0xd3) */
#define RT3261_2ND_HPF_MASK			(0x1 << 15)
#define RT3261_2ND_HPF_SFT			15
#define RT3261_2ND_HPF_DIS			(0x0 << 15)
#define RT3261_2ND_HPF_EN			(0x1 << 15)
#define RT3261_HPF_CF_L_MASK			(0x7 << 12)
#define RT3261_HPF_CF_L_SFT			12
#define RT3261_1ST_HPF_MASK			(0x1 << 11)
#define RT3261_1ST_HPF_SFT			11
#define RT3261_1ST_HPF_DIS			(0x0 << 11)
#define RT3261_1ST_HPF_EN			(0x1 << 11)
#define RT3261_HPF_CF_R_MASK			(0x7 << 8)
#define RT3261_HPF_CF_R_SFT			8
#define RT3261_ZD_T_MASK			(0x3 << 6)
#define RT3261_ZD_T_SFT				6
#define RT3261_ZD_F_MASK			(0x3 << 4)
#define RT3261_ZD_F_SFT				4
#define RT3261_ZD_F_IM				(0x0 << 4)
#define RT3261_ZD_F_ZC_IM			(0x1 << 4)
#define RT3261_ZD_F_ZC_IOD			(0x2 << 4)
#define RT3261_ZD_F_UN				(0x3 << 4)

/* HP calibration control and Amp detection (0xd6) */
#define RT3261_SI_DAC_MASK			(0x1 << 11)
#define RT3261_SI_DAC_SFT			11
#define RT3261_SI_DAC_AUTO			(0x0 << 11)
#define RT3261_SI_DAC_TEST			(0x1 << 11)
#define RT3261_DC_CAL_M_MASK			(0x1 << 10)
#define RT3261_DC_CAL_M_SFT			10
#define RT3261_DC_CAL_M_CAL			(0x0 << 10)
#define RT3261_DC_CAL_M_NOR			(0x1 << 10)
#define RT3261_DC_CAL_MASK			(0x1 << 9)
#define RT3261_DC_CAL_SFT			9
#define RT3261_DC_CAL_DIS			(0x0 << 9)
#define RT3261_DC_CAL_EN			(0x1 << 9)
#define RT3261_HPD_RCV_MASK			(0x7 << 6)
#define RT3261_HPD_RCV_SFT			6
#define RT3261_HPD_PS_MASK			(0x1 << 5)
#define RT3261_HPD_PS_SFT			5
#define RT3261_HPD_PS_DIS			(0x0 << 5)
#define RT3261_HPD_PS_EN			(0x1 << 5)
#define RT3261_CAL_M_MASK			(0x1 << 4)
#define RT3261_CAL_M_SFT			4
#define RT3261_CAL_M_DEP			(0x0 << 4)
#define RT3261_CAL_M_CAL			(0x1 << 4)
#define RT3261_CAL_MASK			(0x1 << 3)
#define RT3261_CAL_SFT				3
#define RT3261_CAL_DIS				(0x0 << 3)
#define RT3261_CAL_EN				(0x1 << 3)
#define RT3261_CAL_TEST_MASK			(0x1 << 2)
#define RT3261_CAL_TEST_SFT			2
#define RT3261_CAL_TEST_DIS			(0x0 << 2)
#define RT3261_CAL_TEST_EN			(0x1 << 2)
#define RT3261_CAL_P_MASK			(0x3)
#define RT3261_CAL_P_SFT			0
#define RT3261_CAL_P_NONE			(0x0)
#define RT3261_CAL_P_CAL			(0x1)
#define RT3261_CAL_P_DAC_CAL			(0x2)

/* Soft volume and zero cross control 1 (0xd9) */
#define RT3261_SV_MASK				(0x1 << 15)
#define RT3261_SV_SFT				15
#define RT3261_SV_DIS				(0x0 << 15)
#define RT3261_SV_EN				(0x1 << 15)
#define RT3261_SPO_SV_MASK			(0x1 << 14)
#define RT3261_SPO_SV_SFT			14
#define RT3261_SPO_SV_DIS			(0x0 << 14)
#define RT3261_SPO_SV_EN			(0x1 << 14)
#define RT3261_OUT_SV_MASK			(0x1 << 13)
#define RT3261_OUT_SV_SFT			13
#define RT3261_OUT_SV_DIS			(0x0 << 13)
#define RT3261_OUT_SV_EN			(0x1 << 13)
#define RT3261_HP_SV_MASK			(0x1 << 12)
#define RT3261_HP_SV_SFT			12
#define RT3261_HP_SV_DIS			(0x0 << 12)
#define RT3261_HP_SV_EN			(0x1 << 12)
#define RT3261_ZCD_DIG_MASK			(0x1 << 11)
#define RT3261_ZCD_DIG_SFT			11
#define RT3261_ZCD_DIG_DIS			(0x0 << 11)
#define RT3261_ZCD_DIG_EN			(0x1 << 11)
#define RT3261_ZCD_MASK			(0x1 << 10)
#define RT3261_ZCD_SFT				10
#define RT3261_ZCD_PD				(0x0 << 10)
#define RT3261_ZCD_PU				(0x1 << 10)
#define RT3261_M_ZCD_MASK			(0x3f << 4)
#define RT3261_M_ZCD_SFT			4
#define RT3261_M_ZCD_RM_L			(0x1 << 9)
#define RT3261_M_ZCD_RM_R			(0x1 << 8)
#define RT3261_M_ZCD_SM_L			(0x1 << 7)
#define RT3261_M_ZCD_SM_R			(0x1 << 6)
#define RT3261_M_ZCD_OM_L			(0x1 << 5)
#define RT3261_M_ZCD_OM_R			(0x1 << 4)
#define RT3261_SV_DLY_MASK			(0xf)
#define RT3261_SV_DLY_SFT			0

/* Soft volume and zero cross control 2 (0xda) */
#define RT3261_ZCD_HP_MASK			(0x1 << 15)
#define RT3261_ZCD_HP_SFT			15
#define RT3261_ZCD_HP_DIS			(0x0 << 15)
#define RT3261_ZCD_HP_EN			(0x1 << 15)


/* Codec Private Register definition */
/* 3D Speaker Control (0x63) */
#define RT3261_3D_SPK_MASK			(0x1 << 15)
#define RT3261_3D_SPK_SFT			15
#define RT3261_3D_SPK_DIS			(0x0 << 15)
#define RT3261_3D_SPK_EN			(0x1 << 15)
#define RT3261_3D_SPK_M_MASK			(0x3 << 13)
#define RT3261_3D_SPK_M_SFT			13
#define RT3261_3D_SPK_CG_MASK			(0x1f << 8)
#define RT3261_3D_SPK_CG_SFT			8
#define RT3261_3D_SPK_SG_MASK			(0x1f)
#define RT3261_3D_SPK_SG_SFT			0

/* Wind Noise Detection Control 1 (0x6c) */
#define RT3261_WND_MASK			(0x1 << 15)
#define RT3261_WND_SFT				15
#define RT3261_WND_DIS				(0x0 << 15)
#define RT3261_WND_EN				(0x1 << 15)

/* Wind Noise Detection Control 2 (0x6d) */
#define RT3261_WND_FC_NW_MASK		(0x3f << 10)
#define RT3261_WND_FC_NW_SFT			10
#define RT3261_WND_FC_WK_MASK		(0x3f << 4)
#define RT3261_WND_FC_WK_SFT			4

/* Wind Noise Detection Control 3 (0x6e) */
#define RT3261_HPF_FC_MASK			(0x3f << 6)
#define RT3261_HPF_FC_SFT			6
#define RT3261_WND_FC_ST_MASK			(0x3f)
#define RT3261_WND_FC_ST_SFT			0

/* Wind Noise Detection Control 4 (0x6f) */
#define RT3261_WND_TH_LO_MASK		(0x3ff)
#define RT3261_WND_TH_LO_SFT			0

/* Wind Noise Detection Control 5 (0x70) */
#define RT3261_WND_TH_HI_MASK			(0x3ff)
#define RT3261_WND_TH_HI_SFT			0

/* Wind Noise Detection Control 8 (0x73) */
#define RT3261_WND_WIND_MASK			(0x1 << 13) /* Read-Only */
#define RT3261_WND_WIND_SFT			13
#define RT3261_WND_STRONG_MASK		(0x1 << 12) /* Read-Only */
#define RT3261_WND_STRONG_SFT			12
enum {
	RT3261_NO_WIND,
	RT3261_BREEZE,
	RT3261_STORM,
};

/* Dipole Speaker Interface (0x75) */
#define RT3261_DP_ATT_MASK			(0x3 << 14)
#define RT3261_DP_ATT_SFT			14
#define RT3261_DP_SPK_MASK			(0x1 << 10)
#define RT3261_DP_SPK_SFT			10
#define RT3261_DP_SPK_DIS			(0x0 << 10)
#define RT3261_DP_SPK_EN			(0x1 << 10)

/* EQ Pre Volume Control (0xb3) */
#define RT3261_EQ_PRE_VOL_MASK		(0xffff)
#define RT3261_EQ_PRE_VOL_SFT			0

/* EQ Post Volume Control (0xb4) */
#define RT3261_EQ_PST_VOL_MASK		(0xffff)
#define RT3261_EQ_PST_VOL_SFT			0

/* General Control1 (0xfa) */
#define RT3261_M_MAMIX_L			(0x1 << 13)
#define RT3261_M_MAMIX_R			(0x1 << 12)

/* General Control2 (0xfb) */
#define RT3261_RXDC_SRC_MASK		(0x1 << 7)
#define RT3261_RXDC_SRC_STO		(0x0 << 7)
#define RT3261_RXDC_SRC_MONO		(0x1 << 7)
#define RT3261_RXDC_SRC_SFT		(7)
#define RT3261_RXDP2_SEL_MASK		(0x1 << 3)
#define RT3261_RXDP2_SEL_IF2		(0x0 << 3)
#define RT3261_RXDP2_SEL_ADC		(0x1 << 3)
#define RT3261_RXDP2_SEL_SFT		(3)


/* Vendor ID (0xfd) */
#define RT3261_VER_C				0x2
#define RT3261_VER_D				0x3


/* Volume Rescale */
#define RT3261_VOL_RSCL_MAX 0x27
#define RT3261_VOL_RSCL_RANGE 0x1F
/* Debug String Length */
#define RT3261_REG_DISP_LEN 10

#define RT3261_NO_JACK		BIT(0)
#define RT3261_HEADSET_DET	BIT(1)
#define RT3261_HEADPHO_DET	BIT(2)

int rt3261_headset_detect(struct snd_soc_codec *codec, int jack_insert);

/* System Clock Source */
enum {
	RT3261_SCLK_S_MCLK,
	RT3261_SCLK_S_PLL1,
	RT3261_SCLK_S_RCCLK,
};

/* PLL1 Source */
enum {
	RT3261_PLL1_S_MCLK,
	RT3261_PLL1_S_BCLK1,
	RT3261_PLL1_S_BCLK2,
	RT3261_PLL1_S_BCLK3,
};

enum {
	RT3261_AIF1,
	RT3261_AIF2,
	RT3261_AIF3,
	RT3261_AIFS,
};

#define RT3261_U_IF1 (0x1)
#define RT3261_U_IF2 (0x1 << 1)
#define RT3261_U_IF3 (0x1 << 2)

enum {
	RT3261_IF_123,
	RT3261_IF_132,
	RT3261_IF_312,
	RT3261_IF_321,
	RT3261_IF_231,
	RT3261_IF_213,
	RT3261_IF_113,
	RT3261_IF_223,
	RT3261_IF_ALL,
};

enum {
	RT3261_DMIC_DIS,
	RT3261_DMIC1,
	RT3261_DMIC2,
};

struct rt3261_pll_code {
	bool m_bp; /* Indicates bypass m code or not. */
	int m_code;
	int n_code;
	int k_code;
};

struct rt3261_priv {
	struct snd_soc_codec *codec;
	struct delayed_work patch_work;

	int aif_pu;
	int sysclk;
	int sysclk_src;
	int lrck[RT3261_AIFS];
	int bclk[RT3261_AIFS];
	int master[RT3261_AIFS];

	int pll_src;
	int pll_in;
	int pll_out;

	int dmic_en;
	int dsp_sw; /* expected parameter setting */
	bool dsp_play_pass;
	bool dsp_rec_pass;

	unsigned int codec_en_gpio;
	int (*io_init)(int gpio, char *iomux_name, int iomux_mode);
};

int rt3261_conn_mux_path(struct snd_soc_codec *codec,
		char *widget_name, char *path_name);

#endif /* __RT3261_H__ */
