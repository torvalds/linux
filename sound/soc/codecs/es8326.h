/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * es8326.h -- es8326 ALSA SoC audio driver
 *
 * Copyright (c) 2021 Everest Semiconductor Co Ltd.
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: David <zhuning@everset-semi.com>
 * Author: Xing Zheng <zhengxing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ES8326_H
#define _ES8326_H

#define CONFIG_HHTECH_MINIPMP		1

/* ES8326 register space */

#define ES8326_RESET_00			0x00
#define ES8326_CLK_CTL_01		0x01
#define ES8326_CLK_INV_02		0x02
#define ES8326_CLK_RESAMPLE_03		0x03
#define ES8326_CLK_DIV1_04		0x04
#define ES8326_CLK_DIV2_05		0x05
#define ES8326_CLK_DLL_06		0x06
#define ES8326_CLK_MUX_07		0x07
#define ES8326_CLK_ADC_SEL_08		0x08
#define ES8326_CLK_DAC_SEL_09		0x09
#define ES8326_CLK_ADC_OSR_0A		0x0a
#define ES8326_CLK_DAC_OSR_0B		0x0b
#define ES8326_CLK_DIV_CPC_0C		0x0c
#define ES8326_CLK_DIV_BCLK_0D		0x0d
#define ES8326_CLK_TRI_0E		0x0e
#define ES8326_CLK_DIV_LRCK_0F		0x0f
#define ES8326_CLK_VMIDS1_10		0x10
#define ES8326_CLK_VMIDS2_11		0x11
#define ES8326_CLK_CAL_TIME_12		0x12
#define ES8326_FMT_13			0x13

#define ES8326_DAC_MUTE_14		0x14
#define ES8326_ADC_MUTE_15		0x15
#define ES8326_ANA_PDN_16		0x16
#define ES8326_PGA_PDN_17		0x17
#define ES8326_VMIDSEL_18		0x18
#define ES8326_ANA_LOWPOWER_19		0x19
#define ES8326_ANA_DMS_1A		0x1a
#define ES8326_ANA_MICBIAS_1B		0x1b
#define ES8326_ANA_VSEL_1C		0x1c
#define ES8326_SYS_BIAS_1D		0x1d
#define ES8326_BIAS_SW1_1E		0x1e
#define ES8326_BIAS_SW2_1F		0x1f
#define ES8326_BIAS_SW3_20		0x20
#define ES8326_BIAS_SW4_21		0x21
#define ES8326_VMIDLOW_22		0x22

#define ES8326_PAGGAIN_23		0x23
#define ES8326_HP_DRVIER_24		0x24
#define ES8326_DAC2HPMIX_25		0x25
#define ES8326_HP_VOL_26		0x26
#define ES8326_HP_CAL_27		0x27
#define ES8326_HP_DRIVER_REF_28		0x28
#define ES8326_ADC_SCALE_29		0x29
#define ES8326_ADC1_SRC_2A		0x2a
#define ES8326_ADC2_SRC_2B		0x2b
#define ES8326_ADC1_VOL_2C		0x2c
#define ES8326_ADC2_VOL_2D		0x2d
#define ES8326_ADC_RAMPRATE_2E		0x2e
#define ES8326_2F			0x2f
#define ES8326_30			0x30
#define ES8326_31			0x31
#define ES8326_ALC_RECOVERY_32		0x32
#define ES8326_ALC_LEVEL_33		0x33
#define ES8326_ADC_HPFS1_34		0x34
#define ES8326_ADC_HPFS2_35		0x35
#define ES8326_ADC_EQ_36		0x36
#define ES8326_HP_CAL_4A		0x4A
#define ES8326_HPL_OFFSET_INI_4B	0x4B
#define ES8326_HPR_OFFSET_INI_4C	0x4C
#define ES8326_DAC_DSM_4D		0x4D
#define ES8326_DAC_RAMPRATE_4E		0x4E
#define ES8326_DAC_VPPSCALE_4F		0x4F
#define ES8326_DAC_VOL_50		0x50
#define ES8326_DRC_RECOVERY_53		0x53
#define ES8326_DRC_WINSIZE_54		0x54
#define ES8326_HPJACK_TIMER_56		0x56
#define ES8326_HPJACK_POL_57		0x57
#define ES8326_INT_SOURCE_58		0x58
#define ES8326_INTOUT_IO_59		0x59
#define ES8326_SDINOUT1_IO_5A		0x5A
#define ES8326_SDINOUT23_IO_5B		0x5B
#define ES8326_JACK_PULSE_5C		0x5C

#define ES8326_PULLUP_CTL_F9		0xF9
#define ES8326_HP_DECTECT_FB		0xFB
#define ES8326_CHIP_ID1_FD		0xFD
#define ES8326_CHIP_ID2_FE		0xFE
#define ES8326_CHIP_VERSION_FF		0xFF

#define ES8326_LADC_VOL			ES8326_ADCCONTROL8
#define ES8326_RADC_VOL			ES8326_ADCCONTROL9

#define ES8326_LDAC_VOL			ES8326_DACCONTROL4
#define ES8326_RDAC_VOL			ES8326_DACCONTROL5

#define ES8326_LOUT1_VOL		ES8326_DACCONTROL24
#define ES8326_ROUT1_VOL		ES8326_DACCONTROL25
#define ES8326_LOUT2_VOL		ES8326_DACCONTROL26
#define ES8326_ROUT2_VOL		ES8326_DACCONTROL27

#define ES8326_ADC_MUTE			ES8326_ADCCONTROL7
#define ES8326_DAC_MUTE			ES8326_DACCONTROL3

#define ES8326_IFACE			ES8326_MASTERMODE

#define ES8326_ADC_IFACE		ES8326_ADCCONTROL4
#define ES8326_ADC_SRATE		ES8326_ADCCONTROL5

#define ES8326_DAC_IFACE		ES8326_DACCONTROL1
#define ES8326_DAC_SRATE		ES8326_DACCONTROL2

#define ES8326_CACHEREGNUM		53
#define ES8326_SYSCLK			0

#define ES8326_PLL1			0
#define ES8326_PLL2			1

/* clock inputs */
#define ES8326_MCLK			0
#define ES8326_PCMCLK			1

/* clock divider id's */
#define ES8326_PCMDIV			0
#define ES8326_BCLKDIV			1
#define ES8326_VXCLKDIV			2

/* PCM clock dividers */
#define ES8326_PCM_DIV_1		(0 << 6)
#define ES8326_PCM_DIV_3		(2 << 6)
#define ES8326_PCM_DIV_5_5		(3 << 6)
#define ES8326_PCM_DIV_2		(4 << 6)
#define ES8326_PCM_DIV_4		(5 << 6)
#define ES8326_PCM_DIV_6		(6 << 6)
#define ES8326_PCM_DIV_8		(7 << 6)

/* BCLK clock dividers */
#define ES8326_BCLK_DIV_1		(0 << 7)
#define ES8326_BCLK_DIV_2		(1 << 7)
#define ES8326_BCLK_DIV_4		(2 << 7)
#define ES8326_BCLK_DIV_8		(3 << 7)

/* VXCLK clock dividers */
#define ES8326_VXCLK_DIV_1		(0 << 6)
#define ES8326_VXCLK_DIV_2		(1 << 6)
#define ES8326_VXCLK_DIV_4		(2 << 6)
#define ES8326_VXCLK_DIV_8		(3 << 6)
#define ES8326_VXCLK_DIV_16		(4 << 6)

#define ES8326_DAI_HIFI			0
#define ES8326_DAI_VOICE		1

#define ES8326_1536FS			1536
#define ES8326_1024FS			1024
#define ES8326_768FS			768
#define ES8326_512FS			512
#define ES8326_384FS			384
#define ES8326_256FS			256
#define ES8326_128FS			128

#endif /* _ES8326_H */
