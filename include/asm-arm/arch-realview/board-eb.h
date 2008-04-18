/*
 * include/asm-arm/arch-realview/board-eb.h
 *
 * Copyright (C) 2007 ARM Limited
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

#ifndef __ASM_ARCH_BOARD_EB_H
#define __ASM_ARCH_BOARD_EB_H

#include <asm/arch/platform.h>

/*
 * RealView EB + ARM11MPCore peripheral addresses
 */
#define REALVIEW_EB_UART0_BASE		0x10009000	/* UART 0 */
#define REALVIEW_EB_UART1_BASE		0x1000A000	/* UART 1 */
#define REALVIEW_EB_UART2_BASE		0x1000B000	/* UART 2 */
#define REALVIEW_EB_UART3_BASE		0x1000C000	/* UART 3 */
#define REALVIEW_EB_SSP_BASE		0x1000D000	/* Synchronous Serial Port */
#define REALVIEW_EB_WATCHDOG_BASE	0x10010000	/* watchdog interface */
#define REALVIEW_EB_TIMER0_1_BASE	0x10011000	/* Timer 0 and 1 */
#define REALVIEW_EB_TIMER2_3_BASE	0x10012000	/* Timer 2 and 3 */
#define REALVIEW_EB_GPIO0_BASE		0x10013000	/* GPIO port 0 */
#define REALVIEW_EB_RTC_BASE		0x10017000	/* Real Time Clock */
#define REALVIEW_EB_CLCD_BASE		0x10020000	/* CLCD */
#define REALVIEW_EB_GIC_CPU_BASE	0x10040000	/* Generic interrupt controller CPU interface */
#define REALVIEW_EB_GIC_DIST_BASE	0x10041000	/* Generic interrupt controller distributor */
#define REALVIEW_EB_SMC_BASE		0x10080000	/* Static memory controller */

#define REALVIEW_EB_FLASH_BASE		0x40000000
#define REALVIEW_EB_FLASH_SIZE		SZ_64M
#define REALVIEW_EB_ETH_BASE		0x4E000000	/* Ethernet */
#define REALVIEW_EB_USB_BASE		0x4F000000	/* USB */

#ifdef CONFIG_REALVIEW_EB_ARM11MP_REVB
#define REALVIEW_EB11MP_SCU_BASE	0x10100000	/* SCU registers */
#define REALVIEW_EB11MP_GIC_CPU_BASE	0x10100100	/* Generic interrupt controller CPU interface */
#define REALVIEW_EB11MP_TWD_BASE	0x10100700
#define REALVIEW_EB11MP_TWD_SIZE	0x00000100
#define REALVIEW_EB11MP_GIC_DIST_BASE	0x10101000	/* Generic interrupt controller distributor */
#define REALVIEW_EB11MP_L220_BASE	0x10102000	/* L220 registers */
#define REALVIEW_EB11MP_SYS_PLD_CTRL1	0xD8		/* Register offset for MPCore sysctl */
#else
#define REALVIEW_EB11MP_SCU_BASE	0x1F000000	/* SCU registers */
#define REALVIEW_EB11MP_GIC_CPU_BASE	0x1F000100	/* Generic interrupt controller CPU interface */
#define REALVIEW_EB11MP_TWD_BASE	0x1F000700
#define REALVIEW_EB11MP_TWD_SIZE	0x00000100
#define REALVIEW_EB11MP_GIC_DIST_BASE	0x1F001000	/* Generic interrupt controller distributor */
#define REALVIEW_EB11MP_L220_BASE	0x1F002000	/* L220 registers */
#define REALVIEW_EB11MP_SYS_PLD_CTRL1	0x74		/* Register offset for MPCore sysctl */
#endif

#define IRQ_EB_GIC_START	32

/*
 * RealView EB interrupt sources
 */
#define IRQ_EB_WDOG		(IRQ_EB_GIC_START + 0)		/* Watchdog timer */
#define IRQ_EB_SOFT		(IRQ_EB_GIC_START + 1)		/* Software interrupt */
#define IRQ_EB_COMMRx		(IRQ_EB_GIC_START + 2)		/* Debug Comm Rx interrupt */
#define IRQ_EB_COMMTx		(IRQ_EB_GIC_START + 3)		/* Debug Comm Tx interrupt */
#define IRQ_EB_TIMER0_1		(IRQ_EB_GIC_START + 4)		/* Timer 0 and 1 */
#define IRQ_EB_TIMER2_3		(IRQ_EB_GIC_START + 5)		/* Timer 2 and 3 */
#define IRQ_EB_GPIO0		(IRQ_EB_GIC_START + 6)		/* GPIO 0 */
#define IRQ_EB_GPIO1		(IRQ_EB_GIC_START + 7)		/* GPIO 1 */
#define IRQ_EB_GPIO2		(IRQ_EB_GIC_START + 8)		/* GPIO 2 */
								/* 9 reserved */
#define IRQ_EB_RTC		(IRQ_EB_GIC_START + 10)		/* Real Time Clock */
#define IRQ_EB_SSP		(IRQ_EB_GIC_START + 11)		/* Synchronous Serial Port */
#define IRQ_EB_UART0		(IRQ_EB_GIC_START + 12)		/* UART 0 on development chip */
#define IRQ_EB_UART1		(IRQ_EB_GIC_START + 13)		/* UART 1 on development chip */
#define IRQ_EB_UART2		(IRQ_EB_GIC_START + 14)		/* UART 2 on development chip */
#define IRQ_EB_UART3		(IRQ_EB_GIC_START + 15)		/* UART 3 on development chip */
#define IRQ_EB_SCI		(IRQ_EB_GIC_START + 16)		/* Smart Card Interface */
#define IRQ_EB_MMCI0A		(IRQ_EB_GIC_START + 17)		/* Multimedia Card 0A */
#define IRQ_EB_MMCI0B		(IRQ_EB_GIC_START + 18)		/* Multimedia Card 0B */
#define IRQ_EB_AACI		(IRQ_EB_GIC_START + 19)		/* Audio Codec */
#define IRQ_EB_KMI0		(IRQ_EB_GIC_START + 20)		/* Keyboard/Mouse port 0 */
#define IRQ_EB_KMI1		(IRQ_EB_GIC_START + 21)		/* Keyboard/Mouse port 1 */
#define IRQ_EB_CHARLCD		(IRQ_EB_GIC_START + 22)		/* Character LCD */
#define IRQ_EB_CLCD		(IRQ_EB_GIC_START + 23)		/* CLCD controller */
#define IRQ_EB_DMA		(IRQ_EB_GIC_START + 24)		/* DMA controller */
#define IRQ_EB_PWRFAIL		(IRQ_EB_GIC_START + 25)		/* Power failure */
#define IRQ_EB_PISMO		(IRQ_EB_GIC_START + 26)		/* PISMO interface */
#define IRQ_EB_DoC		(IRQ_EB_GIC_START + 27)		/* Disk on Chip memory controller */
#define IRQ_EB_ETH		(IRQ_EB_GIC_START + 28)		/* Ethernet controller */
#define IRQ_EB_USB		(IRQ_EB_GIC_START + 29)		/* USB controller */
#define IRQ_EB_TSPEN		(IRQ_EB_GIC_START + 30)		/* Touchscreen pen */
#define IRQ_EB_TSKPAD		(IRQ_EB_GIC_START + 31)		/* Touchscreen keypad */

/*
 * RealView EB + ARM11MPCore interrupt sources (primary GIC on the core tile)
 */
#define IRQ_EB11MP_AACI		(IRQ_EB_GIC_START + 0)
#define IRQ_EB11MP_TIMER0_1	(IRQ_EB_GIC_START + 1)
#define IRQ_EB11MP_TIMER2_3	(IRQ_EB_GIC_START + 2)
#define IRQ_EB11MP_USB		(IRQ_EB_GIC_START + 3)
#define IRQ_EB11MP_UART0	(IRQ_EB_GIC_START + 4)
#define IRQ_EB11MP_UART1	(IRQ_EB_GIC_START + 5)
#define IRQ_EB11MP_RTC		(IRQ_EB_GIC_START + 6)
#define IRQ_EB11MP_KMI0		(IRQ_EB_GIC_START + 7)
#define IRQ_EB11MP_KMI1		(IRQ_EB_GIC_START + 8)
#define IRQ_EB11MP_ETH		(IRQ_EB_GIC_START + 9)
#define IRQ_EB11MP_EB_IRQ1	(IRQ_EB_GIC_START + 10)		/* main GIC */
#define IRQ_EB11MP_EB_IRQ2	(IRQ_EB_GIC_START + 11)		/* tile GIC */
#define IRQ_EB11MP_EB_FIQ1	(IRQ_EB_GIC_START + 12)		/* main GIC */
#define IRQ_EB11MP_EB_FIQ2	(IRQ_EB_GIC_START + 13)		/* tile GIC */
#define IRQ_EB11MP_MMCI0A	(IRQ_EB_GIC_START + 14)
#define IRQ_EB11MP_MMCI0B	(IRQ_EB_GIC_START + 15)

#define IRQ_EB11MP_PMU_CPU0	(IRQ_EB_GIC_START + 17)
#define IRQ_EB11MP_PMU_CPU1	(IRQ_EB_GIC_START + 18)
#define IRQ_EB11MP_PMU_CPU2	(IRQ_EB_GIC_START + 19)
#define IRQ_EB11MP_PMU_CPU3	(IRQ_EB_GIC_START + 20)
#define IRQ_EB11MP_PMU_SCU0	(IRQ_EB_GIC_START + 21)
#define IRQ_EB11MP_PMU_SCU1	(IRQ_EB_GIC_START + 22)
#define IRQ_EB11MP_PMU_SCU2	(IRQ_EB_GIC_START + 23)
#define IRQ_EB11MP_PMU_SCU3	(IRQ_EB_GIC_START + 24)
#define IRQ_EB11MP_PMU_SCU4	(IRQ_EB_GIC_START + 25)
#define IRQ_EB11MP_PMU_SCU5	(IRQ_EB_GIC_START + 26)
#define IRQ_EB11MP_PMU_SCU6	(IRQ_EB_GIC_START + 27)
#define IRQ_EB11MP_PMU_SCU7	(IRQ_EB_GIC_START + 28)

#define IRQ_EB11MP_L220_EVENT	(IRQ_EB_GIC_START + 29)
#define IRQ_EB11MP_L220_SLAVE	(IRQ_EB_GIC_START + 30)
#define IRQ_EB11MP_L220_DECODE	(IRQ_EB_GIC_START + 31)

#define IRQ_EB11MP_UART2	-1
#define IRQ_EB11MP_UART3	-1
#define IRQ_EB11MP_CLCD		-1
#define IRQ_EB11MP_DMA		-1
#define IRQ_EB11MP_WDOG		-1
#define IRQ_EB11MP_GPIO0	-1
#define IRQ_EB11MP_GPIO1	-1
#define IRQ_EB11MP_GPIO2	-1
#define IRQ_EB11MP_SCI		-1
#define IRQ_EB11MP_SSP		-1

#define NR_GIC_EB11MP		2

/*
 * Only define NR_IRQS if less than NR_IRQS_EB
 */
#define NR_IRQS_EB		(IRQ_EB_GIC_START + 96)

#if defined(CONFIG_MACH_REALVIEW_EB) \
	&& (!defined(NR_IRQS) || (NR_IRQS < NR_IRQS_EB))
#undef NR_IRQS
#define NR_IRQS			NR_IRQS_EB
#endif

#if defined(CONFIG_REALVIEW_EB_ARM11MP) \
	&& (!defined(MAX_GIC_NR) || (MAX_GIC_NR < NR_GIC_EB11MP))
#undef MAX_GIC_NR
#define MAX_GIC_NR		NR_GIC_EB11MP
#endif

/*
 * Core tile identification (REALVIEW_SYS_PROCID)
 */
#define REALVIEW_EB_PROC_MASK		0xFF000000
#define REALVIEW_EB_PROC_ARM7TDMI	0x00000000
#define REALVIEW_EB_PROC_ARM9		0x02000000
#define REALVIEW_EB_PROC_ARM11		0x04000000
#define REALVIEW_EB_PROC_ARM11MP	0x06000000

#define check_eb_proc(proc_type)						\
	((readl(__io_address(REALVIEW_SYS_PROCID)) & REALVIEW_EB_PROC_MASK)	\
	 == proc_type)

#ifdef CONFIG_REALVIEW_EB_ARM11MP
#define core_tile_eb11mp()	check_eb_proc(REALVIEW_EB_PROC_ARM11MP)
#else
#define core_tile_eb11mp()	0
#endif

#endif	/* __ASM_ARCH_BOARD_EB_H */
