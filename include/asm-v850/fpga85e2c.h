/*
 * include/asm-v850/fpga85e2c.h -- Machine-dependent defs for
 *	FPGA implementation of V850E2/NA85E2C
 *
 *  Copyright (C) 2002,03  NEC Electronics Corporation
 *  Copyright (C) 2002,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_FPGA85E2C_H__
#define __V850_FPGA85E2C_H__

#include <asm/v850e2.h>
#include <asm/clinkage.h>


#define CPU_MODEL	"v850e2/fpga85e2c"
#define CPU_MODEL_LONG	"NEC V850E2/NA85E2C"
#define PLATFORM	"fpga85e2c"
#define PLATFORM_LONG	"NA85E2C FPGA implementation"


/* `external ram'.  */
#define ERAM_ADDR		0
#define ERAM_SIZE		0x00100000 /* 1MB */


/* FPGA specific control registers.  */

/* Writing a non-zero value to FLGREG(0) will signal the controlling CPU
   to stop execution.  */
#define FLGREG_ADDR(n)		(0xFFE80100 + 2*(n))
#define FLGREG(n)		(*(volatile unsigned char *)FLGREG_ADDR (n))
#define FLGREG_NUM		2

#define CSDEV_ADDR(n)		(0xFFE80110 + 2*(n))
#define CSDEV(n)		(*(volatile unsigned char *)CSDEV_ADDR (n))


/* Timer interrupts 0-3, interrupt at intervals from CLK/4096 to CLK/16384.  */
#define IRQ_RPU(n)		(60 + (n))
#define IRQ_RPU_NUM		4

/* For <asm/irq.h> */
#define NUM_CPU_IRQS		64


/* General-purpose timer.  */
/* control/status register (can only be read/written via bit insns) */
#define RPU_GTMC_ADDR		0xFFFFFB00
#define RPU_GTMC		(*(volatile unsigned char *)RPU_GTMC_ADDR)
#define RPU_GTMC_CE_BIT		7 /* clock enable (control) */
#define RPU_GTMC_OV_BIT		6 /* overflow (status) */
#define RPU_GTMC_CLK_BIT	1 /* 0 = .5 MHz CLK, 1 = 1 Mhz (control) */
/* 32-bit count (8 least-significant bits are always zero).  */
#define RPU_GTM_ADDR		0xFFFFFB28
#define RPU_GTM			(*(volatile unsigned long *)RPU_GTMC_ADDR)


/* For <asm/page.h> */
#define PAGE_OFFSET		ERAM_ADDR /* minimum allocatable address */


/* For <asm/entry.h> */
/* `R0 RAM', used for a few miscellaneous variables that must be accessible
   using a load instruction relative to R0.  The FPGA implementation
   actually has no on-chip RAM, so we use part of main ram just after the
   interrupt vectors.  */
#ifdef __ASSEMBLY__
#define R0_RAM_ADDR		lo(C_SYMBOL_NAME(_r0_ram))
#else
extern char _r0_ram;
#define R0_RAM_ADDR		((unsigned long)&_r0_ram);
#endif


#endif /* __V850_FPGA85E2C_H__ */
