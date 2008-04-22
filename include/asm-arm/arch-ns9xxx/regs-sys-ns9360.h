/*
 * include/asm-arm/arch-ns9xxx/regs-sys-ns9360.h
 *
 * Copyright (C) 2006,2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_REGSSYSNS9360_H
#define __ASM_ARCH_REGSSYSNS9360_H

#include <asm/hardware.h>

/* System Control Module */

/* AHB Arbiter Gen Configuration */
#define SYS_AHBAGENCONF	__REG(0xa0900000)

/* BRC */
#define SYS_BRC(x)	__REG2(0xa0900004, (x))

/* Timer x Reload Count register */
#define SYS_TRC(x)	__REG2(0xa0900044, (x))

/* Timer x Read register */
#define SYS_TR(x)	__REG2(0xa0900084, (x))

/* Timer Interrupt Status register */
#define SYS_TIS		__REG(0xa0900170)

/* PLL Configuration register */
#define SYS_PLL		__REG(0xa0900188)

/* PLL FS status */
#define SYS_PLL_FS		__REGBITS(24, 23)

/* PLL ND status */
#define SYS_PLL_ND		__REGBITS(20, 16)

/* PLL Configuration register: PLL SW change */
#define SYS_PLL_SWC		__REGBIT(15)
#define SYS_PLL_SWC_NO			__REGVAL(SYS_PLL_SWC, 0)
#define SYS_PLL_SWC_YES			__REGVAL(SYS_PLL_SWC, 1)

/* Timer x Control register */
#define SYS_TC(x)	__REG2(0xa0900190, (x))

/* Timer x Control register: Timer enable */
#define SYS_TCx_TEN		__REGBIT(15)
#define SYS_TCx_TEN_DIS			__REGVAL(SYS_TCx_TEN, 0)
#define SYS_TCx_TEN_EN			__REGVAL(SYS_TCx_TEN, 1)

/* Timer x Control register: CPU debug mode */
#define SYS_TCx_TDBG		__REGBIT(10)
#define SYS_TCx_TDBG_CONT		__REGVAL(SYS_TCx_TDBG, 0)
#define SYS_TCx_TDBG_STOP		__REGVAL(SYS_TCx_TDBG, 1)

/* Timer x Control register: Interrupt clear */
#define SYS_TCx_INTC		__REGBIT(9)
#define SYS_TCx_INTC_UNSET		__REGVAL(SYS_TCx_INTC, 0)
#define SYS_TCx_INTC_SET		__REGVAL(SYS_TCx_INTC, 1)

/* Timer x Control register: Timer clock select */
#define SYS_TCx_TLCS		__REGBITS(8, 6)
#define SYS_TCx_TLCS_CPU		__REGVAL(SYS_TCx_TLCS, 0)	/* CPU clock */
#define SYS_TCx_TLCS_DIV2		__REGVAL(SYS_TCx_TLCS, 1)	/* CPU clock / 2 */
#define SYS_TCx_TLCS_DIV4		__REGVAL(SYS_TCx_TLCS, 2)	/* CPU clock / 4 */
#define SYS_TCx_TLCS_DIV8		__REGVAL(SYS_TCx_TLCS, 3)	/* CPU clock / 8 */
#define SYS_TCx_TLCS_DIV16		__REGVAL(SYS_TCx_TLCS, 4)	/* CPU clock / 16 */
#define SYS_TCx_TLCS_DIV32		__REGVAL(SYS_TCx_TLCS, 5)	/* CPU clock / 32 */
#define SYS_TCx_TLCS_DIV64		__REGVAL(SYS_TCx_TLCS, 6)	/* CPU clock / 64 */
#define SYS_TCx_TLCS_EXT		__REGVAL(SYS_TCx_TLCS, 7)

/* Timer x Control register: Timer mode */
#define SYS_TCx_TM		__REGBITS(5, 4)
#define SYS_TCx_TM_IEE			__REGVAL(SYS_TCx_TM, 0)		/* Internal timer or external event */
#define SYS_TCx_TM_ELL			__REGVAL(SYS_TCx_TM, 1)		/* External low-level, gated timer */
#define SYS_TCx_TM_EHL			__REGVAL(SYS_TCx_TM, 2)		/* External high-level, gated timer */
#define SYS_TCx_TM_CONCAT		__REGVAL(SYS_TCx_TM, 3)		/* Concatenate the lower timer. */

/* Timer x Control register: Interrupt select */
#define SYS_TCx_INTS		__REGBIT(3)
#define SYS_TCx_INTS_DIS		__REGVAL(SYS_TCx_INTS, 0)
#define SYS_TCx_INTS_EN			__REGVAL(SYS_TCx_INTS, 1)

/* Timer x Control register: Up/down select */
#define SYS_TCx_UDS		__REGBIT(2)
#define SYS_TCx_UDS_UP			__REGVAL(SYS_TCx_UDS, 0)
#define SYS_TCx_UDS_DOWN		__REGVAL(SYS_TCx_UDS, 1)

/* Timer x Control register: 32- or 16-bit timer */
#define SYS_TCx_TSZ		__REGBIT(1)
#define SYS_TCx_TSZ_16			__REGVAL(SYS_TCx_TSZ, 0)
#define SYS_TCx_TSZ_32			__REGVAL(SYS_TCx_TSZ, 1)

/* Timer x Control register: Reload enable */
#define SYS_TCx_REN		__REGBIT(0)
#define SYS_TCx_REN_DIS			__REGVAL(SYS_TCx_REN, 0)
#define SYS_TCx_REN_EN			__REGVAL(SYS_TCx_REN, 1)

/* System Memory Chip Select x Dynamic Memory Base */
#define SYS_SMCSDMB(x)	__REG2(0xa09001d0, (x) << 1)

/* System Memory Chip Select x Dynamic Memory Mask */
#define SYS_SMCSDMM(x)	__REG2(0xa09001d4, (x) << 1)

/* System Memory Chip Select x Static Memory Base */
#define SYS_SMCSSMB(x)	__REG2(0xa09001f0, (x) << 1)

/* System Memory Chip Select x Static Memory Base: Chip select x base */
#define SYS_SMCSSMB_CSxB	__REGBITS(31, 12)

/* System Memory Chip Select x Static Memory Mask */
#define SYS_SMCSSMM(x)	__REG2(0xa09001f4, (x) << 1)

/* System Memory Chip Select x Static Memory Mask: Chip select x mask */
#define SYS_SMCSSMM_CSxM	__REGBITS(31, 12)

/* System Memory Chip Select x Static Memory Mask: Chip select x enable */
#define SYS_SMCSSMM_CSEx	__REGBIT(0)
#define SYS_SMCSSMM_CSEx_DIS		__REGVAL(SYS_SMCSSMM_CSEx, 0)
#define SYS_SMCSSMM_CSEx_EN		__REGVAL(SYS_SMCSSMM_CSEx, 1)

/* General purpose, user-defined ID register */
#define SYS_GENID	__REG(0xa0900210)

/* External Interrupt x Control register */
#define SYS_EIC(x)	__REG2(0xa0900214, (x))

/* External Interrupt x Control register: Status */
#define SYS_EIC_STS		__REGBIT(3)

/* External Interrupt x Control register: Clear */
#define SYS_EIC_CLR		__REGBIT(2)

/* External Interrupt x Control register: Polarity */
#define SYS_EIC_PLTY		__REGBIT(1)
#define SYS_EIC_PLTY_AH			__REGVAL(SYS_EIC_PLTY, 0)
#define SYS_EIC_PLTY_AL			__REGVAL(SYS_EIC_PLTY, 1)

/* External Interrupt x Control register: Level edge */
#define SYS_EIC_LVEDG		__REGBIT(0)
#define SYS_EIC_LVEDG_LEVEL		__REGVAL(SYS_EIC_LVEDG, 0)
#define SYS_EIC_LVEDG_EDGE		__REGVAL(SYS_EIC_LVEDG, 1)

#endif /* ifndef __ASM_ARCH_REGSSYSNS9360_H */
