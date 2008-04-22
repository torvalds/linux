/*
 * include/asm-arm/arch-realview/board-pb1176.h
 *
 * Copyright (C) 2008 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef __ASM_ARCH_BOARD_PB1176_H
#define __ASM_ARCH_BOARD_PB1176_H

#include <asm/arch/platform.h>

/*
 * Peripheral addresses
 */
#define REALVIEW_PB1176_SCTL_BASE		0x10100000 /* System controller */
#define REALVIEW_PB1176_SMC_BASE		0x10111000 /* SMC */
#define REALVIEW_PB1176_DMC_BASE		0x10109000 /* DMC configuration */
#define REALVIEW_PB1176_SDRAM67_BASE		0x70000000 /* SDRAM banks 6 and 7 */
#define REALVIEW_PB1176_FLASH_BASE		0x30000000
#define REALVIEW_PB1176_FLASH_SIZE		SZ_64M

#define REALVIEW_PB1176_TIMER0_1_BASE		0x10104000 /* Timer 0 and 1 */
#define REALVIEW_PB1176_TIMER2_3_BASE		0x10105000 /* Timer 2 and 3 */
#define REALVIEW_PB1176_TIMER4_5_BASE		0x10106000 /* Timer 4 and 5 */
#define REALVIEW_PB1176_WATCHDOG_BASE		0x10107000 /* watchdog interface */
#define REALVIEW_PB1176_RTC_BASE		0x10108000 /* Real Time Clock */
#define REALVIEW_PB1176_GPIO0_BASE		0x1010A000 /* GPIO port 0 */
#define REALVIEW_PB1176_SSP_BASE		0x1010B000 /* Synchronous Serial Port */
#define REALVIEW_PB1176_UART0_BASE		0x1010C000 /* UART 0 */
#define REALVIEW_PB1176_UART1_BASE		0x1010D000 /* UART 1 */
#define REALVIEW_PB1176_UART2_BASE		0x1010E000 /* UART 2 */
#define REALVIEW_PB1176_UART3_BASE		0x1010F000 /* UART 3 */
#define REALVIEW_PB1176_CLCD_BASE		0x10112000 /* CLCD */
#define REALVIEW_PB1176_ETH_BASE		0x3A000000 /* Ethernet */
#define REALVIEW_PB1176_USB_BASE		0x3B000000 /* USB */

/*
 * PCI regions
 */
#define REALVIEW_PB1176_PCI_BASE		0x60000000 /* PCI self config */
#define REALVIEW_PB1176_PCI_CFG_BASE		0x61000000 /* PCI config */
#define REALVIEW_PB1176_PCI_IO_BASE0		0x62000000 /* PCI IO region */
#define REALVIEW_PB1176_PCI_MEM_BASE0		0x63000000 /* Memory region 1 */
#define REALVIEW_PB1176_PCI_MEM_BASE1		0x64000000 /* Memory region 2 */
#define REALVIEW_PB1176_PCI_MEM_BASE2		0x68000000 /* Memory region 3 */

#define REALVIEW_PB1176_PCI_BASE_SIZE		0x01000000 /* 16MB */
#define REALVIEW_PB1176_PCI_CFG_BASE_SIZE	0x01000000 /* 16MB */
#define REALVIEW_PB1176_PCI_IO_BASE0_SIZE	0x01000000 /* 16MB */
#define REALVIEW_PB1176_PCI_MEM_BASE0_SIZE	0x01000000 /* 16MB */
#define REALVIEW_PB1176_PCI_MEM_BASE1_SIZE	0x04000000 /* 64MB */
#define REALVIEW_PB1176_PCI_MEM_BASE2_SIZE	0x08000000 /* 128MB */

#define REALVIEW_DC1176_GIC_CPU_BASE		0x10120000 /* GIC CPU interface, on devchip */
#define REALVIEW_DC1176_GIC_DIST_BASE		0x10121000 /* GIC distributor, on devchip */
#define REALVIEW_PB1176_GIC_CPU_BASE		0x10040000 /* GIC CPU interface, on FPGA */
#define REALVIEW_PB1176_GIC_DIST_BASE		0x10041000 /* GIC distributor, on FPGA */
#define REALVIEW_PB1176_L220_BASE		0x10110000 /* L220 registers */

/*
 * Irqs
 */
#define IRQ_DC1176_GIC_START			32
#define IRQ_PB1176_GIC_START			64

/*
 * ARM1176 DevChip interrupt sources (primary GIC)
 */
#define IRQ_DC1176_WATCHDOG	(IRQ_DC1176_GIC_START + 0)	/* Watchdog timer */
#define IRQ_DC1176_SOFTINT	(IRQ_DC1176_GIC_START + 1)	/* Software interrupt */
#define IRQ_DC1176_COMMRx	(IRQ_DC1176_GIC_START + 2)	/* Debug Comm Rx interrupt */
#define IRQ_DC1176_COMMTx	(IRQ_DC1176_GIC_START + 3)	/* Debug Comm Tx interrupt */
#define IRQ_DC1176_TIMER0	(IRQ_DC1176_GIC_START + 8)	/* Timer 0 */
#define IRQ_DC1176_TIMER1	(IRQ_DC1176_GIC_START + 9)	/* Timer 1 */
#define IRQ_DC1176_TIMER2	(IRQ_DC1176_GIC_START + 10)	/* Timer 2 */
#define IRQ_DC1176_APC		(IRQ_DC1176_GIC_START + 11)
#define IRQ_DC1176_IEC		(IRQ_DC1176_GIC_START + 12)
#define IRQ_DC1176_L2CC		(IRQ_DC1176_GIC_START + 13)
#define IRQ_DC1176_RTC		(IRQ_DC1176_GIC_START + 14)
#define IRQ_DC1176_CLCD		(IRQ_DC1176_GIC_START + 15)	/* CLCD controller */
#define IRQ_DC1176_UART0	(IRQ_DC1176_GIC_START + 18)	/* UART 0 on development chip */
#define IRQ_DC1176_UART1	(IRQ_DC1176_GIC_START + 19)	/* UART 1 on development chip */
#define IRQ_DC1176_UART2	(IRQ_DC1176_GIC_START + 20)	/* UART 2 on development chip */
#define IRQ_DC1176_UART3	(IRQ_DC1176_GIC_START + 21)	/* UART 3 on development chip */

#define IRQ_DC1176_PB_IRQ2	(IRQ_DC1176_GIC_START + 30)	/* tile GIC */
#define IRQ_DC1176_PB_IRQ1	(IRQ_DC1176_GIC_START + 31)	/* main GIC */

/*
 * RealView PB1176 interrupt sources (secondary GIC)
 */
#define IRQ_PB1176_MMCI0A	(IRQ_PB1176_GIC_START + 1)	/* Multimedia Card 0A */
#define IRQ_PB1176_MMCI0B	(IRQ_PB1176_GIC_START + 2)	/* Multimedia Card 0A */
#define IRQ_PB1176_KMI0		(IRQ_PB1176_GIC_START + 3)	/* Keyboard/Mouse port 0 */
#define IRQ_PB1176_KMI1		(IRQ_PB1176_GIC_START + 4)	/* Keyboard/Mouse port 1 */
#define IRQ_PB1176_SCI		(IRQ_PB1176_GIC_START + 5)
#define IRQ_PB1176_UART4	(IRQ_PB1176_GIC_START + 6)	/* UART 4 on baseboard */
#define IRQ_PB1176_CHARLCD	(IRQ_PB1176_GIC_START + 7)	/* Character LCD */
#define IRQ_PB1176_GPIO1	(IRQ_PB1176_GIC_START + 8)
#define IRQ_PB1176_GPIO2	(IRQ_PB1176_GIC_START + 9)
#define IRQ_PB1176_ETH		(IRQ_PB1176_GIC_START + 10)	/* Ethernet controller */
#define IRQ_PB1176_USB		(IRQ_PB1176_GIC_START + 11)	/* USB controller */

#define IRQ_PB1176_PISMO	(IRQ_PB1176_GIC_START + 16)

#define IRQ_PB1176_AACI		(IRQ_PB1176_GIC_START + 19)	/* Audio Codec */

#define IRQ_PB1176_TIMER0_1	(IRQ_PB1176_GIC_START + 22)
#define IRQ_PB1176_TIMER2_3	(IRQ_PB1176_GIC_START + 23)
#define IRQ_PB1176_DMAC		(IRQ_PB1176_GIC_START + 24)	/* DMA controller */
#define IRQ_PB1176_RTC		(IRQ_PB1176_GIC_START + 25)	/* Real Time Clock */

#define IRQ_PB1176_GPIO0	-1
#define IRQ_PB1176_SSP		-1
#define IRQ_PB1176_SCTL		-1

#define NR_GIC_PB1176		2

/*
 * Only define NR_IRQS if less than NR_IRQS_PB1176
 */
#define NR_IRQS_PB1176		(IRQ_DC1176_GIC_START + 96)

#if defined(CONFIG_MACH_REALVIEW_PB1176)

#if !defined(NR_IRQS) || (NR_IRQS < NR_IRQS_PB1176)
#undef NR_IRQS
#define NR_IRQS			NR_IRQS_PB1176
#endif

#if !defined(MAX_GIC_NR) || (MAX_GIC_NR < NR_GIC_PB1176)
#undef MAX_GIC_NR
#define MAX_GIC_NR		NR_GIC_PB1176
#endif

#endif	/* CONFIG_MACH_REALVIEW_PB1176 */

#endif	/* __ASM_ARCH_BOARD_PB1176_H */
