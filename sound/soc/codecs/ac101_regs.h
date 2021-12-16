/*
 * ac101_regs.h
 *
 * (C) Copyright 2017-2018
 * Seeed Technology Co., Ltd. <www.seeedstudio.com>
 *
 * PeterYang <linsheng.yang@seeed.cc>
 *
 * (C) Copyright 2010-2017
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * huangxin <huangxin@reuuimllatech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef __AC101_REGS_H__
#define __AC101_REGS_H__

/*pll source*/
#define AC101_MCLK1 1
#define AC101_MCLK2 2
#define AC101_BCLK1 3
#define AC101_BCLK2 4

#define AIF1_CLK 1
#define AIF2_CLK 2

#define	CHIP_AUDIO_RST		0x0
#define PLL_CTRL1		0x1
#define PLL_CTRL2		0x2
#define SYSCLK_CTRL		0x3
#define MOD_CLK_ENA		0x4
#define MOD_RST_CTRL		0x5
#define AIF_SR_CTRL		0x6

#define AIF1_CLK_CTRL		0x10
#define AIF1_ADCDAT_CTRL	0x11
#define AIF1_DACDAT_CTRL	0x12
#define AIF1_MXR_SRC		0x13
#define AIF1_VOL_CTRL1		0x14
#define AIF1_VOL_CTRL2		0x15
#define AIF1_VOL_CTRL3		0x16
#define AIF1_VOL_CTRL4		0x17
#define AIF1_MXR_GAIN		0x18
#define AIF1_RXD_CTRL		0x19
#define ADC_DIG_CTRL		0x40
#define ADC_VOL_CTRL		0x41
#define ADC_DBG_CTRL		0x42

#define HMIC_CTRL1			0x44
#define HMIC_CTRL2			0x45
#define HMIC_STS			0x46

#define DAC_DIG_CTRL		0x48
#define DAC_VOL_CTRL		0x49
#define DAC_DBG_CTRL		0x4a
#define DAC_MXR_SRC			0x4c
#define DAC_MXR_GAIN		0x4d

#define ADC_APC_CTRL		0x50
#define ADC_SRC				0x51
#define ADC_SRCBST_CTRL		0x52
#define OMIXER_DACA_CTRL	0x53
#define OMIXER_SR			0x54
#define OMIXER_BST1_CTRL	0x55
#define HPOUT_CTRL			0x56
#define ESPKOUT_CTRL		0x57
#define SPKOUT_CTRL			0x58
#define LOUT_CTRL			0x59
#define ADDA_TUNE1			0x5a
#define ADDA_TUNE2			0x5b
#define ADDA_TUNE3			0x5c
#define HPOUT_STR			0x5d

/*CHIP_AUDIO_RST*/
#define AC101_CHIP_ID			0x0101

/*PLL_CTRL1*/
#define DPLL_DAC_BIAS		14
#define PLL_POSTDIV_M		8
#define CLOSE_LOOP		6
#define INT			0

/*PLL_CTRL2*/
#define PLL_EN			15
#define PLL_LOCK_STATUS		14
#define PLL_PREDIV_NI		4
#define PLL_POSTDIV_NF		0

/*SYSCLK_CTRL*/
#define PLLCLK_ENA			15
#define PLLCLK_SRC			12
#define AIF1CLK_ENA			11
#define AIF1CLK_SRC			8
#define AIF2CLK_ENA			7
#define AIF2CLK_SRC			4
#define SYSCLK_ENA			3
#define SYSCLK_SRC			0

/*MOD_CLK_ENA*/
#define MOD_CLK_AIF1		15
#define MOD_CLK_AIF2		14
#define MOD_CLK_AIF3		13
#define MOD_CLK_SRC1		11
#define MOD_CLK_SRC2		10
#define MOD_CLK_HPF_AGC		7
#define MOD_CLK_HPF_DRC		6
#define MOD_CLK_ADC_DIG		3
#define MOD_CLK_DAC_DIG		2

/*MOD_RST_CTRL*/
#define MOD_RESET_CTL		0
#define MOD_RESET_AIF1		15
#define MOD_RESET_AIF2		14
#define MOD_RESET_AIF3		13
#define MOD_RESET_SRC1		11
#define MOD_RESET_SRC2		10
#define MOD_RESET_HPF_AGC	7
#define MOD_RESET_HPF_DRC	6
#define MOD_RESET_ADC_DIG	3
#define MOD_RESET_DAC_DIG	2

/*AIF_SR_CTRL*/
#define AIF1_FS				12	//AIF1 Sample Rate
#define AIF2_FS				8	//AIF2 Sample Rate
#define SRC1_ENA			3
#define SRC1_SRC			2
#define SRC2_ENA			1
#define SRC2_SRC			0

/*AIF1LCK_CTRL*/
#define AIF1_MSTR_MOD		15
#define AIF1_BCLK_INV		14
#define AIF1_LRCK_INV		13
#define AIF1_BCLK_DIV		9
#define AIF1_LRCK_DIV		6
#define AIF1_WORK_SIZ		4
#define AIF1_DATA_FMT		2
#define DSP_MONO_PCM		1
#define AIF1_TDMM_ENA		0

/*AIF1_ADCDAT_CTRL*/
#define AIF1_AD0L_ENA		15
#define AIF1_AD0R_ENA		14
#define AIF1_AD1L_ENA		13
#define AIF1_AD1R_ENA		12
#define AIF1_AD0L_SRC		10
#define AIF1_AD0R_SRC		8
#define AIF1_AD1L_SRC		6
#define AIF1_AD1R_SRC		4
#define AIF1_ADCP_ENA		3
#define AIF1_ADUL_ENA		2
#define AIF1_SLOT_SIZ		0

/*AIF1_DACDAT_CTRL*/
#define AIF1_DA0L_ENA		15
#define AIF1_DA0R_ENA		14
#define AIF1_DA1L_ENA		13
#define AIF1_DA1R_ENA		12
#define AIF1_DA0L_SRC		10
#define AIF1_DA0R_SRC		8
#define AIF1_DA1L_SRC		6
#define AIF1_DA1R_SRC		4
#define AIF1_DACP_ENA		3
#define AIF1_DAUL_ENA		2
#define AIF1_SLOT_SIZ		0

/*AIF1_MXR_SRC*/
#define AIF1_AD0L_AIF1_DA0L_MXR		15
#define AIF1_AD0L_AIF2_DACL_MXR		14
#define AIF1_AD0L_ADCL_MXR		13
#define AIF1_AD0L_AIF2_DACR_MXR		12
#define AIF1_AD0R_AIF1_DA0R_MXR		11
#define AIF1_AD0R_AIF2_DACR_MXR		10
#define AIF1_AD0R_ADCR_MXR		9
#define AIF1_AD0R_AIF2_DACL_MXR		8
#define AIF1_AD1L_AIF2_DACL_MXR		7
#define AIF1_AD1L_ADCL_MXR		6
#define AIF1_AD1L_MXR_SRC	6
#define AIF1_AD1R_AIF2_DACR_MXR		3
#define AIF1_AD1R_ADCR_MXR		2
#define AIF1_AD1R_MXR_SRC	2

/*AIF1_VOL_CTRL1*/
#define AIF1_AD0L_VOL		8
#define AIF1_AD0R_VOL		0

/*AIF1_VOL_CTRL2*/
#define AIF1_AD1L_VOL		8
#define AIF1_AD1R_VOL		0

/*AIF1_VOL_CTRL3*/
#define AIF1_DA0L_VOL		8
#define AIF1_DA0R_VOL		0

/*AIF1_VOL_CTRL4*/
#define AIF1_DA1L_VOL		8
#define AIF1_DA1R_VOL 		0

/*AIF1_MXR_GAIN*/
#define AIF1_AD0L_MXR_GAIN	12
#define AIF1_AD0R_MXR_GAIN	8
#define AIF1_AD1L_MXR_GAIN	6
#define AIF1_AD1R_MXR_GAIN	2

/*AIF1_RXD_CTRL*/
#define AIF1_N_DATA_DISCARD	8

/*ADC_DIG_CTRL*/
#define ENAD				15
#define ENDM				14
#define ADFIR32				13
#define ADOUT_DTS			2
#define ADOUT_DLY			1

/*ADC_VOL_CTRL*/
#define ADC_VOL_L			8
#define ADC_VOL_R			0

/*ADC_DBG_CTRL*/
#define ADSW				15
#define DMIC_CLK_PIN_CTRL 	12

/*HMIC_CTRL1*/
#define HMIC_M				12
#define HMIC_N				8
#define HMIC_DATA_IRQ_MODE	7
#define HMIC_TH1_HYSTERESIS	5
#define HMIC_PULLOUT_IRQ	4
#define HMIC_PLUGIN_IRQ		3
#define HMIC_KEYUP_IRQ		2
#define HMIC_KEYDOWN_IRQ	1
#define HMIC_DATA_IRQ_EN	0

/*HMIC_CTRL2*/
#define HMIC_SAMPLE_SELECT	14
#define HMIC_TH2_HYSTERESIS	13
#define HMIC_TH2			8
#define HMIC_SF				6
#define KEYUP_CLEAR			5
#define HMIC_TH1			0

/*HMIC_STS*/
#define HMIC_DATA			8
#define GET_HMIC_DATA(r)		(((r) >> HMIC_DATA) & 0x1F)
#define HMIC_PULLOUT_PEND	4
#define HMIC_PLUGIN_PEND	3
#define HMIC_KEYUP_PEND		2
#define HMKC_KEYDOWN_PEND	1
#define HMIC_DATA_PEND		0
#define HMIC_PEND_ALL		(0x1F)

/*DAC_DIG_CTRL*/
#define ENDA				15
#define ENHPF				14
#define DAFIR32				13
#define MODQU				8

/*DAC_VOL_CTRL*/
#define DAC_VOL_L			8
#define DAC_VOL_R			0

/*DAC_DBG_CTRL*/
#define DASW				15
#define ENDWA_N				14
#define DAC_MOD_DBG			13
#define DAC_PTN_SEL			6
#define DVC				0

/*DAC_MXR_SRC*/
#define DACL_MXR_AIF1_DA0L		15
#define DACL_MXR_AIF1_DA1L		14
#define DACL_MXR_AIF2_DACL		13
#define DACL_MXR_ADCL			12
#define DACL_MXR_SRC			12
#define DACR_MXR_AIF1_DA0R		11
#define DACR_MXR_AIF1_DA1R		10
#define DACR_MXR_AIF2_DACR		9
#define DACR_MXR_ADCR			8
#define DACR_MXR_SRC		8

/*DAC_MXR_GAIN*/
#define DACL_MXR_GAIN		12
#define DACR_MXR_GAIN		8

/*ADC_APC_CTRL*/
#define ADCREN				15
#define ADCRG				12
#define ADCLEN				11
#define ADCLG				8
#define MBIASEN				7
#define MMIC_BIAS_CHOP_EN		6
#define MMIC_BIAS_CHOP_CKS		4
#define HBIASMOD			2
#define HBIASEN				1
#define HBIASADCEN			0

/*ADC_SRC*/
#define RADCMIXMUTEMIC1BOOST	  (13)
#define RADCMIXMUTEMIC2BOOST	  (12)
#define RADCMIXMUTELINEINLR		  (11)
#define RADCMIXMUTELINEINR		  (10)
#define RADCMIXMUTEAUXINR		  (9)
#define RADCMIXMUTEROUTPUT		  (8)
#define RADCMIXMUTELOUTPUT		  (7)
#define LADCMIXMUTEMIC1BOOST	  (6)
#define LADCMIXMUTEMIC2BOOST	  (5)
#define LADCMIXMUTELINEINLR		  (4)
#define LADCMIXMUTELINEINL		  (3)
#define LADCMIXMUTEAUXINL		  (2)
#define LADCMIXMUTELOUTPUT		  (1)
#define LADCMIXMUTEROUTPUT		  (0)


/*ADC_SRCBST_CTRL*/
#define MIC1AMPEN			15
#define ADC_MIC1G			12
#define MIC2AMPEN			11
#define ADC_MIC2G			8
#define MIC2SLT				7
#define LINEIN_PREG			4
#define AUXI_PREG			0

/*OMIXER_DACA_CTRL*/
#define DACAREN				15
#define DACALEN				14
#define RMIXEN				13
#define LMIXEN				12
#define HPOUTPUTENABLE			8

/*OMIXER_SR*/
#define RMIXMUTEMIC1BOOST		  (13)
#define RMIXMUTEMIC2BOOST		  (12)
#define RMIXMUTELINEINLR		  (11)
#define RMIXMUTELINEINR			  (10)
#define RMIXMUTEAUXINR			  (9)
#define RMIXMUTEDACR			  (8)
#define RMIXMUTEDACL			  (7)
#define LMIXMUTEMIC1BOOST		  (6)
#define LMIXMUTEMIC2BOOST		  (5)
#define LMIXMUTELINEINLR		  (4)
#define LMIXMUTELINEINL			  (3)
#define LMIXMUTEAUXINL			  (2)
#define LMIXMUTEDACL			  (1)
#define LMIXMUTEDACR			  (0)

/*OMIXER_BST1_CTRL*/
#define BIASVOLTAGE			12
#define AXG				9
#define OMIXER_MIC1G			6
#define OMIXER_MIC2G			3
#define LINEING				0

/*HPOUT_CTRL*/
#define RHPS				15
#define LHPS				14
#define RHPPA_MUTE			13
#define LHPPA_MUTE			12
#define HPPA_EN				11
#define HP_VOL				4
#define HPPA_DEL			2
#define HPPA_IS				0

/*ESPKOUT_CTRL*/
#define EAR_RAMP_TIME		11
#define	ESPA_OUT_CURRENT	9
#define ESPSR				7
#define ESPPA_MUTE			6
#define ESPPA_EN			5
#define ESP_VOL				0

/*SPKOUT_CTRL*/
#define HPCALICKS			13
#define RSPKS				12
#define RSPKINVEN			11
#define RSPK_EN				9
#define LSPKS				8
#define LSPKINVEN			7
#define LSPK_EN				5
#define SPK_VOL				0

/*LOUT_CTRL*/
#define LINEOUTG			5
#define LINEOUTEN			4
#define LINEOUTS0			3
#define LINEOUTS1			2
#define LINEOUTS2			1
#define LINEOUTS3			0

/*ADDA_TUNE1*/
#define CURRENT_TEST_SELECT	14
#define BIHE_CTRL			12
#define DITHER				11
#define DITHER_CLK			9
#define ZERO_CROSSOVER_EN	8
#define ZERO_CROSSOVER_TIME 7
#define EAR_SPEED_SELECT	6
#define REF_CHOPPEN_CKS		4
#define OPMIC_BIAS_CUR		0

/*ADDA_TUNE2*/
#define OPDAC_BIAS_CUR		14
#define OPDRV_BIAS_CUR		12
#define OPMIX_BIAS_CUR		10
#define OPEAR_BIAS_CUR		8
#define OPVR_BIAS_CUR		6
#define OPAAF_BIAS_CUR		4
#define OPADC1_BIAS_CUR		2
#define OPADC2_BIAS_CUR		0

/*ADDA_TUNE3*/
#define LDOEN				15
#define LDO_SEL				12
#define BIASCALIVERIFY			11
#define BIASMODE			10
#define BIASCALIDATA			9
#define OSCS				1
#define OSCEN				0

/*HPOUT_STR*/
#define HPVL_SOFT_MOD		14
#define	HPVL_STEP_CTRL		8
#define  DACA_CHND_ENA		7
#define HPPA_MXRD_ENA		6
#define HPVL_CTRL_OUT		0

#endif//__AC101_REGS_H__
