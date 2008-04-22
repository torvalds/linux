/*
 * include/asm-arm/arch-realview/board-pb11mp.h
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

#ifndef __ASM_ARCH_BOARD_PB11MP_H
#define __ASM_ARCH_BOARD_PB11MP_H

#include <asm/arch/platform.h>

/*
 * Peripheral addresses
 */
#define REALVIEW_PB11MP_UART0_BASE		0x10009000	/* UART 0 */
#define REALVIEW_PB11MP_UART1_BASE		0x1000A000	/* UART 1 */
#define REALVIEW_PB11MP_UART2_BASE		0x1000B000	/* UART 2 */
#define REALVIEW_PB11MP_UART3_BASE		0x1000C000	/* UART 3 */
#define REALVIEW_PB11MP_SSP_BASE		0x1000D000	/* Synchronous Serial Port */
#define REALVIEW_PB11MP_WATCHDOG0_BASE		0x1000F000	/* Watchdog 0 */
#define REALVIEW_PB11MP_WATCHDOG_BASE		0x10010000	/* watchdog interface */
#define REALVIEW_PB11MP_TIMER0_1_BASE		0x10011000	/* Timer 0 and 1 */
#define REALVIEW_PB11MP_TIMER2_3_BASE		0x10012000	/* Timer 2 and 3 */
#define REALVIEW_PB11MP_GPIO0_BASE		0x10013000	/* GPIO port 0 */
#define REALVIEW_PB11MP_RTC_BASE		0x10017000	/* Real Time Clock */
#define REALVIEW_PB11MP_TIMER4_5_BASE		0x10018000	/* Timer 4/5 */
#define REALVIEW_PB11MP_TIMER6_7_BASE		0x10019000	/* Timer 6/7 */
#define REALVIEW_PB11MP_SCTL_BASE		0x1001A000	/* System Controller */
#define REALVIEW_PB11MP_CLCD_BASE		0x10020000	/* CLCD */
#define REALVIEW_PB11MP_ONB_SRAM_BASE		0x10060000	/* On-board SRAM */
#define REALVIEW_PB11MP_DMC_BASE		0x100E0000	/* DMC configuration */
#define REALVIEW_PB11MP_SMC_BASE		0x100E1000	/* SMC configuration */
#define REALVIEW_PB11MP_CAN_BASE		0x100E2000	/* CAN bus */
#define REALVIEW_PB11MP_CF_BASE			0x18000000	/* Compact flash */
#define REALVIEW_PB11MP_CF_MEM_BASE		0x18003000	/* SMC for Compact flash */
#define REALVIEW_PB11MP_GIC_CPU_BASE		0x1E000000	/* Generic interrupt controller CPU interface */
#define REALVIEW_PB11MP_FLASH0_BASE		0x40000000
#define REALVIEW_PB11MP_FLASH0_SIZE		SZ_64M
#define REALVIEW_PB11MP_FLASH1_BASE		0x44000000
#define REALVIEW_PB11MP_FLASH1_SIZE		SZ_64M
#define REALVIEW_PB11MP_ETH_BASE		0x4E000000	/* Ethernet */
#define REALVIEW_PB11MP_USB_BASE		0x4F000000	/* USB */
#define REALVIEW_PB11MP_GIC_DIST_BASE		0x1E001000	/* Generic interrupt controller distributor */
#define REALVIEW_PB11MP_LT_BASE			0xC0000000	/* Logic Tile expansion */
#define REALVIEW_PB11MP_SDRAM6_BASE		0x70000000	/* SDRAM bank 6 256MB */
#define REALVIEW_PB11MP_SDRAM7_BASE		0x80000000	/* SDRAM bank 7 256MB */

#define REALVIEW_PB11MP_SYS_PLD_CTRL1		0x74

/*
 * PB11MPCore PCI regions
 */
#define REALVIEW_PB11MP_PCI_BASE		0x90040000	/* PCI-X Unit base */
#define REALVIEW_PB11MP_PCI_IO_BASE		0x90050000	/* IO Region on AHB */
#define REALVIEW_PB11MP_PCI_MEM_BASE		0xA0000000	/* MEM Region on AHB */

#define REALVIEW_PB11MP_PCI_BASE_SIZE		0x10000		/* 16 Kb */
#define REALVIEW_PB11MP_PCI_IO_SIZE		0x1000		/* 4 Kb */
#define REALVIEW_PB11MP_PCI_MEM_SIZE		0x20000000	/* 512 MB */

/*
 * Testchip peripheral and fpga gic regions
 */
#define REALVIEW_TC11MP_SCU_BASE		0x1F000000	/* IRQ, Test chip */
#define REALVIEW_TC11MP_GIC_CPU_BASE		0x1F000100	/* Test chip interrupt controller CPU interface */
#define REALVIEW_TC11MP_TWD_BASE		0x1F000700
#define REALVIEW_TC11MP_TWD_SIZE		0x00000100
#define REALVIEW_TC11MP_GIC_DIST_BASE		0x1F001000	/* Test chip interrupt controller distributor */
#define REALVIEW_TC11MP_L220_BASE		0x1F002000	/* L220 registers */

/*
 * Irqs
 */
#define IRQ_TC11MP_GIC_START			32
#define IRQ_PB11MP_GIC_START			64

/*
 * ARM11MPCore test chip interrupt sources (primary GIC on the test chip)
 */
#define IRQ_TC11MP_AACI		(IRQ_TC11MP_GIC_START + 0)
#define IRQ_TC11MP_TIMER0_1	(IRQ_TC11MP_GIC_START + 1)
#define IRQ_TC11MP_TIMER2_3	(IRQ_TC11MP_GIC_START + 2)
#define IRQ_TC11MP_USB		(IRQ_TC11MP_GIC_START + 3)
#define IRQ_TC11MP_UART0	(IRQ_TC11MP_GIC_START + 4)
#define IRQ_TC11MP_UART1	(IRQ_TC11MP_GIC_START + 5)
#define IRQ_TC11MP_RTC		(IRQ_TC11MP_GIC_START + 6)
#define IRQ_TC11MP_KMI0		(IRQ_TC11MP_GIC_START + 7)
#define IRQ_TC11MP_KMI1		(IRQ_TC11MP_GIC_START + 8)
#define IRQ_TC11MP_ETH		(IRQ_TC11MP_GIC_START + 9)
#define IRQ_TC11MP_PB_IRQ1	(IRQ_TC11MP_GIC_START + 10)		/* main GIC */
#define IRQ_TC11MP_PB_IRQ2	(IRQ_TC11MP_GIC_START + 11)		/* tile GIC */
#define IRQ_TC11MP_PB_FIQ1	(IRQ_TC11MP_GIC_START + 12)		/* main GIC */
#define IRQ_TC11MP_PB_FIQ2	(IRQ_TC11MP_GIC_START + 13)		/* tile GIC */
#define IRQ_TC11MP_MMCI0A	(IRQ_TC11MP_GIC_START + 14)
#define IRQ_TC11MP_MMCI0B	(IRQ_TC11MP_GIC_START + 15)

#define IRQ_TC11MP_PMU_CPU0	(IRQ_TC11MP_GIC_START + 17)
#define IRQ_TC11MP_PMU_CPU1	(IRQ_TC11MP_GIC_START + 18)
#define IRQ_TC11MP_PMU_CPU2	(IRQ_TC11MP_GIC_START + 19)
#define IRQ_TC11MP_PMU_CPU3	(IRQ_TC11MP_GIC_START + 20)
#define IRQ_TC11MP_PMU_SCU0	(IRQ_TC11MP_GIC_START + 21)
#define IRQ_TC11MP_PMU_SCU1	(IRQ_TC11MP_GIC_START + 22)
#define IRQ_TC11MP_PMU_SCU2	(IRQ_TC11MP_GIC_START + 23)
#define IRQ_TC11MP_PMU_SCU3	(IRQ_TC11MP_GIC_START + 24)
#define IRQ_TC11MP_PMU_SCU4	(IRQ_TC11MP_GIC_START + 25)
#define IRQ_TC11MP_PMU_SCU5	(IRQ_TC11MP_GIC_START + 26)
#define IRQ_TC11MP_PMU_SCU6	(IRQ_TC11MP_GIC_START + 27)
#define IRQ_TC11MP_PMU_SCU7	(IRQ_TC11MP_GIC_START + 28)

#define IRQ_TC11MP_L220_EVENT	(IRQ_TC11MP_GIC_START + 29)
#define IRQ_TC11MP_L220_SLAVE	(IRQ_TC11MP_GIC_START + 30)
#define IRQ_TC11MP_L220_DECODE	(IRQ_TC11MP_GIC_START + 31)

/*
 * RealView PB11MPCore GIC interrupt sources (secondary GIC on the board)
 */
#define IRQ_PB11MP_WATCHDOG	(IRQ_PB11MP_GIC_START + 0)	/* Watchdog timer */
#define IRQ_PB11MP_SOFT		(IRQ_PB11MP_GIC_START + 1)	/* Software interrupt */
#define IRQ_PB11MP_COMMRx	(IRQ_PB11MP_GIC_START + 2)	/* Debug Comm Rx interrupt */
#define IRQ_PB11MP_COMMTx	(IRQ_PB11MP_GIC_START + 3)	/* Debug Comm Tx interrupt */
#define IRQ_PB11MP_GPIO0	(IRQ_PB11MP_GIC_START + 6)	/* GPIO 0 */
#define IRQ_PB11MP_GPIO1	(IRQ_PB11MP_GIC_START + 7)	/* GPIO 1 */
#define IRQ_PB11MP_GPIO2	(IRQ_PB11MP_GIC_START + 8)	/* GPIO 2 */
								/* 9 reserved */
#define IRQ_PB11MP_RTC_GIC1	(IRQ_PB11MP_GIC_START + 10)	/* Real Time Clock */
#define IRQ_PB11MP_SSP		(IRQ_PB11MP_GIC_START + 11)	/* Synchronous Serial Port */
#define IRQ_PB11MP_UART0_GIC1	(IRQ_PB11MP_GIC_START + 12)	/* UART 0 on development chip */
#define IRQ_PB11MP_UART1_GIC1	(IRQ_PB11MP_GIC_START + 13)	/* UART 1 on development chip */
#define IRQ_PB11MP_UART2	(IRQ_PB11MP_GIC_START + 14)	/* UART 2 on development chip */
#define IRQ_PB11MP_UART3	(IRQ_PB11MP_GIC_START + 15)	/* UART 3 on development chip */
#define IRQ_PB11MP_SCI		(IRQ_PB11MP_GIC_START + 16)	/* Smart Card Interface */
#define IRQ_PB11MP_MMCI0A_GIC1	(IRQ_PB11MP_GIC_START + 17)	/* Multimedia Card 0A */
#define IRQ_PB11MP_MMCI0B_GIC1	(IRQ_PB11MP_GIC_START + 18)	/* Multimedia Card 0B */
#define IRQ_PB11MP_AACI_GIC1	(IRQ_PB11MP_GIC_START + 19)	/* Audio Codec */
#define IRQ_PB11MP_KMI0_GIC1	(IRQ_PB11MP_GIC_START + 20)	/* Keyboard/Mouse port 0 */
#define IRQ_PB11MP_KMI1_GIC1	(IRQ_PB11MP_GIC_START + 21)	/* Keyboard/Mouse port 1 */
#define IRQ_PB11MP_CHARLCD	(IRQ_PB11MP_GIC_START + 22)	/* Character LCD */
#define IRQ_PB11MP_CLCD		(IRQ_PB11MP_GIC_START + 23)	/* CLCD controller */
#define IRQ_PB11MP_DMAC		(IRQ_PB11MP_GIC_START + 24)	/* DMA controller */
#define IRQ_PB11MP_PWRFAIL	(IRQ_PB11MP_GIC_START + 25)	/* Power failure */
#define IRQ_PB11MP_PISMO	(IRQ_PB11MP_GIC_START + 26)	/* PISMO interface */
#define IRQ_PB11MP_DoC		(IRQ_PB11MP_GIC_START + 27)	/* Disk on Chip memory controller */
#define IRQ_PB11MP_ETH_GIC1	(IRQ_PB11MP_GIC_START + 28)	/* Ethernet controller */
#define IRQ_PB11MP_USB_GIC1	(IRQ_PB11MP_GIC_START + 29)	/* USB controller */
#define IRQ_PB11MP_TSPEN	(IRQ_PB11MP_GIC_START + 30)	/* Touchscreen pen */
#define IRQ_PB11MP_TSKPAD	(IRQ_PB11MP_GIC_START + 31)	/* Touchscreen keypad */

#define IRQ_PB11MP_SMC		-1
#define IRQ_PB11MP_SCTL		-1

#define NR_GIC_PB11MP		2

/*
 * Only define NR_IRQS if less than NR_IRQS_PB11MP
 */
#define NR_IRQS_PB11MP		(IRQ_TC11MP_GIC_START + 96)

#if defined(CONFIG_MACH_REALVIEW_PB11MP)

#if !defined(NR_IRQS) || (NR_IRQS < NR_IRQS_PB11MP)
#undef NR_IRQS
#define NR_IRQS			NR_IRQS_PB11MP
#endif

#if !defined(MAX_GIC_NR) || (MAX_GIC_NR < NR_GIC_PB11MP)
#undef MAX_GIC_NR
#define MAX_GIC_NR		NR_GIC_PB11MP
#endif

#endif	/* CONFIG_MACH_REALVIEW_PB11MP */

#endif	/* __ASM_ARCH_BOARD_PB11MP_H */
