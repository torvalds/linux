/*
 * rk616.h  --  RK616 CODEC ALSA SoC audio driver
 *
 * Copyright 2013 Rockship
 * Author: chenjq <chenjq@rock-chips.com>
 *
 */

#ifndef __RK616_CODEC_H__
#define __RK616_CODEC_H__

/* mfd register */
//#define CRU_PCM2IS2_CON2   			0x0098
#define PCM_TO_I2S_MUX				(1 << 3)
#define APS_SEL					(1 << 2)
#define APS_CLR					(1 << 1)
#define I2S_CHANNEL_SEL				(1 << 0)

//#define CRU_CFGMISC_CON			0x009C
#define MICDET1_PIN_F_CODEC			(1 << 18)
#define MICDET2_PIN_F_CODEC			(1 << 17)
#define AD_DA_LOOP				(1 << 0)
#define AD_DA_LOOP_SFT				0

/* codec register */
#define RK616_CODEC_BASE			0x0800

#define RK616_RESET				(RK616_CODEC_BASE + 0x00)
#define RK616_DAC_VOL				(RK616_CODEC_BASE + 0x04)
#define RK616_ADC_INT_CTL1			(RK616_CODEC_BASE + 0x08)
#define RK616_ADC_INT_CTL2			(RK616_CODEC_BASE + 0x0c)
#define RK616_DAC_INT_CTL1			(RK616_CODEC_BASE + 0x10)
#define RK616_DAC_INT_CTL2			(RK616_CODEC_BASE + 0x14)
#define RK616_CLK_CHPUMP			(RK616_CODEC_BASE + 0x1c)
#define RK616_PGA_AGC_CTL			(RK616_CODEC_BASE + 0x28)
#define RK616_PWR_ADD1				(RK616_CODEC_BASE + 0x3c)
#define RK616_BST_CTL				(RK616_CODEC_BASE + 0x40)
#define RK616_DIFFIN_CTL			(RK616_CODEC_BASE + 0x44)
#define RK616_MIXINL_CTL			(RK616_CODEC_BASE + 0x48)
#define RK616_MIXINL_VOL1			(RK616_CODEC_BASE + 0x4c)
#define RK616_MIXINL_VOL2			(RK616_CODEC_BASE + 0x50)
#define RK616_MIXINR_CTL			(RK616_CODEC_BASE + 0x54)
#define RK616_MIXINR_VOL1			(RK616_CODEC_BASE + 0x58)
#define RK616_MIXINR_VOL2			(RK616_CODEC_BASE + 0x5c)
#define RK616_PGAL_CTL				(RK616_CODEC_BASE + 0x60)
#define RK616_PGAR_CTL				(RK616_CODEC_BASE + 0x64)
#define RK616_PWR_ADD2				(RK616_CODEC_BASE + 0x68)
#define RK616_DAC_CTL				(RK616_CODEC_BASE + 0x6c)
#define RK616_LINEMIX_CTL			(RK616_CODEC_BASE + 0x70)
#define RK616_MUXHP_HPMIX_CTL			(RK616_CODEC_BASE + 0x74)
#define RK616_HPMIX_CTL				(RK616_CODEC_BASE + 0x78)
#define RK616_HPMIX_VOL1			(RK616_CODEC_BASE + 0x7c)
#define RK616_HPMIX_VOL2			(RK616_CODEC_BASE + 0x80)
#define RK616_LINEOUT1_CTL			(RK616_CODEC_BASE + 0x84)
#define RK616_LINEOUT2_CTL			(RK616_CODEC_BASE + 0x88)
#define RK616_SPKL_CTL				(RK616_CODEC_BASE + 0x8c)
#define RK616_SPKR_CTL				(RK616_CODEC_BASE + 0x90)
#define RK616_HPL_CTL				(RK616_CODEC_BASE + 0x94)
#define RK616_HPR_CTL				(RK616_CODEC_BASE + 0x98)
#define RK616_MICBIAS_CTL			(RK616_CODEC_BASE + 0x9c)
#define RK616_MICKEY_DET_CTL			(RK616_CODEC_BASE + 0xa0)
#define RK616_PWR_ADD3				(RK616_CODEC_BASE + 0xa4)
#define RK616_ADC_CTL				(RK616_CODEC_BASE + 0xa8)
#define RK616_SINGNAL_ZC_CTL1			(RK616_CODEC_BASE + 0xac)//Signal zero-crossing detection
#define RK616_SINGNAL_ZC_CTL2			(RK616_CODEC_BASE + 0xB0)//Signal zero-crossing detection
#define RK616_PGAL_AGC_CTL1			(RK616_CODEC_BASE + 0xc0)
#define RK616_PGAL_AGC_CTL2			(RK616_CODEC_BASE + 0xc4)
#define RK616_PGAL_AGC_CTL3			(RK616_CODEC_BASE + 0xc8)
#define RK616_PGAL_AGC_CTL4			(RK616_CODEC_BASE + 0xcc)
#define RK616_PGAL_ASR_CTL			(RK616_CODEC_BASE + 0xd0)
#define RK616_PGAL_AGC_MAX_H			(RK616_CODEC_BASE + 0xd4)
#define RK616_PGAL_AGC_MAX_L			(RK616_CODEC_BASE + 0xd8)
#define RK616_PGAL_AGC_MIN_H			(RK616_CODEC_BASE + 0xdc)
#define RK616_PGAL_AGC_MIN_L			(RK616_CODEC_BASE + 0xe0)
#define RK616_PGAL_AGC_CTL5			(RK616_CODEC_BASE + 0xe4)
#define RK616_PGAR_AGC_CTL1			(RK616_CODEC_BASE + 0x100)
#define RK616_PGAR_AGC_CTL2			(RK616_CODEC_BASE + 0x104)
#define RK616_PGAR_AGC_CTL3			(RK616_CODEC_BASE + 0x108)
#define RK616_PGAR_AGC_CTL4			(RK616_CODEC_BASE + 0x10c)
#define RK616_PGAR_ASR_CTL			(RK616_CODEC_BASE + 0x110)
#define RK616_PGAR_AGC_MAX_H			(RK616_CODEC_BASE + 0x114)
#define RK616_PGAR_AGC_MAX_L			(RK616_CODEC_BASE + 0x118)
#define RK616_PGAR_AGC_MIN_H			(RK616_CODEC_BASE + 0x11c)
#define RK616_PGAR_AGC_MIN_L			(RK616_CODEC_BASE + 0x120)
#define RK616_PGAR_AGC_CTL5			(RK616_CODEC_BASE + 0x124)

/* global definition (0x8c 0x90 0x94 0x98) */
#define RK616_PWRD				(0x1 << 7)
#define RK616_PWRD_SFT				7

#define RK616_INIT_MASK				(0x1 << 6)
#define RK616_INIT_SFT				6
#define RK616_INIT_RN				(0x1 << 6)
#define RK616_INIT_AFT				(0x0 << 6)

#define RK616_MUTE				(0x1 << 5)
#define RK616_MUTE_SFT				5

#define RK616_VOL_MASK				0x1f
#define RK616_VOL_SFT				0

/* ADC Interface Control 1 (0x08) */
#define RK616_ALRCK_POL_MASK			(0x1 << 7)
#define RK616_ALRCK_POL_SFT			7
#define RK616_ALRCK_POL_EN			(0x1 << 7)
#define RK616_ALRCK_POL_DIS			(0x0 << 7)

#define RK616_ADC_VWL_MASK			(0x3 << 5)
#define RK616_ADC_VWL_SFT			5
#define RK616_ADC_VWL_32			(0x3 << 5)
#define RK616_ADC_VWL_24			(0x2 << 5)
#define RK616_ADC_VWL_20			(0x1 << 5)
#define RK616_ADC_VWL_16			(0x0 << 5)

#define RK616_ADC_DF_MASK			(0x3 << 3)
#define RK616_ADC_DF_SFT			3
#define RK616_ADC_DF_PCM			(0x3 << 3)
#define RK616_ADC_DF_I2S			(0x2 << 3)
#define RK616_ADC_DF_LJ				(0x1 << 3)
#define RK616_ADC_DF_RJ				(0x0 << 3)

#define RK616_ADC_SWAP_MASK			(0x1 << 1)
#define RK616_ADC_SWAP_SFT			1
#define RK616_ADC_SWAP_EN			(0x1 << 1)
#define RK616_ADC_SWAP_DIS			(0x0 << 1)

#define RK616_ADC_TYPE_MASK			0x1
#define RK616_ADC_TYPE_SFT			0
#define RK616_ADC_TYPE_MONO			0x1
#define RK616_ADC_TYPE_STEREO			0x0

/* ADC Interface Control 2 (0x0c) */
#define RK616_I2S_MODE_MASK			(0x1 << 4)
#define RK616_I2S_MODE_SFT			4
#define RK616_I2S_MODE_MST			(0x1 << 4)
#define RK616_I2S_MODE_SLV			(0x0 << 4)

#define RK616_ADC_WL_MASK			(0x3 << 2)
#define RK616_ADC_WL_SFT			2
#define RK616_ADC_WL_32				(0x3 << 2)
#define RK616_ADC_WL_24				(0x2 << 2)
#define RK616_ADC_WL_20				(0x1 << 2)
#define RK616_ADC_WL_16				(0x0 << 2)

#define RK616_ADC_RST_MASK			(0x1 << 1)
#define RK616_ADC_RST_SFT			1
#define RK616_ADC_RST_DIS			(0x1 << 1)
#define RK616_ADC_RST_EN			(0x0 << 1)

#define RK616_ABCLK_POL_MASK			0x1
#define RK616_ABCLK_POL_SFT			0
#define RK616_ABCLK_POL_EN			0x1
#define RK616_ABCLK_POL_DIS			0x0

/* DAC Interface Control 1 (0x10) */
#define RK616_DLRCK_POL_MASK			(0x1 << 7)
#define RK616_DLRCK_POL_SFT			7
#define RK616_DLRCK_POL_EN			(0x1 << 7)
#define RK616_DLRCK_POL_DIS			(0x0 << 7)

#define RK616_DAC_VWL_MASK			(0x3 << 5)
#define RK616_DAC_VWL_SFT			5
#define RK616_DAC_VWL_32			(0x3 << 5)
#define RK616_DAC_VWL_24			(0x2 << 5)
#define RK616_DAC_VWL_20			(0x1 << 5)
#define RK616_DAC_VWL_16			(0x0 << 5)

#define RK616_DAC_DF_MASK			(0x3 << 3)
#define RK616_DAC_DF_SFT			3
#define RK616_DAC_DF_PCM			(0x3 << 3)
#define RK616_DAC_DF_I2S			(0x2 << 3)
#define RK616_DAC_DF_LJ				(0x1 << 3)
#define RK616_DAC_DF_RJ				(0x0 << 3)

#define RK616_DAC_SWAP_MASK			(0x1 << 2)
#define RK616_DAC_SWAP_SFT			2
#define RK616_DAC_SWAP_EN			(0x1 << 2)
#define RK616_DAC_SWAP_DIS			(0x0 << 2)

/* DAC Interface Control 2 (0x14) */
#define RK616_DAC_WL_MASK			(0x3 << 2)
#define RK616_DAC_WL_SFT			2
#define RK616_DAC_WL_32				(0x3 << 2)
#define RK616_DAC_WL_24				(0x2 << 2)
#define RK616_DAC_WL_20				(0x1 << 2)
#define RK616_DAC_WL_16				(0x0 << 2)

#define RK616_DAC_RST_MASK			(0x1 << 1)
#define RK616_DAC_RST_SFT			1
#define RK616_DAC_RST_DIS			(0x1 << 1)
#define RK616_DAC_RST_EN			(0x0 << 1)

#define RK616_DBCLK_POL_MASK			0x1
#define RK616_DBCLK_POL_SFT			0
#define RK616_DBCLK_POL_EN			0x1
#define RK616_DBCLK_POL_DIS			0x0

/* PGA AGC Enable (0x28) */
#define RK616_PGAL_AGC_EN_MASK			(0x1 << 5)
#define RK616_PGAL_AGC_EN_SFT			5
#define RK616_PGAL_AGC_EN			(0x1 << 5)
#define RK616_PGAL_AGC_DIS			(0x0 << 5)

#define RK616_PGAR_AGC_EN_MASK			(0x1 << 4)
#define RK616_PGAR_AGC_EN_SFT			4
#define RK616_PGAR_AGC_EN			(0x1 << 4)
#define RK616_PGAR_AGC_DIS			(0x0 << 4)

/* Power Management Addition 1 (0x3c) */
#define RK616_ADC_PWRD				(0x1 << 6)
#define RK616_ADC_PWRD_SFT			6

#define RK616_DIFFIN_MIR_PGAR_RLPWRD		(0x1 << 5)
#define RK616_DIFFIN_MIR_PGAR_RLPWRD_SFT	5

#define RK616_MIC1_MIC2_MIL_PGAL_RLPWRD		(0x1 << 4)
#define RK616_MIC1_MIC2_MIL_PGAL_RLPWRD_SFT	4

#define RK616_ADCL_RLPWRD			(0x1 << 3)
#define RK616_ADCL_RLPWRD_SFT			3

#define RK616_ADCR_RLPWRD			(0x1 << 2)
#define RK616_ADCR_RLPWRD_SFT			2

/* BST Control (0x40) */
#define RK616_BSTL_PWRD				(0x1 << 7)
#define RK616_BSTL_PWRD_SFT			7

#define RK616_BSTL_MODE_MASK			(0x1 << 6)
#define RK616_BSTL_MODE_SFT			6
#define RK616_BSTL_MODE_SE			(0x1 << 6)
#define RK616_BSTL_MODE_DIFF			(0x0 << 6)

#define RK616_BSTL_GAIN_MASK			(0x1 << 5)
#define RK616_BSTL_GAIN_SFT			5
#define RK616_BSTL_GAIN_20DB			(0x1 << 5)
#define RK616_BSTL_GAIN_0DB			(0x0 << 5)

#define RK616_BSTL_MUTE				(0x1 << 4)
#define RK616_BSTL_MUTE_SFT			4

#define RK616_BSTR_PWRD				(0x1 << 3)
#define RK616_BSTR_PWRD_SFT			3

#define RK616_BSTR_MODE_MASK			(0x1 << 2)
#define RK616_BSTR_MODE_SFT			2
#define RK616_BSTR_MODE_SE			(0x1 << 2)
#define RK616_BSTR_MODE_DIFF			(0x0 << 2)

#define RK616_BSTR_GAIN_MASK			(0x1 << 1)
#define RK616_BSTR_GAIN_SFT			1
#define RK616_BSTR_GAIN_20DB			(0x1 << 1)
#define RK616_BSTR_GAIN_0DB			(0x0 << 1)

#define RK616_BSTR_MUTE				0x1
#define RK616_BSTR_MUTE_SFT			0

/* DIFFIN Control (0x44) */
#define RK616_DIFFIN_PWRD			(0x1 << 5)
#define RK616_DIFFIN_PWRD_SFT			5

#define RK616_DIFFIN_MODE_MASK			(0x1 << 4)
#define RK616_DIFFIN_MODE_SFT			4
#define RK616_DIFFIN_MODE_SE			(0x1 << 4)
#define RK616_DIFFIN_MODE_DIFF			(0x0 << 4)

#define RK616_DIFFIN_GAIN_MASK			(0x1 << 3)
#define RK616_DIFFIN_GAIN_SFT			3
#define RK616_DIFFIN_GAIN_20DB			(0x1 << 3)
#define RK616_DIFFIN_GAIN_0DB			(0x0 << 3)

#define RK616_DIFFIN_MUTE			(0x1 << 2)
#define RK616_DIFFIN_MUTE_SFT			2

#define RK616_MIRM_F_MASK			(0x1 << 1)
#define RK616_MIRM_F_SFT			1
#define RK616_MIRM_F_IN1N			(0x1 << 1)
#define RK616_MIRM_F_DIFFIN			(0x0 << 1)

#define RK616_HMM_F_MASK			0x1
#define RK616_HMM_F_SFT				0
#define RK616_HMM_F_IN1N			0x1
#define RK616_HMM_F_DIFFIN			0x0


/* BSTR MUXMIC MIXINL Control (0x48) */
#define RK616_SE_BSTR_F_MASK			(0x1 << 6)
#define RK616_SE_BSTR_F_SFT			6
#define RK616_SE_BSTR_F_MIN2P			(0x1 << 6)
#define RK616_SE_BSTR_F_MIN2N			(0x0 << 6)

#define RK616_MM_F_MASK				(0x1 << 5)
#define RK616_MM_F_SFT				5
#define RK616_MM_F_BSTR				(0x1 << 5)
#define RK616_MM_F_BSTL				(0x0 << 5)

#define RK616_MIL_PWRD				(0x1 << 4)
#define RK616_MIL_PWRD_SFT			4

#define RK616_MIL_MUTE				(0x1 << 3)
#define RK616_MIL_MUTE_SFT			3

#define RK616_MIL_F_IN3L			(0x1 << 2)
#define RK616_MIL_F_IN3L_SFT			2

#define RK616_MIL_F_IN1P			(0x1 << 1)
#define RK616_MIL_F_IN1P_SFT			1

#define RK616_MIL_F_MUX				(0x1 << 0)
#define RK616_MIL_F_MUX_SFT			0

/* MIXINL volume 1 (0x4c) */
#define RK616_MIL_F_MUX_VOL_MASK		(0x7 << 3)
#define RK616_MIL_F_MUX_VOL_SFT			3

#define RK616_MIL_F_IN1P_VOL_MASK		0x7
#define RK616_MIL_F_IN1P_VOL_SFT		0

/* MIXINL volume 2 (0x50) */
#define RK616_MIL_F_IN3L_VOL_MASK		0x7
#define RK616_MIL_F_IN3L_VOL_SFT		0

/* MIXINR Control (0x54) */
#define RK616_MIR_PWRD				(0x1 << 5)
#define RK616_MIR_PWRD_SFT			5

#define RK616_MIR_MUTE				(0x1 << 4)
#define RK616_MIR_MUTE_SFT			4

#define RK616_MIR_F_MIC2N			(0x1 << 3)
#define RK616_MIR_F_MIC2N_SFT			3

#define RK616_MIR_F_IN1P			(0x1 << 2)
#define RK616_MIR_F_IN1P_SFT			2

#define RK616_MIR_F_IN3R			(0x1 << 1)
#define RK616_MIR_F_IN3R_SFT			1

#define RK616_MIR_F_MIRM			0x1
#define RK616_MIR_F_MIRM_SFT			0

/* MIXINR volume 1 (0x58) */
#define RK616_MIR_F_MIRM_VOL_MASK		(0x7 << 3)
#define RK616_MIR_F_MIRM_VOL_SFT		3

#define RK616_MIR_F_IN3R_VOL_MASK		0x7
#define RK616_MIR_F_IN3R_VOL_SFT		0

/* MIXINR volume 2 (0x5c) */
#define RK616_MIR_F_MIC2N_VOL_MASK		(0x7 << 3)
#define RK616_MIR_F_MIC2N_VOL_SFT		3

#define RK616_MIR_F_IN1P_VOL_MASK		0x7
#define RK616_MIR_F_IN1P_VOL_SFT		0

/* PGA Control (0x60 0x64) */
#define RK616_PGA_PWRD				(0x1 << 7)
#define RK616_PGA_PWRD_SFT			7

#define RK616_PGA_MUTE				(0x1 << 6)
#define RK616_PGA_MUTE_SFT			6

#define RK616_PGA_VOL_MASK			(0x1f << 0)
#define RK616_PGA_VOL_SFT			0


/* Power Management Addition 2 (0x68) */
#define RK616_HPL_HPR_PWRD			(0x1 << 7)
#define RK616_HPL_HPR_PWRD_SFT			7

#define RK616_DAC_PWRD				(0x1 << 6)
#define RK616_DAC_PWRD_SFT			6

#define RK616_DACL_RLPWRD			(0x1 << 5)
#define RK616_DACL_RLPWRD_SFT			5

#define RK616_DACL_SPKL_RLPWRD			(0x1 << 4)
#define RK616_DACL_SPKL_RLPWRD_SFT		4

#define RK616_DACR_RLPWRD			(0x1 << 3)
#define RK616_DACR_RLPWRD_SFT			3

#define RK616_DACR_SPKR_RLPWRD			(0x1 << 2)//? BIT 3 BIT 6 BIT 2
#define RK616_DACR_SPKR_RLPWRD_SFT		2

#define RK616_LM_LO_RLPWRD			(0x1 << 1)
#define RK616_LM_LO_RLPWRD_SFT			1

#define RK616_HM_RLPWRD				0x1
#define RK616_HM_RLPWRD_SFT			0

/* DAC Control (0x6c) */
#define RK616_DACL_INIT_MASK			(0x1 << 5)
#define RK616_DACL_INIT_SFT			5
#define RK616_DACL_INIT_WORK			(0x1 << 5)
#define RK616_DACL_INIT_NOT			(0x0 << 5)

#define RK616_DACR_INIT_MASK			(0x1 << 4)
#define RK616_DACR_INIT_SFT			4
#define RK616_DACR_INIT_WORK			(0x1 << 4)
#define RK616_DACR_INIT_NOT			(0x0 << 4)

#define RK616_DACL_PWRD				(0x1 << 3)
#define RK616_DACL_PWRD_SFT			3

#define RK616_DACR_PWRD				(0x1 << 2)
#define RK616_DACR_PWRD_SFT			2

#define RK616_DACR_CLK_PWRD			(0x1 << 1)
#define RK616_DACR_CLK_PWRD_SFT			1

#define RK616_DACL_CLK_PWRD			0x1
#define RK616_DACL_CLK_PWRD_SFT			0

/* Linemix Control (0x70) */
#define RK616_LM_PWRD				(0x1 << 4)
#define RK616_LM_PWRD_SFT			4

#define RK616_LM_F_PGAR				(0x1 << 3)
#define RK616_LM_F_PGAR_SFT			3

#define RK616_LM_F_PGAL				(0x1 << 2)
#define RK616_LM_F_PGAL_SFT			2

#define RK616_LM_F_DACR				(0x1 << 1)
#define RK616_LM_F_DACR_SFT			1

#define RK616_LM_F_DACL				0x1
#define RK616_LM_F_DACL_SFT			0

/* MUXHP HPMIX Control (0x74) */
#define RK616_HML_PWRD				(0x1 << 5)
#define RK616_HML_PWRD_SFT			5

#define RK616_HML_INIT_MASK			(0x1 << 4)
#define RK616_HML_INIT_SFT			4
#define RK616_HML_INIT_RN			(0x1 << 4)
#define RK616_HML_INIT_AFT			(0x0 << 4)

#define RK616_HMR_PWRD				(0x1 << 3)
#define RK616_HMR_PWRD_SFT			3

#define RK616_HMR_INIT_MASK			(0x1 << 2)
#define RK616_HMR_INIT_SFT			2
#define RK616_HMR_INIT_RN			(0x1 << 2)
#define RK616_HMR_INIT_AFT			(0x0 << 2)

#define RK616_MHL_F_MASK			(0x1 << 1)
#define RK616_MHL_F_SFT				1
#define RK616_MHL_F_DACL			(0x1 << 1)
#define RK616_MHL_F_HPMIXL			(0x0 << 1)

#define RK616_MHR_F_MASK			0x1
#define RK616_MHR_F_SFT				0
#define RK616_MHR_F_DACR			0x1
#define RK616_MHR_F_HPMIXR			0x0

/* HPMIX Control (0x78) */
#define RK616_HML_F_HMM				(0x1 << 7)
#define RK616_HML_F_HMM_SFT			7

#define RK616_HML_F_IN1P			(0x1 << 6)
#define RK616_HML_F_IN1P_SFT			6

#define RK616_HML_F_PGAL			(0x1 << 5)
#define RK616_HML_F_PGAL_SFT			5

#define RK616_HML_F_DACL			(0x1 << 4)
#define RK616_HML_F_DACL_SFT			4

#define RK616_HMR_F_HMM				(0x1 << 3)
#define RK616_HMR_F_HMM_SFT			3

#define RK616_HMR_F_PGAR			(0x1 << 2)
#define RK616_HMR_F_PGAR_SFT			2

#define RK616_HMR_F_PGAL			(0x1 << 1)
#define RK616_HMR_F_PGAL_SFT			1

#define RK616_HMR_F_DACR			0x1
#define RK616_HMR_F_DACR_SFT			0

/* HPMIX Volume Control 1 (0x7c) */
#define RK616_HML_F_IN1P_VOL_MASK		0x7
#define RK616_HML_F_IN1P_VOL_SFT		0

/* HPMIX Volume Control 2 (0x80) */
#define RK616_HML_F_HMM_VOL_MASK		(0x7 << 3)
#define RK616_HML_F_HMM_VOL_SFT			3

#define RK616_HMR_F_HMM_VOL_MASK		0x7
#define RK616_HMR_F_HMM_VOL_SFT			0

/* Lineout1 Control (0x84 0x88) */
#define RK616_LINEOUT_PWRD			(0x1 << 6)
#define RK616_LINEOUT_PWRD_SFT			6

#define RK616_LINEOUT_MUTE			(0x1 << 5)
#define RK616_LINEOUT_MUTE_SFT			5

#define RK616_LINEOUT_VOL_MASK			0x1f
#define RK616_LINEOUT_VOL_SFT			0

/* Micbias Control 1 (0x9c) */
#define RK616_MICBIAS1_PWRD			(0x1 << 7)
#define RK616_MICBIAS1_PWRD_SFT			7

#define RK616_MICBIAS2_PWRD			(0x1 << 6)
#define RK616_MICBIAS2_PWRD_SFT			6

#define RK616_MICBIAS1_V_MASK			(0x7 << 3)
#define RK616_MICBIAS1_V_SFT			3
#define RK616_MICBIAS1_V_1_7			(0x7 << 3)
#define RK616_MICBIAS1_V_1_6			(0x6 << 3)
#define RK616_MICBIAS1_V_1_5			(0x5 << 3)
#define RK616_MICBIAS1_V_1_4			(0x4 << 3)
#define RK616_MICBIAS1_V_1_3			(0x3 << 3)
#define RK616_MICBIAS1_V_1_2			(0x2 << 3)
#define RK616_MICBIAS1_V_1_1			(0x1 << 3)
#define RK616_MICBIAS1_V_1_0			(0x0 << 3)

#define RK616_MICBIAS2_V_MASK			0x7
#define RK616_MICBIAS2_V_SFT			0
#define RK616_MICBIAS2_V_1_7			0x7
#define RK616_MICBIAS2_V_1_6			0x6
#define RK616_MICBIAS2_V_1_5			0x5
#define RK616_MICBIAS2_V_1_4			0x4
#define RK616_MICBIAS2_V_1_3			0x3
#define RK616_MICBIAS2_V_1_2			0x2
#define RK616_MICBIAS2_V_1_1			0x1
#define RK616_MICBIAS2_V_1_0			0x0

/* MIC Key Detection Control (0xa0) */
#define RK616_MK1_DET_MASK			(0x1 << 7)
#define RK616_MK1_DET_SFT			7
#define RK616_MK1_EN				(0x1 << 7)
#define RK616_MK1_DIS				(0x0 << 7)

#define RK616_MK2_DET_MASK			(0x1 << 6)
#define RK616_MK2_DET_SFT			6
#define RK616_MK2_EN				(0x1 << 6)
#define RK616_MK2_DIS				(0x0 << 6)

#define RK616_MK1_DET_I_MASK			(0x7 << 3)
#define RK616_MK1_DET_I_SFT			3
#define RK616_MK1_DET_I_1500			(0x7 << 3)
#define RK616_MK1_DET_I_1300			(0x6 << 3)
#define RK616_MK1_DET_I_1100			(0x5 << 3)
#define RK616_MK1_DET_I_900			(0x4 << 3)
#define RK616_MK1_DET_I_700			(0x3 << 3)
#define RK616_MK1_DET_I_500			(0x2 << 3)
#define RK616_MK1_DET_I_300			(0x1 << 3)
#define RK616_MK1_DET_I_100			(0x0 << 3)

#define RK616_MK2_DET_I_MASK			0x7
#define RK616_MK2_DET_I_SFT			0
#define RK616_MK2_DET_I_1500			0x7
#define RK616_MK2_DET_I_1300			0x6
#define RK616_MK2_DET_I_1100			0x5
#define RK616_MK2_DET_I_900			0x4
#define RK616_MK2_DET_I_700			0x3
#define RK616_MK2_DET_I_500			0x2
#define RK616_MK2_DET_I_300			0x1
#define RK616_MK2_DET_I_100			0x0

/* Power Management Addition 3 (0xa4) */
#define RK616_ADCL_ZO_PWRD			(0x1 << 3)
#define RK616_ADCL_ZO_PWRD_SFT			3

#define RK616_ADCR_ZO_PWRD			(0x1 << 2)
#define RK616_ADCR_ZO_PWRD_SFT			2

#define RK616_DACL_ZO_PWRD			(0x1 << 1)
#define RK616_DACL_ZO_PWRD_SFT			1

#define RK616_DACR_ZO_PWRD			0x1
#define RK616_DACR_ZO_PWRD_SFT			0

/* ADC control (0xa8) */
#define RK616_ADCL_CLK_PWRD			(0x1 << 5)
#define RK616_ADCL_CLK_PWRD_SFT			5

#define RK616_ADCL_PWRD				(0x1 << 4)
#define RK616_ADCL_PWRD_SFT			4

#define RK616_ADCL_CLEAR_MASK			(0x1 << 3)//clear buf
#define RK616_ADCL_CLEAR_SFT			3
#define RK616_ADCL_CLEAR_EN			(0x1 << 3)
#define RK616_ADCL_CLEAR_DIS			(0x0 << 3)

#define RK616_ADCR_CLK_PWRD			(0x1 << 2)
#define RK616_ADCR_CLK_PWRD_SFT			2

#define RK616_ADCR_PWRD				(0x1 << 1)
#define RK616_ADCR_PWRD_SFT			1

#define RK616_ADCR_CLEAR_MASK			0x1//clear buf
#define RK616_ADCR_CLEAR_SFT			0
#define RK616_ADCR_CLEAR_EN			0x1
#define RK616_ADCR_CLEAR_DIS			0x0

/* PGA AGC control 1 (0xc0 0x110) */
#define RK616_PGA_AGC_WAY_MASK			(0x1 << 4)
#define RK616_PGA_AGC_WAY_SFT			4
#define RK616_PGA_AGC_WAY_JACK			(0x1 << 4)
#define RK616_PGA_AGC_WAY_NOR			(0x0 << 4)

#define RK616_PGA_AGC_HOLD_T_MASK		0xf
#define RK616_PGA_AGC_HOLD_T_SFT		0
#define RK616_PGA_AGC_HOLD_T_1024		0xa
#define RK616_PGA_AGC_HOLD_T_512		0x9
#define RK616_PGA_AGC_HOLD_T_256		0x8
#define RK616_PGA_AGC_HOLD_T_128		0x7
#define RK616_PGA_AGC_HOLD_T_64			0x6
#define RK616_PGA_AGC_HOLD_T_32			0x5
#define RK616_PGA_AGC_HOLD_T_16			0x4
#define RK616_PGA_AGC_HOLD_T_8			0x3
#define RK616_PGA_AGC_HOLD_T_4			0x2
#define RK616_PGA_AGC_HOLD_T_2			0x1
#define RK616_PGA_AGC_HOLD_T_0			0x0

/* PGA AGC control 2 (0xc4 0x104) */
#define RK616_PGA_AGC_GRU_T_MASK		(0xf << 4)
#define RK616_PGA_AGC_GRU_T_SFT			4
#define RK616_PGA_AGC_GRU_T_512			(0xa << 4)
#define RK616_PGA_AGC_GRU_T_256			(0x9 << 4)
#define RK616_PGA_AGC_GRU_T_128			(0x8 << 4)
#define RK616_PGA_AGC_GRU_T_64			(0x7 << 4)
#define RK616_PGA_AGC_GRU_T_32			(0x6 << 4)
#define RK616_PGA_AGC_GRU_T_16			(0x5 << 4)
#define RK616_PGA_AGC_GRU_T_8			(0x4 << 4)
#define RK616_PGA_AGC_GRU_T_4			(0x3 << 4)
#define RK616_PGA_AGC_GRU_T_2			(0x2 << 4)
#define RK616_PGA_AGC_GRU_T_1			(0x1 << 4)
#define RK616_PGA_AGC_GRU_T_0_5			(0x0 << 4)

#define RK616_PGA_AGC_GRD_T_MASK		0xf
#define RK616_PGA_AGC_GRD_T_SFT			0
#define RK616_PGA_AGC_GRD_T_128_32		0xa
#define RK616_PGA_AGC_GRD_T_64_16		0x9
#define RK616_PGA_AGC_GRD_T_32_8		0x8
#define RK616_PGA_AGC_GRD_T_16_4		0x7
#define RK616_PGA_AGC_GRD_T_8_2			0x6
#define RK616_PGA_AGC_GRD_T_4_1			0x5
#define RK616_PGA_AGC_GRD_T_2_0_512		0x4
#define RK616_PGA_AGC_GRD_T_1_0_256		0x3
#define RK616_PGA_AGC_GRD_T_0_500_128		0x2
#define RK616_PGA_AGC_GRD_T_0_250_64		0x1
#define RK616_PGA_AGC_GRD_T_0_125_32		0x0

/* PGA AGC control 3 (0xc8 0x108) */
#define RK616_PGA_AGC_MODE_MASK			(0x1 << 7)
#define RK616_PGA_AGC_MODE_SFT			7
#define RK616_PGA_AGC_MODE_LIMIT		(0x1 << 7)
#define RK616_PGA_AGC_MODE_NOR			(0x0 << 7)

#define RK616_PGA_AGC_ZO_MASK			(0x1 << 6)
#define RK616_PGA_AGC_ZO_SFT			6
#define RK616_PGA_AGC_ZO_EN			(0x1 << 6)
#define RK616_PGA_AGC_ZO_DIS			(0x0 << 6)

#define RK616_PGA_AGC_REC_MODE_MASK		(0x1 << 5)
#define RK616_PGA_AGC_REC_MODE_SFT		5
#define RK616_PGA_AGC_REC_MODE_AC		(0x1 << 5)
#define RK616_PGA_AGC_REC_MODE_RN		(0x0 << 5)

#define RK616_PGA_AGC_FAST_D_MASK		(0x1 << 4)
#define RK616_PGA_AGC_FAST_D_SFT		4
#define RK616_PGA_AGC_FAST_D_EN			(0x1 << 4)
#define RK616_PGA_AGC_FAST_D_DIS		(0x0 << 4)

#define RK616_PGA_AGC_NG_MASK			(0x1 << 3)
#define RK616_PGA_AGC_NG_SFT			3
#define RK616_PGA_AGC_NG_EN			(0x1 << 3)
#define RK616_PGA_AGC_NG_DIS			(0x0 << 3)

#define RK616_PGA_AGC_NG_THR_MASK		0x7
#define RK616_PGA_AGC_NG_THR_SFT		0
#define RK616_PGA_AGC_NG_THR_N81DB		0x7
#define RK616_PGA_AGC_NG_THR_N75DB		0x6
#define RK616_PGA_AGC_NG_THR_N69DB		0x5
#define RK616_PGA_AGC_NG_THR_N63DB		0x4
#define RK616_PGA_AGC_NG_THR_N57DB		0x3
#define RK616_PGA_AGC_NG_THR_N51DB		0x2
#define RK616_PGA_AGC_NG_THR_N45DB		0x1
#define RK616_PGA_AGC_NG_THR_N39DB		0x0

/* PGA AGC Control 4 (0xcc 0x10c) */
#define RK616_PGA_AGC_ZO_MODE_MASK		(0x1 << 5)
#define RK616_PGA_AGC_ZO_MODE_SFT		5
#define RK616_PGA_AGC_ZO_MODE_UWRC		(0x1 << 5)
#define RK616_PGA_AGC_ZO_MODE_UARC		(0x0 << 5)

#define RK616_PGA_AGC_VOL_MASK			0x1f
#define RK616_PGA_AGC_VOL_SFT			0

/* PGA ASR Control (0xd0 0x110) */
#define RK616_PGA_SLOW_CLK_MASK			(0x1 << 3)
#define RK616_PGA_SLOW_CLK_SFT			3
#define RK616_PGA_SLOW_CLK_EN			(0x1 << 3)
#define RK616_PGA_SLOW_CLK_DIS			(0x0 << 3)

#define RK616_PGA_ASR_MASK			0x7
#define RK616_PGA_ASR_SFT			0
#define RK616_PGA_ASR_8KHz			0x5
#define RK616_PGA_ASR_12KHz			0x4
#define RK616_PGA_ASR_16KHz			0x3
#define RK616_PGA_ASR_24KHz			0x2
#define RK616_PGA_ASR_32KHz			0x1
#define RK616_PGA_ASR_48KHz			0x0

/* PGA AGC Control 5 (0xe4 0x124) */
#define RK616_PGA_AGC_MASK			(0x1 << 6)
#define RK616_PGA_AGC_SFT			6
#define RK616_PGA_AGC_EN			(0x1 << 6)
#define RK616_PGA_AGC_DIS			(0x0 << 6)

#define RK616_PGA_AGC_MAX_G_MASK		(0x7 << 3)
#define RK616_PGA_AGC_MAX_G_SFT			3
#define RK616_PGA_AGC_MAX_G_28_5DB		(0x7 << 3)
#define RK616_PGA_AGC_MAX_G_22_5DB		(0x6 << 3)
#define RK616_PGA_AGC_MAX_G_16_5DB		(0x5 << 3)
#define RK616_PGA_AGC_MAX_G_10_5DB		(0x4 << 3)
#define RK616_PGA_AGC_MAX_G_4_5DB		(0x3 << 3)
#define RK616_PGA_AGC_MAX_G_N1_5DB		(0x2 << 3)
#define RK616_PGA_AGC_MAX_G_N7_5DB		(0x1 << 3)
#define RK616_PGA_AGC_MAX_G_N13_5DB		(0x0 << 3)

#define RK616_PGA_AGC_MIN_G_MASK		0x7
#define RK616_PGA_AGC_MIN_G_SFT			0
#define RK616_PGA_AGC_MIN_G_24DB		0x7
#define RK616_PGA_AGC_MIN_G_18DB		0x6
#define RK616_PGA_AGC_MIN_G_12DB		0x5
#define RK616_PGA_AGC_MIN_G_6DB			0x4
#define RK616_PGA_AGC_MIN_G_0DB			0x3
#define RK616_PGA_AGC_MIN_G_N6DB		0x2
#define RK616_PGA_AGC_MIN_G_N12DB		0x1
#define RK616_PGA_AGC_MIN_G_N18DB		0x0

enum {
	RK616_HIFI,
	RK616_VOICE,
};

enum {
	RK616_MONO = 1,
	RK616_STEREO,
};

enum {
	OFF,
	RCV,
	SPK_PATH,
	HP_PATH,
	HP_NO_MIC,
	BT,
	SPK_HP,
	RING_SPK,
	RING_HP,
	RING_HP_NO_MIC,
	RING_SPK_HP,
};

enum {
	MIC_OFF,
	Main_Mic,
	Hands_Free_Mic,
	BT_Sco_Mic,
};

struct rk616_reg_val_typ {
	unsigned int reg;
	unsigned int value;
};

struct rk616_init_bit_typ {
	unsigned int reg;
	unsigned int power_bit;
	unsigned int init_bit;
};

bool get_hdmi_state(void);

#endif //__RK616_CODEC_H__
