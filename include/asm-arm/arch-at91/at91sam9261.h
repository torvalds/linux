/*
 * include/asm-arm/arch-at91/at91sam9261.h
 *
 * Copyright (C) SAN People
 *
 * Common definitions.
 * Based on AT91SAM9261 datasheet revision E. (Preliminary)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91SAM9261_H
#define AT91SAM9261_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91_ID_FIQ		0	/* Advanced Interrupt Controller (FIQ) */
#define AT91_ID_SYS		1	/* System Peripherals */
#define AT91SAM9261_ID_PIOA	2	/* Parallel IO Controller A */
#define AT91SAM9261_ID_PIOB	3	/* Parallel IO Controller B */
#define AT91SAM9261_ID_PIOC	4	/* Parallel IO Controller C */
#define AT91SAM9261_ID_US0	6	/* USART 0 */
#define AT91SAM9261_ID_US1	7	/* USART 1 */
#define AT91SAM9261_ID_US2	8	/* USART 2 */
#define AT91SAM9261_ID_MCI	9	/* Multimedia Card Interface */
#define AT91SAM9261_ID_UDP	10	/* USB Device Port */
#define AT91SAM9261_ID_TWI	11	/* Two-Wire Interface */
#define AT91SAM9261_ID_SPI0	12	/* Serial Peripheral Interface 0 */
#define AT91SAM9261_ID_SPI1	13	/* Serial Peripheral Interface 1 */
#define AT91SAM9261_ID_SSC0	14	/* Serial Synchronous Controller 0 */
#define AT91SAM9261_ID_SSC1	15	/* Serial Synchronous Controller 1 */
#define AT91SAM9261_ID_SSC2	16	/* Serial Synchronous Controller 2 */
#define AT91SAM9261_ID_TC0	17	/* Timer Counter 0 */
#define AT91SAM9261_ID_TC1	18	/* Timer Counter 1 */
#define AT91SAM9261_ID_TC2	19	/* Timer Counter 2 */
#define AT91SAM9261_ID_UHP	20	/* USB Host port */
#define AT91SAM9261_ID_LCDC	21	/* LDC Controller */
#define AT91SAM9261_ID_IRQ0	29	/* Advanced Interrupt Controller (IRQ0) */
#define AT91SAM9261_ID_IRQ1	30	/* Advanced Interrupt Controller (IRQ1) */
#define AT91SAM9261_ID_IRQ2	31	/* Advanced Interrupt Controller (IRQ2) */


/*
 * User Peripheral physical base addresses.
 */
#define AT91SAM9261_BASE_TCB0		0xfffa0000
#define AT91SAM9261_BASE_TC0		0xfffa0000
#define AT91SAM9261_BASE_TC1		0xfffa0040
#define AT91SAM9261_BASE_TC2		0xfffa0080
#define AT91SAM9261_BASE_UDP		0xfffa4000
#define AT91SAM9261_BASE_MCI		0xfffa8000
#define AT91SAM9261_BASE_TWI		0xfffac000
#define AT91SAM9261_BASE_US0		0xfffb0000
#define AT91SAM9261_BASE_US1		0xfffb4000
#define AT91SAM9261_BASE_US2		0xfffb8000
#define AT91SAM9261_BASE_SSC0		0xfffbc000
#define AT91SAM9261_BASE_SSC1		0xfffc0000
#define AT91SAM9261_BASE_SSC2		0xfffc4000
#define AT91SAM9261_BASE_SPI0		0xfffc8000
#define AT91SAM9261_BASE_SPI1		0xfffcc000
#define AT91_BASE_SYS			0xffffea00


/*
 * System Peripherals (offset from AT91_BASE_SYS)
 */
#define AT91_SDRAMC	(0xffffea00 - AT91_BASE_SYS)
#define AT91_SMC	(0xffffec00 - AT91_BASE_SYS)
#define AT91_MATRIX	(0xffffee00 - AT91_BASE_SYS)
#define AT91_AIC	(0xfffff000 - AT91_BASE_SYS)
#define AT91_DBGU	(0xfffff200 - AT91_BASE_SYS)
#define AT91_PIOA	(0xfffff400 - AT91_BASE_SYS)
#define AT91_PIOB	(0xfffff600 - AT91_BASE_SYS)
#define AT91_PIOC	(0xfffff800 - AT91_BASE_SYS)
#define AT91_PMC	(0xfffffc00 - AT91_BASE_SYS)
#define AT91_RSTC	(0xfffffd00 - AT91_BASE_SYS)
#define AT91_SHDWC	(0xfffffd10 - AT91_BASE_SYS)
#define AT91_RTT	(0xfffffd20 - AT91_BASE_SYS)
#define AT91_PIT	(0xfffffd30 - AT91_BASE_SYS)
#define AT91_WDT	(0xfffffd40 - AT91_BASE_SYS)
#define AT91_GPBR	(0xfffffd50 - AT91_BASE_SYS)


/*
 * Internal Memory.
 */
#define AT91SAM9261_SRAM_BASE	0x00300000	/* Internal SRAM base address */
#define AT91SAM9261_SRAM_SIZE	0x00028000	/* Internal SRAM size (160Kb) */

#define AT91SAM9261_ROM_BASE	0x00400000	/* Internal ROM base address */
#define AT91SAM9261_ROM_SIZE	SZ_32K		/* Internal ROM size (32Kb) */

#define AT91SAM9261_UHP_BASE	0x00500000	/* USB Host controller */
#define AT91SAM9261_LCDC_BASE	0x00600000	/* LDC controller */


#if 0
/*
 * PIO pin definitions (peripheral A/B multiplexing).
 */
#define AT91_PA0_SPI0_MISO	(1 <<  0)	/* A: SPI0 Master In Slave */
#define AT91_PA0_MCDA0		(1 <<  0)	/* B: Multimedia Card A Data 0 */
#define AT91_PA1_SPI0_MOSI	(1 <<  1)	/* A: SPI0 Master Out Slave */
#define AT91_PA1_MCCDA		(1 <<  1)	/* B: Multimedia Card A Command */
#define AT91_PA2_SPI0_SPCK	(1 <<  2)	/* A: SPI0 Serial Clock */
#define AT91_PA2_MCCK		(1 <<  2)	/* B: Multimedia Card Clock */
#define AT91_PA3_SPI0_NPCS0	(1 <<  3)	/* A: SPI0 Peripheral Chip Select 0 */
#define AT91_PA4_SPI0_NPCS1	(1 <<  4)	/* A: SPI0 Peripheral Chip Select 1 */
#define AT91_PA4_MCDA1		(1 <<  4)	/* B: Multimedia Card A Data 1 */
#define AT91_PA5_SPI0_NPCS2	(1 <<  5)	/* A: SPI0 Peripheral Chip Select 2 */
#define AT91_PA5_MCDA2		(1 <<  5)	/* B: Multimedia Card A Data 2 */
#define AT91_PA6_SPI0_NPCS3	(1 <<  6)	/* A: SPI0 Peripheral Chip Select 3 */
#define AT91_PA6_MCDA3		(1 <<  6)	/* B: Multimedia Card A Data 3 */
#define AT91_PA7_TWD		(1 <<  7)	/* A: TWI Two-wire Serial Data */
#define AT91_PA7_PCK0		(1 <<  7)	/* B: PMC Programmable clock Output 0 */
#define AT91_PA8_TWCK		(1 <<  8)	/* A: TWI Two-wire Serial Clock */
#define AT91_PA8_PCK1		(1 <<  8)	/* B: PMC Programmable clock Output 1 */
#define AT91_PA9_DRXD		(1 <<  9)	/* A: DBGU Debug Receive Data */
#define AT91_PA9_PCK2		(1 <<  9)	/* B: PMC Programmable clock Output 2 */
#define AT91_PA10_DTXD		(1 << 10)	/* A: DBGU Debug Transmit Data */
#define AT91_PA10_PCK3		(1 << 10)	/* B: PMC Programmable clock Output 3 */
#define AT91_PA11_TSYNC		(1 << 11)	/* A: Trace Synchronization Signal */
#define AT91_PA11_SCK1		(1 << 11)	/* B: USART1 Serial Clock */
#define AT91_PA12_TCLK		(1 << 12)	/* A: Trace Clock */
#define AT91_PA12_RTS1		(1 << 12)	/* B: USART1 Ready To Send */
#define AT91_PA13_TPS0		(1 << 13)	/* A: Trace ARM Pipeline Status 0 */
#define AT91_PA13_CTS1		(1 << 13)	/* B: USART1 Clear To Send */
#define AT91_PA14_TPS1		(1 << 14)	/* A: Trace ARM Pipeline Status 1 */
#define AT91_PA14_SCK2		(1 << 14)	/* B: USART2 Serial Clock */
#define AT91_PA15_TPS2		(1 << 15)	/* A: Trace ARM Pipeline Status 2 */
#define AT91_PA15_RTS2		(1 << 15)	/* B: USART2 Ready To Send */
#define AT91_PA16_TPK0		(1 << 16)	/* A: Trace Packet Port 0 */
#define AT91_PA16_CTS2		(1 << 16)	/* B: USART2 Clear To Send */
#define AT91_PA17_TPK1		(1 << 17)	/* A: Trace Packet Port 1 */
#define AT91_PA17_TF1		(1 << 17)	/* B: SSC1 Transmit Frame Sync */
#define AT91_PA18_TPK2		(1 << 18)	/* A: Trace Packet Port 2 */
#define AT91_PA18_TK1		(1 << 18)	/* B: SSC1 Transmit Clock */
#define AT91_PA19_TPK3		(1 << 19)	/* A: Trace Packet Port 3 */
#define AT91_PA19_TD1		(1 << 19)	/* B: SSC1 Transmit Data */
#define AT91_PA20_TPK4		(1 << 20)	/* A: Trace Packet Port 4 */
#define AT91_PA20_RD1		(1 << 20)	/* B: SSC1 Receive Data */
#define AT91_PA21_TPK5		(1 << 21)	/* A: Trace Packet Port 5 */
#define AT91_PA21_RK1		(1 << 21)	/* B: SSC1 Receive Clock */
#define AT91_PA22_TPK6		(1 << 22)	/* A: Trace Packet Port 6 */
#define AT91_PA22_RF1		(1 << 22)	/* B: SSC1 Receive Frame Sync */
#define AT91_PA23_TPK7		(1 << 23)	/* A: Trace Packet Port 7 */
#define AT91_PA23_RTS0		(1 << 23)	/* B: USART0 Ready To Send */
#define AT91_PA24_TPK8		(1 << 24)	/* A: Trace Packet Port 8 */
#define AT91_PA24_SPI1_NPCS1	(1 << 24)	/* B: SPI1 Peripheral Chip Select 1 */
#define AT91_PA25_TPK9		(1 << 25)	/* A: Trace Packet Port 9 */
#define AT91_PA25_SPI1_NPCS2	(1 << 25)	/* B: SPI1 Peripheral Chip Select 2 */
#define AT91_PA26_TPK10		(1 << 26)	/* A: Trace Packet Port 10 */
#define AT91_PA26_SPI1_NPCS3	(1 << 26)	/* B: SPI1 Peripheral Chip Select 3 */
#define AT91_PA27_TPK11		(1 << 27)	/* A: Trace Packet Port 11 */
#define AT91_PA27_SPI0_NPCS1	(1 << 27)	/* B: SPI0 Peripheral Chip Select 1 */
#define AT91_PA28_TPK12		(1 << 28)	/* A: Trace Packet Port 12 */
#define AT91_PA28_SPI0_NPCS2	(1 << 28)	/* B: SPI0 Peripheral Chip Select 2 */
#define AT91_PA29_TPK13		(1 << 29)	/* A: Trace Packet Port 13 */
#define AT91_PA29_SPI0_NPCS3	(1 << 29)	/* B: SPI0 Peripheral Chip Select 3 */
#define AT91_PA30_TPK14		(1 << 30)	/* A: Trace Packet Port 14 */
#define AT91_PA30_A23		(1 << 30)	/* B: Address Bus bit 23 */
#define AT91_PA31_TPK15		(1 << 31)	/* A: Trace Packet Port 15 */
#define AT91_PA31_A24		(1 << 31)	/* B: Address Bus bit 24 */

#define AT91_PB0_LCDVSYNC	(1 <<  0)	/* A: LCD Vertical Synchronization */
#define AT91_PB1_LCDHSYNC	(1 <<  1)	/* A: LCD Horizontal Synchronization */
#define AT91_PB2_LCDDOTCK	(1 <<  2)	/* A: LCD Dot Clock */
#define AT91_PB2_PCK0		(1 <<  2)	/* B: PMC Programmable clock Output 0 */
#define AT91_PB3_LCDDEN		(1 <<  3)	/* A: LCD Data Enable */
#define AT91_PB4_LCDCC		(1 <<  4)	/* A: LCD Contrast Control */
#define AT91_PB4_LCDD2		(1 <<  4)	/* B: LCD Data Bus Bit 2 */
#define AT91_PB5_LCDD0		(1 <<  5)	/* A: LCD Data Bus Bit 0 */
#define AT91_PB5_LCDD3		(1 <<  5)	/* B: LCD Data Bus Bit 3 */
#define AT91_PB6_LCDD1		(1 <<  6)	/* A: LCD Data Bus Bit 1 */
#define AT91_PB6_LCDD4		(1 <<  6)	/* B: LCD Data Bus Bit 4 */
#define AT91_PB7_LCDD2		(1 <<  7)	/* A: LCD Data Bus Bit 2 */
#define AT91_PB7_LCDD5		(1 <<  7)	/* B: LCD Data Bus Bit 5 */
#define AT91_PB8_LCDD3		(1 <<  8)	/* A: LCD Data Bus Bit 3 */
#define AT91_PB8_LCDD6		(1 <<  8)	/* B: LCD Data Bus Bit 6 */
#define AT91_PB9_LCDD4		(1 <<  9)	/* A: LCD Data Bus Bit 4 */
#define AT91_PB9_LCDD7		(1 <<  9)	/* B: LCD Data Bus Bit 7 */
#define AT91_PB10_LCDD5		(1 << 10)	/* A: LCD Data Bus Bit 5 */
#define AT91_PB10_LCDD10	(1 << 10)	/* B: LCD Data Bus Bit 10 */
#define AT91_PB11_LCDD6		(1 << 11)	/* A: LCD Data Bus Bit 6 */
#define AT91_PB11_LCDD11	(1 << 11)	/* B: LCD Data Bus Bit 11 */
#define AT91_PB12_LCDD7		(1 << 12)	/* A: LCD Data Bus Bit 7 */
#define AT91_PB12_LCDD12	(1 << 12)	/* B: LCD Data Bus Bit 12 */
#define AT91_PB13_LCDD8		(1 << 13)	/* A: LCD Data Bus Bit 8 */
#define AT91_PB13_LCDD13	(1 << 13)	/* B: LCD Data Bus Bit 13 */
#define AT91_PB14_LCDD9		(1 << 14)	/* A: LCD Data Bus Bit 9 */
#define AT91_PB14_LCDD14	(1 << 14)	/* B: LCD Data Bus Bit 14 */
#define AT91_PB15_LCDD10	(1 << 15)	/* A: LCD Data Bus Bit 10 */
#define AT91_PB15_LCDD15	(1 << 15)	/* B: LCD Data Bus Bit 15 */
#define AT91_PB16_LCDD11	(1 << 16)	/* A: LCD Data Bus Bit 11 */
#define AT91_PB16_LCDD19	(1 << 16)	/* B: LCD Data Bus Bit 19 */
#define AT91_PB17_LCDD12	(1 << 17)	/* A: LCD Data Bus Bit 12 */
#define AT91_PB17_LCDD20	(1 << 17)	/* B: LCD Data Bus Bit 20 */
#define AT91_PB18_LCDD13	(1 << 18)	/* A: LCD Data Bus Bit 13 */
#define AT91_PB18_LCDD21	(1 << 18)	/* B: LCD Data Bus Bit 21 */
#define AT91_PB19_LCDD14	(1 << 19)	/* A: LCD Data Bus Bit 14 */
#define AT91_PB19_LCDD22	(1 << 19)	/* B: LCD Data Bus Bit 22 */
#define AT91_PB20_LCDD15	(1 << 20)	/* A: LCD Data Bus Bit 15 */
#define AT91_PB20_LCDD23	(1 << 20)	/* B: LCD Data Bus Bit 23 */
#define AT91_PB21_TF0		(1 << 21)	/* A: SSC0 Transmit Frame Sync */
#define AT91_PB21_LCDD16	(1 << 21)	/* B: LCD Data Bus Bit 16 */
#define AT91_PB22_TK0		(1 << 22)	/* A: SSC0 Transmit Clock */
#define AT91_PB22_LCDD17	(1 << 22)	/* B: LCD Data Bus Bit 17 */
#define AT91_PB23_TD0		(1 << 23)	/* A: SSC0 Transmit Data */
#define AT91_PB23_LCDD18	(1 << 23)	/* B: LCD Data Bus Bit 18 */
#define AT91_PB24_RD0		(1 << 24)	/* A: SSC0 Receive Data */
#define AT91_PB24_LCDD19	(1 << 24)	/* B: LCD Data Bus Bit 19 */
#define AT91_PB25_RK0		(1 << 25)	/* A: SSC0 Receive Clock */
#define AT91_PB25_LCDD20	(1 << 25)	/* B: LCD Data Bus Bit 20 */
#define AT91_PB26_RF0		(1 << 26)	/* A: SSC0 Receive Frame Sync */
#define AT91_PB26_LCDD21	(1 << 26)	/* B: LCD Data Bus Bit 21 */
#define AT91_PB27_SPI1_NPCS1	(1 << 27)	/* A: SPI1 Peripheral Chip Select 1 */
#define AT91_PB27_LCDD22	(1 << 27)	/* B: LCD Data Bus Bit 22 */
#define AT91_PB28_SPI1_NPCS0	(1 << 28)	/* A: SPI1 Peripheral Chip Select 0 */
#define AT91_PB28_LCDD23	(1 << 28)	/* B: LCD Data Bus Bit 23 */
#define AT91_PB29_SPI1_SPCK	(1 << 29)	/* A: SPI1 Serial Clock */
#define AT91_PB29_IRQ2		(1 << 29)	/* B: Interrupt input 2 */
#define AT91_PB30_SPI1_MISO	(1 << 30)	/* A: SPI1 Master In Slave */
#define AT91_PB30_IRQ1		(1 << 30)	/* B: Interrupt input 1 */
#define AT91_PB31_SPI1_MOSI	(1 << 31)	/* A: SPI1 Master Out Slave */
#define AT91_PB31_PCK2		(1 << 31)	/* B: PMC Programmable clock Output 2 */

#define AT91_PC0_SMOE		(1 << 0)	/* A: SmartMedia Output Enable */
#define AT91_PC0_NCS6		(1 << 0)	/* B: Chip Select 6 */
#define AT91_PC1_SMWE		(1 << 1)	/* A: SmartMedia Write Enable */
#define AT91_PC1_NCS7		(1 << 1)	/* B: Chip Select 7 */
#define AT91_PC2_NWAIT		(1 << 2)	/* A: NWAIT */
#define AT91_PC2_IRQ0		(1 << 2)	/* B: Interrupt input 0 */
#define AT91_PC3_A25_CFRNW	(1 << 3)	/* A: Address Bus[25] / Compact Flash Read Not Write */
#define AT91_PC4_NCS4_CFCS0	(1 << 4)	/* A: Chip Select 4 / CompactFlash Chip Select 0 */
#define AT91_PC5_NCS5_CFCS1	(1 << 5)	/* A: Chip Select 5 / CompactFlash Chip Select 1 */
#define AT91_PC6_CFCE1		(1 << 6)	/* A: CompactFlash Chip Enable 1 */
#define AT91_PC7_CFCE2		(1 << 7)	/* A: CompactFlash Chip Enable 2 */
#define AT91_PC8_TXD0		(1 << 8)	/* A: USART0 Transmit Data */
#define AT91_PC8_PCK2		(1 << 8)	/* B: PMC Programmable clock Output 2 */
#define AT91_PC9_RXD0		(1 << 9)	/* A: USART0 Receive Data */
#define AT91_PC9_PCK3		(1 << 9)	/* B: PMC Programmable clock Output 3 */
#define AT91_PC10_RTS0		(1 << 10)	/* A: USART0 Ready To Send */
#define AT91_PC10_SCK0		(1 << 10)	/* B: USART0 Serial Clock */
#define AT91_PC11_CTS0		(1 << 11)	/* A: USART0 Clear To Send */
#define AT91_PC11_FIQ		(1 << 11)	/* B: AIC Fast Interrupt Input */
#define AT91_PC12_TXD1		(1 << 12)	/* A: USART1 Transmit Data */
#define AT91_PC12_NCS6		(1 << 12)	/* B: Chip Select 6 */
#define AT91_PC13_RXD1		(1 << 13)	/* A: USART1 Receive Data */
#define AT91_PC13_NCS7		(1 << 13)	/* B: Chip Select 7 */
#define AT91_PC14_TXD2		(1 << 14)	/* A: USART2 Transmit Data */
#define AT91_PC14_SPI1_NPCS2	(1 << 14)	/* B: SPI1 Peripheral Chip Select 2 */
#define AT91_PC15_RXD2		(1 << 15)	/* A: USART2 Receive Data */
#define AT91_PC15_SPI1_NPCS3	(1 << 15)	/* B: SPI1 Peripheral Chip Select 3 */
#define AT91_PC16_D16		(1 << 16)	/* A: Data Bus [16] */
#define AT91_PC16_TCLK0		(1 << 16)	/* B: Timer Counter 0 external clock input */
#define AT91_PC17_D17		(1 << 17)	/* A: Data Bus [17] */
#define AT91_PC17_TCLK1		(1 << 17)	/* B: Timer Counter 1 external clock input */
#define AT91_PC18_D18		(1 << 18)	/* A: Data Bus [18] */
#define AT91_PC18_TCLK2		(1 << 18)	/* B: Timer Counter 2 external clock input */
#define AT91_PC19_D19		(1 << 19)	/* A: Data Bus [19] */
#define AT91_PC19_TIOA0		(1 << 19)	/* B: Timer Counter 0 Multipurpose Timer I/O Pin A */
#define AT91_PC20_D20		(1 << 20)	/* A: Data Bus [20] */
#define AT91_PC20_TIOB0		(1 << 20)	/* B: Timer Counter 0 Multipurpose Timer I/O Pin B */
#define AT91_PC21_D21		(1 << 21)	/* A: Data Bus [21] */
#define AT91_PC21_TIOA1		(1 << 21)	/* B: Timer Counter 1 Multipurpose Timer I/O Pin A */
#define AT91_PC22_D22		(1 << 22)	/* A: Data Bus [22] */
#define AT91_PC22_TIOB1		(1 << 22)	/* B: Timer Counter 1 Multipurpose Timer I/O Pin B */
#define AT91_PC23_D23		(1 << 23)	/* A: Data Bus [23] */
#define AT91_PC23_TIOA2		(1 << 23)	/* B: Timer Counter 2 Multipurpose Timer I/O Pin A */
#define AT91_PC24_D24		(1 << 24)	/* A: Data Bus [24] */
#define AT91_PC24_TIOB2		(1 << 24)	/* B: Timer Counter 2 Multipurpose Timer I/O Pin B */
#define AT91_PC25_D25		(1 << 25)	/* A: Data Bus [25] */
#define AT91_PC25_TF2		(1 << 25)	/* B: SSC2 Transmit Frame Sync */
#define AT91_PC26_D26		(1 << 26)	/* A: Data Bus [26] */
#define AT91_PC26_TK2		(1 << 26)	/* B: SSC2 Transmit Clock */
#define AT91_PC27_D27		(1 << 27)	/* A: Data Bus [27] */
#define AT91_PC27_TD2		(1 << 27)	/* B: SSC2 Transmit Data */
#define AT91_PC28_D28		(1 << 28)	/* A: Data Bus [28] */
#define AT91_PC28_RD2		(1 << 28)	/* B: SSC2 Receive Data */
#define AT91_PC29_D29		(1 << 29)	/* A: Data Bus [29] */
#define AT91_PC29_RK2		(1 << 29)	/* B: SSC2 Receive Clock */
#define AT91_PC30_D30		(1 << 30)	/* A: Data Bus [30] */
#define AT91_PC30_RF2		(1 << 30)	/* B: SSC2 Receive Frame Sync */
#define AT91_PC31_D31		(1 << 31)	/* A: Data Bus [31] */
#define AT91_PC31_PCK1		(1 << 31)	/* B: PMC Programmable clock Output 1 */
#endif

#endif
