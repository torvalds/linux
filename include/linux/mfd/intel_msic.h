/*
 * include/linux/mfd/intel_msic.h - Core interface for Intel MSIC
 *
 * Copyright (C) 2011, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_INTEL_MSIC_H__
#define __LINUX_MFD_INTEL_MSIC_H__

/* ID */
#define INTEL_MSIC_ID0			0x000	/* RO */
#define INTEL_MSIC_ID1			0x001	/* RO */

/* IRQ */
#define INTEL_MSIC_IRQLVL1		0x002
#define INTEL_MSIC_ADC1INT		0x003
#define INTEL_MSIC_CCINT		0x004
#define INTEL_MSIC_PWRSRCINT		0x005
#define INTEL_MSIC_PWRSRCINT1		0x006
#define INTEL_MSIC_CHRINT		0x007
#define INTEL_MSIC_CHRINT1		0x008
#define INTEL_MSIC_RTCIRQ		0x009
#define INTEL_MSIC_GPIO0LVIRQ		0x00a
#define INTEL_MSIC_GPIO1LVIRQ		0x00b
#define INTEL_MSIC_GPIOHVIRQ		0x00c
#define INTEL_MSIC_VRINT		0x00d
#define INTEL_MSIC_OCAUDIO		0x00e
#define INTEL_MSIC_ACCDET		0x00f
#define INTEL_MSIC_RESETIRQ1		0x010
#define INTEL_MSIC_RESETIRQ2		0x011
#define INTEL_MSIC_MADC1INT		0x012
#define INTEL_MSIC_MCCINT		0x013
#define INTEL_MSIC_MPWRSRCINT		0x014
#define INTEL_MSIC_MPWRSRCINT1		0x015
#define INTEL_MSIC_MCHRINT		0x016
#define INTEL_MSIC_MCHRINT1		0x017
#define INTEL_MSIC_RTCIRQMASK		0x018
#define INTEL_MSIC_GPIO0LVIRQMASK	0x019
#define INTEL_MSIC_GPIO1LVIRQMASK	0x01a
#define INTEL_MSIC_GPIOHVIRQMASK	0x01b
#define INTEL_MSIC_VRINTMASK		0x01c
#define INTEL_MSIC_OCAUDIOMASK		0x01d
#define INTEL_MSIC_ACCDETMASK		0x01e
#define INTEL_MSIC_RESETIRQ1MASK	0x01f
#define INTEL_MSIC_RESETIRQ2MASK	0x020
#define INTEL_MSIC_IRQLVL1MSK		0x021
#define INTEL_MSIC_PBCONFIG		0x03e
#define INTEL_MSIC_PBSTATUS		0x03f	/* RO */

/* GPIO */
#define INTEL_MSIC_GPIO0LV7CTLO		0x040
#define INTEL_MSIC_GPIO0LV6CTLO		0x041
#define INTEL_MSIC_GPIO0LV5CTLO		0x042
#define INTEL_MSIC_GPIO0LV4CTLO		0x043
#define INTEL_MSIC_GPIO0LV3CTLO		0x044
#define INTEL_MSIC_GPIO0LV2CTLO		0x045
#define INTEL_MSIC_GPIO0LV1CTLO		0x046
#define INTEL_MSIC_GPIO0LV0CTLO		0x047
#define INTEL_MSIC_GPIO1LV7CTLOS	0x048
#define INTEL_MSIC_GPIO1LV6CTLO		0x049
#define INTEL_MSIC_GPIO1LV5CTLO		0x04a
#define INTEL_MSIC_GPIO1LV4CTLO		0x04b
#define INTEL_MSIC_GPIO1LV3CTLO		0x04c
#define INTEL_MSIC_GPIO1LV2CTLO		0x04d
#define INTEL_MSIC_GPIO1LV1CTLO		0x04e
#define INTEL_MSIC_GPIO1LV0CTLO		0x04f
#define INTEL_MSIC_GPIO0LV7CTLI		0x050
#define INTEL_MSIC_GPIO0LV6CTLI		0x051
#define INTEL_MSIC_GPIO0LV5CTLI		0x052
#define INTEL_MSIC_GPIO0LV4CTLI		0x053
#define INTEL_MSIC_GPIO0LV3CTLI		0x054
#define INTEL_MSIC_GPIO0LV2CTLI		0x055
#define INTEL_MSIC_GPIO0LV1CTLI		0x056
#define INTEL_MSIC_GPIO0LV0CTLI		0x057
#define INTEL_MSIC_GPIO1LV7CTLIS	0x058
#define INTEL_MSIC_GPIO1LV6CTLI		0x059
#define INTEL_MSIC_GPIO1LV5CTLI		0x05a
#define INTEL_MSIC_GPIO1LV4CTLI		0x05b
#define INTEL_MSIC_GPIO1LV3CTLI		0x05c
#define INTEL_MSIC_GPIO1LV2CTLI		0x05d
#define INTEL_MSIC_GPIO1LV1CTLI		0x05e
#define INTEL_MSIC_GPIO1LV0CTLI		0x05f
#define INTEL_MSIC_PWM0CLKDIV1		0x061
#define INTEL_MSIC_PWM0CLKDIV0		0x062
#define INTEL_MSIC_PWM1CLKDIV1		0x063
#define INTEL_MSIC_PWM1CLKDIV0		0x064
#define INTEL_MSIC_PWM2CLKDIV1		0x065
#define INTEL_MSIC_PWM2CLKDIV0		0x066
#define INTEL_MSIC_PWM0DUTYCYCLE	0x067
#define INTEL_MSIC_PWM1DUTYCYCLE	0x068
#define INTEL_MSIC_PWM2DUTYCYCLE	0x069
#define INTEL_MSIC_GPIO0HV3CTLO		0x06d
#define INTEL_MSIC_GPIO0HV2CTLO		0x06e
#define INTEL_MSIC_GPIO0HV1CTLO		0x06f
#define INTEL_MSIC_GPIO0HV0CTLO		0x070
#define INTEL_MSIC_GPIO1HV3CTLO		0x071
#define INTEL_MSIC_GPIO1HV2CTLO		0x072
#define INTEL_MSIC_GPIO1HV1CTLO		0x073
#define INTEL_MSIC_GPIO1HV0CTLO		0x074
#define INTEL_MSIC_GPIO0HV3CTLI		0x075
#define INTEL_MSIC_GPIO0HV2CTLI		0x076
#define INTEL_MSIC_GPIO0HV1CTLI		0x077
#define INTEL_MSIC_GPIO0HV0CTLI		0x078
#define INTEL_MSIC_GPIO1HV3CTLI		0x079
#define INTEL_MSIC_GPIO1HV2CTLI		0x07a
#define INTEL_MSIC_GPIO1HV1CTLI		0x07b
#define INTEL_MSIC_GPIO1HV0CTLI		0x07c

/* SVID */
#define INTEL_MSIC_SVIDCTRL0		0x080
#define INTEL_MSIC_SVIDCTRL1		0x081
#define INTEL_MSIC_SVIDCTRL2		0x082
#define INTEL_MSIC_SVIDTXLASTPKT3	0x083	/* RO */
#define INTEL_MSIC_SVIDTXLASTPKT2	0x084	/* RO */
#define INTEL_MSIC_SVIDTXLASTPKT1	0x085	/* RO */
#define INTEL_MSIC_SVIDTXLASTPKT0	0x086	/* RO */
#define INTEL_MSIC_SVIDPKTOUTBYTE3	0x087
#define INTEL_MSIC_SVIDPKTOUTBYTE2	0x088
#define INTEL_MSIC_SVIDPKTOUTBYTE1	0x089
#define INTEL_MSIC_SVIDPKTOUTBYTE0	0x08a
#define INTEL_MSIC_SVIDRXVPDEBUG1	0x08b
#define INTEL_MSIC_SVIDRXVPDEBUG0	0x08c
#define INTEL_MSIC_SVIDRXLASTPKT3	0x08d	/* RO */
#define INTEL_MSIC_SVIDRXLASTPKT2	0x08e	/* RO */
#define INTEL_MSIC_SVIDRXLASTPKT1	0x08f	/* RO */
#define INTEL_MSIC_SVIDRXLASTPKT0	0x090	/* RO */
#define INTEL_MSIC_SVIDRXCHKSTATUS3	0x091	/* RO */
#define INTEL_MSIC_SVIDRXCHKSTATUS2	0x092	/* RO */
#define INTEL_MSIC_SVIDRXCHKSTATUS1	0x093	/* RO */
#define INTEL_MSIC_SVIDRXCHKSTATUS0	0x094	/* RO */

/* VREG */
#define INTEL_MSIC_VCCLATCH		0x0c0
#define INTEL_MSIC_VNNLATCH		0x0c1
#define INTEL_MSIC_VCCCNT		0x0c2
#define INTEL_MSIC_SMPSRAMP		0x0c3
#define INTEL_MSIC_VNNCNT		0x0c4
#define INTEL_MSIC_VNNAONCNT		0x0c5
#define INTEL_MSIC_VCC122AONCNT		0x0c6
#define INTEL_MSIC_V180AONCNT		0x0c7
#define INTEL_MSIC_V500CNT		0x0c8
#define INTEL_MSIC_VIHFCNT		0x0c9
#define INTEL_MSIC_LDORAMP1		0x0ca
#define INTEL_MSIC_LDORAMP2		0x0cb
#define INTEL_MSIC_VCC108AONCNT		0x0cc
#define INTEL_MSIC_VCC108ASCNT		0x0cd
#define INTEL_MSIC_VCC108CNT		0x0ce
#define INTEL_MSIC_VCCA100ASCNT		0x0cf
#define INTEL_MSIC_VCCA100CNT		0x0d0
#define INTEL_MSIC_VCC180AONCNT		0x0d1
#define INTEL_MSIC_VCC180CNT		0x0d2
#define INTEL_MSIC_VCC330CNT		0x0d3
#define INTEL_MSIC_VUSB330CNT		0x0d4
#define INTEL_MSIC_VCCSDIOCNT		0x0d5
#define INTEL_MSIC_VPROG1CNT		0x0d6
#define INTEL_MSIC_VPROG2CNT		0x0d7
#define INTEL_MSIC_VEMMCSCNT		0x0d8
#define INTEL_MSIC_VEMMC1CNT		0x0d9
#define INTEL_MSIC_VEMMC2CNT		0x0da
#define INTEL_MSIC_VAUDACNT		0x0db
#define INTEL_MSIC_VHSPCNT		0x0dc
#define INTEL_MSIC_VHSNCNT		0x0dd
#define INTEL_MSIC_VHDMICNT		0x0de
#define INTEL_MSIC_VOTGCNT		0x0df
#define INTEL_MSIC_V1P35CNT		0x0e0
#define INTEL_MSIC_V330AONCNT		0x0e1

/* RESET */
#define INTEL_MSIC_CHIPCNTRL		0x100	/* WO */
#define INTEL_MSIC_ERCONFIG		0x101

/* BURST */
#define INTEL_MSIC_BATCURRENTLIMIT12	0x102
#define INTEL_MSIC_BATTIMELIMIT12	0x103
#define INTEL_MSIC_BATTIMELIMIT3	0x104
#define INTEL_MSIC_BATTIMEDB		0x105
#define INTEL_MSIC_BRSTCONFIGOUTPUTS	0x106
#define INTEL_MSIC_BRSTCONFIGACTIONS	0x107
#define INTEL_MSIC_BURSTCONTROLSTATUS	0x108

/* RTC */
#define INTEL_MSIC_RTCB1		0x140	/* RO */
#define INTEL_MSIC_RTCB2		0x141	/* RO */
#define INTEL_MSIC_RTCB3		0x142	/* RO */
#define INTEL_MSIC_RTCB4		0x143	/* RO */
#define INTEL_MSIC_RTCOB1		0x144
#define INTEL_MSIC_RTCOB2		0x145
#define INTEL_MSIC_RTCOB3		0x146
#define INTEL_MSIC_RTCOB4		0x147
#define INTEL_MSIC_RTCAB1		0x148
#define INTEL_MSIC_RTCAB2		0x149
#define INTEL_MSIC_RTCAB3		0x14a
#define INTEL_MSIC_RTCAB4		0x14b
#define INTEL_MSIC_RTCWAB1		0x14c
#define INTEL_MSIC_RTCWAB2		0x14d
#define INTEL_MSIC_RTCWAB3		0x14e
#define INTEL_MSIC_RTCWAB4		0x14f
#define INTEL_MSIC_RTCSC1		0x150
#define INTEL_MSIC_RTCSC2		0x151
#define INTEL_MSIC_RTCSC3		0x152
#define INTEL_MSIC_RTCSC4		0x153
#define INTEL_MSIC_RTCSTATUS		0x154	/* RO */
#define INTEL_MSIC_RTCCONFIG1		0x155
#define INTEL_MSIC_RTCCONFIG2		0x156

/* CHARGER */
#define INTEL_MSIC_BDTIMER		0x180
#define INTEL_MSIC_BATTRMV		0x181
#define INTEL_MSIC_VBUSDET		0x182
#define INTEL_MSIC_VBUSDET1		0x183
#define INTEL_MSIC_ADPHVDET		0x184
#define INTEL_MSIC_ADPLVDET		0x185
#define INTEL_MSIC_ADPDETDBDM		0x186
#define INTEL_MSIC_LOWBATTDET		0x187
#define INTEL_MSIC_CHRCTRL		0x188
#define INTEL_MSIC_CHRCVOLTAGE		0x189
#define INTEL_MSIC_CHRCCURRENT		0x18a
#define INTEL_MSIC_SPCHARGER		0x18b
#define INTEL_MSIC_CHRTTIME		0x18c
#define INTEL_MSIC_CHRCTRL1		0x18d
#define INTEL_MSIC_PWRSRCLMT		0x18e
#define INTEL_MSIC_CHRSTWDT		0x18f
#define INTEL_MSIC_WDTWRITE		0x190	/* WO */
#define INTEL_MSIC_CHRSAFELMT		0x191
#define INTEL_MSIC_SPWRSRCINT		0x192	/* RO */
#define INTEL_MSIC_SPWRSRCINT1		0x193	/* RO */
#define INTEL_MSIC_CHRLEDPWM		0x194
#define INTEL_MSIC_CHRLEDCTRL		0x195

/* ADC */
#define INTEL_MSIC_ADC1CNTL1		0x1c0
#define INTEL_MSIC_ADC1CNTL2		0x1c1
#define INTEL_MSIC_ADC1CNTL3		0x1c2
#define INTEL_MSIC_ADC1OFFSETH		0x1c3	/* RO */
#define INTEL_MSIC_ADC1OFFSETL		0x1c4	/* RO */
#define INTEL_MSIC_ADC1ADDR0		0x1c5
#define INTEL_MSIC_ADC1ADDR1		0x1c6
#define INTEL_MSIC_ADC1ADDR2		0x1c7
#define INTEL_MSIC_ADC1ADDR3		0x1c8
#define INTEL_MSIC_ADC1ADDR4		0x1c9
#define INTEL_MSIC_ADC1ADDR5		0x1ca
#define INTEL_MSIC_ADC1ADDR6		0x1cb
#define INTEL_MSIC_ADC1ADDR7		0x1cc
#define INTEL_MSIC_ADC1ADDR8		0x1cd
#define INTEL_MSIC_ADC1ADDR9		0x1ce
#define INTEL_MSIC_ADC1ADDR10		0x1cf
#define INTEL_MSIC_ADC1ADDR11		0x1d0
#define INTEL_MSIC_ADC1ADDR12		0x1d1
#define INTEL_MSIC_ADC1ADDR13		0x1d2
#define INTEL_MSIC_ADC1ADDR14		0x1d3
#define INTEL_MSIC_ADC1SNS0H		0x1d4	/* RO */
#define INTEL_MSIC_ADC1SNS0L		0x1d5	/* RO */
#define INTEL_MSIC_ADC1SNS1H		0x1d6	/* RO */
#define INTEL_MSIC_ADC1SNS1L		0x1d7	/* RO */
#define INTEL_MSIC_ADC1SNS2H		0x1d8	/* RO */
#define INTEL_MSIC_ADC1SNS2L		0x1d9	/* RO */
#define INTEL_MSIC_ADC1SNS3H		0x1da	/* RO */
#define INTEL_MSIC_ADC1SNS3L		0x1db	/* RO */
#define INTEL_MSIC_ADC1SNS4H		0x1dc	/* RO */
#define INTEL_MSIC_ADC1SNS4L		0x1dd	/* RO */
#define INTEL_MSIC_ADC1SNS5H		0x1de	/* RO */
#define INTEL_MSIC_ADC1SNS5L		0x1df	/* RO */
#define INTEL_MSIC_ADC1SNS6H		0x1e0	/* RO */
#define INTEL_MSIC_ADC1SNS6L		0x1e1	/* RO */
#define INTEL_MSIC_ADC1SNS7H		0x1e2	/* RO */
#define INTEL_MSIC_ADC1SNS7L		0x1e3	/* RO */
#define INTEL_MSIC_ADC1SNS8H		0x1e4	/* RO */
#define INTEL_MSIC_ADC1SNS8L		0x1e5	/* RO */
#define INTEL_MSIC_ADC1SNS9H		0x1e6	/* RO */
#define INTEL_MSIC_ADC1SNS9L		0x1e7	/* RO */
#define INTEL_MSIC_ADC1SNS10H		0x1e8	/* RO */
#define INTEL_MSIC_ADC1SNS10L		0x1e9	/* RO */
#define INTEL_MSIC_ADC1SNS11H		0x1ea	/* RO */
#define INTEL_MSIC_ADC1SNS11L		0x1eb	/* RO */
#define INTEL_MSIC_ADC1SNS12H		0x1ec	/* RO */
#define INTEL_MSIC_ADC1SNS12L		0x1ed	/* RO */
#define INTEL_MSIC_ADC1SNS13H		0x1ee	/* RO */
#define INTEL_MSIC_ADC1SNS13L		0x1ef	/* RO */
#define INTEL_MSIC_ADC1SNS14H		0x1f0	/* RO */
#define INTEL_MSIC_ADC1SNS14L		0x1f1	/* RO */
#define INTEL_MSIC_ADC1BV0H		0x1f2	/* RO */
#define INTEL_MSIC_ADC1BV0L		0x1f3	/* RO */
#define INTEL_MSIC_ADC1BV1H		0x1f4	/* RO */
#define INTEL_MSIC_ADC1BV1L		0x1f5	/* RO */
#define INTEL_MSIC_ADC1BV2H		0x1f6	/* RO */
#define INTEL_MSIC_ADC1BV2L		0x1f7	/* RO */
#define INTEL_MSIC_ADC1BV3H		0x1f8	/* RO */
#define INTEL_MSIC_ADC1BV3L		0x1f9	/* RO */
#define INTEL_MSIC_ADC1BI0H		0x1fa	/* RO */
#define INTEL_MSIC_ADC1BI0L		0x1fb	/* RO */
#define INTEL_MSIC_ADC1BI1H		0x1fc	/* RO */
#define INTEL_MSIC_ADC1BI1L		0x1fd	/* RO */
#define INTEL_MSIC_ADC1BI2H		0x1fe	/* RO */
#define INTEL_MSIC_ADC1BI2L		0x1ff	/* RO */
#define INTEL_MSIC_ADC1BI3H		0x200	/* RO */
#define INTEL_MSIC_ADC1BI3L		0x201	/* RO */
#define INTEL_MSIC_CCCNTL		0x202
#define INTEL_MSIC_CCOFFSETH		0x203	/* RO */
#define INTEL_MSIC_CCOFFSETL		0x204	/* RO */
#define INTEL_MSIC_CCADCHA		0x205	/* RO */
#define INTEL_MSIC_CCADCLA		0x206	/* RO */

/* AUDIO */
#define INTEL_MSIC_AUDPLLCTRL		0x240
#define INTEL_MSIC_DMICBUF0123		0x241
#define INTEL_MSIC_DMICBUF45		0x242
#define INTEL_MSIC_DMICGPO		0x244
#define INTEL_MSIC_DMICMUX		0x245
#define INTEL_MSIC_DMICCLK		0x246
#define INTEL_MSIC_MICBIAS		0x247
#define INTEL_MSIC_ADCCONFIG		0x248
#define INTEL_MSIC_MICAMP1		0x249
#define INTEL_MSIC_MICAMP2		0x24a
#define INTEL_MSIC_NOISEMUX		0x24b
#define INTEL_MSIC_AUDIOMUX12		0x24c
#define INTEL_MSIC_AUDIOMUX34		0x24d
#define INTEL_MSIC_AUDIOSINC		0x24e
#define INTEL_MSIC_AUDIOTXEN		0x24f
#define INTEL_MSIC_HSEPRXCTRL		0x250
#define INTEL_MSIC_IHFRXCTRL		0x251
#define INTEL_MSIC_VOICETXVOL		0x252
#define INTEL_MSIC_SIDETONEVOL		0x253
#define INTEL_MSIC_MUSICSHARVOL		0x254
#define INTEL_MSIC_VOICETXCTRL		0x255
#define INTEL_MSIC_HSMIXER		0x256
#define INTEL_MSIC_DACCONFIG		0x257
#define INTEL_MSIC_SOFTMUTE		0x258
#define INTEL_MSIC_HSLVOLCTRL		0x259
#define INTEL_MSIC_HSRVOLCTRL		0x25a
#define INTEL_MSIC_IHFLVOLCTRL		0x25b
#define INTEL_MSIC_IHFRVOLCTRL		0x25c
#define INTEL_MSIC_DRIVEREN		0x25d
#define INTEL_MSIC_LINEOUTCTRL		0x25e
#define INTEL_MSIC_VIB1CTRL1		0x25f
#define INTEL_MSIC_VIB1CTRL2		0x260
#define INTEL_MSIC_VIB1CTRL3		0x261
#define INTEL_MSIC_VIB1SPIPCM_1		0x262
#define INTEL_MSIC_VIB1SPIPCM_2		0x263
#define INTEL_MSIC_VIB1CTRL5		0x264
#define INTEL_MSIC_VIB2CTRL1		0x265
#define INTEL_MSIC_VIB2CTRL2		0x266
#define INTEL_MSIC_VIB2CTRL3		0x267
#define INTEL_MSIC_VIB2SPIPCM_1		0x268
#define INTEL_MSIC_VIB2SPIPCM_2		0x269
#define INTEL_MSIC_VIB2CTRL5		0x26a
#define INTEL_MSIC_BTNCTRL1		0x26b
#define INTEL_MSIC_BTNCTRL2		0x26c
#define INTEL_MSIC_PCM1TXSLOT01		0x26d
#define INTEL_MSIC_PCM1TXSLOT23		0x26e
#define INTEL_MSIC_PCM1TXSLOT45		0x26f
#define INTEL_MSIC_PCM1RXSLOT0123	0x270
#define INTEL_MSIC_PCM1RXSLOT045	0x271
#define INTEL_MSIC_PCM2TXSLOT01		0x272
#define INTEL_MSIC_PCM2TXSLOT23		0x273
#define INTEL_MSIC_PCM2TXSLOT45		0x274
#define INTEL_MSIC_PCM2RXSLOT01		0x275
#define INTEL_MSIC_PCM2RXSLOT23		0x276
#define INTEL_MSIC_PCM2RXSLOT45		0x277
#define INTEL_MSIC_PCM1CTRL1		0x278
#define INTEL_MSIC_PCM1CTRL2		0x279
#define INTEL_MSIC_PCM1CTRL3		0x27a
#define INTEL_MSIC_PCM2CTRL1		0x27b
#define INTEL_MSIC_PCM2CTRL2		0x27c

/* HDMI */
#define INTEL_MSIC_HDMIPUEN		0x280
#define INTEL_MSIC_HDMISTATUS		0x281	/* RO */

/* Physical address of the start of the MSIC interrupt tree in SRAM */
#define INTEL_MSIC_IRQ_PHYS_BASE	0xffff7fc0

/**
 * struct intel_msic_gpio_pdata - platform data for the MSIC GPIO driver
 * @gpio_base: base number for the GPIOs
 */
struct intel_msic_gpio_pdata {
	unsigned	gpio_base;
};

/**
 * struct intel_msic_ocd_pdata - platform data for the MSIC OCD driver
 * @gpio: GPIO number used for OCD interrupts
 *
 * The MSIC MFD driver converts @gpio into an IRQ number and passes it to
 * the OCD driver as %IORESOURCE_IRQ.
 */
struct intel_msic_ocd_pdata {
	unsigned	gpio;
};

/* MSIC embedded blocks (subdevices) */
enum intel_msic_block {
	INTEL_MSIC_BLOCK_TOUCH,
	INTEL_MSIC_BLOCK_ADC,
	INTEL_MSIC_BLOCK_BATTERY,
	INTEL_MSIC_BLOCK_GPIO,
	INTEL_MSIC_BLOCK_AUDIO,
	INTEL_MSIC_BLOCK_HDMI,
	INTEL_MSIC_BLOCK_THERMAL,
	INTEL_MSIC_BLOCK_POWER_BTN,
	INTEL_MSIC_BLOCK_OCD,

	INTEL_MSIC_BLOCK_LAST,
};

/**
 * struct intel_msic_platform_data - platform data for the MSIC driver
 * @irq: array of interrupt numbers, one per device. If @irq is set to %0
 *	 for a given block, the corresponding platform device is not
 *	 created. For devices which don't have an interrupt, use %0xff
 *	 (this is same as in SFI spec).
 * @gpio: platform data for the MSIC GPIO driver
 * @ocd: platform data for the MSIC OCD driver
 *
 * Once the MSIC driver is initialized, the register interface is ready to
 * use. All the platform devices for subdevices are created after the
 * register interface is ready so that we can guarantee its availability to
 * the subdevice drivers.
 *
 * Interrupt numbers are passed to the subdevices via %IORESOURCE_IRQ
 * resources of the created platform device.
 */
struct intel_msic_platform_data {
	int				irq[INTEL_MSIC_BLOCK_LAST];
	struct intel_msic_gpio_pdata	*gpio;
	struct intel_msic_ocd_pdata	*ocd;
};

struct intel_msic;

extern int intel_msic_reg_read(unsigned short reg, u8 *val);
extern int intel_msic_reg_write(unsigned short reg, u8 val);
extern int intel_msic_reg_update(unsigned short reg, u8 val, u8 mask);
extern int intel_msic_bulk_read(unsigned short *reg, u8 *buf, size_t count);
extern int intel_msic_bulk_write(unsigned short *reg, u8 *buf, size_t count);

/*
 * pdev_to_intel_msic - gets an MSIC instance from the platform device
 * @pdev: platform device pointer
 *
 * The client drivers need to have pointer to the MSIC instance if they
 * want to call intel_msic_irq_read(). This macro can be used for
 * convenience to get the MSIC pointer from @pdev where needed. This is
 * _only_ valid for devices which are managed by the MSIC.
 */
#define pdev_to_intel_msic(pdev)	(dev_get_drvdata(pdev->dev.parent))

extern int intel_msic_irq_read(struct intel_msic *msic, unsigned short reg,
			       u8 *val);

#endif /* __LINUX_MFD_INTEL_MSIC_H__ */
