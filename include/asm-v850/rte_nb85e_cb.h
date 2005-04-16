/*
 * include/asm-v850/rte_nb85e_cb.h -- Midas labs RTE-V850/NB85E-CB board
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

#ifndef __V850_RTE_NB85E_CB_H__
#define __V850_RTE_NB85E_CB_H__

#include <asm/rte_cb.h>		/* Common defs for Midas RTE-CB boards.  */


#define PLATFORM		"rte-v850e/nb85e-cb"
#define PLATFORM_LONG		"Midas lab RTE-V850E/NB85E-CB"

#define CPU_CLOCK_FREQ		50000000 /* 50MHz */

/* 1MB of onboard SRAM.  Note that the monitor ROM uses parts of this
   for its own purposes, so care must be taken.  */
#define SRAM_ADDR		0x03C00000
#define SRAM_SIZE		0x00100000 /* 1MB */

/* 16MB of onbard SDRAM.  */
#define SDRAM_ADDR		0x01000000
#define SDRAM_SIZE		0x01000000 /* 16MB */


/* CPU addresses of GBUS memory spaces.  */
#define GCS0_ADDR		0x00400000 /* GCS0 - Common SRAM (2MB) */
#define GCS0_SIZE		0x00400000 /*   4MB */
#define GCS1_ADDR		0x02000000 /* GCS1 - Flash ROM (8MB) */
#define GCS1_SIZE		0x00800000 /*   8MB */
#define GCS2_ADDR		0x03900000 /* GCS2 - I/O registers */
#define GCS2_SIZE		0x00080000 /*   512KB */
#define GCS3_ADDR		0x02800000 /* GCS3 - EXT-bus: memory space */
#define GCS3_SIZE		0x00800000 /*   8MB */
#define GCS4_ADDR		0x03A00000 /* GCS4 - EXT-bus: I/O space */
#define GCS4_SIZE		0x00200000 /*   2MB */
#define GCS5_ADDR		0x00800000 /* GCS5 - PCI bus space */
#define GCS5_SIZE		0x00800000 /*   8MB */
#define GCS6_ADDR		0x03980000 /* GCS6 - PCI control registers */
#define GCS6_SIZE		0x00010000 /*   64KB */


/* The GBUS GINT0 - GINT3 interrupts are connected to CPU interrupts 10-12.
   These are shared among the GBUS interrupts.  */
#define IRQ_GINT(n)		(10 + (n))
#define IRQ_GINT_NUM		3

/* Used by <asm/rte_cb.h> to derive NUM_MACH_IRQS.  */
#define NUM_RTE_CB_IRQS		NUM_CPU_IRQS


#ifdef CONFIG_ROM_KERNEL
/* Kernel is in ROM, starting at address 0.  */

#define INTV_BASE	0

#else /* !CONFIG_ROM_KERNEL */
/* We're using the ROM monitor.  */

/* The chip's real interrupt vectors are in ROM, but they jump to a
   secondary interrupt vector table in RAM.  */
#define INTV_BASE		0x03CF8000

/* Scratch memory used by the ROM monitor, which shouldn't be used by
   linux (except for the alternate interrupt vector area, defined
   above).  */
#define MON_SCRATCH_ADDR	0x03CE8000
#define MON_SCRATCH_SIZE	0x00018000 /* 96KB */

#endif /* CONFIG_ROM_KERNEL */


/* Some misc. on-board devices.  */

/* Seven-segment LED display (two digits).  Write-only.  */
#define LED_ADDR(n)	(0x03802000 + (n))
#define LED(n)		(*(volatile unsigned char *)LED_ADDR(n))
#define LED_NUM_DIGITS	4


/* Override the basic TEG UART pre-initialization so that we can
   initialize extra stuff.  */
#undef V850E_UART_PRE_CONFIGURE	/* should be defined by <asm/teg.h> */
#define V850E_UART_PRE_CONFIGURE	rte_nb85e_cb_uart_pre_configure
#ifndef __ASSEMBLY__
extern void rte_nb85e_cb_uart_pre_configure (unsigned chan,
					     unsigned cflags, unsigned baud);
#endif

/* This board supports RTS/CTS for the on-chip UART. */

/* CTS is pin P00.  */
#define V850E_UART_CTS(chan)	(! (TEG_PORT0_IO & 0x1))
/* RTS is pin P02.  */
#define V850E_UART_SET_RTS(chan, val)					      \
   do {									      \
	   unsigned old = TEG_PORT0_IO;					      \
	   TEG_PORT0_IO = val ? (old & ~0x4) : (old | 0x4);		      \
   } while (0)


#endif /* __V850_RTE_NB85E_CB_H__ */
