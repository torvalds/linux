/*
 * include/asm-v850/sim85e2.h -- Machine-dependent defs for
 *	V850E2 RTL simulator
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

#ifndef __V850_SIM85E2_H__
#define __V850_SIM85E2_H__


#include <asm/v850e2.h>		/* Based on V850E2 core.  */


/* Various memory areas supported by the simulator.
   These should match the corresponding definitions in the linker script.  */

/* `instruction RAM'; instruction fetches are much faster from IRAM than
   from DRAM.  */
#define IRAM_ADDR		0
#define IRAM_SIZE		0x00100000 /* 1MB */
/* `data RAM', below and contiguous with the I/O space.
   Data fetches are much faster from DRAM than from IRAM.  */
#define DRAM_ADDR		0xfff00000
#define DRAM_SIZE		0x000ff000 /* 1020KB */
/* `external ram'.  Unlike the above RAM areas, this memory is cached,
   so both instruction and data fetches should be (mostly) fast --
   however, currently only write-through caching is supported, so writes
   to ERAM will be slow.  */
#define ERAM_ADDR		0x00100000
#define ERAM_SIZE		0x07f00000 /* 127MB (max) */
/* Dynamic RAM; uses memory controller.  */
#define SDRAM_ADDR		0x10000000
#define SDRAM_SIZE		0x01000000 /* 16MB */


/* Simulator specific control registers.  */
/* NOTHAL controls whether the simulator will stop at a `halt' insn.  */
#define SIM85E2_NOTHAL_ADDR	0xffffff22
#define SIM85E2_NOTHAL		(*(volatile u8 *)SIM85E2_NOTHAL_ADDR)
/* The simulator will stop N cycles after N is written to SIMFIN.  */
#define SIM85E2_SIMFIN_ADDR	0xffffff24
#define SIM85E2_SIMFIN		(*(volatile u16 *)SIM85E2_SIMFIN_ADDR)


/* For <asm/irq.h> */
#define NUM_CPU_IRQS		64


/* For <asm/page.h> */
#define PAGE_OFFSET		SDRAM_ADDR


/* For <asm/entry.h> */
/* `R0 RAM', used for a few miscellaneous variables that must be accessible
   using a load instruction relative to R0.  The sim85e2 simulator
   actually puts 1020K of RAM from FFF00000 to FFFFF000, so we arbitarily
   choose a small portion at the end of that.  */
#define R0_RAM_ADDR		0xFFFFE000


/* For <asm/param.h> */
#ifndef HZ
#define HZ			24	/* Minimum supported frequency.  */
#endif


#endif /* __V850_SIM85E2_H__ */
