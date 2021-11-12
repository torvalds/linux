/*
 * ALSA SoC ES7202 pdm adc driver
 *
 * Author:      David Yang, <yangxiaohua@everest-semi.com>
 * Copyright:   (C) 2020 Everest Semiconductor Co Ltd.,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ES7202_H
#define _ES7202_H

/* ES7202 register space */
#define ES7202_RESET_REG00        	0x00
#define ES7202_SOFT_MODE_REG01			0x01
#define ES7202_CLK_DIV_REG02   			0x02
#define ES7202_CLK_EN_REG03     		0x03
#define ES7202_T1_VMID_REG04    		0x04
#define ES7202_T2_VMID_REG05    		0x05
#define ES7202_CHIP_STA_REG06    		0x06
#define ES7202_PDM_INF_CTL_REG07  	0x07
#define ES7202_MISC_CTL_REG08     	0x08
#define ES7202_ANALOG_EN_REG10    	0x10
#define ES7202_BIAS_VMID_REG11    	0x11
#define ES7202_PGA1_BIAS_REG12    	0x12
#define ES7202_PGA2_BIAS_REG13    	0x13
#define ES7202_MOD1_BIAS_REG14    	0x14
#define ES7202_MOD2_BIAS_REG15    	0x15
#define ES7202_VREFP_BIAS_REG16     0x16
#define ES7202_VMMOD_BIAS_REG17     0x17
#define ES7202_MODS_BIAS_REG18      0x18
#define ES7202_ANALOG_LP1_REG19     0x19
#define ES7202_ANALOG_LP2_REG1A     0x1A
#define ES7202_ANALOG_MISC1_REG1B   0x1B
#define ES7202_ANALOG_MISC2_REG1C   0x1C
#define ES7202_PGA1_REG1D     			0x1D
#define ES7202_PGA2_REG1E     			0x1E

/* ES7202 User Marco define */
#define VDD_1V8   0
#define VDD_3V3   1
#define ES7202_VDD_VOLTAGE	    VDD_3V3

/*
* Select Microphone channels for mic array
*/
#define MIC_CHN_16      16
#define MIC_CHN_14      14
#define MIC_CHN_12      12
#define MIC_CHN_10      10
#define MIC_CHN_8       8
#define MIC_CHN_6       6
#define MIC_CHN_4       4
#define MIC_CHN_2       2

#define ES7202_CHANNELS_MAX     CONFIG_SND_SOC_ES7202_MIC_MAX_CHANNELS

#if ES7202_CHANNELS_MAX == MIC_CHN_2
#define ADC_DEV_MAXNUM  1
#endif
#if ES7202_CHANNELS_MAX == MIC_CHN_4
#define ADC_DEV_MAXNUM  2
#endif
#if ES7202_CHANNELS_MAX == MIC_CHN_6
#define ADC_DEV_MAXNUM  3
#endif
#if ES7202_CHANNELS_MAX == MIC_CHN_8
#define ADC_DEV_MAXNUM  4
#endif
#if ES7202_CHANNELS_MAX == MIC_CHN_10
#define ADC_DEV_MAXNUM  5
#endif
#if ES7202_CHANNELS_MAX == MIC_CHN_12
#define ADC_DEV_MAXNUM  6
#endif
#if ES7202_CHANNELS_MAX == MIC_CHN_14
#define ADC_DEV_MAXNUM  7
#endif
#if ES7202_CHANNELS_MAX == MIC_CHN_16
#define ADC_DEV_MAXNUM  8
#endif

/* select I2C bus number for es7202 */
#define ES7202_I2C_BUS_NUM	CONFIG_SND_SOC_ES7202_I2C_BUS

/* 
* select DTS or I2C Detect method for es7202 
* 0: i2c_detect, 1:of_device_id
*/
#define ES7202_MATCH_DTS_EN		1	

#endif
