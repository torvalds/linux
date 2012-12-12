/*
 * rt5631.h  --  RT5631 ALSA SoC audio driver
 *
 * Copyright 2011 Realtek Microelectronics
 * Author: flove <flove@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5631_H__
#define __RT5631_H__


#define RT5631_RESET				0x00
#define RT5631_SPK_OUT_VOL			0x02
#define RT5631_HP_OUT_VOL			0x04
#define RT5631_MONO_AXO_1_2_VOL		0x06
#define RT5631_AUX_IN_VOL			0x0A
#define RT5631_STEREO_DAC_VOL_1		0x0C
#define RT5631_MIC_CTRL_1			0x0E
#define RT5631_STEREO_DAC_VOL_2		0x10
#define RT5631_ADC_CTRL_1			0x12
#define RT5631_ADC_REC_MIXER			0x14
#define RT5631_ADC_CTRL_2			0x16
#define RT5631_VDAC_DIG_VOL			0x18
#define RT5631_OUTMIXER_L_CTRL			0x1A
#define RT5631_OUTMIXER_R_CTRL			0x1C
#define RT5631_AXO1MIXER_CTRL			0x1E
#define RT5631_AXO2MIXER_CTRL			0x20
#define RT5631_MIC_CTRL_2			0x22
#define RT5631_DIG_MIC_CTRL			0x24
#define RT5631_MONO_INPUT_VOL			0x26
#define RT5631_SPK_MIXER_CTRL			0x28
#define RT5631_SPK_MONO_OUT_CTRL		0x2A
#define RT5631_SPK_MONO_HP_OUT_CTRL		0x2C
#define RT5631_SDP_CTRL				0x34
#define RT5631_MONO_SDP_CTRL			0x36
#define RT5631_STEREO_AD_DA_CLK_CTRL		0x38
#define RT5631_PWR_MANAG_ADD1		0x3A
#define RT5631_PWR_MANAG_ADD2		0x3B
#define RT5631_PWR_MANAG_ADD3		0x3C
#define RT5631_PWR_MANAG_ADD4		0x3E
#define RT5631_GEN_PUR_CTRL_REG		0x40
#define RT5631_GLOBAL_CLK_CTRL			0x42
#define RT5631_PLL_CTRL				0x44
#define RT5631_PLL2_CTRL			0x46
#define RT5631_INT_ST_IRQ_CTRL_1		0x48
#define RT5631_INT_ST_IRQ_CTRL_2		0x4A
#define RT5631_GPIO_CTRL			0x4C
#define RT5631_MISC_CTRL			0x52
#define RT5631_DEPOP_FUN_CTRL_1		0x54
#define RT5631_DEPOP_FUN_CTRL_2		0x56
#define RT5631_JACK_DET_CTRL			0x5A
#define RT5631_SOFT_VOL_CTRL			0x5C
#define RT5631_ALC_CTRL_1			0x64
#define RT5631_ALC_CTRL_2			0x65
#define RT5631_ALC_CTRL_3			0x66
#define RT5631_PSEUDO_SPATL_CTRL		0x68
#define RT5631_INDEX_ADD			0x6A
#define RT5631_INDEX_DATA			0x6C
#define RT5631_EQ_CTRL				0x6E
#define RT5631_VENDOR_ID			0x7A
#define RT5631_VENDOR_ID1			0x7C
#define RT5631_VENDOR_ID2			0x7E

/* Index of Codec Private Register definition */
#define RT5631_EQ_BW_LOP			0x00
#define RT5631_EQ_GAIN_LOP			0x01
#define RT5631_EQ_FC_BP1			0x02
#define RT5631_EQ_BW_BP1			0x03
#define RT5631_EQ_GAIN_BP1			0x04
#define RT5631_EQ_FC_BP2			0x05
#define RT5631_EQ_BW_BP2			0x06
#define RT5631_EQ_GAIN_BP2			0x07
#define RT5631_EQ_FC_BP3			0x08
#define RT5631_EQ_BW_BP3			0x09
#define RT5631_EQ_GAIN_BP3			0x0a
#define RT5631_EQ_BW_HIP			0x0b
#define RT5631_EQ_GAIN_HIP			0x0c
#define RT5631_EQ_HPF_A1			0x0d
#define RT5631_EQ_HPF_A2			0x0e
#define RT5631_EQ_HPF_GAIN			0x0f
#define RT5631_EQ_PRE_VOL_CTRL			0x11
#define RT5631_EQ_POST_VOL_CTRL		0x12
#define RT5631_AVC_REG1			0x21
#define RT5631_AVC_REG2			0x22
#define RT5631_ALC_REG3				0x23
#define RT5631_TEST_MODE_CTRL			0x39
#define RT5631_CP_INTL_REG2			0x45
#define RT5631_ADDA_MIXER_INTL_REG3		0x52
#define RT5631_SPK_INTL_CTRL			0x56


/* global definition */
#define RT5631_L_MUTE					(0x1 << 15)
#define RT5631_L_MUTE_SHIFT				15
#define RT5631_L_EN					(0x1 << 14)
#define RT5631_L_EN_SHIFT				14
#define RT5631_R_MUTE					(0x1 << 7)
#define RT5631_R_MUTE_SHIFT				7
#define RT5631_R_EN					(0x1 << 6)
#define RT5631_R_EN_SHIFT				6
#define RT5631_VOL_MASK				0x1f
#define RT5631_L_VOL_SHIFT				8
#define RT5631_R_VOL_SHIFT				0

/* Speaker Output Control(0x02) */
#define RT5631_SPK_L_VOL_SEL_MASK			(0x1 << 14)
#define RT5631_SPK_L_VOL_SEL_VMID			(0x0 << 14)
#define RT5631_SPK_L_VOL_SEL_SPKMIX_L			(0x1 << 14)
#define RT5631_SPK_R_VOL_SEL_MASK			(0x1 << 6)
#define RT5631_SPK_R_VOL_SEL_VMID			(0x0 << 6)
#define RT5631_SPK_R_VOL_SEL_SPKMIX_R			(0x1 << 6)

/* Headphone Output Control(0x04) */
#define RT5631_HP_L_VOL_SEL_MASK			(0x1 << 14)
#define RT5631_HP_L_VOL_SEL_VMID			(0x0 << 14)
#define RT5631_HP_L_VOL_SEL_OUTMIX_L			(0x1 << 14)
#define RT5631_HP_R_VOL_SEL_MASK			(0x1 << 6)
#define RT5631_HP_R_VOL_SEL_VMID			(0x0 << 6)
#define RT5631_HP_R_VOL_SEL_OUTMIX_R			(0x1 << 6)

/* Output Control for AUXOUT/MONO(0x06) */
#define RT5631_AUXOUT_1_VOL_SEL_MASK			(0x1 << 14)
#define RT5631_AUXOUT_1_VOL_SEL_VMID			(0x0 << 14)
#define RT5631_AUXOUT_1_VOL_SEL_OUTMIX_L		(0x1 << 14)
#define RT5631_MUTE_MONO				(0x1 << 13)
#define RT5631_MUTE_MONO_SHIFT			13
#define RT5631_AUXOUT_2_VOL_SEL_MASK			(0x1 << 6)
#define RT5631_AUXOUT_2_VOL_SEL_VMID			(0x0 << 6)
#define RT5631_AUXOUT_2_VOL_SEL_OUTMIX_R		(0x1 << 6)

/* Microphone Input Control 1(0x0E) */
#define RT5631_MIC1_DIFF_INPUT_CTRL			(0x1 << 15)
#define RT5631_MIC1_DIFF_INPUT_SHIFT			15
#define RT5631_MIC2_DIFF_INPUT_CTRL			(0x1 << 7)
#define RT5631_MIC2_DIFF_INPUT_SHIFT			7

/* Stereo DAC Digital Volume2(0x10) */
#define RT5631_DAC_VOL_MASK				0xff

/* ADC Recording Mixer Control(0x14) */
#define RT5631_M_OUTMIXER_L_TO_RECMIXER_L		(0x1 << 15)
#define RT5631_M_OUTMIXL_RECMIXL_BIT			15
#define RT5631_M_MIC1_TO_RECMIXER_L			(0x1 << 14)
#define RT5631_M_MIC1_RECMIXL_BIT			14
#define RT5631_M_AXIL_TO_RECMIXER_L			(0x1 << 13)
#define RT5631_M_AXIL_RECMIXL_BIT			13
#define RT5631_M_MONO_IN_TO_RECMIXER_L		(0x1 << 12)
#define RT5631_M_MONO_IN_RECMIXL_BIT			12
#define RT5631_M_OUTMIXER_R_TO_RECMIXER_R		(0x1 << 7)
#define RT5631_M_OUTMIXR_RECMIXR_BIT			7
#define RT5631_M_MIC2_TO_RECMIXER_R			(0x1 << 6)
#define RT5631_M_MIC2_RECMIXR_BIT			6
#define RT5631_M_AXIR_TO_RECMIXER_R			(0x1 << 5)
#define RT5631_M_AXIR_RECMIXR_BIT			5
#define RT5631_M_MONO_IN_TO_RECMIXER_R		(0x1 << 4)
#define RT5631_M_MONO_IN_RECMIXR_BIT			4

/* Left Output Mixer Control(0x1A) */
#define RT5631_M_RECMIXER_L_TO_OUTMIXER_L		(0x1 << 15)
#define RT5631_M_RECMIXL_OUTMIXL_BIT			15
#define RT5631_M_RECMIXER_R_TO_OUTMIXER_L		(0x1 << 14)
#define RT5631_M_RECMIXR_OUTMIXL_BIT			14
#define RT5631_M_DAC_L_TO_OUTMIXER_L			(0x1 << 13)
#define RT5631_M_DACL_OUTMIXL_BIT			13
#define RT5631_M_MIC1_TO_OUTMIXER_L			(0x1 << 12)
#define RT5631_M_MIC1_OUTMIXL_BIT			12
#define RT5631_M_MIC2_TO_OUTMIXER_L			(0x1 << 11)
#define RT5631_M_MIC2_OUTMIXL_BIT			11
#define RT5631_M_MONO_IN_P_TO_OUTMIXER_L		(0x1 << 10)
#define RT5631_M_MONO_INP_OUTMIXL_BIT		10
#define RT5631_M_AXIL_TO_OUTMIXER_L			(0x1 << 9)
#define RT5631_M_AXIL_OUTMIXL_BIT			9
#define RT5631_M_AXIR_TO_OUTMIXER_L			(0x1 << 8)
#define RT5631_M_AXIR_OUTMIXL_BIT			8
#define RT5631_M_VDAC_TO_OUTMIXER_L			(0x1 << 7)
#define RT5631_M_VDAC_OUTMIXL_BIT			7

/* Right Output Mixer Control(0x1C) */
#define RT5631_M_RECMIXER_L_TO_OUTMIXER_R		(0x1 << 15)
#define RT5631_M_RECMIXL_OUTMIXR_BIT			15
#define RT5631_M_RECMIXER_R_TO_OUTMIXER_R		(0x1 << 14)
#define RT5631_M_RECMIXR_OUTMIXR_BIT			14
#define RT5631_M_DAC_R_TO_OUTMIXER_R			(0x1 << 13)
#define RT5631_M_DACR_OUTMIXR_BIT			13
#define RT5631_M_MIC1_TO_OUTMIXER_R			(0x1 << 12)
#define RT5631_M_MIC1_OUTMIXR_BIT			12
#define RT5631_M_MIC2_TO_OUTMIXER_R			(0x1 << 11)
#define RT5631_M_MIC2_OUTMIXR_BIT			11
#define RT5631_M_MONO_IN_N_TO_OUTMIXER_R		(0x1 << 10)
#define RT5631_M_MONO_INN_OUTMIXR_BIT		10
#define RT5631_M_AXIL_TO_OUTMIXER_R			(0x1 << 9)
#define RT5631_M_AXIL_OUTMIXR_BIT			9
#define RT5631_M_AXIR_TO_OUTMIXER_R			(0x1 << 8)
#define RT5631_M_AXIR_OUTMIXR_BIT			8
#define RT5631_M_VDAC_TO_OUTMIXER_R			(0x1 << 7)
#define RT5631_M_VDAC_OUTMIXR_BIT			7

/* Lout Mixer Control(0x1E) */
#define RT5631_M_MIC1_TO_AXO1MIXER			(0x1 << 15)
#define RT5631_M_MIC1_AXO1MIX_BIT			15
#define RT5631_M_MIC2_TO_AXO1MIXER			(0x1 << 11)
#define RT5631_M_MIC2_AXO1MIX_BIT			11
#define RT5631_M_OUTMIXER_L_TO_AXO1MIXER		(0x1 << 7)
#define RT5631_M_OUTMIXL_AXO1MIX_BIT			7
#define RT5631_M_OUTMIXER_R_TO_AXO1MIXER		(0x1 << 6)
#define RT5631_M_OUTMIXR_AXO1MIX_BIT			6

/* Rout Mixer Control(0x20) */
#define RT5631_M_MIC1_TO_AXO2MIXER			(0x1 << 15)
#define RT5631_M_MIC1_AXO2MIX_BIT			15
#define RT5631_M_MIC2_TO_AXO2MIXER			(0x1 << 11)
#define RT5631_M_MIC2_AXO2MIX_BIT			11
#define RT5631_M_OUTMIXER_L_TO_AXO2MIXER		(0x1 << 7)
#define RT5631_M_OUTMIXL_AXO2MIX_BIT			7
#define RT5631_M_OUTMIXER_R_TO_AXO2MIXER		(0x1 << 6)
#define RT5631_M_OUTMIXR_AXO2MIX_BIT			6

/* Micphone Input Control 2(0x22) */
#define RT5631_MIC_BIAS_90_PRECNET_AVDD 		1
#define RT5631_MIC_BIAS_75_PRECNET_AVDD 		2

#define RT5631_MIC1_BOOST_CTRL_MASK			(0xf << 12)
#define RT5631_MIC1_BOOST_CTRL_BYPASS		(0x0 << 12)
#define RT5631_MIC1_BOOST_CTRL_20DB			(0x1 << 12)
#define RT5631_MIC1_BOOST_CTRL_24DB			(0x2 << 12)
#define RT5631_MIC1_BOOST_CTRL_30DB			(0x3 << 12)
#define RT5631_MIC1_BOOST_CTRL_35DB			(0x4 << 12)
#define RT5631_MIC1_BOOST_CTRL_40DB			(0x5 << 12)
#define RT5631_MIC1_BOOST_CTRL_34DB			(0x6 << 12)
#define RT5631_MIC1_BOOST_CTRL_50DB			(0x7 << 12)
#define RT5631_MIC1_BOOST_CTRL_52DB			(0x8 << 12)
#define RT5631_MIC1_BOOST_SHIFT			12

#define RT5631_MIC2_BOOST_CTRL_MASK			(0xf << 8)
#define RT5631_MIC2_BOOST_CTRL_BYPASS		(0x0 << 8)
#define RT5631_MIC2_BOOST_CTRL_20DB			(0x1 << 8)
#define RT5631_MIC2_BOOST_CTRL_24DB			(0x2 << 8)
#define RT5631_MIC2_BOOST_CTRL_30DB			(0x3 << 8)
#define RT5631_MIC2_BOOST_CTRL_35DB			(0x4 << 8)
#define RT5631_MIC2_BOOST_CTRL_40DB			(0x5 << 8)
#define RT5631_MIC2_BOOST_CTRL_34DB			(0x6 << 8)
#define RT5631_MIC2_BOOST_CTRL_50DB			(0x7 << 8)
#define RT5631_MIC2_BOOST_CTRL_52DB			(0x8 << 8)
#define RT5631_MIC2_BOOST_SHIFT			8

#define RT5631_MICBIAS1_VOLT_CTRL_MASK		(0x1 << 7)
#define RT5631_MICBIAS1_VOLT_CTRL_90P			(0x0 << 7)
#define RT5631_MICBIAS1_VOLT_CTRL_75P			(0x1 << 7)

#define RT5631_MICBIAS1_S_C_DET_MASK			(0x1 << 6)
#define RT5631_MICBIAS1_S_C_DET_DIS			(0x0 << 6)
#define RT5631_MICBIAS1_S_C_DET_ENA			(0x1 << 6)

#define RT5631_MICBIAS1_SHORT_CURR_DET_MASK		(0x3 << 4)
#define RT5631_MICBIAS1_SHORT_CURR_DET_600UA	(0x0 << 4)
#define RT5631_MICBIAS1_SHORT_CURR_DET_1500UA	(0x1 << 4)
#define RT5631_MICBIAS1_SHORT_CURR_DET_2000UA	(0x2 << 4)

#define RT5631_MICBIAS2_VOLT_CTRL_MASK		(0x1 << 3)
#define RT5631_MICBIAS2_VOLT_CTRL_90P			(0x0 << 3)
#define RT5631_MICBIAS2_VOLT_CTRL_75P			(0x1 << 3)

#define RT5631_MICBIAS2_S_C_DET_MASK			(0x1 << 2)
#define RT5631_MICBIAS2_S_C_DET_DIS			(0x0 << 2)
#define RT5631_MICBIAS2_S_C_DET_ENA			(0x1 << 2)

#define RT5631_MICBIAS2_SHORT_CURR_DET_MASK		(0x3)
#define RT5631_MICBIAS2_SHORT_CURR_DET_600UA	(0x0)
#define RT5631_MICBIAS2_SHORT_CURR_DET_1500UA	(0x1)
#define RT5631_MICBIAS2_SHORT_CURR_DET_2000UA	(0x2)


/* Digital Microphone Control(0x24) */
#define RT5631_DMIC_ENA_MASK				(0x1 << 15)
#define RT5631_DMIC_ENA_SHIFT				15
/* DMIC_ENA: DMIC to ADC Digital filter */
#define RT5631_DMIC_ENA				(0x1 << 15)
/* DMIC_DIS: ADC mixer to ADC Digital filter */
#define RT5631_DMIC_DIS					(0x0 << 15)
#define RT5631_DMIC_L_CH_MUTE				(0x1 << 13)
#define RT5631_DMIC_L_CH_MUTE_SHIFT			13
#define RT5631_DMIC_R_CH_MUTE				(0x1 << 12)
#define RT5631_DMIC_R_CH_MUTE_SHIFT			12
#define RT5631_DMIC_L_CH_LATCH_MASK			(0x1 << 9)
#define RT5631_DMIC_L_CH_LATCH_RISING			(0x1 << 9)
#define RT5631_DMIC_L_CH_LATCH_FALLING		(0x0 << 9)
#define RT5631_DMIC_R_CH_LATCH_MASK			(0x1 << 8)
#define RT5631_DMIC_R_CH_LATCH_RISING			(0x1 << 8)
#define RT5631_DMIC_R_CH_LATCH_FALLING		(0x0 << 8)
#define RT5631_DMIC_CLK_CTRL_MASK			(0x3 << 4)
#define RT5631_DMIC_CLK_CTRL_TO_128FS			(0x0 << 4)
#define RT5631_DMIC_CLK_CTRL_TO_64FS			(0x1 << 4)
#define RT5631_DMIC_CLK_CTRL_TO_32FS			(0x2 << 4)

/* Microphone Input Volume(0x26) */
#define RT5631_MONO_DIFF_INPUT_SHIFT			15

/* Speaker Mixer Control(0x28) */
#define RT5631_M_RECMIXER_L_TO_SPKMIXER_L		(0x1 << 15)
#define RT5631_M_RECMIXL_SPKMIXL_BIT			15
#define RT5631_M_MIC1_P_TO_SPKMIXER_L		(0x1 << 14)
#define RT5631_M_MIC1P_SPKMIXL_BIT			14
#define RT5631_M_DAC_L_TO_SPKMIXER_L			(0x1 << 13)
#define RT5631_M_DACL_SPKMIXL_BIT			13
#define RT5631_M_OUTMIXER_L_TO_SPKMIXER_L		(0x1 << 12)
#define RT5631_M_OUTMIXL_SPKMIXL_BIT			12

#define RT5631_M_RECMIXER_R_TO_SPKMIXER_R		(0x1 << 7)
#define RT5631_M_RECMIXR_SPKMIXR_BIT			7
#define RT5631_M_MIC2_P_TO_SPKMIXER_R		(0x1 << 6)
#define RT5631_M_MIC2P_SPKMIXR_BIT			6
#define RT5631_M_DAC_R_TO_SPKMIXER_R			(0x1 << 5)
#define RT5631_M_DACR_SPKMIXR_BIT			5
#define RT5631_M_OUTMIXER_R_TO_SPKMIXER_R		(0x1 << 4)
#define RT5631_M_OUTMIXR_SPKMIXR_BIT			4

/* Speaker/Mono Output Control(0x2A) */
#define RT5631_M_SPKVOL_L_TO_SPOL_MIXER		(0x1 << 15)
#define RT5631_M_SPKVOLL_SPOLMIX_BIT			15
#define RT5631_M_SPKVOL_R_TO_SPOL_MIXER		(0x1 << 14)
#define RT5631_M_SPKVOLR_SPOLMIX_BIT			14
#define RT5631_M_SPKVOL_L_TO_SPOR_MIXER		(0x1 << 13)
#define RT5631_M_SPKVOLL_SPORMIX_BIT			13
#define RT5631_M_SPKVOL_R_TO_SPOR_MIXER		(0x1 << 12)
#define RT5631_M_SPKVOLR_SPORMIX_BIT			12
#define RT5631_M_OUTVOL_L_TO_MONOMIXER		(0x1 << 11)
#define RT5631_M_OUTVOLL_MONOMIX_BIT			11
#define RT5631_M_OUTVOL_R_TO_MONOMIXER		(0x1 << 10)
#define RT5631_M_OUTVOLR_MONOMIX_BIT			10

/* Speaker/Mono/HP Output Control(0x2C) */
#define RT5631_SPK_L_MUX_SEL_MASK			(0x3 << 14)
#define RT5631_SPK_L_MUX_SEL_SPKMIXER_L		(0x0 << 14)
#define RT5631_SPK_L_MUX_SEL_MONO_IN			(0x1 << 14)
#define RT5631_SPK_L_MUX_SEL_DAC_L			(0x3 << 14)
#define RT5631_SPK_L_MUX_SEL_SHIFT			14

#define RT5631_SPK_R_MUX_SEL_MASK			(0x3 << 10)
#define RT5631_SPK_R_MUX_SEL_SPKMIXER_R		(0x0 << 10)
#define RT5631_SPK_R_MUX_SEL_MONO_IN			(0x1 << 10)
#define RT5631_SPK_R_MUX_SEL_DAC_R			(0x3 << 10)
#define RT5631_SPK_R_MUX_SEL_SHIFT			10

#define RT5631_MONO_MUX_SEL_MASK			(0x3 << 6)
#define RT5631_MONO_MUX_SEL_MONOMIXER		(0x0 << 6)
#define RT5631_MONO_MUX_SEL_MONO_IN			(0x1 << 6)
#define RT5631_MONO_MUX_SEL_SHIFT			6

#define RT5631_HP_L_MUX_SEL_MASK			(0x1 << 3)
#define RT5631_HP_L_MUX_SEL_HPVOL_L			(0x0 << 3)
#define RT5631_HP_L_MUX_SEL_DAC_L			(0x1 << 3)
#define RT5631_HP_L_MUX_SEL_SHIFT			3

#define RT5631_HP_R_MUX_SEL_MASK			(0x1 << 2)
#define RT5631_HP_R_MUX_SEL_HPVOL_R			(0x0 << 2)
#define RT5631_HP_R_MUX_SEL_DAC_R			(0x1 << 2)
#define RT5631_HP_R_MUX_SEL_SHIFT			2

/* Stereo I2S Serial Data Port Control(0x34) */
#define RT5631_SDP_MODE_SEL_MASK			(0x1 << 15)
#define RT5631_SDP_MODE_SEL_MASTER			(0x0 << 15)
#define RT5631_SDP_MODE_SEL_SLAVE			(0x1 << 15)

#define RT5631_SDP_ADC_CPS_SEL_MASK			(0x3 << 10)
#define RT5631_SDP_ADC_CPS_SEL_OFF			(0x0 << 10)
#define RT5631_SDP_ADC_CPS_SEL_U_LAW			(0x1 << 10)
#define RT5631_SDP_ADC_CPS_SEL_A_LAW			(0x2 << 10)

#define RT5631_SDP_DAC_CPS_SEL_MASK			(0x3 << 8)
#define RT5631_SDP_DAC_CPS_SEL_OFF			(0x0 << 8)
#define RT5631_SDP_DAC_CPS_SEL_U_LAW			(0x1 << 8)
#define RT5631_SDP_DAC_CPS_SEL_A_LAW			(0x2 << 8)
/* 0:Normal 1:Invert */
#define RT5631_SDP_I2S_BCLK_POL_CTRL			(0x1 << 7)
/* 0:Normal 1:Invert */
#define RT5631_SDP_DAC_R_INV				(0x1 << 6)
/* 0:ADC data appear at left phase of LRCK
 * 1:ADC data appear at right phase of LRCK
 */
#define RT5631_SDP_ADC_DATA_L_R_SWAP			(0x1 << 5)
/* 0:DAC data appear at left phase of LRCK
 * 1:DAC data appear at right phase of LRCK
 */
#define RT5631_SDP_DAC_DATA_L_R_SWAP			(0x1 << 4)

/* Data Length Slection */
#define RT5631_SDP_I2S_DL_MASK				(0x3 << 2)
#define RT5631_SDP_I2S_DL_16				(0x0 << 2)
#define RT5631_SDP_I2S_DL_20				(0x1 << 2)
#define RT5631_SDP_I2S_DL_24				(0x2 << 2)
#define RT5631_SDP_I2S_DL_8				(0x3 << 2)

/* PCM Data Format Selection */
#define RT5631_SDP_I2S_DF_MASK				(0x3)
#define RT5631_SDP_I2S_DF_I2S				(0x0)
#define RT5631_SDP_I2S_DF_LEFT				(0x1)
#define RT5631_SDP_I2S_DF_PCM_A			(0x2)
#define RT5631_SDP_I2S_DF_PCM_B			(0x3)

/* Stereo AD/DA Clock Control(0x38) */
#define RT5631_I2S_PRE_DIV_MASK			(0x7 << 13)
#define RT5631_I2S_PRE_DIV_1				(0x0 << 13)
#define RT5631_I2S_PRE_DIV_2				(0x1 << 13)
#define RT5631_I2S_PRE_DIV_4				(0x2 << 13)
#define RT5631_I2S_PRE_DIV_8				(0x3 << 13)
#define RT5631_I2S_PRE_DIV_16				(0x4 << 13)
#define RT5631_I2S_PRE_DIV_32				(0x5 << 13)
/* CLOCK RELATIVE OF BCLK AND LCRK */
#define RT5631_I2S_LRCK_SEL_N_BCLK_MASK		(0x1 << 12)
#define RT5631_I2S_LRCK_SEL_64_BCLK			(0x0 << 12)
#define RT5631_I2S_LRCK_SEL_32_BCLK			(0x1 << 12)
#define RT5631_DAC_OSR_SEL_MASK			(0x3 << 10)
#define RT5631_DAC_OSR_SEL_128FS			(0x3 << 10)
#define RT5631_DAC_OSR_SEL_64FS			(0x3 << 10)
#define RT5631_DAC_OSR_SEL_32FS			(0x3 << 10)
#define RT5631_DAC_OSR_SEL_16FS			(0x3 << 10)
#define RT5631_ADC_OSR_SEL_MASK			(0x3 << 8)
#define RT5631_ADC_OSR_SEL_128FS			(0x3 << 8)
#define RT5631_ADC_OSR_SEL_64FS			(0x3 << 8)
#define RT5631_ADC_OSR_SEL_32FS			(0x3 << 8)
#define RT5631_ADC_OSR_SEL_16FS			(0x3 << 8)
#define RT5631_ADDA_FILTER_CLK_SEL_MASK		(0x1 << 7)
#define RT5631_ADDA_FILTER_CLK_SEL_256FS		(0x0 << 7)
#define RT5631_ADDA_FILTER_CLK_SEL_384FS		(0x1 << 7)

#define RT5631_I2S_PRE_DIV2_MASK			(0x7 << 4)
#define RT5631_I2S_PRE_DIV2_1				(0x0 << 4)
#define RT5631_I2S_PRE_DIV2_2				(0x1 << 4)
#define RT5631_I2S_PRE_DIV2_4				(0x2 << 4)
#define RT5631_I2S_PRE_DIV2_8				(0x3 << 4)
#define RT5631_I2S_PRE_DIV2_16				(0x4 << 4)
#define RT5631_I2S_PRE_DIV2_32				(0x5 << 4)
#define RT5631_I2S_LRCK_SEL_N_BCLK2_MASK		(0x1 << 3)
#define RT5631_I2S_LRCK_SEL_64_BCLK2			(0x0 << 3)
#define RT5631_I2S_LRCK_SEL_32_BCLK2			(0x1 << 3)
#define RT5631_ADDA_FILTER_CLK2_SEL_MASK		(0x1 << 2)
#define RT5631_ADDA_FILTER_CLK2_SEL_256FS		(0x0 << 2)
#define RT5631_ADDA_FILTER_CLK2_SEL_384FS		(0x1 << 2)

/* Power managment addition 1 (0x3A) */
#define RT5631_PWR_MAIN_I2S_EN			(0x1 << 15)
#define RT5631_PWR_MAIN_I2S_BIT			15
#define RT5631_PWR_VOICE_I2S_EN			(0x1 << 14)
#define RT5631_PWR_VOICE_I2S_BIT			14
#define RT5631_PWR_CLASS_D				(0x1 << 12)
#define RT5631_PWR_CLASS_D_BIT			12
#define RT5631_PWR_ADC_L_CLK				(0x1 << 11)
#define RT5631_PWR_ADC_L_CLK_BIT			11
#define RT5631_PWR_ADC_R_CLK				(0x1 << 10)
#define RT5631_PWR_ADC_R_CLK_BIT			10
#define RT5631_PWR_DAC_L_CLK				(0x1 << 9)
#define RT5631_PWR_DAC_L_CLK_BIT			9
#define RT5631_PWR_DAC_R_CLK				(0x1 << 8)
#define RT5631_PWR_DAC_R_CLK_BIT			8
#define RT5631_PWR_DAC_REF				(0x1 << 7)
#define RT5631_PWR_DAC_REF_BIT			7
#define RT5631_PWR_DAC_L_TO_MIXER			(0x1 << 6)
#define RT5631_PWR_DAC_L_TO_MIXER_BIT		6
#define RT5631_PWR_DAC_R_TO_MIXER			(0x1 << 5)
#define RT5631_PWR_DAC_R_TO_MIXER_BIT		5
#define RT5631_PWR_VDAC_CLK				(0x1 << 4)
#define RT5631_PWR_VDAC_CLK_BIT			4
#define RT5631_PWR_VDAC_TO_MIXER			(0x1 << 3)
#define RT5631_PWR_VDAC_TO_MIXER_BIT			3

/* Power managment addition 2 (0x3B) */
#define RT5631_PWR_OUTMIXER_L				(0x1 << 15)
#define RT5631_PWR_OUTMIXER_L_BIT			15
#define RT5631_PWR_OUTMIXER_R				(0x1 << 14)
#define RT5631_PWR_OUTMIXER_R_BIT			14
#define RT5631_PWR_SPKMIXER_L				(0x1 << 13)
#define RT5631_PWR_SPKMIXER_L_BIT			13
#define RT5631_PWR_SPKMIXER_R				(0x1 << 12)
#define RT5631_PWR_SPKMIXER_R_BIT			12
#define RT5631_PWR_RECMIXER_L				(0x1 << 11)
#define RT5631_PWR_RECMIXER_L_BIT			11
#define RT5631_PWR_RECMIXER_R				(0x1 << 10)
#define RT5631_PWR_RECMIXER_R_BIT			10
#define RT5631_PWR_MIC1_BOOT_GAIN			(0x1 << 5)
#define RT5631_PWR_MIC1_BOOT_GAIN_BIT		5
#define RT5631_PWR_MIC2_BOOT_GAIN			(0x1 << 4)
#define RT5631_PWR_MIC2_BOOT_GAIN_BIT		4
#define RT5631_PWR_MICBIAS1_VOL			(0x1 << 3)
#define RT5631_PWR_MICBIAS1_VOL_BIT			3
#define RT5631_PWR_MICBIAS2_VOL			(0x1 << 2)
#define RT5631_PWR_MICBIAS2_VOL_BIT			2
#define RT5631_PWR_PLL1				(0x1 << 1)
#define RT5631_PWR_PLL1_BIT				1
#define RT5631_PWR_PLL2				(0x1 << 0)
#define RT5631_PWR_PLL2_BIT				0

/* Power managment addition 3(0x3C) */
#define RT5631_PWR_VREF				(0x1 << 15)
#define RT5631_PWR_VREF_BIT				15
#define RT5631_PWR_FAST_VREF_CTRL			(0x1 << 14)
#define RT5631_PWR_FAST_VREF_CTRL_BIT			14
#define RT5631_PWR_MAIN_BIAS				(0x1 << 13)
#define RT5631_PWR_MAIN_BIAS_BIT			13
#define RT5631_PWR_AXO1MIXER				(0x1 << 11)
#define RT5631_PWR_AXO1MIXER_BIT			11
#define RT5631_PWR_AXO2MIXER				(0x1 << 10)
#define RT5631_PWR_AXO2MIXER_BIT			10
#define RT5631_PWR_MONOMIXER				(0x1 << 9)
#define RT5631_PWR_MONOMIXER_BIT			9
#define RT5631_PWR_MONO_DEPOP_DIS			(0x1 << 8)
#define RT5631_PWR_MONO_DEPOP_DIS_BIT		8
#define RT5631_PWR_MONO_AMP_EN			(0x1 << 7)
#define RT5631_PWR_MONO_AMP_EN_BIT			7
#define RT5631_PWR_CHARGE_PUMP			(0x1 << 4)
#define RT5631_PWR_CHARGE_PUMP_BIT			4
#define RT5631_PWR_HP_L_AMP				(0x1 << 3)
#define RT5631_PWR_HP_L_AMP_BIT			3
#define RT5631_PWR_HP_R_AMP				(0x1 << 2)
#define RT5631_PWR_HP_R_AMP_BIT			2
#define RT5631_PWR_HP_DEPOP_DIS			(0x1 << 1)
#define RT5631_PWR_HP_DEPOP_DIS_BIT			1
#define RT5631_PWR_HP_AMP_DRIVING			(0x1 << 0)
#define RT5631_PWR_HP_AMP_DRIVING_BIT		0

/* Power managment addition 4(0x3E) */
#define RT5631_PWR_SPK_L_VOL				(0x1 << 15)
#define RT5631_PWR_SPK_L_VOL_BIT			15
#define RT5631_PWR_SPK_R_VOL				(0x1 << 14)
#define RT5631_PWR_SPK_R_VOL_BIT			14
#define RT5631_PWR_LOUT_VOL				(0x1 << 13)
#define RT5631_PWR_LOUT_VOL_BIT			13
#define RT5631_PWR_ROUT_VOL				(0x1 << 12)
#define RT5631_PWR_ROUT_VOL_BIT			12
#define RT5631_PWR_HP_L_OUT_VOL			(0x1 << 11)
#define RT5631_PWR_HP_L_OUT_VOL_BIT			11
#define RT5631_PWR_HP_R_OUT_VOL			(0x1 << 10)
#define RT5631_PWR_HP_R_OUT_VOL_BIT			10
#define RT5631_PWR_AXIL_IN_VOL				(0x1 << 9)
#define RT5631_PWR_AXIL_IN_VOL_BIT			9
#define RT5631_PWR_AXIR_IN_VOL			(0x1 << 8)
#define RT5631_PWR_AXIR_IN_VOL_BIT			8
#define RT5631_PWR_MONO_IN_P_VOL			(0x1 << 7)
#define RT5631_PWR_MONO_IN_P_VOL_BIT			7
#define RT5631_PWR_MONO_IN_N_VOL			(0x1 << 6)
#define RT5631_PWR_MONO_IN_N_VOL_BIT			6

/* General Purpose Control Register(0x40) */
#define RT5631_SPK_AMP_AUTO_RATIO_EN			(0x1 << 15)

#define RT5631_SPK_AMP_RATIO_CTRL_MASK		(0x7 << 12)
#define RT5631_SPK_AMP_RATIO_CTRL_2_34		(0x0 << 12) /* 7.40DB */
#define RT5631_SPK_AMP_RATIO_CTRL_1_99		(0x1 << 12) /* 5.99DB */
#define RT5631_SPK_AMP_RATIO_CTRL_1_68		(0x2 << 12) /* 4.50DB */
#define RT5631_SPK_AMP_RATIO_CTRL_1_56		(0x3 << 12) /* 3.86DB */
#define RT5631_SPK_AMP_RATIO_CTRL_1_44		(0x4 << 12) /* 3.16DB */
#define RT5631_SPK_AMP_RATIO_CTRL_1_27		(0x5 << 12) /* 2.10DB */
#define RT5631_SPK_AMP_RATIO_CTRL_1_09		(0x6 << 12) /* 0.80DB */
#define RT5631_SPK_AMP_RATIO_CTRL_1_00		(0x7 << 12) /* 0.00DB */
#define RT5631_SPK_AMP_RATIO_CTRL_SHIFT		12

#define RT5631_STEREO_DAC_HI_PASS_FILT_EN		(0x1 << 11)
#define RT5631_STEREO_ADC_HI_PASS_FILT_EN		(0x1 << 10)
/* Select ADC Wind Filter Clock type */
#define RT5631_ADC_WIND_FILT_MASK			(0x3 << 4)
#define RT5631_ADC_WIND_FILT_8_16_32K			(0x0 << 4)
#define RT5631_ADC_WIND_FILT_11_22_44K		(0x1 << 4)
#define RT5631_ADC_WIND_FILT_12_24_48K		(0x2 << 4)
#define RT5631_ADC_WIND_FILT_EN			(0x1 << 3)
/* SelectADC Wind Filter Corner Frequency */
#define RT5631_ADC_WIND_CNR_FREQ_MASK		(0x7 << 0)
#define RT5631_ADC_WIND_CNR_FREQ_82_113_122 	(0x0 << 0)
#define RT5631_ADC_WIND_CNR_FREQ_102_141_153 	(0x1 << 0)
#define RT5631_ADC_WIND_CNR_FREQ_131_180_156 	(0x2 << 0)
#define RT5631_ADC_WIND_CNR_FREQ_163_225_245 	(0x3 << 0)
#define RT5631_ADC_WIND_CNR_FREQ_204_281_306 	(0x4 << 0)
#define RT5631_ADC_WIND_CNR_FREQ_261_360_392 	(0x5 << 0)
#define RT5631_ADC_WIND_CNR_FREQ_327_450_490 	(0x6 << 0)
#define RT5631_ADC_WIND_CNR_FREQ_408_563_612 	(0x7 << 0)

/* Global Clock Control Register(0x42) */
#define RT5631_SYSCLK_SOUR_SEL_MASK			(0x3 << 14)
#define RT5631_SYSCLK_SOUR_SEL_MCLK			(0x0 << 14)
#define RT5631_SYSCLK_SOUR_SEL_PLL			(0x1 << 14)
#define RT5631_SYSCLK_SOUR_SEL_PLL_TCK		(0x2 << 14)
#define RT5631_PLLCLK_SOUR_SEL_MASK			(0x3 << 12)
#define RT5631_PLLCLK_SOUR_SEL_MCLK			(0x0 << 12)
#define RT5631_PLLCLK_SOUR_SEL_BCLK			(0x1 << 12)
#define RT5631_PLLCLK_SOUR_SEL_VBCLK			(0x2 << 12)
#define RT5631_PLLCLK_PRE_DIV1				(0x0 << 11)
#define RT5631_PLLCLK_PRE_DIV2				(0x1 << 11)

#define RT5631_ADCR_FUN_MASK				(0x1 << 8)
#define RT5631_ADCR_FUN_SFT				8
#define RT5631_ADCR_FUN_ADC				(0x0 << 8)
#define RT5631_ADCR_FUN_VADC				(0x1 << 8)

#define RT5631_SYSCLK2_SOUR_SEL_MASK			(0x3 << 6)
#define RT5631_SYSCLK2_SOUR_SEL_MCLK			(0x0 << 6)
#define RT5631_SYSCLK2_SOUR_SEL_PLL2			(0x1 << 6)
#define RT5631_PLLCLK2_SOUR_SEL_MASK			(0x3 << 4)
#define RT5631_PLLCLK2_SOUR_SEL_MCLK			(0x0 << 4)
#define RT5631_PLLCLK2_SOUR_SEL_BCLK			(0x1 << 4)
#define RT5631_PLLCLK2_SOUR_SEL_VBCLK			(0x2 << 4)
#define RT5631_PLLCLK2_PRE_DIV1			(0x0 << 3)
#define RT5631_PLLCLK2_PRE_DIV2			(0x1 << 3)

#define RT5631_VDAC_CLK_SOUR_MASK			(0x1)
#define RT5631_VDAC_CLK_SOUR_SFT			0
#define RT5631_VDAC_CLK_SOUR_SCLK1			(0x0)
#define RT5631_VDAC_CLK_SOUR_SCLK2			(0x1)

/* PLL Control(0x44) */
#define RT5631_PLL_CTRL_M_VAL(m)			((m)&0xf)
#define RT5631_PLL_CTRL_K_VAL(k)			(((k)&0x7) << 4)
#define RT5631_PLL_CTRL_N_VAL(n)			(((n)&0xff) << 8)

/* Internal Status and IRQ Control2(0x4A) */
#define RT5631_ADC_DATA_SEL_MASK			(0x3 << 14)
#define RT5631_ADC_DATA_SEL_SHIFT			14
#define RT5631_ADC_DATA_SEL_Disable			(0x0 << 14)
#define RT5631_ADC_DATA_SEL_MIC1			(0x1 << 14)
#define RT5631_ADC_DATA_SEL_MIC1_SHIFT		14
#define RT5631_ADC_DATA_SEL_MIC2			(0x2 << 14)
#define RT5631_ADC_DATA_SEL_MIC2_SHIFT		15
#define RT5631_ADC_DATA_SEL_SWAP			(0x3 << 14)

/* GPIO Pin Configuration(0x4C) */
#define RT5631_GPIO_PIN_FUN_SEL_MASK			(0x1 << 15)
#define RT5631_GPIO_PIN_FUN_SEL_IRQ			(0x1 << 15)
#define RT5631_GPIO_PIN_FUN_SEL_GPIO_DIMC		(0x0 << 15)

#define RT5631_GPIO_DMIC_FUN_SEL_MASK		(0x1 << 3)
#define RT5631_GPIO_DMIC_FUN_SEL_DIMC		(0x1 << 3)
#define RT5631_GPIO_DMIC_FUN_SEL_GPIO			(0x0 << 3)

#define RT5631_GPIO_PIN_CON_MASK			(0x1 << 2)
#define RT5631_GPIO_PIN_SET_INPUT			(0x0 << 2)
#define RT5631_GPIO_PIN_SET_OUTPUT			(0x1 << 2)

/* De-POP function Control 1(0x54) */
#define RT5631_POW_ON_SOFT_GEN			(0x1 << 15)
#define RT5631_EN_MUTE_UNMUTE_DEPOP			(0x1 << 14)
#define RT5631_EN_DEPOP2_FOR_HP			(0x1 << 7)
/* Power Down HPAMP_L Starts Up Signal */
#define RT5631_PD_HPAMP_L_ST_UP			(0x1 << 5)
/* Power Down HPAMP_R Starts Up Signal */
#define RT5631_PD_HPAMP_R_ST_UP			(0x1 << 4)
/* Enable left HP mute/unmute depop */
#define RT5631_EN_HP_L_M_UN_MUTE_DEPOP		(0x1 << 1)
/* Enable right HP mute/unmute depop */
#define RT5631_EN_HP_R_M_UN_MUTE_DEPOP		(0x1 << 0)

/* De-POP Fnction Control(0x56) */
#define RT5631_EN_ONE_BIT_DEPOP			(0x1 << 15)
#define RT5631_EN_CAP_FREE_DEPOP			(0x1 << 14)

/* Jack Detect Control Register(0x5A) */
#define RT5631_JD_USE_MASK				(0x3 << 14)
#define RT5631_JD_USE_JD2				(0x3 << 14)
#define RT5631_JD_USE_JD1				(0x2 << 14)
#define RT5631_JD_USE_GPIO				(0x1 << 14)
#define RT5631_JD_OFF					(0x0 << 14)
/* JD trigger enable for HP */
#define RT5631_JD_HP_EN					(0x1 << 11)
#define RT5631_JD_HP_TRI_MASK				(0x1 << 10)
#define RT5631_JD_HP_TRI_HI				(0x1 << 10)
#define RT5631_JD_HP_TRI_LO				(0x1 << 10)
/* JD trigger enable for speaker LP/LN */
#define RT5631_JD_SPK_L_EN				(0x1 << 9)
#define RT5631_JD_SPK_L_TRI_MASK			(0x1 << 8)
#define RT5631_JD_SPK_L_TRI_HI				(0x1 << 8)
#define RT5631_JD_SPK_L_TRI_LO				(0x0 << 8)
/* JD trigger enable for speaker RP/RN */
#define RT5631_JD_SPK_R_EN				(0x1 << 7)
#define RT5631_JD_SPK_R_TRI_MASK			(0x1 << 6)
#define RT5631_JD_SPK_R_TRI_HI				(0x1 << 6)
#define RT5631_JD_SPK_R_TRI_LO				(0x0 << 6)
/* JD trigger enable for monoout */
#define RT5631_JD_MONO_EN				(0x1 << 5)
#define RT5631_JD_MONO_TRI_MASK			(0x1 << 4)
#define RT5631_JD_MONO_TRI_HI				(0x1 << 4)
#define RT5631_JD_MONO_TRI_LO				(0x0 << 4)
/* JD trigger enable for Lout */
#define RT5631_JD_AUX_1_EN				(0x1 << 3)
#define RT5631_JD_AUX_1_MASK				(0x1 << 2)
#define RT5631_JD_AUX_1_TRI_HI				(0x1 << 2)
#define RT5631_JD_AUX_1_TRI_LO				(0x0 << 2)
/* JD trigger enable for Rout */
#define RT5631_JD_AUX_2_EN				(0x1 << 1)
#define RT5631_JD_AUX_2_MASK				(0x1 << 0)
#define RT5631_JD_AUX_2_TRI_HI				(0x1 << 0)
#define RT5631_JD_AUX_2_TRI_LO				(0x0 << 0)

/* ALC CONTROL 1(0x64) */
#define RT5631_ALC_ATTACK_RATE_MASK			(0x1F << 8)
#define RT5631_ALC_RECOVERY_RATE_MASK		(0x1F << 0)

/* ALC CONTROL 2(0x65) */
/* select Compensation gain for Noise gate function */
#define RT5631_ALC_COM_NOISE_GATE_MASK		(0xF << 0)

/* ALC CONTROL 3(0x66) */
#define RT5631_ALC_FUN_MASK				(0x3 << 14)
#define RT5631_ALC_FUN_DIS				(0x0 << 14)
#define RT5631_ALC_ENA_DAC_PATH			(0x1 << 14)
#define RT5631_ALC_ENA_ADC_PATH			(0x3 << 14)
#define RT5631_ALC_PARA_UPDATE			(0x1 << 13)
#define RT5631_ALC_LIMIT_LEVEL_MASK			(0x1F << 8)
#define RT5631_ALC_NOISE_GATE_FUN_MASK		(0x1 << 7)
#define RT5631_ALC_NOISE_GATE_FUN_DIS			(0x0 << 7)
#define RT5631_ALC_NOISE_GATE_FUN_ENA		(0x1 << 7)
/* ALC noise gate hold data function */
#define RT5631_ALC_NOISE_GATE_H_D_MASK		(0x1 << 6)
#define RT5631_ALC_NOISE_GATE_H_D_DIS			(0x0 << 6)
#define RT5631_ALC_NOISE_GATE_H_D_ENA		(0x1 << 6)

/* Psedueo Stereo & Spatial Effect Block Control(0x68) */
#define RT5631_SPATIAL_CTRL_EN				(0x1 << 15)
#define RT5631_ALL_PASS_FILTER_EN			(0x1 << 14)
#define RT5631_PSEUDO_STEREO_EN			(0x1 << 13)
#define RT5631_STEREO_EXPENSION_EN			(0x1 << 12)
/* 3D gain parameter */
#define RT5631_GAIN_3D_PARA_MASK			(0x3 << 6)
#define RT5631_GAIN_3D_PARA_1_00			(0x0 << 6)
#define RT5631_GAIN_3D_PARA_1_50			(0x1 << 6)
#define RT5631_GAIN_3D_PARA_2_00			(0x2 << 6)
/* 3D ratio parameter */
#define RT5631_RATIO_3D_MASK				(0x3 << 4)
#define RT5631_RATIO_3D_0_0				(0x0 << 4)
#define RT5631_RATIO_3D_0_66				(0x1 << 4)
#define RT5631_RATIO_3D_1_0				(0x2 << 4)
/* select samplerate for all pass filter */
#define RT5631_APF_FUN_SLE_MASK			(0x3 << 0)
#define RT5631_APF_FUN_SEL_48K				(0x3 << 0)
#define RT5631_APF_FUN_SEL_44_1K			(0x2 << 0)
#define RT5631_APF_FUN_SEL_32K				(0x1 << 0)
#define RT5631_APF_FUN_DIS				(0x0 << 0)

/* EQ CONTROL 1(0x6E) */
#define RT5631_HW_EQ_PATH_SEL_MASK			(0x1 << 15)
#define RT5631_HW_EQ_PATH_SEL_DAC			(0x0 << 15)
#define RT5631_HW_EQ_PATH_SEL_ADC			(0x1 << 15)
#define RT5631_HW_EQ_UPDATE_CTRL			(0x1 << 14)

#define RT5631_EN_HW_EQ_HPF2				(0x1 << 5)
#define RT5631_EN_HW_EQ_HPF1				(0x1 << 4)
#define RT5631_EN_HW_EQ_BP3				(0x1 << 3)
#define RT5631_EN_HW_EQ_BP2				(0x1 << 2)
#define RT5631_EN_HW_EQ_BP1				(0x1 << 1)
#define RT5631_EN_HW_EQ_LPF				(0x1 << 0)

enum {
	RT5631_AIF1,
	RT5631_AIF2,
	RT5631_AIFS,
};

enum {
	RT5631_SCLK1,
	RT5631_SCLK2,
	RT5631_SCLKS,
};

enum {
	RT5631_PLL1,
	RT5631_PLL2,
	RT5631_PLLS,
};

enum {
	RT5631_PLL_S_MCLK,
	RT5631_PLL_S_BCLK,
	RT5631_PLL_S_VBCLK,
};

#define RT5631_NO_JACK		BIT(0)
#define RT5631_HEADSET_DET	BIT(1)
#define RT5631_HEADPHO_DET	BIT(2)

int rt5631_headset_detect(struct snd_soc_codec *codec, int jack_insert);

#endif /* __RT5631_H__ */
