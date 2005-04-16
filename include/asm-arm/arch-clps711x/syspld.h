/*
 *  linux/include/asm-arm/arch-clps711x/syspld.h
 *
 *  System Control PLD register definitions.
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARCH_SYSPLD_H
#define __ASM_ARCH_SYSPLD_H

#define SYSPLD_PHYS_BASE	(0x10000000)

#ifndef __ASSEMBLY__
#include <asm/types.h>

#define SYSPLD_REG(type,off)	(*(volatile type *)(SYSPLD_BASE + off))
#else
#define SYSPLD_REG(type,off)	(off)
#endif

#define PLD_INT		SYSPLD_REG(u32, 0x000000)
#define PLD_INT_PENIRQ		(1 << 5)
#define PLD_INT_UCB_IRQ		(1 << 1)
#define PLD_INT_KBD_ATN		(1 << 0)	/* EINT1 */

#define PLD_PWR		SYSPLD_REG(u32, 0x000004)
#define PLD_PWR_EXT		(1 << 5)
#define PLD_PWR_MODE		(1 << 4)	/* 1 = PWM, 0 = PFM */
#define PLD_S4_ON		(1 << 3)	/* LCD bias voltage enable */
#define PLD_S3_ON		(1 << 2)	/* LCD backlight enable */
#define PLD_S2_ON		(1 << 1)	/* LCD 3V3 supply enable */
#define PLD_S1_ON		(1 << 0)	/* LCD 3V supply enable */

#define PLD_KBD		SYSPLD_REG(u32, 0x000008)
#define PLD_KBD_WAKE		(1 << 1)
#define PLD_KBD_EN		(1 << 0)

#define PLD_SPI		SYSPLD_REG(u32, 0x00000c)
#define PLD_SPI_EN		(1 << 0)

#define PLD_IO		SYSPLD_REG(u32, 0x000010)
#define PLD_IO_BOOTSEL		(1 << 6)	/* boot sel switch */
#define PLD_IO_USER		(1 << 5)	/* user defined switch */
#define PLD_IO_LED3		(1 << 4)
#define PLD_IO_LED2		(1 << 3)
#define PLD_IO_LED1		(1 << 2)
#define PLD_IO_LED0		(1 << 1)
#define PLD_IO_LEDEN		(1 << 0)

#define PLD_IRDA	SYSPLD_REG(u32, 0x000014)
#define PLD_IRDA_EN		(1 << 0)

#define PLD_COM2	SYSPLD_REG(u32, 0x000018)
#define PLD_COM2_EN		(1 << 0)

#define PLD_COM1	SYSPLD_REG(u32, 0x00001c)
#define PLD_COM1_EN		(1 << 0)

#define PLD_AUD		SYSPLD_REG(u32, 0x000020)
#define PLD_AUD_DIV1		(1 << 6)
#define PLD_AUD_DIV0		(1 << 5)
#define PLD_AUD_CLK_SEL1	(1 << 4)
#define PLD_AUD_CLK_SEL0	(1 << 3)
#define PLD_AUD_MIC_PWR		(1 << 2)
#define PLD_AUD_MIC_GAIN	(1 << 1)
#define PLD_AUD_CODEC_EN	(1 << 0)

#define PLD_CF		SYSPLD_REG(u32, 0x000024)
#define PLD_CF2_SLEEP		(1 << 5)
#define PLD_CF1_SLEEP		(1 << 4)
#define PLD_CF2_nPDREQ		(1 << 3)
#define PLD_CF1_nPDREQ		(1 << 2)
#define PLD_CF2_nIRQ		(1 << 1)
#define PLD_CF1_nIRQ		(1 << 0)

#define PLD_SDC		SYSPLD_REG(u32, 0x000028)
#define PLD_SDC_INT_EN		(1 << 2)
#define PLD_SDC_WP		(1 << 1)
#define PLD_SDC_CD		(1 << 0)

#define PLD_FPGA	SYSPLD_REG(u32, 0x00002c)

#define PLD_CODEC	SYSPLD_REG(u32, 0x400000)
#define PLD_CODEC_IRQ3		(1 << 4)
#define PLD_CODEC_IRQ2		(1 << 3)
#define PLD_CODEC_IRQ1		(1 << 2)
#define PLD_CODEC_EN		(1 << 0)

#define PLD_BRITE	SYSPLD_REG(u32, 0x400004)
#define PLD_BRITE_UP		(1 << 1)
#define PLD_BRITE_DN		(1 << 0)

#define PLD_LCDEN	SYSPLD_REG(u32, 0x400008)
#define PLD_LCDEN_EN		(1 << 0)

#define PLD_ID		SYSPLD_REG(u32, 0x40000c)

#define PLD_TCH		SYSPLD_REG(u32, 0x400010)
#define PLD_TCH_PENIRQ		(1 << 1)
#define PLD_TCH_EN		(1 << 0)

#define PLD_GPIO	SYSPLD_REG(u32, 0x400014)
#define PLD_GPIO2		(1 << 2)
#define PLD_GPIO1		(1 << 1)
#define PLD_GPIO0		(1 << 0)

#endif
