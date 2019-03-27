/* $Id: ar5312reg.h,v 1.4 2011/07/07 05:06:44 matt Exp $ */
/*
 * Copyright (c) 2006 Urbana-Champaign Independent Media Center.
 * Copyright (c) 2006 Garrett D'Amore.
 * All rights reserved.
 *
 * This code was written by Garrett D'Amore for the Champaign-Urbana
 * Community Wireless Network Project.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgements:
 *      This product includes software developed by the Urbana-Champaign
 *      Independent Media Center.
 *	This product includes software developed by Garrett D'Amore.
 * 4. Urbana-Champaign Independent Media Center's name and Garrett
 *    D'Amore's name may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE URBANA-CHAMPAIGN INDEPENDENT
 * MEDIA CENTER AND GARRETT D'AMORE ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE URBANA-CHAMPAIGN INDEPENDENT
 * MEDIA CENTER OR GARRETT D'AMORE BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MIPS_ATHEROS_AR5312REG_H_
#define	_MIPS_ATHEROS_AR5312REG_H_

#define	AR5312_MEM0_BASE		0x00000000	/* sdram */
#define	AR5312_MEM1_BASE		0x08000000	/* sdram/flash */
#define	AR5312_MEM3_BASE		0x10000000	/* flash */
#define	AR5312_WLAN0_BASE		0x18000000
#define	AR5312_ENET0_BASE		0x18100000
#define	AR5312_ENET1_BASE		0x18200000
#define	AR5312_SDRAMCTL_BASE		0x18300000
#define	AR5312_FLASHCTL_BASE		0x18400000
#define	AR5312_WLAN1_BASE		0x18500000
#define	AR5312_UART0_BASE		0x1C000000	/* high speed */
#define	AR5312_UART1_BASE		0x1C001000
#define	AR5312_GPIO_BASE		0x1C002000
#define	AR5312_SYSREG_BASE		0x1C003000
#define	AR5312_UARTDMA_BASE		0x1C004000
#define	AR5312_FLASH_BASE		0x1E000000
#define	AR5312_FLASH_END		0x20000000	/* possibly aliased */

/*
 * FLASHCTL registers  -- offset relative to AR531X_FLASHCTL_BASE
 */
#define	AR5312_FLASHCTL_0		0x00
#define	AR5312_FLASHCTL_1		0x04
#define	AR5312_FLASHCTL_2		0x08

#define	AR5312_FLASHCTL_IDCY		__BITS(0,3)	/* idle cycle turn */
#define	AR5312_FLASHCTL_WST1		__BITS(5,9)	/* wait state 1 */
#define AR5312_FLASHCTL_RBLE		__BIT(10)	/* rd byte enable */
#define	AR5312_FLASHCTL_WST2		__BITS(11,15)	/* wait state 1 */
#define	AR5312_FLASHCTL_AC		__BITS(16,18)	/* addr chk */
#define	AR5312_FLASHCTL_AC_128K		0
#define	AR5312_FLASHCTL_AC_256K		1
#define	AR5312_FLASHCTL_AC_512K		2
#define	AR5312_FLASHCTL_AC_1M		3
#define	AR5312_FLASHCTL_AC_2M		4
#define	AR5312_FLASHCTL_AC_4M		5
#define	AR5312_FLASHCTL_AC_8M		6
#define	AR5312_FLASHCTL_AC_16M		7
#define AR5312_FLASHCTL_E		__BIT(19)	/* enable */
#define AR5312_FLASHCTL_BUSERR		__BIT(24)	/* buserr */
#define AR5312_FLASHCTL_WPERR		__BIT(25)	/* wperr */
#define AR5312_FLASHCTL_WP		__BIT(26)	/* wp */
#define AR5312_FLASHCTL_BM		__BIT(27)	/* bm */
#define AR5312_FLASHCTL_MW		__BITS(28,29)	/* mem width */
#define AR5312_FLASHCTL_AT		__BITS(31,30)	/* access type */

/*
 * GPIO registers  -- offset relative to AR531X_GPIO_BASE
 */
#define AR5312_GPIO_DO			0
#define AR5312_GPIO_DI			4
#define AR5312_GPIO_CR			8

#define AR5312_GPIO_PINS		8

/*
 * SYSREG registers  -- offset relative to AR531X_SYSREG_BASE
 */
#define	AR5312_SYSREG_TIMER		0x0000
#define	AR5312_SYSREG_TIMER_RELOAD	0x0004
#define	AR5312_SYSREG_WDOG_CTL		0x0008
#define	AR5312_SYSREG_WDOG_TIMER	0x000c
#define	AR5312_SYSREG_MISC_INTSTAT	0x0010
#define	AR5312_SYSREG_MISC_INTMASK	0x0014
#define	AR5312_SYSREG_INTSTAT		0x0018
#define	AR5312_SYSREG_RESETCTL		0x0020
#define	AR5312_SYSREG_CLOCKCTL		0x0064
#define	AR5312_SYSREG_SCRATCH		0x006c
#define	AR5312_SYSREG_AHBPERR		0x0070
#define	AR5312_SYSREG_PROC		0x0074
#define	AR5312_SYSREG_AHBDMAE		0x0078
#define	AR5312_SYSREG_ENABLE		0x0080
#define	AR5312_SYSREG_REVISION		0x0090

/* WDOG_CTL watchdog control bits */
#define	AR5312_WDOG_CTL_IGNORE		0x0000
#define	AR5312_WDOG_CTL_NMI		0x0001
#define	AR5312_WDOG_CTL_RESET		0x0002

/* Resets */
#define	AR5312_RESET_SYSTEM		__BIT(0)
#define	AR5312_RESET_CPU		__BIT(1)
#define	AR5312_RESET_WLAN0		__BIT(2)	/* mac & bb */
#define	AR5312_RESET_PHY0		__BIT(3)	/* enet phy */
#define	AR5312_RESET_PHY1		__BIT(4)	/* enet phy */
#define	AR5312_RESET_ENET0		__BIT(5)	/* mac */
#define	AR5312_RESET_ENET1		__BIT(6)	/* mac */
#define	AR5312_RESET_UART0		__BIT(8)	/* mac */
#define	AR5312_RESET_WLAN1		__BIT(9)	/* mac & bb */
#define	AR5312_RESET_APB		__BIT(10)	/* bridge */
#define	AR5312_RESET_WARM_CPU		__BIT(16)
#define	AR5312_RESET_WARM_WLAN0_MAC	__BIT(17)
#define	AR5312_RESET_WARM_WLAN0_BB	__BIT(18)
#define	AR5312_RESET_NMI		__BIT(20)
#define	AR5312_RESET_WARM_WLAN1_MAC	__BIT(21)
#define	AR5312_RESET_WARM_WLAN1_BB	__BIT(22)
#define	AR5312_RESET_LOCAL_BUS		__BIT(23)
#define	AR5312_RESET_WDOG		__BIT(24)

/* AR5312/2312 clockctl bits */
#define	AR5312_CLOCKCTL_PREDIVIDE	__BITS(4,5)
#define	AR5312_CLOCKCTL_MULTIPLIER	__BITS(8,12)
#define	AR5312_CLOCKCTL_DOUBLER		__BIT(16)

/* AR2313 clockctl */
#define	AR2313_CLOCKCTL_PREDIVIDE	__BITS(12,13)
#define	AR2313_CLOCKCTL_MULTIPLIER	__BITS(16,20)

/* Enables */
#define	AR5312_ENABLE_WLAN0		__BIT(0)
#define	AR5312_ENABLE_ENET0		__BIT(1)
#define	AR5312_ENABLE_ENET1		__BIT(2)
#define	AR5312_ENABLE_WLAN1		__BITS(7,8)	/* both DMA and PIO */

/* Revision ids */
#define	AR5312_REVISION_WMAC_MAJOR(x)	(((x) >> 12) & 0xf)
#define	AR5312_REVISION_WMAC_MINOR(x)	(((x) >> 8) & 0xf)
#define	AR5312_REVISION_WMAC(x)		(((x) >> 8) & 0xff)
#define	AR5312_REVISION_MAJOR(x)	(((x) >> 4) & 0xf)
#define	AR5312_REVISION_MINOR(x)	(((x) >> 0) & 0xf)

#define	AR5312_REVISION_MAJ_AR5311	0x1
#define	AR5312_REVISION_MAJ_AR5312	0x4
#define	AR5312_REVISION_MAJ_AR2313	0x5
#define	AR5312_REVISION_MAJ_AR5315	0xB

/*
 * SDRAMCTL registers  -- offset relative to SDRAMCTL
 */
#define	AR5312_SDRAMCTL_MEM_CFG0	0x0000
#define	AR5312_SDRAMCTL_MEM_CFG1	0x0004

/* memory config 1 bits */
#define	AR5312_MEM_CFG1_BANK0		__BITS(8,10)
#define	AR5312_MEM_CFG1_BANK1		__BITS(12,15)

/* helper macro for accessing system registers without bus space */
#define	REGVAL(x)	*((volatile uint32_t *)(MIPS_PHYS_TO_KSEG1((x))))
#define	GETSYSREG(x)	REGVAL((x) + AR5312_SYSREG_BASE)
#define	PUTSYSREG(x,v)	(REGVAL((x) + AR5312_SYSREG_BASE)) = (v)
#define	GETSDRAMREG(x)	REGVAL((x) + AR5312_SDRAMCTL_BASE)
#define	PUTSDRAMREG(x,v)	(REGVAL((x) + AR5312_SDRAMCTL_BASE)) = (v)

/*
 * Interrupts.
 */
#define	AR5312_IRQ_WLAN0		0
#define	AR5312_IRQ_ENET0		1
#define	AR5312_IRQ_ENET1		2
#define	AR5312_IRQ_WLAN1		3
#define	AR5312_IRQ_MISC			4

#define	AR5312_MISC_IRQ_TIMER		1
#define	AR5312_MISC_IRQ_AHBPERR		2
#define	AR5312_MISC_IRQ_AHBDMAE		3
#define	AR5312_MISC_IRQ_GPIO		4
#define	AR5312_MISC_IRQ_UART0		5
#define	AR5312_MISC_IRQ_UART0_DMA	6
#define	AR5312_MISC_IRQ_WDOG		7

/*
 * Board data.  This is located in flash somewhere, ar531x_board_info
 * locates it.
 */
#include <dev/ath/ath_hal/ah_soc.h>	/* XXX really doesn't belong in hal */

/* XXX write-around for now */
#define	AR5312_BOARD_MAGIC		AR531X_BD_MAGIC

/* config bits */
#define	AR5312_BOARD_CONFIG_ENET0	BD_ENET0
#define	AR5312_BOARD_CONFIG_ENET1	BD_ENET1
#define	AR5312_BOARD_CONFIG_UART1	BD_UART1
#define	AR5312_BOARD_CONFIG_UART0	BD_UART0
#define	AR5312_BOARD_CONFIG_RSTFACTORY	BD_RSTFACTORY
#define	AR5312_BOARD_CONFIG_SYSLED	BD_SYSLED
#define	AR5312_BOARD_CONFIG_EXTUARTCLK	BD_EXTUARTCLK
#define	AR5312_BOARD_CONFIG_CPUFREQ	BD_CPUFREQ
#define	AR5312_BOARD_CONFIG_SYSFREQ	BD_SYSFREQ
#define	AR5312_BOARD_CONFIG_WLAN0	BD_WLAN0
#define	AR5312_BOARD_CONFIG_MEMCAP	BD_MEMCAP
#define	AR5312_BOARD_CONFIG_DISWDOG	BD_DISWATCHDOG
#define	AR5312_BOARD_CONFIG_WLAN1	BD_WLAN1
#define	AR5312_BOARD_CONFIG_AR2312	BD_ISCASPER
#define	AR5312_BOARD_CONFIG_WLAN0_2G	BD_WLAN0_2G_EN
#define	AR5312_BOARD_CONFIG_WLAN0_5G	BD_WLAN0_5G_EN
#define	AR5312_BOARD_CONFIG_WLAN1_2G	BD_WLAN1_2G_EN
#define	AR5312_BOARD_CONFIG_WLAN1_5G	BD_WLAN1_5G_EN

#define AR5312_APB_BASE         AR5312_UART0_BASE
#define AR5312_APB_SIZE         0x02000000

#endif	/* _MIPS_ATHEROS_AR531XREG_H_ */
