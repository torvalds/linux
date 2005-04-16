/*
 * include/asm-v850/rte_ma1_cb.h -- Midas labs RTE-V850/MA1-CB board
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_RTE_MA1_CB_H__
#define __V850_RTE_MA1_CB_H__

#include <asm/rte_cb.h>		/* Common defs for Midas RTE-CB boards.  */


#define PLATFORM		"rte-v850e/ma1-cb"
#define PLATFORM_LONG		"Midas lab RTE-V850E/MA1-CB"

#define CPU_CLOCK_FREQ		50000000 /* 50MHz */

/* 1MB of onboard SRAM.  Note that the monitor ROM uses parts of this
   for its own purposes, so care must be taken.  Some address lines are
   not decoded, so the SRAM area is mirrored every 1MB from 0x400000 to
   0x800000 (exclusive).  */
#define SRAM_ADDR		0x00400000
#define SRAM_SIZE		0x00100000 /* 1MB */

/* 32MB of onbard SDRAM.  */
#define SDRAM_ADDR		0x00800000
#define SDRAM_SIZE		0x02000000 /* 32MB */


/* CPU addresses of GBUS memory spaces.  */
#define GCS0_ADDR		0x05000000 /* GCS0 - Common SRAM (2MB) */
#define GCS0_SIZE		0x00200000 /*   2MB */
#define GCS1_ADDR		0x06000000 /* GCS1 - Flash ROM (8MB) */
#define GCS1_SIZE		0x00800000 /*   8MB */
#define GCS2_ADDR		0x07900000 /* GCS2 - I/O registers */
#define GCS2_SIZE		0x00400000 /*   4MB */
#define GCS5_ADDR		0x04000000 /* GCS5 - PCI bus space */
#define GCS5_SIZE		0x01000000 /*   16MB */
#define GCS6_ADDR		0x07980000 /* GCS6 - PCI control registers */
#define GCS6_SIZE		0x00000200 /*   512B */


/* For <asm/page.h> */
#define PAGE_OFFSET 		SRAM_ADDR


/* The GBUS GINT0 - GINT3 interrupts are connected to the INTP000 - INTP011
   pins on the CPU.  These are shared among the GBUS interrupts.  */
#define IRQ_GINT(n)		IRQ_INTP(n)
#define IRQ_GINT_NUM		4

/* Used by <asm/rte_cb.h> to derive NUM_MACH_IRQS.  */
#define NUM_RTE_CB_IRQS		NUM_CPU_IRQS


#ifdef CONFIG_ROM_KERNEL
/* Kernel is in ROM, starting at address 0.  */

#define INTV_BASE		0

#else /* !CONFIG_ROM_KERNEL */

#ifdef CONFIG_RTE_CB_MULTI
/* Using RAM kernel with ROM monitor for Multi debugger.  */

/* The chip's real interrupt vectors are in ROM, but they jump to a
   secondary interrupt vector table in RAM.  */
#define INTV_BASE		0x004F8000

/* Scratch memory used by the ROM monitor, which shouldn't be used by
   linux (except for the alternate interrupt vector area, defined
   above).  */
#define MON_SCRATCH_ADDR	0x004F8000
#define MON_SCRATCH_SIZE	0x00008000 /* 32KB */

#else /* !CONFIG_RTE_CB_MULTI */
/* Using RAM-kernel.  Assume some sort of boot-loader got us loaded at
   address 0.  */

#define INTV_BASE		0

#endif /* CONFIG_RTE_CB_MULTI */

#endif /* CONFIG_ROM_KERNEL */


/* Some misc. on-board devices.  */

/* Seven-segment LED display (two digits).  Write-only.  */
#define LED_ADDR(n)		(0x07802000 + (n))
#define LED(n)			(*(volatile unsigned char *)LED_ADDR(n))
#define LED_NUM_DIGITS		2


/* Override the basic MA uart pre-initialization so that we can
   initialize extra stuff.  */
#undef V850E_UART_PRE_CONFIGURE	/* should be defined by <asm/ma.h> */
#define V850E_UART_PRE_CONFIGURE	rte_ma1_cb_uart_pre_configure
#ifndef __ASSEMBLY__
extern void rte_ma1_cb_uart_pre_configure (unsigned chan,
					   unsigned cflags, unsigned baud);
#endif

/* This board supports RTS/CTS for the on-chip UART, but only for channel 0. */

/* CTS for UART channel 0 is pin P43 (bit 3 of port 4).  */
#define V850E_UART_CTS(chan)	((chan) == 0 ? !(MA_PORT4_IO & 0x8) : 1)
/* RTS for UART channel 0 is pin P42 (bit 2 of port 4).  */
#define V850E_UART_SET_RTS(chan, val)					      \
   do {									      \
	   if (chan == 0) {						      \
		   unsigned old = MA_PORT4_IO; 				      \
		   if (val)						      \
			   MA_PORT4_IO = old & ~0x4;			      \
		   else							      \
			   MA_PORT4_IO = old | 0x4;			      \
	   }								      \
   } while (0)


#endif /* __V850_RTE_MA1_CB_H__ */
