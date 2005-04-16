/*
 * linux/include/asm-arm/arch-h720x/h7202-regs.h
 *
 * Copyright (C) 2000 Jungjun Kim, Hynix Semiconductor Inc.
 *           (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *           (C) 2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *           (C) 2004 Sascha Hauer    <s.hauer@pengutronix.de>
 *
 * This file contains the hardware definitions of the h720x processors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Do not add implementations specific defines here. This files contains
 * only defines of the onchip peripherals. Add those defines to boards.h,
 * which is included by this file.
 */

#define SERIAL2_OFS		0x2d000
#define SERIAL2_BASE		(IO_PHYS + SERIAL2_OFS)
#define SERIAL2_VIRT 		(IO_VIRT + SERIAL2_OFS)
#define SERIAL3_OFS		0x2e000
#define SERIAL3_BASE		(IO_PHYS + SERIAL3_OFS)
#define SERIAL3_VIRT 		(IO_VIRT + SERIAL3_OFS)

/* Matrix Keyboard Controller */
#define KBD_VIRT		(IO_VIRT + 0x22000)
#define KBD_KBCR		0x00
#define KBD_KBSC		0x04
#define KBD_KBTR		0x08
#define KBD_KBVR0		0x0C
#define KBD_KBVR1		0x10
#define KBD_KBSR		0x18

#define KBD_KBCR_SCANENABLE	(1 << 7)
#define KBD_KBCR_NPOWERDOWN	(1 << 2)
#define KBD_KBCR_CLKSEL_MASK	(3)
#define KBD_KBCR_CLKSEL_PCLK2	0x0
#define KBD_KBCR_CLKSEL_PCLK128	0x1
#define KBD_KBCR_CLKSEL_PCLK256	0x2
#define KBD_KBCR_CLKSEL_PCLK512	0x3

#define KBD_KBSR_INTR		(1 << 0)
#define KBD_KBSR_WAKEUP		(1 << 1)

/* USB device controller */

#define USBD_BASE		(IO_VIRT + 0x12000)
#define USBD_LENGTH		0x3C

#define USBD_GCTRL		0x00
#define USBD_EPCTRL		0x04
#define USBD_INTMASK		0x08
#define USBD_INTSTAT		0x0C
#define USBD_PWR		0x10
#define USBD_DMARXTX		0x14
#define USBD_DEVID		0x18
#define USBD_DEVCLASS		0x1C
#define USBD_INTCLASS		0x20
#define USBD_SETUP0		0x24
#define USBD_SETUP1		0x28
#define USBD_ENDP0RD		0x2C
#define USBD_ENDP0WT		0x30
#define USBD_ENDP1RD		0x34
#define USBD_ENDP2WT		0x38

/* PS/2 port */
#define PSDATA 0x00
#define PSSTAT 0x04
#define PSSTAT_TXEMPTY (1<<0)
#define PSSTAT_TXBUSY (1<<1)
#define PSSTAT_RXFULL (1<<2)
#define PSSTAT_RXBUSY (1<<3)
#define PSSTAT_CLKIN (1<<4)
#define PSSTAT_DATAIN (1<<5)
#define PSSTAT_PARITY (1<<6)

#define PSCONF 0x08
#define PSCONF_ENABLE (1<<0)
#define PSCONF_TXINTEN (1<<2)
#define PSCONF_RXINTEN (1<<3)
#define PSCONF_FORCECLKLOW (1<<4)
#define PSCONF_FORCEDATLOW (1<<5)
#define PSCONF_LCE (1<<6)

#define PSINTR 0x0C
#define PSINTR_TXINT (1<<0)
#define PSINTR_RXINT (1<<1)
#define PSINTR_PAR (1<<2)
#define PSINTR_RXTO (1<<3)
#define PSINTR_TXTO (1<<4)

#define PSTDLO 0x10 /* clk low before start transmission */
#define PSTPRI 0x14 /* PRI clock */
#define PSTXMT 0x18 /* maximum transmission time */
#define PSTREC 0x20 /* maximum receive time */
#define PSPWDN 0x3c

/* ADC converter */
#define ADC_BASE 		(IO_VIRT + 0x29000)
#define ADC_CR 			0x00
#define ADC_TSCTRL 		0x04
#define ADC_BT_CTRL 		0x08
#define ADC_MC_CTRL		0x0C
#define ADC_STATUS		0x10

/* ADC control register bits */
#define ADC_CR_PW_CTRL 		0x80
#define ADC_CR_DIRECTC		0x04
#define ADC_CR_CONTIME_NO	0x00
#define ADC_CR_CONTIME_2	0x04
#define ADC_CR_CONTIME_4	0x08
#define ADC_CR_CONTIME_ADE	0x0c
#define ADC_CR_LONGCALTIME	0x01

/* ADC touch panel register bits */
#define ADC_TSCTRL_ENABLE 	0x80
#define ADC_TSCTRL_INTR   	0x40
#define	ADC_TSCTRL_SWBYPSS	0x20
#define ADC_TSCTRL_SWINVT	0x10
#define ADC_TSCTRL_S400   	0x03
#define ADC_TSCTRL_S200   	0x02
#define ADC_TSCTRL_S100   	0x01
#define ADC_TSCTRL_S50    	0x00

/* ADC Interrupt Status Register bits */
#define ADC_STATUS_TS_BIT	0x80
#define ADC_STATUS_MBT_BIT	0x40
#define ADC_STATUS_BBT_BIT	0x20
#define ADC_STATUS_MIC_BIT	0x10

/* Touch data registers */
#define ADC_TS_X0X1  		0x30
#define ADC_TS_X2X3		0x34
#define ADC_TS_Y0Y1		0x38
#define ADC_TS_Y2Y3  		0x3c
#define ADC_TS_X4X5  		0x40
#define ADC_TS_X6X7  		0x44
#define ADC_TS_Y4Y5		0x48
#define ADC_TS_Y6Y7		0x50

/* battery data */
#define ADC_MB_DATA		0x54
#define ADC_BB_DATA		0x58

/* Sound data register */
#define ADC_SD_DAT0 		0x60
#define ADC_SD_DAT1		0x64
#define ADC_SD_DAT2		0x68
#define ADC_SD_DAT3		0x6c
#define ADC_SD_DAT4		0x70
#define ADC_SD_DAT5		0x74
#define ADC_SD_DAT6		0x78
#define ADC_SD_DAT7		0x7c
