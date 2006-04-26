/* include/asm-arm/arch-lh7a40x/irqs.h
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *  Copyright (C) 2004 Logic Product Development
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

/* It is to be seen whether or not we can build a kernel for more than
 * one board.  For the time being, these macros assume that we cannot.
 * Thus, it is OK to ifdef machine/board specific IRQ assignments.
 */


#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H


#define FIQ_START	80

#if defined (CONFIG_ARCH_LH7A400)

  /* FIQs */

# define IRQ_GPIO0FIQ	0	/* GPIO External FIQ Interrupt on F0 */
# define IRQ_BLINT	1	/* Battery Low */
# define IRQ_WEINT	2	/* Watchdog Timer, WDT overflow	*/
# define IRQ_MCINT	3	/* Media Change, MEDCHG pin rising */

  /* IRQs */

# define IRQ_CSINT	4	/* Audio Codec (ACI) */
# define IRQ_GPIO1INTR	5	/* GPIO External IRQ Interrupt on F1 */
# define IRQ_GPIO2INTR	6	/* GPIO External IRQ Interrupt on F2 */
# define IRQ_GPIO3INTR	7	/* GPIO External IRQ Interrupt on F3 */
# define IRQ_T1UI	8	/* Timer 1 underflow */
# define IRQ_T2UI	9	/* Timer 2 underflow */
# define IRQ_RTCMI	10
# define IRQ_TINTR	11	/* Clock State Controller 64 Hz tick (CSC) */
# define IRQ_UART1INTR	12
# define IRQ_UART2INTR	13
# define IRQ_LCDINTR	14
# define IRQ_SSIEOT	15	/* Synchronous Serial Interface (SSI) */
# define IRQ_UART3INTR	16
# define IRQ_SCIINTR	17	/* Smart Card Interface (SCI) */
# define IRQ_AACINTR	18	/* Advanced Audio Codec (AAC) */
# define IRQ_MMCINTR	19	/* Multimedia Card (MMC) */
# define IRQ_USBINTR	20
# define IRQ_DMAINTR	21
# define IRQ_T3UI	22	/* Timer 3 underflow */
# define IRQ_GPIO4INTR	23	/* GPIO External IRQ Interrupt on F4 */
# define IRQ_GPIO5INTR	24	/* GPIO External IRQ Interrupt on F5 */
# define IRQ_GPIO6INTR	25	/* GPIO External IRQ Interrupt on F6 */
# define IRQ_GPIO7INTR	26	/* GPIO External IRQ Interrupt on F7 */
# define IRQ_BMIINTR	27	/* Battery Monitor Interface (BMI) */

# define NR_IRQ_CPU	28	/* IRQs directly recognized by CPU */

	/* Given IRQ, return GPIO interrupt number 0-7 */
# define IRQ_TO_GPIO(i)  ((i) \
	- (((i) > IRQ_GPIO3INTR) ? IRQ_GPIO4INTR - IRQ_GPIO3INTR - 1 : 0)\
	- (((i) > IRQ_GPIO0INTR) ? IRQ_GPIO1INTR - IRQ_GPIO0INTR - 1 : 0))

#endif

#if defined (CONFIG_ARCH_LH7A404)

# define IRQ_BROWN	0	/* Brownout */
# define IRQ_WDTINTR	1	/* Watchdog Timer */
# define IRQ_COMMRX	2	/* ARM Comm Rx for Debug */
# define IRQ_COMMTX	3	/* ARM Comm Tx for Debug */
# define IRQ_T1UI	4	/* Timer 1 underflow */
# define IRQ_T2UI	5	/* Timer 2 underflow */
# define IRQ_CSINT	6	/* Codec Interrupt (shared by AAC on 404) */
# define IRQ_DMAM2P0	7	/* -- DMA Memory to Peripheral */
# define IRQ_DMAM2P1	8
# define IRQ_DMAM2P2	9
# define IRQ_DMAM2P3	10
# define IRQ_DMAM2P4	11
# define IRQ_DMAM2P5	12
# define IRQ_DMAM2P6	13
# define IRQ_DMAM2P7	14
# define IRQ_DMAM2P8	15
# define IRQ_DMAM2P9	16
# define IRQ_DMAM2M0	17	/* -- DMA Memory to Memory */
# define IRQ_DMAM2M1	18
# define IRQ_GPIO0INTR	19	/* -- GPIOF Interrupt */
# define IRQ_GPIO1INTR	20
# define IRQ_GPIO2INTR	21
# define IRQ_GPIO3INTR	22
# define IRQ_SOFT_V1_23	23	/* -- Unassigned */
# define IRQ_SOFT_V1_24	24
# define IRQ_SOFT_V1_25	25
# define IRQ_SOFT_V1_26	26
# define IRQ_SOFT_V1_27	27
# define IRQ_SOFT_V1_28	28
# define IRQ_SOFT_V1_29	29
# define IRQ_SOFT_V1_30	30
# define IRQ_SOFT_V1_31	31

# define IRQ_BLINT	32	/* Battery Low */
# define IRQ_BMIINTR	33	/* Battery Monitor */
# define IRQ_MCINTR	34	/* Media Change */
# define IRQ_TINTR	35	/* 64Hz Tick */
# define IRQ_WEINT	36	/* Watchdog Expired */
# define IRQ_RTCMI	37	/* Real-time Clock Match */
# define IRQ_UART1INTR	38	/* UART1 Interrupt (including error) */
# define IRQ_UART1ERR	39	/* UART1 Error */
# define IRQ_UART2INTR	40	/* UART2 Interrupt (including error) */
# define IRQ_UART2ERR	41	/* UART2 Error */
# define IRQ_UART3INTR	42	/* UART3 Interrupt (including error) */
# define IRQ_UART3ERR	43	/* UART3 Error */
# define IRQ_SCIINTR	44	/* Smart Card */
# define IRQ_TSCINTR	45	/* Touchscreen */
# define IRQ_KMIINTR	46	/* Keyboard/Mouse (PS/2) */
# define IRQ_GPIO4INTR	47	/* -- GPIOF Interrupt */
# define IRQ_GPIO5INTR	48
# define IRQ_GPIO6INTR	49
# define IRQ_GPIO7INTR	50
# define IRQ_T3UI	51	/* Timer 3 underflow */
# define IRQ_LCDINTR	52	/* LCD Controller */
# define IRQ_SSPINTR	53	/* Synchronous Serial Port */
# define IRQ_SDINTR	54	/* Secure Digital Port (MMC) */
# define IRQ_USBINTR	55	/* USB Device Port */
# define IRQ_USHINTR	56	/* USB Host Port */
# define IRQ_SOFT_V2_25	57	/* -- Unassigned */
# define IRQ_SOFT_V2_26	58
# define IRQ_SOFT_V2_27	59
# define IRQ_SOFT_V2_28	60
# define IRQ_SOFT_V2_29	61
# define IRQ_SOFT_V2_30	62
# define IRQ_SOFT_V2_31	63

# define NR_IRQ_CPU	64	/* IRQs directly recognized by CPU */

	/* Given IRQ, return GPIO interrupt number 0-7 */
# define IRQ_TO_GPIO(i)  ((i) \
	- (((i) > IRQ_GPIO3INTR) ? IRQ_GPIO4INTR - IRQ_GPIO3INTR - 1 : 0)\
	- IRQ_GPIO0INTR)

			/* Vector Address constants */
# define VA_VECTORED	0x100	/* Set for vectored interrupt */
# define VA_VIC1DEFAULT	0x200	/* Set as default VECTADDR for VIC1 */
# define VA_VIC2DEFAULT	0x400	/* Set as default VECTADDR for VIC2 */

#endif

  /* IRQ aliases */

#if !defined (IRQ_GPIO0INTR)
# define IRQ_GPIO0INTR	IRQ_GPIO0FIQ
#endif
#define IRQ_TICK 	IRQ_TINTR
#define IRQ_PCC1_RDY	IRQ_GPIO6INTR	/* PCCard 1 ready */
#define IRQ_PCC2_RDY	IRQ_GPIO7INTR	/* PCCard 2 ready */

#ifdef CONFIG_MACH_KEV7A400
# define IRQ_TS		IRQ_GPIOFIQ	/* Touchscreen */
# define IRQ_CPLD	IRQ_GPIO1INTR	/* CPLD cascade */
# define IRQ_PCC1_CD	IRQ_GPIO_F2	/* PCCard 1 card detect */
# define IRQ_PCC2_CD	IRQ_GPIO_F3	/* PCCard 2 card detect */
#endif

#if defined (CONFIG_MACH_LPD7A400) || defined (CONFIG_MACH_LPD7A404)
# define IRQ_CPLD_V28	IRQ_GPIO7INTR	/* CPLD cascade through GPIO_PF7 */
# define IRQ_CPLD_V34	IRQ_GPIO3INTR	/* CPLD cascade through GPIO_PF3 */
#endif

  /* System specific IRQs */

#define IRQ_BOARD_START NR_IRQ_CPU

#ifdef CONFIG_MACH_KEV7A400
# define IRQ_KEV7A400_CPLD	IRQ_BOARD_START
# define NR_IRQ_BOARD		5
# define IRQ_KEV7A400_MMC_CD	IRQ_KEV7A400_CPLD + 0	/* MMC Card Detect */
# define IRQ_KEV7A400_RI2	IRQ_KEV7A400_CPLD + 1	/* Ring Indicator 2 */
# define IRQ_KEV7A400_IDE_CF	IRQ_KEV7A400_CPLD + 2	/* Compact Flash (?) */
# define IRQ_KEV7A400_ETH_INT	IRQ_KEV7A400_CPLD + 3	/* Ethernet chip */
# define IRQ_KEV7A400_INT	IRQ_KEV7A400_CPLD + 4
#endif

#if defined (CONFIG_MACH_LPD7A400) || defined (CONFIG_MACH_LPD7A404)
# define IRQ_LPD7A40X_CPLD	IRQ_BOARD_START
# define NR_IRQ_BOARD		2
# define IRQ_LPD7A40X_ETH_INT	IRQ_LPD7A40X_CPLD + 0	/* Ethernet chip */
# define IRQ_LPD7A400_TS	IRQ_LPD7A40X_CPLD + 1	/* Touch screen */
#endif

#define NR_IRQS		(NR_IRQ_CPU + NR_IRQ_BOARD)

#endif
