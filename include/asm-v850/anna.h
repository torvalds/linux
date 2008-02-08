/*
 * include/asm-v850/anna.h -- Anna V850E2 evaluation cpu chip/board
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

#ifndef __V850_ANNA_H__
#define __V850_ANNA_H__

#include <asm/v850e2.h>		/* Based on V850E2 core.  */


#define CPU_MODEL	"v850e2/anna"
#define CPU_MODEL_LONG	"NEC V850E2/Anna"
#define PLATFORM	"anna"
#define PLATFORM_LONG	"NEC/Midas lab V850E2/Anna evaluation board"

#define CPU_CLOCK_FREQ	200000000 /*  200MHz */
#define SYS_CLOCK_FREQ	 33300000 /* 33.3MHz */


/* 1MB of static RAM.  This memory is mirrored 64 times.  */
#define SRAM_ADDR	0x04000000
#define SRAM_SIZE	0x00100000 /* 1MB */
/* 64MB of DRAM.  */
#define SDRAM_ADDR	0x08000000	
#define SDRAM_SIZE	0x04000000 /* 64MB */


/* For <asm/page.h> */
#define PAGE_OFFSET 	SRAM_ADDR

/* We use on-chip RAM, for a few miscellaneous variables that must be
   accessible using a load instruction relative to R0.  The Anna chip has
   128K of `dLB' ram nominally located at 0xFFF00000, but it's mirrored
   every 128K, so we can use the `last mirror' (except for the portion at
   the top which is overridden by I/O space).  In addition, the early
   sample chip we're using has lots of memory errors in the dLB ram, so we
   use a specially chosen location that has at least 20 bytes of contiguous
   valid memory (xxxF0020 - xxxF003F).  */
#define R0_RAM_ADDR			0xFFFF8020


/* Anna specific control registers.  */
#define ANNA_ILBEN_ADDR			0xFFFFF7F2
#define ANNA_ILBEN			(*(volatile u16 *)ANNA_ILBEN_ADDR)


/* I/O port P0-P3. */
/* Direct I/O.  Bits 0-7 are pins Pn0-Pn7.  */
#define ANNA_PORT_IO_ADDR(n)		(0xFFFFF400 + (n) * 2)
#define ANNA_PORT_IO(n)			(*(volatile u8 *)ANNA_PORT_IO_ADDR(n))
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define ANNA_PORT_PM_ADDR(n)		(0xFFFFF410 + (n) * 2)
#define ANNA_PORT_PM(n)			(*(volatile u8 *)ANNA_PORT_PM_ADDR(n))


/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).  */
#define IRQ_INTP(n)	(n)	/* Pnnn (pin) interrupts 0-15 */
#define IRQ_INTP_NUM	16
#define IRQ_INTOV(n)	(0x10 + (n)) /* 0-2 */
#define IRQ_INTOV_NUM	2
#define IRQ_INTCCC(n)	(0x12 + (n))
#define IRQ_INTCCC_NUM	4
#define IRQ_INTCMD(n)	(0x16 + (n)) /* interval timer interrupts 0-5 */
#define IRQ_INTCMD_NUM	6
#define IRQ_INTDMA(n)	(0x1C + (n)) /* DMA interrupts 0-3 */
#define IRQ_INTDMA_NUM	4
#define IRQ_INTDMXER	0x20
#define IRQ_INTSRE(n)	(0x21 + (n)*3) /* UART 0-1 reception error */
#define IRQ_INTSRE_NUM	2
#define IRQ_INTSR(n)	(0x22 + (n)*3) /* UART 0-1 reception completion */
#define IRQ_INTSR_NUM	2
#define IRQ_INTST(n)	(0x23 + (n)*3) /* UART 0-1 transmission completion */
#define IRQ_INTST_NUM	2

#define NUM_CPU_IRQS	64

#ifndef __ASSEMBLY__
/* Initialize chip interrupts.  */
extern void anna_init_irqs (void);
#endif


/* Anna UART details (basically the same as the V850E/MA1, but 2 channels).  */
#define V850E_UART_NUM_CHANNELS		2
#define V850E_UART_BASE_FREQ		(SYS_CLOCK_FREQ / 2)
#define V850E_UART_CHIP_NAME 		"V850E2/NA85E2A"

/* This is the UART channel that's actually connected on the board.  */
#define V850E_UART_CONSOLE_CHANNEL	1

/* This is a function that gets called before configuring the UART.  */
#define V850E_UART_PRE_CONFIGURE	anna_uart_pre_configure
#ifndef __ASSEMBLY__
extern void anna_uart_pre_configure (unsigned chan,
				     unsigned cflags, unsigned baud);
#endif

/* This board supports RTS/CTS for the on-chip UART, but only for channel 1. */

/* CTS for UART channel 1 is pin P37 (bit 7 of port 3).  */
#define V850E_UART_CTS(chan)	((chan) == 1 ? !(ANNA_PORT_IO(3) & 0x80) : 1)
/* RTS for UART channel 1 is pin P07 (bit 7 of port 0).  */
#define V850E_UART_SET_RTS(chan, val)					      \
   do {									      \
	   if (chan == 1) {						      \
		   unsigned old = ANNA_PORT_IO(0); 			      \
		   if (val)						      \
			   ANNA_PORT_IO(0) = old & ~0x80;		      \
		   else							      \
			   ANNA_PORT_IO(0) = old | 0x80;		      \
	   }								      \
   } while (0)


/* Timer C details.  */
#define V850E_TIMER_C_BASE_ADDR		0xFFFFF600

/* Timer D details (the Anna actually has 5 of these; should change later). */
#define V850E_TIMER_D_BASE_ADDR		0xFFFFF540
#define V850E_TIMER_D_TMD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x0)
#define V850E_TIMER_D_CMD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x2)
#define V850E_TIMER_D_TMCD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x4)

#define V850E_TIMER_D_BASE_FREQ		SYS_CLOCK_FREQ
#define V850E_TIMER_D_TMCD_CS_MIN	1 /* min 2^1 divider */


#endif /* __V850_ANNA_H__ */
