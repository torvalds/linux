/*
 * include/asm-arm/arch-at91rm9200/at91rm9200.h
 *
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * Common definitions.
 * Based on AT91RM9200 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91RM9200_H
#define AT91RM9200_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91_ID_FIQ		0	/* Advanced Interrupt Controller (FIQ) */
#define AT91_ID_SYS		1	/* System Peripheral */
#define AT91RM9200_ID_PIOA	2	/* Parallel IO Controller A */
#define AT91RM9200_ID_PIOB	3	/* Parallel IO Controller B */
#define AT91RM9200_ID_PIOC	4	/* Parallel IO Controller C */
#define AT91RM9200_ID_PIOD	5	/* Parallel IO Controller D */
#define AT91RM9200_ID_US0	6	/* USART 0 */
#define AT91RM9200_ID_US1	7	/* USART 1 */
#define AT91RM9200_ID_US2	8	/* USART 2 */
#define AT91RM9200_ID_US3	9	/* USART 3 */
#define AT91RM9200_ID_MCI	10	/* Multimedia Card Interface */
#define AT91RM9200_ID_UDP	11	/* USB Device Port */
#define AT91RM9200_ID_TWI	12	/* Two-Wire Interface */
#define AT91RM9200_ID_SPI	13	/* Serial Peripheral Interface */
#define AT91RM9200_ID_SSC0	14	/* Serial Synchronous Controller 0 */
#define AT91RM9200_ID_SSC1	15	/* Serial Synchronous Controller 1 */
#define AT91RM9200_ID_SSC2	16	/* Serial Synchronous Controller 2 */
#define AT91RM9200_ID_TC0	17	/* Timer Counter 0 */
#define AT91RM9200_ID_TC1	18	/* Timer Counter 1 */
#define AT91RM9200_ID_TC2	19	/* Timer Counter 2 */
#define AT91RM9200_ID_TC3	20	/* Timer Counter 3 */
#define AT91RM9200_ID_TC4	21	/* Timer Counter 4 */
#define AT91RM9200_ID_TC5	22	/* Timer Counter 5 */
#define AT91RM9200_ID_UHP	23	/* USB Host port */
#define AT91RM9200_ID_EMAC	24	/* Ethernet MAC */
#define AT91RM9200_ID_IRQ0	25	/* Advanced Interrupt Controller (IRQ0) */
#define AT91RM9200_ID_IRQ1	26	/* Advanced Interrupt Controller (IRQ1) */
#define AT91RM9200_ID_IRQ2	27	/* Advanced Interrupt Controller (IRQ2) */
#define AT91RM9200_ID_IRQ3	28	/* Advanced Interrupt Controller (IRQ3) */
#define AT91RM9200_ID_IRQ4	29	/* Advanced Interrupt Controller (IRQ4) */
#define AT91RM9200_ID_IRQ5	30	/* Advanced Interrupt Controller (IRQ5) */
#define AT91RM9200_ID_IRQ6	31	/* Advanced Interrupt Controller (IRQ6) */


/*
 * Peripheral physical base addresses.
 */
#define AT91RM9200_BASE_TCB0	0xfffa0000
#define AT91RM9200_BASE_TC0	0xfffa0000
#define AT91RM9200_BASE_TC1	0xfffa0040
#define AT91RM9200_BASE_TC2	0xfffa0080
#define AT91RM9200_BASE_TCB1	0xfffa4000
#define AT91RM9200_BASE_TC3	0xfffa4000
#define AT91RM9200_BASE_TC4	0xfffa4040
#define AT91RM9200_BASE_TC5	0xfffa4080
#define AT91RM9200_BASE_UDP	0xfffb0000
#define AT91RM9200_BASE_MCI	0xfffb4000
#define AT91RM9200_BASE_TWI	0xfffb8000
#define AT91RM9200_BASE_EMAC	0xfffbc000
#define AT91RM9200_BASE_US0	0xfffc0000
#define AT91RM9200_BASE_US1	0xfffc4000
#define AT91RM9200_BASE_US2	0xfffc8000
#define AT91RM9200_BASE_US3	0xfffcc000
#define AT91RM9200_BASE_SSC0	0xfffd0000
#define AT91RM9200_BASE_SSC1	0xfffd4000
#define AT91RM9200_BASE_SSC2	0xfffd8000
#define AT91RM9200_BASE_SPI	0xfffe0000
#define AT91_BASE_SYS		0xfffff000


/*
 * System Peripherals (offset from AT91_BASE_SYS)
 */
#define AT91_AIC	(0xfffff000 - AT91_BASE_SYS)	/* Advanced Interrupt Controller */
#define AT91_DBGU	(0xfffff200 - AT91_BASE_SYS)	/* Debug Unit */
#define AT91_PIOA	(0xfffff400 - AT91_BASE_SYS)	/* PIO Controller A */
#define AT91_PIOB	(0xfffff600 - AT91_BASE_SYS)	/* PIO Controller B */
#define AT91_PIOC	(0xfffff800 - AT91_BASE_SYS)	/* PIO Controller C */
#define AT91_PIOD	(0xfffffa00 - AT91_BASE_SYS)	/* PIO Controller D */
#define AT91_PMC	(0xfffffc00 - AT91_BASE_SYS)	/* Power Management Controller */
#define AT91_ST		(0xfffffd00 - AT91_BASE_SYS)	/* System Timer */
#define AT91_RTC	(0xfffffe00 - AT91_BASE_SYS)	/* Real-Time Clock */
#define AT91_MC		(0xffffff00 - AT91_BASE_SYS)	/* Memory Controllers */

#define AT91_MATRIX	0	/* not supported */

/*
 * Internal Memory.
 */
#define AT91RM9200_ROM_BASE	0x00100000	/* Internal ROM base address */
#define AT91RM9200_ROM_SIZE	SZ_128K		/* Internal ROM size (128Kb) */

#define AT91RM9200_SRAM_BASE	0x00200000	/* Internal SRAM base address */
#define AT91RM9200_SRAM_SIZE	SZ_16K		/* Internal SRAM size (16Kb) */

#define AT91RM9200_UHP_BASE	0x00300000	/* USB Host controller */


#if 0
/*
 * PIO pin definitions (peripheral A/B multiplexing).
 */
#define AT91_PA0_MISO		(1 <<  0)	/* A: SPI Master-In Slave-Out */
#define AT91_PA0_PCK3		(1 <<  0)	/* B: PMC Programmable Clock Output 3 */
#define AT91_PA1_MOSI		(1 <<  1)	/* A: SPI Master-Out Slave-In */
#define AT91_PA1_PCK0		(1 <<  1)	/* B: PMC Programmable Clock Output 0 */
#define AT91_PA2_SPCK		(1 <<  2)	/* A: SPI Serial Clock */
#define AT91_PA2_IRQ4		(1 <<  2)	/* B: External Interrupt 4 */
#define AT91_PA3_NPCS0		(1 <<  3)	/* A: SPI Peripheral Chip Select 0 */
#define AT91_PA3_IRQ5		(1 <<  3)	/* B: External Interrupt 5 */
#define AT91_PA4_NPCS1		(1 <<  4)	/* A: SPI Peripheral Chip Select 1 */
#define AT91_PA4_PCK1		(1 <<  4)	/* B: PMC Programmable Clock Output 1 */
#define AT91_PA5_NPCS2		(1 <<  5)	/* A: SPI Peripheral Chip Select 2 */
#define AT91_PA5_TXD3		(1 <<  5)	/* B: USART Transmit Data 3 */
#define AT91_PA6_NPCS3		(1 <<  6)	/* A: SPI Peripheral Chip Select 3 */
#define AT91_PA6_RXD3		(1 <<  6)	/* B: USART Receive Data 3 */
#define AT91_PA7_ETXCK_EREFCK	(1 <<  7)	/* A: Ethernet Reference Clock / Transmit Clock */
#define AT91_PA7_PCK2		(1 <<  7)	/* B: PMC Programmable Clock Output 2 */
#define AT91_PA8_ETXEN		(1 <<  8)	/* A: Ethernet Transmit Enable */
#define AT91_PA8_MCCDB		(1 <<  8)	/* B: MMC Multimedia Card B Command */
#define AT91_PA9_ETX0		(1 <<  9)	/* A: Ethernet Transmit Data 0 */
#define AT91_PA9_MCDB0		(1 <<  9)	/* B: MMC Multimedia Card B Data 0 */
#define AT91_PA10_ETX1		(1 << 10)	/* A: Ethernet Transmit Data 1 */
#define AT91_PA10_MCDB1		(1 << 10)	/* B: MMC Multimedia Card B Data 1 */
#define AT91_PA11_ECRS_ECRSDV	(1 << 11)	/* A: Ethernet Carrier Sense / Data Valid */
#define AT91_PA11_MCDB2		(1 << 11)	/* B: MMC Multimedia Card B Data 2 */
#define AT91_PA12_ERX0		(1 << 12)	/* A: Ethernet Receive Data 0 */
#define AT91_PA12_MCDB3		(1 << 12)	/* B: MMC Multimedia Card B Data 3 */
#define AT91_PA13_ERX1		(1 << 13)	/* A: Ethernet Receive Data 1 */
#define AT91_PA13_TCLK0		(1 << 13)	/* B: TC External Clock Input 0 */
#define AT91_PA14_ERXER		(1 << 14)	/* A: Ethernet Receive Error */
#define AT91_PA14_TCLK1		(1 << 14)	/* B: TC External Clock Input 1 */
#define AT91_PA15_EMDC		(1 << 15)	/* A: Ethernet Management Data Clock */
#define AT91_PA15_TCLK2		(1 << 15)	/* B: TC External Clock Input 2 */
#define AT91_PA16_EMDIO		(1 << 16)	/* A: Ethernet Management Data I/O */
#define AT91_PA16_IRQ6		(1 << 16)	/* B: External Interrupt 6 */
#define AT91_PA17_TXD0		(1 << 17)	/* A: USART Transmit Data 0 */
#define AT91_PA17_TIOA0		(1 << 17)	/* B: TC I/O Line A 0 */
#define AT91_PA18_RXD0		(1 << 18)	/* A: USART Receive Data 0 */
#define AT91_PA18_TIOB0		(1 << 18)	/* B: TC I/O Line B 0 */
#define AT91_PA19_SCK0		(1 << 19)	/* A: USART Serial Clock 0 */
#define AT91_PA19_TIOA1		(1 << 19)	/* B: TC I/O Line A 1 */
#define AT91_PA20_CTS0		(1 << 20)	/* A: USART Clear To Send 0 */
#define AT91_PA20_TIOB1		(1 << 20)	/* B: TC I/O Line B 1 */
#define AT91_PA21_RTS0		(1 << 21)	/* A: USART Ready To Send 0 */
#define AT91_PA21_TIOA2		(1 << 21)	/* B: TC I/O Line A 2 */
#define AT91_PA22_RXD2		(1 << 22)	/* A: USART Receive Data 2 */
#define AT91_PA22_TIOB2		(1 << 22)	/* B: TC I/O Line B 2 */
#define AT91_PA23_TXD2		(1 << 23)	/* A: USART Transmit Data 2 */
#define AT91_PA23_IRQ3		(1 << 23)	/* B: External Interrupt 3 */
#define AT91_PA24_SCK2		(1 << 24)	/* A: USART Serial Clock 2 */
#define AT91_PA24_PCK1		(1 << 24)	/* B: PMC Programmable Clock Output 1 */
#define AT91_PA25_TWD		(1 << 25)	/* A: TWI Two-wire Serial Data */
#define AT91_PA25_IRQ2		(1 << 25)	/* B: External Interrupt 2 */
#define AT91_PA26_TWCK		(1 << 26)	/* A: TWI Two-wire Serial Clock */
#define AT91_PA26_IRQ1		(1 << 26)	/* B: External Interrupt 1 */
#define AT91_PA27_MCCK		(1 << 27)	/* A: MMC Multimedia Card Clock */
#define AT91_PA27_TCLK3		(1 << 27)	/* B: TC External Clock Input 3 */
#define AT91_PA28_MCCDA		(1 << 28)	/* A: MMC Multimedia Card A Command */
#define AT91_PA28_TCLK4		(1 << 28)	/* B: TC External Clock Input 4 */
#define AT91_PA29_MCDA0		(1 << 29)	/* A: MMC Multimedia Card A Data 0 */
#define AT91_PA29_TCLK5		(1 << 29)	/* B: TC External Clock Input 5 */
#define AT91_PA30_DRXD		(1 << 30)	/* A: DBGU Receive Data */
#define AT91_PA30_CTS2		(1 << 30)	/* B: USART Clear To Send 2 */
#define AT91_PA31_DTXD		(1 << 31)	/* A: DBGU Transmit Data */
#define AT91_PA31_RTS2		(1 << 31)	/* B: USART Ready To Send 2 */

#define AT91_PB0_TF0		(1 <<  0)	/* A: SSC Transmit Frame Sync 0 */
#define AT91_PB0_RTS3		(1 <<  0)	/* B: USART Ready To Send 3 */
#define AT91_PB1_TK0		(1 <<  1)	/* A: SSC Transmit Clock 0 */
#define AT91_PB1_CTS3		(1 <<  1)	/* B: USART Clear To Send 3 */
#define AT91_PB2_TD0		(1 <<  2)	/* A: SSC Transmit Data 0 */
#define AT91_PB2_SCK3		(1 <<  2)	/* B: USART Serial Clock 3 */
#define AT91_PB3_RD0		(1 <<  3)	/* A: SSC Receive Data 0 */
#define AT91_PB3_MCDA1		(1 <<  3)	/* B: MMC Multimedia Card A Data 1 */
#define AT91_PB4_RK0		(1 <<  4)	/* A: SSC Receive Clock 0 */
#define AT91_PB4_MCDA2		(1 <<  4)	/* B: MMC Multimedia Card A Data 2 */
#define AT91_PB5_RF0		(1 <<  5)	/* A: SSC Receive Frame Sync 0 */
#define AT91_PB5_MCDA3		(1 <<  5)	/* B: MMC Multimedia Card A Data 3 */
#define AT91_PB6_TF1		(1 <<  6)	/* A: SSC Transmit Frame Sync 1 */
#define AT91_PB6_TIOA3		(1 <<  6)	/* B: TC I/O Line A 3 */
#define AT91_PB7_TK1		(1 <<  7)	/* A: SSC Transmit Clock 1 */
#define AT91_PB7_TIOB3		(1 <<  7)	/* B: TC I/O Line B 3 */
#define AT91_PB8_TD1		(1 <<  8)	/* A: SSC Transmit Data 1 */
#define AT91_PB8_TIOA4		(1 <<  8)	/* B: TC I/O Line A 4 */
#define AT91_PB9_RD1		(1 <<  9)	/* A: SSC Receive Data 1 */
#define AT91_PB9_TIOB4		(1 <<  9)	/* B: TC I/O Line B 4 */
#define AT91_PB10_RK1		(1 << 10)	/* A: SSC Receive Clock 1 */
#define AT91_PB10_TIOA5		(1 << 10)	/* B: TC I/O Line A 5 */
#define AT91_PB11_RF1		(1 << 11)	/* A: SSC Receive Frame Sync 1 */
#define AT91_PB11_TIOB5		(1 << 11)	/* B: TC I/O Line B 5 */
#define AT91_PB12_TF2		(1 << 12)	/* A: SSC Transmit Frame Sync 2 */
#define AT91_PB12_ETX2		(1 << 12)	/* B: Ethernet Transmit Data 2 */
#define AT91_PB13_TK2		(1 << 13)	/* A: SSC Transmit Clock 3 */
#define AT91_PB13_ETX3		(1 << 13)	/* B: Ethernet Transmit Data 3 */
#define AT91_PB14_TD2		(1 << 14)	/* A: SSC Transmit Data 2 */
#define AT91_PB14_ETXER		(1 << 14)	/* B: Ethernet Transmit Coding Error */
#define AT91_PB15_RD2		(1 << 15)	/* A: SSC Receive Data 2 */
#define AT91_PB15_ERX2		(1 << 15)	/* B: Ethernet Receive Data 2 */
#define AT91_PB16_RK2		(1 << 16)	/* A: SSC Receive Clock 2 */
#define AT91_PB16_ERX3		(1 << 16)	/* B: Ethernet Receive Data 3 */
#define AT91_PB17_RF2		(1 << 17)	/* A: SSC Receive Frame Sync 2 */
#define AT91_PB17_ERXDV		(1 << 17)	/* B: Ethernet Receive Data Valid */
#define AT91_PB18_RI1		(1 << 18)	/* A: USART Ring Indicator 1 */
#define AT91_PB18_ECOL		(1 << 18)	/* B: Ethernet Collision Detected */
#define AT91_PB19_DTR1		(1 << 19)	/* A: USART Data Terminal Ready 1 */
#define AT91_PB19_ERXCK		(1 << 19)	/* B: Ethernet Receive Clock */
#define AT91_PB20_TXD1		(1 << 20)	/* A: USART Transmit Data 1 */
#define AT91_PB21_RXD1		(1 << 21)	/* A: USART Receive Data 1 */
#define AT91_PB22_SCK1		(1 << 22)	/* A: USART Serial Clock 1 */
#define AT91_PB23_DCD1		(1 << 23)	/* A: USART Data Carrier Detect 1 */
#define AT91_PB24_CTS1		(1 << 24)	/* A: USART Clear To Send 1 */
#define AT91_PB25_DSR1		(1 << 25)	/* A: USART Data Set Ready 1 */
#define AT91_PB25_EF100		(1 << 25)	/* B: Ethernet Force 100 Mbit */
#define AT91_PB26_RTS1		(1 << 26)	/* A: USART Ready To Send 1 */
#define AT91_PB27_PCK0		(1 << 27)	/* B: PMC Programmable Clock Output 0 */
#define AT91_PB28_FIQ		(1 << 28)	/* A: Fast Interrupt */
#define AT91_PB29_IRQ0		(1 << 29)	/* A: External Interrupt 0 */

#define AT91_PC0_BFCK		(1 <<  0)	/* A: Burst Flash Clock */
#define AT91_PC1_BFRDY_SMOE	(1 <<  1)	/* A: Burst Flash Ready / SmartMedia Output Enable */
#define AT91_PC2_BFAVD		(1 <<  2)	/* A: Burst Flash Address Valid */
#define AT91_PC3_BFBAA_SMWE	(1 <<  3)	/* A: Burst Flash Address Advance / SmartMedia Write Enable */
#define AT91_PC4_BFOE		(1 <<  4)	/* A: Burst Flash Output Enable */
#define AT91_PC5_BFWE		(1 <<  5)	/* A: Burst Flash Write Enable */
#define AT91_PC6_NWAIT		(1 <<  6)	/* A: SMC Wait Signal */
#define AT91_PC7_A23		(1 <<  7)	/* A: Address Bus 23 */
#define AT91_PC8_A24		(1 <<  8)	/* A: Address Bus 24 */
#define AT91_PC9_A25_CFRNW	(1 <<  9)	/* A: Address Bus 25 / Compact Flash Read Not Write */
#define AT91_PC10_NCS4_CFCS	(1 << 10)	/* A: SMC Chip Select 4 / Compact Flash Chip Select */
#define AT91_PC11_NCS5_CFCE1	(1 << 11)	/* A: SMC Chip Select 5 / Compact Flash Chip Enable 1 */
#define AT91_PC12_NCS6_CFCE2	(1 << 12)	/* A: SMC Chip Select 6 / Compact Flash Chip Enable 2 */
#define AT91_PC13_NCS7		(1 << 13)	/* A: Chip Select 7 */

#define AT91_PD0_ETX0		(1 <<  0)	/* A: Ethernet Transmit Data 0 */
#define AT91_PD1_ETX1		(1 <<  1)	/* A: Ethernet Transmit Data 1 */
#define AT91_PD2_ETX2		(1 <<  2)	/* A: Ethernet Transmit Data 2 */
#define AT91_PD3_ETX3		(1 <<  3)	/* A: Ethernet Transmit Data 3 */
#define AT91_PD4_ETXEN		(1 <<  4)	/* A: Ethernet Transmit Enable */
#define AT91_PD5_ETXER		(1 <<  5)	/* A: Ethernet Transmit Coding Error */
#define AT91_PD6_DTXD		(1 <<  6)	/* A: DBGU Transmit Data */
#define AT91_PD7_PCK0		(1 <<  7)	/* A: PMC Programmable Clock Output 0 */
#define AT91_PD7_TSYNC		(1 <<  7)	/* B: ETM Trace Synchronization Signal */
#define AT91_PD8_PCK1		(1 <<  8)	/* A: PMC Programmable Clock Output 1 */
#define AT91_PD8_TCLK		(1 <<  8)	/* B: ETM Trace Clock */
#define AT91_PD9_PCK2		(1 <<  9)	/* A: PMC Programmable Clock Output 2 */
#define AT91_PD9_TPS0		(1 <<  9)	/* B: ETM Trace ARM Pipeline Status 0 */
#define AT91_PD10_PCK3		(1 << 10)	/* A: PMC Programmable Clock Output 3 */
#define AT91_PD10_TPS1		(1 << 10)	/* B: ETM Trace ARM Pipeline Status 1 */
#define AT91_PD11_TPS2		(1 << 11)	/* B: ETM Trace ARM Pipeline Status 2 */
#define AT91_PD12_TPK0		(1 << 12)	/* B: ETM Trace Packet Port 0 */
#define AT91_PD13_TPK1		(1 << 13)	/* B: ETM Trace Packet Port 1 */
#define AT91_PD14_TPK2		(1 << 14)	/* B: ETM Trace Packet Port 2 */
#define AT91_PD15_TD0		(1 << 15)	/* A: SSC Transmit Data 0 */
#define AT91_PD15_TPK3		(1 << 15)	/* B: ETM Trace Packet Port 3 */
#define AT91_PD16_TD1		(1 << 16)	/* A: SSC Transmit Data 1 */
#define AT91_PD16_TPK4		(1 << 16)	/* B: ETM Trace Packet Port 4 */
#define AT91_PD17_TD2		(1 << 17)	/* A: SSC Transmit Data 2 */
#define AT91_PD17_TPK5		(1 << 17)	/* B: ETM Trace Packet Port 5 */
#define AT91_PD18_NPCS1		(1 << 18)	/* A: SPI Peripheral Chip Select 1 */
#define AT91_PD18_TPK6		(1 << 18)	/* B: ETM Trace Packet Port 6 */
#define AT91_PD19_NPCS2		(1 << 19)	/* A: SPI Peripheral Chip Select 2 */
#define AT91_PD19_TPK7		(1 << 19)	/* B: ETM Trace Packet Port 7 */
#define AT91_PD20_NPCS3		(1 << 20)	/* A: SPI Peripheral Chip Select 3 */
#define AT91_PD20_TPK8		(1 << 20)	/* B: ETM Trace Packet Port 8 */
#define AT91_PD21_RTS0		(1 << 21)  	/* A: USART Ready To Send 0 */
#define AT91_PD21_TPK9		(1 << 21)	/* B: ETM Trace Packet Port 9 */
#define AT91_PD22_RTS1		(1 << 22)	/* A: USART Ready To Send 1 */
#define AT91_PD22_TPK10		(1 << 22)	/* B: ETM Trace Packet Port 10 */
#define AT91_PD23_RTS2		(1 << 23)	/* A: USART Ready To Send 2 */
#define AT91_PD23_TPK11		(1 << 23)	/* B: ETM Trace Packet Port 11 */
#define AT91_PD24_RTS3		(1 << 24)	/* A: USART Ready To Send 3 */
#define AT91_PD24_TPK12		(1 << 24)	/* B: ETM Trace Packet Port 12 */
#define AT91_PD25_DTR1		(1 << 25)	/* A: USART Data Terminal Ready 1 */
#define AT91_PD25_TPK13		(1 << 25)	/* B: ETM Trace Packet Port 13 */
#define AT91_PD26_TPK14		(1 << 26)	/* B: ETM Trace Packet Port 14 */
#define AT91_PD27_TPK15		(1 << 27)	/* B: ETM Trace Packet Port 15 */
#endif

#endif
