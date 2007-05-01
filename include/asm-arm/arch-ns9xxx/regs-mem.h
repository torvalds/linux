/*
 * include/asm-arm/arch-ns9xxx/regs-mem.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_REGSMEM_H
#define __ASM_ARCH_REGSMEM_H

#include <asm/hardware.h>

/* Memory Module */

/* Control register */
#define MEM_CTRL	__REG(0xa0700000)

/* Status register */
#define MEM_STAT	__REG(0xa0700004)

/* Configuration register */
#define MEM_CONF	__REG(0xa0700008)

/* Dynamic Memory Control register */
#define MEM_DMCTRL	__REG(0xa0700020)

/* Dynamic Memory Refresh Timer */
#define MEM_DMRT	__REG(0xa0700024)

/* Dynamic Memory Read Configuration register */
#define MEM_DMRC	__REG(0xa0700028)

/* Dynamic Memory Precharge Command Period (tRP) */
#define MEM_DMPCP	__REG(0xa0700030)

/* Dynamic Memory Active to Precharge Command Period (tRAS) */
#define MEM_DMAPCP	__REG(0xa0700034)

/* Dynamic Memory Self-Refresh Exit Time (tSREX) */
#define MEM_DMSRET	__REG(0xa0700038)

/* Dynamic Memory Last Data Out to Active Time (tAPR) */
#define MEM_DMLDOAT	__REG(0xa070003c)

/* Dynamic Memory Data-in to Active Command Time (tDAL or TAPW) */
#define MEM_DMDIACT	__REG(0xa0700040)

/* Dynamic Memory Write Recovery Time (tWR, tDPL, tRWL, tRDL) */
#define MEM_DMWRT	__REG(0xa0700044)

/* Dynamic Memory Active to Active Command Period (tRC) */
#define MEM_DMAACP	__REG(0xa0700048)

/* Dynamic Memory Auto Refresh Period, and Auto Refresh to Active Command Period (tRFC) */
#define MEM_DMARP	__REG(0xa070004c)

/* Dynamic Memory Exit Self-Refresh to Active Command (tXSR) */
#define MEM_DMESRAC	__REG(0xa0700050)

/* Dynamic Memory Active Bank A to Active B Time (tRRD) */
#define MEM_DMABAABT	__REG(0xa0700054)

/* Dynamic Memory Load Mode register to Active Command Time (tMRD) */
#define MEM_DMLMACT	__REG(0xa0700058)

/* Static Memory Extended Wait */
#define MEM_SMEW	__REG(0xa0700080)

/* Dynamic Memory Configuration Register x */
#define MEM_DMCONF(x) 	__REG2(0xa0700100, (x) << 3)

/* Dynamic Memory RAS and CAS Delay x */
#define MEM_DMRCD(x)	__REG2(0xa0700104, (x) << 3)

/* Static Memory Configuration Register x */
#define MEM_SMC(x)	__REG2(0xa0700200, (x) << 3)

/* Static Memory Configuration Register x: Write protect */
#define MEM_SMC_WSMC		__REGBIT(20)
#define MEM_SMC_WSMC_OFF		__REGVAL(MEM_SMC_WSMC, 0)
#define MEM_SMC_WSMC_ON			__REGVAL(MEM_SMC_WSMC, 1)

/* Static Memory Configuration Register x: Buffer enable */
#define MEM_SMC_BSMC		__REGBIT(19)
#define MEM_SMC_BSMC_OFF		__REGVAL(MEM_SMC_BSMC, 0)
#define MEM_SMC_BSMC_ON			__REGVAL(MEM_SMC_BSMC, 1)

/* Static Memory Configuration Register x: Extended Wait */
#define MEM_SMC_EW		__REGBIT(8)
#define MEM_SMC_EW_OFF			__REGVAL(MEM_SMC_EW, 0)
#define MEM_SMC_EW_ON			__REGVAL(MEM_SMC_EW, 1)

/* Static Memory Configuration Register x: Byte lane state */
#define MEM_SMC_PB		__REGBIT(7)
#define MEM_SMC_PB_0			__REGVAL(MEM_SMC_PB, 0)
#define MEM_SMC_PB_1			__REGVAL(MEM_SMC_PB, 1)

/* Static Memory Configuration Register x: Chip select polarity */
#define MEM_SMC_PC		__REGBIT(6)
#define MEM_SMC_PC_AL			__REGVAL(MEM_SMC_PC, 0)
#define MEM_SMC_PC_AH			__REGVAL(MEM_SMC_PC, 1)

/* static memory configuration register x: page mode*/
#define MEM_SMC_PM		__REGBIT(3)
#define MEM_SMC_PM_DIS			__REGVAL(MEM_SMC_PM, 0)
#define MEM_SMC_PM_ASYNC		__REGVAL(MEM_SMC_PM, 1)

/* static memory configuration register x: Memory width */
#define MEM_SMC_MW		__REGBITS(1, 0)
#define MEM_SMC_MW_8			__REGVAL(MEM_SMC_MW, 0)
#define MEM_SMC_MW_16			__REGVAL(MEM_SMC_MW, 1)
#define MEM_SMC_MW_32			__REGVAL(MEM_SMC_MW, 2)

/* Static Memory Write Enable Delay x */
#define MEM_SMWED(x)	__REG2(0xa0700204, (x) << 3)

/* Static Memory Output Enable Delay x */
#define MEM_SMOED(x)	__REG2(0xa0700208, (x) << 3)

/* Static Memory Read Delay x */
#define MEM_SMRD(x)	__REG2(0xa070020c, (x) << 3)

/* Static Memory Page Mode Read Delay 0 */
#define MEM_SMPMRD(x)	__REG2(0xa0700210, (x) << 3)

/* Static Memory Write Delay */
#define MEM_SMWD(x)	__REG2(0xa0700214, (x) << 3)

/* Static Memory Turn Round Delay x */
#define MEM_SWT(x)	__REG2(0xa0700218, (x) << 3)

#endif /* ifndef __ASM_ARCH_REGSMEM_H */
