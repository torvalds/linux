/*
 * include/asm-arm/arch-ns9xxx/regs-bbu.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_REGSBBU_H
#define __ASM_ARCH_REGSBBU_H

#include <asm/hardware.h>

/* BBus Utility */

/* GPIO Configuration Registers block 1 */
/* NOTE: the HRM starts counting at 1 for the GPIO registers, here the start is
 * at 0 for each block.  That is, BBU_GCONFb1(0) is GPIO Configuration Register
 * #1, BBU_GCONFb2(0) is GPIO Configuration Register #8. */
#define BBU_GCONFb1(x)	__REG2(0x90600010, (x))
#define BBU_GCONFb2(x)	__REG2(0x90600100, (x))

#define BBU_GCONFx_DIR(m)	__REGBIT(3 + (((m) & 7) << 2))
#define BBU_GCONFx_DIR_INPUT(m)	__REGVAL(BBU_GCONFx_DIR(m), 0)
#define BBU_GCONFx_DIR_OUTPUT(m)	__REGVAL(BBU_GCONFx_DIR(m), 1)
#define BBU_GCONFx_INV(m)	__REGBIT(2 + (((m) & 7) << 2))
#define BBU_GCONFx_INV_NO(m)		__REGVAL(BBU_GCONFx_INV(m), 0)
#define BBU_GCONFx_INV_YES(m)		__REGVAL(BBU_GCONFx_INV(m), 1)
#define BBU_GCONFx_FUNC(m)	__REGBITS(1 + (((m) & 7) << 2), ((m) & 7) << 2)
#define BBU_GCONFx_FUNC_0(m)		__REGVAL(BBU_GCONFx_FUNC(m), 0)
#define BBU_GCONFx_FUNC_1(m)		__REGVAL(BBU_GCONFx_FUNC(m), 1)
#define BBU_GCONFx_FUNC_2(m)		__REGVAL(BBU_GCONFx_FUNC(m), 2)
#define BBU_GCONFx_FUNC_3(m)		__REGVAL(BBU_GCONFx_FUNC(m), 3)

#define BBU_GCTRL1	__REG(0x90600030)
#define BBU_GCTRL2	__REG(0x90600034)
#define BBU_GCTRL3	__REG(0x90600120)

#define BBU_GSTAT1	__REG(0x90600040)
#define BBU_GSTAT2	__REG(0x90600044)
#define BBU_GSTAT3	__REG(0x90600130)

#endif /* ifndef __ASM_ARCH_REGSBBU_H */
