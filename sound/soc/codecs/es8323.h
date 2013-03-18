/*
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * Based on ES8323.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ES8323_H
#define _ES8323_H

#define CONFIG_HHTECH_MINIPMP	1

/* ES8323 register space */

#define ES8323_CONTROL1         0x00
#define ES8323_CONTROL2         0x01
#define ES8323_CHIPPOWER        0x02
#define ES8323_ADCPOWER         0x03
#define ES8323_DACPOWER         0x04
#define ES8323_CHIPLOPOW1       0x05
#define ES8323_CHIPLOPOW2       0x06
#define ES8323_ANAVOLMANAG      0x07
#define ES8323_MASTERMODE       0x08
#define ES8323_ADCCONTROL1      0x09
#define ES8323_ADCCONTROL2      0x0a
#define ES8323_ADCCONTROL3      0x0b
#define ES8323_ADCCONTROL4      0x0c
#define ES8323_ADCCONTROL5      0x0d
#define ES8323_ADCCONTROL6      0x0e
#define ES8323_ADCCONTROL7      0x0f
#define ES8323_ADCCONTROL8      0x10
#define ES8323_ADCCONTROL9      0x11
#define ES8323_ADCCONTROL10     0x12
#define ES8323_ADCCONTROL11     0x13
#define ES8323_ADCCONTROL12     0x14
#define ES8323_ADCCONTROL13     0x15
#define ES8323_ADCCONTROL14     0x16

#define ES8323_DACCONTROL1      0x17
#define ES8323_DACCONTROL2      0x18
#define ES8323_DACCONTROL3      0x19
#define ES8323_DACCONTROL4      0x1a
#define ES8323_DACCONTROL5      0x1b
#define ES8323_DACCONTROL6      0x1c
#define ES8323_DACCONTROL7      0x1d
#define ES8323_DACCONTROL8      0x1e
#define ES8323_DACCONTROL9      0x1f
#define ES8323_DACCONTROL10     0x20
#define ES8323_DACCONTROL11     0x21
#define ES8323_DACCONTROL12     0x22
#define ES8323_DACCONTROL13     0x23
#define ES8323_DACCONTROL14     0x24
#define ES8323_DACCONTROL15     0x25
#define ES8323_DACCONTROL16     0x26
#define ES8323_DACCONTROL17     0x27
#define ES8323_DACCONTROL18     0x28
#define ES8323_DACCONTROL19     0x29
#define ES8323_DACCONTROL20     0x2a
#define ES8323_DACCONTROL21     0x2b
#define ES8323_DACCONTROL22     0x2c
#define ES8323_DACCONTROL23     0x2d
#define ES8323_DACCONTROL24     0x2e
#define ES8323_DACCONTROL25     0x2f
#define ES8323_DACCONTROL26     0x30
#define ES8323_DACCONTROL27     0x31
#define ES8323_DACCONTROL28     0x32
#define ES8323_DACCONTROL29     0x33
#define ES8323_DACCONTROL30     0x34

#define ES8323_LADC_VOL         ES8323_ADCCONTROL8
#define ES8323_RADC_VOL         ES8323_ADCCONTROL9

#define ES8323_LDAC_VOL         ES8323_DACCONTROL4
#define ES8323_RDAC_VOL         ES8323_DACCONTROL5

#define ES8323_LOUT1_VOL        ES8323_DACCONTROL24
#define ES8323_ROUT1_VOL        ES8323_DACCONTROL25
#define ES8323_LOUT2_VOL        ES8323_DACCONTROL26
#define ES8323_ROUT2_VOL        ES8323_DACCONTROL27

#define ES8323_ADC_MUTE         ES8323_ADCCONTROL7
#define ES8323_DAC_MUTE         ES8323_DACCONTROL3



#define ES8323_IFACE            ES8323_MASTERMODE

#define ES8323_ADC_IFACE        ES8323_ADCCONTROL4
#define ES8323_ADC_SRATE        ES8323_ADCCONTROL5

#define ES8323_DAC_IFACE        ES8323_DACCONTROL1
#define ES8323_DAC_SRATE        ES8323_DACCONTROL2



#define ES8323_CACHEREGNUM      53
#define ES8323_SYSCLK	        0

struct es8323_setup_data {
	int i2c_bus;	
	unsigned short i2c_address;
};

#if 1 //lzcx
#define ES8323_PLL1			0
#define ES8323_PLL2			1

/* clock inputs */
#define ES8323_MCLK		0
#define ES8323_PCMCLK		1

/* clock divider id's */
#define ES8323_PCMDIV		0
#define ES8323_BCLKDIV		1
#define ES8323_VXCLKDIV		2

/* PCM clock dividers */
#define ES8323_PCM_DIV_1	(0 << 6)
#define ES8323_PCM_DIV_3	(2 << 6)
#define ES8323_PCM_DIV_5_5	(3 << 6)
#define ES8323_PCM_DIV_2	(4 << 6)
#define ES8323_PCM_DIV_4	(5 << 6)
#define ES8323_PCM_DIV_6	(6 << 6)
#define ES8323_PCM_DIV_8	(7 << 6)

/* BCLK clock dividers */
#define ES8323_BCLK_DIV_1	(0 << 7)
#define ES8323_BCLK_DIV_2	(1 << 7)
#define ES8323_BCLK_DIV_4	(2 << 7)
#define ES8323_BCLK_DIV_8	(3 << 7)

/* VXCLK clock dividers */
#define ES8323_VXCLK_DIV_1	(0 << 6)
#define ES8323_VXCLK_DIV_2	(1 << 6)
#define ES8323_VXCLK_DIV_4	(2 << 6)
#define ES8323_VXCLK_DIV_8	(3 << 6)
#define ES8323_VXCLK_DIV_16	(4 << 6)

#define ES8323_DAI_HIFI		0
#define ES8323_DAI_VOICE		1

#define ES8323_1536FS 1536
#define ES8323_1024FS	1024
#define ES8323_768FS	768
#define ES8323_512FS	512
#define ES8323_384FS	384
#define ES8323_256FS	256
#define ES8323_128FS	128
#endif

#endif
