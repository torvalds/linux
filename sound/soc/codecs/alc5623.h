/*
 * alc5623.h  --  alc562[123] ALSA Soc Audio driver
 *
 * Copyright 2008 Realtek Microelectronics
 * Copyright 2010 Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 * Author: flove <flove@realtek.com>
 * Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ALC5623_H
#define _ALC5623_H

#define ALC5623_RESET				0x00
/*				5621 5622 5623  */
/* speaker output vol		   2    2       */
/* line output vol                      4    2  */
/* HP output vol		   4    0    4  */
#define ALC5623_SPK_OUT_VOL			0x02
#define ALC5623_HP_OUT_VOL			0x04
#define ALC5623_MONO_AUX_OUT_VOL		0x06
#define ALC5623_AUXIN_VOL			0x08
#define ALC5623_LINE_IN_VOL			0x0A
#define ALC5623_STEREO_DAC_VOL			0x0C
#define ALC5623_MIC_VOL				0x0E
#define ALC5623_MIC_ROUTING_CTRL		0x10
#define ALC5623_ADC_REC_GAIN			0x12
#define ALC5623_ADC_REC_MIXER			0x14
#define ALC5623_SOFT_VOL_CTRL_TIME		0x16
/* ALC5623_OUTPUT_MIXER_CTRL :			*/
/* same remark as for reg 2 line vs speaker	*/
#define ALC5623_OUTPUT_MIXER_CTRL		0x1C
#define ALC5623_MIC_CTRL			0x22

#define	ALC5623_DAI_CONTROL			0x34
#define ALC5623_DAI_SDP_MASTER_MODE		(0 << 15)
#define ALC5623_DAI_SDP_SLAVE_MODE		(1 << 15)
#define ALC5623_DAI_I2S_PCM_MODE		(1 << 14)
#define ALC5623_DAI_MAIN_I2S_BCLK_POL_CTRL	(1 <<  7)
#define ALC5623_DAI_ADC_DATA_L_R_SWAP		(1 <<  5)
#define ALC5623_DAI_DAC_DATA_L_R_SWAP		(1 <<  4)
#define ALC5623_DAI_I2S_DL_MASK			(3 <<  2)
#define ALC5623_DAI_I2S_DL_32			(3 <<  2)
#define	ALC5623_DAI_I2S_DL_24			(2 <<  2)
#define ALC5623_DAI_I2S_DL_20			(1 <<  2)
#define ALC5623_DAI_I2S_DL_16			(0 <<  2)
#define ALC5623_DAI_I2S_DF_PCM			(3 <<  0)
#define	ALC5623_DAI_I2S_DF_LEFT			(2 <<  0)
#define ALC5623_DAI_I2S_DF_RIGHT		(1 <<  0)
#define ALC5623_DAI_I2S_DF_I2S			(0 <<  0)

#define ALC5623_STEREO_AD_DA_CLK_CTRL		0x36
#define	ALC5623_COMPANDING_CTRL			0x38

#define	ALC5623_PWR_MANAG_ADD1			0x3A
#define ALC5623_PWR_ADD1_MAIN_I2S_EN		(1 << 15)
#define ALC5623_PWR_ADD1_ZC_DET_PD_EN		(1 << 14)
#define ALC5623_PWR_ADD1_MIC1_BIAS_EN		(1 << 11)
#define ALC5623_PWR_ADD1_SHORT_CURR_DET_EN	(1 << 10)
#define ALC5623_PWR_ADD1_SOFTGEN_EN		(1 <<  8) /* rsvd on 5622 */
#define	ALC5623_PWR_ADD1_DEPOP_BUF_HP		(1 <<  6) /* rsvd on 5622 */
#define	ALC5623_PWR_ADD1_HP_OUT_AMP		(1 <<  5)
#define	ALC5623_PWR_ADD1_HP_OUT_ENH_AMP		(1 <<  4) /* rsvd on 5622 */
#define ALC5623_PWR_ADD1_DEPOP_BUF_AUX		(1 <<  2)
#define ALC5623_PWR_ADD1_AUX_OUT_AMP		(1 <<  1)
#define ALC5623_PWR_ADD1_AUX_OUT_ENH_AMP	(1 <<  0) /* rsvd on 5622 */

#define ALC5623_PWR_MANAG_ADD2			0x3C
#define ALC5623_PWR_ADD2_LINEOUT		(1 << 15) /* rt5623 */
#define ALC5623_PWR_ADD2_CLASS_AB		(1 << 15) /* rt5621 */
#define ALC5623_PWR_ADD2_CLASS_D		(1 << 14) /* rt5621 */
#define ALC5623_PWR_ADD2_VREF			(1 << 13)
#define ALC5623_PWR_ADD2_PLL			(1 << 12)
#define ALC5623_PWR_ADD2_DAC_REF_CIR		(1 << 10)
#define ALC5623_PWR_ADD2_L_DAC_CLK		(1 <<  9)
#define ALC5623_PWR_ADD2_R_DAC_CLK		(1 <<  8)
#define ALC5623_PWR_ADD2_L_ADC_CLK_GAIN		(1 <<  7)
#define ALC5623_PWR_ADD2_R_ADC_CLK_GAIN		(1 <<  6)
#define ALC5623_PWR_ADD2_L_HP_MIXER		(1 <<  5)
#define ALC5623_PWR_ADD2_R_HP_MIXER		(1 <<  4)
#define ALC5623_PWR_ADD2_SPK_MIXER		(1 <<  3)
#define ALC5623_PWR_ADD2_MONO_MIXER		(1 <<  2)
#define ALC5623_PWR_ADD2_L_ADC_REC_MIXER	(1 <<  1)
#define ALC5623_PWR_ADD2_R_ADC_REC_MIXER	(1 <<  0)

#define ALC5623_PWR_MANAG_ADD3			0x3E
#define ALC5623_PWR_ADD3_MAIN_BIAS		(1 << 15)
#define ALC5623_PWR_ADD3_AUXOUT_L_VOL_AMP	(1 << 14)
#define ALC5623_PWR_ADD3_AUXOUT_R_VOL_AMP	(1 << 13)
#define ALC5623_PWR_ADD3_SPK_OUT		(1 << 12)
#define ALC5623_PWR_ADD3_HP_L_OUT_VOL		(1 << 10)
#define ALC5623_PWR_ADD3_HP_R_OUT_VOL		(1 <<  9)
#define ALC5623_PWR_ADD3_LINEIN_L_VOL		(1 <<  7)
#define ALC5623_PWR_ADD3_LINEIN_R_VOL		(1 <<  6)
#define ALC5623_PWR_ADD3_AUXIN_L_VOL		(1 <<  5)
#define ALC5623_PWR_ADD3_AUXIN_R_VOL		(1 <<  4)
#define ALC5623_PWR_ADD3_MIC1_FUN_CTRL		(1 <<  3)
#define ALC5623_PWR_ADD3_MIC2_FUN_CTRL		(1 <<  2)
#define ALC5623_PWR_ADD3_MIC1_BOOST_AD		(1 <<  1)
#define ALC5623_PWR_ADD3_MIC2_BOOST_AD		(1 <<  0)

#define ALC5623_ADD_CTRL_REG			0x40

#define	ALC5623_GLOBAL_CLK_CTRL_REG		0x42
#define ALC5623_GBL_CLK_SYS_SOUR_SEL_PLL	(1 << 15)
#define ALC5623_GBL_CLK_SYS_SOUR_SEL_MCLK	(0 << 15)
#define ALC5623_GBL_CLK_PLL_SOUR_SEL_BITCLK	(1 << 14)
#define ALC5623_GBL_CLK_PLL_SOUR_SEL_MCLK	(0 << 14)
#define ALC5623_GBL_CLK_PLL_DIV_RATIO_DIV8	(3 <<  1)
#define ALC5623_GBL_CLK_PLL_DIV_RATIO_DIV4	(2 <<  1)
#define ALC5623_GBL_CLK_PLL_DIV_RATIO_DIV2	(1 <<  1)
#define ALC5623_GBL_CLK_PLL_DIV_RATIO_DIV1	(0 <<  1)
#define ALC5623_GBL_CLK_PLL_PRE_DIV2		(1 <<  0)
#define ALC5623_GBL_CLK_PLL_PRE_DIV1		(0 <<  0)

#define ALC5623_PLL_CTRL			0x44
#define ALC5623_PLL_CTRL_N_VAL(n)		(((n)&0xff) << 8)
#define ALC5623_PLL_CTRL_K_VAL(k)		(((k)&0x7)  << 4)
#define ALC5623_PLL_CTRL_M_VAL(m)		((m)&0xf)

#define ALC5623_GPIO_OUTPUT_PIN_CTRL		0x4A
#define ALC5623_GPIO_PIN_CONFIG			0x4C
#define ALC5623_GPIO_PIN_POLARITY		0x4E
#define ALC5623_GPIO_PIN_STICKY			0x50
#define ALC5623_GPIO_PIN_WAKEUP			0x52
#define ALC5623_GPIO_PIN_STATUS			0x54
#define ALC5623_GPIO_PIN_SHARING		0x56
#define	ALC5623_OVER_CURR_STATUS		0x58
#define ALC5623_JACK_DET_CTRL			0x5A

#define ALC5623_MISC_CTRL			0x5E
#define ALC5623_MISC_DISABLE_FAST_VREG		(1 << 15)
#define ALC5623_MISC_SPK_CLASS_AB_OC_PD		(1 << 13) /* 5621 */
#define ALC5623_MISC_SPK_CLASS_AB_OC_DET	(1 << 12) /* 5621 */
#define ALC5623_MISC_HP_DEPOP_MODE3_EN		(1 << 10)
#define ALC5623_MISC_HP_DEPOP_MODE2_EN		(1 <<  9)
#define ALC5623_MISC_HP_DEPOP_MODE1_EN		(1 <<  8)
#define ALC5623_MISC_AUXOUT_DEPOP_MODE3_EN	(1 <<  6)
#define ALC5623_MISC_AUXOUT_DEPOP_MODE2_EN	(1 <<  5)
#define ALC5623_MISC_AUXOUT_DEPOP_MODE1_EN	(1 <<  4)
#define ALC5623_MISC_M_DAC_L_INPUT		(1 <<  3)
#define ALC5623_MISC_M_DAC_R_INPUT		(1 <<  2)
#define ALC5623_MISC_IRQOUT_INV_CTRL		(1 <<  0)

#define	ALC5623_PSEDUEO_SPATIAL_CTRL		0x60
#define ALC5623_EQ_CTRL				0x62
#define ALC5623_EQ_MODE_ENABLE			0x66
#define ALC5623_AVC_CTRL			0x68
#define ALC5623_HID_CTRL_INDEX			0x6A
#define ALC5623_HID_CTRL_DATA			0x6C
#define ALC5623_VENDOR_ID1			0x7C
#define ALC5623_VENDOR_ID2			0x7E

#define ALC5623_PLL_FR_MCLK			0
#define ALC5623_PLL_FR_BCK			1
#endif
