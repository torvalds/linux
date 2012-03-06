/*
 * rt5625.h  --  RT5625 ALSA SoC audio driver
 *
 * Copyright 2011 Realtek Microelectronics
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5625_H__
#define __RT5625_H__

#define RT5625_RESET				0x00
#define RT5625_SPK_OUT_VOL			0x02
#define RT5625_HP_OUT_VOL			0x04
#define RT5625_AUX_OUT_VOL			0x06
#define RT5625_PHONEIN_VOL			0x08
#define RT5625_LINE_IN_VOL			0x0a
#define RT5625_DAC_VOL				0x0c
#define RT5625_MIC_VOL				0x0e
#define RT5625_DAC_MIC_CTRL			0x10
#define RT5625_ADC_REC_GAIN			0x12
#define RT5625_ADC_REC_MIXER			0x14
#define RT5625_VDAC_OUT_VOL			0x18
#define RT5625_VODSP_PDM_CTL			0x1a
#define RT5625_OUTMIX_CTRL			0x1c
#define RT5625_VODSP_CTL			0x1e
#define RT5625_MIC_CTRL				0x22
#define RT5625_DMIC_CTRL			0x24
#define RT5625_PD_CTRL				0x26
#define RT5625_F_DAC_ADC_VDAC			0x2e
#define RT5625_SDP_CTRL				0x34
#define RT5625_EXT_SDP_CTRL			0x36
#define RT5625_PWR_ADD1			0x3a
#define RT5625_PWR_ADD2			0x3c
#define RT5625_PWR_ADD3			0x3e
#define RT5625_GEN_CTRL1			0x40
#define RT5625_GEN_CTRL2			0x42
#define RT5625_PLL_CTRL				0x44
#define RT5625_PLL2_CTRL			0x46
#define RT5625_LDO_CTRL				0x48
#define RT5625_GPIO_CONFIG			0x4c
#define RT5625_GPIO_POLAR			0x4e
#define RT5625_GPIO_STICKY			0x50
#define RT5625_GPIO_WAKEUP			0x52
#define RT5625_GPIO_STATUS			0x54
#define RT5625_GPIO_SHARING			0x56
#define RT5625_OTC_STATUS			0x58
#define RT5625_SOFT_VOL_CTRL			0x5a
#define RT5625_GPIO_OUT_CTRL			0x5c
#define RT5625_MISC_CTRL			0x5e
#define RT5625_DAC_CLK_CTRL1			0x60
#define RT5625_DAC_CLK_CTRL2			0x62
#define RT5625_VDAC_CLK_CTRL1			0x64
#define RT5625_PS_CTRL				0x68
#define RT5625_PRIV_INDEX			0x6a
#define RT5625_PRIV_DATA			0x6c
#define RT5625_EQ_CTRL				0x6e
#define RT5625_DSP_ADDR	 		0x70
#define RT5625_DSP_DATA 		    	0x72
#define RT5625_DSP_CMD	 			0x74
#define RT5625_VENDOR_ID1			0x7c
#define RT5625_VENDOR_ID2			0x7e

/* global definition */
#define RT5625_L_MUTE				(0x1 << 15)
#define RT5625_L_MUTE_SFT			15
#define RT5625_L_ZC				(0x1 << 14)
#define RT5625_L_VOL_MASK			(0x1f << 8)
#define RT5625_L_HVOL_MASK			(0x3f << 8)
#define RT5625_L_VOL_SFT			8
#define RT5625_R_MUTE				(0x1 << 7)
#define RT5625_R_MUTE_SFT			7
#define RT5625_R_ZC				(0x1 << 6)
#define RT5625_R_VOL_MASK			(0x1f)
#define RT5625_R_HVOL_MASK			(0x3f)
#define RT5625_R_VOL_SFT			0
#define RT5625_M_HPMIX				(0x1 << 15)
#define RT5625_M_SPKMIX			(0x1 << 14)
#define RT5625_M_MONOMIX			(0x1 << 13)

/* Phone Input (0x08) */
#define RT5625_M_PHO_HM			(0x1 << 15)
#define RT5625_M_PHO_HM_SFT			15
#define RT5625_M_PHO_SM			(0x1 << 14)
#define RT5625_M_PHO_SM_SFT			14
#define RT5625_PHO_DIFF				(0x1 << 13)
#define RT5625_PHO_DIFF_SFT			13
#define RT5625_PHO_DIFF_DIS			(0x0 << 13)
#define RT5625_PHO_DIFF_EN			(0x1 << 13)

/* Linein Volume (0x0a) */
#define RT5625_M_LI_HM				(0x1 << 15)
#define RT5625_M_LI_HM_SFT			15
#define RT5625_M_LI_SM				(0x1 << 14)
#define RT5625_M_LI_SM_SFT			14
#define RT5625_M_LI_MM				(0x1 << 13)
#define RT5625_M_LI_MM_SFT			13

/* MIC Input Volume (0x0e) */
#define RT5625_MIC1_DIFF_MASK			(0x1 << 15)
#define RT5625_MIC1_DIFF_SFT			15
#define RT5625_MIC1_DIFF_DIS			(0x0 << 15)
#define RT5625_MIC1_DIFF_EN			(0x1 << 15)
#define RT5625_MIC2_DIFF_MASK			(0x1 << 7)
#define RT5625_MIC2_DIFF_SFT			7
#define RT5625_MIC2_DIFF_DIS			(0x0 << 7)
#define RT5625_MIC2_DIFF_EN			(0x1 << 7)

/* Stereo DAC and MIC Routing Control (0x10) */
#define RT5625_M_MIC1_HM			(0x1 << 15)
#define RT5625_M_MIC1_HM_SFT			15
#define RT5625_M_MIC1_SM			(0x1 << 14)
#define RT5625_M_MIC1_SM_SFT			14
#define RT5625_M_MIC1_MM			(0x1 << 13)
#define RT5625_M_MIC1_MM_SFT			13
#define RT5625_M_MIC2_HM			(0x1 << 11)
#define RT5625_M_MIC2_HM_SFT			11
#define RT5625_M_MIC2_SM			(0x1 << 10)
#define RT5625_M_MIC2_SM_SFT			10
#define RT5625_M_MIC2_MM			(0x1 << 9)
#define RT5625_M_MIC2_MM_SFT			9
#define RT5625_M_DACL_HM			(0x1 << 3)
#define RT5625_M_DACL_HM_SFT			3
#define RT5625_M_DACR_HM			(0x1 << 2)
#define RT5625_M_DACR_HM_SFT			2
#define RT5625_M_DAC_SM			(0x1 << 1)
#define RT5625_M_DAC_SM_SFT			1
#define RT5625_M_DAC_MM			(0x1)
#define RT5625_M_DAC_MM_SFT			0

/* ADC Record Gain (0x12) */
#define RT5625_M_ADCL_HM			(0x1 << 15)
#define RT5625_M_ADCL_HM_SFT			15
#define RT5625_M_ADCL_MM			(0x1 << 14)
#define RT5625_M_ADCL_MM_SFT			14
#define RT5625_ADCL_ZCD			(0x1 << 13)
#define RT5625_G_ADCL_MASK			(0x1f << 8)
#define RT5625_M_ADCR_HM			(0x1 << 7)
#define RT5625_M_ADCR_HM_SFT			7
#define RT5625_M_ADCR_MM			(0x1 << 6)
#define RT5625_M_ADCR_MM_SFT			6
#define RT5625_ADCR_ZCD			(0x1 << 5)
#define RT5625_G_ADCR_MASK			(0x1f)

/* ADC Record Mixer Control (0x14) */
#define RT5625_M_RM_L_MIC1			(0x1 << 14)
#define RT5625_M_RM_L_MIC1_SFT		14
#define RT5625_M_RM_L_MIC2			(0x1 << 13)
#define RT5625_M_RM_L_MIC2_SFT		13
#define RT5625_M_RM_L_LINE			(0x1 << 12)
#define RT5625_M_RM_L_LINE_SFT			12
#define RT5625_M_RM_L_PHO			(0x1 << 11)
#define RT5625_M_RM_L_PHO_SFT			11
#define RT5625_M_RM_L_HM			(0x1 << 10)
#define RT5625_M_RM_L_HM_SFT			10
#define RT5625_M_RM_L_SM			(0x1 << 9)
#define RT5625_M_RM_L_SM_SFT			9
#define RT5625_M_RM_L_MM			(0x1 << 8)
#define RT5625_M_RM_L_MM_SFT			8
#define RT5625_M_RM_R_MIC1			(0x1 << 6)
#define RT5625_M_RM_R_MIC1_SFT		6
#define RT5625_M_RM_R_MIC2			(0x1 << 5)
#define RT5625_M_RM_R_MIC2_SFT		5
#define RT5625_M_RM_R_LINE			(0x1 << 4)
#define RT5625_M_RM_R_LINE_SFT			4
#define RT5625_M_RM_R_PHO			(0x1 << 3)
#define RT5625_M_RM_R_PHO_SFT			3
#define RT5625_M_RM_R_HM			(0x1 << 2)
#define RT5625_M_RM_R_HM_SFT			2
#define RT5625_M_RM_R_SM			(0x1 << 1)
#define RT5625_M_RM_R_SM_SFT			1
#define RT5625_M_RM_R_MM			(0x1)
#define RT5625_M_RM_R_MM_SFT			0

/* Voice DAC Volume (0x18) */
#define RT5625_M_VDAC_HM			(0x1 << 15)
#define RT5625_M_VDAC_HM_SFT			15
#define RT5625_M_VDAC_SM			(0x1 << 14)
#define RT5625_M_VDAC_SM_SFT			14
#define RT5625_M_VDAC_MM			(0x1 << 13)
#define RT5625_M_VDAC_MM_SFT			13

/* AEC & PDM Control (0x1a) */
#define RT5625_SRC1_PWR			(0x1 << 15)
#define RT5625_SRC1_PWR_SFT			15
#define RT5625_SRC2_PWR			(0x1 << 13)
#define RT5625_SRC2_PWR_SFT			13
#define RT5625_SRC2_S_MASK			(0x1 << 12)		
#define RT5625_SRC2_S_SFT			12		
#define RT5625_SRC2_S_TXDP			(0x0 << 12)
#define RT5625_SRC2_S_TXDC			(0x1 << 12)
#define RT5625_RXDP_PWR			(0x1 << 11)
#define RT5625_RXDP_PWR_SFT			11
#define RT5625_RXDP_S_MASK			(0x3 << 9)		
#define RT5625_RXDP_S_SFT			9
#define RT5625_RXDP_S_SRC1			(0x0 << 9)
#define RT5625_RXDP_S_ADCL			(0x1 << 9)
#define RT5625_RXDP_S_VOICE			(0x2 << 9)
#define RT5625_RXDP_S_ADCR			(0x3 << 9)
#define RT5625_RXDC_PWR			(0x1 << 8)
#define RT5625_RXDC_PWR_SFT			8
#define RT5625_PCM_S_MASK			(0x1 << 7)		
#define RT5625_PCM_S_SFT			7
#define RT5625_PCM_S_ADCR			(0x0 << 7)
#define RT5625_PCM_S_TXDP			(0x1 << 7)
#define RT5625_REC_IIS_S_MASK			(0x3 << 4)
#define RT5625_REC_IIS_S_SFT			4
#define RT5625_REC_IIS_S_ADC			(0x0 << 4)
#define RT5625_REC_IIS_S_VOICE			(0x1 << 4)
#define RT5625_REC_IIS_S_SRC2			(0x2 << 4)

/* Output Mixer Control (0x1c) */
#define RT5625_SPKN_S_MASK			(0x3 << 14)
#define RT5625_SPKN_S_SFT			14
#define RT5625_SPKN_S_LN			(0x2 << 14)
#define RT5625_SPKN_S_RP			(0x1 << 14)
#define RT5625_SPKN_S_RN			(0x0 << 14)
#define RT5625_SPK_T_MASK			(0x1 << 13)
#define RT5625_SPK_T_SFT			13
#define RT5625_SPK_T_CLS_D			(0x1 << 13)
#define RT5625_SPK_T_CLS_AB			(0x0 << 13)
#define RT5625_CLS_AB_MASK			(0x1 << 12)
#define RT5625_CLS_AB_SFT			12
#define RT5625_CLS_AB_S_AMP			(0x0 << 12)
#define RT5625_CLS_AB_W_AMP			(0x1 << 12)
#define RT5625_SPKVOL_S_MASK			(0x3 << 10)
#define RT5625_SPKVOL_S_SFT			10
#define RT5625_SPKVOL_S_MM			(0x3 << 10)
#define RT5625_SPKVOL_S_SM			(0x2 << 10)
#define RT5625_SPKVOL_S_HM			(0x1 << 10)
#define RT5625_SPKVOL_S_VMID			(0x0 << 10)
#define RT5625_HPVOL_L_S_MASK			(0x1 << 9)
#define RT5625_HPVOL_L_S_SFT			9
#define RT5625_HPVOL_L_S_HM			(0x1 << 9)
#define RT5625_HPVOL_L_S_VMID			(0x0 << 9)
#define RT5625_HPVOL_R_S_MASK			(0x1 << 8)
#define RT5625_HPVOL_R_S_SFT			8
#define RT5625_HPVOL_R_S_HM			(0x1 << 8)	
#define RT5625_HPVOL_R_S_VMID			(0x0 << 8)
#define RT5625_AUXVOL_S_MASK			(0x3 << 6)
#define RT5625_AUXVOL_S_SFT			6
#define RT5625_AUXVOL_S_MM			(0x3 << 6)
#define RT5625_AUXVOL_S_SM			(0x2 << 6)
#define RT5625_AUXVOL_S_HM			(0x1 << 6)
#define RT5625_AUXVOL_S_VMID			(0x0 << 6)
#define RT5625_AUXOUT_MODE			(0x1 << 4)
#define RT5625_AUXOUT_MODE_SFT		4
#define RT5625_DACL_HP_MASK			(0x1 << 1)
#define RT5625_DACL_HP_SFT			1
#define RT5625_DACL_HP_MUTE			(0x0 << 1)
#define RT5625_DACL_HP_ON			(0x1 << 1)
#define RT5625_DACR_HP_MASK			(0x1)
#define RT5625_DACR_HP_SFT			0
#define RT5625_DACR_HP_MUTE			(0x0)
#define RT5625_DACR_HP_ON			(0x1)

/* VoDSP Control (0x1e) */
#define RT5625_DSP_SCLK_S_MASK		(0x1 << 15)
#define RT5625_DSP_SCLK_S_SFT			15
#define RT5625_DSP_SCLK_S_MCLK	 	(0x0 << 15)
#define RT5625_DSP_SCLK_S_VCLK	 		(0x1 << 15)
#define RT5625_DSP_LRCK_MASK			(0x1 << 13)
#define RT5625_DSP_LRCK_SFT			13
#define RT5625_DSP_LRCK_8K			(0x0 << 13)
#define RT5625_DSP_LRCK_16K			(0x1 << 13)
#define RT5625_DSP_TP_MASK			(0x1 << 3)
#define RT5625_DSP_TP_SFT			3
#define RT5625_DSP_TP_NOR			(0x0 << 3)
#define RT5625_DSP_TP_TEST			(0x1 << 3)
#define RT5625_DSP_BP_MASK			(0x1 << 2)
#define RT5625_DSP_BP_SFT			2
#define RT5625_DSP_BP_EN			(0x0 << 2)
#define RT5625_DSP_BP_NOR			(0x1 << 2)
#define RT5625_DSP_PD_MASK			(0x1 << 1)
#define RT5625_DSP_PD_SFT			1
#define RT5625_DSP_PD_EN			(0x0 << 1)
#define RT5625_DSP_PD_NOR			(0x1 << 1)
#define RT5625_DSP_RST_MASK			(0x1)
#define RT5625_DSP_RST_SFT			0
#define RT5625_DSP_RST_EN			(0x0)
#define RT5625_DSP_RST_NOR			(0x1)

/* Microphone Control (0x22) */
#define RT5625_MIC1_BST_MASK			(0x3 << 10)
#define RT5625_MIC1_BST_SFT			10
#define RT5625_MIC1_BST_BYPASS		(0x0 << 10)
#define RT5625_MIC1_BST_20DB			(0x1 << 10)
#define RT5625_MIC1_BST_30DB			(0x2 << 10)
#define RT5625_MIC1_BST_40DB			(0x3 << 10)
#define RT5625_MIC2_BST_MASK			(0x3 << 8)
#define RT5625_MIC2_BST_SFT			8
#define RT5625_MIC2_BST_BYPASS		(0x0 << 8)
#define RT5625_MIC2_BST_20DB			(0x1 << 8)
#define RT5625_MIC2_BST_30DB			(0x2 << 8)
#define RT5625_MIC2_BST_40DB			(0x3 << 8)
#define RT5625_MB1_OV_MASK			(0x1 << 5)
#define RT5625_MB1_OV_90P			(0x0 << 5)
#define RT5625_MB1_OV_75P			(0x1 << 5)
#define RT5625_MB2_OV_MASK			(0x1 << 4)
#define RT5625_MB2_OV_90P			(0x0 << 4)
#define RT5625_MB2_OV_75P			(0x1 << 4)
#define RT5625_SCD_THD_MASK			(0x3)
#define RT5625_SCD_THD_600UA			(0x0)
#define RT5625_SCD_THD_1500UA			(0x1)
#define RT5625_SCD_THD_2000UA			(0x2)

/* Digital Boost Control (0x24) */
#define RT5625_DIG_BST_MASK			(0x7)
#define RT5625_DIG_BST_SFT			0

/* Power Down Control/Status (0x26) */
#define RT5625_PWR_PR7				(0x1 << 15)
#define RT5625_PWR_PR6				(0x1 << 14)
#define RT5625_PWR_PR5				(0x1 << 13)
#define RT5625_PWR_PR3				(0x1 << 11)
#define RT5625_PWR_PR2				(0x1 << 10)
#define RT5625_PWR_PR1				(0x1 << 9)
#define RT5625_PWR_PR0				(0x1 << 8)
#define RT5625_PWR_REF_ST			(0x1 << 3)
#define RT5625_PWR_AM_ST			(0x1 << 2)
#define RT5625_PWR_DAC_ST			(0x1 << 1)
#define RT5625_PWR_ADC_ST			(0x1)

/* Stereo DAC/Voice DAC/Stereo ADC Function Select (0x2e) */
#define RT5625_DAC_F_MASK			(0x3 << 12)
#define RT5625_DAC_F_SFT			12
#define RT5625_DAC_F_DAC			(0x0 << 12)		
#define RT5625_DAC_F_SRC2			(0x1 << 12)
#define RT5625_DAC_F_TXDP			(0x2 << 12)
#define RT5625_DAC_F_TXDC			(0x3 << 12)
#define RT5625_VDAC_S_MASK			(0x7 << 8)
#define RT5625_VDAC_S_SFT			8
#define RT5625_VDAC_S_VOICE			(0x0 << 8)	
#define RT5625_VDAC_S_SRC2			(0x1 << 8)
#define RT5625_VDAC_S_TXDP			(0x2 << 8)
#define RT5625_VDAC_S_TXDC			(0x3 << 8)
#define RT5625_ADCR_F_MASK			(0x3 << 4)
#define RT5625_ADCR_F_SFT			4
#define RT5625_ADCR_F_ADC			(0x0 << 4)
#define RT5625_ADCR_F_VADC			(0x1 << 4)
#define RT5625_ADCR_F_DSP			(0x2 << 4)
#define RT5625_ADCR_F_PDM			(0x3 << 4)
#define RT5625_ADCL_F_MASK			(0x1)
#define RT5625_ADCL_F_SFT			0
#define RT5625_ADCL_F_ADC			(0x0)
#define RT5625_ADCL_F_DSP			(0x1)

/* Main Serial Data Port Control (Stereo IIS) (0x34) */
#define RT5625_I2S_M_MASK			(0x1 << 15)
#define RT5625_I2S_M_SFT			15
#define RT5625_I2S_M_MST			(0x0 << 15)
#define RT5625_I2S_M_SLV			(0x1 << 15)
#define RT5625_I2S_SAD_MASK			(0x1 << 14)
#define RT5625_I2S_SAD_SFT			14
#define RT5625_I2S_SAD_DIS			(0x0 << 14)
#define RT5625_I2S_SAD_EN			(0x1 << 14)
#define RT5625_I2S_S_MASK			(0x1 << 8)
#define RT5625_I2S_S_SFT			8
#define RT5625_I2S_S_MSCLK			(0x0 << 8)
#define RT5625_I2S_S_VSCLK			(0x1 << 8)
#define RT5625_I2S_BP_MASK			(0x1 << 7)
#define RT5625_I2S_BP_SFT			7
#define RT5625_I2S_BP_NOR			(0x0 << 7)
#define RT5625_I2S_BP_INV			(0x1 << 7)
#define RT5625_I2S_LRCK_MASK			(0x1 << 6)
#define RT5625_I2S_LRCK_SFT			6
#define RT5625_I2S_LRCK_NOR			(0x0 << 6)
#define RT5625_I2S_LRCK_INV			(0x1 << 6)
#define RT5625_I2S_DL_MASK			(0x3 << 2)
#define RT5625_I2S_DL_SFT			2
#define RT5625_I2S_DL_16			(0x0 << 2)
#define RT5625_I2S_DL_20			(0x1 << 2)
#define RT5625_I2S_DL_24			(0x2 << 2)
#define RT5625_I2S_DL_8				(0x3 << 2)
#define RT5625_I2S_DF_MASK			(0x3)
#define RT5625_I2S_DF_SFT			0
#define RT5625_I2S_DF_I2S			(0x0)
#define RT5625_I2S_DF_LEFT			(0x1)
#define RT5625_I2S_DF_PCM_A			(0x2)
#define RT5625_I2S_DF_PCM_B			(0x3)

/* Extend Serial Data Port Control (0x36) */
#define RT5625_PCM_F_MASK			(0x1 << 15)
#define RT5625_PCM_F_SFT			15
#define RT5625_PCM_F_GPIO			(0x0 << 15)
#define RT5625_PCM_F_PCM			(0x1 << 15)
#define RT5625_PCM_M_MASK			(0x1 << 14)
#define RT5625_PCM_M_SFT			14
#define RT5625_PCM_M_MST			(0x0 << 14)
#define RT5625_PCM_M_SLV			(0x1 << 14)
#define RT5625_PCM_CS_MASK			(0x1 << 8)
#define RT5625_PCM_CS_SFT			8
#define RT5625_PCM_CS_SCLK			(0x0 << 8)
#define RT5625_PCM_CS_VSCLK			(0x1 << 8)

/* Power Management Addition 1 (0x3a) */
#define RT5625_P_DACL_MIX			(0x1 << 15)
#define RT5625_P_DACL_MIX_BIT			15
#define RT5625_P_DACR_MIX			(0x1 << 14)
#define RT5625_P_DACR_MIX_BIT			14
#define RT5625_P_ZCD				(0x1 << 13)
#define RT5625_P_ZCD_BIT			13
#define RT5625_P_I2S				(0x1 << 11)
#define RT5625_P_I2S_BIT			11
#define RT5625_P_SPK_AMP			(0x1 << 10)
#define RT5625_P_SPK_AMP_BIT			10
#define RT5625_P_HPO_AMP			(0x1 << 9)
#define RT5625_P_HPO_AMP_BIT			9
#define RT5625_P_HPO_ENH			(0x1 << 8)
#define RT5625_P_HPO_ENH_BIT			8
#define RT5625_P_VDAC_MIX			(0x1 << 7)
#define RT5625_P_VDAC_MIX_BIT			7
#define RT5625_P_SG_EN				(0x1 << 6)	
#define RT5625_P_SG_EN_BIT			6	
#define RT5625_P_MB1_SCD			(0x1 << 5)
#define RT5625_P_MB1_SCD_BIT			5
#define RT5625_P_MB2_SCD			(0x1 << 4)
#define RT5625_P_MB2_SCD_BIT			4
#define RT5625_P_MB1				(0x1 << 3)	
#define RT5625_P_MB1_BIT			3	
#define RT5625_P_MB2				(0x1 << 2)	
#define RT5625_P_MB2_BIT			2	
#define RT5625_P_MAIN_BIAS			(0x1 << 1)
#define RT5625_P_MAIN_BIAS_BIT			1
#define RT5625_P_DAC_REF			(0x1)
#define RT5625_P_DAC_REF_BIT			0

/* Power Management Addition 2 (0x3c) */
#define RT5625_P_PLL1				(0x1 << 15)
#define RT5625_P_PLL1_BIT				15
#define RT5625_P_PLL2				(0x1 << 14)
#define RT5625_P_PLL2_BIT				14
#define RT5625_P_VREF				(0x1 << 13)
#define RT5625_P_VREF_BIT			13
#define RT5625_P_OVT				(0x1 << 12)
#define RT5625_P_OVT_BIT			12
#define RT5625_P_AUX_ADC			(0x1 << 11)
#define RT5625_P_AUX_ADC_BIT			11
#define RT5625_P_VDAC				(0x1 << 10)
#define RT5625_P_VDAC_BIT			10
#define RT5625_P_DACL				(0x1 << 9)
#define RT5625_P_DACL_BIT			9
#define RT5625_P_DACR				(0x1 << 8)
#define RT5625_P_DACR_BIT			8
#define RT5625_P_ADCL				(0x1 << 7)
#define RT5625_P_ADCL_BIT			7
#define RT5625_P_ADCR				(0x1 << 6)
#define RT5625_P_ADCR_BIT			6
#define RT5625_P_HM_L				(0x1 << 5)
#define RT5625_P_HM_L_BIT			5
#define RT5625_P_HM_R				(0x1 << 4)
#define RT5625_P_HM_R_BIT			4
#define RT5625_P_SM				(0x1 << 3)
#define RT5625_P_SM_BIT				3
#define RT5625_P_MM				(0x1 << 2)
#define RT5625_P_MM_BIT			2
#define RT5625_P_ADCL_RM			(0x1 << 1)
#define RT5625_P_ADCL_RM_BIT			1
#define RT5625_P_ADCR_RM			(0x1)
#define RT5625_P_ADCR_RM_BIT			0

/* Power Management Addition 3 (0x3e) */
#define RT5625_P_OSC_EN			(0x1 << 15)
#define RT5625_P_OSC_EN_BIT			15
#define RT5625_P_AUX_VOL			(0x1 << 14)
#define RT5625_P_AUX_VOL_BIT			14
#define RT5625_P_SPKL_VOL			(0x1 << 13)
#define RT5625_P_SPKL_VOL_BIT			13
#define RT5625_P_SPKR_VOL			(0x1 << 12)
#define RT5625_P_SPKR_VOL_BIT			12
#define RT5625_P_HPL_VOL			(0x1 << 11)
#define RT5625_P_HPL_VOL_BIT			11
#define RT5625_P_HPR_VOL			(0x1 << 10)
#define RT5625_P_HPR_VOL_BIT			10
#define RT5625_P_DSP_IF				(0x1 << 9)
#define RT5625_P_DSP_IF_BIT			9
#define RT5625_P_DSP_I2C			(0x1 << 8)
#define RT5625_P_DSP_I2C_BIT			8
#define RT5625_P_LV_L				(0x1 << 7)
#define RT5625_P_LV_L_BIT			7
#define RT5625_P_LV_R				(0x1 << 6)
#define RT5625_P_LV_R_BIT			6
#define RT5625_P_PH_VOL			(0x1 << 5)
#define RT5625_P_PH_VOL_BIT			5
#define RT5625_P_PH_ADMIX			(0x1 << 4)
#define RT5625_P_PH_ADMIX_BIT			4
#define RT5625_P_MIC1_VOL			(0x1 << 3)
#define RT5625_P_MIC1_VOL_BIT			3
#define RT5625_P_MIC2_VOL			(0x1 << 2)
#define RT5625_P_MIC2_VOL_BIT			2
#define RT5625_P_MIC1_BST			(0x1 << 1)
#define RT5625_P_MIC1_BST_BIT			1
#define RT5625_P_MIC2_BST			(0x1)
#define RT5625_P_MIC2_BST_BIT			0

/* General Purpose Control Register 1 (0x40) */
#define RT5625_SCLK_MASK			(0x1 << 15)
#define RT5625_SCLK_SFT				15
#define RT5625_SCLK_MCLK			(0x0 << 15)
#define RT5625_SCLK_PLL1			(0x1 << 15)
#define RT5625_VSCLK_MASK			(0x1 << 4)
#define RT5625_VSCLK_SFT			4
#define RT5625_VSCLK_PLL2			(0x0<<4)
#define RT5625_VSCLK_EXTCLK			(0x1<<4)
#define RT5625_SPK_R_MASK			(0x7 << 1)
#define RT5625_SPK_R_SFT			1
#define RT5625_SPK_R_225V			(0x0 << 1)
#define RT5625_SPK_R_200V			(0x1 << 1)
#define RT5625_SPK_R_175V			(0x2 << 1)
#define RT5625_SPK_R_150V			(0x3 << 1)
#define RT5625_SPK_R_125V			(0x4 << 1)
#define RT5625_SPK_R_100V			(0x5 << 1)

/* General Purpose Control Register 2 (0x42) */
#define RT5625_PLL1_S_MASK			(0x3 << 12)
#define RT5625_PLL1_S_SFT			12
#define RT5625_PLL1_S_MCLK			(0x0 << 12)
#define RT5625_PLL1_S_BCLK			(0x2 << 12)
#define RT5625_PLL1_S_VBCLK			(0x3 << 12)

/* PLL2 Control (0x46) */
#define RT5625_PLL2_MASK			(0x1 << 15)
#define RT5625_PLL2_DIS				(0x0 << 15)
#define RT5625_PLL2_EN				(0x1 << 15)
#define RT5625_PLL2_R_MASK			(0x1)
#define RT5625_PLL2_R_8X			(0x0)
#define RT5625_PLL2_R_16X			(0x1)

/* LDO Control (0x48) */
#define RT5625_LDO_MASK			(0x1 << 15)
#define RT5625_LDO_DIS				(0x0 << 15)
#define RT5625_LDO_EN				(0x1 << 15)
#define RT5625_LDO_VC_MASK			(0xf)
#define RT5625_LDO_VC_1_55V			(0xf<<0)
#define RT5625_LDO_VC_1_50V			(0xe<<0)
#define RT5625_LDO_VC_1_45V			(0xd<<0)
#define RT5625_LDO_VC_1_40V			(0xc<<0)
#define RT5625_LDO_VC_1_35V			(0xb<<0)
#define RT5625_LDO_VC_1_30V			(0xa<<0)
#define RT5625_LDO_VC_1_25V			(0x9<<0)
#define RT5625_LDO_VC_1_20V			(0x8<<0)
#define RT5625_LDO_VC_1_15V			(0x7<<0)
#define RT5625_LDO_VC_1_05V			(0x6<<0)
#define RT5625_LDO_VC_1_00V			(0x5<<0)
#define RT5625_LDO_VC_0_95V			(0x4<<0)
#define RT5625_LDO_VC_0_90V			(0x3<<0)
#define RT5625_LDO_VC_0_85V			(0x2<<0)
#define RT5625_LDO_VC_0_80V			(0x1<<0)
#define RT5625_LDO_VC_0_75V			(0x0<<0)

/* GPIO Pin Configuration (0x4c) */
#define RT5625_GPIO_5				(0x1 << 5)
#define RT5625_GPIO_4				(0x1 << 4)
#define RT5625_GPIO_3				(0x1 << 3)
#define RT5625_GPIO_2				(0x1 << 2)
#define RT5625_GPIO_1				(0x1 << 1)

/* MISC Control (0x5e) */
#define RT5625_FAST_VREF_MASK			(0x1 << 15)
#define RT5625_FAST_VREF_EN			(0x0 << 15)
#define RT5625_FAST_VREF_DIS			(0x1 << 15)
#define RT5625_HP_DEPOP_M2			(0x1 << 8)
#define RT5625_HP_DEPOP_M1			(0x1 << 9)
#define RT5625_HPL_MUM_DEPOP			(0x1 << 7)
#define RT5625_HPR_MUM_DEPOP			(0x1 << 6)
#define RT5625_MUM_DEPOP			(0x1 << 5)

/* Stereo DAC Clock Control 1 (0x60) */
#define RT5625_BCLK_DIV1_MASK			(0xf << 12)
#define RT5625_BCLK_DIV1_1			(0x0 << 12)
#define RT5625_BCLK_DIV1_2			(0x1 << 12)
#define RT5625_BCLK_DIV1_3			(0x2 << 12)
#define RT5625_BCLK_DIV1_4			(0x3 << 12)
#define RT5625_BCLK_DIV1_5			(0x4 << 12)
#define RT5625_BCLK_DIV1_6			(0x5 << 12)
#define RT5625_BCLK_DIV1_7			(0x6 << 12)
#define RT5625_BCLK_DIV1_8			(0x7 << 12)
#define RT5625_BCLK_DIV1_9			(0x8 << 12)
#define RT5625_BCLK_DIV1_10			(0x9 << 12)
#define RT5625_BCLK_DIV1_11			(0xa << 12)
#define RT5625_BCLK_DIV1_12			(0xb << 12)
#define RT5625_BCLK_DIV1_13			(0xc << 12)
#define RT5625_BCLK_DIV1_14			(0xd << 12)
#define RT5625_BCLK_DIV1_15			(0xe << 12)
#define RT5625_BCLK_DIV1_16			(0xf << 12)
#define RT5625_BCLK_DIV2_MASK			(0x7 << 8)
#define RT5625_BCLK_DIV2_2			(0x0 << 8)
#define RT5625_BCLK_DIV2_4			(0x1 << 8)
#define RT5625_BCLK_DIV2_8			(0x2 << 8)
#define RT5625_BCLK_DIV2_16			(0x3 << 8)
#define RT5625_BCLK_DIV2_32			(0x4 << 8)
#define RT5625_AD_LRCK_DIV1_MASK		(0xf << 4)
#define RT5625_AD_LRCK_DIV1_1			(0x0 << 4)
#define RT5625_AD_LRCK_DIV1_2			(0x1 << 4)
#define RT5625_AD_LRCK_DIV1_3			(0x2 << 4)
#define RT5625_AD_LRCK_DIV1_4			(0x3 << 4)
#define RT5625_AD_LRCK_DIV1_5			(0x4 << 4)
#define RT5625_AD_LRCK_DIV1_6			(0x5 << 4)
#define RT5625_AD_LRCK_DIV1_7			(0x6 << 4)
#define RT5625_AD_LRCK_DIV1_8			(0x7 << 4)
#define RT5625_AD_LRCK_DIV1_9			(0x8 << 4)
#define RT5625_AD_LRCK_DIV1_10			(0x9 << 4)
#define RT5625_AD_LRCK_DIV1_11			(0xa << 4)
#define RT5625_AD_LRCK_DIV1_12			(0xb << 4)
#define RT5625_AD_LRCK_DIV1_13			(0xc << 4)
#define RT5625_AD_LRCK_DIV1_14			(0xd << 4)
#define RT5625_AD_LRCK_DIV1_15			(0xe << 4)
#define RT5625_AD_LRCK_DIV1_16			(0xf << 4)
#define RT5625_AD_LRCK_DIV2_MASK		(0x7 << 1)
#define RT5625_AD_LRCK_DIV2_2			(0x0 << 1)
#define RT5625_AD_LRCK_DIV2_4			(0x1 << 1)
#define RT5625_AD_LRCK_DIV2_8			(0x2 << 1)
#define RT5625_AD_LRCK_DIV2_16			(0x3 << 1)
#define RT5625_AD_LRCK_DIV2_32			(0x4 << 1)
#define RT5625_DA_LRCK_DIV_MASK		(1)
#define RT5625_DA_LRCK_DIV_32			(0)
#define RT5625_DA_LRCK_DIV_64			(1)

/* Stereo DAC Clock Control 2 (0x62) */
#define RT5625_DF_DIV1_MASK			(0xF << 12)
#define RT5625_DF_DIV1_1			(0x0 << 12)
#define RT5625_DF_DIV1_2			(0x1 << 12)
#define RT5625_DF_DIV1_3			(0x2 << 12)
#define RT5625_DF_DIV1_4			(0x3 << 12)
#define RT5625_DF_DIV1_5			(0x4 << 12)
#define RT5625_DF_DIV1_6			(0x5 << 12)
#define RT5625_DF_DIV1_7			(0x6 << 12)
#define RT5625_DF_DIV1_8			(0x7 << 12)
#define RT5625_DF_DIV1_9			(0x8 << 12)
#define RT5625_DF_DIV1_10			(0x9 << 12)
#define RT5625_DF_DIV1_11			(0xA << 12)
#define RT5625_DF_DIV1_12			(0xB << 12)
#define RT5625_DF_DIV1_13			(0xC << 12)
#define RT5625_DF_DIV1_14			(0xD << 12)
#define RT5625_DF_DIV1_15			(0xE << 12)
#define RT5625_DF_DIV1_16			(0xF << 12)
#define RT5625_DF_DIV2_MASK			(0x7 << 9)
#define RT5625_DF_DIV2_2			(0x0 << 9)
#define RT5625_DF_DIV2_4			(0x1 << 9)
#define RT5625_DF_DIV2_8			(0x2 << 9)
#define RT5625_DF_DIV2_16			(0x3 << 9)
#define RT5625_DF_DIV2_32			(0x4 << 9)
#define RT5625_AF_DIV1_MASK			(0xF << 4)
#define RT5625_AF_DIV1_1			(0x0 << 4)
#define RT5625_AF_DIV1_2			(0x1 << 4)
#define RT5625_AF_DIV1_3			(0x2 << 4)
#define RT5625_AF_DIV1_4			(0x3 << 4)
#define RT5625_AF_DIV1_5			(0x4 << 4)
#define RT5625_AF_DIV1_6			(0x5 << 4)
#define RT5625_AF_DIV1_7			(0x6 << 4)
#define RT5625_AF_DIV1_8			(0x7 << 4)
#define RT5625_AF_DIV1_9			(0x8 << 4)
#define RT5625_AF_DIV1_10			(0x9 << 4)
#define RT5625_AF_DIV1_11			(0xA << 4)
#define RT5625_AF_DIV1_12			(0xB << 4)
#define RT5625_AF_DIV1_13			(0xC << 4)
#define RT5625_AF_DIV1_14			(0xD << 4)
#define RT5625_AF_DIV1_15			(0xE << 4)
#define RT5625_AF_DIV1_16			(0xF << 4)
#define RT5625_AF_DIV2_MASK			(0x7 << 1)
#define RT5625_AF_DIV2_1			(0x0 << 1)
#define RT5625_AF_DIV2_2			(0x1 << 1)
#define RT5625_AF_DIV2_4			(0x2 << 1)
#define RT5625_AF_DIV2_8			(0x3 << 1)
#define RT5625_AF_DIV2_16			(0x4 << 1)
#define RT5625_AF_DIV2_32			(0x5 << 1)

/* Voice DAC PCM Clock Control 1 (0x64) */
#define RT5625_VBCLK_DIV1_MASK		(0xF << 12)
#define RT5625_VBCLK_DIV1_1			(0x0 << 12)
#define RT5625_VBCLK_DIV1_2			(0x1 << 12)
#define RT5625_VBCLK_DIV1_3			(0x2 << 12)
#define RT5625_VBCLK_DIV1_4			(0x3 << 12)
#define RT5625_VBCLK_DIV1_5			(0x4 << 12)
#define RT5625_VBCLK_DIV1_6			(0x5 << 12)
#define RT5625_VBCLK_DIV1_7			(0x6 << 12)
#define RT5625_VBCLK_DIV1_8			(0x7 << 12)
#define RT5625_VBCLK_DIV1_9			(0x8 << 12)
#define RT5625_VBCLK_DIV1_10			(0x9 << 12)
#define RT5625_VBCLK_DIV1_11			(0xA << 12)
#define RT5625_VBCLK_DIV1_12			(0xB << 12)
#define RT5625_VBCLK_DIV1_13			(0xC << 12)
#define RT5625_VBCLK_DIV1_14			(0xD << 12)
#define RT5625_VBCLK_DIV1_15			(0xE << 12)
#define RT5625_VBCLK_DIV1_16			(0xF << 12)
#define RT5625_VBCLK_DIV2_MASK		(0x7 << 8)
#define RT5625_VBCLK_DIV2_2			(0x0 << 8)
#define RT5625_VBCLK_DIV2_4			(0x1 << 8)
#define RT5625_VBCLK_DIV2_8			(0x2 << 8)
#define RT5625_VBCLK_DIV2_16			(0x3 << 8)
#define RT5625_VBCLK_DIV2_32			(0x4 << 8)
#define RT5625_AD_VLRCK_DIV1_MASK		(0xF << 4)
#define RT5625_AD_VLRCK_DIV1_1			(0x0 << 4)
#define RT5625_AD_VLRCK_DIV1_2			(0x1 << 4)
#define RT5625_AD_VLRCK_DIV1_3			(0x2 << 4)
#define RT5625_AD_VLRCK_DIV1_4			(0x3 << 4)
#define RT5625_AD_VLRCK_DIV1_5			(0x4 << 4)
#define RT5625_AD_VLRCK_DIV1_6			(0x5 << 4)
#define RT5625_AD_VLRCK_DIV1_7			(0x6 << 4)
#define RT5625_AD_VLRCK_DIV1_8			(0x7 << 4)
#define RT5625_AD_VLRCK_DIV1_9			(0x8 << 4)
#define RT5625_AD_VLRCK_DIV1_10		(0x9 << 4)
#define RT5625_AD_VLRCK_DIV1_11		(0xA << 4)
#define RT5625_AD_VLRCK_DIV1_12		(0xB << 4)
#define RT5625_AD_VLRCK_DIV1_13		(0xC << 4)
#define RT5625_AD_VLRCK_DIV1_14		(0xD << 4)
#define RT5625_AD_VLRCK_DIV1_15		(0xE << 4)
#define RT5625_AD_VLRCK_DIV1_16		(0xF << 4)
#define RT5625_AD_VLRCK_DIV2_MASK		(0x7 << 1)
#define RT5625_AD_VLRCK_DIV2_2			(0x0 << 1)
#define RT5625_AD_VLRCK_DIV2_4			(0x1 << 1)
#define RT5625_AD_VLRCK_DIV2_8			(0x2 << 1)
#define RT5625_AD_VLRCK_DIV2_16		(0x3 << 1)
#define RT5625_AD_VLRCK_DIV2_32		(0x4 << 1)
#define RT5625_DA_VLRCK_DIV_MASK		(1)
#define RT5625_DA_VLRCK_DIV_32			(0)
#define RT5625_DA_VLRCK_DIV_64			(1)

/* Psedueo Stereo & Spatial Effect Block Control (0x68) */
#define RT5625_SP_CTRL_EN			(0x1 << 15)
#define RT5625_APF_EN				(0x1 << 14)
#define RT5625_PS_EN				(0x1 << 13)
#define RT5625_STO_EXP_EN			(0x1 << 12)
#define RT5625_SP_3D_G1_MASK			(0x3 << 10)
#define RT5625_SP_3D_G1_1_0			(0x0 << 10)
#define RT5625_SP_3D_G1_1_5			(0x1 << 10)
#define RT5625_SP_3D_G1_2_0			(0x2 << 10)
#define RT5625_SP_3D_R1_MASK			(0x3 << 8)
#define RT5625_SP_3D_R1_0_0			(0x0 << 8)
#define RT5625_SP_3D_R1_0_66			(0x1 << 8)
#define RT5625_SP_3D_R1_1_0			(0x2 << 8)
#define RT5625_SP_3D_G2_MASK			(0x3 << 6)
#define RT5625_SP_3D_G2_1_0			(0x0 << 6)
#define RT5625_SP_3D_G2_1_5			(0x1 << 6)
#define RT5625_SP_3D_G2_2_0			(0x2 << 6)
#define RT5625_SP_3D_R2_MASK			(0x3 << 4)
#define RT5625_SP_3D_R2_0_0			(0x0 << 4)
#define RT5625_SP_3D_R2_0_66			(0x1 << 4)
#define RT5625_SP_3D_R2_1_0			(0x2 << 4)
#define RT5625_APF_MASK			(0x3)
#define RT5625_APF_48K				(0x3)
#define RT5625_APF_44_1K			(0x2)
#define RT5625_APF_32K				(0x1)

/* EQ Control and Status /ADC HPF Control (0x6E) */
#define RT5625_EN_HW_EQ_BLK			(0x1 << 15)
#define RT5625_EQ_SRC_DAC			(0x0 << 14)
#define RT5625_EQ_SRC_ADC			(0x1 << 14)
#define RT5625_EQ_CHG_EN			(0x1 << 7)
#define RT5625_EN_HW_EQ_HPF			(0x1 << 4)
#define RT5625_EN_HW_EQ_BP3			(0x1 << 3)
#define RT5625_EN_HW_EQ_BP2			(0x1 << 2)
#define RT5625_EN_HW_EQ_BP1			(0x1 << 1)
#define RT5625_EN_HW_EQ_LPF			(0x1 << 0)

/* VoDSP Register Command (0x74) */
#define RT5625_DSP_BUSY_MASK			(0x1 << 15)
#define RT5625_DSP_DS_MASK			(0x1 << 14)
#define RT5625_DSP_DS_VODSP			(0x0 << 14)
#define RT5625_DSP_DS_REG72			(0x1 << 14)
#define RT5625_DSP_CLK_MASK			(0x3 << 12)
#define RT5625_DSP_CLK_12_288M		(0x0 << 12)
#define RT5625_DSP_CLK_6_144M			(0x1 << 12)
#define RT5625_DSP_CLK_3_072M			(0x2 << 12)
#define RT5625_DSP_CLK_2_048M			(0x3 << 12)
#define RT5625_DSP_R_EN				(0x1 << 9)
#define RT5625_DSP_W_EN			(0x1 << 8)
#define RT5625_DSP_CMD_MASK			(0xff)
#define RT5625_DSP_CMD_SFT			0
#define RT5625_DSP_CMD_MW			(0x3B)	/* Memory Write */
#define RT5625_DSP_CMD_MR			(0x37)	/* Memory Read */
#define RT5625_DSP_CMD_RR			(0x60)	/* Register Read */
#define RT5625_DSP_CMD_RW			(0x68)	/* Register Write */


/* Index(0x20) for Auto Volume Control */
#define RT5625_AVC_CH_MASK			(0x1 << 7)
#define RT5625_AVC_CH_L_CH			(0x0 << 7)
#define RT5625_AVC_CH_R_CH			(0x1 << 7)
#define RT5625_AVC_GAIN_EN			(0x1 << 15)



enum {
	RT5625_AIF1,
	RT5625_AIF2,
};

/* System Clock Source */
enum {
	RT5625_SCLK_S_MCLK,
	RT5625_SCLK_S_PLL,
};

enum pll_sel {
	RT5625_PLL_MCLK = 0,
	RT5625_PLL_MCLK_TO_VSYSCLK,
	RT5625_PLL_BCLK,
	RT5625_PLL_VBCLK,
};

enum {
	RT5625_AEC_DIS,
	RT5625_AEC_EN,
};

//#ifdef RT5625_F_SMT_PHO
enum {
	RT5625_PLL_DIS,
	RT5625_PLL_112896_225792,
	RT5625_PLL_112896_24576,
};
//#endif

typedef struct { 
	unsigned short index;
	unsigned short value;
} rt5625_dsp_reg;

#endif
